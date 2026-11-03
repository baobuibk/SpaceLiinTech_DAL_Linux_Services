#pragma once

#include <stdint.h>
#include "services/hk_service.h"

/* TYPE 0x04, ADDR 0x02, 64-byte OBC housekeeping response payload. */
#define OBC_HK_WIRE_LEN 64U

void obc_hk_encode(const bee_dal_telemetry_t *snapshot,
                   uint8_t out[OBC_HK_WIRE_LEN]);
