/***************************************************************************
 * Video page <-> mpegplayer: the small files they share (Zen X-Fi).
 *
 * /.rockbox/video_pos.bin   one 16-byte record per video that was played:
 *                           hash of its full path, position, length (both
 *                           in seconds) and a "last played" counter. The
 *                           plugin updates its record in place; the Video
 *                           page reads all of them.
 * /.rockbox/video_last.txt  full path of the video played last (one line).
 * /.rockbox/video_launch.txt written by the Video page before it starts the
 *                           plugin:
 *                             line 1  path of the video to play
 *                             line 2  start position in seconds
 *                             line 3  1 = start the next file when this one
 *                                     ends (series folders), else 0
 *                             line 4  page colours as hex: fg bg accent
 *                                     sel_bg hdr1 hdr_fg
 *                             line 5+ the files after this one in the
 *                                     folder, in list order (for "next")
 *
 * This header is plain C (no Rockbox includes) so that both the firmware
 * and the plugin can include it.
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
#ifndef VIDEO_POS_H
#define VIDEO_POS_H

#define VIDEO_POS_FILE      "/.rockbox/video_pos.bin"
#define VIDEO_LAST_FILE     "/.rockbox/video_last.txt"
#define VIDEO_LAUNCH_FILE   "/.rockbox/video_launch.txt"
#define VIDEO_POS_MAX       200     /* records kept; the oldest is replaced */
#define VIDEO_NEXT_MAX      40      /* files after the current one */

/* played at least this long: counts as started */
#define VIDEO_STARTED_SECS  10

struct video_posrec
{
    unsigned int hash;      /* video_path_hash() of the full path */
    unsigned int pos_s;     /* where it stopped; = dur_s when it ended */
    unsigned int dur_s;     /* length, 0 when unknown */
    unsigned int stamp;     /* bigger = played later */
};

static inline unsigned int video_path_hash(const char *s)
{
    unsigned int h = 2166136261u;           /* FNV-1a */
    while (*s)
    {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h ? h : 1;
}

/* watched: played to (nearly) the end */
static inline int video_rec_watched(const struct video_posrec *r)
{
    return r->dur_s > 0 && r->pos_s * 100u >= r->dur_s * 95u;
}

/* started but not finished */
static inline int video_rec_partial(const struct video_posrec *r)
{
    return !video_rec_watched(r) && r->pos_s >= VIDEO_STARTED_SECS;
}

#endif /* VIDEO_POS_H */
