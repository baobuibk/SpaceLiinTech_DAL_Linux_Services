/*
 * xbld_state.h  --  Persistent state for xbld_client
 *
 * Stores information about the most recent write/flash attempt as a
 * single JSON file at XBLD_STATE_FILE, overwritten every attempt.
 * Mirrors fmod_client's fmod_state.h.
 *
 * Format example:
 * {
 *   "last_flash": {
 *     "filename":        "SAT_EXP_hw110_fw99_1778173112.hex",
 *     "path":            "/home/bee/firmware/EXP/SAT_EXP_hw110_fw99_1778173112.hex",
 *     "port":            "/dev/ttyS1",
 *     "slot":            "main",
 *     "timestamp_flash": 1778173200,
 *     "timestamp_build": 1778173112,
 *     "build_number":    99,
 *     "version":         "1.2.3",
 *     "image_size":      524288,
 *     "image_crc32":     "0xDEADBEEF",
 *     "hw_id":           1,
 *     "hdr_valid":       true,
 *     "status":          "ok"
 *   }
 * }
 */

#ifndef XBLD_STATE_H
#define XBLD_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XBLD_STATE_DIR   "/home/root/tmp/run/xbld"
#define XBLD_STATE_FILE  XBLD_STATE_DIR "/last_flash.json"

typedef struct {
    char     filename[256];
    char     path[512];
    char     port[64];
    uint8_t  slot;              /**< 0 = main, 1 = backup                    */
    uint32_t timestamp_flash;   /**< when this attempt happened (now)        */
    uint32_t timestamp_build;   /**< from image header; 0 if hdr_valid==0    */
    uint32_t build_number;
    uint16_t ver_major;
    uint16_t ver_minor;
    uint16_t ver_patch;
    uint32_t image_size;
    uint32_t image_crc32;
    uint8_t  hw_id;
    uint8_t  hdr_valid;         /**< 1 if image header was parsed OK         */
    char     status[16];        /**< "ok" / "ping_fail" / "erase_fail" /
                                      "write_fail" / "verify_fail"           */
} XbldStateEntry_t;

/**
 * Save state to XBLD_STATE_FILE.
 * Creates XBLD_STATE_DIR if it doesn't exist. Overwrites any existing file.
 * Returns 0 ok, -1 error.
 */
int xbld_state_save(const XbldStateEntry_t *e);

/**
 * Load last state from XBLD_STATE_FILE.
 * Returns 0 ok, -1 if file missing or malformed.
 */
int xbld_state_load(XbldStateEntry_t *e);

/** Print last write/flash info to stdout. Returns 0 ok, -1 if no state. */
int xbld_state_print(void);

#ifdef __cplusplus
}
#endif
#endif /* XBLD_STATE_H */
