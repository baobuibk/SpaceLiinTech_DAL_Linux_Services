#pragma once
/*
 * hk_interval.h - Per-board runtime-adjustable collect interval
 *
 * Layout under var_dir/ (default /home/root/var/hk_service/):
 *   obc.interval    seconds, plain text
 *   exp.interval    seconds, plain text
 *
 * An operator (or an app on the OBC) writes a new value into one of
 * these files at any time to change that board's collect cadence
 * without restarting hk_service. Reading is polled fresh on every
 * tick (cheap: a handful of bytes) so a change takes effect on the
 * next tick, no signal/restart needed.
 *
 * Range is [HK_INTERVAL_MIN_S, HK_INTERVAL_MAX_S]. An out-of-range
 * value written to the file is clamped ONLY for internal use -- the
 * file itself is never rewritten by hk_service, so the operator's
 * literal input is always visible via `cat`.
 */

#include <stdint.h>

#define HK_INTERVAL_MIN_S 3
#define HK_INTERVAL_MAX_S 3600

typedef struct {
    char     path[300];
    uint32_t default_s;
} HkInterval;

/*
 * Ensures var_dir exists. If path (var_dir/<name>.interval) does not
 * already exist, creates it seeded with default_s so `cat` shows a
 * sane value immediately. An existing file (e.g. across a service
 * restart) is left untouched.
 */
void hk_interval_init(HkInterval *iv, const char *var_dir, const char *name, uint32_t default_s);

/* Reads the file fresh, clamps to [HK_INTERVAL_MIN_S, HK_INTERVAL_MAX_S].
 * Falls back to default_s if the file is missing/unreadable/garbage. */
uint32_t hk_interval_get(const HkInterval *iv);
