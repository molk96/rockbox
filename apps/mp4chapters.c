/***************************************************************************
 * Chapter list reader for .m4b / .m4a / .mp4 audiobooks, see mp4chapters.h.
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
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "file.h"
#include "mp4chapters.h"

#define FOURCC(a, b, c, d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (d))

#define A_moov FOURCC('m', 'o', 'o', 'v')
#define A_udta FOURCC('u', 'd', 't', 'a')
#define A_chpl FOURCC('c', 'h', 'p', 'l')
#define A_trak FOURCC('t', 'r', 'a', 'k')
#define A_tkhd FOURCC('t', 'k', 'h', 'd')
#define A_tref FOURCC('t', 'r', 'e', 'f')
#define A_chap FOURCC('c', 'h', 'a', 'p')
#define A_mdia FOURCC('m', 'd', 'i', 'a')
#define A_mdhd FOURCC('m', 'd', 'h', 'd')
#define A_hdlr FOURCC('h', 'd', 'l', 'r')
#define A_minf FOURCC('m', 'i', 'n', 'f')
#define A_stbl FOURCC('s', 't', 'b', 'l')
#define A_stts FOURCC('s', 't', 't', 's')
#define A_stsz FOURCC('s', 't', 's', 'z')
#define A_stsc FOURCC('s', 't', 's', 'c')
#define A_stco FOURCC('s', 't', 'c', 'o')
#define A_co64 FOURCC('c', 'o', '6', '4')
#define A_text FOURCC('t', 'e', 'x', 't')
#define A_sbtl FOURCC('s', 'b', 't', 'l')

#define MAX_TRACKS 8

/* a track as far as chapters are concerned */
struct trk
{
    uint32_t id;
    uint32_t handler;          /* hdlr subtype: 'soun', 'text', ... */
    uint32_t chap_ref;         /* track id this track names as its chapters */
    uint32_t timescale;
    off_t stts, stsz, stsc, stco;   /* payload positions, 0 = missing */
    bool co64;
};

static int ch_fd;

static bool rd(void *buf, size_t n)
{
    return read(ch_fd, buf, n) == (ssize_t)n;
}

static bool rd_u8(uint32_t *v)
{
    uint8_t b;
    if (!rd(&b, 1))
        return false;
    *v = b;
    return true;
}

static bool rd_u16(uint32_t *v)
{
    uint8_t b[2];
    if (!rd(b, 2))
        return false;
    *v = (b[0] << 8) | b[1];
    return true;
}

static bool rd_u32(uint32_t *v)
{
    uint8_t b[4];
    if (!rd(b, 4))
        return false;
    *v = ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
    return true;
}

static bool rd_u64(uint64_t *v)
{
    uint32_t hi, lo;
    if (!rd_u32(&hi) || !rd_u32(&lo))
        return false;
    *v = ((uint64_t)hi << 32) | lo;
    return true;
}

/* Reads an atom header at the current position. *end is where the atom
 * ends, *payload where its content starts. */
static bool atom_header(off_t limit, uint32_t *type, off_t *payload,
                        off_t *end)
{
    off_t start = lseek(ch_fd, 0, SEEK_CUR);
    uint32_t size32;
    uint64_t size;

    if (start < 0 || start + 8 > limit)
        return false;
    if (!rd_u32(&size32) || !rd_u32(type))
        return false;
    size = size32;
    *payload = start + 8;
    if (size32 == 1)
    {
        if (!rd_u64(&size))
            return false;
        *payload = start + 16;
    }
    else if (size32 == 0)
        size = (uint64_t)(limit - start);
    if (size < (uint64_t)(*payload - start))
        return false;
    if ((uint64_t)(limit - start) < size)
        *end = limit; /* cut-off atom: trust the file end */
    else
        *end = start + (off_t)size;
    return true;
}

