#pragma once
#include <stddef.h>
#include <stdint.h>

#define SENSOR_STAT_MAX_PAYLOAD 106U
#define SENSOR_STAT_MAX_RECORDS 8U
/* Returns 0 on success, -2 for invalid selector/cursor, -1 for capacity.
 * Offline boards return a header with zero records and board_status=1. */
int sensor_stat_read(uint8_t board, uint8_t group, uint8_t source, uint8_t cursor,
                     uint8_t *payload, size_t capacity, size_t *length);
