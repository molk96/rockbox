/***************************************************************************
 * Minimal MP4/M4A reader for the ambient audio layer: finds the first
 * AAC track and hands out its samples (AAC frames) one at a time, reading
 * the sample tables straight from the file so that long files need no RAM.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#ifndef AMBIENT_MP4_H
#define AMBIENT_MP4_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define AMB_MP4_MAX_ASC    64
#define AMB_MP4_CACHE      512

struct amb_mp4_cache
{
    off_t base;
    int len;
    uint8_t buf[AMB_MP4_CACHE];
};

struct amb_mp4
{
    int fd;

    /* sample tables (offsets into the file) */
    off_t stsz_off;           /* per-sample size table */
    uint32_t stsz_fixed;      /* nonzero: all samples have this size */
    uint32_t nsamples;
    off_t stco_off;           /* chunk offset table */
    uint32_t nchunks;
    bool co64;                /* 64 bit chunk offsets */
    off_t stsc_off;           /* sample-to-chunk table, 12 bytes per run */
    uint32_t nstsc;

    /* decoder specific config of the track */
    uint8_t asc[AMB_MP4_MAX_ASC];
    uint32_t asc_len;

    /* read cursor */
    uint32_t sample;
    uint32_t next_chunk;
    uint32_t stsc_idx;
    uint32_t left;            /* samples left in the current chunk */
    uint64_t pos;             /* file offset of the next sample */

    struct amb_mp4_cache c_sz, c_co, c_sc;
};

/* Parse the file. True if an AAC track was found. */
bool amb_mp4_open(struct amb_mp4 *m, int fd);

/* Read the next sample into buf. Returns its size, 0 at the end of the
 * track, or -1 if the sample could not be read (the caller may go on). */
int amb_mp4_next(struct amb_mp4 *m, uint8_t *buf, unsigned int bufsize);

#endif
