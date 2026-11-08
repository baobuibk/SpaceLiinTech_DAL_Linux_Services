#pragma once
#include <stddef.h>
#include <stdint.h>
/* Native ICD 01/05: GET length 0, SET length 8, reply length 8.
 * Return 0 on success, -2 invalid request, -1 read/apply failure. */
int sensor_cfg_execute(const uint8_t *request, size_t length, uint8_t reply[8]);
