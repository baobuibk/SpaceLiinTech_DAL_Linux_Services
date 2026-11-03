/*
 * hk_temp_src.c - Read-only consumer of temp_service's published files
 */

#include "hk_temp_src.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <time.h>

#define SENSOR_MAX_AGE_S 10

static int read_file_line(const char *path, char *buf, size_t sz)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, (int)sz, f)) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

static int read_number(const char *dir, const char *name, double *value)
{
    char path[512], buf[80], *end;
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path))
        return -1;
    if (read_file_line(path, buf, sizeof(buf)) < 0) return -1;
    errno = 0;
    *value = strtod(buf, &end);
    if (errno || end == buf || !isfinite(*value)) return -1;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
    return *end == '\0' ? 0 : -1;
}

int hk_temp_src_read(const char *temp_dir, HkObcTemp *out)
{
    memset(out, 0, sizeof(*out));
    double status, before, after, value;
    if (read_number(temp_dir, "temp_timestamp", &before) < 0 ||
        read_number(temp_dir, "temp_status", &status) < 0 ||
        read_number(temp_dir, "temp_onboard", &value) < 0)
        return -1;
    out->onboard_c = (float)value;

    static const char *ntc_names[HK_TEMP_NTC_COUNT] = {
        "temp_ntc1", "temp_ntc2", "temp_ntc3", "temp_ntc4"
    };
    for (int i = 0; i < HK_TEMP_NTC_COUNT; i++) {
        if (read_number(temp_dir, ntc_names[i], &value) < 0)
            return -1;
        out->ntc_c[i] = (float)value;
    }
    if (read_number(temp_dir, "temp_timestamp", &after) < 0 ||
        before != after || status != 1 || before <= 0 ||
        before > (double)time(NULL) + 2 ||
        (double)time(NULL) - before > SENSOR_MAX_AGE_S) {
        memset(out, 0, sizeof(*out));
        return -1;
    }
    out->valid = 1;
    out->src_timestamp = (long)after;
    return 0;
}
