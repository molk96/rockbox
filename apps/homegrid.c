/***************************************************************************
 * Icon home: a desktop-style 4x2 grid of icons (Creative Zen X-Fi), under
 * the title "Mp3 player".
 *
 * Navigation: Up/Down/Left/Right move the selection (wrapping), Select
 * opens the tile.  Back jumps to Now Playing when something is playing.
 * All colours come from the active theme (foreground, background and the
 * list selector colours), so every theme recolours this page.
 *
 * Icons: the built-in icons are small one-bit pictures (homegrid_icons.h)
 * drawn in the theme colours.  A full-colour BMP named
 * /.rockbox/icons/home/<audiobooks|music|podcasts|ambience|sleep|settings|
 * shortcuts|mode>.bmp (up to 40x40, magenta 255,0,255 is transparent)
 * replaces a built-in icon.
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
#include "homegrid.h"

#ifdef HAVE_ICON_HOME

#include <stdio.h>
#include <string.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "lcd.h"
#include "button.h"
#include "action.h"
#include "audio.h"
#include "file.h"
#include "dir.h"
#include "core_alloc.h"
#include "settings.h"
#include "screen_access.h"
#include "viewport.h"
#include "splash.h"
#include "misc.h"
#include "root_menu.h"
#include "ambient.h"
#include "powermgmt.h"
#include "timefuncs.h"
#include "metadata.h"
#include "list.h"
#include "lang.h"
#include "option_select.h"
#include "homegrid_icons.h"
#include "pathfuncs.h"
#include "string-extra.h"
#include "gui/list.h"

#define COLS 4
#define ROWS 2
#define NUM_TILES (COLS * ROWS)

#define ICON_SCALE 2                /* built-in icons are drawn at 2x */
#define BUILTIN_ICON_PX (HOMEGRID_ICON_SIZE * ICON_SCALE)
#define CUSTOM_ICON_MAX 40          /* largest custom icon (pixels) */
#define ICON_BOX CUSTOM_ICON_MAX    /* icons are centred in this box */

#define HOMEGRID_ICON_DIR ROCKBOX_DIR "/icons/home"

enum tile_id
{
    TILE_AUDIOBOOKS = 0,
    TILE_MUSIC,
    TILE_PODCASTS,
    TILE_AMBIENCE,
    TILE_SLEEP,
    TILE_SETTINGS,
    TILE_SHORTCUTS,
    TILE_MODE,
};

struct tile_info
{
    const char *label;
    const char *file;   /* custom icon file name (without dir) */
};

static const struct tile_info tiles[NUM_TILES] = {
    [TILE_AUDIOBOOKS] = { "Books",       "audiobooks.bmp" },
    [TILE_MUSIC]      = { "Music",       "music.bmp" },
    [TILE_PODCASTS]   = { "Podcasts",    "podcasts.bmp" },
    [TILE_AMBIENCE]   = { "Ambience",    "ambience.bmp" },
    [TILE_SLEEP]      = { "Sleep",       "sleep.bmp" },
    [TILE_SETTINGS]   = { "Settings",    "settings.bmp" },
    [TILE_SHORTCUTS]  = { "Shortcuts",   "shortcuts.bmp" },
    [TILE_MODE]       = { "Mode",        "mode.bmp" },
};

/* the icon home is the root screen from boot on */
static bool grid_active = true;
static int selected_tile = TILE_AUDIOBOOKS;

/* custom icons, loaded when the page is shown */
static struct
{
    int handle;           /* buflib handle, <= 0 when there is none */
    struct bitmap bm;
} custom[NUM_TILES];

bool homegrid_is_active(void)
{
    return grid_active;
}

void homegrid_set_active(bool active)
{
    grid_active = active;
}

/* ---- custom icons ------------------------------------------------------ */

