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
 * drawn in the theme colours.  Settings > Theme Settings > Home icon pack
 * picks one of the 48x48 icon packs (homegrid_packs.h, made by
 * tools/gen_homegrid_packs.py); two of them follow the theme colours, so
 * they turn light or dark with the theme.  A full-colour BMP named
 * /.rockbox/icons/home/<audiobooks|music|podcasts|ambience|sleep|settings|
 * shortcuts|mode>.bmp (up to 48x48, magenta 255,0,255 is transparent)
 * replaces any icon.
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
#include <stdlib.h>
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
#include "homegrid_packs.h"
#include "pathfuncs.h"
#include "bookmark.h"
#include "chapters.h"
#include "menu.h"
#include "playlist.h"
#include "filetypes.h"
#include "strnatcmp.h"
#include "recorder/bmp.h"
#ifdef HAVE_JPEG
#include "recorder/jpeg_load.h"
#endif
#include "string-extra.h"
#include "gui/list.h"

#define COLS 4
#define ROWS 2
#define NUM_TILES (COLS * ROWS)

#define ICON_SCALE 2                /* built-in icons are drawn at 2x */
#define BUILTIN_ICON_PX (HOMEGRID_ICON_SIZE * ICON_SCALE)
#define CUSTOM_ICON_MAX 48          /* largest custom icon (pixels) */
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

/* ---- icon packs -------------------------------------------------------- */

#define HOMEGRID_PACK_FILE ROCKBOX_DIR "/home_icon_pack.cfg"

/* 0 = built-in icons, 1..HOMEGRID_NUM_PACKS = homegrid_packs[n - 1] */
static int pack_sel = 0;
static bool pack_loaded = false;

static void pack_load(void)
{
    if (pack_loaded)
        return;
    pack_loaded = true;
    int fd = open(HOMEGRID_PACK_FILE, O_RDONLY);
    if (fd < 0)
        return;
    char buf[8];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return;
    buf[n] = '\0';
    int v = atoi(buf);
    if (v >= 0 && v <= HOMEGRID_NUM_PACKS)
        pack_sel = v;
}

