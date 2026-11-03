#pragma once
#include <stddef.h>
#include <stdint.h>

#define POWER_STAT_MAX_PAYLOAD 198U

/* Returns 0 on success, -2 for an invalid board/mask. Unavailable sources
 * remain in the reply with their validity flags cleared. */
int power_stat_read(uint8_t board, uint16_t mask, uint8_t *payload,
                    size_t capacity, size_t *length);
