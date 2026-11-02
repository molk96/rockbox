/***************************************************************************
 * Icon home: the Video page (Creative Zen X-Fi).  Included by homegrid.c.
 *
 * Lists /Videos with sub folders (series, seasons).  Folders come first,
 * then videos, both sorted A to Z.  Every row has two lines: the title and
 * "length, quality, size" (read from the <name>.txt file that
 * rb_video_convert.py writes next to each <name>.mpg) and a small picture
 * (<name>.thb from the same program, else a drawn icon).  The top row is "Continue"
 * (the video played last) or, inside a folder, "Next up".
 *
 * A video is started in the mpegplayer plugin.  The page tells the plugin
 * where to start and which files come after this one through
 * /.rockbox/video_launch.txt (see video_pos.h); the plugin writes the
 * position back to /.rockbox/video_pos.bin, which this page reads to show
 * progress bars and "watched" ticks.  Colours come from the same palette as
 * the rest of the icon home, so the page follows the chosen icon pack.
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
#include "video_pos.h"

#define VD_ROOT     "/Videos"
#define VD_MAX      120
#define VD_POOL     8192

struct vd_ent
{
    const char *name;           /* file or folder name */
    bool dir;
    bool tried;                 /* info (file) or counts (folder) read */
    bool has_info;              /* the .txt file was found */
    unsigned long size;         /* bytes (files) */
    unsigned long len_s;        /* seconds, 0 = unknown */
    char quality[12];
    int nvid;                   /* folders: videos inside */
    int nwatched;
    unsigned long total_s;      /* folders: their total length */
};

struct vd_list
{
    struct vd_ent ent[VD_MAX];
    int n;
    char pool[VD_POOL];
    size_t used;
};

static struct vd_list vd_cur;   /* the folder that is shown */
static struct vd_list vd_tmp;   /* scratch: the folder of a launched video */
static char vd_dir[MAX_PATH] = VD_ROOT;
static bool vd_root_missing;

/* the details of one video, read from its .txt file */
struct vd_info
{
    bool found;
    char title[72], source[72], picture[40], quality[24], audio[32],
         subs[56], conv[24];
    unsigned long len_s;
};


/* the "Continue" / "Next up" row */
static struct
{
    bool valid;
    bool next_up;               /* in a folder: shown as "Next up" */
    char path[MAX_PATH];
    unsigned long pos_s, len_s;
} vd_cont;

static struct video_posrec vd_rec[VIDEO_POS_MAX];
static int vd_nrec;

static char vd_buf[3072];       /* the launch file */

/* ---- helpers ------------------------------------------------------------ */

static bool vd_is_video(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext)
        return false;
    ext++;
    return !strcasecmp(ext, "mpg") || !strcasecmp(ext, "mpeg") ||
           !strcasecmp(ext, "mpv") || !strcasecmp(ext, "m2v");
}

static void vd_join(char *out, size_t size, const char *dir, const char *name)
{
    snprintf(out, size, "%s/%s", dir, name);
}

/* name.mpg -> name.txt */
static void vd_txt_path(const char *vpath, char *out, size_t size)
{
    strlcpy(out, vpath, size);
    char *dot = strrchr(out, '.');
    char *slash = strrchr(out, '/');
    if (dot && (!slash || dot > slash))
        *dot = '\0';
    strlcat(out, ".txt", size);
}

/* file name without folder and extension */
static void vd_stem(const char *path, char *out, size_t size)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    strlcpy(out, base, size);
    char *dot = strrchr(out, '.');
    if (dot && dot != out)
        *dot = '\0';
}

static void vd_fmt_len(char *buf, size_t size, unsigned long s)
{
    if (s >= 3600)
        snprintf(buf, size, "%luh %02lum", s / 3600, (s / 60) % 60);
    else if (s >= 60)
        snprintf(buf, size, "%lum", s / 60);
    else
        snprintf(buf, size, "%lus", s);
}

static void vd_fmt_size(char *buf, size_t size, unsigned long bytes)
{
    unsigned long mb = bytes / 1000000ul;
    if (mb >= 1000)
        snprintf(buf, size, "%lu.%lu GB", mb / 1000, (mb % 1000) / 100);
    else
        snprintf(buf, size, "%lu MB", mb ? mb : 1);
}

/* "1h 42m 05s" -> seconds */
static unsigned long vd_parse_len(const char *s)
{
    unsigned long total = 0, num = 0;
    for (; *s; s++)
    {
        if (*s >= '0' && *s <= '9')
            num = num * 10 + (*s - '0');
        else if (*s == 'h')
        {
            total += num * 3600;
            num = 0;
        }
        else if (*s == 'm')
        {
            total += num * 60;
            num = 0;
        }
        else if (*s == 's')
        {
            total += num;
            num = 0;
        }
    }
    return total;
}

