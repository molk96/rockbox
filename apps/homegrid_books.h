/***************************************************************************
 * Books page of the icon home (included by homegrid.c, not built alone).
 *
 * A book is a folder under /Audiobooks that holds audio files (chapters or
 * one .m4b).  Folders that only hold other folders are searched one level
 * deeper (Author/Book).  Two tabs, switched with Left/Right:
 *   Recent - the books opened last (default), kept in
 *            /.rockbox/books_recent.txt
 *   All    - every book, sorted by name
 * The list is on the left.  The right side shows the cover, a progress bar
 * with "NN% completed" and the time left, then title and author.
 *
 * Progress comes from the book's bookmark (<folder>.bmark, written by the
 * automatic audiobook bookmarks) and the length of the files, which is read
 * once per book and cached in /.rockbox/books_cache.txt.
 *
 * Cover: cover.jpg / folder.jpg (also .jpeg, .bmp) in the book folder, else
 * the picture embedded in the first file (an .m4b, for example).
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

#define BOOKS_ROOT          "/Audiobooks"
#define BOOKS_MAX           80
#define BOOKS_POOL          12288
#define BOOKS_RECENT_MAX    10
#define BOOKS_RECENT_FILE   ROCKBOX_DIR "/books_recent.txt"
#define BOOKS_CACHE_FILE    ROCKBOX_DIR "/books_cache.txt"
#define BK_FILES_MAX        200
#define BK_FILE_POOL        12288

enum { BK_TAB_RECENT = 0, BK_TAB_ALL };

struct bk_book
{
    const char *path;    /* /Audiobooks/Title */
    const char *name;    /* Title */
    const char *parent;  /* Author folder when nested, else "" */
};

static struct bk_book bk_books[BOOKS_MAX];
static int bk_nbooks;
static char bk_pool[BOOKS_POOL];
static size_t bk_pool_used;

/* list shown in the current tab: indexes into bk_books */
static int bk_view[BOOKS_MAX];
static int bk_nview;

static char bk_recent[BOOKS_RECENT_MAX][MAX_PATH];
static int bk_recent_n;

/* files of the book that is shown on the right */
static const char *bk_files[BK_FILES_MAX];
static char bk_file_pool[BK_FILE_POOL];
static int bk_nf;
static unsigned long bk_dur[BK_FILES_MAX];   /* seconds */

struct bk_detail
{
    int book;               /* index into bk_books, -1 = nothing loaded */
    char title[64];
    char author[48];
    unsigned long total_s;
    unsigned long pos_s;
    bool started;           /* a bookmark exists */
    bool info;              /* length, progress, title and author are loaded */
};
static struct bk_detail bk_det = { .book = -1 };

/* Everything shown about a book except the cover, kept in memory for the
 * whole library so selecting a book draws its details at once. */
struct bk_mem
{
    bool have;              /* title, author and length are known */
    bool pos_known;         /* pos_s / started are known */
    bool started;
    short nf;               /* number of files when it was measured */
    long off;               /* offset of its line in the cache file */
    unsigned long total_s;
    unsigned long pos_s;
    char title[64];
    char author[48];
};
static struct bk_mem bk_mem[BOOKS_MAX];

/* last known position per book (survives leaving the page) */
struct bk_posrec { unsigned long h; unsigned long pos_s; bool started; };
static struct bk_posrec bk_posrec[BOOKS_MAX];
static int bk_nposrec;
static bool bk_fill_tried[BOOKS_MAX];

static struct mp3entry bk_id3;
static char bk_line[2816];      /* cache / bookmark lines */

/* cover */
static int bk_cover_handle;
static size_t bk_cover_size;
static int bk_cover_side;
static struct bitmap bk_cover_bm;
static bool bk_cover_ok;

/* ---- small helpers ------------------------------------------------------ */

static const char *bk_pool_add(const char *s)
{
    size_t n = strlen(s) + 1;
    if (bk_pool_used + n > sizeof(bk_pool))
        return NULL;
    char *p = bk_pool + bk_pool_used;
    memcpy(p, s, n);
    bk_pool_used += n;
    return p;
}

static bool bk_is_audio(const char *name)
{
    return (filetype_get_attr(name) & FILE_ATTR_MASK) == FILE_ATTR_AUDIO;
}

static bool bk_dir_has_audio(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    bool found = false;

    if (!d)
        return false;
    while (!found && (e = readdir(d)) != NULL)
    {
        struct dirinfo info = dir_get_info(d, e);
        if (!(info.attribute & ATTR_DIRECTORY) && bk_is_audio(e->d_name))
            found = true;
    }
    closedir(d);
    return found;
}

static int bk_book_cmp(const void *a, const void *b)
{
    const struct bk_book *x = a, *y = b;
    return strnatcasecmp(x->name, y->name);
}

static void bk_add_book(const char *path, const char *name, const char *parent)
{
    if (bk_nbooks >= BOOKS_MAX)
        return;
    const char *p = bk_pool_add(path);
    const char *n = bk_pool_add(name);
    const char *a = bk_pool_add(parent);
    if (!p || !n || !a)
        return;
    bk_books[bk_nbooks].path = p;
    bk_books[bk_nbooks].name = n;
    bk_books[bk_nbooks].parent = a;
    bk_nbooks++;
}

