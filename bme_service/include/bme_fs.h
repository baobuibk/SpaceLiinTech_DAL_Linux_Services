#pragma once
/*
 * bme_fs.h - Filesystem interface for BME688 data
 *
 * Layout under fs_dir/:
 *   bme_status        "1" = valid, "0" = error/offline
 *   bme_temperature   degC
 *   bme_pressure      Pa
 *   bme_humidity      %rH
 *   bme_gas_valid     "1" = valid gas sample, "0" = unavailable
 *   bme_gas_resistance_ohm   raw resistance in ohms, or 0 when invalid
 *   bme_timestamp     unix epoch of last sample
 */

#include <stdint.h>

#define DEFAULT_BME_FS_DIR  "/run/dal/bme688"

typedef struct {
    int   valid;
    float temperature;
    float pressure;
    float humidity;
    int   gas_valid;
    float gas_resistance_ohm;
    long  timestamp;
} BmeData;

typedef struct {
    char fs_dir[256];
} BmeFs;

/* Create directory, write dummy/offline values. */
int bme_fs_init  (BmeFs *fs, const char *dir);

/* Write all fields atomically. */
int bme_fs_write (BmeFs *fs, const BmeData *d);

/* Write dummy/offline values to all files. */
int bme_fs_dummy (BmeFs *fs);