/* cut "Balanced (800k video, 128k audio)" at the bracket */
static void vd_cut_bracket(char *s)
{
    char *p = strstr(s, " (");
    if (p)
        *p = '\0';
}

/* ---- position records ------------------------------------------------------ */

static void vd_rec_load(void)
{
    vd_nrec = 0;
    int fd = open(VIDEO_POS_FILE, O_RDONLY);
    if (fd < 0)
        return;
    int n = read(fd, vd_rec, sizeof(vd_rec));
    close(fd);
    if (n > 0)
        vd_nrec = n / (int)sizeof(struct video_posrec);
}

static const struct video_posrec *vd_find(const char *path)
{
    unsigned int h = video_path_hash(path);
    for (int i = 0; i < vd_nrec; i++)
        if (vd_rec[i].hash == h)
            return &vd_rec[i];
    return NULL;
}

/* ---- the .txt file next to a video ----------------------------------------- */

static void vd_read_info(const char *vpath, struct vd_info *in)
{
    char txt[MAX_PATH], buf[704];

    memset(in, 0, sizeof(*in));
    vd_txt_path(vpath, txt, sizeof(txt));
    int fd = open(txt, O_RDONLY);
    if (fd < 0)
        return;
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return;
    buf[n] = '\0';
    in->found = true;

    char *line = buf;
    while (line && *line)
    {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl++ = '\0';
        char *cr = strchr(line, '\r');
        if (cr)
            *cr = '\0';
        char *colon = strchr(line, ':');
        if (colon)
        {
            *colon++ = '\0';
            while (*colon == ' ')
                colon++;
            if (!strcmp(line, "Title"))
                strlcpy(in->title, colon, sizeof(in->title));
            else if (!strcmp(line, "Source"))
                strlcpy(in->source, colon, sizeof(in->source));
            else if (!strcmp(line, "Length"))
                in->len_s = vd_parse_len(colon);
            else if (!strcmp(line, "Picture"))
                strlcpy(in->picture, colon, sizeof(in->picture));
            else if (!strcmp(line, "Quality"))
            {
                strlcpy(in->quality, colon, sizeof(in->quality));
                vd_cut_bracket(in->quality);
            }
            else if (!strcmp(line, "Audio"))
            {
                strlcpy(in->audio, colon, sizeof(in->audio));
                vd_cut_bracket(in->audio);
            }
            else if (!strcmp(line, "Subtitles"))
            {
                strlcpy(in->subs, colon, sizeof(in->subs));
                vd_cut_bracket(in->subs);
            }
            else if (!strcmp(line, "Converted"))
            {
                strlcpy(in->conv, colon, 11);   /* the date only */
            }
        }
        line = nl;
    }
}

/* ---- the folder listing ------------------------------------------------------- */

static bool vd_list_add(struct vd_list *l, const char *name, bool dir,
                        unsigned long size)
{
    if (l->n >= VD_MAX)
        return false;
    size_t n = strlen(name) + 1;
    if (l->used + n > sizeof(l->pool))
        return false;
    char *p = l->pool + l->used;
    memcpy(p, name, n);
    l->used += n;
    struct vd_ent *e = &l->ent[l->n++];
    memset(e, 0, sizeof(*e));
    e->name = p;
    e->dir = dir;
    e->size = size;
    return true;
}

static int vd_ent_cmp(const void *a, const void *b)
{
    const struct vd_ent *x = a, *y = b;
    if (x->dir != y->dir)
        return x->dir ? -1 : 1;
    return strnatcasecmp(x->name, y->name);
}

/* folders first, then videos, both A to Z */
static bool vd_scan(struct vd_list *l, const char *dir)
{
    l->n = 0;
    l->used = 0;
    DIR *d = opendir(dir);
    if (!d)
        return false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.')
            continue;
        struct dirinfo info = dir_get_info(d, e);
        if (info.attribute & ATTR_DIRECTORY)
            vd_list_add(l, e->d_name, true, 0);
        else if (vd_is_video(e->d_name))
            vd_list_add(l, e->d_name, false, (unsigned long)info.size);
    }
    closedir(d);
    qsort(l->ent, l->n, sizeof(l->ent[0]), vd_ent_cmp);
    return true;
}

struct vd_stat
{
    int n, watched;
    unsigned long len_s;
};

