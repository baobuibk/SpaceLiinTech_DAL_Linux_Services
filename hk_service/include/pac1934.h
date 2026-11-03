#pragma once
/*
 * pac1934.h - PAC1934 power monitor via the Linux kernel IIO driver
 *
 * DAL U6 (0x1f) and U2 (0x10) are bound to the kernel IIO driver via devicetree,
 * exposed at /sys/bus/i2c/devices/<bus>-<addr>/iio:deviceN/. hk_service
 * used to bit-bang it directly over i2c-dev, but that proved unreliable,
 * so it now just reads the sysfs files the kernel driver already
 * publishes.
 *
 * DAL U6: 5V_SAMA7, 5V_FPGA, 3V3, 5V_AI.
 * DAL U2: PMIC_3V3, PMIC_DDR_1V35, PMIC_CORE_1V15, PMIC_CPU_1V15.
 * hk_pac1934_read() uses the averaged channels (in_voltage5-8_mean_raw /
 * in_current5-8_mean_raw, i.e. channel N's AVG counterpart at index N+4)
 * for a stable housekeeping sample. Per the IIO ABI, raw*scale already
 * yields millivolts / milliamps -- the driver applies the configured
 * shunt resistor itself, no manual resistor math needed here.
 *
 * This channel order is informational (OBC power telemetry) only. It is
 * NOT used to decide whether EXP gets pinged -- that gate uses the
 * xio-driven power switch instead (see hk_xio.h).
 */

#include <stdint.h>

typedef struct {
    char iio_dir[192];   /* .../iio:deviceN, resolved at init time */
    uint8_t addr;
    int  init_ok;
} HkPac1934;

/*
 * Resolves /sys/bus/i2c/devices/<i2c_bus>-<addr(4-hex)>/iio:deviceN/ and
 * sanity-checks the channel labels. Returns 0 on success (device bound
 * and readable), -1 on failure (driver not bound / device absent).
 */
int hk_pac1934_init(HkPac1934 *dev, int i2c_bus, uint8_t addr,
                    int chip_index);
int hk_pac1934_init_at(HkPac1934 *dev, const char *sysfs_root,
                       int i2c_bus, uint8_t addr, int chip_index);

/* Reads all 4 averaged channels. volt_v/curr_a indices match the
 * channel order above. Returns 0 on success, -1 on failure. */
int hk_pac1934_read(HkPac1934 *dev, float volt_v[4], float curr_a[4]);
