#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
/* bee_dal_files.c */
#include "bee_dal_files.h"
#include "bee_dal_crc32.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define CRC_STREAM_BLOCK 4096

#define BEE_DAL_MARKS_FILE     ".dat_marks"
#define BEE_DAL_MARKS_LOG_FILE ".dat_marks.log"

typedef struct {
    char                 data_dir[256];
    bee_dal_file_entry_t files[3][BEE_DAL_FILES_MAX_HK];
    uint32_t             count[3];
    uint32_t             snap_id;
} bee_dal_files_state_t;

static bee_dal_files_state_t g_files;

/* Mark store keys use relative paths so matching basenames in different
 * source folders remain independent. */
typedef struct { char name[BEE_DAL_FILES_NAME_MAX]; uint8_t mark; } bee_dal_mark_entry_t;

static bee_dal_mark_entry_t g_marks[BEE_DAL_FILES_MAX_HK];
static uint32_t             g_marks_count = 0;

static bool mark_bsearch(const char *name, uint32_t *out_idx)
{
    uint32_t lo = 0, hi = g_marks_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int c = strcmp(g_marks[mid].name, name);
        if (c == 0) { *out_idx = mid; return true; }
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    *out_idx = lo;
    return false;
}

static void mark_set_inmem(const char *name, uint8_t mark)
{
    uint32_t idx;
    if (mark_bsearch(name, &idx)) { g_marks[idx].mark = mark; return; }
    if (g_marks_count >= BEE_DAL_FILES_MAX_HK) {
        fprintf(stderr, "[FILES] mark store full, dropping mark for '%s'\n", name);
        return;
    }
    memmove(&g_marks[idx + 1], &g_marks[idx], (size_t)(g_marks_count - idx) * sizeof(g_marks[0]));
    snprintf(g_marks[idx].name, BEE_DAL_FILES_NAME_MAX, "%s", name);
    g_marks[idx].mark = mark;
    g_marks_count++;
}

static void mark_append_log(const char *name, uint8_t mark)
{
    char path[512];
    int n = snprintf(path, sizeof(path), "%s/%s", g_files.data_dir, BEE_DAL_MARKS_LOG_FILE);
    if (n < 0 || (size_t)n >= sizeof(path)) return;

    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        fprintf(stderr, "[FILES] open marks log '%s': %s\n", path, strerror(errno));
        return;
    }
    char line[BEE_DAL_FILES_NAME_MAX + 8];
    int ln = snprintf(line, sizeof(line), "%s %u\n", name, (unsigned)mark);
    if (ln > 0 && (size_t)ln < (int)sizeof(line)) {
        if (write(fd, line, (size_t)ln) != ln)
            fprintf(stderr, "[FILES] append marks log: short write (%s)\n", strerror(errno));
    }
    close(fd);
}

/* Rewrite the compacted mark store from current g_marks[] RAM state, then
 * reset the log. Called from scan() once the fresh hk[] proves which
 * files still exist. */
static void mark_compact(void)
{
    char path[512], tmp[520];
    int n = snprintf(path, sizeof(path), "%s/%s", g_files.data_dir, BEE_DAL_MARKS_FILE);
    if (n < 0 || (size_t)n >= sizeof(path)) return;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *fp = fopen(tmp, "wb");
    if (!fp) {
        fprintf(stderr, "[FILES] marks compact: cannot write '%s': %s\n", tmp, strerror(errno));
        return;
    }
    for (uint32_t i = 0; i < g_marks_count; i++)
        fprintf(fp, "%s %u\n", g_marks[i].name, (unsigned)g_marks[i].mark);
    fclose(fp);

    if (rename(tmp, path) != 0) {
        fprintf(stderr, "[FILES] marks compact rename '%s' -> '%s': %s\n", tmp, path, strerror(errno));
        return;
    }

    char log_path[512];
    n = snprintf(log_path, sizeof(log_path), "%s/%s", g_files.data_dir, BEE_DAL_MARKS_LOG_FILE);
    if (n < 0 || (size_t)n >= sizeof(log_path)) return;
    int fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);
    else fprintf(stderr, "[FILES] marks log truncate '%s': %s\n", log_path, strerror(errno));
}