/* videos in dir and in the folders below it (two levels) */
static void vd_dir_stats(const char *dir, int depth, struct vd_stat *st)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char path[MAX_PATH];

    if (!d)
        return;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.')
            continue;
        struct dirinfo info = dir_get_info(d, e);
        vd_join(path, sizeof(path), dir, e->d_name);
        if (info.attribute & ATTR_DIRECTORY)
        {
            if (depth < 2)
                vd_dir_stats(path, depth + 1, st);
        }
        else if (vd_is_video(e->d_name))
        {
            struct vd_info inf;
            const struct video_posrec *r;

            st->n++;
            vd_read_info(path, &inf);
            st->len_s += inf.len_s;
            r = vd_find(path);
            if (r && video_rec_watched(r))
                st->watched++;
        }
    }
    closedir(d);
}

/* row details of one entry: file = length and quality from its .txt file,
   folder = counts (can take a moment: done when the page is idle) */
static void vd_ent_fill(struct vd_ent *e)
{
    char path[MAX_PATH];

    if (e->tried)
        return;
    e->tried = true;
    vd_join(path, sizeof(path), vd_dir, e->name);
    if (e->dir)
    {
        struct vd_stat st = { 0, 0, 0 };
        vd_dir_stats(path, 0, &st);
        e->nvid = st.n;
        e->nwatched = st.watched;
        e->total_s = st.len_s;
        e->has_info = true;
    }
    else
    {
        struct vd_info inf;
        vd_read_info(path, &inf);
        e->has_info = inf.found;
        e->len_s = inf.len_s;
        strlcpy(e->quality, inf.quality, sizeof(e->quality));
    }
}

/* ---- Continue / Next up ----------------------------------------------------------- */

static void vd_cont_compute(void)
{
    vd_cont.valid = false;

    if (!strcmp(vd_dir, VD_ROOT))
    {
        /* the video played last, when it was left half way */
        char last[MAX_PATH];
        int fd = open(VIDEO_LAST_FILE, O_RDONLY);
        if (fd < 0)
            return;
        int n = read(fd, last, sizeof(last) - 1);
        close(fd);
        if (n <= 0)
            return;
        last[n] = '\0';
        char *nl = strchr(last, '\n');
        if (nl)
            *nl = '\0';
        const struct video_posrec *r = vd_find(last);
        if (!r || !video_rec_partial(r) || !file_exists(last))
            return;
        strlcpy(vd_cont.path, last, sizeof(vd_cont.path));
        vd_cont.pos_s = r->pos_s;
        vd_cont.len_s = r->dur_s;
        vd_cont.next_up = false;
        vd_cont.valid = true;
        return;
    }

    /* inside a folder: the video that was left half way, else the first
       one after the one watched last that is not watched yet */
    int best = -1, last_played = -1;
    unsigned int best_stamp = 0, last_stamp = 0;
    char path[MAX_PATH];

    for (int i = 0; i < vd_cur.n; i++)
    {
        if (vd_cur.ent[i].dir)
            continue;
        vd_join(path, sizeof(path), vd_dir, vd_cur.ent[i].name);
        const struct video_posrec *r = vd_find(path);
        if (!r)
            continue;
        if (video_rec_partial(r) && (best < 0 || r->stamp > best_stamp))
        {
            best = i;
            best_stamp = r->stamp;
        }
        if (last_played < 0 || r->stamp > last_stamp)
        {
            last_played = i;
            last_stamp = r->stamp;
        }
    }
    if (best < 0 && last_played >= 0)
    {
        for (int i = last_played + 1; i < vd_cur.n; i++)
        {
            if (vd_cur.ent[i].dir)
                continue;
            vd_join(path, sizeof(path), vd_dir, vd_cur.ent[i].name);
            const struct video_posrec *r = vd_find(path);
            if (!r || !video_rec_watched(r))
            {
                best = i;
                break;
            }
        }
    }
    if (best < 0)
        return;
    vd_join(vd_cont.path, sizeof(vd_cont.path), vd_dir,
            vd_cur.ent[best].name);
    const struct video_posrec *r = vd_find(vd_cont.path);
    vd_cont.pos_s = (r && video_rec_partial(r)) ? r->pos_s : 0;
    vd_cont.len_s = r ? r->dur_s : 0;
    if (!vd_cont.len_s)
    {
        struct vd_info inf;
        vd_read_info(vd_cont.path, &inf);
        vd_cont.len_s = inf.len_s;
    }
    vd_cont.next_up = true;
    vd_cont.valid = true;
}

/* ---- starting a video ----------------------------------------------------------------- */

static unsigned long vd_hex(unsigned c)
{
    return ((unsigned long)RGB_UNPACK_RED(c) << 16) |
           ((unsigned long)RGB_UNPACK_GREEN(c) << 8) | RGB_UNPACK_BLUE(c);
}

