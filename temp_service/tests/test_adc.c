#include "temp_adc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *dir, const char *name, const char *value)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    assert(f);
    assert(fputs(value, f) >= 0);
    assert(fclose(f) == 0);
}

int main(void)
{
    char root[] = "/tmp/dal-temp-adc-XXXXXX";
    assert(mkdtemp(root));
    char pac[256], adc[256], found[256], path[512];
    snprintf(pac, sizeof(pac), "%s/iio:device0", root);
    snprintf(adc, sizeof(adc), "%s/iio:device1", root);
    assert(mkdir(pac, 0700) == 0);
    assert(mkdir(adc, 0700) == 0);
    write_file(pac, "in_voltage_scale", "0.1\n");
    write_file(pac, "in_voltage1_raw", "1\n");
    write_file(adc, "in_voltage_scale", "0.038146972\n");
    const int channels[] = { 9, 2, 4, 3, 7 };
    for (int i = 0; i < 5; i++) {
        char name[32];
        snprintf(name, sizeof(name), "in_voltage%d_raw", channels[i]);
        write_file(adc, name, i == 0 ? "26214\n" : "43254\n");
    }
    assert(temp_adc_find_device(root, found, sizeof(found)) == 0);
    assert(strcmp(found, adc) == 0);

    TempAdcCfg cfg;
    temp_adc_cfg_defaults(&cfg);
    assert(strlen(adc) < sizeof(cfg.adc_dev));
    strcpy(cfg.adc_dev, adc);
    TempData data = {0};
    assert(temp_adc_read(&data, &cfg) == 0);
    assert(data.valid == 1);
    assert(fabsf(data.onboard - 50.0f) < 0.1f);
    for (int i = 0; i < 4; i++)
        assert(fabsf(data.ntc[i] - 25.0f) < 0.1f);

    snprintf(path, sizeof(path), "%s/in_voltage_scale", adc);
    assert(unlink(path) == 0);
    assert(temp_adc_read(&data, &cfg) == -1);
    for (int i = 0; i < 5; i++) {
        snprintf(path, sizeof(path), "%s/in_voltage%d_raw", adc, channels[i]);
        assert(unlink(path) == 0);
    }
    assert(rmdir(adc) == 0);
    snprintf(path, sizeof(path), "%s/in_voltage_scale", pac);
    assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/in_voltage1_raw", pac);
    assert(unlink(path) == 0);
    assert(rmdir(pac) == 0);
    assert(rmdir(root) == 0);
    puts("ADC discovery and conversion fixture passed");
    return 0;
}