/* Defensive line parse -- bounded buffers, reject anything that doesn't
 * cleanly parse (torn write / bit flip on flash must not OOB-read). */
static void mark_parse_line(char *line)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';
    if (len == 0 || len >= (BEE_DAL_FILES_NAME_MAX + 8)) return;

    char *sp = strrchr(line, ' ');
    if (!sp || sp == line) return;
    *sp = '\0';

    size_t nlen = (size_t)(sp - line);
    if (nlen == 0 || nlen >= BEE_DAL_FILES_NAME_MAX) return;

    const char *num = sp + 1;
    if (!*num) return;
    char *endp = NULL;
    long v = strtol(num, &endp, 10);
    if (endp == num || *endp != '\0' || v < 0 || v > 255) return;

    mark_set_inmem(line, (uint8_t)v);
}

static void mark_load_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return; /* absent on first boot -- not an error */

    char line[BEE_DAL_FILES_NAME_MAX + 8];
    while (fgets(line, sizeof(line), fp))
        mark_parse_line(line);
    fclose(fp);
}

/* Baseline = last compacted snapshot, replay log on top (last write wins). */
static void marks_init(void)
{
    g_marks_count = 0;

    char path[512];
    int n = snprintf(path, sizeof(path), "%s/%s", g_files.data_dir, BEE_DAL_MARKS_FILE);
    if (n > 0 && (size_t)n < sizeof(path)) mark_load_file(path);

    n = snprintf(path, sizeof(path), "%s/%s", g_files.data_dir, BEE_DAL_MARKS_LOG_FILE);
    if (n > 0 && (size_t)n < sizeof(path)) mark_load_file(path);

    printf("[FILES] marks loaded: %u entries\n", g_marks_count);
}

static void marks_merge_prune_filter(uint8_t mark_filt)
{
    for (uint32_t type = 0; type < 3; type++) {
        uint32_t kept = 0;
        for (uint32_t i = 0; i < g_files.count[type]; i++) {
        const char *name = g_files.files[type][i].relpath;
        uint32_t mi;
        uint8_t mark = mark_bsearch(name, &mi) ? g_marks[mi].mark : 0;

        if (mark_filt == BEE_DAL_FILES_MARK_ALL || mark == mark_filt) {
            if (kept != i) g_files.files[type][kept] = g_files.files[type][i];
            kept++;
        }
        }
        g_files.count[type] = kept;
    }
}

/* ============================================================================
 * Helpers
 * ============================================================================ */
static int cmp_entry_name(const void *a, const void *b)
{
    return strcmp(((const bee_dal_file_entry_t *)a)->relpath,
                  ((const bee_dal_file_entry_t *)b)->relpath);
}

/* Resolve (snap_id, data_type, idx) -> full path + size. Never touches a
 * client-supplied string -- only ever a name captured by the last scan(). */
static xerr_t resolve(uint32_t snap_id, uint8_t data_type, uint32_t idx,
                      char *out_path, size_t out_path_len, uint32_t *out_size)
{
    if (snap_id != g_files.snap_id) return XERR_STALE;
    if (data_type > 2)              return XERR_NOT_FOUND;
    if (idx >= g_files.count[data_type]) return XERR_RANGE;

    int n = snprintf(out_path, out_path_len, "%s/%s", g_files.data_dir, g_files.files[data_type][idx].relpath);
    if (n < 0 || (size_t)n >= out_path_len) return XERR_IO;
    if (out_size) *out_size = g_files.files[data_type][idx].size;
    return XERR_OK;
}

/* ============================================================================
 * Public API
 * ============================================================================ */
xerr_t bee_dal_files_init(const char *data_dir)
{
    if (data_dir && data_dir[0]) {
        strncpy(g_files.data_dir, data_dir, sizeof(g_files.data_dir) - 1);
        g_files.data_dir[sizeof(g_files.data_dir) - 1] = '\0';
    }
    memset(g_files.count, 0, sizeof(g_files.count));
    g_files.snap_id  = 0;

    marks_init();

    printf("[FILES] backend init: data_dir=%s max_hk=%d\n",
           g_files.data_dir, BEE_DAL_FILES_MAX_HK);
    return XERR_OK;
}

