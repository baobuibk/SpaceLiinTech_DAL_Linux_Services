/*
 * main.c - temp_service
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "app_args.h"
#include "temp_adc.h"
#include "temp_fs.h"

static volatile int g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static void print_sample(const TempData *d)
{
    printf("[temp] status=%d  onboard=%.2f°C  "
           "ntc1=%.2f  ntc2=%.2f  ntc3=%.2f  ntc4=%.2f\n",
           d->valid, d->onboard,
           d->ntc[0], d->ntc[1], d->ntc[2], d->ntc[3]);
}

int main(int argc, char **argv)
{
    AppArgs   args;
    TempFs    fs;
    TempAdcCfg cfg;

    /* -- Defaults & parse -------------------------------------------- */
    app_args_defaults(&args);
    if (app_args_parse(&args, argc, argv) < 0)
        return EXIT_FAILURE;

    if (args.debug)
        app_args_dump(&args);

    /* -- Build ADC config from args ---------------------------------- */
    temp_adc_cfg_defaults(&cfg);
    snprintf(cfg.adc_dev, sizeof(cfg.adc_dev), "%s", args.adc_dev);
    cfg.vref_mv      = args.vref_mv;
    cfg.ntc_rs       = args.ntc_rs;
    cfg.ntc_r0       = args.ntc_r0;
    cfg.ntc_beta     = args.ntc_beta;
    cfg.ntc_t0       = args.ntc_t0;
    cfg.ntc_pulldown = args.ntc_pulldown;
    cfg.ntc_vdiv_mv  = args.ntc_vdiv_mv;
    cfg.adc_bits     = args.adc_bits;

    /* -- Init FS ------------------------------------------------------ */
    if (temp_fs_init(&fs, args.fs_dir) < 0) {
        fprintf(stderr, "[main] fs init failed\n");
        return EXIT_FAILURE;
    }

    /* -- Signal handling --------------------------------------------- */
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    fprintf(stderr, "[main] temp_service started, writing to %s @ %uHz\n",
            args.fs_dir, args.fs_hz);

    /* -- Main loop ---------------------------------------------------- */
    unsigned int sleep_us = (args.fs_hz > 0)
                            ? (1000000u / args.fs_hz)
                            : 1000000u;

    while (g_running) {
        TempData data;
        memset(&data, 0, sizeof(data));

        if (temp_adc_read(&data, &cfg) == 0) {
            temp_fs_write(&fs, &data);

            if (args.verbose)
                print_sample(&data);
            else if (args.debug)
                print_sample(&data);

        } else {
            fprintf(stderr, "[main] ADC read error — writing dummy, "
                    "retry in %us\n", args.retry_s);
            temp_fs_dummy(&fs);
            sleep(args.retry_s);
            continue;
        }

        usleep(sleep_us);
    }

    /* -- Cleanup ------------------------------------------------------ */
    fprintf(stderr, "[main] shutting down\n");
    temp_fs_dummy(&fs);   /* mark offline on exit */
    return EXIT_SUCCESS;
}