static void icons_load(void)
{
    char path[MAX_PATH];
    for (int i = 0; i < NUM_TILES; i++)
    {
        ssize_t reqd = 0;
        custom[i].handle = 0;
        snprintf(path, sizeof(path), "%s/%s", HOMEGRID_ICON_DIR, tiles[i].file);
        if (!file_exists(path))
            continue;
        int h = core_load_bmp(path, &custom[i].bm,
                              FORMAT_ANY | FORMAT_TRANSPARENT, &reqd, NULL);
        if (h <= 0)
            continue;
        if (custom[i].bm.width > CUSTOM_ICON_MAX ||
            custom[i].bm.height > CUSTOM_ICON_MAX)
        {
            core_free(h); /* too big: use the built-in icon */
            continue;
        }
        /* the block has no move callback, so keep it where it is while the
           page is open (the bitmap data pointer is used for every redraw) */
        core_pin(h);
        custom[i].bm.data = core_get_data(h);
        custom[i].handle = h;
    }
}

static void icons_free(void)
{
    for (int i = 0; i < NUM_TILES; i++)
    {
        if (custom[i].handle > 0)
        {
            core_unpin(custom[i].handle);
            core_free(custom[i].handle);
        }
        custom[i].handle = 0;
    }
}

/* ---- colours ----------------------------------------------------------- */

struct palette
{
    unsigned fg;      /* normal text and icons */
    unsigned bg;      /* page background */
    unsigned dim;     /* secondary text, separators */
    unsigned accent;  /* selection frame and progress bars */
    unsigned sel_bg;  /* fill behind the selected tile */
};

/* pct percent of b, the rest of a */
static unsigned blend(unsigned a, unsigned b, int pct)
{
    int r  = (RGB_UNPACK_RED(a)   * (100 - pct) + RGB_UNPACK_RED(b)   * pct) / 100;
    int g  = (RGB_UNPACK_GREEN(a) * (100 - pct) + RGB_UNPACK_GREEN(b) * pct) / 100;
    int bl = (RGB_UNPACK_BLUE(a)  * (100 - pct) + RGB_UNPACK_BLUE(b)  * pct) / 100;
    return LCD_RGBPACK(r, g, bl);
}

static void palette_init(struct palette *p)
{
    p->fg = global_settings.fg_color;
    p->bg = global_settings.bg_color;
    /* same rule as the lists: colour or gradient selector bars use the
       theme's selector colour, otherwise the text colour is the accent */
    if (global_settings.cursor_style == SYNCLIST_CURSOR_COLOR ||
        global_settings.cursor_style == SYNCLIST_CURSOR_GRADIENT)
        p->accent = global_settings.lss_color;
    else
        p->accent = p->fg;
    p->dim = blend(p->bg, p->fg, 55);
    p->sel_bg = blend(p->bg, p->accent, 22);
}

/* ---- drawing ----------------------------------------------------------- */

static void draw_builtin_icon(struct screen *d, int idx, int x, int y)
{
    for (int row = 0; row < HOMEGRID_ICON_SIZE; row++)
    {
        unsigned long bits = homegrid_icons[idx][row];
        int col = 0;
        while (col < HOMEGRID_ICON_SIZE)
        {
            if (!(bits & (1ul << (HOMEGRID_ICON_SIZE - 1 - col))))
            {
                col++;
                continue;
            }
            int start = col;
            while (col < HOMEGRID_ICON_SIZE &&
                   (bits & (1ul << (HOMEGRID_ICON_SIZE - 1 - col))))
                col++;
            d->fillrect(x + start * ICON_SCALE, y + row * ICON_SCALE,
                        (col - start) * ICON_SCALE, ICON_SCALE);
        }
    }
}

/* label of a tile; the sleep timer tile shows the time left while it runs */
static void tile_label(int idx, char *buf, size_t size)
{
    if (idx == TILE_SLEEP && get_sleep_timer_active())
    {
        int min = (get_sleep_timer() + 59) / 60;
        if (min >= 60)
            snprintf(buf, size, "Sleep %d:%02d", min / 60, min % 60);
        else
            snprintf(buf, size, "Sleep %dm", min);
    }
    else
        snprintf(buf, size, "%s", tiles[idx].label);
}

/* cut text from the end until it is at most maxw pixels wide */
static void fit_text(struct screen *d, char *text, int maxw)
{
    int w;
    size_t len = strlen(text);
    d->getstringsize(text, &w, NULL);
    while (len > 1 && w > maxw)
    {
        text[--len] = '\0';
        d->getstringsize(text, &w, NULL);
    }
}

