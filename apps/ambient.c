/***************************************************************************
 * Ambient audio layer: a second MP3 stream mixed on top of normal playback.
 *
 * A dedicated thread decodes MP3 files with libmad into a ring of PCM
 * frames which are handed to their own PCM mixer channel, in the same way
 * the voice prompt thread does.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "thread.h"
#include "file.h"
#include "dir.h"
#include "pathfuncs.h"
#include "string-extra.h"
#include "audio.h"
#include "pcm.h"
#include "pcm_mixer.h"
#include "settings.h"
#include "ambient.h"
#include "codecs/libmad/mad.h"

/* Number of PCM frames queued towards the mixer and their length in
 * stereo samples */
#define AMB_FRAMES          8
#define AMB_FRAME_SAMPLES   1024

#define AMB_INBUF_SIZE      8192
#define AMB_MAX_FILES       200
#define AMB_NAME_POOL       12288

enum ambient_messages
{
    Q_AMBIENT_PLAY = 1,
    Q_AMBIENT_STOP,
};

/* Static buffers: no buflib allocation at run time, so starting the ambient
 * layer can never disturb the playback buffer. */
static struct mad_stream amb_stream;
static struct mad_frame amb_frame;
static struct mad_synth amb_synth;
static mad_fixed_t amb_sbsample[2][36][32] MEM_ALIGN_ATTR;
static mad_fixed_t amb_overlap[2][32][18] MEM_ALIGN_ATTR;
static unsigned char amb_main_data[MAD_BUFFER_MDLEN] MEM_ALIGN_ATTR;
static unsigned char amb_inbuf[AMB_INBUF_SIZE + MAD_BUFFER_GUARD];

static struct
{
    int16_t pcm[2 * AMB_FRAME_SAMPLES];
} amb_frames[AMB_FRAMES];

static unsigned int volatile amb_frame_in;
static unsigned int volatile amb_frame_out;

/* Thread */
static unsigned int amb_thread_id = 0;
static long amb_stack[(DEFAULT_STACK_SIZE + 0x4000) / sizeof(long)];
static const char amb_thread_name[] = "ambient";
static struct event_queue amb_queue SHAREDBSS_ATTR;

/* State owned by the ambient thread */
static bool amb_active;          /* a track list is loaded and playing */
static bool amb_main_seen;       /* normal playback has been seen running */
static bool amb_paused;
static int  amb_fd = -1;
static bool amb_eof;
static bool amb_interrupted;
static long amb_next_poll;

static char amb_req_path[MAX_PATH];
static char amb_dir[MAX_PATH];
static const char *amb_names[AMB_MAX_FILES];
static char amb_pool[AMB_NAME_POOL];
static int amb_nfiles;
static short amb_order[AMB_MAX_FILES];
static int amb_pos;               /* index into amb_order */

/* Output frame being filled */
static int16_t *amb_cur_pcm;
static int amb_cur_n;

/* Resampler: linear interpolation to the mixer rate */
static uint32_t amb_phase;
static int32_t amb_prev_l, amb_prev_r;
static unsigned int amb_src_rate;

static int amb_volume = 40;

/* ---- PCM ring (same scheme as the voice thread) ---------------------- */

static unsigned int amb_unplayed(void)
{
    return amb_frame_in - amb_frame_out;
}

static void amb_pcm_callback(const void **start, size_t *size)
{
    unsigned int out = ++amb_frame_out;

    if (amb_unplayed() == 0)
        return; /* nothing more: channel stops until restarted */

    *start = amb_frames[out % AMB_FRAMES].pcm;
    *size = AMB_FRAME_SAMPLES * 2 * sizeof (int16_t);
}

static void amb_apply_volume(void)
{
    unsigned int amp = (unsigned int)amb_volume * MIX_AMP_UNITY / 100;
    mixer_channel_set_amplitude(PCM_MIXER_CHAN_AMBIENT, amp);
}

static void amb_start_playback(void)
{
    if (amb_paused ||
        mixer_channel_status(PCM_MIXER_CHAN_AMBIENT) != CHANNEL_STOPPED ||
        amb_unplayed() == 0)
        return;

    static const struct mixer_play_cbs cbs = {
        .get_more = amb_pcm_callback,
    };
    amb_apply_volume();
    mixer_channel_play_data(PCM_MIXER_CHAN_AMBIENT, &cbs,
                            amb_frames[amb_frame_out % AMB_FRAMES].pcm,
                            AMB_FRAME_SAMPLES * 2 * sizeof (int16_t));
}

