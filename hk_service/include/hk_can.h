#pragma once

/* DAL EXP firmware: one CSP node (11), sensors/power in RPARAM table 2. */
#include <stdint.h>
#include <stddef.h>

#define HK_EXP_NTC_COUNT 4
#define HK_EXP_GROUP_COUNT 10
#define HK_EXP_PAC_COUNT 4

typedef struct {
    uint16_t schema;
    uint32_t seq;
    uint32_t tick_ms;
    uint16_t supported;
    uint16_t valid;
    uint8_t status[HK_EXP_GROUP_COUNT];
    uint32_t age_ms[HK_EXP_GROUP_COUNT];
    float ntc_c[HK_EXP_NTC_COUNT];
    float board_temp_c;
    float bme_temp_c;
    uint32_t bme_press_pa;
    float bme_hum_rh;
    int32_t accel_mg[3];
    float gyro_dps[3];
    float imu_temp_c;
    float flow_ul_min[2];
    float flow_temp_c[2];
    uint32_t pwr_volt_mv[HK_EXP_PAC_COUNT];
    int32_t pwr_curr_ma[HK_EXP_PAC_COUNT];
    int32_t pwr_mw[HK_EXP_PAC_COUNT];
    uint8_t rail_enabled[3];
    uint8_t imu_mask;
} HkExpSensors;

typedef struct {
    const char *lite_client_path;
    const char *can_if;
    int ping_timeout_ms;
    int getall_timeout_ms;
    int rgosh_timeout_ms;
    int table_id;
} HkCanCfg;

int hk_can_ping(const HkCanCfg *cfg, int node_addr);
int hk_can_getall_sensors(const HkCanCfg *cfg, int node_addr,
                          HkExpSensors *out);
/* Parse lite-client's getall text. Returns -1 for missing/malformed table 2. */
int hk_can_parse_sensors(const char *text, HkExpSensors *out);
int hk_can_run(const HkCanCfg *cfg, int node_addr, const char *command,
               char *out_buf, size_t out_sz);