static void draw_tile(struct screen *d, const struct palette *pal, int idx,
                      bool selected, int tx, int ty, int tw, int th, int fh)
{
    if (selected)
    {
        d->set_foreground(pal->sel_bg);
        d->fillrect(tx, ty, tw, th);
        d->set_foreground(pal->accent);
        d->drawrect(tx, ty, tw, th);
        d->drawrect(tx + 1, ty + 1, tw - 2, th - 2);
    }

    /* icon box on top, label below, both centred as one block */
    int block_h = ICON_BOX + 2 + fh;
    int by = ty + (th - block_h) / 2;
    int ix = tx + (tw - ICON_BOX) / 2;

    d->set_foreground(selected ? pal->accent : pal->fg);
    if (custom[idx].handle > 0)
    {
        int cx = ix + (ICON_BOX - custom[idx].bm.width) / 2;
        int cy = by + (ICON_BOX - custom[idx].bm.height) / 2;
        d->transparent_bitmap(custom[idx].bm.data, cx, cy,
                              custom[idx].bm.width, custom[idx].bm.height);
    }
    else
    {
        draw_builtin_icon(d, idx, ix + (ICON_BOX - BUILTIN_ICON_PX) / 2,
                          by + (ICON_BOX - BUILTIN_ICON_PX) / 2);
    }

    char label[24];
    int w;
    tile_label(idx, label, sizeof(label));
    fit_text(d, label, tw - 4);
    d->set_foreground(selected ? pal->accent : pal->fg);
    d->getstringsize(label, &w, NULL);
    d->putsxy(tx + (tw - w) / 2, by + ICON_BOX + 2, label);
}

static void format_clock(char *buf, size_t size)
{
#ifdef CONFIG_RTC
    struct tm *tm = get_time();
    snprintf(buf, size, "%02d:%02d", tm->tm_hour, tm->tm_min);
#else
    buf[0] = '\0';
    (void)size;
#endif
}

static void format_elapsed(char *buf, size_t size, unsigned long ms)
{
    unsigned long s = ms / 1000;
    if (s >= 3600)
        snprintf(buf, size, "%lu:%02lu:%02lu", s / 3600, (s / 60) % 60, s % 60);
    else
        snprintf(buf, size, "%lu:%02lu", s / 60, s % 60);
}

static void draw_header(struct screen *d, const struct palette *pal,
                        const char *title, int width, int hh)
{
    char buf[16], tmp[16];
    int w, h;

    d->set_foreground(pal->fg);
    d->getstringsize(title, &w, &h);
    d->putsxy(6, (hh - h) / 2, title);

    format_clock(tmp, sizeof(tmp));
    int level = battery_level();
    if (level >= 0)
        snprintf(buf, sizeof(buf), "%d%%", level);
    else
        buf[0] = '\0';

    d->set_foreground(pal->dim);
    int x = width - 6;
    if (tmp[0])
    {
        d->getstringsize(tmp, &w, &h);
        x -= w;
        d->putsxy(x, (hh - h) / 2, tmp);
        x -= 10;
    }
    if (buf[0])
    {
        d->getstringsize(buf, &w, &h);
        x -= w;
        d->putsxy(x, (hh - h) / 2, buf);
    }

    d->set_foreground(pal->dim);
    d->hline(0, width - 1, hh - 1);
}

