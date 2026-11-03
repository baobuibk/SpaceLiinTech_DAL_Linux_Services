#pragma once
/*
 * hk_temp_src.h - Read-only consumer of temp_service's published files
 *
 * hk_service does not touch the ADC itself: temp_service already owns
 * that hardware and publishes plain-text files under DEFAULT_TEMP_DIR.
 * hk_service just reads them so the OBC row/json share one source of
 * truth with the files under /run/dal/temp/.
 */

#define HK_TEMP_NTC_COUNT 4

typedef struct {
    int   valid;                    /* temp_status: 1 ok, 0 error/offline */
    float onboard_c;
    float ntc_c[HK_TEMP_NTC_COUNT];
    long  src_timestamp;            /* temp_service's own sample time     */
} HkObcTemp;

/*
 * Returns 0 for a valid sample no older than 10 seconds. Returns -1 and
 * clears out for absent, invalid, changing, or stale source files.
 */
int hk_temp_src_read(const char *temp_dir, HkObcTemp *out);
