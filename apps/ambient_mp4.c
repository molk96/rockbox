/***************************************************************************
 * Minimal MP4/M4A reader for the ambient audio layer, see ambient_mp4.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/

#include <string.h>
#include "config.h"
#include "file.h"
#include "ambient_mp4.h"

#define FOURCC(a,b,c,d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                         ((uint32_t)(c) << 8) | (uint32_t)(d))

/* what has been found in the track being parsed */
struct trak_info
{
    bool is_audio;
    bool have_asc, have_stsz, have_stsc, have_stco;
    off_t stsz_off;
    uint32_t stsz_fixed, nsamples;
    off_t stco_off;
    uint32_t nchunks;
    bool co64;
    off_t stsc_off;
    uint32_t nstsc;
    uint8_t asc[AMB_MP4_MAX_ASC];
    uint32_t asc_len;
};

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static bool read_at(int fd, off_t off, void *buf, size_t len)
{
    if (lseek(fd, off, SEEK_SET) != off)
        return false;
    return read(fd, buf, len) == (ssize_t)len;
}

/* Read a 32 bit big endian value from a table, through a small cache */
static bool table_u32(int fd, struct amb_mp4_cache *c, off_t off, uint32_t *v)
{
    if (c->len <= 0 || off < c->base || off + 4 > c->base + c->len)
    {
        ssize_t n;
        if (lseek(fd, off, SEEK_SET) != off)
            return false;
        n = read(fd, c->buf, AMB_MP4_CACHE);
        if (n < 4)
        {
            c->len = 0;
            return false;
        }
        c->base = off;
        c->len = n;
    }
    *v = be32(c->buf + (off - c->base));
    return true;
}

/* Variable length descriptor size used inside esds */
static bool desc_len(const uint8_t *p, unsigned int *i, unsigned int n,
                     uint32_t *len)
{
    uint32_t l = 0;
    for (int k = 0; k < 4; k++)
    {
        if (*i >= n)
            return false;
        uint8_t b = p[(*i)++];
        l = (l << 7) | (b & 0x7f);
        if (!(b & 0x80))
        {
            *len = l;
            return true;
        }
    }
    return false;
}

/* Find the AudioSpecificConfig inside the payload of an esds box */
static bool parse_esds(const uint8_t *p, unsigned int n, struct trak_info *t)
{
    unsigned int i = 4; /* version + flags */
    uint32_t len;

    if (i >= n)
        return false;
    if (p[i] == 0x03) /* ES_Descriptor */
    {
        i++;
        if (!desc_len(p, &i, n, &len) || i + 3 > n)
            return false;
        uint8_t flags = p[i + 2];
        i += 3;
        if (flags & 0x80)
            i += 2;
        if (flags & 0x40)
        {
            if (i >= n)
                return false;
            i += 1 + p[i];
        }
        if (flags & 0x20)
            i += 2;
    }
    if (i >= n || p[i] != 0x04) /* DecoderConfigDescriptor */
        return false;
    i++;
    if (!desc_len(p, &i, n, &len))
        return false;
    i += 13;
    if (i >= n || p[i] != 0x05) /* DecoderSpecificInfo */
        return false;
    i++;
    if (!desc_len(p, &i, n, &len) || len == 0 || len > AMB_MP4_MAX_ASC ||
        i + len > n)
        return false;
    memcpy(t->asc, p + i, len);
    t->asc_len = len;
    t->have_asc = true;
    return true;
}

static void parse_stsd(int fd, off_t payload, off_t end, struct trak_info *t)
{
    uint8_t h[16];
    /* version/flags, entry count, then the first sample entry */
    if (!read_at(fd, payload, h, 16))
        return;
    uint32_t esize = be32(h + 8);
    if (be32(h + 12) != FOURCC('m','p','4','a'))
        return;

    off_t entry = payload + 8;
    off_t entry_end = entry + esize;
    if (entry_end > end || esize < 36)
        return;

    uint8_t ver[2];
    if (!read_at(fd, entry + 8 + 8, ver, 2))
        return;
    unsigned int version = (ver[0] << 8) | ver[1];

    off_t child = entry + 36;
    if (version == 1)
        child += 16;
    else if (version == 2)
        child += 36;

    /* child boxes of the sample entry: look for esds */
    while (child + 8 <= entry_end)
    {
        uint8_t bh[8];
        if (!read_at(fd, child, bh, 8))
            return;
        uint32_t bsize = be32(bh);
        if (bsize < 8 || child + bsize > entry_end)
            return;
        if (be32(bh + 4) == FOURCC('e','s','d','s'))
        {
            uint8_t buf[160];
            unsigned int n = bsize - 8;
            if (n > sizeof (buf))
                n = sizeof (buf);
            if (read_at(fd, child + 8, buf, n))
                parse_esds(buf, n, t);
            return;
        }
        child += bsize;
    }
}

static bool trak_complete(const struct trak_info *t)
{
    return t->is_audio && t->have_asc && t->have_stsz && t->have_stsc &&
           t->have_stco && t->nsamples > 0 && t->nstsc > 0;
}

/* Walk the boxes in [start,end). Returns true when a usable audio track
 * has been found (and stored in *out). */
static bool walk(int fd, off_t start, off_t end, int depth, bool in_trak,
                 struct trak_info *cur, struct trak_info *out)
{
    off_t pos = start;

