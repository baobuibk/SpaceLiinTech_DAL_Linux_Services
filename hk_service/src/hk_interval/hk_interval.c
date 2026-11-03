/*
 * hk_interval.c - Per-board runtime-adjustable collect interval
 */

#include "hk_interval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

static int ensure_dir(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) == 0)
        return S_ISDIR(st.st_mode) ? 0 : -1;

    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return (mkdir(tmp, 0755) == 0 || errno == EEXIST) ? 0 : -1;
}

void hk_interval_init(HkInterval *iv, const char *var_dir, const char *name, uint32_t default_s)
{
    iv->default_s = default_s;
    snprintf(iv->path, sizeof(iv->path), "%s/%s.interval", var_dir, name);

    if (ensure_dir(var_dir) < 0) {
        fprintf(stderr, "[hk_interval] cannot create %s: %s\n", var_dir, strerror(errno));
        return;
    }

    if (access(iv->path, F_OK) == 0)
        return; /* keep whatever the operator/previous run already set */

    FILE *f = fopen(iv->path, "w");
    if (!f) {
        fprintf(stderr, "[hk_interval] cannot seed %s: %s\n", iv->path, strerror(errno));
        return;
    }
    fprintf(f, "%u\n", default_s);
    fclose(f);
}

uint32_t hk_interval_get(const HkInterval *iv)
{
    FILE *f = fopen(iv->path, "r");
    if (!f)
        return iv->default_s;

    long val;
    int n = fscanf(f, "%ld", &val);
    fclose(f);

    if (n != 1)
        return iv->default_s;
    if (val < HK_INTERVAL_MIN_S) return HK_INTERVAL_MIN_S;
    if (val > HK_INTERVAL_MAX_S) return HK_INTERVAL_MAX_S;
    return (uint32_t)val;
}