/* finds all books under /Audiobooks */
static void bk_scan(void)
{
    DIR *d = opendir(BOOKS_ROOT);
    struct dirent *e;
    char path[MAX_PATH];

    bk_nbooks = 0;
    bk_pool_used = 0;
    if (!d)
        return;

    while ((e = readdir(d)) != NULL)
    {
        struct dirinfo info = dir_get_info(d, e);
        if (!(info.attribute & ATTR_DIRECTORY) || e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", BOOKS_ROOT, e->d_name);
        if (bk_dir_has_audio(path))
        {
            bk_add_book(path, e->d_name, "");
            continue;
        }

        /* Author/Book */
        DIR *d2 = opendir(path);
        struct dirent *e2;
        char path2[MAX_PATH];
        if (!d2)
            continue;
        while ((e2 = readdir(d2)) != NULL)
        {
            struct dirinfo info2 = dir_get_info(d2, e2);
            if (!(info2.attribute & ATTR_DIRECTORY) || e2->d_name[0] == '.')
                continue;
            snprintf(path2, sizeof(path2), "%s/%s", path, e2->d_name);
            if (bk_dir_has_audio(path2))
                bk_add_book(path2, e2->d_name, e->d_name);
        }
        closedir(d2);
    }
    closedir(d);

    qsort(bk_books, bk_nbooks, sizeof(bk_books[0]), bk_book_cmp);
}

/* ---- recent list --------------------------------------------------------- */

static void bk_recent_load(void)
{
    int fd = open(BOOKS_RECENT_FILE, O_RDONLY);

    bk_recent_n = 0;
    if (fd < 0)
        return;
    /* read_line() returns 0 at the end of the file */
    while (bk_recent_n < BOOKS_RECENT_MAX &&
           read_line(fd, bk_recent[bk_recent_n], MAX_PATH) > 0)
    {
        if (bk_recent[bk_recent_n][0] == '/')
            bk_recent_n++;
    }
    close(fd);
}

static void bk_recent_save(void)
{
    int fd = open(BOOKS_RECENT_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    for (int i = 0; i < bk_recent_n; i++)
        fdprintf(fd, "%s\n", bk_recent[i]);
    close(fd);
}

static void bk_recent_add(const char *path)
{
    int i;

    for (i = 0; i < bk_recent_n; i++)
        if (!strcmp(bk_recent[i], path))
            break;
    if (i == bk_recent_n)
    {
        if (bk_recent_n < BOOKS_RECENT_MAX)
            bk_recent_n++;
        i = bk_recent_n - 1;
    }
    for (; i > 0; i--)
        strlcpy(bk_recent[i], bk_recent[i - 1], MAX_PATH);
    strlcpy(bk_recent[0], path, MAX_PATH);
    bk_recent_save();
}

/* the book that is playing now counts as recent, wherever it was started */
static void bk_sync_playing(void)
{
    if (!(audio_status() & (AUDIO_STATUS_PLAY | AUDIO_STATUS_PAUSE)))
        return;
    struct mp3entry *id3 = audio_current_track();
    if (!id3)
        return;
    for (int i = 0; i < bk_nbooks; i++)
    {
        size_t len = strlen(bk_books[i].path);
        if (!strncmp(id3->path, bk_books[i].path, len) &&
            id3->path[len] == '/')
        {
            if (bk_recent_n == 0 || strcmp(bk_recent[0], bk_books[i].path))
                bk_recent_add(bk_books[i].path);
            break;
        }
    }
}

/* fills bk_view for a tab */
static void bk_build_view(int tab)
{
    bk_nview = 0;
    if (tab == BK_TAB_ALL)
    {
        for (int i = 0; i < bk_nbooks; i++)
            bk_view[bk_nview++] = i;
        return;
    }
    for (int r = 0; r < bk_recent_n; r++)
        for (int i = 0; i < bk_nbooks; i++)
            if (!strcmp(bk_books[i].path, bk_recent[r]))
            {
                bk_view[bk_nview++] = i;
                break;
            }
}

/* ---- details: files, length, progress ------------------------------------- */

static int bk_name_cmp(const void *a, const void *b)
{
    return strnatcasecmp(*(const char * const *)a, *(const char * const *)b);
}

/* sorted audio files of a folder */
static void bk_list_files(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    size_t used = 0;

    bk_nf = 0;
    if (!d)
        return;
    while ((e = readdir(d)) != NULL && bk_nf < BK_FILES_MAX)
    {
        struct dirinfo info = dir_get_info(d, e);
        size_t len = strlen(e->d_name) + 1;
        if ((info.attribute & ATTR_DIRECTORY) || !bk_is_audio(e->d_name) ||
            used + len > sizeof(bk_file_pool))
            continue;
        memcpy(bk_file_pool + used, e->d_name, len);
        bk_files[bk_nf++] = bk_file_pool + used;
        used += len;
    }
    closedir(d);
    qsort(bk_files, bk_nf, sizeof(bk_files[0]), bk_name_cmp);
}

static void bk_clean_copy(char *dst, size_t size, const char *src)
{
    size_t n = 0;
    if (!src)
        src = "";
    while (*src && n + 1 < size)
    {
        char c = *src++;
        dst[n++] = (c == '|' || c == '\n' || c == '\r') ? ' ' : c;
    }
    dst[n] = '\0';
}

static unsigned long bk_hash(const char *s)
{
    unsigned long h = 5381;
    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h;
}

static void bk_pos_remember(int book)
{
    unsigned long h = bk_hash(bk_books[book].path);
    int i;
    for (i = 0; i < bk_nposrec; i++)
        if (bk_posrec[i].h == h)
            break;
    if (i == bk_nposrec)
    {
        if (i >= BOOKS_MAX)
            return;
        bk_nposrec++;
    }
    bk_posrec[i].h = h;
    bk_posrec[i].pos_s = bk_mem[book].pos_s;
    bk_posrec[i].started = bk_mem[book].started;
}

/* Parses one cache line "dir|nfiles|d1,d2,..|title|author". Returns the
 * book index, or -1. When 'durs' is set the lengths are copied to bk_dur
 * (only if the file count is 'want_nf'). */
static int bk_cache_parse(char *line, bool durs, int want_nf, struct bk_mem *out)
{
    char *f_n = strchr(line, '|');
    if (!f_n)
        return -1;
    *f_n++ = '\0';
    char *f_dur = strchr(f_n, '|');
    if (!f_dur)
        return -1;
    *f_dur++ = '\0';
    char *f_title = strchr(f_dur, '|');
    if (!f_title)
        return -1;
    *f_title++ = '\0';
    char *f_author = strchr(f_title, '|');
    if (!f_author)
        return -1;
    *f_author++ = '\0';

    int n = atoi(f_n);
    if (durs && n != want_nf)
        return -1;
    if (n > BK_FILES_MAX)
        return -1;

    unsigned long total = 0;
    int i = 0;
    char *p = f_dur;
    while (*p && i < n)
    {
        unsigned long v = strtoul(p, &p, 10);
        if (durs)
            bk_dur[i] = v;
        total += v;
        i++;
        if (*p == ',')
            p++;
    }
    if (i != n)
        return -1;

    if (out)
    {
        out->nf = n;
        out->total_s = total;
        strlcpy(out->title, f_title, sizeof(out->title));
        strlcpy(out->author, f_author, sizeof(out->author));
    }
    for (int b = 0; b < bk_nbooks; b++)
        if (!strcmp(bk_books[b].path, line))
            return b;
    return -1;
}

/* Reads the whole cache file once and fills the in-memory table. */
static void bk_mem_load(void)
{
    memset(bk_mem, 0, sizeof(bk_mem));
    memset(bk_fill_tried, 0, sizeof(bk_fill_tried));

    for (int b = 0; b < bk_nbooks; b++)
    {
        unsigned long h = bk_hash(bk_books[b].path);
        for (int i = 0; i < bk_nposrec; i++)
            if (bk_posrec[i].h == h)
            {
                bk_mem[b].pos_s = bk_posrec[i].pos_s;
                bk_mem[b].started = bk_posrec[i].started;
                bk_mem[b].pos_known = true;
                break;
            }
    }

    int fd = open(BOOKS_CACHE_FILE, O_RDONLY);
    if (fd < 0)
        return;
    for (;;)
    {
        long off = lseek(fd, 0, SEEK_CUR);
        if (read_line(fd, bk_line, sizeof(bk_line)) <= 0)
            break;
        struct bk_mem m;
        memset(&m, 0, sizeof(m));
        int b = bk_cache_parse(bk_line, false, 0, &m);
        if (b < 0)
            continue;
        bool pk = bk_mem[b].pos_known, st = bk_mem[b].started;
        unsigned long ps = bk_mem[b].pos_s;
        bk_mem[b] = m;                      /* the last line wins */
        bk_mem[b].have = true;
        bk_mem[b].off = off;
        bk_mem[b].pos_known = pk;
        bk_mem[b].started = st;
        bk_mem[b].pos_s = ps;
        if (bk_mem[b].pos_known && bk_mem[b].pos_s > bk_mem[b].total_s &&
            bk_mem[b].total_s)
            bk_mem[b].pos_s = bk_mem[b].total_s;
    }
    close(fd);
}

/* Loads the file lengths of a book into bk_dur from its cache line.
 * false when it has none or the number of files changed (stale). */
static bool bk_cache_lookup(int book)
{
    if (!bk_mem[book].have)
        return false;
    int fd = open(BOOKS_CACHE_FILE, O_RDONLY);
    if (fd < 0)
        return false;
    bool ok = false;
    if (lseek(fd, bk_mem[book].off, SEEK_SET) >= 0 &&
        read_line(fd, bk_line, sizeof(bk_line)) > 0)
        ok = (bk_cache_parse(bk_line, true, bk_nf, NULL) == book);
    close(fd);
    return ok;
}

/* Appends the measured book (bk_dur, title, author in bk_mem) to the file. */
static void bk_cache_store(int book)
{
    struct bk_mem *m = &bk_mem[book];
    const char *dir = bk_books[book].path;
    int fd = open(BOOKS_CACHE_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
    int n;

    if (fd < 0)
        return;
    long off = lseek(fd, 0, SEEK_END);
    n = snprintf(bk_line, sizeof(bk_line), "%s|%d|", dir, bk_nf);
    for (int i = 0; i < bk_nf && n > 0 && n < (int)sizeof(bk_line) - 16; i++)
        n += snprintf(bk_line + n, sizeof(bk_line) - n, "%s%lu",
                      i ? "," : "", bk_dur[i]);
    if (n > 0 && n < (int)sizeof(bk_line) - 200)
        snprintf(bk_line + n, sizeof(bk_line) - n, "|%s|%s\n",
                 m->title, m->author);
    else
        bk_line[0] = '\0'; /* does not fit: do not cache */
    if (bk_line[0])
    {
        write(fd, bk_line, strlen(bk_line));
        m->off = off;
    }
    close(fd);
    m->have = true;
    m->nf = bk_nf;
    m->total_s = 0;
    for (int i = 0; i < bk_nf; i++)
        m->total_s += bk_dur[i];
}

/* reads the length of every file (slow, so the result is cached) */
static void bk_measure(const struct bk_book *book, struct bk_mem *det)
{
    char path[MAX_PATH];
    char title[64] = "", author[48] = "";

    for (int i = 0; i < bk_nf; i++)
    {
        bk_dur[i] = 0;
        snprintf(path, sizeof(path), "%s/%s", book->path, bk_files[i]);
        int fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;
        if (get_metadata(&bk_id3, fd, path))
        {
            bk_dur[i] = bk_id3.length / 1000;
            if (i == 0)
            {
                bk_clean_copy(title, sizeof(title),
                              (bk_id3.album && bk_id3.album[0]) ? bk_id3.album
                                                                : bk_id3.title);
                bk_clean_copy(author, sizeof(author),
                              (bk_id3.albumartist && bk_id3.albumartist[0])
                                  ? bk_id3.albumartist : bk_id3.artist);
            }
        }
        close(fd);
        yield();
    }
    bk_clean_copy(det->title, sizeof(det->title),
                  title[0] ? title : book->name);
    bk_clean_copy(det->author, sizeof(det->author),
                  author[0] ? author : book->parent);
}

/* position in the book from the bookmark, in seconds; false if none */
static bool bk_read_progress(const char *dir, unsigned long *pos_s)
{
    char name[MAX_PATH];
    int fd;
    unsigned long tok[5] = { 0, 0, 0, 0, 0 };

    snprintf(name, sizeof(name), "%s.bmark", dir);
    fd = open(name, O_RDONLY);
    if (fd < 0)
        return false;
    int len = read_line(fd, bk_line, sizeof(bk_line));
    close(fd);
    if (len <= 0)
        return false;

    /* ">flags;index;offset;seed;elapsed;..." or the old format without
       the flags: "index;offset;seed;x;elapsed;...". The file name is the
       text after the last ';'. */
    const char *s = bk_line;
    bool new_format = (*s == '>');
    if (new_format)
        s++;
    for (int i = 0; i < 5; i++)
    {
        tok[i] = strtoul(s, (char **)&s, 10);
        if (*s != ';')
            break;
        s++;
    }
    unsigned long index = new_format ? tok[1] : tok[0];
    unsigned long elapsed_ms = tok[4];

    const char *file = strrchr(bk_line, ';');
    int cur = -1;
    if (file)
    {
        const char *slash = strrchr(file, '/');
        file = slash ? slash + 1 : file + 1;
        for (int i = 0; i < bk_nf; i++)
            if (!strcasecmp(bk_files[i], file))
            {
                cur = i;
                break;
            }
    }
    if (cur < 0)
        cur = (index < (unsigned long)bk_nf) ? (int)index : 0;

    unsigned long pos = elapsed_ms / 1000;
    for (int i = 0; i < cur && i < bk_nf; i++)
        pos += bk_dur[i];
    *pos_s = pos;
    return true;
}

/* ---- cover ---------------------------------------------------------------- */

static void bk_cover_free(void)
{
    if (bk_cover_handle > 0)
    {
        core_unpin(bk_cover_handle);
        core_free(bk_cover_handle);
    }
    bk_cover_handle = 0;
    bk_cover_ok = false;
}

static void bk_cover_alloc(int side)
{
    bk_cover_side = side;
#ifdef HAVE_JPEG
    bk_cover_size = (size_t)side * side * sizeof(fb_data) + JPEG_DECODE_OVERHEAD;
#else
    bk_cover_size = (size_t)side * side * sizeof(fb_data);
#endif
    bk_cover_handle = core_alloc(bk_cover_size);
    if (bk_cover_handle > 0)
        core_pin(bk_cover_handle);
    else
        bk_cover_handle = 0;
}

/* cover.jpg, folder.jpg ... in the folder; false if none */
static bool bk_find_cover(const char *dir, char *out, size_t size)
{
    static const char *const names[] = {
        "cover.jpg", "folder.jpg", "cover.jpeg", "folder.jpeg",
        "front.jpg", "cover.bmp", "folder.bmp",
    };
    DIR *d = opendir(dir);
    struct dirent *e;
    int best = -1;

    if (!d)
        return false;
    while ((e = readdir(d)) != NULL)
    {
        for (int i = 0; i < (int)ARRAYLEN(names); i++)
            if (!strcasecmp(e->d_name, names[i]) && (best < 0 || i < best))
            {
                best = i;
                snprintf(out, size, "%s/%s", dir, e->d_name);
            }
    }
    closedir(d);
    return best >= 0;
}

/* ---- cover cache ---------------------------------------------------------
 * The decoded, scaled cover is kept in /.rockbox/bookcovers/ so that showing
 * a book again does not have to decode the big JPEG. Delete that folder to
 * make the covers load again from the pictures. */
#define BK_COVER_DIR ROCKBOX_DIR "/bookcovers"

struct bk_cvr_head
{
    char magic[4];
    unsigned short w, h;
};

static void bk_cover_cache_name(char *out, size_t size, const char *bookpath)
{
    uint32_t h = 5381;
    for (const char *p = bookpath; *p; p++)
        h = h * 33 + (unsigned char)*p;
    snprintf(out, size, BK_COVER_DIR "/%08lx_%d.cvr", (unsigned long)h,
             bk_cover_side);
}

/* 1 = cover loaded from the cache, -1 = cached "this book has no cover",
 * 0 = not cached yet */
static int bk_cover_cache_read(const char *bookpath, struct bitmap *bm)
{
    char name[MAX_PATH];
    struct bk_cvr_head hd;
    int ok = 0;

    bk_cover_cache_name(name, sizeof(name), bookpath);
    int fd = open(name, O_RDONLY);
    if (fd < 0)
        return 0;
    if (read(fd, &hd, sizeof(hd)) == (ssize_t)sizeof(hd) &&
        !memcmp(hd.magic, "CVR1", 4) && hd.w == 0 && hd.h == 0)
    {
        ok = -1;
    }
    else if (!memcmp(hd.magic, "CVR1", 4) && hd.w > 0 && hd.h > 0 &&
             hd.w <= bk_cover_side && hd.h <= bk_cover_side)
    {
        size_t bytes = (size_t)hd.w * hd.h * sizeof(fb_data);
        if (read(fd, bm->data, bytes) == (ssize_t)bytes)
        {
            bm->width = hd.w;
            bm->height = hd.h;
            ok = 1;
        }
    }
    close(fd);
    return ok;
}

static bool bk_cover_cache_exists(const char *bookpath)
{
    char name[MAX_PATH];
    bk_cover_cache_name(name, sizeof(name), bookpath);
    return file_exists(name);
}

/* bm == NULL writes the "no cover" marker */
static void bk_cover_cache_write(const char *bookpath, const struct bitmap *bm)
{
    char name[MAX_PATH];
    struct bk_cvr_head hd;

    mkdir(BK_COVER_DIR); /* fails harmlessly when it exists */
    bk_cover_cache_name(name, sizeof(name), bookpath);
    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    memcpy(hd.magic, "CVR1", 4);
    hd.w = bm ? bm->width : 0;
    hd.h = bm ? bm->height : 0;
    write(fd, &hd, sizeof(hd));
    if (bm)
        write(fd, bm->data, (size_t)bm->width * bm->height * sizeof(fb_data));
    close(fd);
}

static void bk_cover_load(const struct bk_book *book)
{
    char path[MAX_PATH];
    int rc = 0;

    bk_cover_ok = false;
    if (bk_cover_handle <= 0)
        return;

    struct bitmap *bm = &bk_cover_bm;
    bm->width = bk_cover_side;
    bm->height = bk_cover_side;
    bm->data = core_get_data(bk_cover_handle);
#if (LCD_DEPTH > 1)
    bm->maskdata = NULL;
#endif
    const int format = FORMAT_NATIVE | FORMAT_DITHER |
                       FORMAT_RESIZE | FORMAT_KEEP_ASPECT;

    int cached = bk_cover_cache_read(book->path, bm);
    if (cached != 0)
    {
        bk_cover_ok = (cached > 0);
        return;
    }

    cpu_boost(true); /* decoding a big JPEG is slow at the idle clock */
    if (bk_find_cover(book->path, path, sizeof(path)))
    {
        int fd = open(path, O_RDONLY);
        if (fd >= 0)
        {
            size_t len = strlen(path);
            if (len > 4 && !strcasecmp(path + len - 4, ".bmp"))
                rc = read_bmp_fd(fd, bm, (int)bk_cover_size, format, NULL);
#ifdef HAVE_JPEG
            else
                rc = read_jpeg_fd(fd, bm, (int)bk_cover_size, format, NULL);
#endif
            close(fd);
        }
    }
#ifdef HAVE_JPEG
    else if (bk_nf > 0)
    {
        /* picture embedded in the first file */
        snprintf(path, sizeof(path), "%s/%s", book->path, bk_files[0]);
        int fd = open(path, O_RDONLY);
        if (fd >= 0)
        {
            if (get_metadata(&bk_id3, fd, path) &&
                bk_id3.has_embedded_albumart &&
                (bk_id3.albumart.type & AA_CLEAR_FLAGS_MASK) == AA_TYPE_JPG)
            {
                lseek(fd, bk_id3.albumart.pos, SEEK_SET);
                rc = clip_jpeg_fd(fd, bk_id3.albumart.type,
                                  bk_id3.albumart.size, bm,
                                  (int)bk_cover_size, format, NULL);
            }
            close(fd);
        }
    }
#endif
    cpu_boost(false);
    bk_cover_ok = rc > 0;
    /* remember the result, also "no cover", so it is not searched again */
    bk_cover_cache_write(book->path, bk_cover_ok ? bm : NULL);
}

/* Books whose cover / length data are known to be ready (cached) */
static bool bk_pre_cover[BOOKS_MAX];
static bool bk_pre_info[BOOKS_MAX];

/* Stage 1 of showing a book: the cover, which is what the eye looks for. */
static void bk_load_stage1(int book)
{
    const struct bk_book *b = &bk_books[book];
    struct bk_detail *det = &bk_det;

    bk_pre_cover[book] = true;
    det->book = book;
    det->info = false;
    det->started = false;
    det->total_s = 0;
    det->pos_s = 0;
    det->title[0] = det->author[0] = '\0';

    bk_list_files(b->path);
    bk_cover_load(b);
}

/* Works out the position from the bookmark; needs bk_files and bk_dur. */
static void bk_refresh_pos(int book)
{
    struct bk_mem *m = &bk_mem[book];
    unsigned long pos;

    m->total_s = 0;
    for (int i = 0; i < bk_nf; i++)
        m->total_s += bk_dur[i];
    if (bk_read_progress(bk_books[book].path, &pos))
    {
        m->started = true;
        m->pos_s = (m->total_s && pos > m->total_s) ? m->total_s : pos;
    }
    else
    {
        m->started = false;
        m->pos_s = 0;
    }
    m->pos_known = true;
    bk_pos_remember(book);
}

/* Stage 2: makes sure the details are right (slow only the first time). */
static void bk_load_stage2(int book)
{
    const struct bk_book *b = &bk_books[book];
    struct bk_detail *det = &bk_det;

    if (det->book != book)
        return;
    bk_pre_info[book] = true;
    if (!bk_cache_lookup(book))
    {
        bk_measure(b, &bk_mem[book]);
        bk_cache_store(book);
    }
    bk_refresh_pos(book);
    det->info = true;
}

/* ---- preparing the neighbours while idle ----------------------------------- */

/* Does one piece of preparation work: first the books near the shown one
 * (cover into the cover cache, file lengths into the length cache), then
 * the details of the whole library, recent books first. Returns false when
 * everything is ready. It is called when nothing has been pressed for a
 * moment, so the next book opens instantly. */
static bool bk_precache_step(int sel)
{
    struct bk_detail keep_det = bk_det;
    bool did = false, did_cover = false;

    for (int dist = 1; dist <= 6 && !did; dist++)
    {
        for (int side = 0; side < 2 && !did; side++)
        {
            int pos = sel + (side ? -dist : dist);
            if (pos < 0 || pos >= bk_nview)
                continue;
            int book = bk_view[pos];
            const struct bk_book *b = &bk_books[book];

            if (!bk_pre_cover[book])
            {
                bk_pre_cover[book] = true;
                if (!bk_cover_cache_exists(b->path))
                {
                    bk_list_files(b->path);
                    bk_cover_load(b);   /* decodes and fills the cache */
                    did = did_cover = true;
                }
            }
            else if (!bk_pre_info[book])
            {
                bk_pre_info[book] = true;
                bk_fill_tried[book] = true;
                bk_list_files(b->path);
                if (!bk_cache_lookup(book))
                {
                    bk_measure(b, &bk_mem[book]);
                    bk_cache_store(book);
                    bk_refresh_pos(book);
                    did = true;
                }
                else if (!bk_mem[book].pos_known)
                {
                    bk_refresh_pos(book);
                    did = true;
                }
            }
        }
    }

    /* the rest of the library: recent books first, then the others */
    for (int k = 0; k < bk_recent_n + bk_nbooks && !did; k++)
    {
        int book = -1;
        if (k < bk_recent_n)
        {
            for (int i = 0; i < bk_nbooks; i++)
                if (!strcmp(bk_books[i].path, bk_recent[k]))
                {
                    book = i;
                    break;
                }
        }
        else
            book = k - bk_recent_n;
        if (book < 0 || bk_fill_tried[book])
            continue;
        bk_fill_tried[book] = true;
        bk_list_files(bk_books[book].path);
        if (!bk_cache_lookup(book))
        {
            bk_measure(&bk_books[book], &bk_mem[book]);
            bk_cache_store(book);
            bk_refresh_pos(book);
            did = true;
        }
    }

    if (did && keep_det.book >= 0)
    {
        /* the shared buffers now hold a neighbour's data: put the shown
           book back (the cover comes from the cache, so this is quick) */
        bk_det = keep_det;
        bk_list_files(bk_books[keep_det.book].path);
        if (did_cover)
            bk_cover_load(&bk_books[keep_det.book]);
    }
    return did;
}

/* ---- drawing -------------------------------------------------------------- */

static void bk_format_left(char *buf, size_t size, unsigned long secs)
{
    unsigned long mins = (secs + 59) / 60;
    if (mins >= 60)
        snprintf(buf, size, "%luh %02lum left", mins / 60, mins % 60);
    else
        snprintf(buf, size, "%lum left", mins);
}

static void bk_text_centered(struct screen *d, int x, int w, int y,
                             const char *text)
{
    int tw;
    d->getstringsize(text, &tw, NULL);
    d->putsxy(x + (w - tw) / 2, y, text);
}

static void bk_draw_tabs(struct screen *d, const struct palette *pal,
                         int tab, int y, int h)
{
    static const char *const labels[2] = { "Recent", "All" };
    int x = 6;

    for (int i = 0; i < 2; i++)
    {
        int w;
        d->getstringsize(labels[i], &w, NULL);
        if (i == tab)
        {
            d->set_foreground(pal->sel_bg);
            d->fillrect(x - 4, y + 1, w + 8, h - 2);
            d->set_foreground(pal->accent);
            d->hline(x - 4, x + w + 3, y + h - 2);
            d->hline(x - 4, x + w + 3, y + h - 3);
        }
        d->set_foreground(i == tab ? pal->accent : pal->dim);
        d->putsxy(x, y + (h - d->getcharheight()) / 2, labels[i]);
        x += w + 20;
    }
}

static void bk_draw_pane(struct screen *d, const struct palette *pal,
                         int sel_book, bool reading, int px, int pw,
                         int top, int side, int fh)
{
    int x = px + (pw - side) / 2;
    int y = top + 4;
    char line[48];
    const struct bk_book *b = &bk_books[sel_book];
    bool have = (bk_det.book == sel_book);
    const struct bk_mem *m = &bk_mem[sel_book];
    bool info = m->have;

    /* cover or placeholder */
    if (have && bk_cover_ok)
    {
        d->bitmap(bk_cover_bm.data,
                  x + (side - bk_cover_bm.width) / 2,
                  y + (side - bk_cover_bm.height) / 2,
                  bk_cover_bm.width, bk_cover_bm.height);
    }
    else
    {
        d->set_foreground(pal->dim);
        d->drawrect(x, y, side, side);
        d->set_foreground(pal->dim);
        draw_builtin_icon(d, TILE_AUDIOBOOKS,
                          x + (side - BUILTIN_ICON_PX) / 2,
                          y + (side - BUILTIN_ICON_PX) / 2);
    }
    y += side + 4;

    if (info && m->total_s > 0)
    {
        unsigned long pos_s = m->pos_known ? m->pos_s : 0;
        int pct = (int)(pos_s * 100 / m->total_s);
        int fill = (int)((side - 2) * (unsigned long long)pos_s /
                         m->total_s);
        d->set_foreground(pal->dim);
        d->drawrect(x, y, side, 6);
        d->set_foreground(pal->accent);
        d->fillrect(x + 1, y + 1, fill, 4);
        y += 8;

        if (m->pos_known && m->started)
        {
            snprintf(line, sizeof(line), "%d%% completed", pct);
            d->set_foreground(pal->accent);
            bk_text_centered(d, px, pw, y, line);
            y += fh + 2;
            bk_format_left(line, sizeof(line), m->total_s - m->pos_s);
        }
        else
        {
            d->set_foreground(pal->accent);
            if (m->pos_known)
                bk_text_centered(d, px, pw, y, "Not started");
            y += fh + 2;
            bk_format_left(line, sizeof(line), m->total_s);
        }
        d->set_foreground(pal->dim);
        bk_text_centered(d, px, pw, y, line);
        y += fh + 2;
    }
    else
    {
        (void)reading;
        y += 8 + 2 * (fh + 2);
    }

    /* title and author */
    snprintf(line, sizeof(line), "%s", info && m->title[0] ? m->title
                                                              : b->name);
    fit_text(d, line, pw - 4);
    d->set_foreground(pal->fg);
    bk_text_centered(d, px, pw, y, line);
    y += fh + 2;

    snprintf(line, sizeof(line), "%s",
             info ? m->author : b->parent);
    if (line[0])
    {
        fit_text(d, line, pw - 4);
        d->set_foreground(pal->dim);
        bk_text_centered(d, px, pw, y, line);
    }
}

/* ---- opening a book -------------------------------------------------------- */

/* Returns true when the book plays (go to Now Playing). */
static bool bk_open(int book)
{
    const char *dir = bk_books[book].path;
    char bmark[MAX_PATH];

    snprintf(bmark, sizeof(bmark), "%s.bmark", dir);
    if (!file_exists(bmark))
    {
        /* never started: offer the chapter list before starting */
        MENUITEM_STRINGLIST(start_menu, "Start listening", NULL,
                            "Start from the beginning", "Choose chapter...");
        int choice = do_menu(&start_menu, NULL, NULL, false);
        if (choice == 1)
        {
            bookmark_autoload(dir);     /* saves what is playing now */
            bool ok = chapters_choose(dir);
            if (ok)
                bk_recent_add(dir);
            return ok;
        }
        if (choice != 0)
            return false;
    }

    int res = bookmark_autoload(dir);   /* the resume menu, if there is one */

    if (res == BOOKMARK_CANCEL)
        return false;
    if (res == BOOKMARK_DO_RESUME)
    {
        bk_recent_add(dir);
        return true;
    }

    /* no bookmark, or "start from the beginning" */
    if (!warn_on_pl_erase())
        return false;
    splash(0, ID2P(LANG_WAIT));
    if (playlist_create(dir, NULL) == -1)
        return false;
    playlist_insert_directory(NULL, dir, PLAYLIST_INSERT_LAST, false, false,
                              NULL);
    playlist_start(0, 0, 0);
    bk_recent_add(dir);
    return true;
}

/* ---- the page ----------------------------------------------------------------- */

/* Returns true when the icon home should be left (with *result as the next
 * screen), false to go back to the grid. */
static bool books_page(struct screen *display, struct viewport *vp,
                       int *result)
{
    struct palette pal;
    int tab = BK_TAB_RECENT;
    int sel = 0, top = 0;
    bool leave = false, done = false;
    bool need_load = false;      /* the shown book has no details yet */
    int stage = 0;               /* pending load: 1 = cover, 2 = the rest */

    action_wait_for_release();
    splash(0, ID2P(LANG_WAIT));
    bk_scan();
    bk_recent_load();
    bk_sync_playing();
    bk_mem_load();
    bk_det.book = -1;
    memset(bk_pre_cover, 0, sizeof(bk_pre_cover));
    memset(bk_pre_info, 0, sizeof(bk_pre_info));
    bk_build_view(BK_TAB_RECENT);
    if (bk_nview == 0)
    {
        tab = BK_TAB_ALL;
        bk_build_view(tab);
    }

    if (bk_nview)
        stage = 1;      /* show the first book's cover right away */

    {
        int fh = display->getcharheight();
        int hh = fh + 6;
        int pane_h = vp->height - hh - (fh + 8) - (fh + 9) - 8;
        int side = pane_h - (4 + 8 + 4 * (fh + 2) + 4);
        if (side > 98)
            side = 98;
        if (side < 40)
            side = 40;
        bk_cover_alloc(side);
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
        int tabs_h = fh + 8;
        int foot_h = fh + 9;
        int fy = height - foot_h;
        int ly = hh + tabs_h;               /* list top */
        int list_w = width * 45 / 100;
        int row_h = fh + 6;
        int visible = (fy - ly - 4) / row_h;
        if (visible < 1)
            visible = 1;

        if (sel >= bk_nview)
            sel = bk_nview ? bk_nview - 1 : 0;
        if (sel < top)
            top = sel;
        if (sel >= top + visible)
            top = sel - visible + 1;

        draw_header(display, &pal, "Books", width, hh);
        bk_draw_tabs(display, &pal, tab, hh, tabs_h);
        display->set_foreground(pal.dim);
        display->hline(0, width - 1, ly - 1);
        display->vline(list_w, ly, fy - 1);

        if (bk_nview == 0)
        {
            display->set_foreground(pal.dim);
            if (bk_nbooks == 0)
            {
                display->putsxy(6, ly + 6, "No books found");
                display->putsxy(6, ly + 6 + fh + 2, "Put each book in");
                display->putsxy(6, ly + 6 + 2 * (fh + 2),
                                BOOKS_ROOT "/<Title>");
            }
            else
                display->putsxy(6, ly + 6, "Nothing recent yet");
        }
        else
        {
            for (int r = 0; r < visible && top + r < bk_nview; r++)
            {
                int idx = top + r;
                int y = ly + 2 + r * row_h;
                char name[64];

                if (idx == sel)
                {
                    display->set_foreground(pal.sel_bg);
                    display->fillrect(1, y, list_w - 2, row_h - 1);
                    display->set_foreground(pal.accent);
                    display->drawrect(1, y, list_w - 2, row_h - 1);
                }
                snprintf(name, sizeof(name), "%s", bk_books[bk_view[idx]].name);
                fit_text(display, name, list_w - 12);
                display->set_foreground(idx == sel ? pal.accent : pal.fg);
                display->putsxy(6, y + (row_h - 1 - fh) / 2, name);
            }
            if (top > 0)
            {
                display->set_foreground(pal.dim);
                display->putsxy(list_w - 10, ly + 2, "^");
            }
            if (top + visible < bk_nview)
            {
                display->set_foreground(pal.dim);
                display->putsxy(list_w - 10, fy - fh - 2, "v");
            }
            bk_draw_pane(display, &pal, bk_view[sel], stage == 2,
                         list_w + 4, width - list_w - 8, ly, bk_cover_side,
                         fh);
            need_load = (bk_det.book != bk_view[sel]);
        }

        display->set_foreground(pal.dim);
        display->hline(0, width - 1, fy);
        display->putsxy(6, fy + 3, "L/R tab   MID open   BACK home");
        display->update();

        if (stage == 1)
        {
            bk_load_stage1(bk_view[sel]);
            stage = 2;      /* draw the cover, then do the slow part */
            continue;
        }
        if (stage == 2)
        {
            bk_load_stage2(bk_view[sel]);
            stage = 0;
            continue;
        }

        long button = button_get_w_tmo(HZ / 6);
        switch (button)
        {
            case BUTTON_UP:
            case BUTTON_UP | BUTTON_REPEAT:
                if (bk_nview)
                    sel = (sel + bk_nview - 1) % bk_nview;
                break;
            case BUTTON_DOWN:
            case BUTTON_DOWN | BUTTON_REPEAT:
                if (bk_nview)
                    sel = (sel + 1) % bk_nview;
                break;
            case BUTTON_LEFT:
            case BUTTON_RIGHT:
                tab = (tab == BK_TAB_RECENT) ? BK_TAB_ALL : BK_TAB_RECENT;
                bk_build_view(tab);
                sel = top = 0;
                break;
            case BUTTON_SELECT | BUTTON_REL:
                if (bk_nview)
                {
                    int book = bk_view[sel];
                    bool play = bk_open(book);
                    display->set_viewport(vp);
                    action_wait_for_release();
                    if (play)
                    {
                        *result = GO_TO_WPS;
                        leave = true;
                        done = true;
                    }
                    else
                    {
                        /* the recent order may have changed */
                        bk_build_view(tab);
                        if (sel >= bk_nview)
                            sel = bk_nview ? bk_nview - 1 : 0;
                    }
                }
                break;
            case BUTTON_PLAYPAUSE | BUTTON_REL:
                if (audio_status() &
                    (AUDIO_STATUS_PLAY | AUDIO_STATUS_PAUSE))
                {
                    *result = GO_TO_WPS;
                    leave = true;
                    done = true;
                }
                break;
            case BUTTON_BACK | BUTTON_REL:
                done = true;
                break;
            case BUTTON_NONE:
                /* nothing pressed for a moment: load the shown book */
                if (need_load && bk_nview)
                    stage = 1;
                else if (bk_nview)
                    bk_precache_step(sel);
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

    bk_cover_free();
    bk_det.book = -1;
    return leave;
}
