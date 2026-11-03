/*
 * main.c - hk_service
 *
 * Single-threaded loop, base tick = --dr-poll-ms (default 100ms), driving
 * four independent cadences via millisecond accumulators:
 *
 *   heartbeat (fixed 5000ms)   : xio power-switch gate -> ping EXP
 *                                -> refresh exp_status. Also
 *                                refreshes the PAC1934 snapshot (OBC's
 *                                own power telemetry, unrelated to the
 *                                EXP gate).
 *   collect obc/exp           : independently timed against
 *                                own runtime-adjustable interval file
 *                                under --var-dir (obc.interval,
 *                                exp.interval -- see hk_interval.h).
 *                                Collecting EXP fetches sensor table 2 if online.
 *   data-ready poll (every tick, EXP) : MCU->MPU SPI PSRAM
 *                                handoff handshake, see hk_data_ready.h.
 *
 * The OBC directly drives EXP power via GPIO (xio pin
 * pow_onoff_5v_exp1). That switch state -- not a PAC1934 rail
 * reading -- is the authoritative "does this board have power" signal,
 * so it is what gates the ping: no power => defaults offline, never
 * pinged. --no-power-gate bypasses this for bring-up/testing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>

#include "app_args.h"
#include "hk_can.h"
#include "hk_xio.h"
#include "hk_temp_src.h"
#include "hk_bme_src.h"
#include "hk_csv.h"
#include "hk_fs.h"
#include "hk_interval.h"
#include "hk_data_ready.h"
#include "pac1934.h"

#define HEARTBEAT_MS 5000u

static const char *EXP_CSV_HEADER =
    "obc_timestamp,online,data_valid,sensor_seq,supported_mask,valid_mask,"
    "ntc1_c,ntc2_c,ntc3_c,ntc4_c,board_temp_c,"
    "bme_temp_c,bme_press_pa,bme_hum_rh,"
    "volt_12v_motor_mv,volt_12v_exp_mv,volt_5v_exp_mv,volt_5v_sln_hd_mv,"
    "curr_12v_motor_ma,curr_12v_exp_ma,curr_5v_exp_ma,curr_5v_sln_hd_ma,"
    "power_12v_motor_mw,power_12v_exp_mw,power_5v_exp_mw,power_5v_sln_hd_mw";

static const char *OBC_CSV_HEADER =
    "obc_timestamp,temp_valid,temp_onboard_c,"
    "temp_ntc1_c,temp_ntc2_c,temp_ntc3_c,temp_ntc4_c,"
    "bme_valid,bme_temp_c,bme_pressure_pa,bme_humidity_rh,"
    "pac_valid,pac_u6_valid,pac_u2_valid,"
    "pwr_5v_sama7_v,pwr_5v_fpga_v,pwr_3v3_v,pwr_5v_ai_v,"
    "pwr_pmic_3v3_v,pwr_pmic_ddr_1v35_v,pwr_pmic_core_1v15_v,pwr_pmic_cpu_1v15_v,"
    "cur_5v_sama7_a,cur_5v_fpga_a,cur_3v3_a,cur_5v_ai_a,"
    "cur_pmic_3v3_a,cur_pmic_ddr_1v35_a,cur_pmic_core_1v15_a,cur_pmic_cpu_1v15_a";

static volatile atomic_int g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    atomic_store(&g_running, 0);
}

static void sleep_ms(uint32_t ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* File is "N | date | reason" (e.g. ".boot_count"); only the leading
 * integer N is used. Read once at startup -- a reboot restarts this
 * process anyway, so the value can't change during a run. */
static long read_boot_count(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long v;
    int  ok = (fscanf(f, "%ld", &v) == 1);
    fclose(f);
    return ok ? v : -1;
}

typedef struct {
    const char *name;
    int         addr;
    const char *pow_pin;   /* xio pin gating this board's ping (power switch) */
    const char *sig_pin;   /* xio pin for the data-ready handshake (input)    */
    int         spi_bus;   /* spi_flash -d bus for this board's PSRAM         */
    int         online;
} ExpNode;