/* Writes the launch file and starts the plugin.  Returns the plugin's
   return code. */
static int vd_launch(const struct palette *pal, const char *path,
                     unsigned long start_s)
{
    char dir[MAX_PATH];
    int pos;

    strlcpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    const char *base = path;
    if (slash)
    {
        base = path + (slash - dir) + 1;
        *slash = '\0';
    }

    /* series folder (not /Videos itself): the next video starts by itself,
       so the plugin gets the files that follow this one */
    bool series = strcmp(dir, VD_ROOT) != 0 && vd_scan(&vd_tmp, dir);
    int first_next = -1;
    if (series)
    {
        for (int i = 0; i < vd_tmp.n; i++)
            if (!vd_tmp.ent[i].dir && !strcmp(vd_tmp.ent[i].name, base))
            {
                first_next = i + 1;
                break;
            }
        series = first_next >= 0;
    }

    pos = snprintf(vd_buf, sizeof(vd_buf), "%s\n%lu\n%d\n%06lX %06lX %06lX "
                   "%06lX %06lX %06lX\n", path, start_s, series ? 1 : 0,
                   vd_hex(pal->fg), vd_hex(pal->bg), vd_hex(pal->accent),
                   vd_hex(pal->sel_bg),
                   vd_hex(pal->hdr_on ? pal->hdr1 : pal->sel_bg),
                   vd_hex(pal->hdr_on ? pal->hdr_fg : pal->fg));
    if (series)
    {
        int added = 0;
        for (int i = first_next; i < vd_tmp.n && added < VIDEO_NEXT_MAX; i++)
        {
            if (vd_tmp.ent[i].dir)
                continue;
            int n = snprintf(vd_buf + pos, sizeof(vd_buf) - pos, "%s/%s\n",
                             dir, vd_tmp.ent[i].name);
            if (n < 0 || pos + n >= (int)sizeof(vd_buf))
                break;
            pos += n;
            added++;
        }
    }

    int fd = open(VIDEO_LAUNCH_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0)
    {
        write(fd, vd_buf, pos);
        close(fd);
    }
    fd = open(VIDEO_LAST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0)
    {
        write(fd, path, strlen(path));
        write(fd, "\n", 1);
        close(fd);
    }
    return filetype_load_plugin("mpegplayer", path);
}

/* ---- thumbnails ---------------------------------------------------------------------------
 * <name>.thb next to the video, made by rb_video_convert.py:
 *   "THB1", width, height (one byte each, at most VD_TH_W x VD_TH_H), then
 *   width*height RGB triplets. A few of them are kept in memory. */

#define VD_TH_W     48
#define VD_TH_H     36
#define VD_TH_SLOTS 10

struct vd_th_meta
{
    unsigned hash;              /* 0 = slot free */
    unsigned stamp;
    unsigned char w, h;
    bool ok;
};

static int vd_th_handle;
static struct vd_th_meta vd_th[VD_TH_SLOTS];
static unsigned vd_th_clock;
static unsigned char vd_th_raw[VD_TH_W * VD_TH_H * 3];

static void vd_th_free(void)
{
    if (vd_th_handle > 0)
    {
        core_unpin(vd_th_handle);
        core_free(vd_th_handle);
    }
    vd_th_handle = 0;
}

static void vd_th_init(void)
{
    vd_th_free();
    memset(vd_th, 0, sizeof(vd_th));
    vd_th_handle = core_alloc((size_t)VD_TH_SLOTS * VD_TH_W * VD_TH_H *
                              sizeof(fb_data));
    if (vd_th_handle > 0)
        core_pin(vd_th_handle);
    else
        vd_th_handle = 0;
}