static void draw_footer(struct screen *d, const struct palette *pal,
                        int width, int fy, int fh_total, int fh)
{
    char text[MAX_PATH], tbuf[16];
    int w, h;
    int status = audio_status();
    struct mp3entry *id3 = (status & (AUDIO_STATUS_PLAY | AUDIO_STATUS_PAUSE))
                           ? audio_current_track() : NULL;

    d->set_foreground(pal->dim);
    d->hline(0, width - 1, fy);

    int ty = fy + 3;
    int right = width - 6;

    if (ambient_is_active())
    {
        d->getstringsize("AMB", &w, NULL);
        right -= w;
        d->set_foreground(pal->accent);
        d->putsxy(right, ty, "AMB");
        right -= 8;
    }

    if (id3)
    {
        const char *title = id3->title;
        if (!title || !title[0])
        {
            title = strrchr(id3->path, '/');
            title = title ? title + 1 : id3->path;
        }
        format_elapsed(tbuf, sizeof(tbuf), id3->elapsed);
        snprintf(text, sizeof(text), "%s %s",
                 (status & AUDIO_STATUS_PAUSE) ? "||" : ">", title);

        d->getstringsize(tbuf, &w, &h);
        right -= w;
        d->set_foreground(pal->dim);
        d->putsxy(right, ty, tbuf);
        right -= 8;

        /* trim the title so that it never runs into the time */
        size_t len = strlen(text);
        d->getstringsize(text, &w, NULL);
        while (len > 4 && 6 + w > right)
        {
            text[--len] = '\0';
            d->getstringsize(text, &w, NULL);
        }
        d->set_foreground(pal->fg);
        d->putsxy(6, ty, text);

        /* progress bar along the bottom edge */
        if (id3->length > 0)
        {
            int bw = (int)((unsigned long long)(width - 12) * id3->elapsed /
                           id3->length);
            if (bw > width - 12)
                bw = width - 12;
            d->set_foreground(pal->dim);
            d->fillrect(6, fy + fh_total - 4, width - 12, 2);
            d->set_foreground(pal->accent);
            d->fillrect(6, fy + fh_total - 4, bw, 2);
        }
    }
    else
    {
        d->set_foreground(pal->dim);
        d->putsxy(6, ty, "Nothing playing");
    }
    (void)fh;
}

/* ---- tile actions ------------------------------------------------------ */

/* open the file browser in dir, or in the root if it does not exist */
static int browse_folder(const char *dir)
{
    if (!dir_exists(dir))
    {
        splashf(HZ * 2, "No %s folder", dir);
        dir = "/";
    }
    root_menu_browse_folder(dir);
    return GO_TO_FILEBROWSER;
}

/* the sleep timer picker: Off / 15 / 30 / 60 min, the same setting as the
   quick screen item */
static void sleep_picker(void)
{
    const struct settings_list *setting =
                                find_setting(&global_settings.sleep_preset);
    if (!setting)
        return;
    if (!get_sleep_timer_active())
        global_settings.sleep_preset = 0; /* nothing is running: show Off */
    option_screen(setting, NULL, false, ID2P(LANG_SLEEP_TIMER));
}

/* Opens a tile. Returns the next screen for the root menu loop, or sets
 * *stay when the page should stay up (the sleep timer picker). */
static int open_tile(int idx, bool *stay)
{
    switch (idx)
    {
        case TILE_AUDIOBOOKS:
            /* placeholder until the audiobook page exists */
            return browse_folder("/Audiobooks");
        case TILE_MUSIC:
            return browse_folder("/Music");
        case TILE_PODCASTS:
            return browse_folder("/Podcasts");
        case TILE_AMBIENCE:
            return GO_TO_ROOT; /* handled by homegrid_show() */
        case TILE_SLEEP:
            sleep_picker();
            *stay = true;
            return GO_TO_ROOT;
        case TILE_SETTINGS:
            return GO_TO_MAINMENU;
        case TILE_SHORTCUTS:
            return GO_TO_SHORTCUTMENU;
        case TILE_MODE:
            homegrid_set_active(false);
            return GO_TO_ROOT;
    }
    return GO_TO_ROOT;
}


/* ---- Ambience page ------------------------------------------------------ */

/* set while the file browser was opened from the ambience page: a file
   picked there is started as ambient instead of normal playback */
static bool ambient_pick_mode;
/* the page that opened the browser is shown again when the browser closes */
static bool ambience_reopen;

bool homegrid_ambient_pick(void)
{
    return ambient_pick_mode;
}

enum
{
    AROW_VOLUME = 0,
    AROW_PAUSE,
    AROW_STOP,
    AROW_RECENT,
    AROW_BROWSE,
    AROW_COUNT
};

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return (slash && slash[1]) ? slash + 1 : path;
}

static const char *ambient_mode_name(void)
{
    switch (global_settings.ambient_mode)
    {
        case AMBIENT_MODE_LOOP:   return "Loop one";
        case AMBIENT_MODE_FOLDER: return "Folder";
        default:                  return "Shuffle";
    }
}

static const char *recent_get_name(int selected_item, void *data,
                                   char *buffer, size_t buffer_len)
{
    char path[MAX_PATH];
    (void)data;
    if (!ambient_recent_get(selected_item, path, sizeof(path)))
        return "";
    strlcpy(buffer, base_name(path), buffer_len);
    return buffer;
}

