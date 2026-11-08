#pragma once
#include <stdint.h>
#define TM_GET_V12_LEN 57U
/* Cached ICD 04/00 payload; returns -1 when the OBC snapshot is unavailable.
 * I16 invalid = INT16_MIN, U16 invalid = UINT16_MAX. */
int tm_get_encode(uint8_t payload[TM_GET_V12_LEN]);
