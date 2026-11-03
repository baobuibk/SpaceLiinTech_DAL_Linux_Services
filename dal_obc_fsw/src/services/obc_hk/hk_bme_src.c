#include "hk_bme_src.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SENSOR_MAX_AGE_S 10

static int read_number(const char *dir, const char *name, double *value)
{
    char path[512], buf[80], *end;
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path))
        return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int ok = fgets(buf, sizeof(buf), f) != NULL;
    fclose(f);
    if (!ok) return -1;
    errno = 0;
    *value = strtod(buf, &end);
    if (errno || end == buf || !isfinite(*value)) return -1;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
    return *end == '\0' ? 0 : -1;
}

int hk_bme_src_read(const char *dir, time_t now, HkBme *out)
{
    memset(out, 0, sizeof(*out));
    double status, before, after, temp, pressure, humidity;
    if (read_number(dir, "bme_timestamp", &before) < 0 ||
        read_number(dir, "bme_status", &status) < 0 ||
        read_number(dir, "bme_temperature", &temp) < 0 ||
        read_number(dir, "bme_pressure", &pressure) < 0 ||
        read_number(dir, "bme_humidity", &humidity) < 0 ||
        read_number(dir, "bme_timestamp", &after) < 0)
        return -1;
    if (before != after || status != 1 || before <= 0 ||
        before > (double)now + 2 || (double)now - before > SENSOR_MAX_AGE_S)
        return -1;
    out->valid = 1;
    out->src_timestamp = (time_t)after;
    out->temperature_c = (float)temp;
    out->pressure_pa = (float)pressure;
    out->humidity_rh = (float)humidity;
    double gas_status, gas_ohm;
    if (read_number(dir, "bme_gas_valid", &gas_status) == 0 &&
        gas_status == 1 &&
        read_number(dir, "bme_gas_resistance_ohm", &gas_ohm) == 0 &&
        gas_ohm > 0 && gas_ohm <= 1e12) {
        out->gas_valid = 1;
        out->gas_resistance_ohm = (float)gas_ohm;
    }
    double final_timestamp;
    if (read_number(dir, "bme_timestamp", &final_timestamp) < 0 ||
        final_timestamp != after) {
        memset(out, 0, sizeof(*out));
        return -1;
    }
    return 0;
}
