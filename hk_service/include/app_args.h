#pragma once
/*
 * app_args.h - CLI argument parsing for hk_service
 */

#include <stdint.h>
#include "hk_interval.h"   /* HK_INTERVAL_MIN_S / HK_INTERVAL_MAX_S: single source of truth */

#define HK_SERVICE_VERSION      "0.1.0"

#define DEFAULT_INTERVAL_S      5       /* seed value for obc/exp .interval files */
#define DEFAULT_HISTORY_INTERVAL_S 60

#define DEFAULT_DATA_DIR        "/home/bee/data"
#define DEFAULT_RUN_DIR         "/run/dal/hk_service"
#define DEFAULT_VAR_DIR         "/home/root/var/hk_service"
#define DEFAULT_TEMP_DIR        "/run/dal/temp"
#define DEFAULT_BME_DIR         "/run/dal/bme688"
#define DEFAULT_LITE_CLIENT     "/home/root/tools/bin/lite-client"
#define DEFAULT_CAN_IF          "can0"
#define DEFAULT_EXP_ADDR        11
#define DEFAULT_TABLE_ID        2
#define DEFAULT_I2C_BUS         0
#define DEFAULT_PAC_ADDR        0x1f
#define DEFAULT_PAC_LOW_ADDR    0x10
#define DEFAULT_PING_TIMEOUT_MS 1000
#define DEFAULT_GETALL_TIMEOUT_MS 10000
#define DEFAULT_RGOSH_TIMEOUT_MS  13000 /* rgosh server side hardcodes a 10s timeout */

#define DEFAULT_XIO_PATH        "/home/root/tools/bin/xio"
#define DEFAULT_XIO_TIMEOUT_MS  1000
#define DEFAULT_XIO_INPUT_CMD   "list"  /* ASSUMED: see hk_xio.h note */
#define DEFAULT_EXP_POW_PIN     "pow_onoff_5v_exp1"

#define DEFAULT_SPI_FLASH_PATH  "/home/root/tools/bin/spi_flash"
#define DEFAULT_EXP_SIG_PIN     "sig_input_exp1"   /* data-ready disabled pending wiring validation */
#define DEFAULT_EXP_SPI_BUS     0
#define DEFAULT_DR_POLL_MS      100
#define DEFAULT_DR_DEBOUNCE_MS  500
#define DEFAULT_DR_DUMP_BASE_MS 5000

#define DEFAULT_BOOT_COUNT_FILE "/home/root/.boot_count"

typedef struct {
    uint32_t    interval_s;
    uint32_t    history_interval_s;
    const char *data_dir;
    const char *run_dir;
    const char *var_dir;
    const char *temp_dir;
    const char *bme_dir;
    const char *lite_client;
    const char *can_if;
    uint32_t    exp_addr;
    uint32_t    table_id;
    uint32_t    i2c_bus;
    uint32_t    pac_addr;
    uint32_t    pac_low_addr;
    uint32_t    ping_timeout_ms;
    uint32_t    getall_timeout_ms;
    uint32_t    rgosh_timeout_ms;
    const char *xio_path;
    uint32_t    xio_timeout_ms;
    const char *xio_input_cmd;
    const char *exp_pow_pin;
    int         no_power_gate;   /* force ping regardless of xio power-switch state */
    int         dr_enabled;      /* data-ready handshake polling; off by default
                                   * (HW bug being investigated) -- re-enable with
                                   * --enable-data-ready once fixed */
    const char *spi_flash_path;
    const char *exp_sig_pin;
    uint32_t    exp_spi_bus;
    uint32_t    dr_poll_ms;
    uint32_t    dr_debounce_ms;
    uint32_t    dr_dump_base_ms;
    const char *boot_count_file; /* "N | date | reason" -- leading N is read as boot_count */
    int         debug;
    int         verbose;
} AppArgs;

void app_args_defaults  (AppArgs *a);
int  app_args_parse     (AppArgs *a, int argc, char **argv);
void app_args_print_help(const char *prog);
void app_args_dump      (const AppArgs *a);
