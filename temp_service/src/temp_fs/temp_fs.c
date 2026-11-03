/*
 * temp_fs.c - Atomic filesystem writes for temperature data
 */

#include "temp_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

/* -- Atomic write helper ------------------------------------------------ */
static int write_atomic(const char *path, const char *content)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;

    size_t len  = strlen(content);
    ssize_t w   = write(fd, content, len);
    close(fd);

    if (w != (ssize_t)len) {
        unlink(tmp);
        return -1;
    }
    return rename(tmp, path);
}

static void mkpath(char *out, size_t sz,
                   const char *dir, const char *name)
{
    snprintf(out, sz, "%s/%s", dir, name);
}

static int ensure_dir(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        return -1;
    }
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 ? 0 : (errno == EEXIST ? 0 : -1);
}

/* -- Public API --------------------------------------------------------- */

int temp_fs_init(TempFs *fs, const char *dir)
{
    snprintf(fs->fs_dir, sizeof(fs->fs_dir), "%s", dir);

    if (ensure_dir(fs->fs_dir) < 0) {
        fprintf(stderr, "[temp_fs] cannot create dir %s: %s\n",
                fs->fs_dir, strerror(errno));
        return -1;
    }

    return temp_fs_dummy(fs);
}

int temp_fs_dummy(TempFs *fs)
{
    char path[512];
    const char *dir = fs->fs_dir;

#define WF(name, val) \
    mkpath(path, sizeof(path), dir, name); \
    write_atomic(path, val)

    WF("temp_status",    "0\n");
    WF("temp_onboard",   "0.00\n");
    WF("temp_ntc1",      "0.00\n");
    WF("temp_ntc2",      "0.00\n");
    WF("temp_ntc3",      "0.00\n");
    WF("temp_ntc4",      "0.00\n");
    WF("temp_raw_ad9",   "0\n");
    WF("temp_raw_ad2",   "0\n");
    WF("temp_raw_ad4",   "0\n");
    WF("temp_raw_ad3",   "0\n");
    WF("temp_raw_ad7",   "0\n");
    WF("temp_timestamp", "0\n");

#undef WF
    return 0;
}

int temp_fs_write(TempFs *fs, const TempData *d)
{
    char path[512], buf[64];
    const char *dir = fs->fs_dir;

    static const char *ntc_names[] = {
        "temp_ntc1", "temp_ntc2", "temp_ntc3", "temp_ntc4"
    };
    static const char *raw_names[] = {
        "temp_raw_ad9", "temp_raw_ad2",
        "temp_raw_ad4", "temp_raw_ad3", "temp_raw_ad7"
    };

#define WF(name, ...) \
    mkpath(path, sizeof(path), dir, name); \
    snprintf(buf, sizeof(buf), __VA_ARGS__); \
    write_atomic(path, buf)

    WF("temp_status",  "%d\n",    d->valid);
    WF("temp_onboard", "%.2f\n",  d->onboard);

    for (int i = 0; i < TEMP_NTC_COUNT; i++) {
        mkpath(path, sizeof(path), dir, ntc_names[i]);
        snprintf(buf, sizeof(buf), "%.2f\n", d->ntc[i]);
        write_atomic(path, buf);
    }

    for (int i = 0; i < 5; i++) {
        mkpath(path, sizeof(path), dir, raw_names[i]);
        snprintf(buf, sizeof(buf), "%d\n", d->raw_ad[i]);
        write_atomic(path, buf);
    }

    WF("temp_timestamp", "%ld\n", d->timestamp);

#undef WF
    return 0;
}