static bool daily_csv_name(const char *name, const char *day)
{
    size_t len = strlen(name);
    return len == 16 &&
           (!memcmp(name, "HK0_", 4) || !memcmp(name, "HK1_", 4)) &&
           !memcmp(name + 4, day, 8) && !memcmp(name + 12, ".csv", 4);
}

static bool active_daily_csv(const char *name)
{
    time_t now = time(NULL);
    struct tm utc;
    char day[9];
    if (!gmtime_r(&now, &utc) || strftime(day, sizeof(day), "%Y%m%d", &utc) != 8)
        return false;
    return daily_csv_name(name, day);
}

static void scan_add_file(uint8_t type, const char *relpath, const char *name,
                          uint32_t *stored, uint32_t *seen)
{
    char full[512];
    int n = snprintf(full, sizeof(full), "%s/%s", g_files.data_dir, relpath);
    if (n < 0 || (size_t)n >= sizeof(full)) return;
    struct stat st;
    if (lstat(full, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) return;
    ++*seen;
    if (*stored >= BEE_DAL_FILES_MAX_HK) return;
    bee_dal_file_entry_t *entry = &g_files.files[type][*stored];
    if (snprintf(entry->name, sizeof(entry->name), "%s", name) >= (int)sizeof(entry->name) ||
        snprintf(entry->relpath, sizeof(entry->relpath), "%s", relpath) >=
            (int)sizeof(entry->relpath)) return;
    entry->size = (uint32_t)st.st_size;
    ++*stored;
}

static const char *const source_dirs[3][4] = {
    {"logs/hk", NULL},
    {"images/usb", "images/mipi", "images/exp", NULL},
    {"logs/lwl", "logs/pc", NULL}
};

static void scan_source(uint8_t type, const char *folder, uint32_t *seen)
{
    char directory[512];
    if (snprintf(directory, sizeof(directory), "%s/%s", g_files.data_dir, folder) >=
        (int)sizeof(directory)) return;
    DIR *dir = opendir(directory);
    if (!dir) return; /* An unused source need not have a folder yet. */
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.' || strlen(ent->d_name) >= BEE_DAL_FILES_NAME_MAX) continue;
        char relative[BEE_DAL_FILES_REL_PATH_MAX];
        if (snprintf(relative, sizeof(relative), "%s/%s", folder, ent->d_name) >=
            (int)sizeof(relative)) continue;
        scan_add_file(type, relative, ent->d_name, &g_files.count[type], seen);
    }
    closedir(dir);
}

xerr_t bee_dal_files_scan(uint8_t mark_filt, uint32_t *out_snap_id, uint32_t *out_hk_count,
                          uint32_t *out_sci1_count, uint32_t *out_sci2_count)
{
    struct stat root;
    if (stat(g_files.data_dir, &root) != 0 || !S_ISDIR(root.st_mode)) return XERR_IO;
    memset(g_files.count, 0, sizeof(g_files.count));
    for (uint8_t type = 0; type < 3; type++) {
        uint32_t seen = 0;
        for (unsigned source = 0; source_dirs[type][source]; source++)
            scan_source(type, source_dirs[type][source], &seen);
        if (seen > g_files.count[type])
            fprintf(stderr, "[FILES] category %u truncated at %u entries\n",
                    type, g_files.count[type]);
        qsort(g_files.files[type], g_files.count[type], sizeof(g_files.files[type][0]),
              cmp_entry_name);
    }
    g_files.snap_id++;
    marks_merge_prune_filter(mark_filt);
    mark_compact();
    if (out_snap_id) *out_snap_id = g_files.snap_id;
    if (out_hk_count) *out_hk_count = g_files.count[0];
    if (out_sci1_count) *out_sci1_count = g_files.count[1];
    if (out_sci2_count) *out_sci2_count = g_files.count[2];
    return XERR_OK;
}

