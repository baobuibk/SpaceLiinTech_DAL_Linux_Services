#pragma once
/*
 * app_args.h - CLI argument parsing for bme_service
 */

#include <stdint.h>
#include "bme_i2c.h"
#include "bme_fs.h"

#define DEFAULT_FS_HZ    1
#define DEFAULT_RETRY_S  5

typedef struct {
    const char *i2c_dev;
    int         i2c_addr;
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
