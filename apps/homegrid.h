/***************************************************************************
 * Icon home: a desktop-style 4x2 grid of big icons, shown instead of the
 * stock root menu on the Creative Zen X-Fi.  Colours follow the active
 * theme.  "Mode" switches to the stock Rockbox menu, whose
 * "Icon Home" entry switches back.
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
#ifndef _HOMEGRID_H
#define _HOMEGRID_H

#include "config.h"

#if defined(CREATIVE_ZENXFI) && !defined(BOOTLOADER) && defined(HAVE_LCD_COLOR)
#define HAVE_ICON_HOME
#endif

#ifdef HAVE_ICON_HOME
#include <stdbool.h>

/* true while the icon home is the "root" screen (it is on after boot) */
bool homegrid_is_active(void);
void homegrid_set_active(bool active);

/* Runs the icon home until the user picks something that leaves it.
 * Returns a GO_TO_* code for the root menu loop. */
int homegrid_show(void);

/* true while the file browser was opened from the ambience page: picking a
 * file there starts it as ambient */
bool homegrid_ambient_pick(void);

/* Settings > Theme Settings > Home icon pack: pick the icon pack */
int homegrid_pack_menu(void);
#endif

#endif /* _HOMEGRID_H */
