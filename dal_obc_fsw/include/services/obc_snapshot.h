#pragma once

#include <time.h>
#include "services/hk_service.h"

/* Read one atomic hk_service snapshot. Returns 0 only for fresh, valid JSON. */
int obc_snapshot_read(const char *path, time_t now, unsigned max_age_sec,
                      bee_dal_telemetry_t *out);
