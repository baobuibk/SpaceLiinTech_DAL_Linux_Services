#include "services/obc_hk_collector.h"

#include "hk_bme_src.h"
#include "hk_csv.h"
#include "hk_fs.h"
#include "hk_temp_src.h"
#include "pac1934.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define OBC_CSV_HEADER \
    "obc_timestamp,temp_valid,temp_onboard_c,temp_ntc1_c,temp_ntc2_c,temp_ntc3_c,temp_ntc4_c," \
    "bme_valid,bme_temp_c,bme_pressure_pa,bme_humidity_rh,pac_valid,pac_u6_valid,pac_u2_valid," \
    "pwr_5v_sama7_v,pwr_5v_fpga_v,pwr_3v3_v,pwr_5v_ai_v,pwr_pmic_3v3_v,pwr_pmic_ddr_1v35_v," \
    "pwr_pmic_core_1v15_v,pwr_pmic_cpu_1v15_v,cur_5v_sama7_a,cur_5v_fpga_a,cur_3v3_a," \
    "cur_5v_ai_a,cur_pmic_3v3_a,cur_pmic_ddr_1v35_a,cur_pmic_core_1v15_a,cur_pmic_cpu_1v15_a"

static pthread_t worker;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static bool running;

static const char *setting(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value && *value ? value : fallback;
}

static long boot_count(void)
{
    FILE *file = fopen(setting("DAL_BOOT_COUNT_FILE", "/home/root/.boot_count"), "r");
    long count = -1;
    if (file) {
        if (fscanf(file, "%ld", &count) != 1) count = -1;
        fclose(file);
    }
    return count;
}

static void *collect(void *unused)
{
    (void)unused;
    HkFs fs;
    HkCsv csv;
    HkPac1934 pac[2];
    int available[2] = {0, 0};
    char csv_dir[512];
    const char *run_dir = setting("DAL_OBC_HK_RUN_DIR", "/run/dal/dal_obc_fsw");
    const char *data_dir = setting("DAL_DATA_DIR", "/home/bee/data");
    if (hk_fs_init(&fs, run_dir) < 0) return NULL;
    if (snprintf(csv_dir, sizeof(csv_dir), "%s/logs/hk", data_dir) >= (int)sizeof(csv_dir))
        return NULL;
    hk_csv_init(&csv, csv_dir, "HK0", 60);
    const long count = boot_count();
    while (true) {
        pthread_mutex_lock(&lock);
        bool active = running;
        pthread_mutex_unlock(&lock);
        if (!active) break;

        HkObcTemp temp = {0};
        HkBme bme = {0};
        float volt[8] = {0}, curr[8] = {0};
        int valid[2] = {0, 0};
        time_t now = time(NULL);
        hk_temp_src_read(setting("DAL_TEMP_RUN_DIR", "/run/dal/temp"), &temp);
        hk_bme_src_read(setting("DAL_BME_RUN_DIR", "/run/dal/bme688"), now, &bme);
        for (int chip = 0; chip < 2; chip++) {
            if (!available[chip])
                available[chip] = hk_pac1934_init(&pac[chip], 0,
                    chip == 0 ? 0x1f : 0x10, chip) == 0;
            valid[chip] = available[chip] &&
                hk_pac1934_read(&pac[chip], volt + chip * 4, curr + chip * 4) == 0;
            if (!valid[chip]) {
                memset(volt + chip * 4, 0, 4 * sizeof(float));
                memset(curr + chip * 4, 0, 4 * sizeof(float));
            }
        }
        char row[512];
        snprintf(row, sizeof(row),
            "%d,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%.2f,%.1f,%.2f,%d,%d,%d,"
            "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
            "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",
            temp.valid, temp.onboard_c, temp.ntc_c[0], temp.ntc_c[1],
            temp.ntc_c[2], temp.ntc_c[3], bme.valid, bme.temperature_c,
            bme.pressure_pa, bme.humidity_rh, valid[0] && valid[1],
            valid[0], valid[1], volt[0], volt[1], volt[2], volt[3],
            volt[4], volt[5], volt[6], volt[7], curr[0], curr[1], curr[2],
            curr[3], curr[4], curr[5], curr[6], curr[7]);
        hk_csv_append(&csv, OBC_CSV_HEADER, now, row);
        hk_fs_write_status(&fs, "obc", 1);
        hk_fs_write_obc_json(&fs, now, &temp, &bme, valid, now, volt, curr, count);

        pthread_mutex_lock(&lock);
        struct timespec next;
        clock_gettime(CLOCK_REALTIME, &next);
        next.tv_sec += 5;
        while (running && pthread_cond_timedwait(&wake, &lock, &next) == 0) {}
        pthread_mutex_unlock(&lock);
    }
    hk_fs_write_status(&fs, "obc", 0);
    hk_csv_close(&csv);
    return NULL;
}

int obc_hk_collector_start(void)
{
    if (!strcmp(setting("DAL_OBC_HK_COLLECTOR_ENABLE", "1"), "0")) return 0;
    pthread_mutex_lock(&lock);
    if (running) { pthread_mutex_unlock(&lock); return -1; }
    running = true;
    int rc = pthread_create(&worker, NULL, collect, NULL);
    if (rc) running = false;
    pthread_mutex_unlock(&lock);
    return rc ? -1 : 0;
}

void obc_hk_collector_stop(void)
{
    pthread_mutex_lock(&lock);
    bool started = running;
    running = false;
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&lock);
    if (started) pthread_join(worker, NULL);
}
