/*
 * bme_i2c.c - Linux i2c-dev bus wrapper for BME688
 */

#include "bme_i2c.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

int bme_i2c_open(BmeI2c *bus, const char *dev_path, uint8_t addr)
{
    memset(bus, 0, sizeof(*bus));

    bus->fd = open(dev_path, O_RDWR | O_CLOEXEC);
    if (bus->fd < 0) {
        fprintf(stderr, "[bme_i2c] open(%s) failed: %s\n",
                dev_path, strerror(errno));
        return -1;
    }

    bus->addr = addr;
    snprintf(bus->dev_path, sizeof(bus->dev_path), "%s", dev_path);
    return 0;
}

void bme_i2c_close(BmeI2c *bus)
{
    if (bus->fd >= 0) {
        close(bus->fd);
        bus->fd = -1;
    }
}

int bme_i2c_write_reg(BmeI2c *bus, uint8_t reg, uint8_t val)
{
    return bme_i2c_write_regs(bus, reg, &val, 1);
}

int bme_i2c_write_regs(BmeI2c *bus, uint8_t reg,
                       const uint8_t *data, size_t len)
{
    if (!bus || bus->fd < 0 || !data || len == 0 || len >= UINT16_MAX) {
        errno = EINVAL;
        return -1;
    }
    uint8_t *frame = malloc(len + 1);
    if (!frame)
        return -1;
    frame[0] = reg;
    memcpy(&frame[1], data, len);
    struct i2c_msg msg = {
        .addr  = bus->addr,
        .flags = 0,
        .len   = (uint16_t)(len + 1),
        .buf   = frame,
    };
    struct i2c_rdwr_ioctl_data xfer = { .msgs = &msg, .nmsgs = 1 };

    int result = ioctl(bus->fd, I2C_RDWR, &xfer);
    free(frame);
    if (result < 0) {
        fprintf(stderr, "[bme_i2c] write reg=0x%02X failed: %s\n",
                reg, strerror(errno));
        return -1;
    }
    return 0;
}

int bme_i2c_read_regs(BmeI2c *bus, uint8_t reg, uint8_t *buf, size_t len)
{
    if (!bus || bus->fd < 0 || !buf || len == 0 || len > UINT16_MAX) {
        errno = EINVAL;
        return -1;
    }
    struct i2c_msg msgs[2] = {
        {
            .addr  = bus->addr,
            .flags = 0,
            .len   = 1,
            .buf   = &reg,
        },
        {
            .addr  = bus->addr,
            .flags = I2C_M_RD,
            .len   = (uint16_t)len,
            .buf   = buf,
        },
    };
    struct i2c_rdwr_ioctl_data xfer = { .msgs = msgs, .nmsgs = 2 };

    if (ioctl(bus->fd, I2C_RDWR, &xfer) < 0) {
        fprintf(stderr, "[bme_i2c] read reg=0x%02X len=%zu failed: %s\n",
                reg, len, strerror(errno));
        return -1;
    }
    return 0;
}