static int recent_action(int action, struct gui_synclist *lists)
{
    (void)lists;
    /* returning CANCEL for OK makes simplelist exit with the selection set */
    return action == ACTION_STD_OK ? ACTION_STD_CANCEL : action;
}

/* lets the user pick one of the recent ambient files/folders */
static void recent_picker(void)
{
    struct simplelist_info info;
    int n = ambient_recent_count();

    if (n <= 0)
    {
        splash(HZ, "No recent ambience yet");
        return;
    }
    simplelist_info_init(&info, "Recent ambience", n, NULL);
    info.get_name = recent_get_name;
    info.action_callback = recent_action;
    if (simplelist_show_list(&info) || info.selection < 0)
        return;
    char path[MAX_PATH];
    if (ambient_recent_get(info.selection, path, sizeof(path)))
        ambient_start(path);
}

/* the volume slider row */
static void draw_volume_row(struct screen *d, const struct palette *pal,
                            bool selected, int x, int y, int w, int h)
{
    char pct[8];
    int tw, th;
    int vol = ambient_get_volume();

    d->set_foreground(selected ? pal->accent : pal->fg);
    d->getstringsize("Volume", &tw, &th);
    d->putsxy(x + 6, y + (h - th) / 2, "Volume");

    snprintf(pct, sizeof(pct), "%d%%", vol);
    d->getstringsize(pct, &tw, &th);
    d->putsxy(x + w - 6 - tw, y + (h - th) / 2, pct);

    int d1, d2;
    d->getstringsize("Volume", &d1, NULL);
    d->getstringsize("100%", &d2, NULL);
    int sx = x + 6 + d1 + 8;
    int sw = (x + w - 6 - d2 - 8) - sx;
    int sy = y + h / 2 - 2;
    if (sw < 10)
        return;
    d->set_foreground(pal->dim);
    d->drawrect(sx, sy, sw, 5);
    d->set_foreground(selected ? pal->accent : pal->fg);
    d->fillrect(sx + 1, sy + 1, (sw - 2) * vol / 100, 3);
    int kx = sx + (sw - 4) * vol / 100;
    d->fillrect(kx, y + 3, 4, h - 6);
}

/* Shows the ambience page. Returns true when the whole icon home should be
 * left (with *result as the next screen), false to return to the grid. */
