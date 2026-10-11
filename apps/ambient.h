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

#endif /* _AMBIENT_H */
