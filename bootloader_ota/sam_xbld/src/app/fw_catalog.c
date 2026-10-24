/*
 * fw_catalog.c  --  Firmware file catalog implementation
 */

#include "fw_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <stddef.h>
#include <errno.h>
#include <time.h>

/* =========================================================================
 * CRC-32 (ISO-HDLC, poly 0xEDB88320)
 * =========================================================================*/

static uint32_t crc32_table[256];
static int      crc32_table_ready = 0;

static void crc32_build_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_table_ready = 1;
}

static uint32_t crc32_compute(const uint8_t *data, size_t len)
{
    if (!crc32_table_ready) crc32_build_table();
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

/* =========================================================================
 * Config file
 * =========================================================================*/

void fw_config_load(FwOutputConfig_t *cfg, const char *config_path)
{
    /* Defaults */
    snprintf(cfg->prefix,     sizeof(cfg->prefix),     "FW");
    snprintf(cfg->board,      sizeof(cfg->board),      "BOARD");
    snprintf(cfg->hw_version, sizeof(cfg->hw_version), "0");

    FILE *f = fopen(config_path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* strip newline */
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        /* skip comments / blanks */
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        /* trim whitespace on key */
        while (*key == ' ' || *key == '\t') key++;
        char *ke = key + strlen(key) - 1;
        while (ke > key && (*ke == ' ' || *ke == '\t')) *ke-- = '\0';

        if (strcmp(key, "PREFIX_PROJECT") == 0)
            snprintf(cfg->prefix,     sizeof(cfg->prefix),     "%s", val);
        else if (strcmp(key, "BOARD_NAME") == 0)
            snprintf(cfg->board,      sizeof(cfg->board),      "%s", val);
        else if (strcmp(key, "HW_VERSION") == 0)
            snprintf(cfg->hw_version, sizeof(cfg->hw_version), "%s", val);
    }
    fclose(f);
}

/* =========================================================================
 * Filename parsing
 *   Expected: [PREFIX]_[BOARD]_hw[HW]_fw[BUILD]_[EPOCH][_backup].hex
 *   Also detect slot 1 in other signed naming schemes.
 * =========================================================================*/

static size_t backup_suffix_len(const char *stem)
{
    static const char *suffixes[] = {"_slot1_signed", "_backup", "_s1"};
    size_t len = strlen(stem);
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        size_t suffix_len = strlen(suffixes[i]);
        if (len > suffix_len && strcmp(stem + len - suffix_len, suffixes[i]) == 0)
            return suffix_len;
    }
    return 0;
}

uint8_t fw_catalog_slot_from_name(const char *path)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t len = strlen(base);
    if (len <= 4 || strcmp(base + len - 4, ".hex") != 0) return 0;
    char stem[256];
    if (len - 4 >= sizeof(stem)) return 0;
    memcpy(stem, base, len - 4);
    stem[len - 4] = '\0';
    return backup_suffix_len(stem) != 0;
}

