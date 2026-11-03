/*
 * main.c - bme_service
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "app_args.h"
#include "bme_i2c.h"
#include "bme688.h"
#include "bme_fs.h"

static volatile int g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static void print_sample(const BmeData *d)
{
    printf("[bme688] status=%d  temp=%.2fC  press=%.2fPa  hum=%.2f%%  gas=%d %.2fohm\n",
           d->valid, d->temperature, d->pressure, d->humidity,
           d->gas_valid, d->gas_resistance_ohm);
}

int main(int argc, char **argv)
{
    AppArgs args;
    BmeFs   fs;

    app_args_defaults(&args);
    if (app_args_parse(&args, argc, argv) < 0)
        return EXIT_FAILURE;

    if (args.debug)
        app_args_dump(&args);

    if (bme_fs_init(&fs, args.fs_dir) < 0) {
        fprintf(stderr, "[main] fs init failed\n");
        return EXIT_FAILURE;
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    fprintf(stderr, "[main] bme_service started, %s addr=0x%02X, writing to %s @ %uHz\n",
            args.i2c_dev, args.i2c_addr, args.fs_dir, args.fs_hz);

    unsigned int sleep_us = (args.fs_hz > 0) ? (1000000u / args.fs_hz) : 1000000u;

    while (g_running) {
        BmeI2c    bus;
        Bme688Dev dev;

        if (bme_i2c_open(&bus, args.i2c_dev, (uint8_t)args.i2c_addr) < 0) {
            fprintf(stderr, "[main] I2C open failed, retry in %us\n", args.retry_s);
            bme_fs_dummy(&fs);
            sleep(args.retry_s);
            continue;
        }

        memset(&dev, 0, sizeof(dev));
        dev.bus  = &bus;

        if (bme688_init(&dev) != 0) {
            fprintf(stderr, "[main] BME688 init failed (bad chip id or bus error), "
                    "retry in %us\n", args.retry_s);
            bme_fs_dummy(&fs);
            bme_i2c_close(&bus);
            sleep(args.retry_s);
            continue;
        }
        fprintf(stderr, "[main] BME688 online.\n");

        int consecutive_errors = 0;
        while (g_running) {
            Bme688Data    out;
            BmeData       data;

            if (bme688_read_forced(&dev, &out) != 0) {
                consecutive_errors++;
                fprintf(stderr, "[main] read error #%d\n", consecutive_errors);
                if (consecutive_errors >= 10) {
                    fprintf(stderr, "[main] too many errors, reopening bus\n");
                    break;
                }
                usleep(sleep_us);
                continue;
            }
            consecutive_errors = 0;

            data.valid       = 1;
            data.temperature = out.temperature;
            data.pressure    = out.pressure;
            data.humidity    = out.humidity;
            data.gas_valid   = out.gas_valid;
            data.gas_resistance_ohm = out.gas_resistance_ohm;
            data.timestamp   = (long)time(NULL);

            bme_fs_write(&fs, &data);

            if (args.verbose || args.debug)
                print_sample(&data);

            usleep(sleep_us);
        }

        bme_fs_dummy(&fs);
        bme_i2c_close(&bus);

        if (g_running) {
            fprintf(stderr, "[main] sensor loop ended, retry in %us\n", args.retry_s);
            sleep(args.retry_s);
        }
    }

    fprintf(stderr, "[main] shutting down\n");
    bme_fs_dummy(&fs);
    return EXIT_SUCCESS;
}