static void copy_title(char *dst, const uint8_t *src, size_t len)
{
    size_t n = len < MP4CH_TITLE_LEN - 1 ? len : MP4CH_TITLE_LEN - 1;
    /* do not cut a UTF-8 character in half */
    while (n > 0 && n < len && (src[n] & 0xC0) == 0x80)
        n--;
    memcpy(dst, src, n);
    dst[n] = '\0';
    /* a UTF-16 title starts with a byte order mark: keep it simple and
       drop anything that is not text we can show */
    for (size_t i = 0; i < n; i++)
        if ((uint8_t)dst[i] < 32)
            dst[i] = ' ';
}

/* ---- Nero chpl -------------------------------------------------------- */

static int read_chpl(off_t payload, off_t end, struct mp4_chapter *out,
                     int max)
{
    uint32_t version, count, len, skip;
    int n = 0;

    /* version(1) flags(3) [reserved(4) when version is 1] count(1) */
    lseek(ch_fd, payload, SEEK_SET);
    if (!rd_u8(&version))
        return 0;
    lseek(ch_fd, payload + 4, SEEK_SET);
    if (version == 1 && !rd_u32(&skip))
        return 0;
    if (!rd_u8(&count))
        return 0;

    while (n < (int)count && n < max)
    {
        uint64_t t;
        uint8_t title[256];

        if (lseek(ch_fd, 0, SEEK_CUR) + 9 > end)
            break;
        if (!rd_u64(&t) || !rd_u8(&len))
            break;
        if (!rd(title, len))
            break;
        out[n].start_ms = (unsigned long)(t / 10000); /* units of 100 ns */
        copy_title(out[n].title, title, len);
        n++;
    }
    return n;
}

/* ---- QuickTime chapter track ------------------------------------------ */

static void parse_stbl(off_t payload, off_t end, struct trk *t)
{
    uint32_t type;
    off_t p, e;

    lseek(ch_fd, payload, SEEK_SET);
    while (atom_header(end, &type, &p, &e))
    {
        switch (type)
        {
            case A_stts: t->stts = p; break;
            case A_stsz: t->stsz = p; break;
            case A_stsc: t->stsc = p; break;
            case A_stco: t->stco = p; t->co64 = false; break;
            case A_co64: t->stco = p; t->co64 = true; break;
        }
        lseek(ch_fd, e, SEEK_SET);
    }
}

static void parse_trak(off_t payload, off_t end, struct trk *t)
{
    uint32_t type, v, ver;
    off_t p, e;

    memset(t, 0, sizeof(*t));
    lseek(ch_fd, payload, SEEK_SET);
    while (atom_header(end, &type, &p, &e))
    {
        switch (type)
        {
            case A_tkhd:
                lseek(ch_fd, p, SEEK_SET);
                if (rd_u8(&ver))
                {
                    /* version 0: flags(3) created(4) modified(4) id(4)
                       version 1: flags(3) created(8) modified(8) id(4) */
                    lseek(ch_fd, p + 4 + (ver == 1 ? 16 : 8), SEEK_SET);
                    if (rd_u32(&v))
                        t->id = v;
                }
                break;
            case A_tref:
            {
                uint32_t rt;
                off_t rp, re;
                lseek(ch_fd, p, SEEK_SET);
                while (atom_header(e, &rt, &rp, &re))
                {
                    if (rt == A_chap)
                    {
                        lseek(ch_fd, rp, SEEK_SET);
                        if (rd_u32(&v))
                            t->chap_ref = v;
                    }
                    lseek(ch_fd, re, SEEK_SET);
                }
                break;
            }
            case A_mdia:
            {
                uint32_t mt;
                off_t mp, me;
                lseek(ch_fd, p, SEEK_SET);
                while (atom_header(e, &mt, &mp, &me))
                {
                    if (mt == A_mdhd)
                    {
                        lseek(ch_fd, mp, SEEK_SET);
                        if (rd_u8(&ver))
                        {
                            lseek(ch_fd, mp + 4 + (ver == 1 ? 16 : 8),
                                  SEEK_SET);
                            if (rd_u32(&v))
                                t->timescale = v;
                        }
                    }
                    else if (mt == A_hdlr)
                    {
                        /* version/flags(4) pre_defined(4) handler(4) */
                        lseek(ch_fd, mp + 8, SEEK_SET);
                        if (rd_u32(&v))
                            t->handler = v;
                    }
                    else if (mt == A_minf)
                    {
                        uint32_t it;
                        off_t ip, ie;
                        lseek(ch_fd, mp, SEEK_SET);
                        while (atom_header(me, &it, &ip, &ie))
                        {
                            if (it == A_stbl)
                                parse_stbl(ip, ie, t);
                            lseek(ch_fd, ie, SEEK_SET);
                        }
                    }
                    lseek(ch_fd, me, SEEK_SET);
                }
                break;
            }
        }
        lseek(ch_fd, e, SEEK_SET);
    }
}

