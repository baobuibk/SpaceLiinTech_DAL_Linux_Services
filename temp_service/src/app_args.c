/*
 * app_args.c - CLI argument parsing for temp_service
 */

#include "app_args.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

void app_args_defaults(AppArgs *a)
{
    a->adc_dev      = DEFAULT_ADC_DEV;
    a->vref_mv      = DEFAULT_VREF_MV;
    a->ntc_vdiv_mv  = DEFAULT_NTC_VDIV_MV;
    a->ntc_rs       = DEFAULT_NTC_RS;
    a->ntc_r0       = DEFAULT_NTC_R0;
    a->ntc_beta     = DEFAULT_NTC_BETA;
    a->ntc_t0       = DEFAULT_NTC_T0;
    a->ntc_pulldown = DEFAULT_NTC_PULLDOWN;
    a->adc_bits     = 16;
    a->fs_hz        = DEFAULT_FS_HZ;
    a->retry_s      = DEFAULT_RETRY_S;
    a->fs_dir       = DEFAULT_TEMP_FS_DIR;
    a->debug        = 0;
    a->verbose      = 0;
}

void app_args_print_help(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  -d, --adc-dev  PATH    IIO sysfs device or auto (default: %s)\n"
        "  -u, --vdiv     MV      NTC divider Vcc mV (default: 3300)\n"
        "  -r, --rs       OHM     NTC series R ohm   (default: 10000)\n"
        "  -n, --r0       OHM     NTC nominal R ohm  (default: 10000)\n"
        "  -b, --beta     VAL     NTC beta coeff     (default: 3950)\n"
        "  -t, --t0       DEGC    NTC nominal temp   (default: 25)\n"
        "  -p, --pulldown 0|1     NTC topology       (default: 0=pullup)\n"
        "  -B, --bits     12|16   ADC resolution     (default: 16)\n"
        "  -f, --fs-hz    HZ      FS write rate      (default: 1)\n"
        "  -R, --retry    SEC     retry on error     (default: 5)\n"
        "  -o, --fs-dir   DIR     output directory   (default: %s)\n"
        "  -D, --debug            enable debug log\n"
        "  -V, --verbose          print every sample\n"
        "  -h, --help             show this help\n"
        "\n",
        prog, DEFAULT_ADC_DEV, DEFAULT_TEMP_FS_DIR
    );
}

int app_args_parse(AppArgs *a, int argc, char **argv)
{
    static const struct option long_opts[] = {
        { "adc-dev",  required_argument, NULL, 'd' },
        { "vdiv",     required_argument, NULL, 'u' },
        { "rs",       required_argument, NULL, 'r' },
        { "r0",       required_argument, NULL, 'n' },
        { "beta",     required_argument, NULL, 'b' },
        { "t0",       required_argument, NULL, 't' },
        { "pulldown", required_argument, NULL, 'p' },
        { "bits",     required_argument, NULL, 'B' },
        { "fs-hz",    required_argument, NULL, 'f' },
        { "retry",    required_argument, NULL, 'R' },
        { "fs-dir",   required_argument, NULL, 'o' },
        { "debug",    no_argument,       NULL, 'D' },
        { "verbose",  no_argument,       NULL, 'V' },
        { "help",     no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int c;
    while ((c = getopt_long(argc, argv, "d:u:r:n:b:t:p:B:f:R:o:DVh",
                            long_opts, NULL)) != -1) {
        switch (c) {
        case 'd': a->adc_dev      = optarg;                  break;
        case 'u': a->ntc_vdiv_mv  = atof(optarg);            break;
        case 'r': a->ntc_rs       = atof(optarg);            break;
        case 'n': a->ntc_r0       = atof(optarg);            break;
        case 'b': a->ntc_beta     = atof(optarg);            break;
        case 't': a->ntc_t0       = atof(optarg);            break;
        case 'p': a->ntc_pulldown = atoi(optarg);            break;
        case 'B': a->adc_bits     = atoi(optarg);            break;
        case 'f': a->fs_hz        = (uint32_t)atoi(optarg); break;
        case 'R': a->retry_s      = (uint32_t)atoi(optarg); break;
        case 'o': a->fs_dir       = optarg;                  break;
        case 'D': a->debug        = 1;                       break;
        case 'V': a->verbose      = 1;                       break;
        case 'h': app_args_print_help(argv[0]); exit(0);
        default:  app_args_print_help(argv[0]); return -1;
        }
    }
    return 0;
}

void app_args_dump(const AppArgs *a)
{
    fprintf(stderr,
        "[args] adc_dev=%s  bits=%d\n"
        "[args] ntc: vdiv=%.0fmV  rs=%.0f  r0=%.0f  beta=%.0f  t0=%.1f  pulldown=%d\n"
        "[args] fs_hz=%u  retry_s=%u  fs_dir=%s\n"
        "[args] debug=%d  verbose=%d\n",
        a->adc_dev, a->adc_bits,
        a->ntc_vdiv_mv, a->ntc_rs, a->ntc_r0,
        a->ntc_beta, a->ntc_t0, a->ntc_pulldown,
        a->fs_hz, a->retry_s, a->fs_dir,
        a->debug, a->verbose
    );
}
