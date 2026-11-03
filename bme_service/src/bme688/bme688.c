/* Linux I2C adapter and forced-mode acquisition using Bosch SensorAPI. */

#include "bme688.h"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <time.h>

#define HEATER_TEMP_C 300u
#define HEATER_DURATION_MS 100u
#define READY_MARGIN_US 10000u
#define READY_RETRY_US 10000u
#define READY_RETRIES 4

static BME68X_INTF_RET_TYPE read_cb(uint8_t reg, uint8_t *data,
                                    uint32_t len, void *context)
{
    return bme_i2c_read_regs(context, reg, data, len) == 0 ? 0 : -1;
}

static BME68X_INTF_RET_TYPE write_cb(uint8_t reg, const uint8_t *data,
                                     uint32_t len, void *context)
{
    return bme_i2c_write_regs(context, reg, data, len) == 0 ? 0 : -1;
}

static void delay_cb(uint32_t period_us, void *context)
{
    (void)context;
    struct timespec req = {
        .tv_sec = period_us / 1000000u,
        .tv_nsec = (long)(period_us % 1000000u) * 1000L,
    };
    while (nanosleep(&req, &req) < 0 && errno == EINTR) { }
}

int bme688_init(Bme688Dev *dev)
{
    if (!dev || !dev->bus || dev->bus->fd < 0)
        return -1;

    dev->init_ok = false;
    memset(&dev->api, 0, sizeof(dev->api));
    memset(&dev->conf, 0, sizeof(dev->conf));
    memset(&dev->heater, 0, sizeof(dev->heater));

    dev->api.intf = BME68X_I2C_INTF;
    dev->api.intf_ptr = dev->bus;
    dev->api.read = read_cb;
    dev->api.write = write_cb;
    dev->api.delay_us = delay_cb;
    dev->api.amb_temp = 25;
    if (bme68x_init(&dev->api) != BME68X_OK)
        return -1;

    dev->conf.os_temp = BME68X_OS_2X;
    dev->conf.os_pres = BME68X_OS_4X;
    dev->conf.os_hum = BME68X_OS_1X;
    dev->conf.filter = BME68X_FILTER_SIZE_3;
    dev->conf.odr = BME68X_ODR_NONE;
    if (bme68x_set_conf(&dev->conf, &dev->api) != BME68X_OK)
        return -1;

    dev->heater.enable = BME68X_ENABLE;
    dev->heater.heatr_temp = HEATER_TEMP_C;
    dev->heater.heatr_dur = HEATER_DURATION_MS;
    if (bme68x_set_heatr_conf(BME68X_FORCED_MODE, &dev->heater,
                              &dev->api) != BME68X_OK)
        return -1;

    dev->init_ok = true;
    return 0;
}

int bme688_decode_field(const struct bme68x_data *field, Bme688Data *out)
{
    if (!field || !out || !(field->status & BME68X_NEW_DATA_MSK) ||
        !isfinite(field->temperature) || !isfinite(field->pressure) ||
        !isfinite(field->humidity))
        return -1;

    memset(out, 0, sizeof(*out));
    out->temperature = field->temperature;
    out->pressure = field->pressure;
    out->humidity = field->humidity;

    const uint8_t gas_ok = BME68X_GASM_VALID_MSK | BME68X_HEAT_STAB_MSK;
    if ((field->status & gas_ok) == gas_ok &&
        isfinite(field->gas_resistance) &&
        field->gas_resistance > 0.0f &&
        field->gas_resistance <= 1e12f) {
        out->gas_valid = 1;
        out->gas_resistance_ohm = field->gas_resistance;
    }
    return 0;
}

int bme688_read_forced(Bme688Dev *dev, Bme688Data *out)
{
    if (!dev || !out || !dev->init_ok)
        return -1;
    if (bme68x_set_op_mode(BME68X_FORCED_MODE, &dev->api) != BME68X_OK)
        return -1;

    uint32_t wait_us = bme68x_get_meas_dur(BME68X_FORCED_MODE,
                                             &dev->conf, &dev->api) +
                       (uint32_t)dev->heater.heatr_dur * 1000u +
                       READY_MARGIN_US;
    delay_cb(wait_us, NULL);

    for (int retry = 0; retry < READY_RETRIES; retry++) {
        struct bme68x_data field = {0};
        uint8_t count = 0;
        int8_t result = bme68x_get_data(BME68X_FORCED_MODE, &field,
                                        &count, &dev->api);
        if (result == BME68X_OK && count > 0)
            return bme688_decode_field(&field, out);
        if (result < 0)
            return -1;
        delay_cb(READY_RETRY_US, NULL);
    }
    return -1;
}