/* Reads the samples of a text track as chapters. */
static int read_text_track(const struct trk *t, struct mp4_chapter *out,
                           int max)
{
    uint32_t count, v, i;
    static uint32_t sizes[MP4CH_MAX_SAMPLES];
    static off_t offs[MP4CH_MAX_SAMPLES];
    static uint32_t ticks[MP4CH_MAX_SAMPLES];
    uint32_t n;
    uint32_t uniform = 0;

    if (!t->stts || !t->stsz || !t->stsc || !t->stco || !t->timescale)
        return 0;

    /* sample sizes */
    lseek(ch_fd, t->stsz + 4, SEEK_SET);
    if (!rd_u32(&uniform) || !rd_u32(&count))
        return 0;
    n = count < (uint32_t)max ? count : (uint32_t)max;
    if (n > MP4CH_MAX_SAMPLES)
        n = MP4CH_MAX_SAMPLES;
    for (i = 0; i < n; i++)
    {
        if (uniform)
            sizes[i] = uniform;
        else if (!rd_u32(&sizes[i]))
            return 0;
    }

    /* durations -> start times (in ticks) */
    lseek(ch_fd, t->stts + 4, SEEK_SET);
    if (!rd_u32(&count))
        return 0;
    {
        uint32_t s = 0;
        uint32_t sum = 0;
        for (uint32_t e = 0; e < count && s < n; e++)
        {
            uint32_t cnt, delta;
            if (!rd_u32(&cnt) || !rd_u32(&delta))
                return 0;
            for (uint32_t k = 0; k < cnt && s < n; k++)
            {
                ticks[s++] = sum;
                sum += delta;
            }
        }
        if (s < n)
            n = s;
    }

    /* where the samples are: walk the chunks */
    {
        uint32_t stsc_n, chunks, s = 0;
        uint32_t per = 0, next_first = 0;
        uint32_t stsc_i = 0;

        lseek(ch_fd, t->stsc + 4, SEEK_SET);
        if (!rd_u32(&stsc_n))
            return 0;
        lseek(ch_fd, t->stco + 4, SEEK_SET);
        if (!rd_u32(&chunks))
            return 0;

        /* first stsc run */
        if (stsc_n == 0)
            return 0;
        {
            uint32_t desc;
            lseek(ch_fd, t->stsc + 8, SEEK_SET);
            uint32_t first0;
            if (!rd_u32(&first0) || !rd_u32(&per) || !rd_u32(&desc))
                return 0;
            stsc_i = 1;
            if (stsc_n > 1)
            {
                if (!rd_u32(&next_first))
                    return 0;
            }
            else
                next_first = 0xFFFFFFFF;
        }

        for (uint32_t c = 1; c <= chunks && s < n; c++)
        {
            uint64_t base;

            while (c >= next_first)
            {
                /* move to the next run: we are positioned after its
                   first_chunk word */
                uint32_t desc;
                lseek(ch_fd, t->stsc + 8 + (off_t)stsc_i * 12 + 4, SEEK_SET);
                if (!rd_u32(&per) || !rd_u32(&desc))
                    return 0;
                stsc_i++;
                if (stsc_i < stsc_n)
                {
                    lseek(ch_fd, t->stsc + 8 + (off_t)stsc_i * 12, SEEK_SET);
                    if (!rd_u32(&next_first))
                        return 0;
                }
                else
                    next_first = 0xFFFFFFFF;
            }

            lseek(ch_fd, t->stco + 8 + (off_t)(c - 1) * (t->co64 ? 8 : 4),
                  SEEK_SET);
            if (t->co64)
            {
                if (!rd_u64(&base))
                    return 0;
            }
            else
            {
                if (!rd_u32(&v))
                    return 0;
                base = v;
            }
            for (uint32_t k = 0; k < per && s < n; k++)
            {
                offs[s] = (off_t)base;
                base += sizes[s];
                s++;
            }
        }
        if (s < n)
            n = s;
    }

    /* read the titles */
    {
        int got = 0;
        for (i = 0; i < n && got < max; i++)
        {
            uint32_t len;
            uint8_t buf[256];

            lseek(ch_fd, offs[i], SEEK_SET);
            if (!rd_u16(&len))
                break;
            if (len > sizeof(buf))
                len = sizeof(buf);
            if (len > sizes[i] - 2)
                len = sizes[i] > 2 ? sizes[i] - 2 : 0;
            if (!rd(buf, len))
                break;
            out[got].start_ms = (unsigned long)
                ((uint64_t)ticks[i] * 1000 / t->timescale);
            copy_title(out[got].title, buf, len);
            got++;
        }
        return got;
    }
}

