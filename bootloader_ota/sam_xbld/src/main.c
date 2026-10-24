/*
 * main.c  --  xbld-client
 *
 * Handles three modes:
 *   MODE_REPL
 *   MODE_*        : one-shot command, exits with code 0/1/3
 *
 * [1.1.2] stdout is now line-buffered even when redirected to a file/pipe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "app_args.h"
#include "xbld_client.h"
#include "xbld_state.h"
#include "fw_catalog.h"
#include "../../lib/linenoise/linenoise.h"

/* =========================================================================
 * Exit codes
 * =========================================================================*/
#define EXIT_OK       0
#define EXIT_ERR      1
#define EXIT_BADARGS  2
#define EXIT_TIMEOUT  3

/* =========================================================================
 * Signal handler
 * =========================================================================*/
static volatile int g_quit = 0;
static void sig_handler(int s) { (void)s; g_quit = 1; }

static FwCatalog_t  g_catalog;
static int          g_catalog_valid = 0;  
static uint32_t     g_hdr_addr      = 0; 

#define FW_DIR FW_CATALOG_DEFAULT_DIR      /* /home/bee/firmware/EXP */

/* =========================================================================
 * Tab completion for linenoise
 * =========================================================================*/
static const char *s_commands[] = {
    "ping", "erase", "erase backup",
    "write ", "verify", "verify backup",
    "jump", "jump backup",
    "flash ", "bootinfo", "status", "reboot",
    "read ", "dump ", "check", "xbldinfo", "help", "exit", NULL
};

static void completion_cb(const char *buf, linenoiseCompletions *lc)
{
    for (int i = 0; s_commands[i]; i++)
        if (strncmp(buf, s_commands[i], strlen(buf)) == 0)
            linenoiseAddCompletion(lc, s_commands[i]);
}

/* =========================================================================
 * REPL help text
 * =========================================================================*/
static const char *REPL_HELP =
    "\n"
    "  ping                             - Bootloader info\n"
    "  reboot                           - Reboot to bootloader then ping\n"
    "  check                            - Scan for signed hex files\n"
    "  bootinfo                         - Display boot information\n"
    "  erase  [backup]                  - Erase main slot (default) or backup\n"
    "  write  <N|path|latest> [backup]  - Write firmware; auto-detected backup\n"
    "  verify [backup]                  - Verify slot CRC\n"
    "  jump   [backup]                  - Jump to slot\n"
    "  flash  <N|path|latest> [backup]  - Full: erase+write+verify[+jump if main]\n"
    "  status                           - Bootloader state\n"
    "  xbldinfo                         - Show last write/flash attempt (host-only)\n"
    "  help                             - This help\n"
    "  exit                             - Quit\n"
    "\n"
    "  Tip: run 'check' first, then 'write 1' or 'flash 2'\n"
    "  Tip: 'flash latest' / 'write latest' to pick the newest file\n"
    "\n";

static void print_repl_help(void)
{
    printf("%s", REPL_HELP);
    printf("  Port, baud rate, and other options are set when starting samv71_xbld.\n"
           "  Exit and restart the client to change them.\n\n");
    app_args_help("samv71_xbld");
}

/* =========================================================================
 * slot_from_path  --  detect slot from filename suffix
 * =========================================================================*/
static uint8_t slot_from_path(const char *path)
{
    return path ? fw_catalog_slot_from_name(path) : 0;
}

/* =========================================================================
 * do_check  --  scan firmware dir, print catalog
 * =========================================================================*/