typedef struct {
    int   valid;
    int   chip_valid[2];
    float volt_v[8];
    float curr_a[8];
    time_t sampled_at;
} PacSnapshot;

static void heartbeat_tick(const AppArgs *args, const HkCanCfg *can_cfg,
                            const HkXioCfg *xio_cfg,
                            HkPac1934 pac[2], int pac_available[2],
                            PacSnapshot *snap, ExpNode *node, HkFs *fs)
{
    /* OBC's own power telemetry -- independent of the EXP gate. */
    for (int chip = 0; chip < 2; chip++) {
        if (!pac_available[chip]) {
            uint8_t addr = (uint8_t)(chip == 0 ? args->pac_addr : args->pac_low_addr);
            pac_available[chip] = hk_pac1934_init(&pac[chip],
                (int)args->i2c_bus, addr, chip) == 0;
        }
        float *volt = &snap->volt_v[chip * 4];
        float *curr = &snap->curr_a[chip * 4];
        memset(volt, 0, 4 * sizeof(*volt));
        memset(curr, 0, 4 * sizeof(*curr));
        snap->chip_valid[chip] = pac_available[chip] &&
            hk_pac1934_read(&pac[chip], volt, curr) == 0;
        if (!snap->chip_valid[chip]) {
            memset(volt, 0, 4 * sizeof(*volt));
            memset(curr, 0, 4 * sizeof(*curr));
        }
    }
    snap->valid = snap->chip_valid[0] && snap->chip_valid[1];
    snap->sampled_at = time(NULL);

    int power_ok = args->no_power_gate ||
        hk_xio_pin_state(xio_cfg, node->pow_pin) == 1;
    node->online = power_ok && hk_can_ping(can_cfg, node->addr) == 1;
    hk_fs_write_status(fs, node->name, node->online);
    if (!node->online)
        hk_fs_write_exp_invalid_json(fs, node->name, node->addr,
                                      0, time(NULL));
    if (args->verbose)
        fprintf(stderr, "[hb] %s addr=%d pin=%s power_ok=%d online=%d\n",
                node->name, node->addr, node->pow_pin, power_ok, node->online);
}

static void build_exp_row(char *buf, size_t sz, int online, const HkExpSensors *d)
{
    if (!d) {
        int n = snprintf(buf, sz, "%d,0", online);
        for (int i = 0; i < 23 && n > 0 && (size_t)n < sz; i++)
            n += snprintf(buf + n, sz - (size_t)n, ",");
        return;
    }

    snprintf(buf, sz,
        "%d,1,%u,%u,%u,"
        "%.3f,%.3f,%.3f,%.3f,%.3f,"
        "%.3f,%u,%.3f,"
        "%u,%u,%u,%u,"
        "%d,%d,%d,%d,"
        "%d,%d,%d,%d",
        online, d->seq, d->supported, d->valid,
        d->ntc_c[0], d->ntc_c[1], d->ntc_c[2], d->ntc_c[3],
        d->board_temp_c,
        d->bme_temp_c, d->bme_press_pa, d->bme_hum_rh,
        d->pwr_volt_mv[0], d->pwr_volt_mv[1], d->pwr_volt_mv[2], d->pwr_volt_mv[3],
        d->pwr_curr_ma[0], d->pwr_curr_ma[1], d->pwr_curr_ma[2], d->pwr_curr_ma[3],
        d->pwr_mw[0], d->pwr_mw[1], d->pwr_mw[2], d->pwr_mw[3]);
}

