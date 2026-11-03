/*
 * hk_csv.c - Daily-rotating CSV logger
 */

#include "hk_csv.h"
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

    char tmp[400];
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

void hk_csv_init(HkCsv *csv, const char *base_dir, const char *prefix,
                 uint32_t min_interval_s)
{
    memset(csv, 0, sizeof(*csv));
    snprintf(csv->base_dir, sizeof(csv->base_dir), "%s", base_dir);
    snprintf(csv->prefix, sizeof(csv->prefix), "%s", prefix);
    csv->min_interval_s = min_interval_s;
}

int hk_csv_day_dir(const char *base_dir, time_t ts, char *out, size_t out_sz)
{
    struct tm tmv;
    gmtime_r(&ts, &tmv);

    char date_str[9];
    if (strftime(date_str, sizeof(date_str), "%Y%m%d", &tmv) != 8)
        return -1;

    snprintf(out, out_sz, "%s/%s", base_dir, date_str);
    return ensure_dir(out);
}

static int rotate_if_needed(HkCsv *csv, time_t ts, const char *header_line)
{
    struct tm tmv;
    gmtime_r(&ts, &tmv);

    char date_str[9];
    if (strftime(date_str, sizeof(date_str), "%Y%m%d", &tmv) != 8)
        return -1;

    if (csv->fp && strcmp(date_str, csv->cur_date) == 0)
        return 0;

    if (csv->fp) {
        fclose(csv->fp);
        csv->fp = NULL;
    }

    char path[512];
    if (ensure_dir(csv->base_dir) < 0)
        return -1;

    snprintf(path, sizeof(path), "%s/%s_%s.csv", csv->base_dir, csv->prefix, date_str);

    int need_header = (access(path, F_OK) != 0);

    csv->fp = fopen(path, "a");
    if (!csv->fp)
        return -1;

    setvbuf(csv->fp, NULL, _IOLBF, 0);

    if (need_header) {
        fprintf(csv->fp, "%s\n", header_line);
        fflush(csv->fp);
    }

    snprintf(csv->cur_date, sizeof(csv->cur_date), "%s", date_str);
    return 0;
}

int hk_csv_append(HkCsv *csv, const char *header_line, time_t ts, const char *row_fields)
{
    if (csv->last_sample_ts && ts >= csv->last_sample_ts &&
        (uint64_t)(ts - csv->last_sample_ts) < csv->min_interval_s)
        return 0;
    if (rotate_if_needed(csv, ts, header_line) < 0)
        return -1;

    fprintf(csv->fp, "%ld,%s\n", (long)ts, row_fields);
    if (fflush(csv->fp) != 0)
        return -1;
    csv->last_sample_ts = ts;
    return 0;
}

void hk_csv_close(HkCsv *csv)
{
    if (csv->fp) {
        fclose(csv->fp);
        csv->fp = NULL;
    }
}
