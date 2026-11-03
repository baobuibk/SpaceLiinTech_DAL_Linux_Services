/*
 * hk_fs.c - Filesystem publish interface for hk_service
 */

#include "hk_fs.h"
#include <stdio.h>
#include <stdlib.h>
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

    size_t  len = strlen(content);
    ssize_t w   = write(fd, content, len);
    close(fd);

    if (w != (ssize_t)len) {
        unlink(tmp);
        return -1;
    }
    return rename(tmp, path);
}

/* Display-layer only: NTC < -40 degC means "not populated" (floating ADC). */
static void fmt_ntc(char *out, size_t sz, float c)
{
    if (c < -40.0f)
        snprintf(out, sz, "\"N/A\"");
    else
        snprintf(out, sz, "%.2f", c);
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
    return (mkdir(tmp, 0755) == 0 || errno == EEXIST) ? 0 : -1;
}

int hk_fs_init(HkFs *fs, const char *run_dir)
{
    snprintf(fs->run_dir, sizeof(fs->run_dir), "%s", run_dir);
    if (ensure_dir(fs->run_dir) < 0) {
        fprintf(stderr, "[hk_fs] cannot create dir %s: %s\n",
                fs->run_dir, strerror(errno));
        return -1;
    }
    return 0;
}

int hk_fs_write_status(HkFs *fs, const char *board, int online)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s_status", fs->run_dir, board);
    return write_atomic(path, online ? "online\n" : "offline\n");
}

int hk_fs_write_exp_json(HkFs *fs, const char *board, int node_addr,
                          int online, time_t ts, const HkExpSensors *d)
{
    char path[512], buf[3072], status[64] = "", age[128] = "";
    size_t status_used = 0, age_used = 0;
    for (int i = 0; i < HK_EXP_GROUP_COUNT; i++) {
        status_used += (size_t)snprintf(status + status_used,
            sizeof(status) - status_used, "%s%u", i ? ", " : "", d->status[i]);
        age_used += (size_t)snprintf(age + age_used,
            sizeof(age) - age_used, "%s%u", i ? ", " : "", d->age_ms[i]);
    }

    snprintf(path, sizeof(path), "%s/%s_data.json", fs->run_dir, board);

    snprintf(buf, sizeof(buf),
        "{\n"
        "  \"board\": \"%s\",\n"
        "  \"schema_version\": 1,\n"
        "  \"node_addr\": %d,\n"
        "  \"online\": %d,\n"
        "  \"data_valid\": 1,\n"
        "  \"obc_timestamp\": %ld,\n"
        "  \"rparam_schema\": %u,\n"
        "  \"sensor_seq\": %u,\n"
        "  \"sensor_tick_ms\": %u,\n"
        "  \"sensor_supported_mask\": %u,\n"
        "  \"sensor_valid_mask\": %u,\n"
        "  \"sensor_status\": [%s],\n"
        "  \"sensor_age_ms\": [%s],\n"
        "  \"ntc_c\": [%.3f, %.3f, %.3f, %.3f],\n"
        "  \"board_temp_c\": %.2f,\n"
        "  \"bme_temp_c\": %.2f,\n"
        "  \"bme_press_pa\": %u,\n"
        "  \"bme_hum_rh\": %.3f,\n"
        "  \"imu_mask\": %u,\n"
        "  \"imu_accel_mg\": [%d, %d, %d],\n"
        "  \"imu_gyro_dps\": [%.3f, %.3f, %.3f],\n"
        "  \"imu_temp_c\": %.3f,\n"
        "  \"flow_ul_min\": [%.2f, %.2f],\n"
        "  \"flow_temp_c\": [%.3f, %.3f],\n"
        "  \"pwr_rail_names\": [\"12V_MOTOR\", \"12V_EXP\", \"5V_EXP\", \"5V_SLN_HD\"],\n"
        "  \"pwr_volt_mv\": [%u, %u, %u, %u],\n"
        "  \"pwr_curr_ma\": [%d, %d, %d, %d],\n"
        "  \"pwr_mw\": [%d, %d, %d, %d],\n"
        "  \"rail_enabled\": [%u, %u, %u]\n"
        "}\n",
        board, node_addr, online, (long)ts,
        d->schema, d->seq, d->tick_ms, d->supported, d->valid,
        status, age,
        d->ntc_c[0], d->ntc_c[1], d->ntc_c[2], d->ntc_c[3],
        d->board_temp_c, d->bme_temp_c, d->bme_press_pa, d->bme_hum_rh,
        d->imu_mask,
        d->accel_mg[0], d->accel_mg[1], d->accel_mg[2],
        d->gyro_dps[0], d->gyro_dps[1], d->gyro_dps[2],
        d->imu_temp_c,
        d->flow_ul_min[0], d->flow_ul_min[1],
        d->flow_temp_c[0], d->flow_temp_c[1],
        d->pwr_volt_mv[0], d->pwr_volt_mv[1], d->pwr_volt_mv[2], d->pwr_volt_mv[3],
        d->pwr_curr_ma[0], d->pwr_curr_ma[1], d->pwr_curr_ma[2], d->pwr_curr_ma[3],
        d->pwr_mw[0], d->pwr_mw[1], d->pwr_mw[2], d->pwr_mw[3],
        d->rail_enabled[0], d->rail_enabled[1], d->rail_enabled[2]);

    return write_atomic(path, buf);
}