static void amb_stop_playback(void)
{
    mixer_channel_stop(PCM_MIXER_CHAN_AMBIENT);
    amb_frame_in = amb_frame_out = 0;
    amb_cur_pcm = NULL;
    amb_cur_n = 0;
}

/* Returns a free frame or NULL if a message arrived while waiting */
static int16_t *amb_get_free_frame(void)
{
    while (amb_unplayed() >= AMB_FRAMES)
    {
        amb_start_playback();
        if (!queue_empty(&amb_queue))
        {
            amb_interrupted = true;
            return NULL;
        }
        sleep(HZ / 50);
    }
    return amb_frames[amb_frame_in % AMB_FRAMES].pcm;
}

static void amb_emit(int16_t l, int16_t r)
{
    if (amb_interrupted)
        return;

    if (amb_cur_pcm == NULL)
    {
        amb_cur_pcm = amb_get_free_frame();
        if (amb_cur_pcm == NULL)
            return;
        amb_cur_n = 0;
    }

    amb_cur_pcm[2 * amb_cur_n]     = l;
    amb_cur_pcm[2 * amb_cur_n + 1] = r;

    if (++amb_cur_n >= AMB_FRAME_SAMPLES)
    {
        amb_frame_in++;
        amb_cur_pcm = NULL;
        amb_cur_n = 0;
        /* start once a couple of frames are ready */
        if (amb_unplayed() >= 2)
            amb_start_playback();
    }
}

/* ---- Decoding ---------------------------------------------------------- */

static inline int16_t amb_scale(mad_fixed_t s)
{
    /* MAD_F_FRACBITS = 28, 16 bit output has 15 fraction bits */
    s += 1L << (MAD_F_FRACBITS - 16);
    s >>= (MAD_F_FRACBITS - 15);
    if (s > 32767)
        s = 32767;
    else if (s < -32768)
        s = -32768;
    return (int16_t)s;
}

static void amb_init_mad(void)
{
    memset(&amb_stream, 0, sizeof (amb_stream));
    memset(&amb_frame, 0, sizeof (amb_frame));
    memset(&amb_synth, 0, sizeof (amb_synth));

    amb_frame.sbsample_prev = &amb_sbsample;
    amb_frame.sbsample      = &amb_sbsample;
    amb_frame.overlap       = &amb_overlap;
    amb_stream.main_data    = &amb_main_data;

    mad_stream_init(&amb_stream);
    mad_frame_init(&amb_frame);
    mad_synth_init(&amb_synth);
}

static void amb_close_track(void)
{
    if (amb_fd >= 0)
    {
        close(amb_fd);
        amb_fd = -1;
    }
}

static bool amb_open_track(const char *path)
{
    unsigned char hdr[10];

    amb_close_track();
    amb_fd = open(path, O_RDONLY);
    if (amb_fd < 0)
        return false;

    /* skip an ID3v2 tag so embedded pictures are not parsed as audio */
    if (read(amb_fd, hdr, sizeof (hdr)) == (ssize_t)sizeof (hdr) &&
        !memcmp(hdr, "ID3", 3))
    {
        off_t size = ((hdr[6] & 0x7f) << 21) | ((hdr[7] & 0x7f) << 14) |
                     ((hdr[8] & 0x7f) << 7)  |  (hdr[9] & 0x7f);
        size += 10;
        if (hdr[5] & 0x10)
            size += 10; /* footer */
        lseek(amb_fd, size, SEEK_SET);
    }
    else
        lseek(amb_fd, 0, SEEK_SET);

    amb_init_mad();
    amb_eof = false;
    amb_phase = 0;
    amb_prev_l = amb_prev_r = 0;
    amb_src_rate = 0;
    return true;
}

/* Decode one MP3 frame and emit resampled stereo PCM.
 * Returns false at the end of the track (or on a fatal error). */