static void pack_save(void)
{
    char buf[8];
    int fd = open(HOMEGRID_PACK_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    int n = snprintf(buf, sizeof(buf), "%d\n", pack_sel);
    write(fd, buf, n);
    close(fd);
}

static const struct icon_pack *current_pack(void)
{
    return (pack_sel > 0 && pack_sel <= HOMEGRID_NUM_PACKS)
           ? &homegrid_packs[pack_sel - 1] : NULL;
}

static bool pack_picked;

static int pack_action(int action, struct gui_synclist *lists)
{
    (void)lists;
    if (action == ACTION_STD_OK)
    {
        pack_picked = true;
        return ACTION_STD_CANCEL;   /* ends the list, selection stays set */
    }
    return action;
}

static const char *pack_get_name(int item, void *data, char *buf, size_t len)
{
    (void)data;
    const char *name = item == 0 ? "Built-in" : homegrid_packs[item - 1].name;
    snprintf(buf, len, "%s%s", item == pack_sel ? "> " : "", name);
    return buf;
}

/* ---- the pack as a whole Rockbox theme ------------------------------------
 * Choosing a pack writes a small theme (colours, selector bar, status bar and
 * a Now Playing skin) from the data in the pack and loads it like any other
 * theme, so lists, menus, settings, the file browser, the status bar and
 * Now Playing all follow the pack. */

#define PACK_THEME_CFG ROCKBOX_DIR "/pack_theme.cfg"
#define PACK_THEME_WPS ROCKBOX_DIR "/wps/pack_theme.wps"

/* pct percent of colour b over a, both 0xRRGGBB */
static unsigned long mix_rgb(unsigned long a, unsigned long b, int pct)
{
    unsigned long out = 0;
    for (int sh = 16; sh >= 0; sh -= 8)
    {
        unsigned long x = (a >> sh) & 0xff, y = (b >> sh) & 0xff;
        out |= ((x * (100 - pct) + y * pct) / 100) << sh;
    }
    return out;
}

static bool pack_write_file(const char *name, const char *text)
{
    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    int len = strlen(text);
    bool ok = (write(fd, text, len) == len);
    close(fd);
    return ok;
}

static bool pack_apply_theme(const struct icon_pack *pk)
{
    char buf[1536];

    mkdir(ROCKBOX_DIR "/wps");   /* fails harmlessly when it exists */
    unsigned long dim = mix_rgb(pk->t_bg, pk->t_fg, 55);

    /* Now Playing skin: art on the left, title, artist, album, times,
       progress bar and a status line, under a header in the pack's colours */
    snprintf(buf, sizeof(buf),
        "# made by the icon home, pack %s\n"
        "%%wd\n"
        "%%V(0,0,320,24,1)\n%%Vb(%06lX)\n%%Vf(%06lX)\n%%al Now playing\n"
        "%%V(236,0,80,24,1)\n%%Vb(%06lX)\n%%Vf(%06lX)\n%%ar%%cH:%%cM\n"
        "%%V(12,34,120,120,-)\n%%Cl(0,0,120,120,c,c)\n%%?C<%%Cd|NO ART>\n"
        "%%V(14,196,292,7,-)\n%%Vf(%06lX)\n%%pb(0,0,292,7,noborder)\n"
        "%%V(148,45,164,18,1)\n%%Vf(%06lX)\n%%s%%?it<%%it|%%fn>\n"
        "%%V(148,83,164,16,1)\n%%Vf(%06lX)\n%%s%%?ia<%%ia|%%?iA<%%iA|Unknown artist>>\n"
        "%%V(148,117,164,16,1)\n%%Vf(%06lX)\n%%s%%?id<%%id|%%?d(1)<%%d(1)|Unknown album>>\n"
        "%%V(148,140,164,14,1)\n%%Vf(%06lX)\n%%fc  %%fb kbps\n"
        "%%V(12,176,100,14,1)\n%%Vf(%06lX)\n%%pc\n"
        "%%V(208,176,100,14,1)\n%%Vf(%06lX)\n%%ar-%%pr\n"
        "%%V(12,214,296,14,1)\n%%Vf(%06lX)\n"
        "%%?mp<[STOP]|[PLAY]|[PAUSE]|[FF]|[REW]>  VOL %%pv dB\n",
        pk->name,
        pk->hdr1, pk->hdr_fg, pk->hdr1, pk->hdr_fg,
        pk->t_fg, pk->t_fg, dim, dim, dim,
        pk->t_fg, pk->t_fg, dim);
    if (!pack_write_file(PACK_THEME_WPS, buf))
        return false;

    snprintf(buf, sizeof(buf),
        "# made by the icon home, pack %s\n"
        "foreground color: %06lX\n"
        "background color: %06lX\n"
        "selector type: %s\n"
        "line selector start color: %06lX\n"
        "line selector end color: %06lX\n"
        "line selector text color: %06lX\n"
        "list separator color: %06lX\n"
        "statusbar: top\n"
        "backdrop: -\n"
        "filetype colours: -\n"
        "wps: " PACK_THEME_WPS "\n",
        pk->name, pk->t_fg, pk->t_bg,
        pk->t_grad ? "bar (gradient)" : "bar (color)",
        pk->t_sel1, pk->t_sel2, pk->t_seltxt,
        mix_rgb(pk->t_bg, pk->t_fg, 25));
    if (!pack_write_file(PACK_THEME_CFG, buf))
        return false;
    return settings_load_config(PACK_THEME_CFG, true);
}

/* Settings > Theme Settings > Home icon pack */
int homegrid_pack_menu(void)
{
    struct simplelist_info info;

    pack_load();
    pack_picked = false;
    simplelist_info_init(&info, "Home icon pack", HOMEGRID_NUM_PACKS + 1, NULL);
    info.get_name = pack_get_name;
    info.action_callback = pack_action;
    info.selection = pack_sel;
    if (simplelist_show_list(&info) || !pack_picked ||
        info.selection < 0 || info.selection > HOMEGRID_NUM_PACKS)
        return 0;
    pack_sel = info.selection;
    pack_save();
    if (pack_sel > 0)
    {
        splash(0, ID2P(LANG_WAIT));
        if (!pack_apply_theme(&homegrid_packs[pack_sel - 1]))
            splash(HZ * 2, "Could not apply the theme");
    }
    return 0;
}

/* ---- colours ----------------------------------------------------------- */

struct palette
{
    unsigned fg;      /* normal text and icons */
    unsigned bg;      /* page background */
    unsigned dim;     /* secondary text, separators */
    unsigned accent;  /* selection frame and progress bars */
    unsigned sel_bg;  /* fill behind the selected tile */
    bool hdr_on;      /* header band with its own colours (icon packs) */
    unsigned hdr1, hdr2, hdr_fg;
};

static unsigned hex_color(unsigned long c)
{
    return LCD_RGBPACK((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
}

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
    p->hdr_on = false;

    /* an icon pack with its own look sets the page colours */
    const struct icon_pack *pk = current_pack();
    if (pk && pk->own_colors)
    {
        p->bg = hex_color(pk->bg);
        p->fg = hex_color(pk->fg);
        p->dim = hex_color(pk->dim);
        p->accent = hex_color(pk->accent);
        p->sel_bg = hex_color(pk->sel_bg);
        p->hdr_on = true;
        p->hdr1 = hex_color(pk->hdr1);
        p->hdr2 = hex_color(pk->hdr2);
        p->hdr_fg = hex_color(pk->hdr_fg);
    }
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

/* a picture of an icon pack; selected tiles use the selection colours */
static void draw_pack_icon(struct screen *d, const struct palette *pal,
                           const struct icon_pack *pk, int idx, bool selected,
                           int x, int y)
{
    unsigned fgc = selected ? pal->accent : pal->fg;
    unsigned bgc = selected ? pal->sel_bg : pal->bg;
    unsigned lut[64];
    int n = pk->npal > 63 ? 63 : pk->npal;

    for (int i = 0; i < n; i++)
    {
        unsigned long e = pk->pal[i];
        switch (e >> 24)
        {
            case 1:  lut[i + 1] = fgc; break;
            case 2:  lut[i + 1] = bgc; break;
            case 3:  lut[i + 1] = blend(bgc, fgc, 33); break;
            case 4:  lut[i + 1] = blend(bgc, fgc, 67); break;
            default: lut[i + 1] = hex_color(e & 0xffffff); break;
        }
    }

    const unsigned char *pix = pk->pix + (size_t)idx * pk->size * pk->size;
    int sc = pk->scale;
    for (int row = 0; row < pk->size; row++)
    {
        int col = 0;
        while (col < pk->size)
        {
            unsigned char c = pix[row * pk->size + col];
            if (c == 0 || c > n)
            {
                col++;
                continue;
            }
            int start = col;
            while (col < pk->size && pix[row * pk->size + col] == c)
                col++;
            d->set_foreground(lut[c]);
            d->fillrect(x + start * sc, y + row * sc, (col - start) * sc, sc);
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

/* text without a background box, so it is readable on any fill */
static void put_text(struct screen *d, int x, int y, const char *s)
{
    d->set_drawmode(DRMODE_FG);
    d->putsxy(x, y, s);
    d->set_drawmode(DRMODE_SOLID);
}

static int luma(unsigned c)
{
    return (RGB_UNPACK_RED(c) * 30 + RGB_UNPACK_GREEN(c) * 59 +
            RGB_UNPACK_BLUE(c) * 11) / 100;
}

/* colour of the label under the selected icon: the accent when it can be
   read on the selection fill, else black or white */
static unsigned label_color(const struct palette *pal, bool selected)
{
    if (!selected)
        return pal->fg;
    int diff = luma(pal->accent) - luma(pal->sel_bg);
    if (diff < 0)
        diff = -diff;
    if (diff >= 90)
        return pal->accent;
    return luma(pal->sel_bg) < 128 ? LCD_RGBPACK(255, 255, 255)
                                   : LCD_RGBPACK(0, 0, 0);
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
    else if (current_pack())
    {
        const struct icon_pack *pk = current_pack();
        int px = pk->size * pk->scale;
        draw_pack_icon(d, pal, pk, idx, selected,
                       ix + (ICON_BOX - px) / 2, by + (ICON_BOX - px) / 2);
        d->set_foreground(selected ? pal->accent : pal->fg);
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
    d->set_foreground(label_color(pal, selected));
    d->getstringsize(label, &w, NULL);
    put_text(d, tx + (tw - w) / 2, by + ICON_BOX + 2, label);
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

    if (pal->hdr_on)
    {
        for (int x = 0; x < width; x++)
        {
            d->set_foreground(blend(pal->hdr1, pal->hdr2,
                                    width > 1 ? x * 100 / (width - 1) : 0));
            d->vline(x, 0, hh - 2);
        }
    }

    d->set_foreground(pal->hdr_on ? pal->hdr_fg : pal->fg);
    d->getstringsize(title, &w, &h);
    put_text(d, 6, (hh - h) / 2, title);

    format_clock(tmp, sizeof(tmp));
    int level = battery_level();
    if (level >= 0)
        snprintf(buf, sizeof(buf), "%d%%", level);
    else
        buf[0] = '\0';

    d->set_foreground(pal->hdr_on ? pal->hdr_fg : pal->dim);
    int x = width - 6;
    if (tmp[0])
    {
        d->getstringsize(tmp, &w, &h);
        x -= w;
        put_text(d, x, (hh - h) / 2, tmp);
        x -= 10;
    }
    if (buf[0])
    {
        d->getstringsize(buf, &w, &h);
        x -= w;
        put_text(d, x, (hh - h) / 2, buf);
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
        put_text(d, right, ty, "AMB");
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
        put_text(d, right, ty, tbuf);
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
        put_text(d, 6, ty, text);

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
        put_text(d, 6, ty, "Nothing playing");
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
            return GO_TO_ROOT; /* handled by homegrid_show() */
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
    AROW_TRACK,
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
    put_text(d, x + 6, y + (h - th) / 2, "Volume");

    snprintf(pct, sizeof(pct), "%d%%", vol);
    d->getstringsize(pct, &tw, &th);
    put_text(d, x + w - 6 - tw, y + (h - th) / 2, pct);

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

/* the previous / next track row */
static void draw_track_row(struct screen *d, const struct palette *pal,
                           bool selected, bool usable, int x, int y, int w,
                           int h)
{
    char mid[24];
    int tw, th, idx, total;

    d->set_foreground(!usable ? pal->dim : selected ? pal->accent : pal->fg);
    d->getstringsize("<", &tw, &th);
    put_text(d, x + 8, y + (h - th) / 2, "< Prev");
    d->getstringsize("Next >", &tw, NULL);
    put_text(d, x + w - 8 - tw, y + (h - th) / 2, "Next >");

    if (usable && ambient_get_index(&idx, &total))
        snprintf(mid, sizeof(mid), "%d of %d", idx, total);
    else
        snprintf(mid, sizeof(mid), "Track");
    d->getstringsize(mid, &tw, NULL);
    put_text(d, x + (w - tw) / 2, y + (h - th) / 2, mid);
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
        int row_h = fh + 10;
        /* the info block takes 4 lines under the header; make the rows
           smaller when the font is big */
        int info_bottom = hh + 4 + 4 * (fh + 2);
        int avail = fy - 4 - info_bottom;
        if (row_h * AROW_COUNT > avail)
            row_h = avail / AROW_COUNT;
        if (row_h < fh + 2)
            row_h = fh + 2;
        int ry = fy - AROW_COUNT * row_h - 2;
        char track[MAX_PATH], dir[MAX_PATH], line[64];
        int idx, total;
        bool active = ambient_is_active();

        draw_header(display, &pal, "Ambience", width, hh);

        /* what is playing */
        int iy = hh + 4;
        display->set_foreground(pal.dim);
        put_text(display, 6, iy, "NOW PLAYING");
        iy += fh + 2;
        if (active && ambient_get_track(track, sizeof(track)))
        {
            display->set_foreground(pal.fg);
            fit_text(display, track, width - 12);
            put_text(display, 6, iy, track);
            iy += fh + 2;
            if (ambient_get_dir(dir, sizeof(dir)))
            {
                display->set_foreground(pal.dim);
                fit_text(display, dir, width - 12);
                put_text(display, 6, iy, dir);
            }
            iy += fh + 2;
            if (ambient_get_index(&idx, &total))
                snprintf(line, sizeof(line), "%s - %d of %d%s",
                         ambient_mode_name(), idx, total,
                         ambient_is_paused() ? " - paused" : "");
            else
                snprintf(line, sizeof(line), "%s", ambient_mode_name());
            display->set_foreground(pal.accent);
            put_text(display, 6, iy, line);
        }
        else
        {
            display->set_foreground(pal.fg);
            put_text(display, 6, iy, "Nothing playing");
            iy += fh + 2;
            display->set_foreground(pal.dim);
            snprintf(line, sizeof(line), "Mode: %s", ambient_mode_name());
            put_text(display, 6, iy, line);
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
            if (r == AROW_TRACK)
            {
                draw_track_row(display, &pal, is_sel, active, 4, y,
                               width - 8, row_h - 2);
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
            put_text(display, 12, y + (row_h - 2 - fh) / 2, label);
            if (r == AROW_RECENT || r == AROW_BROWSE)
                put_text(display, width - 8 - 8, y + (row_h - 2 - fh) / 2, ">");
        }

        /* footer */
        display->set_foreground(pal.dim);
        display->hline(0, width - 1, fy);
        put_text(display, 6, fy + 3, "L/R change   MID select   BACK home");
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
            case BUTTON_RIGHT:
                if (sel == AROW_TRACK && ambient_is_active())
                {
                    if (button == BUTTON_RIGHT)
                        ambient_next();
                    else
                        ambient_prev();
                    sleep(HZ / 20);
                    break;
                }
                if (sel != AROW_VOLUME)
                    break;
                /* fall through */
            case BUTTON_LEFT | BUTTON_REPEAT:
            case BUTTON_RIGHT | BUTTON_REPEAT:
            {
                if (sel != AROW_VOLUME)
                    break; /* no auto-repeat track skipping */
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
                    case AROW_TRACK:
                        if (ambient_is_active())
                        {
                            ambient_next();
                            sleep(HZ / 20);
                        }
                        break;
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

#include "homegrid_books.h"

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
    pack_load();
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
                if (selected_tile == TILE_AMBIENCE ||
                    selected_tile == TILE_AUDIOBOOKS)
                {
                    bool leave_home;
                    if (selected_tile == TILE_AMBIENCE)
                        leave_home = ambience_page(display, &vp, &result);
                    else
                        leave_home = books_page(display, &vp, &result);
                    if (leave_home)
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
            case BUTTON_PLAYPAUSE | BUTTON_REL:
                /* Play opens the Now Playing screen */
                if (audio_status() &
                    (AUDIO_STATUS_PLAY | AUDIO_STATUS_PAUSE))
                {
                    result = GO_TO_WPS;
                    done = true;
                }
                else
                    splash(HZ, "Nothing playing");
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