static bool ambience_page(struct screen *display, struct viewport *vp,
                          int *result)
{
    struct palette pal;
    int sel = AROW_PAUSE;
    bool leave = false, done = false;

    action_wait_for_release();

    while (!done)
    {
        palette_init(&pal);
        vp->fg_pattern = pal.fg;
        vp->bg_pattern = pal.bg;
        display->clear_viewport();

        int width = vp->width, height = vp->height;
        int fh = display->getcharheight();
        int hh = fh + 6;
        int foot_h = fh + 9;
        int fy = height - foot_h;
        int rows_h = AROW_COUNT * (fh + 10);
        int ry = fy - rows_h - 4;
        int row_h = fh + 10;
        char track[MAX_PATH], dir[MAX_PATH], line[64];
        int idx, total;
        bool active = ambient_is_active();

        draw_header(display, &pal, "Ambience", width, hh);

        /* what is playing */
        int iy = hh + 4;
        display->set_foreground(pal.dim);
        display->putsxy(6, iy, "NOW PLAYING");
        iy += fh + 2;
        if (active && ambient_get_track(track, sizeof(track)))
        {
            display->set_foreground(pal.fg);
            fit_text(display, track, width - 12);
            display->putsxy(6, iy, track);
            iy += fh + 2;
            if (ambient_get_dir(dir, sizeof(dir)))
            {
                display->set_foreground(pal.dim);
                fit_text(display, dir, width - 12);
                display->putsxy(6, iy, dir);
            }
            iy += fh + 2;
            if (ambient_get_index(&idx, &total))
                snprintf(line, sizeof(line), "%s - %d of %d%s",
                         ambient_mode_name(), idx, total,
                         ambient_is_paused() ? " - paused" : "");
            else
                snprintf(line, sizeof(line), "%s", ambient_mode_name());
            display->set_foreground(pal.accent);
            display->putsxy(6, iy, line);
        }
        else
        {
            display->set_foreground(pal.fg);
            display->putsxy(6, iy, "Nothing playing");
            iy += fh + 2;
            display->set_foreground(pal.dim);
            snprintf(line, sizeof(line), "Mode: %s", ambient_mode_name());
            display->putsxy(6, iy, line);
        }

        /* the rows */
        for (int r = 0; r < AROW_COUNT; r++)
        {
            int y = ry + r * row_h;
            bool is_sel = (r == sel);
            const char *label = NULL;

            if (is_sel)
            {
                display->set_foreground(pal.sel_bg);
                display->fillrect(4, y, width - 8, row_h - 2);
                display->set_foreground(pal.accent);
                display->drawrect(4, y, width - 8, row_h - 2);
            }
            if (r == AROW_VOLUME)
            {
                draw_volume_row(display, &pal, is_sel, 4, y, width - 8,
                                row_h - 2);
                continue;
            }
            switch (r)
            {
                case AROW_PAUSE:
                    label = ambient_is_paused() ? "Resume" : "Pause";
                    break;
                case AROW_STOP:   label = "Stop ambience"; break;
                case AROW_RECENT: label = "Recent"; break;
                case AROW_BROWSE: label = "Browse music"; break;
            }
            bool usable = active || r == AROW_RECENT || r == AROW_BROWSE;
            display->set_foreground(!usable ? pal.dim :
                                    is_sel ? pal.accent : pal.fg);
            display->putsxy(12, y + (row_h - 2 - fh) / 2, label);
            if (r == AROW_RECENT || r == AROW_BROWSE)
                display->putsxy(width - 8 - 8, y + (row_h - 2 - fh) / 2, ">");
        }

        /* footer */
        display->set_foreground(pal.dim);
        display->hline(0, width - 1, fy);
        display->putsxy(6, fy + 3, "L/R volume   MID select   BACK home");
        display->update();

        long button = button_get_w_tmo(HZ / 2);
        switch (button)
        {
            case BUTTON_UP:
            case BUTTON_UP | BUTTON_REPEAT:
                sel = (sel + AROW_COUNT - 1) % AROW_COUNT;
                break;
            case BUTTON_DOWN:
            case BUTTON_DOWN | BUTTON_REPEAT:
                sel = (sel + 1) % AROW_COUNT;
                break;
            case BUTTON_LEFT:
            case BUTTON_LEFT | BUTTON_REPEAT:
            case BUTTON_RIGHT:
            case BUTTON_RIGHT | BUTTON_REPEAT:
            {
                int v = ambient_get_volume();
                v += (button & BUTTON_RIGHT) ? 5 : -5;
                if (v < 0)
                    v = 0;
                if (v > 100)
                    v = 100;
                global_settings.ambient_volume = v;
                ambient_set_volume(v);
                break;
            }
            case BUTTON_SELECT | BUTTON_REL:
                switch (sel)
                {
                    case AROW_PAUSE:
                        if (ambient_is_active())
                        {
                            if (ambient_is_paused())
                                ambient_resume();
                            else
                                ambient_pause();
                            sleep(HZ / 20);
                        }
                        break;
                    case AROW_STOP:
                        if (ambient_is_active())
                        {
                            ambient_stop();
                            sleep(HZ / 20);
                        }
                        break;
                    case AROW_RECENT:
                        recent_picker();
                        display->set_viewport(vp);
                        action_wait_for_release();
                        sleep(HZ / 20);
                        break;
                    case AROW_BROWSE:
                    {
                        const char *dir = "/Music";
                        DIR *dp = opendir(dir);
                        if (dp)
                            closedir(dp);
                        else
                        {
                            splash(HZ * 2, "No /Music folder");
                            dir = "/";
                        }
                        root_menu_browse_folder(dir);
                        ambient_pick_mode = true;
                        ambience_reopen = true;
                        *result = GO_TO_FILEBROWSER;
                        leave = true;
                        done = true;
                        break;
                    }
                }
                break;
            case BUTTON_BACK | BUTTON_REL:
                done = true;
                break;
            case BUTTON_NONE:
                break;
            default:
                if (default_event_handler(button) == SYS_USB_CONNECTED)
                {
                    *result = GO_TO_ROOT;
                    leave = true;
                    done = true;
                }
                break;
        }
    }
    return leave;
}