static void collect_obc(const AppArgs *args, HkFs *fs, HkCsv *obc_csv,
                         const PacSnapshot *snap, long boot_count)
{
    time_t ts = time(NULL);

    HkObcTemp temp;
    if (hk_temp_src_read(args->temp_dir, &temp) < 0)
        memset(&temp, 0, sizeof(temp));
    HkBme bme;
    if (hk_bme_src_read(args->bme_dir, ts, &bme) < 0)
        memset(&bme, 0, sizeof(bme));

    char obc_row[512];
    snprintf(obc_row, sizeof(obc_row),
        "%d,%.2f,%.2f,%.2f,%.2f,%.2f,"
        "%d,%.2f,%.1f,%.2f,%d,%d,%d,"
        "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
        "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",
        temp.valid, temp.onboard_c,
        temp.ntc_c[0], temp.ntc_c[1], temp.ntc_c[2], temp.ntc_c[3],
        bme.valid, bme.temperature_c, bme.pressure_pa, bme.humidity_rh,
        snap->valid, snap->chip_valid[0], snap->chip_valid[1],
        snap->volt_v[0], snap->volt_v[1], snap->volt_v[2], snap->volt_v[3],
        snap->volt_v[4], snap->volt_v[5], snap->volt_v[6], snap->volt_v[7],
        snap->curr_a[0], snap->curr_a[1], snap->curr_a[2], snap->curr_a[3],
        snap->curr_a[4], snap->curr_a[5], snap->curr_a[6], snap->curr_a[7]);

    hk_csv_append(obc_csv, OBC_CSV_HEADER, ts, obc_row);
    hk_fs_write_status(fs, "obc", 1);
    hk_fs_write_obc_json(fs, ts, &temp, &bme, snap->chip_valid,
                         snap->sampled_at, snap->volt_v, snap->curr_a,
                         boot_count);

    if (args->verbose)
        fprintf(stderr, "[collect] obc temp_valid=%d onboard=%.2f pac_u6=%d pac_u2=%d\n",
                temp.valid, temp.onboard_c, snap->chip_valid[0], snap->chip_valid[1]);
}

static void collect_exp(const AppArgs *args, const HkCanCfg *can_cfg,
                         ExpNode *node, HkCsv *csv, HkFs *fs)
{
    time_t ts = time(NULL);

    HkExpSensors data;
    const HkExpSensors *dp = NULL;

    if (node->online && hk_can_getall_sensors(can_cfg, node->addr, &data) == 0) {
        dp = &data;
        hk_fs_write_exp_json(fs, node->name, node->addr, 1, ts, &data);
    } else if (node->online) {
        fprintf(stderr, "[collect] %s: getall failed despite online ping\n", node->name);
    }
    if (!dp)
        hk_fs_write_exp_invalid_json(fs, node->name, node->addr, node->online, ts);

    char row[512];
    build_exp_row(row, sizeof(row), node->online, dp);
    hk_csv_append(csv, EXP_CSV_HEADER, ts, row);

    if (args->verbose)
        fprintf(stderr, "[collect] %s online=%d data=%s\n",
                node->name, node->online, dp ? "ok" : "none");
}