/* the picture of a video, or NULL when it has none */
static const fb_data *vd_th_get(const char *vpath, int *w, int *h)
{
    if (vd_th_handle <= 0)
        return NULL;

    unsigned hash = video_path_hash(vpath);
    int slot = -1;
    for (int i = 0; i < VD_TH_SLOTS; i++)
        if (vd_th[i].hash == hash)
        {
            slot = i;
            break;
        }

    fb_data *base = (fb_data *)core_get_data(vd_th_handle);
    if (slot >= 0)
    {
        vd_th[slot].stamp = ++vd_th_clock;
        if (!vd_th[slot].ok)
            return NULL;
        *w = vd_th[slot].w;
        *h = vd_th[slot].h;
        return base + (size_t)slot * VD_TH_W * VD_TH_H;
    }

    /* not loaded: take a free slot, else the one used longest ago */
    slot = 0;
    for (int i = 0; i < VD_TH_SLOTS; i++)
    {
        if (!vd_th[i].hash)
        {
            slot = i;
            break;
        }
        if (vd_th[i].stamp < vd_th[slot].stamp)
            slot = i;
    }
    vd_th[slot].hash = hash;
    vd_th[slot].stamp = ++vd_th_clock;
    vd_th[slot].ok = false;

    char name[MAX_PATH];
    strlcpy(name, vpath, sizeof(name));
    char *dot = strrchr(name, '.');
    char *slash = strrchr(name, '/');
    if (dot && (!slash || dot > slash))
        *dot = '\0';
    strlcat(name, ".thb", sizeof(name));

    int fd = open(name, O_RDONLY);
    if (fd < 0)
        return NULL;
    unsigned char hd[6];
    if (read(fd, hd, 6) == 6 && !memcmp(hd, "THB1", 4) &&
        hd[4] > 0 && hd[4] <= VD_TH_W && hd[5] > 0 && hd[5] <= VD_TH_H)
    {
        int n = hd[4] * hd[5];
        if (read(fd, vd_th_raw, n * 3) == n * 3)
        {
            fb_data *pix = base + (size_t)slot * VD_TH_W * VD_TH_H;
            for (int i = 0; i < n; i++)
                pix[i] = FB_RGBPACK(vd_th_raw[3 * i], vd_th_raw[3 * i + 1],
                                    vd_th_raw[3 * i + 2]);
            vd_th[slot].w = hd[4];
            vd_th[slot].h = hd[5];
            vd_th[slot].ok = true;
        }
    }
    close(fd);
    if (!vd_th[slot].ok)
        return NULL;
    *w = vd_th[slot].w;
    *h = vd_th[slot].h;
    return base + (size_t)slot * VD_TH_W * VD_TH_H;
}

/* ---- drawing ------------------------------------------------------------------------------- */

/* small pictures drawn in the row colour, so they follow the pack */
static void vd_draw_folder_icon(struct screen *d, int x, int y)
{
    d->fillrect(x, y, 7, 3);
    d->fillrect(x, y + 3, 17, 11);
}

static void vd_draw_video_icon(struct screen *d, int x, int y)
{
    d->drawrect(x, y, 17, 14);
    for (int i = 0; i < 4; i++)
        d->vline(x + 6 + i, y + 3 + i, y + 10 - i);
}

static void vd_draw_tick(struct screen *d, int x, int y)
{
    d->drawline(x, y + 4, x + 3, y + 7);
    d->drawline(x + 3, y + 7, x + 9, y);
    d->drawline(x, y + 5, x + 3, y + 8);
    d->drawline(x + 3, y + 8, x + 9, y + 1);
}

static void vd_draw_progress(struct screen *d, const struct palette *pal,
                             int x, int y, int w, int h, int pct)
{
    d->set_foreground(pal->dim);
    d->drawrect(x, y, w, h);
    d->set_foreground(pal->accent);
    d->fillrect(x + 1, y + 1, (w - 2) * pct / 100, h - 2);
}

static int vd_pct(unsigned long pos, unsigned long len)
{
    if (!len)
        return 0;
    unsigned long p = pos * 100 / len;
    return p > 100 ? 100 : (int)p;
}

/* the two lines of text of one row, as drawn */
static void vd_row_text(const struct vd_ent *e, char *l1, size_t n1,
                        char *l2, size_t n2)
{
    char a[24], b[24];

    if (e->dir)
    {
        snprintf(l1, n1, "%s", e->name);
        if (!e->tried)
            l2[0] = '\0';
        else if (e->nvid == 0)
            snprintf(l2, n2, "No videos");
        else if (e->total_s)
        {
            vd_fmt_len(a, sizeof(a), e->total_s);
            snprintf(l2, n2, "%d video%s, %s", e->nvid,
                     e->nvid == 1 ? "" : "s", a);
        }
        else
            snprintf(l2, n2, "%d video%s", e->nvid, e->nvid == 1 ? "" : "s");
        return;
    }

    vd_stem(e->name, l1, n1);
    vd_fmt_size(b, sizeof(b), e->size);
    if (e->has_info && e->len_s)
    {
        vd_fmt_len(a, sizeof(a), e->len_s);
        if (e->quality[0])
            snprintf(l2, n2, "%s, %s, %s", a, e->quality, b);
        else
            snprintf(l2, n2, "%s, %s", a, b);
    }
    else
        snprintf(l2, n2, "No info file, %s", b);
}

/* ---- the page ---------------------------------------------------------------------------------- */

