#pragma once
/*
 * hk_fs.h - Filesystem publish interface for hk_service
 *
 * Layout under run_dir/ (default /run/dal/hk_service/):
 *   obc_status     "online\n" / "offline\n"
 *   obc_data.json  latest OBC temp + PAC1934 power snapshot
 *   exp_status     "online\n" / "offline\n"
 *   exp_data.json  latest EXP table-2 sensor snapshot
 *
 * All writes are atomic (write-to-tmp then rename), matching the
 * convention already used by temp_fs / imu_fs.
 *
 * Display-layer notes (JSON only -- CSV and CAN/temp values are untouched):
 *   - NTC channels read below -40 degC mean "sensor not populated" (floating
 *     ADC), so they are emitted as the JSON string "N/A" instead of a number.
 *   - EXP IMU fields are emitted as imu_accel_mg/imu_gyro_dps/imu_temp_c
 *     (the LSM6xxx part is an IMU; internal struct field names are unchanged).
 */

#include <time.h>
#include "hk_can.h"
#include "hk_temp_src.h"
#include "hk_bme_src.h"

typedef struct {
    char run_dir[256];
} HkFs;

int hk_fs_init(HkFs *fs, const char *run_dir);

/* online: 1 = online, 0 = offline. board: "obc" | "exp". */
int hk_fs_write_status(HkFs *fs, const char *board, int online);

int hk_fs_write_exp_json(HkFs *fs, const char *board, int node_addr,
                          int online, time_t ts, const HkExpSensors *d);
int hk_fs_write_exp_invalid_json(HkFs *fs, const char *board, int node_addr,
                                  int online, time_t ts);

/* boot_count: from /home/root/.boot_count, -1 if unreadable (written as "N/A"). */
int hk_fs_write_obc_json(HkFs *fs, time_t ts, const HkObcTemp *temp,
                          const HkBme *bme,
                          const int pac_valid[2],
                          time_t pac_sampled_at,
                          const float pwr_volt_v[8], const float pwr_curr_a[8],
                          long boot_count);