static void do_check(XbldClient_t *c)
{
    /* If we have a hdr_addr from a previous ping, use it; else default */
    uint32_t hdr = g_hdr_addr ? g_hdr_addr : 0x00420000U;

    printf("  Scanning ...\n");

    if (g_catalog_valid) {
        fw_catalog_free(&g_catalog);
        g_catalog_valid = 0;
    }

    int n = fw_catalog_scan(&g_catalog, FW_DIR, hdr, FW_CATALOG_CONFIG_PATH);
    if (n < 0) {
        printf("  [ERROR] Could not open directory %s\n", FW_DIR);
        return;
    }

    fw_catalog_print(&g_catalog);

    if (n == 0) {
        printf("  No .hex files found in %s\n\n", FW_DIR);
        fw_catalog_free(&g_catalog);
        return;
    }

    /* Check if no files matched the expected prefix */
    int any_matched = 0;
    for (int i = 0; i < n; i++)
        if (g_catalog.entries[i].name_matched) { any_matched = 1; break; }

    if (!any_matched && n > 0) {
        printf("  List all %d file(s) above for 'write'/'flash'? (y/n) ", n);
        fflush(stdout);
        char ans[8];
        if (!fgets(ans, sizeof(ans), stdin) || (ans[0] != 'y' && ans[0] != 'Y')) {
            fw_catalog_free(&g_catalog);
            printf("\n");
            return;
        }
    }

    g_catalog_valid = 1;
    printf("  Use 'write <N>' or 'flash <N>' with the number above.\n\n");

    (void)c;  /* not needed for scan, but consistent signature */
}

/* =========================================================================
 * resolve_fw_arg
 *   token : "1" / "2" / "/path/to/file.hex"
 *   out_path : filled with full path (>= 512 bytes)
 * Returns 0 on success, -1 on failure.
 * =========================================================================*/
static int ensure_catalog(void)
{
    if (g_catalog_valid) return 0;
    uint32_t hdr = g_hdr_addr ? g_hdr_addr : 0x00420000U;
    int n = fw_catalog_scan(&g_catalog, FW_DIR, hdr, FW_CATALOG_CONFIG_PATH);
    if (n <= 0) return -1;
    g_catalog_valid = 1;
    return 0;
}

static int resolve_fw_arg(const char *token, char *out_path)
{
    if (strcmp(token, "latest") == 0) {
        if (ensure_catalog() < 0) {
            printf("  [ERROR] No firmware found in %s\n", FW_DIR);
            return -1;
        }
        const FwEntry_t *best = fw_catalog_best(&g_catalog);
        if (!best) { printf("  [ERROR] No firmware found in %s\n", FW_DIR); return -1; }
        snprintf(out_path, 512, "%s", best->path);
        return 0;
    }

    /* If it looks like a number, try catalog first */
    char *end;
    (void)strtol(token, &end, 10);
    if (end != token && *end == '\0') {
        if (!g_catalog_valid) {
            printf("  [INFO] No catalog loaded -- run 'check' first.\n"
                   "         (or pass a full file path)\n");
            return -1;
        }
        return fw_catalog_resolve(&g_catalog, token, out_path);
    }

    /* Literal path */
    strncpy(out_path, token, 511);
    out_path[511] = '\0';
    return 0;
}

/* =========================================================================
 * parse_slot_keyword
 *   "backup" -> 1,  "main" -> 0,  "1" -> 1,  "0" -> 0,  NULL -> 255 (no override)
 * =========================================================================*/
static uint8_t parse_slot_keyword(const char *s)
{
    if (!s) return 255;
    if (strcmp(s, "backup") == 0) return 1;
    if (strcmp(s, "main")   == 0) return 0;
    /* Numeric fallback */
    char *end;
    long v = strtol(s, &end, 10);
    if (end != s && *end == '\0' && (v == 0 || v == 1)) return (uint8_t)v;
    return 255;  /* not a slot token */
}

/* resolve slot: from catalog entry if number, else from filename */
static uint8_t resolve_slot_arg(const char *token, const char *path,
                                 const char *slot_kw)
{
    uint8_t kw = parse_slot_keyword(slot_kw);
    if (kw != 255) return kw;
    if (strcmp(token, "latest") == 0 && g_catalog_valid) {
        const FwEntry_t *best = fw_catalog_best(&g_catalog);
        if (best) return best->slot;
    }
    if (g_catalog_valid) {
        char *end;
        long idx = strtol(token, &end, 10);
        if (end != token && *end == '\0' && idx >= 1 && idx <= (long)g_catalog.count)
            return g_catalog.entries[idx-1].slot;
    }
    return slot_from_path(path);
}