static bool amb_decode_step(void)
{
    if (amb_stream.buffer == NULL || amb_stream.error == MAD_ERROR_BUFLEN)
    {
        size_t remaining = 0;

        if (amb_stream.next_frame != NULL)
        {
            remaining = amb_stream.bufend - amb_stream.next_frame;
            memmove(amb_inbuf, amb_stream.next_frame, remaining);
        }

        if (amb_eof)
            return false;

        ssize_t n = read(amb_fd, amb_inbuf + remaining,
                         AMB_INBUF_SIZE - remaining);
        if (n <= 0)
        {
            amb_eof = true;
            /* guard bytes let libmad finish the last frame */
            memset(amb_inbuf + remaining, 0, MAD_BUFFER_GUARD);
            n = MAD_BUFFER_GUARD;
        }
        mad_stream_buffer(&amb_stream, amb_inbuf, remaining + n);
        amb_stream.error = 0;
    }

    if (mad_frame_decode(&amb_frame, &amb_stream))
    {
        if (MAD_RECOVERABLE(amb_stream.error) ||
            amb_stream.error == MAD_ERROR_BUFLEN)
            return true; /* resync or refill on the next call */
        return false;
    }

    mad_synth_frame(&amb_synth, &amb_frame);

    unsigned int src = amb_synth.pcm.samplerate;
    unsigned int dst = mixer_get_frequency();
    if (src == 0 || dst == 0)
        return true;

    if (src != amb_src_rate)
    {
        amb_src_rate = src;
        amb_phase = 0;
    }
    uint32_t step = ((uint32_t)src << 16) / dst;
    bool stereo = amb_synth.pcm.channels == 2;

    for (unsigned int i = 0; i < amb_synth.pcm.length; i++)
    {
        int32_t l = amb_scale(amb_synth.pcm.samples[0][i]);
        int32_t r = stereo ? amb_scale(amb_synth.pcm.samples[1][i]) : l;

        while (amb_phase < 65536)
        {
            int32_t ol = amb_prev_l + (((l - amb_prev_l) * (int32_t)amb_phase) >> 16);
            int32_t or_ = amb_prev_r + (((r - amb_prev_r) * (int32_t)amb_phase) >> 16);
            amb_emit((int16_t)ol, (int16_t)or_);
            amb_phase += step;
        }
        amb_phase -= 65536;
        amb_prev_l = l;
        amb_prev_r = r;

        if (amb_interrupted)
            return true;
    }
    return true;
}

/* ---- Track list --------------------------------------------------------- */

static bool amb_is_mp3(const char *name)
{
    size_t len = strlen(name);
    return len > 4 && !strcasecmp(name + len - 4, ".mp3");
}

static int amb_name_cmp(const void *a, const void *b)
{
    return strcasecmp(*(const char * const *)a, *(const char * const *)b);
}

/* Fills amb_names/amb_nfiles from a folder. Returns false if unreadable. */
static bool amb_scan_dir(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;

    size_t used = 0;
    struct dirent *e;
    amb_nfiles = 0;

    while ((e = readdir(d)) != NULL && amb_nfiles < AMB_MAX_FILES)
    {
        size_t len = strlen(e->d_name) + 1;
        if (!amb_is_mp3(e->d_name) || used + len > sizeof (amb_pool))
            continue;
        memcpy(amb_pool + used, e->d_name, len);
        amb_names[amb_nfiles++] = amb_pool + used;
        used += len;
    }
    closedir(d);

    qsort(amb_names, amb_nfiles, sizeof (amb_names[0]), amb_name_cmp);
    return amb_nfiles > 0;
}

static void amb_shuffle_order(void)
{
    for (int i = amb_nfiles - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        short t = amb_order[i];
        amb_order[i] = amb_order[j];
        amb_order[j] = t;
    }
}

static bool amb_open_current(void)
{
    char path[MAX_PATH];
    const char *name = amb_names[amb_order[amb_pos]];

    if (amb_dir[0] == '/' && amb_dir[1] == '\0')
        snprintf(path, sizeof (path), "/%s", name);
    else
        snprintf(path, sizeof (path), "%s/%s", amb_dir, name);
    return amb_open_track(path);
}

/* Handle a play request: build the track list and open the first track */
static bool amb_load_request(void)
{
    char *start_name = NULL;
    DIR *d = opendir(amb_req_path);

    if (d)
    {
        closedir(d);
        strlcpy(amb_dir, amb_req_path, sizeof (amb_dir));
    }
    else
    {
        /* a file: use its folder, starting at that file */
        char *slash = strrchr(amb_req_path, '/');
        if (!slash)
            return false;
        start_name = slash + 1;
        if (slash == amb_req_path)
            strlcpy(amb_dir, "/", sizeof (amb_dir));
        else
        {
            *slash = '\0';
            strlcpy(amb_dir, amb_req_path, sizeof (amb_dir));
        }
    }

    if (!amb_scan_dir(amb_dir))
        return false;

    for (int i = 0; i < amb_nfiles; i++)
        amb_order[i] = i;

    amb_pos = 0;
    if (start_name)
    {
        for (int i = 0; i < amb_nfiles; i++)
            if (!strcasecmp(amb_names[i], start_name))
                amb_pos = i;
    }

    if (global_settings.ambient_mode == AMBIENT_MODE_SHUFFLE)
    {
        amb_shuffle_order();
        amb_pos = 0;
    }

    return amb_open_current();
}

