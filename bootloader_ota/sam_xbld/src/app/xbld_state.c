/*
 * xbld_state.c  --  Persistent state: last write/flash attempt (JSON)
 */

#include "xbld_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

/* =========================================================================
 * Directory creation
 * =========================================================================*/

static int mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len-1] == '/') tmp[--len] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* =========================================================================
 * Minimal JSON helpers
 * =========================================================================*/

static const char *json_get_str(const char *json, const char *key,
                                 char *out, size_t outsz)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return NULL;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i < outsz-1) out[i++] = *p++;
        out[i] = '\0';
    } else {
        size_t i = 0;
        while (*p && *p != ',' && *p != '\n' && *p != '}' && i < outsz-1)
            out[i++] = *p++;
        while (i > 0 && (out[i-1]==' '||out[i-1]=='\r'||out[i-1]=='\n'))
            i--;
        out[i] = '\0';
    }
    return out;
}

/* =========================================================================
 * Save / Load / Print
 * =========================================================================*/

int xbld_state_save(const XbldStateEntry_t *e)
{
    if (mkdir_p(XBLD_STATE_DIR) < 0) {
        fprintf(stderr, "[state] cannot create dir %s: %s\n",
                XBLD_STATE_DIR, strerror(errno));
        return -1;
    }

    FILE *f = fopen(XBLD_STATE_FILE, "w");
    if (!f) {
        fprintf(stderr, "[state] cannot write %s: %s\n",
                XBLD_STATE_FILE, strerror(errno));
        return -1;
    }

    time_t t = (time_t)e->timestamp_flash;
    struct tm *tm = gmtime(&t);
    char ts_str[32] = "-";
    if (tm) strftime(ts_str, sizeof(ts_str), "%Y-%m-%d %H:%M:%S UTC", tm);

    char tb_str[32] = "-";
    if (e->hdr_valid) {
        time_t tb = (time_t)e->timestamp_build;
        struct tm *tmb = gmtime(&tb);
        if (tmb) strftime(tb_str, sizeof(tb_str), "%Y-%m-%d %H:%M:%S UTC", tmb);
    }

    fprintf(f,
        "{\n"
        "  \"last_flash\": {\n"
        "    \"filename\":        \"%s\",\n"
        "    \"path\":            \"%s\",\n"
        "    \"port\":            \"%s\",\n"
        "    \"slot\":            \"%s\",\n"
        "    \"timestamp_flash\": %u,\n"
        "    \"timestamp_flash_str\": \"%s\",\n"
        "    \"timestamp_build\": %u,\n"
        "    \"timestamp_build_str\": \"%s\",\n"
        "    \"build_number\":    %u,\n"
        "    \"version\":         \"%u.%u.%u\",\n"
        "    \"image_size\":      %u,\n"
        "    \"image_crc32\":     \"0x%08X\",\n"
        "    \"hw_id\":           %u,\n"
        "    \"hdr_valid\":       %s,\n"
        "    \"status\":          \"%s\"\n"
        "  }\n"
        "}\n",
        e->filename,
        e->path,
        e->port,
        e->slot ? "backup" : "main",
        e->timestamp_flash, ts_str,
        e->timestamp_build, tb_str,
        e->build_number,
        e->ver_major, e->ver_minor, e->ver_patch,
        e->image_size,
        e->image_crc32,
        e->hw_id,
        e->hdr_valid ? "true" : "false",
        e->status
    );

    fclose(f);
    return 0;
}

int xbld_state_load(XbldStateEntry_t *e)
{
    memset(e, 0, sizeof(*e));

    FILE *f = fopen(XBLD_STATE_FILE, "r");
    if (!f) return -1;

    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz <= 0 || sz > 16384) { fclose(f); return -1; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);

    char tmp[512];

    json_get_str(buf, "filename", e->filename, sizeof(e->filename));
    json_get_str(buf, "path",     e->path,     sizeof(e->path));
    json_get_str(buf, "port",     e->port,     sizeof(e->port));
    json_get_str(buf, "status",   e->status,   sizeof(e->status));

    if (json_get_str(buf, "slot", tmp, sizeof(tmp)))
        e->slot = (strcmp(tmp, "backup") == 0) ? 1U : 0U;

    if (json_get_str(buf, "timestamp_flash", tmp, sizeof(tmp)))
        e->timestamp_flash = (uint32_t)strtoul(tmp, NULL, 10);
    if (json_get_str(buf, "timestamp_build", tmp, sizeof(tmp)))
        e->timestamp_build = (uint32_t)strtoul(tmp, NULL, 10);
    if (json_get_str(buf, "build_number", tmp, sizeof(tmp)))
        e->build_number = (uint32_t)strtoul(tmp, NULL, 10);
    if (json_get_str(buf, "image_size", tmp, sizeof(tmp)))
        e->image_size = (uint32_t)strtoul(tmp, NULL, 10);
    if (json_get_str(buf, "image_crc32", tmp, sizeof(tmp)))
        e->image_crc32 = (uint32_t)strtoul(tmp, NULL, 16);
    if (json_get_str(buf, "hw_id", tmp, sizeof(tmp)))
        e->hw_id = (uint8_t)atoi(tmp);
    if (json_get_str(buf, "hdr_valid", tmp, sizeof(tmp)))
        e->hdr_valid = (strcmp(tmp, "true") == 0) ? 1U : 0U;

    if (json_get_str(buf, "version", tmp, sizeof(tmp))) {
        unsigned mj = 0, mn = 0, pt = 0;
        sscanf(tmp, "%u.%u.%u", &mj, &mn, &pt);
        e->ver_major = (uint16_t)mj;
        e->ver_minor = (uint16_t)mn;
        e->ver_patch = (uint16_t)pt;
    }

    free(buf);
    return (e->filename[0] ? 0 : -1);
}

int xbld_state_print(void)
{
    XbldStateEntry_t e;
    if (xbld_state_load(&e) < 0) {
        printf("  [info] No previous write/flash recorded.\n");
        printf("         (file: %s)\n", XBLD_STATE_FILE);
        return -1;
    }

    time_t tf = (time_t)e.timestamp_flash;
    struct tm *tm = gmtime(&tf);
    char ts[32] = "-";
    if (tm) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S UTC", tm);

    printf("\n  -- Last xBLD Write/Flash --------------------------\n");
    printf("  File     : %s\n", e.filename);
    printf("  Path     : %s\n", e.path);
    printf("  Port     : %s\n", e.port[0] ? e.port : "-");
    printf("  Slot     : %s\n", e.slot ? "backup" : "main");
    printf("  Attempted: %s\n", ts);

    if (e.hdr_valid) {
        time_t tb = (time_t)e.timestamp_build;
        struct tm *tmb = gmtime(&tb);
        char tb_str[32] = "-";
        if (tmb) strftime(tb_str, sizeof(tb_str), "%Y-%m-%d %H:%M:%S UTC", tmb);

        printf("  Built    : %s\n", tb_str);
        printf("  Version  : v%u.%u.%u  (build #%u)\n",
               e.ver_major, e.ver_minor, e.ver_patch, e.build_number);
        printf("  Size     : %u bytes\n", e.image_size);
        printf("  CRC-32   : 0x%08X\n", e.image_crc32);
        printf("  HW ID    : %u\n", e.hw_id);
    } else {
        printf("  Header   : (unsigned/unavailable at time of attempt)\n");
    }

    printf("  Status   : %s\n", e.status);
    printf("  -----------------------------------------------------\n\n");
    return 0;
}
