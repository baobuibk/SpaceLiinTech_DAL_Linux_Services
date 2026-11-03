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
    uint32_t sample_seq[HK_EXP_GROUP_COUNT];
    uint8_t last_error[HK_EXP_GROUP_COUNT];
    uint8_t desired[HK_EXP_GROUP_COUNT];
    uint8_t applied[HK_EXP_GROUP_COUNT];
    uint8_t imu_mask;
    uint32_t bme_gas_ohm;
    uint8_t bme_gas_valid;
} HkExpSensors;

/* MCU RPARAM table 3, schema 6: four heater profile runtime snapshots. */
typedef struct {
    uint16_t schema;
    uint32_t seq, tick_ms;
    uint8_t state[4], enabled[4], fault[4], configured[4], sensors_valid[4];
    uint8_t main_ntc[4], sec_ntc[4];
    int32_t main_mC[4], sec_mC[4], setpoint_mC[4];
    uint8_t request_pct[4], applied_pct[4], limit_pct[4];
    uint32_t main_age_ms[4], sec_age_ms[4];
    uint8_t owner_kind[4], owner_profile[4], owner_valid[4];
    float pid_demand[4];
    uint32_t output_ms[4], config_gen[4];
} HkExpThermal;

/* Complete RPARAM table 6 inventory, including the oldest pending object. */
typedef struct {
    uint16_t schema;
    uint32_t seq, tick_ms, boot_id;
    uint8_t ready, result;
    uint32_t pending, published, acknowledged, discarded, dropped;
    uint8_t type_count[3], capacity;
    uint16_t max_read;
    uint32_t object_id, length, crc32;
    uint8_t type, source, encoding;
    uint16_t width, height;
    uint32_t run_id, object_seq, created_ms;
    uint8_t image_count[2];
} HkExpInventory;

/* MCU RPARAM table 12, schema 6, compact ICD status layout 1. */
typedef struct {
    uint16_t schema;
    uint32_t seq, tick_ms, boot_id, group_tick_ms[4];
    uint8_t layout_ver, valid_mask;
    uint8_t thm_stat[6], fld_stat[16], rba_stat[16], pc_stat;
    char build_id[21];
} HkExpIcdStatus;

/* Static identity fields from MCU RPARAM table 0 (schema 6). */
typedef struct {
    uint16_t schema;
    uint32_t boot_id;
    char board[17], firmware[17], hardware[9];
} HkExpIdentity;

typedef struct {
    const char *lite_client_path;
    char transport_option[128]; /* -c=can0 or -k/dev/ttyS2,speed=115200 */
    char address_option[16];   /* empty for legacy CAN, -a2 for RS485 */
    int ping_timeout_ms;
    int getall_timeout_ms;
    int rgosh_timeout_ms;
    int table_id;
} HkCanCfg;

int hk_can_ping(const HkCanCfg *cfg, int node_addr);
int hk_can_getall_sensors(const HkCanCfg *cfg, int node_addr,
                          HkExpSensors *out);
int hk_can_getall_thermal(const HkCanCfg *cfg, int node_addr,
                          HkExpThermal *out);
int hk_can_parse_thermal(const char *text, HkExpThermal *out);
int hk_can_getall_inventory(const HkCanCfg *cfg, int node_addr,
                            HkExpInventory *out);
int hk_can_parse_inventory(const char *text, HkExpInventory *out);
int hk_can_getall_icd_status(const HkCanCfg *cfg, int node_addr,
                             HkExpIcdStatus *out);
int hk_can_parse_icd_status(const char *text, HkExpIcdStatus *out);
int hk_can_getall_identity(const HkCanCfg *cfg, int node_addr,
                           HkExpIdentity *out);
int hk_can_parse_identity(const char *text, HkExpIdentity *out);
/* Parse lite-client's getall text. Accepts complete schema 5 or 6 table 2. */
int hk_can_parse_sensors(const char *text, HkExpSensors *out);
int hk_can_run(const HkCanCfg *cfg, int node_addr, const char *command,
               char *out_buf, size_t out_sz);
/* One-shot RPARAM command; only the caller selects writable tables. */
int hk_can_rparam(const HkCanCfg *cfg, int node_addr, int table_id,
                  const char *verb, const char *field, const char *value,
                  char *out_buf, size_t out_sz);
