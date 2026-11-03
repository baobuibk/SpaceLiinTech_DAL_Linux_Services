/*
 * app_args.c - CLI argument parsing for hk_service
 */

#include "app_args.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

enum {
    OPT_EXP_ADDR = 1000,
    OPT_TABLE_ID,
    OPT_I2C_BUS,
    OPT_PAC_ADDR,
    OPT_PAC_LOW_ADDR,
    OPT_BME_DIR,
    OPT_HISTORY_INTERVAL,
    OPT_PING_TO,
    OPT_GETALL_TO,
    OPT_RGOSH_TO,
    OPT_VAR_DIR,
    OPT_XIO_PATH,
    OPT_XIO_TIMEOUT,
    OPT_XIO_INPUT_CMD,
    OPT_EXP_POW_PIN,
    OPT_NO_POWER_GATE,
    OPT_ENABLE_DATA_READY,
    OPT_SPI_FLASH_PATH,
    OPT_EXP_SIG_PIN,
    OPT_EXP_SPI_BUS,
    OPT_DR_POLL_MS,
    OPT_DR_DEBOUNCE_MS,
    OPT_DR_DUMP_BASE_MS,
    OPT_BOOT_COUNT_FILE,
};

void app_args_defaults(AppArgs *a)
{
    a->interval_s        = DEFAULT_INTERVAL_S;
    a->history_interval_s = DEFAULT_HISTORY_INTERVAL_S;
    a->data_dir           = DEFAULT_DATA_DIR;
    a->run_dir            = DEFAULT_RUN_DIR;
    a->var_dir            = DEFAULT_VAR_DIR;
    a->temp_dir           = DEFAULT_TEMP_DIR;
    a->bme_dir            = DEFAULT_BME_DIR;
    a->lite_client        = DEFAULT_LITE_CLIENT;
    a->can_if             = DEFAULT_CAN_IF;
    a->exp_addr           = DEFAULT_EXP_ADDR;
    a->table_id           = DEFAULT_TABLE_ID;
    a->i2c_bus            = DEFAULT_I2C_BUS;
    a->pac_addr           = DEFAULT_PAC_ADDR;
    a->pac_low_addr       = DEFAULT_PAC_LOW_ADDR;
    a->ping_timeout_ms    = DEFAULT_PING_TIMEOUT_MS;
    a->getall_timeout_ms  = DEFAULT_GETALL_TIMEOUT_MS;
    a->rgosh_timeout_ms   = DEFAULT_RGOSH_TIMEOUT_MS;
    a->xio_path           = DEFAULT_XIO_PATH;
    a->xio_timeout_ms     = DEFAULT_XIO_TIMEOUT_MS;
    a->xio_input_cmd      = DEFAULT_XIO_INPUT_CMD;
    a->exp_pow_pin        = DEFAULT_EXP_POW_PIN;
    a->no_power_gate      = 0;
    a->dr_enabled         = 0;   /* off by default: HW bug being investigated */
    a->spi_flash_path     = DEFAULT_SPI_FLASH_PATH;
    a->exp_sig_pin        = DEFAULT_EXP_SIG_PIN;
    a->exp_spi_bus        = DEFAULT_EXP_SPI_BUS;
    a->dr_poll_ms         = DEFAULT_DR_POLL_MS;
    a->dr_debounce_ms     = DEFAULT_DR_DEBOUNCE_MS;
    a->dr_dump_base_ms    = DEFAULT_DR_DUMP_BASE_MS;
    a->boot_count_file    = DEFAULT_BOOT_COUNT_FILE;
    a->debug              = 0;
    a->verbose            = 0;
}

