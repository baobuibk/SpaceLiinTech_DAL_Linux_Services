#pragma once
/*
 * temp_fs.h - Filesystem interface for temperature data
 *
 * Writes sensor values as text files under a configurable directory.
 * All writes are atomic (write-to-tmp then rename).
 *
 * Layout under fs_dir/:
 *   temp_status        "1" = valid, "0" = error
 *   temp_onboard       °C  (TC1047A via AD9/PC22)
 *   temp_ntc1          °C  (AD2/PC15)
 *   temp_ntc2          °C  (AD4/PC17)
 *   temp_ntc3          °C  (AD3/PC16)
 *   temp_ntc4          °C  (AD7/PC20)
 *   temp_raw_ad9/ad2/ad4/ad3/ad7: raw ADC values
 *   temp_timestamp     unix epoch of last sample
 */

#include <stdint.h>

#define TEMP_NTC_COUNT       4
#define DEFAULT_TEMP_FS_DIR  "/run/dal/temp"

typedef struct {
    int    valid;                   /* 1 = ok, 0 = error/disconnected  */
    float  onboard;                 /* TC1047A °C                       */
    float  ntc[TEMP_NTC_COUNT];     /* NTC1..4 °C                       */
    int    raw_ad[5];               /* AD9, AD2, AD4, AD3, AD7          */
    long   timestamp;               /* unix epoch                       */
} TempData;

typedef struct {
    char fs_dir[256];
} TempFs;

/* Create directory, write dummy/offline values. */
int temp_fs_init  (TempFs *fs, const char *dir);

/* Write all fields atomically. */
int temp_fs_write (TempFs *fs, const TempData *d);

/* Write dummy/offline values to all files. */
int temp_fs_dummy (TempFs *fs);
