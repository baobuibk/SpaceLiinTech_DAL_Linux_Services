/*
 * pac1934.c - PAC1934 power monitor via the Linux kernel IIO driver
 */

#include "pac1934.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

static const char *const u6_labels[4] = {
    "SENSE_5V_SAMA7", "SENSE_5V_FPGA", "SENSE_3V3", "SENSE_5V_AI"
};
static const char *const u2_labels[4] = {
    "PMIC_3V3", "PMIC_DDR_1V35", "PMIC_CORE_1V15", "PMIC_CPU_1V15"
};

static int read_sysfs_line(const char *path, char *buf, size_t sz)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, (int)sz, f)) { fclose(f); return -1; }
    fclose(f);

    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    return 0;
}

static int read_sysfs_double(const char *path, double *out)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = fscanf(f, "%lf", out);
    fclose(f);
    return (n == 1) ? 0 : -1;
}

int hk_pac1934_init_at(HkPac1934 *dev, const char *sysfs_root,
                       int i2c_bus, uint8_t addr, int chip_index)
{
    memset(dev, 0, sizeof(*dev));
    dev->init_ok = 0;
    if (chip_index < 0 || chip_index > 1)
        return -1;

    char client_dir[128];
    if (snprintf(client_dir, sizeof(client_dir), "%s/%d-%04x",
                 sysfs_root, i2c_bus, addr) >= (int)sizeof(client_dir))
        return -1;

    DIR *d = opendir(client_dir);
    if (!d) {
        fprintf(stderr, "[pac1934] cannot open %s (device absent / not probed)\n", client_dir);
        return -1;
    }

    char iio_dir[192] = { 0 };
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "iio:device", 10) == 0) {
            if (snprintf(iio_dir, sizeof(iio_dir), "%s/%s",
                         client_dir, ent->d_name) >= (int)sizeof(iio_dir)) {
                closedir(d);
                return -1;
            }
            break;
        }
    }
    closedir(d);

    if (iio_dir[0] == '\0') {
        fprintf(stderr, "[pac1934] no iio:deviceN under %s "
                "(kernel driver not bound -- check devicetree)\n", client_dir);
        return -1;
    }

    snprintf(dev->iio_dir, sizeof(dev->iio_dir), "%s", iio_dir);

    /* The driver appends _VBUS_N to each device-tree channel label.
     * Refuse a mismatched mapping rather than publish misidentified rails. */
    const char *const *expect = chip_index == 1 ? u2_labels : u6_labels;
    for (int i = 0; i < 4; i++) {
        char path[256], label[128], expected[128];
        snprintf(path, sizeof(path), "%s/in_voltage%d_label", iio_dir, i + 1);
        snprintf(expected, sizeof(expected), "%s_VBUS_%d", expect[i], i + 1);
        if (read_sysfs_line(path, label, sizeof(label)) < 0) {
            fprintf(stderr, "[pac1934] cannot read %s\n", path);
            return -1;
        }
        if (strcmp(label, expected) != 0) {
            fprintf(stderr, "[pac1934] channel %d label '%s' differs from '%s'\n",
                    i + 1, label, expected);
            return -1;
        }
    }

    dev->init_ok = 1;
    dev->addr = addr;
    return 0;
}

int hk_pac1934_init(HkPac1934 *dev, int i2c_bus, uint8_t addr,
                    int chip_index)
{
    return hk_pac1934_init_at(dev, "/sys/bus/i2c/devices", i2c_bus,
                              addr, chip_index);
}

int hk_pac1934_read(HkPac1934 *dev, float volt_v[4], float curr_a[4])
{
    if (!dev->init_ok)
        return -1;

    for (int i = 0; i < 4; i++) {
        char path[256];
        double raw = 0, scale = 0;

        snprintf(path, sizeof(path), "%s/in_voltage%d_mean_raw", dev->iio_dir, i + 5);
        if (read_sysfs_double(path, &raw) < 0) return -1;
        snprintf(path, sizeof(path), "%s/in_voltage%d_scale", dev->iio_dir, i + 5);
        if (read_sysfs_double(path, &scale) < 0) return -1;
        volt_v[i] = (float)(raw * scale / 1000.0); /* mV -> V */

        snprintf(path, sizeof(path), "%s/in_current%d_mean_raw", dev->iio_dir, i + 5);
        if (read_sysfs_double(path, &raw) < 0) return -1;
        snprintf(path, sizeof(path), "%s/in_current%d_scale", dev->iio_dir, i + 5);
        if (read_sysfs_double(path, &scale) < 0) return -1;
        curr_a[i] = (float)(raw * scale / 1000.0); /* mA -> A */
    }

    return 0;
}
