#include "bme688.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    struct bme68x_data field = {0};
    Bme688Data out;
    field.temperature = 24.5f;
    field.pressure = 101325.0f;
    field.humidity = 43.0f;
    field.gas_resistance = 12500.0f;

    field.status = BME68X_NEW_DATA_MSK |
                   BME68X_GASM_VALID_MSK | BME68X_HEAT_STAB_MSK;
    assert(bme688_decode_field(&field, &out) == 0);
    assert(out.gas_valid == 1 && out.gas_resistance_ohm == 12500.0f);
    assert(out.temperature == 24.5f && out.pressure == 101325.0f);

    field.status &= (uint8_t)~BME68X_HEAT_STAB_MSK;
    assert(bme688_decode_field(&field, &out) == 0);
    assert(out.gas_valid == 0 && out.gas_resistance_ohm == 0.0f);

    field.status |= BME68X_HEAT_STAB_MSK;
    field.gas_resistance = NAN;
    assert(bme688_decode_field(&field, &out) == 0);
    assert(out.gas_valid == 0);

    field.status = BME68X_GASM_VALID_MSK | BME68X_HEAT_STAB_MSK;
    assert(bme688_decode_field(&field, &out) == -1);

    puts("BME688 gas status decoding fixtures passed");
    return 0;
}
