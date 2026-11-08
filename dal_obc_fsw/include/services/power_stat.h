#pragma once
#include <stddef.h>
#include <stdint.h>

#define POWER_STAT_MAX_PAYLOAD 198U

/* Returns 0 on success, -2 for an invalid board/mask. Unavailable sources
 * remain in the reply with their validity flags cleared. */
int power_stat_read(uint8_t board, uint16_t mask, uint8_t *payload,
                    size_t capacity, size_t *length);

/* ICD v1.2: one report selector (0 all, 1 monitor, 2 enable, 3 error).
 * TM: schema/report U16, count U16, availability-error summary U16,
 * followed by six-byte source records and a 16-byte PDU status trailer.
 * Schema 2; max 27 records, 184 bytes. */
#define POWER_STAT_V12_MAX_PAYLOAD 200U
int power_stat_report(uint8_t report, uint8_t *payload, size_t capacity, size_t *length);