static int parse_filename(FwEntry_t *e, const char *filename)
{
    /* Work on a copy so we can mutilate it */
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", filename);
    tmp[sizeof(tmp)-1] = '\0';

    /* Strip .hex extension */
    size_t flen = strlen(tmp);
    if (flen > 4 && strcmp(tmp + flen - 4, ".hex") == 0)
        tmp[flen - 4] = '\0';

    /* Detect the slot even if the rest of the filename is not in the
     * legacy catalog format. This chooses the right header address. */
    size_t suffix_len = backup_suffix_len(tmp);
    e->slot = suffix_len ? 1 : 0;
    if (suffix_len) tmp[strlen(tmp) - suffix_len] = '\0';

    /* Find last '_' -> epoch */
    char *p = strrchr(tmp, '_');
    if (!p) return 0;
    *p = '\0';
    char *epoch_str = p + 1;

    /* Find next-to-last '_' -> fw<BUILD> */
    p = strrchr(tmp, '_');
    if (!p) return 0;
    *p = '\0';
    char *fw_str = p + 1;

    /* Find next '_' -> hw<HW> */
    p = strrchr(tmp, '_');
    if (!p) return 0;
    *p = '\0';
    char *hw_str = p + 1;

    /* Validate tokens */
    if (strncmp(fw_str, "fw", 2) != 0) return 0;
    if (strncmp(hw_str, "hw", 2) != 0) return 0;

    /* What remains is PREFIX_BOARD; split at first '_' for prefix */
    char *underscore = strchr(tmp, '_');
    if (!underscore) return 0;
    *underscore = '\0';

    snprintf(e->prefix, sizeof(e->prefix), "%s", tmp);
    snprintf(e->board,  sizeof(e->board),  "%s", underscore + 1);
    snprintf(e->hw_ver, sizeof(e->hw_ver), "%s", hw_str + 2);
    e->fw_build = (uint32_t)strtoul(fw_str  + 2, NULL, 10);
    e->epoch    = (uint32_t)strtoul(epoch_str,    NULL, 10);

    return 1;
}

/* =========================================================================
 * Simple inline hex parser: reads bytes for addresses [want_start, want_end)
 * into buf[0..].  Returns number of bytes stored, -1 on I/O error.
 * =========================================================================*/
static int read_hex_range(const char *path,
                           uint32_t want_start, uint32_t want_end,
                           uint8_t *buf, uint32_t buf_size)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    memset(buf, 0xFF, buf_size);

    char     line[600];
    uint32_t base   = 0;
    int      stored = 0;

    while (fgets(line, sizeof(line), f)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        if (line[0] != ':') continue;
        if (strlen(line) < 11) continue;

#define HB(s) (  \
    ((*(s)>='0'&&*(s)<='9')?(*(s)-'0'):  \
     (*(s)>='A'&&*(s)<='F')?(*(s)-'A'+10):  \
     (*(s)>='a'&&*(s)<='f')?(*(s)-'a'+10):-1) >= 0 &&  \
    ((*((s)+1)>='0'&&*((s)+1)<='9')?(*((s)+1)-'0'):  \
     (*((s)+1)>='A'&&*((s)+1)<='F')?(*((s)+1)-'A'+10):  \
     (*((s)+1)>='a'&&*((s)+1)<='f')?(*((s)+1)-'a'+10):-1) >= 0  \
    ? ( (((*(s)>='0'&&*(s)<='9')?(*(s)-'0'):  \
          (*(s)>='A'&&*(s)<='F')?(*(s)-'A'+10):(*(s)-'a'+10)) << 4) |  \
         ((*((s)+1)>='0'&&*((s)+1)<='9')?(*((s)+1)-'0'):  \
          (*((s)+1)>='A'&&*((s)+1)<='F')?(*((s)+1)-'A'+10):(*((s)+1)-'a'+10)) )  \
    : -1 )

        int bc  = HB(line+1); if (bc  < 0) continue;
        int ahi = HB(line+3); if (ahi < 0) continue;
        int alo = HB(line+5); if (alo < 0) continue;
        int rty = HB(line+7); if (rty < 0) continue;

        uint16_t offset = (uint16_t)((ahi << 8) | alo);

        if (rty == 0x01) break;  /* EOF record */

        if (rty == 0x04) {
            int hi = HB(line+9), lo = HB(line+11);
            if (hi >= 0 && lo >= 0)
                base = (uint32_t)((hi << 8) | lo) << 16;
            continue;
        }
        if (rty == 0x02) {
            int hi = HB(line+9), lo = HB(line+11);
            if (hi >= 0 && lo >= 0)
                base = (uint32_t)((hi << 8) | lo) << 4;
            continue;
        }
        if (rty != 0x00) continue;

        for (int i = 0; i < bc; i++) {
            int b = HB(line + 9 + i*2);
            if (b < 0) continue;
            uint32_t a = base + offset + (uint32_t)i;
            if (a >= want_start && a < want_end) {
                uint32_t idx = a - want_start;
                if (idx < buf_size) {
                    buf[idx] = (uint8_t)b;
                    stored++;
                }
            }
        }
