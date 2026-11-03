#pragma once
#include <stdint.h>

typedef struct {
    uint16_t on;
    uint16_t known;
    uint16_t failed;
} power_en_result_t;

/* addr 3=OBC, 4=EXP, 5=PDU. select=0 reads without changing outputs. */
int power_en_execute(uint8_t addr, uint16_t select, uint16_t on, uint8_t flags,
                     power_en_result_t *result);