void app_args_print_help(const char *prog)
{
    fprintf(stderr,
        "hk_service v%s\n"
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  -i, --interval    SEC    seed collect interval for obc/exp.interval\n"
        "                           files, %u..%u  (default: %u). Once running, edit\n"
        "                           <var-dir>/{obc,exp}.interval to change live.\n"
        "      --history-interval SEC minimum CSV spacing     (default: %u)\n"
        "  -o, --data-dir    DIR    CSV + .bin dump root dir        (default: %s)\n"
        "  -r, --run-dir     DIR    status/json publish dir         (default: %s)\n"
        "      --var-dir     DIR    runtime interval-file dir       (default: %s)\n"
        "  -t, --temp-dir    DIR    temp_service publish dir        (default: %s)\n"
        "      --bme-dir     DIR    bme_service publish dir         (default: %s)\n"
        "  -l, --lite-client PATH   lite-client binary              (default: %s)\n"
        "  -c, --can-if      NAME   CAN interface                   (default: %s)\n"
        "      --exp-addr    N      EXP CAN node address            (default: %u)\n"
        "      --table-id    N      EXP sensor rparam table         (required: %u)\n"
        "      --i2c-bus     N      PAC1934 i2c bus number          (default: %u)\n"
        "      --pac-addr    HEX    U6 PAC1934 7-bit address        (default: 0x%02x)\n"
        "      --pac-low-addr HEX U2 PAC1934 7-bit address        (default: 0x%02x)\n"
        "      --ping-timeout   MS  ping watchdog timeout           (default: %u)\n"
        "      --getall-timeout MS  getall watchdog timeout         (default: %u)\n"
        "      --rgosh-timeout  MS  rgosh run watchdog timeout      (default: %u)\n"
        "      --xio-path    PATH   xio binary (power/input query)  (default: %s)\n"
        "      --xio-timeout MS     xio watchdog timeout            (default: %u)\n"
        "      --xio-input-cmd ARG  xio argv[1] for input pin table (default: %s)\n"
        "      --exp-pow-pin NAME   xio pin gating EXP ping         (default: %s)\n"
        "      --no-power-gate      always ping EXP, ignore xio power state\n"
        "      --enable-data-ready  enable MCU->MPU data-ready polling (default: off,\n"
        "                           disabled pending a hardware fix)\n"
        "      --spi-flash-path PATH spi_flash binary               (default: %s)\n"
        "      --exp-sig-pin NAME   xio input pin, EXP data-ready   (default: %s)\n"
        "      --exp-spi-bus N      spi_flash -d bus for EXP        (default: %u)\n"
        "      --dr-poll-ms MS      data-ready pin poll period      (default: %u)\n"
        "      --dr-debounce-ms MS  data-ready pin debounce hold    (default: %u)\n"
        "      --dr-dump-base-ms MS spi_flash dump watchdog base    (default: %u)\n"
        "      --boot-count-file PATH \"N | date | reason\" boot log  (default: %s)\n"
        "  -D, --debug              enable debug log\n"
        "  -V, --verbose            print every sample\n"
        "  -h, --help               show this help\n"
        "\n",
        HK_SERVICE_VERSION, prog,
        HK_INTERVAL_MIN_S, HK_INTERVAL_MAX_S, DEFAULT_INTERVAL_S,
        DEFAULT_HISTORY_INTERVAL_S,
        DEFAULT_DATA_DIR, DEFAULT_RUN_DIR, DEFAULT_VAR_DIR, DEFAULT_TEMP_DIR,
        DEFAULT_BME_DIR,
        DEFAULT_LITE_CLIENT, DEFAULT_CAN_IF,
        (unsigned)DEFAULT_EXP_ADDR,
        (unsigned)DEFAULT_TABLE_ID, (unsigned)DEFAULT_I2C_BUS,
        (unsigned)DEFAULT_PAC_ADDR,
        (unsigned)DEFAULT_PAC_LOW_ADDR,
        (unsigned)DEFAULT_PING_TIMEOUT_MS, (unsigned)DEFAULT_GETALL_TIMEOUT_MS,
        (unsigned)DEFAULT_RGOSH_TIMEOUT_MS,
        DEFAULT_XIO_PATH, (unsigned)DEFAULT_XIO_TIMEOUT_MS, DEFAULT_XIO_INPUT_CMD,
        DEFAULT_EXP_POW_PIN,
        DEFAULT_SPI_FLASH_PATH, DEFAULT_EXP_SIG_PIN,
        (unsigned)DEFAULT_EXP_SPI_BUS,
        (unsigned)DEFAULT_DR_POLL_MS, (unsigned)DEFAULT_DR_DEBOUNCE_MS,
        (unsigned)DEFAULT_DR_DUMP_BASE_MS,
        DEFAULT_BOOT_COUNT_FILE
    );
}