/* =========================================================================
 * REPL -- dispatch one command line
 * Returns 1 to exit, 0 to continue.
 * =========================================================================*/
static int repl_dispatch(XbldClient_t *c, char *line)
{
    /* Tokenise (modifies line) */
    char *tok[8];
    int   ntok = 0;
    char *p = line;
    while (*p && ntok < 8) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        tok[ntok++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    if (ntok == 0) return 0;

    const char *cmd = tok[0];

    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) return 1;

    if (strcmp(cmd, "help") == 0) { print_repl_help(); return 0; }

    /* ------------------------------------------------------------------ */
    if (strcmp(cmd, "check") == 0) {
        do_check(c);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    if (strcmp(cmd, "ping") == 0) {
        if (xbld_ping_app(c) == 0) {
            printf("  [OK] App responding\n");
        } else if (xbld_ping(c) == 0) {
            g_hdr_addr = c->s0_hdr_addr;   /* bootloader responding */
        } else {
            printf("  [FAIL] No response (bootloader or app)\n");
        }
        return 0;
    }

    /* erase [backup] */
    if (strcmp(cmd, "erase") == 0) {
        uint8_t kw = (ntok >= 2) ? parse_slot_keyword(tok[1]) : 255;
        uint8_t slot = (kw != 255) ? kw : 0;
        if (xbld_ensure_bootloader(c) < 0) {
            printf("  [ERROR] Cannot erase: bootloader did not respond.\n");
            return 0;
        }
        xbld_erase(c, slot);
        return 0;
    }

    /* verify [backup] */
    if (strcmp(cmd, "verify") == 0) {
        uint8_t kw = (ntok >= 2) ? parse_slot_keyword(tok[1]) : 255;
        uint8_t slot = (kw != 255) ? kw : 0;
        xbld_verify(c, slot);
        return 0;
    }

    /* jump [backup] */
    if (strcmp(cmd, "jump") == 0) {
        uint8_t kw = (ntok >= 2) ? parse_slot_keyword(tok[1]) : 255;
        uint8_t slot = (kw != 255) ? kw : 0;
        if (xbld_jump(c, slot) == 0 && slot == 0)
            xbld_wait_for_app(c, 3000);
        return 0;
    }

    if (strcmp(cmd, "reboot") == 0) { xbld_reboot_and_ping(c); return 0; }

    if (strcmp(cmd, "status") == 0) {
        uint8_t  state   = 0;
        uint32_t written = 0;
        static const char *snames[] = {"IDLE","READY","WRITING","DONE","VERIFIED","ERROR"};
        if (xbld_status(c, &state, &written) == 0)
            printf("  state=%s written=%u B  active=%s\n",
                   state < 6 ? snames[state] : "?", written,
                   c->active_slot ? "backup" : "main");
        else printf("  No response\n");
        return 0;
    }

    /* ------------------------------------------------------------------ */
    /* flash <N|path> [slot_override] */
    if (strcmp(cmd, "flash") == 0) {
        if (ntok < 2) {
            printf("  Usage: flash <N>  or  flash <file.hex> [backup]\n");
            if (g_catalog_valid)
                printf("         (run 'check' to see available files)\n");
            return 0;
        }
        char path[512];
        if (resolve_fw_arg(tok[1], path) < 0) return 0;
        uint8_t slot = resolve_slot_arg(tok[1], path,
                           (ntok >= 3 && parse_slot_keyword(tok[2]) != 255) ? tok[2] : NULL);
        printf("  File: %s  (%s)\n", path, slot ? "backup slot" : "main slot");
        xbld_flash(c, path, slot);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    /* write <N|path> [slot_override] */
    if (strcmp(cmd, "write") == 0) {
        if (ntok < 2) {
            printf("  Usage: write <N>  or  write <file.hex> [backup]\n");
            if (g_catalog_valid)
                printf("         (run 'check' to see available files)\n");
            return 0;
        }

        char path[512];
        if (resolve_fw_arg(tok[1], path) < 0) return 0;
        uint8_t slot = resolve_slot_arg(tok[1], path,
                           (ntok >= 3 && parse_slot_keyword(tok[2]) != 255) ? tok[2] : NULL);
        printf("  File: %s  (%s)\n", path, slot ? "backup slot" : "main slot");

        if (xbld_ensure_bootloader(c) < 0) {
            printf("  [ERROR] Cannot write: bootloader did not respond.\n");
            return 0;
        }
        uint8_t state = 0;
        if (xbld_status(c, &state, NULL) < 0) {
            printf("  [ERROR] Bootloader did not respond; run 'reboot' and wait "
                   "for '[OK] Bootloader responding' before erase/write.\n");
            return 0;
        }
        if (state != 1 && state != 2) {
            printf("  [WARN] State %u -- erase %s first? (y/n) ",
                   state, slot ? "backup slot" : "main slot");
            fflush(stdout);
            char ans[8];
            if (fgets(ans, sizeof(ans), stdin) && (ans[0]=='y'||ans[0]=='Y'))
                if (xbld_erase(c, slot) < 0) { printf("  Erase failed\n"); return 0; }
        }
        xbld_select_slot(c, slot);
        xbld_write_hex(c, path);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    if (strcmp(cmd, "read") == 0) {
        if (ntok < 2) { printf("  Usage: read <addr_hex>\n"); return 0; }
        uint32_t addr = (uint32_t)strtoul(tok[1], NULL, 16);
        xbld_read(c, addr);
        return 0;
    }

    if (strcmp(cmd, "dump") == 0) {
        if (ntok < 4) { printf("  Usage: dump <addr> <size> <file>\n"); return 0; }
        uint32_t addr = (uint32_t)strtoul(tok[1], NULL, 0);
        uint32_t size = (uint32_t)strtoul(tok[2], NULL, 0);
        xbld_dump(c, addr, size, tok[3]);
        return 0;
    }

    if (strcmp(cmd, "bootinfo") == 0) {
        xbld_bootinfo(c);
        return 0;
    }

    if (strcmp(cmd, "xbldinfo") == 0) {
        xbld_state_print();
        return 0;
    }

    printf("  Unknown command: '%s'  (type 'help')\n", cmd);
    return 0;
}

/* =========================================================================
 * main
 * =========================================================================*/
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);   /* flush on '\n' even when piped/redirected */

    AppArgs_t args;
    if (app_args_parse(argc, argv, &args) < 0)
        return EXIT_BADARGS;

    /* xbldinfo is a pure host-side file read - no MCU/port needed at all. */
    if (args.mode == MODE_XBLDINFO)
        return xbld_state_print() == 0 ? EXIT_OK : EXIT_ERR;

    if ((args.mode == MODE_FLASH || args.mode == MODE_WRITE) &&
        strcmp(args.hex_file, "latest") == 0) {
        FwCatalog_t cat;
        int n = fw_catalog_scan(&cat, FW_CATALOG_DEFAULT_DIR, 0x00420000U,
                                FW_CATALOG_CONFIG_PATH);
        const FwEntry_t *best = (n > 0) ? fw_catalog_best(&cat) : NULL;
        if (!best) {
            fprintf(stderr, "error: no firmware found in %s\n", FW_CATALOG_DEFAULT_DIR);
            if (n > 0) fw_catalog_free(&cat);
            return EXIT_ERR;
        }
        strncpy(args.hex_file, best->path, sizeof(args.hex_file)-1);
        args.hex_file[sizeof(args.hex_file)-1] = '\0';
        if (!args.slot_explicit) args.slot = best->slot;
        fw_catalog_free(&cat);
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* Open client */
    XbldClient_t client;
    xbld_client_init(&client,
                     args.port,
                     args.baud,
                     args.use_arq,
                     args.use_ecc,
                     args.crc_type,
                     args.verbose);

    if (args.verbose && args.mode == MODE_REPL)
        printf("  Connecting to %s @ %u baud\n", args.port, args.baud);

    if (xbld_client_open(&client) < 0) {
        fprintf(stderr, "error: cannot open %s\n", args.port);
        return EXIT_TIMEOUT;
    }

    /* Small settle delay */
    struct timespec ts = {0, 100000000L};  /* 100 ms */
    nanosleep(&ts, NULL);

    int rc = EXIT_OK;

    /* ------------------------------------------------------------------ *
     * One-shot mode                                                        *
     * ------------------------------------------------------------------ */
    if (args.mode != MODE_REPL) {
        switch (args.mode) {
        case MODE_PING:
            if (xbld_ping_app(&client) == 0 || xbld_ping(&client) == 0) {
                rc = EXIT_OK;
            } else {
                fprintf(stderr, "  [FAIL] No response (bootloader or app)\n");
                rc = EXIT_ERR;
            }
            break;
        case MODE_ERASE:
            if (xbld_ensure_bootloader(&client) < 0) { rc = EXIT_ERR; break; }
            rc = (xbld_erase(&client, args.slot) == 0) ? EXIT_OK : EXIT_ERR;
            break;
        case MODE_VERIFY:
            rc = (xbld_verify(&client, args.slot) == 0) ? EXIT_OK : EXIT_ERR;
            break;
        case MODE_JUMP:
            rc = (xbld_jump(&client, args.slot) == 0) ? EXIT_OK : EXIT_ERR;
            break;
        case MODE_STATUS: {
            uint8_t  state   = 0;
            uint32_t written = 0;
            static const char *snames[] = {"IDLE","READY","WRITING","DONE","VERIFIED","ERROR"};
            if (xbld_status(&client, &state, &written) == 0) {
                printf("state=%s written=%u active_slot=%u\n",
                       state < 6 ? snames[state] : "?", written, client.active_slot);
                rc = EXIT_OK;
            } else rc = EXIT_ERR;
            break;
        }
        case MODE_REBOOT:
            rc = (xbld_reboot_and_ping(&client) == 0) ? EXIT_OK : EXIT_TIMEOUT;
            break;
        case MODE_FLASH:
            rc = (xbld_flash(&client, args.hex_file, args.slot) == 0) ? EXIT_OK : EXIT_ERR;
            break;
        case MODE_WRITE:
            if (xbld_ensure_bootloader(&client) < 0) { rc = EXIT_ERR; break; }
            xbld_select_slot(&client, args.slot);
            rc = (xbld_write_hex(&client, args.hex_file) == 0) ? EXIT_OK : EXIT_ERR;
            break;
        case MODE_DUMP:
            if (xbld_ping(&client) < 0) { rc = EXIT_ERR; break; }
            rc = (xbld_dump(&client, args.dump_addr,
                             args.dump_size, args.dump_file) == 0) ? EXIT_OK : EXIT_ERR;
            break;
        default:
            rc = EXIT_BADARGS;
        }
        goto done;
    }

    /* ------------------------------------------------------------------ *
     * Interactive REPL                                                     *
     * ------------------------------------------------------------------ */
    linenoiseSetCompletionCallback(completion_cb);
    linenoiseHistoryLoad(".xbld_history");
    linenoiseHistorySetMaxLen(200);

    printf("\n");
    printf("  ██╗ ██╗████╗ ██╗   ████╗ \n");
    printf("   ╚██╔╝ █═█║<║██║   ██ ██║\n");
    printf("  ██╔╝██╗████╔╝█████╗████╔╝\n");
    printf("  ╚═╝ ╚═╝╚════╝╚════╝╚═══╝ \n");
    printf("  xBLD Bootloader Shell [ver 1.1.3]  -  type 'help' for usage\n\n");

    print_repl_help();

    char *line;
    while (!g_quit && (line = linenoise("xbld> ")) != NULL) {
        /* Trim leading/trailing whitespace */
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        size_t len = strlen(s);
        while (len > 0 && (s[len-1]==' '||s[len-1]=='\t'||s[len-1]=='\n')) s[--len]='\0';

        if (*s) {
            linenoiseHistoryAdd(s);
            if (repl_dispatch(&client, s)) { linenoiseFree(line); break; }
        }
        linenoiseFree(line);
    }

    linenoiseHistorySave(".xbld_history");

    if (g_catalog_valid)
        fw_catalog_free(&g_catalog);

done:
    xbld_client_close(&client);
    return rc;
}