#undef HB
    }

    fclose(f);
    return stored;
}

/* =========================================================================
 * Read xBLD header + compute image CRC from a signed hex file.
 *
 * The signed hex has the 512-byte header at hdr_addr, followed by the
 * firmware image.  We re-read the raw hex bytes in address order.
 * =========================================================================*/

static int scan_entry(FwEntry_t *e, uint32_t hdr_addr)
{
    e->status = FW_STATUS_NO_READ;

    /* ---- Read the 512-byte header ---- */
    uint8_t hdr_buf[XBLD_IMAGE_HDR_SIZE];
    int n = read_hex_range(e->path, hdr_addr,
                           hdr_addr + XBLD_IMAGE_HDR_SIZE,
                           hdr_buf, XBLD_IMAGE_HDR_SIZE);
    if (n < (int)XBLD_IMAGE_HDR_SIZE) {
        e->status = FW_STATUS_NO_HDR;
        return -1;
    }

    /* ---- Overlay struct ---- */
    XbldImageHeader_t hdr;
    memcpy(&hdr, hdr_buf, sizeof(hdr));

    if (hdr.magic != XBLD_IMAGE_MAGIC ||
        hdr.hdr_version != XBLD_IMAGE_HDR_VERSION) {
        e->status = FW_STATUS_NO_HDR;
        return -1;
    }

    /* ---- Verify header CRC ---- */
    uint32_t saved_hdr_crc = hdr.hdr_crc32;
    memset(hdr_buf + offsetof(XbldImageHeader_t, hdr_crc32), 0, 4);
    uint32_t calc_hdr_crc = crc32_compute(hdr_buf, XBLD_IMAGE_HDR_SIZE);
    hdr_buf[offsetof(XbldImageHeader_t, hdr_crc32) + 0] = (uint8_t)(saved_hdr_crc);
    hdr_buf[offsetof(XbldImageHeader_t, hdr_crc32) + 1] = (uint8_t)(saved_hdr_crc >> 8);
    hdr_buf[offsetof(XbldImageHeader_t, hdr_crc32) + 2] = (uint8_t)(saved_hdr_crc >> 16);
    hdr_buf[offsetof(XbldImageHeader_t, hdr_crc32) + 3] = (uint8_t)(saved_hdr_crc >> 24);

    if (calc_hdr_crc != saved_hdr_crc) {
        e->status = FW_STATUS_NO_HDR;
        return -1;
    }

    /* ---- Copy fields ---- */
    e->ver_major   = hdr.version_major;
    e->ver_minor   = hdr.version_minor;
    e->ver_patch   = hdr.version_patch;
    e->image_size  = hdr.image_size;
    e->image_crc32 = hdr.image_crc32;
    e->hdr_crc32   = saved_hdr_crc;
    e->build_number= hdr.build_number;
    e->load_addr   = hdr.load_addr;
    e->hw_id       = hdr.hw_id;

    /* ---- Read firmware region and compute CRC ---- */
    if (hdr.image_size == 0 || hdr.image_size > 4*1024*1024U) {
        e->status = FW_STATUS_NO_HDR;
        return -1;
    }

    uint8_t *fw_buf = malloc(hdr.image_size);
    if (!fw_buf) {
        e->status = FW_STATUS_NO_READ;
        return -1;
    }

    uint32_t load = hdr.load_addr;
    n = read_hex_range(e->path, load, load + hdr.image_size,
                       fw_buf, hdr.image_size);
    if (n <= 0) {
        free(fw_buf);
        e->status = FW_STATUS_NO_HDR;
        return -1;
    }

    e->computed_crc32 = crc32_compute(fw_buf, hdr.image_size);
    free(fw_buf);

    e->status = (e->computed_crc32 == hdr.image_crc32)
                ? FW_STATUS_OK : FW_STATUS_CRC_FAIL;
    return 0;
}