int main(int argc, char **argv)
{
    AppArgs args;
    app_args_defaults(&args);
    if (app_args_parse(&args, argc, argv) < 0)
        return EXIT_FAILURE;
    if (args.debug)
        app_args_dump(&args);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    HkFs fs;
    if (hk_fs_init(&fs, args.run_dir) < 0) {
        fprintf(stderr, "[main] hk_fs_init failed\n");
        return EXIT_FAILURE;
    }

    HkCsv obc_csv, exp_csv;
    char hk_dir[512];
    snprintf(hk_dir, sizeof(hk_dir), "%s/logs/hk", args.data_dir);
    hk_csv_init(&obc_csv, hk_dir, "HK0", args.history_interval_s);
    hk_csv_init(&exp_csv, hk_dir, "HK1", args.history_interval_s);

    HkCanCfg can_cfg = {
        .lite_client_path  = args.lite_client,
        .can_if            = args.can_if,
        .ping_timeout_ms   = (int)args.ping_timeout_ms,
        .getall_timeout_ms = (int)args.getall_timeout_ms,
        .rgosh_timeout_ms  = (int)args.rgosh_timeout_ms,
        .table_id          = (int)args.table_id,
    };

    ExpNode node = { "exp", (int)args.exp_addr, args.exp_pow_pin,
                     args.exp_sig_pin, (int)args.exp_spi_bus, 0 };

    HkXioCfg xio_cfg = {
        .xio_path     = args.xio_path,
        .input_subcmd = args.xio_input_cmd,
        .timeout_ms   = (int)args.xio_timeout_ms,
    };

    HkDataReadyCfg dr_cfg = {
        .spi_flash_path       = args.spi_flash_path,
        .data_dir             = args.data_dir,
        .debounce_ms          = args.dr_debounce_ms,
        .dump_timeout_base_ms = args.dr_dump_base_ms,
    };
    HkDataReadyCtx dr_ctx;
    hk_data_ready_ctx_init(&dr_ctx);

    HkInterval iv_obc, iv_exp;
    hk_interval_init(&iv_obc,  args.var_dir, "obc",  args.interval_s);
    hk_interval_init(&iv_exp, args.var_dir, "exp", args.interval_s);

    HkPac1934 pac[2];
    int pac_available[2] = {
        hk_pac1934_init(&pac[0], (int)args.i2c_bus, (uint8_t)args.pac_addr, 0) == 0,
        hk_pac1934_init(&pac[1], (int)args.i2c_bus, (uint8_t)args.pac_low_addr, 1) == 0
    };

    long boot_count = read_boot_count(args.boot_count_file);

    fprintf(stderr,
        "[main] hk_service v%s started. data_dir=%s run_dir=%s var_dir=%s "
        "temp_dir=%s can_if=%s exp=%u(%s) table=%u pac_u6=%d pac_u2=%d "
        "data_ready=%s boot_count=%ld\n",
        HK_SERVICE_VERSION, args.data_dir, args.run_dir, args.var_dir,
        args.temp_dir, args.can_if,
        args.exp_addr, args.exp_pow_pin,
        args.table_id, pac_available[0], pac_available[1],
        args.dr_enabled ? "enabled" : "disabled (HW bug pending fix)",
        boot_count);

    PacSnapshot snap = { 0 };
    uint32_t    poll_ms = args.dr_poll_ms;
    uint32_t    since_heartbeat_ms   = HEARTBEAT_MS;   /* fire immediately */
    uint32_t    since_collect_obc_ms = UINT32_MAX / 2; /* fire immediately */
    uint32_t    since_collect_exp_ms = UINT32_MAX / 2;

    while (atomic_load(&g_running)) {
        since_heartbeat_ms += poll_ms;
        if (since_heartbeat_ms >= HEARTBEAT_MS) {
            heartbeat_tick(&args, &can_cfg, &xio_cfg, pac, pac_available, &snap, &node, &fs);
            since_heartbeat_ms = 0;
        }

        since_collect_obc_ms += poll_ms;
        if (since_collect_obc_ms >= hk_interval_get(&iv_obc) * 1000u) {
            collect_obc(&args, &fs, &obc_csv, &snap, boot_count);
            since_collect_obc_ms = 0;
        }

        since_collect_exp_ms += poll_ms;
        if (since_collect_exp_ms >= hk_interval_get(&iv_exp) * 1000u) {
            collect_exp(&args, &can_cfg, &node, &exp_csv, &fs);
            since_collect_exp_ms = 0;
        }

        if (args.dr_enabled)
            hk_data_ready_tick(&dr_cfg, &xio_cfg, &can_cfg, &dr_ctx,
                                node.name, node.addr, node.sig_pin,
                                node.spi_bus, node.online, poll_ms);

        sleep_ms(poll_ms);
    }

    fprintf(stderr, "[main] shutting down\n");
    hk_fs_write_status(&fs, "obc",  0);
    hk_fs_write_status(&fs, "exp", 0);
    hk_csv_close(&obc_csv);
    hk_csv_close(&exp_csv);

    return EXIT_SUCCESS;
}