int hk_fs_write_exp_invalid_json(HkFs *fs, const char *board, int node_addr,
                                  int online, time_t ts)
{
    char path[512], buf[256];
    snprintf(path, sizeof(path), "%s/%s_data.json", fs->run_dir, board);
    snprintf(buf, sizeof(buf),
             "{\"schema_version\":1,\"board\":\"%s\",\"node_addr\":%d,"
             "\"online\":%d,\"data_valid\":0,\"obc_timestamp\":%ld,"
             "\"sample_unix_s\":null}\n",
             board, node_addr, online, (long)ts);
    return write_atomic(path, buf);
}

int hk_fs_write_obc_json(HkFs *fs, time_t ts, const HkObcTemp *temp,
                          const HkBme *bme,
                          const int pac_valid[2],
                          time_t pac_sampled_at,
                          const float pwr_volt_v[8], const float pwr_curr_a[8],
                          long boot_count)
{
    char path[512], buf[2048], gas_buf[32];
    char ntc[4][16];
    for (int i = 0; i < 4; i++)
        fmt_ntc(ntc[i], sizeof(ntc[i]), temp->ntc_c[i]);
    if (bme->valid && bme->gas_valid)
        snprintf(gas_buf, sizeof(gas_buf), "%.2f", bme->gas_resistance_ohm);
    else
        snprintf(gas_buf, sizeof(gas_buf), "null");

    char boot_buf[32];
    if (boot_count < 0)
        snprintf(boot_buf, sizeof(boot_buf), "\"N/A\"");
    else
        snprintf(boot_buf, sizeof(boot_buf), "%ld", boot_count);

    snprintf(path, sizeof(path), "%s/obc_data.json", fs->run_dir);

    snprintf(buf, sizeof(buf),
        "{\n"
        "  \"board\": \"obc\",\n"
        "  \"schema_version\": 1,\n"
        "  \"online\": 1,\n"
        "  \"boot_count\": %s,\n"
        "  \"obc_timestamp\": %ld,\n"
        "  \"temp_valid\": %d,\n"
        "  \"temp_sample_unix_s\": %ld,\n"
        "  \"temp_onboard_c\": %.2f,\n"
        "  \"temp_ntc_c\": [%s, %s, %s, %s],\n"
        "  \"bme_valid\": %d,\n"
        "  \"bme_sample_unix_s\": %ld,\n"
        "  \"bme_temp_c\": %.2f,\n"
        "  \"bme_pressure_pa\": %.1f,\n"
        "  \"bme_humidity_rh\": %.2f,\n"
        "  \"bme_gas_valid\": %d,\n"
        "  \"bme_gas_resistance_ohm\": %s,\n"
        "  \"pac_valid\": %d,\n"
        "  \"pac_sample_unix_s\": %ld,\n"
        "  \"pac_u6_valid\": %d,\n"
        "  \"pac_u2_valid\": %d,\n"
        "  \"pwr_rail_names\": [\"5V_SAMA7\", \"5V_FPGA\", \"3V3\", \"5V_AI\", "
        "\"PMIC_3V3\", \"PMIC_DDR_1V35\", \"PMIC_CORE_1V15\", \"PMIC_CPU_1V15\"],\n"
        "  \"pwr_volt_v\": [%.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f],\n"
        "  \"pwr_curr_a\": [%.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f]\n"
        "}\n",
        boot_buf, (long)ts, temp->valid, temp->src_timestamp, temp->onboard_c,
        ntc[0], ntc[1], ntc[2], ntc[3],
        bme->valid, (long)bme->src_timestamp, bme->temperature_c,
        bme->pressure_pa, bme->humidity_rh,
        bme->valid && bme->gas_valid, gas_buf,
        pac_valid[0] && pac_valid[1], (long)pac_sampled_at,
        pac_valid[0], pac_valid[1],
        pwr_volt_v[0], pwr_volt_v[1], pwr_volt_v[2], pwr_volt_v[3],
        pwr_volt_v[4], pwr_volt_v[5], pwr_volt_v[6], pwr_volt_v[7],
        pwr_curr_a[0], pwr_curr_a[1], pwr_curr_a[2], pwr_curr_a[3],
        pwr_curr_a[4], pwr_curr_a[5], pwr_curr_a[6], pwr_curr_a[7]);

    return write_atomic(path, buf);
}
