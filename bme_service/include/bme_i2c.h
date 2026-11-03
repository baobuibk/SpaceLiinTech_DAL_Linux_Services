#pragma once
/*
 * bme_i2c.h - Linux i2c-dev bus wrapper for BME688
 */

#include <stdint.h>
#include <stddef.h>

#define DEFAULT_BME_I2C_DEV   "/dev/i2c-0"
#define DEFAULT_BME_I2C_ADDR  0x76u  /* SDO -> GND */

typedef struct {
    int     fd;
    uint8_t addr;
    char    dev_path[64];
} BmeI2c;

/* Open dev_path and bind the 7-bit slave address. Returns 0 on success. */
int  bme_i2c_open (BmeI2c *bus, const char *dev_path, uint8_t addr);
void bme_i2c_close(BmeI2c *bus);

/* Single register write: [reg, val]. Returns 0 on success. */
int bme_i2c_write_reg(BmeI2c *bus, uint8_t reg, uint8_t val);

/* Write consecutive registers; required by Bosch's SensorAPI callback. */
int bme_i2c_write_regs(BmeI2c *bus, uint8_t reg,
                       const uint8_t *data, size_t len);

/* Write reg pointer (no STOP) then repeated-START read len bytes. */
int bme_i2c_read_regs(BmeI2c *bus, uint8_t reg, uint8_t *buf, size_t len);
