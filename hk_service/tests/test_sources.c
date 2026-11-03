#include "hk_bme_src.h"
#include "hk_temp_src.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void write_value(const char *dir, const char *name, const char *value)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    assert(f);
    assert(fputs(value, f) >= 0);
    assert(fclose(f) == 0);
}

static void remove_values(const char *dir, const char *const *names, size_t count)
{
    char path[512];
    for (size_t i = 0; i < count; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        assert(unlink(path) == 0);
    }
    assert(rmdir(dir) == 0);
}

int main(void)
{
    char root[] = "/tmp/dal-hk-sources-XXXXXX";
    assert(mkdtemp(root));
    char temp_dir[256], bme_dir[256], stamp[32];
    snprintf(temp_dir, sizeof(temp_dir), "%s/temp", root);
    snprintf(bme_dir, sizeof(bme_dir), "%s/bme", root);
    assert(mkdir(temp_dir, 0700) == 0);
    assert(mkdir(bme_dir, 0700) == 0);
    time_t now = time(NULL);
    snprintf(stamp, sizeof(stamp), "%ld\n", (long)now);

    const char *const temp_names[] = {
        "temp_status", "temp_timestamp", "temp_onboard", "temp_ntc1",
        "temp_ntc2", "temp_ntc3", "temp_ntc4"
    };
    write_value(temp_dir, "temp_status", "1\n");
    write_value(temp_dir, "temp_timestamp", stamp);
    write_value(temp_dir, "temp_onboard", "30.5\n");
    for (int i = 1; i <= 4; i++) {
        char name[24];
        snprintf(name, sizeof(name), "temp_ntc%d", i);
        write_value(temp_dir, name, "25.0\n");
    }
    HkObcTemp temp;
    assert(hk_temp_src_read(temp_dir, &temp) == 0);
    assert(temp.valid == 1 && temp.onboard_c == 30.5f);
    snprintf(stamp, sizeof(stamp), "%ld\n", (long)(now - 11));
    write_value(temp_dir, "temp_timestamp", stamp);
    assert(hk_temp_src_read(temp_dir, &temp) == -1);
    assert(temp.valid == 0);

    const char *const bme_names[] = {
        "bme_status", "bme_timestamp", "bme_temperature",
        "bme_pressure", "bme_humidity", "bme_gas_valid",
        "bme_gas_resistance_ohm"
    };
    write_value(bme_dir, "bme_status", "1\n");
    snprintf(stamp, sizeof(stamp), "%ld\n", (long)now);
    write_value(bme_dir, "bme_timestamp", stamp);
    write_value(bme_dir, "bme_temperature", "24.5\n");
    write_value(bme_dir, "bme_pressure", "101325\n");
    write_value(bme_dir, "bme_humidity", "40.25\n");
    write_value(bme_dir, "bme_gas_valid", "0\n");
    write_value(bme_dir, "bme_gas_resistance_ohm", "0.00\n");
    HkBme bme;
    assert(hk_bme_src_read(bme_dir, now, &bme) == 0);
    assert(bme.valid == 1 && bme.pressure_pa == 101325.0f && !bme.gas_valid);
    write_value(bme_dir, "bme_gas_valid", "1\n");
    write_value(bme_dir, "bme_gas_resistance_ohm", "12500.00\n");
    assert(hk_bme_src_read(bme_dir, now, &bme) == 0);
    assert(bme.gas_valid == 1 && bme.gas_resistance_ohm == 12500.0f);
    write_value(bme_dir, "bme_gas_resistance_ohm", "invalid\n");
    assert(hk_bme_src_read(bme_dir, now, &bme) == 0);
    assert(!bme.gas_valid);
    write_value(bme_dir, "bme_status", "0\n");
    assert(hk_bme_src_read(bme_dir, now, &bme) == -1);
    assert(bme.valid == 0);

    remove_values(temp_dir, temp_names, sizeof(temp_names) / sizeof(temp_names[0]));
    remove_values(bme_dir, bme_names, sizeof(bme_names) / sizeof(bme_names[0]));
    assert(rmdir(root) == 0);
    puts("BME and temperature freshness fixtures passed");
    return 0;
}