/* Move on when a track ends. Returns false if nothing can be played. */
static bool amb_next_track(void)
{
    switch (global_settings.ambient_mode)
    {
    case AMBIENT_MODE_FOLDER:
        amb_pos = (amb_pos + 1) % amb_nfiles;
        break;
    case AMBIENT_MODE_SHUFFLE:
        if (++amb_pos >= amb_nfiles)
        {
            amb_pos = 0;
            amb_shuffle_order();
        }
        break;
    case AMBIENT_MODE_LOOP:
    default:
        break; /* same track again */
    }
    return amb_open_current();
}

/* ---- Thread ------------------------------------------------------------- */

static void amb_halt(void)
{
    amb_active = false;
    amb_main_seen = false;
    amb_paused = false;
    amb_interrupted = false;
    amb_close_track();
    amb_stop_playback();
}

/* Follow the normal player: pause with it, stop when it stops */
static void amb_follow_main(void)
{
    if (!TIME_AFTER(current_tick, amb_next_poll))
        return;
    amb_next_poll = current_tick + HZ / 5;

    int status = audio_status();
    if (status & AUDIO_STATUS_PLAY)
    {
        amb_main_seen = true;
        bool main_paused = (status & AUDIO_STATUS_PAUSE) != 0;
        if (main_paused && !amb_paused)
        {
            amb_paused = true;
            mixer_channel_play_pause(PCM_MIXER_CHAN_AMBIENT, false);
        }
        else if (!main_paused && amb_paused)
        {
            amb_paused = false;
            if (mixer_channel_status(PCM_MIXER_CHAN_AMBIENT) == CHANNEL_PAUSED)
                mixer_channel_play_pause(PCM_MIXER_CHAN_AMBIENT, true);
            else
                amb_start_playback();
        }
    }
    else if (amb_main_seen)
    {
        amb_halt(); /* the player was stopped */
    }
}

static void amb_thread(void)
{
    struct queue_event ev;

    while (1)
    {
        queue_wait_w_tmo(&amb_queue, &ev,
                         amb_active ? TIMEOUT_NOBLOCK : TIMEOUT_BLOCK);

        if (ev.id == Q_AMBIENT_STOP)
        {
            amb_halt();
            continue;
        }
        if (ev.id == Q_AMBIENT_PLAY)
        {
            amb_halt();
            amb_main_seen = (audio_status() & AUDIO_STATUS_PLAY) != 0;
            amb_next_poll = current_tick + HZ / 5;
            amb_active = amb_load_request();
            if (!amb_active)
                amb_halt();
            continue;
        }

        if (!amb_active)
            continue;

        amb_interrupted = false;
        amb_follow_main();
        if (!amb_active)
            continue;

        if (amb_paused)
        {
            sleep(HZ / 20);
            continue;
        }

        if (!amb_decode_step())
        {
            if (!amb_next_track())
                amb_halt();
        }
        else
            yield();
    }
}

/* ---- Public interface --------------------------------------------------- */

void ambient_init(void)
{
    if (amb_thread_id != 0)
        return;

    amb_volume = global_settings.ambient_volume;
    queue_init(&amb_queue, false);
    amb_thread_id = create_thread(amb_thread, amb_stack, sizeof (amb_stack), 0,
                                  amb_thread_name
                                  IF_PRIO(, PRIORITY_PLAYBACK) IF_COP(, CPU));
}

bool ambient_start(const char *path)
{
    if (amb_thread_id == 0 || !path || !*path)
        return false;

    strlcpy(amb_req_path, path, sizeof (amb_req_path));
    queue_post(&amb_queue, Q_AMBIENT_PLAY, 0);
    return true;
}

void ambient_stop(void)
{
    if (amb_thread_id != 0)
        queue_post(&amb_queue, Q_AMBIENT_STOP, 0);
}

bool ambient_is_active(void)
{
    return amb_active;
}

void ambient_set_volume(int percent)
{
    if (percent < 0)
        percent = 0;
    else if (percent > 100)
        percent = 100;
    amb_volume = percent;
    if (amb_thread_id != 0)
        amb_apply_volume();
}
