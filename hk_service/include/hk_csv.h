#pragma once
/*
 * hk_csv.h - Daily-rotating CSV logger
 *
 * Layout: <base_dir>/<YYYYMMDD>/<prefix>_<YYYYMMDD>.csv
 * Dates are UTC (matches the unix-epoch obc_timestamp column).
 * Every row is fsync-flushed immediately (housekeeping is low-rate,
 * so the durability cost is negligible and worth it on a satellite).
 */

#include <stdio.h>
#include <time.h>
#include <stdint.h>

typedef struct {
    char  base_dir[200];
    char  prefix[16];
    char  cur_date[9];   /* "YYYYMMDD" of the currently open file, "" if none */
    FILE *fp;
    uint32_t min_interval_s;
    time_t last_sample_ts;
} HkCsv;

void hk_csv_init(HkCsv *csv, const char *base_dir, const char *prefix,
                 uint32_t min_interval_s);

/*
 * Append one row at timestamp ts. Automatically rotates to a new
 * date folder/file when the UTC date changes, writing header_line
 * to any newly created file. row_fields is everything AFTER the
 * obc_timestamp column (no leading/trailing comma, no newline) —
 * this function prepends "ts," and appends "\n".
 * Returns 0 on success, -1 on I/O error.
 */
int hk_csv_append(HkCsv *csv, const char *header_line, time_t ts, const char *row_fields);

void hk_csv_close(HkCsv *csv);

/*
 * Ensures <base_dir>/<YYYYMMDD (UTC of ts)>/ exists and writes its path
 * into out. Shared by hk_csv itself and anything else that drops dated
 * files under the same data root (e.g. hk_data_ready's .bin dumps).
 * Returns 0 on success, -1 if the directory could not be created.
 */
int hk_csv_day_dir(const char *base_dir, time_t ts, char *out, size_t out_sz);
