#include "pac1934.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const labels[2][4] = {
    { "SENSE_5V_SAMA7", "SENSE_5V_FPGA", "SENSE_3V3", "SENSE_5V_AI" },
    { "PMIC_3V3", "PMIC_DDR_1V35", "PMIC_CORE_1V15", "PMIC_CPU_1V15" }
};

static void write_value(const char *dir, const char *name, const char *value)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    assert(f);
    assert(fputs(value, f) >= 0);
    assert(fclose(f) == 0);
}

static void make_chip(const char *root, int chip, int addr)
{
    char dir[512], name[64], value[64];
    snprintf(dir, sizeof(dir), "%s/0-%04x", root, addr);
    assert(mkdir(dir, 0700) == 0);
    size_t len = strlen(dir);
    snprintf(dir + len, sizeof(dir) - len, "/iio:device%d", chip);
    assert(mkdir(dir, 0700) == 0);

    for (int channel = 1; channel <= 4; channel++) {
        snprintf(name, sizeof(name), "in_voltage%d_label", channel);
        snprintf(value, sizeof(value), "%s_VBUS_%d\n",
                 labels[chip][channel - 1], channel);
        write_value(dir, name, value);
        for (int kind = 0; kind < 2; kind++) {
            const char *type = kind ? "current" : "voltage";
            snprintf(name, sizeof(name), "in_%s%d_mean_raw", type, channel + 4);
            snprintf(value, sizeof(value), "%d\n", 1000 * (chip * 4 + channel));
            write_value(dir, name, value);
            snprintf(name, sizeof(name), "in_%s%d_scale", type, channel + 4);
            write_value(dir, name, kind ? "2\n" : "1\n");
        }
    }
}

int main(void)
{
    char root[] = "/tmp/hk-pac-test-XXXXXX";
    assert(mkdtemp(root));
    make_chip(root, 0, 0x1f);
    make_chip(root, 1, 0x10);

    HkPac1934 dev[2];
    assert(hk_pac1934_init_at(&dev[0], root, 0, 0x1f, 0) == 0);
    assert(hk_pac1934_init_at(&dev[1], root, 0, 0x10, 1) == 0);
    for (int chip = 0; chip < 2; chip++) {
        float voltage[4], current[4];
        assert(hk_pac1934_read(&dev[chip], voltage, current) == 0);
        for (int channel = 0; channel < 4; channel++) {
            float expected = (float)(chip * 4 + channel + 1);
            assert(fabsf(voltage[channel] - expected) < 0.001f);
            assert(fabsf(current[channel] - 2.0f * expected) < 0.001f);
        }
    }

    char wrong_dir[512];
    snprintf(wrong_dir, sizeof(wrong_dir), "%s/0-0010/iio:device1", root);
    write_value(wrong_dir, "in_voltage1_label", "WRONG_VBUS_1\n");
    assert(hk_pac1934_init_at(&dev[1], root, 0, 0x10, 1) == -1);
    write_value(wrong_dir, "in_voltage1_label", "PMIC_3V3_VBUS_1\n");
    assert(hk_pac1934_init_at(&dev[1], root, 0, 0x10, 1) == 0);

    char missing[512];
    snprintf(missing, sizeof(missing), "%s/0-0010/iio:device1/in_current8_scale", root);
    assert(unlink(missing) == 0);
    float voltage[4], current[4];
    assert(hk_pac1934_read(&dev[1], voltage, current) == -1);

    for (int chip = 0; chip < 2; chip++) {
        char dir[512], path[1024], name[64];
        snprintf(dir, sizeof(dir), "%s/0-%04x/iio:device%d", root,
                 chip ? 0x10 : 0x1f, chip);
        for (int channel = 1; channel <= 4; channel++) {
            snprintf(name, sizeof(name), "in_voltage%d_label", channel);
            snprintf(path, sizeof(path), "%s/%s", dir, name);
            assert(unlink(path) == 0);
            for (int kind = 0; kind < 2; kind++) {
                const char *type = kind ? "current" : "voltage";
                for (int attr = 0; attr < 2; attr++) {
                    if (chip == 1 && channel == 4 && kind == 1 && attr == 1)
                        continue;
                    snprintf(name, sizeof(name), "in_%s%d_%s", type,
                             channel + 4, attr ? "scale" : "mean_raw");
                    snprintf(path, sizeof(path), "%s/%s", dir, name);
                    assert(unlink(path) == 0);
                }
            }
        }
        assert(rmdir(dir) == 0);
        snprintf(dir, sizeof(dir), "%s/0-%04x", root, chip ? 0x10 : 0x1f);
        assert(rmdir(dir) == 0);
    }
    assert(rmdir(root) == 0);
    puts("PAC1934 two-chip IIO fixture passed");
    return 0;
}