/* =========================================================================
 * Catalog scan
 * =========================================================================*/

static int cat_add(FwCatalog_t *cat, const FwEntry_t *e)
{
    if (cat->count >= cat->cap) {
        size_t nc = cat->cap ? cat->cap * 2 : 8;
        FwEntry_t *nb = realloc(cat->entries, nc * sizeof(FwEntry_t));
        if (!nb) return -1;
        cat->entries = nb;
        cat->cap     = nc;
    }
    cat->entries[cat->count++] = *e;
    return 0;
}

/* Sort: matched-format first, then by epoch descending */
static int entry_cmp(const void *a, const void *b)
{
    const FwEntry_t *ea = (const FwEntry_t *)a;
    const FwEntry_t *eb = (const FwEntry_t *)b;
    if (ea->name_matched != eb->name_matched)
        return eb->name_matched - ea->name_matched;
    /* Both matched: sort newest first */
    if (ea->epoch != eb->epoch)
        return (ea->epoch > eb->epoch) ? -1 : 1;
    return strcmp(ea->filename, eb->filename);
}

int fw_catalog_scan(FwCatalog_t *cat, const char *dir,
                    uint32_t hdr_addr, const char *config_path)
{
    memset(cat, 0, sizeof(*cat));
    fw_config_load(&cat->cfg, config_path ? config_path : FW_CATALOG_CONFIG_PATH);

    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "[catalog] cannot open dir '%s': %s\n",
                dir, strerror(errno));
        return -1;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        size_t nlen = strlen(name);
        if (nlen < 5) continue;
        if (strcmp(name + nlen - 4, ".hex") != 0) continue;

        FwEntry_t e;
        memset(&e, 0, sizeof(e));
        snprintf(e.filename, sizeof(e.filename), "%s", name);
        snprintf(e.path, sizeof(e.path), "%s/%s", dir, name);

        e.name_matched = parse_filename(&e, name);

        /* Build match: compare prefix+board+hw with config */
        if (e.name_matched) {
            if (strcmp(e.prefix, cat->cfg.prefix)     != 0 ||
                strcmp(e.board,  cat->cfg.board)       != 0 ||
                strcmp(e.hw_ver, cat->cfg.hw_version)  != 0)
                e.name_matched = 0;  /* file exists but different project */
        }

        uint32_t entry_hdr = (e.slot == 1) ? XBLD_S1_HDR_ADDR : hdr_addr;
        scan_entry(&e, entry_hdr);
        cat_add(cat, &e);
    }
    closedir(d);

    if (cat->count > 1)
        qsort(cat->entries, cat->count, sizeof(FwEntry_t), entry_cmp);

    return (int)cat->count;
}

/* =========================================================================
 * Print catalog
 * =========================================================================*/

/* Format epoch as "YYYY-MM-DD HH:MM" UTC */
static void fmt_epoch(uint32_t epoch, char *buf, size_t bufsz)
{
    time_t t = (time_t)epoch;
    struct tm *tm = gmtime(&t);
    if (tm)
        strftime(buf, bufsz, "%Y-%m-%d %H:%M", tm);
    else
        snprintf(buf, bufsz, "%u", epoch);
}

static const char *status_str(FwStatus_t s)
{
    switch (s) {
    case FW_STATUS_OK:       return "OK";
    case FW_STATUS_CRC_FAIL: return "CRC FAIL";
    case FW_STATUS_NO_HDR:   return "NO HEADER";
    case FW_STATUS_NO_READ:  return "UNREADABLE";
    default:                 return "?";
    }
}

