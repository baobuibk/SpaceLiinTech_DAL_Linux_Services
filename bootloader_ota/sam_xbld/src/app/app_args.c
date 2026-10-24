/*
 * app_args.c  -  argument parsing
 */

#include "app_args.h"
#include "fw_catalog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

void app_args_help(const char *prog)
{
    printf(
        "Usage: %s [options] [command]\n"
        "\n"
        "Connection:\n"
        "  -p, --port <dev>      Serial port  (default: /dev/ttyS4)\n"
        "  -b, --baud <rate>     Baud rate    (default: 250000)\n"
        "\n"
        "Protocol:\n"
        "  --no-arq              Disable ARQ (fire-and-forget)\n"
        "  --no-ecc              Disable ECC\n"
        "  --crc-type <16|32>    CRC type    (default: 16)\n"
        "\n"
        "Commands (one-shot, exits after completion):\n"
        "  --ping                Probe app text CLI or ping bootloader\n"
        "  --erase               Erase application flash\n"
        "  --verify              Verify image CRC\n"
        "  --jump                Jump to application\n"
        "  --status              Print bootloader state\n"
        "  --reboot              Send text reset, then ping bootloader\n"
        "  --flash  <file.hex|latest>   Full: text reset+ping+erase+write+verify+jump\n"
        "  --write  <file.hex|latest>   Write HEX (no erase, no jump)\n"
        "  --dump   <addr> <size> <file>  Dump flash to .bin/.hex\n"
        "  xbldinfo              Show last write/flash attempt\n"
        "\n"
        "Slot selection (applies to --erase/--verify/--jump/--flash/--write):\n"
        "  --slot <0|1>          Target slot (default: 0, detected from backup filename)\n"
        "  --backup              Shorthand for --slot 1\n"
        "\n"
        "Output:\n"
        "  -q, --quiet           Suppress progress (errors still shown)\n"
        "  -v, --verbose         Show extra debug output (default)\n"
        "  -h, --help            This help\n"
        "\n"
        "If no command is given, enters interactive REPL.\n"
        "\n"
        "Exit codes:  0=ok  1=protocol error  2=bad args  3=timeout\n"
        "\n"
        "Examples:\n"
        "  %s --port /dev/ttyS4 --baud 250000\n"
        "  %s --port /dev/ttyS4 --flash firmware.hex\n"
        "  %s --port /dev/ttyS4 --flash firmware_backup.hex\n"
        "  %s --port /dev/ttyS4 --flash latest\n"
        "  %s --port /dev/ttyS4 --ping --quiet && echo OK\n",
        prog, prog, prog, prog, prog, prog);
}

int app_args_parse(int argc, char **argv, AppArgs_t *args)
{
    memset(args, 0, sizeof(*args));

    /* Defaults */
    strncpy(args->port, "/dev/ttyS4", sizeof(args->port)-1);
    args->baud     = 250000;
    args->use_arq  = 1;
    args->use_ecc  = 1;
    args->crc_type = 16;
    args->verbose  = 1;
    args->mode     = MODE_REPL;
    args->slot     = 0;

    static const struct option long_opts[] = {
        {"port",     required_argument, NULL, 'p'},
        {"baud",     required_argument, NULL, 'b'},
        {"no-arq",   no_argument,       NULL,  1 },
        {"no-ecc",   no_argument,       NULL,  2 },
        {"crc-type", required_argument, NULL,  3 },
        {"ping",     no_argument,       NULL, 10 },
        {"erase",    no_argument,       NULL, 11 },
        {"verify",   no_argument,       NULL, 12 },
        {"jump",     no_argument,       NULL, 13 },
        {"status",   no_argument,       NULL, 14 },
        {"reboot",   no_argument,       NULL, 15 },
        {"flash",    required_argument, NULL, 16 },
        {"write",    required_argument, NULL, 17 },
        {"dump",     no_argument,       NULL, 18 },
        {"slot",     required_argument, NULL, 19 },
        {"backup",   no_argument,       NULL, 20 },
        {"quiet",    no_argument,       NULL, 'q'},
        {"verbose",  no_argument,       NULL, 'v'},
        {"help",     no_argument,       NULL, 'h'},
        {NULL,       0,                 NULL,  0 },
    };

    int c, idx;
    while ((c = getopt_long(argc, argv, "p:b:qvh", long_opts, &idx)) != -1) {
        switch (c) {
        case 'p': strncpy(args->port, optarg, sizeof(args->port)-1); break;
        case 'b': args->baud = (uint32_t)atoi(optarg); break;
        case 1:   args->use_arq = 0; break;
        case 2:   args->use_ecc = 0; break;
        case 3:   args->crc_type = atoi(optarg); break;
        case 10:  args->mode = MODE_PING;   break;
        case 11:  args->mode = MODE_ERASE;  break;
        case 12:  args->mode = MODE_VERIFY; break;
        case 13:  args->mode = MODE_JUMP;   break;
        case 14:  args->mode = MODE_STATUS; break;
        case 15:  args->mode = MODE_REBOOT; break;
        case 16:
            args->mode = MODE_FLASH;
            strncpy(args->hex_file, optarg, sizeof(args->hex_file)-1);
            break;
        case 17:
            args->mode = MODE_WRITE;
            strncpy(args->hex_file, optarg, sizeof(args->hex_file)-1);
            break;
        case 18:
            args->mode = MODE_DUMP;
            /* --dump addr size file -> consume next 3 positional args */
            if (optind + 2 >= argc) {
                fprintf(stderr, "error: --dump requires <addr> <size> <file>\n");
                return -1;
            }
            args->dump_addr = (uint32_t)strtoul(argv[optind],   NULL, 0);
            args->dump_size = (uint32_t)strtoul(argv[optind+1], NULL, 0);
            strncpy(args->dump_file, argv[optind+2], sizeof(args->dump_file)-1);
            optind += 3;
            break;
        case 19:
            args->slot = (uint8_t)atoi(optarg);
            if (args->slot > 1) {
                fprintf(stderr, "error: --slot must be 0 or 1\n");
                return -1;
            }
            args->slot_explicit = 1;
            break;
        case 20:  args->slot = 1; args->slot_explicit = 1; break;
        case 'q': args->verbose = 0; break;
        case 'v': args->verbose = 1; break;
        case 'h': app_args_help(argv[0]); exit(0);
        default:
            fprintf(stderr, "Unknown option. Use --help.\n");
            return -1;
        }
    }

    if (args->baud == 0) {
        fprintf(stderr, "error: invalid baud rate\n"); return -1;
    }
    if (args->crc_type != 16 && args->crc_type != 32) {
        fprintf(stderr, "error: --crc-type must be 16 or 32\n"); return -1;
    }

    /* Bare positional command (e.g. `xbld_client xbldinfo`), mirrors
     * fmod_client's positional-command style for one-shot host-only info. */
    if (optind < argc) {
        const char *cmd = argv[optind];
        if (strcmp(cmd, "xbldinfo") == 0) {
            args->mode = MODE_XBLDINFO;
            optind++;
        } else {
            fprintf(stderr, "error: unknown command '%s'\n", cmd);
            return -1;
        }
    }

    /* Auto-detect slot from filename if not explicitly set via --slot/--backup */
    if (args->slot == 0 &&
        (args->mode == MODE_FLASH || args->mode == MODE_WRITE)) {
        args->slot = fw_catalog_slot_from_name(args->hex_file);
    }

    return 0;
}