    while (pos + 8 <= end)
    {
        uint8_t h[16];
        if (!read_at(fd, pos, h, 8))
            return false;

        uint64_t size = be32(h);
        uint32_t type = be32(h + 4);
        unsigned int hdr = 8;

        if (size == 1)
        {
            if (!read_at(fd, pos + 8, h + 8, 8))
                return false;
            size = ((uint64_t)be32(h + 8) << 32) | be32(h + 12);
            hdr = 16;
        }
        else if (size == 0)
            size = end - pos;

        if (size < hdr || pos + (off_t)size > end)
            return false;

        off_t payload = pos + hdr;
        off_t boxend = pos + (off_t)size;
        uint8_t b[16];

        switch (type)
        {
        case FOURCC('m','o','o','v'):
        case FOURCC('m','d','i','a'):
        case FOURCC('m','i','n','f'):
        case FOURCC('s','t','b','l'):
            if (depth < 6 &&
                walk(fd, payload, boxend, depth + 1, in_trak, cur, out))
                return true;
            break;
        case FOURCC('t','r','a','k'):
        {
            struct trak_info t;
            memset(&t, 0, sizeof (t));
            if (depth < 6 && walk(fd, payload, boxend, depth + 1, true, &t, out))
                return true;
            if (trak_complete(&t))
            {
                *out = t;
                return true;
            }
            break;
        }
        case FOURCC('h','d','l','r'):
            if (in_trak && read_at(fd, payload, b, 12))
                cur->is_audio = be32(b + 8) == FOURCC('s','o','u','n');
            break;
        case FOURCC('s','t','s','d'):
            if (in_trak)
                parse_stsd(fd, payload, boxend, cur);
            break;
        case FOURCC('s','t','s','z'):
            if (in_trak && read_at(fd, payload, b, 12))
            {
                cur->stsz_fixed = be32(b + 4);
                cur->nsamples = be32(b + 8);
                cur->stsz_off = payload + 12;
                cur->have_stsz = true;
            }
            break;
        case FOURCC('s','t','s','c'):
            if (in_trak && read_at(fd, payload, b, 8))
            {
                cur->nstsc = be32(b + 4);
                cur->stsc_off = payload + 8;
                cur->have_stsc = cur->nstsc > 0;
            }
            break;
        case FOURCC('s','t','c','o'):
        case FOURCC('c','o','6','4'):
            if (in_trak && read_at(fd, payload, b, 8))
            {
                cur->nchunks = be32(b + 4);
                cur->stco_off = payload + 8;
                cur->co64 = type == FOURCC('c','o','6','4');
                cur->have_stco = true;
            }
            break;
        default:
            break;
        }

        pos = boxend;
    }
    return false;
}

bool amb_mp4_open(struct amb_mp4 *m, int fd)
{
    struct trak_info cur, found;
    off_t size = lseek(fd, 0, SEEK_END);

    memset(m, 0, sizeof (*m));
    memset(&cur, 0, sizeof (cur));
    memset(&found, 0, sizeof (found));
    m->fd = fd;

    if (size <= 8 || !walk(fd, 0, size, 0, false, &cur, &found))
        return false;

    m->stsz_off = found.stsz_off;
    m->stsz_fixed = found.stsz_fixed;
    m->nsamples = found.nsamples;
    m->stco_off = found.stco_off;
    m->nchunks = found.nchunks;
    m->co64 = found.co64;
    m->nstsc = found.nstsc;
    m->stsc_off = found.stsc_off;
    memcpy(m->asc, found.asc, found.asc_len);
    m->asc_len = found.asc_len;
    return true;
}

int amb_mp4_next(struct amb_mp4 *m, uint8_t *buf, unsigned int bufsize)
{
    uint32_t size;

    if (m->sample >= m->nsamples)
        return 0;

    if (m->left == 0)
    {
        /* start the next chunk */
        uint32_t c = m->next_chunk++;
        if (c >= m->nchunks)
            return 0;

        /* find the run of the sample-to-chunk table this chunk belongs to */
        uint32_t first, spc;
        while (m->stsc_idx + 1 < m->nstsc)
        {
            if (!table_u32(m->fd, &m->c_sc,
                           m->stsc_off + 12 * (off_t)(m->stsc_idx + 1), &first))
                return 0;
            if (first > c + 1)
                break;
            m->stsc_idx++;
        }
        if (!table_u32(m->fd, &m->c_sc,
                       m->stsc_off + 12 * (off_t)m->stsc_idx + 4, &spc))
            return 0;
        m->left = spc;

        uint32_t hi = 0, lo = 0;
        if (m->co64)
        {
            if (!table_u32(m->fd, &m->c_co, m->stco_off + 8 * (off_t)c, &hi) ||
                !table_u32(m->fd, &m->c_co, m->stco_off + 8 * (off_t)c + 4, &lo))
                return 0;
        }
        else if (!table_u32(m->fd, &m->c_co, m->stco_off + 4 * (off_t)c, &lo))
            return 0;
        m->pos = ((uint64_t)hi << 32) | lo;

        if (m->left == 0)
            return -1; /* empty chunk, caller asks again */
    }

    if (m->stsz_fixed)
        size = m->stsz_fixed;
    else if (!table_u32(m->fd, &m->c_sz, m->stsz_off + 4 * (off_t)m->sample,
                        &size))
        return 0;

    off_t at = (off_t)m->pos;
    m->pos += size;
    m->left--;
    m->sample++;

    if (size == 0 || size > bufsize)
        return -1;
    if (!read_at(m->fd, at, buf, size))
        return 0;
    return (int)size;
}
