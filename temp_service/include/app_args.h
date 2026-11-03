#pragma once
/*
 * app_args.h - CLI argument parsing for temp_service
 */

#include <stdint.h>
#include "temp_adc.h"

#define DEFAULT_FS_HZ     1
#define DEFAULT_RETRY_S   5

typedef struct {
    const char *adc_dev;
    float       vref_mv;        /* legacy, kept for compat              */
    float       ntc_vdiv_mv;    /* NTC divider supply voltage mV        */
    float       ntc_rs;
    float       ntc_r0;
    float       ntc_beta;
    float       ntc_t0;
    int         ntc_pulldown;
    int         adc_bits;
    uint32_t    fs_hz;
    uint32_t    retry_s;
    const char *fs_dir;
    int         debug;
    int         verbose;
} AppArgs;

void app_args_defaults  (AppArgs *a);
int  app_args_parse     (AppArgs *a, int argc, char **argv);
void app_args_print_help(const char *prog);
void app_args_dump      (const AppArgs *a);