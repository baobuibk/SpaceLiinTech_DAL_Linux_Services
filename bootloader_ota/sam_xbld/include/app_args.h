/*
 * app_args.h  -  xbld-client command-line argument parsing

 * Exit codes:
 *   0  success
 *   1  protocol / MCU error
 *   2  bad arguments
 *   3  timeout / no response
 */

#ifndef APP_ARGS_H
#define APP_ARGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MODE_REPL   = 0,
    MODE_FLASH,
    MODE_WRITE,
    MODE_ERASE,
    MODE_VERIFY,
    MODE_JUMP,
    MODE_PING,
    MODE_STATUS,
    MODE_REBOOT,
    MODE_DUMP,
    MODE_XBLDINFO,
} AppMode_t;

typedef struct {
    /* Connection */
    char     port[64];
    uint32_t baud;

    /* Protocol */
    int use_arq;
    int use_ecc;
    int crc_type;   /* 16 or 32 */

    /* Output */
    int verbose;    /* 0 = quiet (errors only), 1 = normal */

    /* Mode */
    AppMode_t mode;

    uint8_t slot;    // 0 = main, 1 = backup
    int     slot_explicit;   /* 1 if --slot/--backup was passed */

    /* Mode-specific args */
    char     hex_file[256];   /* --flash / --write */
    uint32_t dump_addr;       /* --dump */
    uint32_t dump_size;       /* --dump */
    char     dump_file[256];  /* --dump */
} AppArgs_t;

/**
 * Parse argc/argv into args.
 * Prints usage and returns -1 on error.
 * Returns 0 on success.
 */
int app_args_parse(int argc, char **argv, AppArgs_t *args);

/** Print help to stdout. */
void app_args_help(const char *progname);

#ifdef __cplusplus
}
#endif
#endif /* APP_ARGS_H */