void fw_catalog_print(const FwCatalog_t *cat)
{
    printf("\n");
    printf("  Config: %s_%s_hw%s\n",
           cat->cfg.prefix, cat->cfg.board, cat->cfg.hw_version);
    printf("\n");

    if (cat->count == 0) {
        printf("  (no .hex files found)\n\n");
        return;
    }

    /* Check if any matched the expected naming format */
    int any_matched = 0;
    for (size_t i = 0; i < cat->count; i++)
        if (cat->entries[i].name_matched) { any_matched = 1; break; }

    if (!any_matched) {
        printf("  Could not find any file matching prefix"
               " %s_%s_hw%s...\n",
               cat->cfg.prefix, cat->cfg.board, cat->cfg.hw_version);
    }

    for (size_t i = 0; i < cat->count; i++) {
        const FwEntry_t *e = &cat->entries[i];

        /* Line 1: index + filename + slot tag */
        printf("  [%zu] %s%s\n", i + 1, e->filename,
               e->slot == 1 ? "  [BACKUP slot 1]" : "");

        /* Line 2: metadata (only if header was read) */
        if (e->status == FW_STATUS_OK || e->status == FW_STATUS_CRC_FAIL) {
            char date_buf[20] = "-";
            if (e->epoch) fmt_epoch(e->epoch, date_buf, sizeof(date_buf));
            printf("       v%u.%u.%u | build #%u | %s | %u B | hw_id=%u\n",
                   e->ver_major, e->ver_minor, e->ver_patch,
                   e->build_number, date_buf,
                   e->image_size, e->hw_id);
            /* Line 3: CRC check */
            printf("       CRC: %08X  calc: %08X  [%s]\n",
                   e->image_crc32, e->computed_crc32,
                   status_str(e->status));
        } else {
            printf("       [%s]\n", status_str(e->status));
        }

        if (!e->name_matched)
            printf("       (not from this project)\n");

        printf("\n");
    }
}

const FwEntry_t *fw_catalog_best(const FwCatalog_t *cat)
{
    if (cat->count == 0) return NULL;
    for (size_t i = 0; i < cat->count; i++)
        if (cat->entries[i].name_matched && cat->entries[i].status == FW_STATUS_OK)
            return &cat->entries[i];
    for (size_t i = 0; i < cat->count; i++)
        if (cat->entries[i].status == FW_STATUS_OK)
            return &cat->entries[i];
    return &cat->entries[0];
}

/* =========================================================================
 * Resolve user token -> file path
 * =========================================================================*/

int fw_catalog_resolve(const FwCatalog_t *cat,
                       const char *token, char *out_path)
{
    /* Check if token is a pure integer */
    char *end;
    long idx = strtol(token, &end, 10);
    if (end != token && *end == '\0') {
        /* It's a number */
        if (idx < 1 || idx > (long)cat->count) {
            fprintf(stderr, "  [ERROR] No entry #%ld (catalog has %zu files)\n",
                    idx, cat->count);
            return -1;
        }
        snprintf(out_path, 512, "%s", cat->entries[idx - 1].path);
        return 0;
    }

    /* Treat as a literal path */
    snprintf(out_path, 512, "%s", token);
    return 0;
}

/* =========================================================================
 * Resolve slot from catalog entry index (1-based)
 * Returns 0 or 1; defaults to 0 if index out of range.
 * =========================================================================*/
uint8_t fw_catalog_slot(const FwCatalog_t *cat, const char *token)
{
    char *end;
    long idx = strtol(token, &end, 10);
    if (end != token && *end == '\0' && idx >= 1 && idx <= (long)cat->count)
        return cat->entries[idx - 1].slot;
    /* Literal path - detect from filename */
    return fw_catalog_slot_from_name(token);
}

/* =========================================================================
 * Free
 * =========================================================================*/

void fw_catalog_free(FwCatalog_t *cat)
{
    free(cat->entries);
    memset(cat, 0, sizeof(*cat));
}
