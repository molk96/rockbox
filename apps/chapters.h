/***************************************************************************
 * Chapter selector for audiobooks (Creative Zen X-Fi build).
 *
 * Shows the chapters of a book and starts playback at the one picked.
 * A book that is a folder of several files has one chapter per file; a book
 * that is a single .m4b / .m4a uses the chapters stored inside the file.
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
#ifndef _CHAPTERS_H
#define _CHAPTERS_H

#include "config.h"
#include <stdbool.h>

#if defined(CREATIVE_ZENXFI) && !defined(BOOTLOADER)
#define HAVE_CHAPTERS

/* Lets the user pick a chapter of the book in folder dir and starts playing
 * it. Returns true when playback was started. */
bool chapters_choose(const char *dir);
#endif

#endif /* _CHAPTERS_H */