/* shows folder dir; select_name is the entry to put the cursor on */
static void vd_open_dir(const char *dir, const char *select_name, int *sel,
                        int *top)
{
    strlcpy(vd_dir, dir, sizeof(vd_dir));
    vd_root_missing = !vd_scan(&vd_cur, vd_dir);
    vd_cont_compute();
    *sel = 0;
    *top = 0;
    if (select_name)
        for (int i = 0; i < vd_cur.n; i++)
            if (!strcmp(vd_cur.ent[i].name, select_name))
            {
                *sel = i + (vd_cont.valid ? 1 : 0);
                break;
            }
}

/* the start menu for a video that was left half way. 0 = resume,
   1 = from the beginning, -1 = cancelled, -2 = USB connected */
static int vd_start_menu(void)
{
    MENUITEM_STRINGLIST(menu, "Continue watching", NULL,
                        "Resume where I stopped", "Start from the beginning");
    int choice = do_menu(&menu, NULL, NULL, false);
    if (choice == MENU_ATTACHED_USB)
        return -2;
    return (choice == 0 || choice == 1) ? choice : -1;
}

/* Returns true when the icon home should be left (with *result as the next
 * screen), false to go back to the grid. */
static bool video_page(struct screen *display, struct viewport *vp,
                       int *result)
{
    struct palette pal;
    int sel = 0, top = 0;
    bool leave = false, done = false, go_home = false;

    action_wait_for_release();
    vd_th_init();
    vd_rec_load();
    if (!dir_exists(vd_dir))
        strlcpy(vd_dir, VD_ROOT, sizeof(vd_dir));
    {
        /* back where we were last time */
        char keep[MAX_PATH];
        strlcpy(keep, vd_dir, sizeof(keep));
        vd_open_dir(keep, NULL, &sel, &top);
    }

    while (!done)
    {
        palette_init(&pal);
        vp->fg_pattern = pal.fg;
        vp->bg_pattern = pal.bg;
        display->clear_viewport();

        int width = vp->width, height = vp->height;
        int fh = display->getcharheight();
        int hh = fh + 6;
        int row_h = 2 * fh + 6;
        if (row_h < VD_TH_H + 4)
            row_h = VD_TH_H + 4;
        int visible = (height - hh - 2) / row_h;
        if (visible < 1)
            visible = 1;
        bool has_cont = vd_cont.valid;
        int nitems = vd_cur.n + (has_cont ? 1 : 0);

        if (sel >= nitems)
            sel = nitems ? nitems - 1 : 0;
        if (sel < top)
            top = sel;
        if (sel >= top + visible)
            top = sel - visible + 1;

        /* header: Videos / Series / Season */
        char head[96];
        {
            size_t n = 0;
            const char *p = "Videos";
            while (*p && n < sizeof(head) - 4)
                head[n++] = *p++;
            for (p = vd_dir + strlen(VD_ROOT); *p && n < sizeof(head) - 4; p++)
            {
                if (*p == '/')
                {
                    head[n++] = ' ';
                    head[n++] = '/';
                    head[n++] = ' ';
                }
                else
                    head[n++] = *p;
            }
            head[n] = '\0';
        }
        fit_text(display, head, width - 90);
        draw_header(display, &pal, head, width, hh);

        const char *sel_path = NULL;
        const struct vd_ent *sel_ent = NULL;
        char sel_buf[MAX_PATH];

        if (nitems == 0)
        {
            display->set_foreground(pal.dim);
            if (vd_root_missing)
            {
                put_text(display, 6, hh + 6, "No Videos folder");
                put_text(display, 6, hh + 6 + fh + 2, "Make /Videos on the card");
            }
            else
            {
                put_text(display, 6, hh + 6, "No videos here");
                put_text(display, 6, hh + 6 + fh + 2,
                         "Convert with rb_video_convert.py");
            }
            put_text(display, 6, hh + 6 + 2 * (fh + 2),
                     "and copy to " VD_ROOT);
        }

        for (int r = 0; r < visible && top + r < nitems; r++)
        {
            int idx = top + r;
            int y = hh + 2 + r * row_h;
            bool is_sel = (idx == sel);
            bool is_cont = has_cont && idx == 0;
            struct vd_ent *e = NULL;
            char l1[96], l2[96], path[MAX_PATH];
            char a[24], b[24];

            if (!is_cont)
            {
                e = &vd_cur.ent[idx - (has_cont ? 1 : 0)];
                vd_ent_fill(e);
                vd_join(path, sizeof(path), vd_dir, e->name);
            }
            else
                strlcpy(path, vd_cont.path, sizeof(path));

            if (is_sel)
            {
                strlcpy(sel_buf, path, sizeof(sel_buf));
                sel_path = sel_buf;
                sel_ent = e;
                display->set_foreground(pal.sel_bg);
                display->fillrect(1, y, width - 6, row_h - 1);
                display->set_foreground(pal.accent);
                display->drawrect(1, y, width - 6, row_h - 1);
            }
            else if (is_cont)
            {
                display->set_foreground(blend(pal.bg, pal.accent, 10));
                display->fillrect(1, y, width - 6, row_h - 1);
            }

            unsigned txt = label_color(&pal, is_sel);
            unsigned sub = is_sel ? txt : pal.dim;
            {
                /* picture slot: thumbnail, else a drawn icon */
                int sx = 5, sy2 = y + (row_h - VD_TH_H) / 2;
                int tw = 0, th = 0;
                const fb_data *tp = (e && e->dir) ? NULL
                                    : vd_th_get(path, &tw, &th);
                if (tp)
                {
                    int bx = sx + (VD_TH_W - tw) / 2;
                    int by = sy2 + (VD_TH_H - th) / 2;
                    display->set_foreground(pal.dim);
                    display->drawrect(bx - 1, by - 1, tw + 2, th + 2);
                    display->bitmap(tp, bx, by, tw, th);
                }
                else
                {
                    display->set_foreground(is_sel ? txt : pal.accent);
                    if (e && e->dir)
                        vd_draw_folder_icon(display, sx + (VD_TH_W - 17) / 2,
                                            sy2 + (VD_TH_H - 14) / 2);
                    else
                        vd_draw_video_icon(display, sx + (VD_TH_W - 17) / 2,
                                           sy2 + (VD_TH_H - 14) / 2);
                }
            }

            int tx = 5 + VD_TH_W + 8;
            int right_w = 36;               /* room for marker */

            if (is_cont)
            {
                char stem[64];
                vd_stem(path, stem, sizeof(stem));
                snprintf(l1, sizeof(l1), "%s", stem);
                fit_text(display, l1, width - tx - 10);
                display->set_foreground(txt);
                put_text(display, tx, y + 3, l1);

                const char *label = vd_cont.next_up ? "Next up" : "Continue";
                int lw;
                display->getstringsize(label, &lw, NULL);
                display->set_foreground(is_sel ? txt : pal.accent);
                put_text(display, tx, y + 3 + fh, label);
                if (vd_cont.pos_s)
                {
                    format_elapsed(a, sizeof(a), vd_cont.pos_s * 1000ul);
                    if (vd_cont.len_s)
                    {
                        format_elapsed(b, sizeof(b), vd_cont.len_s * 1000ul);
                        snprintf(l2, sizeof(l2), ", %s of %s", a, b);
                    }
                    else
                        snprintf(l2, sizeof(l2), ", %s", a);
                }
                else if (vd_cont.len_s)
                {
                    vd_fmt_len(a, sizeof(a), vd_cont.len_s);
                    snprintf(l2, sizeof(l2), ", %s", a);
                }
                else
                    l2[0] = '\0';
                fit_text(display, l2, width - tx - 10 - lw);
                display->set_foreground(sub);
                put_text(display, tx + lw, y + 3 + fh, l2);
                if (vd_cont.pos_s && vd_cont.len_s)
                {
                    int bw = (width - 8) * vd_pct(vd_cont.pos_s,
                                                  vd_cont.len_s) / 100;
                    display->set_foreground(is_sel ? txt : pal.accent);
                    display->fillrect(2, y + row_h - 3, bw, 2);
                }
                continue;
            }

            vd_row_text(e, l1, sizeof(l1), l2, sizeof(l2));
            fit_text(display, l1, width - tx - right_w - 8);
            fit_text(display, l2, width - tx - right_w - 8);
            display->set_foreground(txt);
            put_text(display, tx, y + 3, l1);
            display->set_foreground(sub);
            put_text(display, tx, y + 3 + fh, l2);

            /* marker on the right */
            int mx = width - 10 - right_w;
            int my = y + (row_h - 8) / 2;
            if (e->dir)
            {
                if (e->tried && e->nvid)
                {
                    snprintf(a, sizeof(a), "%d/%d", e->nwatched, e->nvid);
                    int aw;
                    display->getstringsize(a, &aw, NULL);
                    display->set_foreground(sub);
                    put_text(display, width - 10 - aw, y + (row_h - fh) / 2, a);
                }
            }
            else
            {
                const struct video_posrec *rec = vd_find(path);
                if (rec && video_rec_watched(rec))
                {
                    display->set_foreground(is_sel ? txt : pal.accent);
                    vd_draw_tick(display, width - 22, my);
                }
                else if (rec && video_rec_partial(rec))
                {
                    vd_draw_progress(display, &pal, mx + 4, my + 1, right_w - 6,
                                     7, vd_pct(rec->pos_s, rec->dur_s));
                }
            }
        }

        /* scroll bar */
        if (nitems > visible)
        {
            int track = visible * row_h;
            int bar = track * visible / nitems;
            if (bar < 6)
                bar = 6;
            int by = hh + 2 + (track - bar) * top / (nitems - visible);
            display->set_foreground(pal.dim);
            display->fillrect(width - 3, by, 2, bar);
        }

        display->update();

        long button = button_get_w_tmo(HZ / 6);
        switch (button)
        {
            case BUTTON_UP:
            case BUTTON_UP | BUTTON_REPEAT:
                if (nitems)
                    sel = (sel + nitems - 1) % nitems;
                break;
            case BUTTON_DOWN:
            case BUTTON_DOWN | BUTTON_REPEAT:
                if (nitems)
                    sel = (sel + 1) % nitems;
                break;
            case BUTTON_SELECT | BUTTON_REL:
            case BUTTON_PLAYPAUSE | BUTTON_REL:
            {
                bool is_play = (button == (BUTTON_PLAYPAUSE | BUTTON_REL));
                /* Play always carries on with the Continue row */
                bool is_cont = has_cont && (sel == 0 || is_play);
                char path[MAX_PATH];
                unsigned long start = 0;

                if (!nitems || !sel_path)
                    break;
                strlcpy(path, is_cont ? vd_cont.path : sel_path, sizeof(path));

                if (!is_cont && sel_ent && sel_ent->dir)
                {
                    if (!is_play)
                        vd_open_dir(path, NULL, &sel, &top);
                    break;
                }

                if (is_cont)
                    start = vd_cont.pos_s;
                else
                {
                    const struct video_posrec *rec = vd_find(path);
                    if (rec && video_rec_partial(rec))
                    {
                        int c = vd_start_menu();
                        if (c == -2)
                        {
                            *result = GO_TO_ROOT;
                            leave = true;
                            done = true;
                            break;
                        }
                        display->set_viewport(vp);
                        action_wait_for_release();
                        if (c < 0)
                            break;
                        start = (c == 0) ? rec->pos_s : 0;
                    }
                }

                splash(0, ID2P(LANG_WAIT));
                vd_th_free();       /* the player wants the memory */
                int ret = vd_launch(&pal, path, start);
                vd_th_init();
                display->set_viewport(vp);
                /* Back is held on after the video stopped: go straight on
                   to the icon grid instead of showing this list */
                {
                    long t0 = current_tick;
                    while ((button_status() & BUTTON_BACK) &&
                           TIME_BEFORE(current_tick, t0 + HZ * 3 / 4))
                        sleep(HZ / 20);
                    if (button_status() & BUTTON_BACK)
                        go_home = true;
                }
                action_wait_for_release();
                if (ret == PLUGIN_USB_CONNECTED)
                {
                    *result = GO_TO_ROOT;
                    leave = true;
                    done = true;
                    break;
                }
                /* the plugin has saved where it stopped */
                vd_rec_load();
                for (int i = 0; i < vd_cur.n; i++)
                    if (vd_cur.ent[i].dir)
                        vd_cur.ent[i].tried = false;
                vd_cont_compute();
                if (go_home)
                    done = true;
                break;
            }
            case BUTTON_BACK | BUTTON_REL:
                if (strcmp(vd_dir, VD_ROOT))
                {
                    /* up one folder, cursor on the folder we came from */
                    char up[MAX_PATH], name[MAX_PATH];
                    strlcpy(up, vd_dir, sizeof(up));
                    char *slash = strrchr(up, '/');
                    name[0] = '\0';
                    if (slash)
                    {
                        strlcpy(name, slash + 1, sizeof(name));
                        *slash = '\0';
                    }
                    if (!up[0] || strncmp(up, VD_ROOT, strlen(VD_ROOT)))
                        strlcpy(up, VD_ROOT, sizeof(up));
                    vd_open_dir(up, name, &sel, &top);
                }
                else
                    done = true;
                break;
            case BUTTON_NONE:
                /* nothing pressed for a moment: count one folder's videos */
                for (int i = 0; i < vd_cur.n; i++)
                {
                    struct vd_ent *e = &vd_cur.ent[i];
                    if (e->dir && !e->tried)
                    {
                        vd_ent_fill(e);
                        break;
                    }
                }
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

    vd_th_free();
    return leave;
}