/* ---- the screen -------------------------------------------------------- */

int homegrid_show(void)
{
    struct screen *display = &screens[SCREEN_MAIN];
    struct viewport vp, *last_vp;
    struct palette pal;
    int result = GO_TO_ROOT;
    bool done = false;

    /* the page draws everything itself: switch the theme's status bar and
       backdrop off while we are here */
    viewportmanager_theme_enable(SCREEN_MAIN, false, &vp);
    last_vp = display->set_viewport(&vp);
    display->scroll_stop();
    icons_load();
    action_wait_for_release();

    /* the ambience page's file browser was closed: back to that page */
    ambient_pick_mode = false;
    if (ambience_reopen)
    {
        ambience_reopen = false;
        selected_tile = TILE_AMBIENCE;
        if (ambience_page(display, &vp, &result))
            done = true;
        display->set_viewport(&vp);
    }

    while (!done)
    {
        palette_init(&pal);
        vp.fg_pattern = pal.fg;
        vp.bg_pattern = pal.bg;
        display->clear_viewport();

        int width = vp.width;
        int height = vp.height;
        int fh = display->getcharheight();
        int hh = fh + 6;            /* header height */
        int foot_h = fh + 9;        /* footer height */
        int fy = height - foot_h;   /* footer top */
        int gy = hh;                /* grid top */
        int gh = fy - gy;           /* grid height */
        const int pad = 4;
        int tw = (width - pad * (COLS + 1)) / COLS;
        int th = (gh - pad * (ROWS + 1)) / ROWS;

        draw_header(display, &pal, "Mp3 player", width, hh);
        for (int i = 0; i < NUM_TILES; i++)
        {
            int col = i % COLS, row = i / COLS;
            draw_tile(display, &pal, i, i == selected_tile,
                      pad + col * (tw + pad), gy + pad + row * (th + pad),
                      tw, th, fh);
        }
        draw_footer(display, &pal, width, fy, foot_h, fh);
        display->update();

        long button = button_get_w_tmo(HZ / 2);
        int col = selected_tile % COLS, row = selected_tile / COLS;

        switch (button)
        {
            case BUTTON_LEFT:
            case BUTTON_LEFT | BUTTON_REPEAT:
                col = (col + COLS - 1) % COLS;
                break;
            case BUTTON_RIGHT:
            case BUTTON_RIGHT | BUTTON_REPEAT:
                col = (col + 1) % COLS;
                break;
            case BUTTON_UP:
            case BUTTON_UP | BUTTON_REPEAT:
                row = (row + ROWS - 1) % ROWS;
                break;
            case BUTTON_DOWN:
            case BUTTON_DOWN | BUTTON_REPEAT:
                row = (row + 1) % ROWS;
                break;
            case BUTTON_SELECT | BUTTON_REL:
            {
                bool stay = false;
                if (selected_tile == TILE_AMBIENCE)
                {
                    if (ambience_page(display, &vp, &result))
                        done = true;
                    display->set_viewport(&vp);
                    action_wait_for_release();
                    break;
                }
                result = open_tile(selected_tile, &stay);
                if (stay)
                {
                    /* back from the sleep timer picker */
                    display->set_viewport(&vp);
                    action_wait_for_release();
                }
                else
                    done = true;
                break;
            }
            case BUTTON_BACK | BUTTON_REL:
                /* jump to Now Playing when there is something playing */
                if (audio_status() &
                    (AUDIO_STATUS_PLAY | AUDIO_STATUS_PAUSE))
                {
                    result = GO_TO_WPS;
                    done = true;
                }
                break;
            case BUTTON_NONE:
                break;
            default:
                if (default_event_handler(button) == SYS_USB_CONNECTED)
                {
                    result = GO_TO_ROOT;
                    done = true;
                }
                break;
        }
        selected_tile = row * COLS + col;
    }

    icons_free();
    display->set_viewport(last_vp);
    viewportmanager_theme_undo(SCREEN_MAIN, false);
    return result;
}

#endif /* HAVE_ICON_HOME */
