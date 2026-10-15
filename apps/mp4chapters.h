/***************************************************************************
 * Chapter list reader for .m4b / .m4a / .mp4 audiobooks.
 *
 * Understands both chapter formats in use:
 *   - the Nero "chpl" atom (moov/udta/chpl), written by most audiobook tools
 *   - a QuickTime chapter track (a text track that the audio track points
 *     to with tref/chap), written by Apple tools and some rippers
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
#ifndef _MP4CHAPTERS_H
#define _MP4CHAPTERS_H

#define MP4CH_TITLE_LEN 48
#define MP4CH_MAX_SAMPLES 300   /* most chapters read from a text track */

struct mp4_chapter
{
    unsigned long start_ms;
    char title[MP4CH_TITLE_LEN];
};

/* Reads the chapters of an MP4 file into out[] (at most max). Returns the
 * number of chapters, 0 if the file has none or cannot be read. */
int mp4_read_chapters(const char *path, struct mp4_chapter *out, int max);

#endif /* _MP4CHAPTERS_H */
