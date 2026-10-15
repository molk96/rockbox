/***************************************************************************
 * Chapter selector for audiobooks, see chapters.h.
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
#include "chapters.h"

#ifdef HAVE_CHAPTERS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "system.h"
#include "file.h"
#include "dir.h"
#include "action.h"
#include "list.h"
#include "lang.h"
#include "settings.h"
#include "splash.h"
#include "misc.h"
#include "tree.h"
#include "filetypes.h"
#include "strnatcmp.h"
#include "string-extra.h"
#include "mp4chapters.h"

#define CH_FILES_MAX 200
#define CH_FILE_POOL 12288
#define CH_MAX       300

struct ch_item
{
    unsigned long start_ms;
    short file;                 /* index into ch_files */
    char title[MP4CH_TITLE_LEN];
};

static struct ch_item ch_items[CH_MAX];
static int ch_nitems;
static int ch_cur;              /* chapter being listened to, -1 unknown */
static bool ch_in_file;         /* chapters are inside one file */

static const char *ch_files[CH_FILES_MAX];
static char ch_pool[CH_FILE_POOL];
static int ch_nfiles;

static struct mp4_chapter ch_mp4[CH_MAX];
static char ch_line[1024];

static int ch_name_cmp(const void *a, const void *b)
{
    return strnatcasecmp(*(const char * const *)a, *(const char * const *)b);
}

static void ch_list_files(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    size_t used = 0;

    ch_nfiles = 0;
    if (!d)
        return;
    while ((e = readdir(d)) != NULL && ch_nfiles < CH_FILES_MAX)
    {
        struct dirinfo info = dir_get_info(d, e);
        size_t len = strlen(e->d_name) + 1;
        if ((info.attribute & ATTR_DIRECTORY) ||
            (filetype_get_attr(e->d_name) & FILE_ATTR_MASK) != FILE_ATTR_AUDIO ||
            used + len > sizeof(ch_pool))
            continue;
        memcpy(ch_pool + used, e->d_name, len);
        ch_files[ch_nfiles++] = ch_pool + used;
        used += len;
    }
    closedir(d);
    qsort(ch_files, ch_nfiles, sizeof(ch_files[0]), ch_name_cmp);
}

/* file name without its extension, as a chapter title */
static void ch_title_from_name(char *dst, size_t size, const char *name)
{
    strlcpy(dst, name, size);
    char *dot = strrchr(dst, '.');
    if (dot && dot != dst)
        *dot = '\0';
}

/* Reads the position from the book's bookmark: the file name and the time
 * in that file. Returns false when there is no bookmark. */
static bool ch_read_position(const char *dir, char *file, size_t fsize,
                             unsigned long *elapsed_ms)
{
    char name[MAX_PATH];
    unsigned long tok[5] = { 0, 0, 0, 0, 0 };

    snprintf(name, sizeof(name), "%s.bmark", dir);
    int fd = open(name, O_RDONLY);
    if (fd < 0)
        return false;
    int len = read_line(fd, ch_line, sizeof(ch_line));
    close(fd);
    if (len <= 0)
        return false;

    /* ">flags;index;offset;seed;elapsed;..." (old format has no flags) */
    const char *s = ch_line;
    if (*s == '>')
        s++;
    for (int i = 0; i < 5; i++)
    {
        tok[i] = strtoul(s, (char **)&s, 10);
        if (*s != ';')
            break;
        s++;
    }
    *elapsed_ms = tok[4];

    const char *f = strrchr(ch_line, ';');
    if (!f)
        return false;
    const char *slash = strrchr(f, '/');
    strlcpy(file, slash ? slash + 1 : f + 1, fsize);
    return true;
}

static void ch_find_current(const char *dir)
{
    char file[MAX_PATH];
    unsigned long elapsed;

    ch_cur = -1;
    if (!ch_read_position(dir, file, sizeof(file), &elapsed))
        return;
    if (ch_in_file)
    {
        for (int i = 0; i < ch_nitems; i++)
            if (ch_items[i].start_ms <= elapsed)
                ch_cur = i;
    }
    else
    {
        for (int i = 0; i < ch_nitems; i++)
            if (!strcasecmp(ch_files[ch_items[i].file], file))
                ch_cur = i;
    }
}

static const char *ch_get_name(int selected_item, void *data, char *buffer,
                               size_t buffer_len)
{
    (void)data;
    if (selected_item < 0 || selected_item >= ch_nitems)
        return "";
    const struct ch_item *it = &ch_items[selected_item];
    const char *mark = (selected_item == ch_cur) ? "> " : "";

    if (ch_in_file)
    {
        unsigned long s = it->start_ms / 1000;
        snprintf(buffer, buffer_len, "%s%lu:%02lu:%02lu  %s", mark,
                 s / 3600, (s / 60) % 60, s % 60, it->title);
    }
    else
        snprintf(buffer, buffer_len, "%s%s", mark, it->title);
    return buffer;
}

static bool ch_picked;

static int ch_action(int action, struct gui_synclist *lists)
{
    (void)lists;
    /* returning CANCEL for OK ends the list with the selection set; Back
       also ends it, so remember which one it was */
    if (action == ACTION_STD_OK)
    {
        ch_picked = true;
        return ACTION_STD_CANCEL;
    }
    return action;
}

bool chapters_choose(const char *dir)
{
    char path[MAX_PATH];
    char resume[MAX_PATH];
    char fname[MAX_PATH];

    splash(0, ID2P(LANG_WAIT));
    ch_nitems = 0;
    ch_in_file = false;
    ch_list_files(dir);

    if (ch_nfiles >= 2)
    {
        for (int i = 0; i < ch_nfiles && ch_nitems < CH_MAX; i++)
        {
            ch_items[ch_nitems].start_ms = 0;
            ch_items[ch_nitems].file = i;
            ch_title_from_name(ch_items[ch_nitems].title,
                               sizeof(ch_items[0].title), ch_files[i]);
            ch_nitems++;
        }
    }
    else if (ch_nfiles == 1)
    {
        snprintf(path, sizeof(path), "%s/%s", dir, ch_files[0]);
        int n = mp4_read_chapters(path, ch_mp4, CH_MAX);
        for (int i = 0; i < n; i++)
        {
            ch_items[i].start_ms = ch_mp4[i].start_ms;
            ch_items[i].file = 0;
            strlcpy(ch_items[i].title, ch_mp4[i].title,
                    sizeof(ch_items[i].title));
        }
        ch_nitems = n;
        ch_in_file = true;
    }

    if (ch_nitems == 0)
    {
        splash(HZ * 2, "No chapters found");
        return false;
    }

    ch_find_current(dir);

    struct simplelist_info info;
    simplelist_info_init(&info, "Chapters", ch_nitems, NULL);
    info.get_name = ch_get_name;
    info.action_callback = ch_action;
    info.selection = ch_cur >= 0 ? ch_cur : 0;
    ch_picked = false;
    if (simplelist_show_list(&info) || !ch_picked || info.selection < 0 ||
        info.selection >= ch_nitems)
        return false;

    const struct ch_item *it = &ch_items[info.selection];
    int index = ch_in_file ? 0 : it->file;
    unsigned long elapsed = ch_in_file ? it->start_ms : 0;

    strlcpy(resume, dir, sizeof(resume));
    strlcpy(fname, ch_files[ch_in_file ? 0 : it->file], sizeof(fname));
    if (!warn_on_pl_erase())
        return false;
    splash(0, ID2P(LANG_WAIT));
    return bookmark_play(resume, index, elapsed, 0, 0, fname, 0);
}

#endif /* HAVE_CHAPTERS */
