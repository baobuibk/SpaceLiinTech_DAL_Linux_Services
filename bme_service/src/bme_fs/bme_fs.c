/*
 * bme_fs.c - Atomic filesystem writes for BME688 data
 */

#include "bme_fs.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static int write_atomic(const char *path, const char *content)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;

    size_t len = strlen(content);
    ssize_t w  = write(fd, content, len);
    close(fd);

    if (w != (ssize_t)len) {
        unlink(tmp);
        return -1;
    }
    return rename(tmp, path);
}

static void mkpath(char *out, size_t sz, const char *dir, const char *name)
{
    snprintf(out, sz, "%s/%s", dir, name);
}

static int ensure_dir(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) == 0)
        return S_ISDIR(st.st_mode) ? 0 : -1;

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

int bme_fs_init(BmeFs *fs, const char *dir)
{
    snprintf(fs->fs_dir, sizeof(fs->fs_dir), "%s", dir);

    if (ensure_dir(fs->fs_dir) < 0) {
        fprintf(stderr, "[bme_fs] cannot create dir %s: %s\n",
                fs->fs_dir, strerror(errno));
        return -1;
    }

    return bme_fs_dummy(fs);
}

int bme_fs_dummy(BmeFs *fs)
{
    char path[512];
    const char *dir = fs->fs_dir;

#define WF(name, val) \
    mkpath(path, sizeof(path), dir, name); \
    write_atomic(path, val)

    WF("bme_status",      "0\n");
    WF("bme_temperature", "0.00\n");
    WF("bme_pressure",    "0.00\n");
    WF("bme_humidity",    "0.00\n");
    WF("bme_gas_resistance_ohm", "0.00\n");
    WF("bme_gas_valid", "0\n");
    WF("bme_timestamp",   "0\n");

#undef WF
    return 0;
}

int bme_fs_write(BmeFs *fs, const BmeData *d)
{
    char path[512], buf[64];
    const char *dir = fs->fs_dir;

#define WF(name, ...) \
    mkpath(path, sizeof(path), dir, name); \
    snprintf(buf, sizeof(buf), __VA_ARGS__); \
    write_atomic(path, buf)

    WF("bme_status",      "%d\n",    d->valid);
    WF("bme_temperature", "%.2f\n",  d->temperature);
    WF("bme_pressure",    "%.2f\n",  d->pressure);
    WF("bme_humidity",    "%.2f\n",  d->humidity);
    WF("bme_gas_resistance_ohm", "%.2f\n",
       d->gas_valid ? d->gas_resistance_ohm : 0.0f);
    WF("bme_gas_valid", "%d\n", d->gas_valid ? 1 : 0);
    WF("bme_timestamp",   "%ld\n",   d->timestamp);

#undef WF
    return 0;
}
