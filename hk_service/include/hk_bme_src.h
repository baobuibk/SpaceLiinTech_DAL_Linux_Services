#pragma once

#include <time.h>

typedef struct {
    int valid;
    float temperature_c;
    float pressure_pa;
    float humidity_rh;
    int gas_valid;
    float gas_resistance_ohm;
    time_t src_timestamp;
} HkBme;

/* Reads bme_service's published sample. Validity includes age and status. */
int hk_bme_src_read(const char *dir, time_t now, HkBme *out);