/* ---- top level -------------------------------------------------------- */

int mp4_read_chapters(const char *path, struct mp4_chapter *out, int max)
{
    uint32_t type;
    off_t p, e, limit;
    off_t moov_p = 0, moov_e = 0;
    struct trk tracks[MAX_TRACKS];
    int ntracks = 0;
    int n = 0;

    ch_fd = open(path, O_RDONLY);
    if (ch_fd < 0)
        return 0;
    limit = lseek(ch_fd, 0, SEEK_END);
    lseek(ch_fd, 0, SEEK_SET);

    /* find the moov atom (it may come after the audio data) */
    while (atom_header(limit, &type, &p, &e))
    {
        if (type == A_moov)
        {
            moov_p = p;
            moov_e = e;
            break;
        }
        if (lseek(ch_fd, e, SEEK_SET) < 0)
            break;
    }
    if (!moov_p)
        goto out;

    /* 1. Nero chpl, and collect the tracks on the way */
    lseek(ch_fd, moov_p, SEEK_SET);
    while (atom_header(moov_e, &type, &p, &e))
    {
        if (type == A_udta)
        {
            uint32_t ut;
            off_t up, ue;
            lseek(ch_fd, p, SEEK_SET);
            while (atom_header(e, &ut, &up, &ue))
            {
                if (ut == A_chpl && n == 0)
                    n = read_chpl(up, ue, out, max);
                lseek(ch_fd, ue, SEEK_SET);
            }
        }
        else if (type == A_trak && ntracks < MAX_TRACKS)
        {
            parse_trak(p, e, &tracks[ntracks]);
            ntracks++;
        }
        lseek(ch_fd, e, SEEK_SET);
    }
    if (n > 0)
        goto out;

    /* 2. a QuickTime chapter track: named by tref/chap of some track, else
          the only text track */
    {
        uint32_t want = 0;
        for (int i = 0; i < ntracks; i++)
            if (tracks[i].chap_ref)
            {
                want = tracks[i].chap_ref;
                break;
            }
        for (int i = 0; i < ntracks; i++)
        {
            bool is_text = tracks[i].handler == A_text ||
                           tracks[i].handler == A_sbtl;
            if ((want && tracks[i].id == want) || (!want && is_text))
            {
                n = read_text_track(&tracks[i], out, max);
                if (n > 0)
                    break;
            }
        }
    }

out:
    close(ch_fd);
    return n;
}
