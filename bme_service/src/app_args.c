/*
 * app_args.c - CLI argument parsing for bme_service
 */

#include "app_args.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

void app_args_defaults(AppArgs *a)
{
    a->i2c_dev  = DEFAULT_BME_I2C_DEV;
    a->i2c_addr = DEFAULT_BME_I2C_ADDR;
    a->fs_hz    = DEFAULT_FS_HZ;
    a->retry_s  = DEFAULT_RETRY_S;
    a->fs_dir   = DEFAULT_BME_FS_DIR;
    a->debug    = 0;
    a->verbose  = 0;
}

void app_args_print_help(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  -d, --i2c-dev  PATH    I2C bus device      (default: %s)\n"
        "  -a, --addr     HEX     I2C slave address   (default: 0x%02X)\n"
        "  -f, --fs-hz    HZ      FS write rate        (default: 1)\n"
        "  -R, --retry    SEC     retry on error        (default: 5)\n"
        "  -o, --fs-dir   DIR     output directory      (default: %s)\n"
        "  -D, --debug            enable debug log\n"
        "  -V, --verbose          print every sample\n"
        "  -h, --help             show this help\n"
        "\n",
        prog, DEFAULT_BME_I2C_DEV, DEFAULT_BME_I2C_ADDR, DEFAULT_BME_FS_DIR
    );
}

int app_args_parse(AppArgs *a, int argc, char **argv)
{
    static const struct option long_opts[] = {
        { "i2c-dev", required_argument, NULL, 'd' },
        { "addr",    required_argument, NULL, 'a' },
        { "fs-hz",   required_argument, NULL, 'f' },
        { "retry",   required_argument, NULL, 'R' },
        { "fs-dir",  required_argument, NULL, 'o' },
        { "debug",   no_argument,       NULL, 'D' },
        { "verbose", no_argument,       NULL, 'V' },
        { "help",    no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int c;
    while ((c = getopt_long(argc, argv, "d:a:f:R:o:DVh",
                            long_opts, NULL)) != -1) {
        switch (c) {
        case 'd': a->i2c_dev  = optarg;                        break;
        case 'a': a->i2c_addr = (int)strtol(optarg, NULL, 0); break;
        case 'f': a->fs_hz    = (uint32_t)atoi(optarg);       break;
        case 'R': a->retry_s  = (uint32_t)atoi(optarg);       break;
        case 'o': a->fs_dir   = optarg;                        break;
        case 'D': a->debug    = 1;                             break;
        case 'V': a->verbose  = 1;                             break;
        case 'h': app_args_print_help(argv[0]); exit(0);
        default:  app_args_print_help(argv[0]); return -1;
        }
    }
    return 0;
}

void app_args_dump(const AppArgs *a)
{
    fprintf(stderr,
        "[args] i2c_dev=%s  addr=0x%02X\n"
        "[args] fs_hz=%u  retry_s=%u  fs_dir=%s\n"
        "[args] debug=%d  verbose=%d\n",
        a->i2c_dev, a->i2c_addr,
        a->fs_hz, a->retry_s, a->fs_dir,
        a->debug, a->verbose
    );
}
