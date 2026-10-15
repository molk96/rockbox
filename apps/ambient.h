/***************************************************************************
 * Ambient audio layer: a second MP3 stream mixed on top of normal playback
 * (for example ambient music under an audiobook).
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
#ifndef _AMBIENT_H
#define _AMBIENT_H

#include <stdbool.h>
#include <stddef.h>

enum ambient_modes
{
    AMBIENT_MODE_LOOP = 0, /* repeat one track */
    AMBIENT_MODE_FOLDER,   /* play the folder in order, then repeat */
    AMBIENT_MODE_SHUFFLE,  /* play the folder shuffled */
};

void ambient_init(void);
/* path is an .mp3 file or a folder */
bool ambient_start(const char *path);
void ambient_stop(void);
bool ambient_is_active(void);
/* percent 0..100, relative to full scale */
void ambient_set_volume(int percent);
int ambient_get_volume(void);

/* jump to the next / previous file of the folder */
void ambient_next(void);
void ambient_prev(void);

/* user pause from the ambience page (fades like the stop) */
void ambient_pause(void);
void ambient_resume(void);
bool ambient_is_paused(void);

/* what is playing: file name, its folder and "n of total" in play order */
bool ambient_get_track(char *buf, size_t size);
bool ambient_get_dir(char *buf, size_t size);
bool ambient_get_index(int *index, int *total);

/* the last ambient files/folders that were started, newest first, kept in
 * /.rockbox/ambient_recent.txt */
#define AMBIENT_RECENT_MAX 8
void ambient_recent_add(const char *path);
int ambient_recent_count(void);
bool ambient_recent_get(int index, char *buf, size_t size);

void ambient_sleepfade_restore(bool hardware);
void sleep_preset_changed(int index);

#endif /* _AMBIENT_H */
