/*
 * fw_catalog.h  --  Firmware file catalog
 *
 * Scans a directory for signed hex files, reads xBLD_ImageHeader_t,
 * verifies CRC, and presents a numbered list to the user.
 *
 * Naming convention parsed (from xbld_sign.py):
 *   [PREFIX]_[BOARD]_hw[HW]_fw[BUILD]_[EPOCH].hex
 *   e.g.  SAT_EXP_hw110_fw99_1778173112.hex
 * Backup suffixes include _backup.hex and _slot1_signed.hex.
 *
 * Config file (~/.config/outputconfig.txt or same dir as binary):
 *   PREFIX_PROJECT=SAT
 *   BOARD_NAME=EXP
 *   HW_VERSION=110
 */

#ifndef FW_CATALOG_H
#define FW_CATALOG_H

#include <stdint.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * xBLD image header (must match xbld_image.h on the MCU side)
 * ------------------------------------------------------------------------- */
#define XBLD_IMAGE_MAGIC       0xB007C0DEU
#define XBLD_IMAGE_HDR_SIZE    512U
#define XBLD_IMAGE_HDR_VERSION 1

#ifndef XBLD_S0_HDR_ADDR
#define XBLD_S0_HDR_ADDR  0x00420000U
#endif

#ifndef XBLD_S1_HDR_ADDR
#define XBLD_S1_HDR_ADDR  0x00520000U
#endif

typedef struct __attribute__((packed)) {
    uint32_t magic;           /* 0xB007C0DE                         */
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint16_t hdr_version;     /* = 1                                */
    uint32_t image_size;      /* firmware bytes (no header)         */
    uint32_t load_addr;       /* = hdr_addr + 512                   */
    uint32_t entry_addr;
    uint32_t image_crc32;     /* CRC32 of firmware bytes            */
    uint32_t hdr_crc32;       /* CRC32 of header (this field = 0)   */
    uint32_t timestamp;       /* unix epoch                         */
    uint32_t build_number;
    uint8_t  hw_id;
    uint8_t  flags;
    uint8_t  reserved[6];
    uint8_t  padding[464];
} XbldImageHeader_t;          /* 512 bytes total                    */

/* -------------------------------------------------------------------------
 * OutputConfig  --  parsed from outputconfig.txt
 * ------------------------------------------------------------------------- */
#define FW_CATALOG_DEFAULT_DIR   "/home/bee/firmware/EXP"
#define FW_CATALOG_CONFIG_PATH   "/home/root/.config/outputconfig.txt"

typedef struct {
    char prefix[64];          /* PREFIX_PROJECT  */
    char board[64];           /* BOARD_NAME      */
    char hw_version[32];      /* HW_VERSION      */
} FwOutputConfig_t;

/* -------------------------------------------------------------------------
 * FwEntry  --  one discovered firmware file
 * ------------------------------------------------------------------------- */
typedef enum {
    FW_STATUS_OK       = 0,   /* header valid, CRC matches          */
    FW_STATUS_NO_HDR   = 1,   /* file exists but header bad/missing */
    FW_STATUS_CRC_FAIL = 2,   /* header OK but CRC mismatch         */
    FW_STATUS_NO_READ  = 3,   /* could not open/read file           */
} FwStatus_t;

typedef struct {
    char       path[512];     /* full path                          */
    char       filename[256]; /* basename only                      */

    /* parsed from filename (0 if not matched) */
    char       prefix[64];
    char       board[64];
    char       hw_ver[32];
    uint32_t   fw_build;
    uint32_t   epoch;
    int        name_matched;  /* 1 if filename matched expected fmt */

    uint8_t slot;   // 0 = main, 1 = backup
    /* from header */
    uint16_t   ver_major;
    uint16_t   ver_minor;
    uint16_t   ver_patch;
    uint32_t   image_size;
    uint32_t   image_crc32;
    uint32_t   hdr_crc32;
    uint32_t   build_number;
    uint32_t   load_addr;
    uint8_t    hw_id;

    /* CRC recalculated from file */
    uint32_t   computed_crc32;

    FwStatus_t status;
} FwEntry_t;

/* -------------------------------------------------------------------------
 * FwCatalog  --  list of discovered entries
 * ------------------------------------------------------------------------- */
typedef struct {
    FwEntry_t  *entries;
    size_t      count;
    size_t      cap;
    FwOutputConfig_t cfg;     /* config used for this scan          */
} FwCatalog_t;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/* Load config file. Falls back to built-in defaults if path unreadable. */
void fw_config_load(FwOutputConfig_t *cfg, const char *config_path);

/* Detect slot from a signed HEX filename or path (0=main, 1=backup). */
uint8_t fw_catalog_slot_from_name(const char *path);

/* Scan dir for .hex files, parse headers & compute CRCs.
 * Pass hdr_addr from xbld_ping() (default 0x0040C000 if not yet pinged).
 * Returns number of entries found, or -1 on error. */
int  fw_catalog_scan(FwCatalog_t *cat, const char *dir,
                     uint32_t hdr_addr, const char *config_path);

/* Print catalog to stdout in the check-command format. */
void fw_catalog_print(const FwCatalog_t *cat);

uint8_t fw_catalog_slot(const FwCatalog_t *cat, const char *token);
/* Resolve a user token:
 *   "1", "2", ... -> entry index (1-based)
 *   anything else -> treated as a file path (returned verbatim in out_path)
 * Returns 0 on success, -1 if index out of range.
 * out_path must be >= 512 bytes. */
int  fw_catalog_resolve(const FwCatalog_t *cat,
                        const char *token, char *out_path);

/* Matched+OK first, else any OK, else entries[0]. NULL if empty. */
const FwEntry_t *fw_catalog_best(const FwCatalog_t *cat);

void fw_catalog_free(FwCatalog_t *cat);

#endif /* FW_CATALOG_H */