xerr_t bee_dal_files_get_entry(uint32_t snap_id, uint8_t data_type, uint32_t idx,
                                bee_dal_file_entry_t *out)
{
    if (!out) return XERR_INVAL;
    if (snap_id != g_files.snap_id) return XERR_STALE;
    if (data_type > 2) return XERR_NOT_FOUND;
    if (idx >= g_files.count[data_type]) return XERR_RANGE;
    *out = g_files.files[data_type][idx];
    return XERR_OK;
}

xerr_t bee_dal_files_info(uint32_t snap_id, uint8_t data_type, uint32_t idx,
                          uint32_t *out_size, uint32_t *out_crc32)
{
    char     path[512];
    uint32_t size = 0;
    xerr_t   err  = resolve(snap_id, data_type, idx, path, sizeof(path), &size);
    if (err != XERR_OK) return err;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return XERR_NOT_FOUND;

    uint32_t crc = bee_dal_crc32_init();
    uint64_t bytes_read = 0;
    uint8_t  buf[CRC_STREAM_BLOCK];
    ssize_t  n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        crc = bee_dal_crc32_update(crc, buf, (size_t)n);
        bytes_read += (size_t)n;
    }
    close(fd);

    if (n < 0) return XERR_IO;
    /* Listing fixes the file size. Re-list if it changes before selection. */
    if (bytes_read != size) return XERR_STALE;

    if (out_size)  *out_size  = size;
    if (out_crc32) *out_crc32 = bee_dal_crc32_finalize(crc);
    return XERR_OK;
}

xerr_t bee_dal_files_read_chunk(uint32_t snap_id, uint8_t data_type, uint32_t idx,
                                uint32_t chunk_index, uint16_t chunk_size,
                                uint8_t *out_buf, uint16_t *out_len)
{
    if (!out_buf || !out_len || chunk_size == 0) return XERR_INVAL;

    char     path[512];
    uint32_t size = 0;
    xerr_t   err  = resolve(snap_id, data_type, idx, path, sizeof(path), &size);
    if (err != XERR_OK) return err;

    uint32_t nchunks = size / chunk_size + (size % chunk_size != 0);
    if (chunk_index >= nchunks) return XERR_RANGE;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return XERR_NOT_FOUND;

    off_t    offset = (off_t)chunk_index * (off_t)chunk_size;
    uint32_t remain = size - (uint32_t)offset;
    uint16_t psize   = (remain < chunk_size) ? (uint16_t)remain : chunk_size;

    ssize_t rd = pread(fd, out_buf, psize, offset);
    close(fd);

    if (rd < 0 || (uint16_t)rd != psize) return XERR_IO;

    *out_len = psize;
    return XERR_OK;
}

xerr_t bee_dal_files_delete(uint32_t snap_id, uint8_t data_type, uint32_t idx,
                            uint32_t *out_hk_count, uint32_t *out_sci1_count,
                            uint32_t *out_sci2_count)
{
    char path[512];
    xerr_t err = resolve(snap_id, data_type, idx, path, sizeof(path), NULL);
    if (err != XERR_OK) return err;
    bee_dal_file_entry_t *entry = &g_files.files[data_type][idx];
    if (data_type == 0 && active_daily_csv(entry->name)) return XERR_STATE;
    if (remove(path) != 0) return XERR_IO;
    for (uint32_t i = idx; i + 1 < g_files.count[data_type]; i++)
        g_files.files[data_type][i] = g_files.files[data_type][i + 1];
    g_files.count[data_type]--;
    g_files.snap_id++;
    if (out_hk_count) *out_hk_count = g_files.count[0];
    if (out_sci1_count) *out_sci1_count = g_files.count[1];
    if (out_sci2_count) *out_sci2_count = g_files.count[2];
    return XERR_OK;
}

xerr_t bee_dal_files_mark(uint32_t snap_id, uint8_t data_type, uint32_t idx, uint8_t mark)
{
    char path[512];
    xerr_t err = resolve(snap_id, data_type, idx, path, sizeof(path), NULL);
    if (err != XERR_OK) return err;
    const char *key = g_files.files[data_type][idx].relpath;
    mark_set_inmem(key, mark);
    mark_append_log(key, mark);
    return XERR_OK;
}
