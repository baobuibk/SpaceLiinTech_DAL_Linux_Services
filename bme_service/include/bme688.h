#pragma once

#include <stdbool.h>

#include "bme_i2c.h"
#include "bosch/bme68x.h"

typedef struct {
    BmeI2c *bus;
    struct bme68x_dev api;
    struct bme68x_conf conf;
    struct bme68x_heatr_conf heater;
    bool init_ok;
} Bme688Dev;

typedef struct {
    float temperature;        /* degC */
    float pressure;           /* Pa */
    float humidity;           /* %rH */
    int gas_valid;
    float gas_resistance_ohm; /* raw resistance, never an IAQ/ppm value */
} Bme688Data;

/* dev->bus must point to an open BmeI2c. Configures a 300 C / 100 ms heater. */
int bme688_init(Bme688Dev *dev);

/* Trigger one forced-mode T/P/H/gas conversion. Invalid gas leaves T/P/H valid. */
int bme688_read_forced(Bme688Dev *dev, Bme688Data *out);

/* Decode compensated Bosch field data; kept separate for fixture testing. */
int bme688_decode_field(const struct bme68x_data *field, Bme688Data *out);