int app_args_parse(AppArgs *a, int argc, char **argv)
{
    static const struct option long_opts[] = {
        { "interval",        required_argument, NULL, 'i' },
        { "data-dir",        required_argument, NULL, 'o' },
        { "run-dir",         required_argument, NULL, 'r' },
        { "var-dir",         required_argument, NULL, OPT_VAR_DIR },
        { "temp-dir",        required_argument, NULL, 't' },
        { "bme-dir",         required_argument, NULL, OPT_BME_DIR },
        { "history-interval", required_argument, NULL, OPT_HISTORY_INTERVAL },
        { "lite-client",     required_argument, NULL, 'l' },
        { "can-if",          required_argument, NULL, 'c' },
        { "exp-addr",        required_argument, NULL, OPT_EXP_ADDR },
        { "exp1-addr",       required_argument, NULL, OPT_EXP_ADDR },
        { "table-id",        required_argument, NULL, OPT_TABLE_ID },
        { "i2c-bus",         required_argument, NULL, OPT_I2C_BUS },
        { "pac-addr",        required_argument, NULL, OPT_PAC_ADDR },
        { "pac-low-addr",    required_argument, NULL, OPT_PAC_LOW_ADDR },
        { "ping-timeout",    required_argument, NULL, OPT_PING_TO },
        { "getall-timeout",  required_argument, NULL, OPT_GETALL_TO },
        { "rgosh-timeout",   required_argument, NULL, OPT_RGOSH_TO },
        { "xio-path",        required_argument, NULL, OPT_XIO_PATH },
        { "xio-timeout",     required_argument, NULL, OPT_XIO_TIMEOUT },
        { "xio-input-cmd",   required_argument, NULL, OPT_XIO_INPUT_CMD },
        { "exp-pow-pin",     required_argument, NULL, OPT_EXP_POW_PIN },
        { "exp1-pow-pin",    required_argument, NULL, OPT_EXP_POW_PIN },
        { "no-power-gate",   no_argument,       NULL, OPT_NO_POWER_GATE },
        { "enable-data-ready", no_argument,     NULL, OPT_ENABLE_DATA_READY },
        { "spi-flash-path",  required_argument, NULL, OPT_SPI_FLASH_PATH },
        { "exp-sig-pin",     required_argument, NULL, OPT_EXP_SIG_PIN },
        { "exp1-sig-pin",    required_argument, NULL, OPT_EXP_SIG_PIN },
        { "exp-spi-bus",     required_argument, NULL, OPT_EXP_SPI_BUS },
        { "exp1-spi-bus",    required_argument, NULL, OPT_EXP_SPI_BUS },
        { "dr-poll-ms",      required_argument, NULL, OPT_DR_POLL_MS },
        { "dr-debounce-ms",  required_argument, NULL, OPT_DR_DEBOUNCE_MS },
        { "dr-dump-base-ms", required_argument, NULL, OPT_DR_DUMP_BASE_MS },
        { "boot-count-file", required_argument, NULL, OPT_BOOT_COUNT_FILE },
        { "debug",           no_argument,       NULL, 'D' },
        { "verbose",         no_argument,       NULL, 'V' },
        { "help",            no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int c;
    while ((c = getopt_long(argc, argv, "i:o:r:t:l:c:DVh",
                            long_opts, NULL)) != -1) {
        switch (c) {
        case 'i': a->interval_s       = (uint32_t)strtoul(optarg, NULL, 10); break;
        case 'o': a->data_dir         = optarg; break;
        case 'r': a->run_dir          = optarg; break;
        case 't': a->temp_dir         = optarg; break;
        case OPT_BME_DIR: a->bme_dir  = optarg; break;
        case OPT_HISTORY_INTERVAL:
            a->history_interval_s = (uint32_t)strtoul(optarg, NULL, 10);
            break;
        case 'l': a->lite_client      = optarg; break;
        case 'c': a->can_if           = optarg; break;
        case OPT_VAR_DIR:        a->var_dir           = optarg;                              break;
        case OPT_EXP_ADDR:       a->exp_addr          = (uint32_t)strtoul(optarg, NULL, 0);  break;
        case OPT_TABLE_ID:       a->table_id          = (uint32_t)strtoul(optarg, NULL, 0);  break;
        case OPT_I2C_BUS:        a->i2c_bus           = (uint32_t)strtoul(optarg, NULL, 0);  break;
        case OPT_PAC_ADDR:       a->pac_addr          = (uint32_t)strtoul(optarg, NULL, 0);  break;
        case OPT_PAC_LOW_ADDR:   a->pac_low_addr      = (uint32_t)strtoul(optarg, NULL, 0);  break;
        case OPT_PING_TO:        a->ping_timeout_ms   = (uint32_t)strtoul(optarg, NULL, 10); break;
        case OPT_GETALL_TO:      a->getall_timeout_ms = (uint32_t)strtoul(optarg, NULL, 10); break;
        case OPT_RGOSH_TO:       a->rgosh_timeout_ms  = (uint32_t)strtoul(optarg, NULL, 10); break;
        case OPT_XIO_PATH:       a->xio_path          = optarg;                              break;
        case OPT_XIO_TIMEOUT:    a->xio_timeout_ms    = (uint32_t)strtoul(optarg, NULL, 10); break;
        case OPT_XIO_INPUT_CMD:  a->xio_input_cmd     = optarg;                              break;
        case OPT_EXP_POW_PIN:    a->exp_pow_pin       = optarg;                              break;
        case OPT_NO_POWER_GATE:  a->no_power_gate     = 1;                                   break;
        case OPT_ENABLE_DATA_READY: a->dr_enabled     = 1;                                   break;
        case OPT_SPI_FLASH_PATH: a->spi_flash_path    = optarg;                              break;
        case OPT_EXP_SIG_PIN:    a->exp_sig_pin       = optarg;                              break;
        case OPT_EXP_SPI_BUS:    a->exp_spi_bus       = (uint32_t)strtoul(optarg, NULL, 0);  break;
        case OPT_DR_POLL_MS:     a->dr_poll_ms        = (uint32_t)strtoul(optarg, NULL, 10); break;
        case OPT_DR_DEBOUNCE_MS: a->dr_debounce_ms    = (uint32_t)strtoul(optarg, NULL, 10); break;
        case OPT_DR_DUMP_BASE_MS:a->dr_dump_base_ms   = (uint32_t)strtoul(optarg, NULL, 10); break;
        case OPT_BOOT_COUNT_FILE:a->boot_count_file   = optarg;                              break;
        case 'D': a->debug   = 1; break;
        case 'V': a->verbose = 1; break;
        case 'h': app_args_print_help(argv[0]); exit(0);
        default:  app_args_print_help(argv[0]); return -1;
        }
    }

    if (a->interval_s < HK_INTERVAL_MIN_S) {
        fprintf(stderr, "[args] interval %us too low, clamped to %us\n",
                a->interval_s, HK_INTERVAL_MIN_S);
        a->interval_s = HK_INTERVAL_MIN_S;
    }
    if (a->interval_s > HK_INTERVAL_MAX_S) {
        fprintf(stderr, "[args] interval %us too high, clamped to %us\n",
                a->interval_s, HK_INTERVAL_MAX_S);
        a->interval_s = HK_INTERVAL_MAX_S;
    }
    if (a->history_interval_s < 1 || a->history_interval_s > 3600) {
        fprintf(stderr, "[args] history interval must be 1..3600 seconds\n");
        return -1;
    }
    if (a->table_id != DEFAULT_TABLE_ID) {
        fprintf(stderr, "[args] EXP sensor layout requires table %u\n", DEFAULT_TABLE_ID);
        return -1;
    }

    return 0;
}

void app_args_dump(const AppArgs *a)
{
    fprintf(stderr,
        "[args] interval_s=%u data_dir=%s run_dir=%s var_dir=%s temp_dir=%s\n"
        "[args] lite_client=%s can_if=%s exp_addr=%u table_id=%u\n"
        "[args] i2c_bus=%u pac_addr=0x%02x pac_low_addr=0x%02x\n"
        "[args] ping_timeout_ms=%u getall_timeout_ms=%u rgosh_timeout_ms=%u\n"
        "[args] xio_path=%s xio_timeout_ms=%u xio_input_cmd=%s\n"
        "[args] exp_pow_pin=%s no_power_gate=%d dr_enabled=%d\n"
        "[args] spi_flash_path=%s exp_sig_pin=%s(bus%u)\n"
        "[args] dr_poll_ms=%u dr_debounce_ms=%u dr_dump_base_ms=%u\n"
        "[args] boot_count_file=%s\n"
        "[args] debug=%d verbose=%d\n",
        a->interval_s, a->data_dir, a->run_dir, a->var_dir, a->temp_dir,
        a->lite_client, a->can_if, a->exp_addr, a->table_id,
        a->i2c_bus, a->pac_addr, a->pac_low_addr,
        a->ping_timeout_ms, a->getall_timeout_ms, a->rgosh_timeout_ms,
        a->xio_path, a->xio_timeout_ms, a->xio_input_cmd,
        a->exp_pow_pin, a->no_power_gate, a->dr_enabled,
        a->spi_flash_path, a->exp_sig_pin, a->exp_spi_bus,
        a->dr_poll_ms, a->dr_debounce_ms, a->dr_dump_base_ms,
        a->boot_count_file,
        a->debug, a->verbose
    );
}
