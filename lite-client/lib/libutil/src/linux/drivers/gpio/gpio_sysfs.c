/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */
/**
    @file

    @brief GPIO Implementation for Linux of the GPIO API in libutil.

    The GPIO driver provides a simple interface toward driving HW GPIO's.
*/

#include <gs/util/linux/drivers/gpio/gpio_sysfs.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <gs/util/log.h>

#include <gs/util/linux/sysfs_helper.h>

gs_error_t gs_gpio_sysfs_initialize(gs_gpio_t gpio, bool output,bool init_value, bool active_low)
{
    char gpio_pin_str[6];
    snprintf(gpio_pin_str, sizeof(gpio_pin_str), "%d", gpio.pin);

    /* Try to unexport first */
    gs_sysfs_write_file("/sys/class/gpio/unexport", gpio_pin_str);

    if (gs_sysfs_write_file("/sys/class/gpio/export", gpio_pin_str) != GS_OK)
    {
        log_warning("failed to export GPIO %s: %s", gpio_pin_str, strerror(errno));
        return GS_ERROR_NOT_SUPPORTED;
    }

    char gpio_sys_fname[128];
    snprintf(gpio_sys_fname, sizeof(gpio_sys_fname), "/sys/class/gpio/gpio%d/active_low", gpio.pin);
    const char * active_low_str = active_low ? "1" : "0";

    if (gs_sysfs_write_file(gpio_sys_fname, active_low_str) != GS_OK)
    {
        log_warning("failed to set GPIO %d active_low: %s", gpio.pin, strerror(errno));
        return GS_ERROR_NOT_SUPPORTED;
    }

    snprintf(gpio_sys_fname, sizeof(gpio_sys_fname), "/sys/class/gpio/gpio%d/direction", gpio.pin);

    /* Glitch-free output set (high/low makes pin an output and sets value to 1/0 respectively)*/
    const char * dir = output ? (init_value ? "high" : "low") : "in";

    if (gs_sysfs_write_file(gpio_sys_fname, dir) != GS_OK)
    {
        log_warning("failed to set GPIO %d direction: %s", gpio.pin, strerror(errno));
        return GS_ERROR_NOT_SUPPORTED;
    }

    return GS_OK;
}

gs_error_t gs_gpio_sysfs_get(gs_gpio_t gpio, bool *value, void * driver_data)
{
    char gpio_sys_fname[128];
    snprintf(gpio_sys_fname, sizeof(gpio_sys_fname), "/sys/class/gpio/gpio%d/value", gpio.pin);

    if (access(gpio_sys_fname, R_OK) != 0)
    {
        log_error("GPIO %d not initialized - Can't read the input.", gpio.pin);
        return GS_ERROR_ACCESS;
    }

    char value_str[10];
    gs_error_t ret = gs_sysfs_read_file(gpio_sys_fname, value_str, sizeof(value_str));
    if (ret == GS_OK)
    {
        if (strcmp(value_str, "1") == 0)
            *value = true;
        else
            *value = false;
    }

    return ret;
}

bool gs_gpio_sysfs_get_nc(gs_gpio_t gpio, void * driver_data)
{
    char gpio_sys_fname[128];
    snprintf(gpio_sys_fname, sizeof(gpio_sys_fname), "/sys/class/gpio/gpio%d/value", gpio.pin);

    if (access(gpio_sys_fname, R_OK) != 0)
    {
        log_error("GPIO %d not initialized - Can't read the input.", gpio.pin);
        return 0;
    }

    char value_str[10];
    gs_sysfs_read_file(gpio_sys_fname, value_str, sizeof(value_str));

    if (strncmp(value_str, "1", 10) == 0) {
        return true;
    } else {
        return false;
    }
}

gs_error_t gs_gpio_sysfs_set(gs_gpio_t gpio, bool value, void * driver_data)
{
    const char *value_str = value ? "1" : "0";

    char gpio_sys_fname[128];
    snprintf(gpio_sys_fname, sizeof(gpio_sys_fname), "/sys/class/gpio/gpio%d/value", gpio.pin);
    if (access(gpio_sys_fname, W_OK) == 0)
    {
        return gs_sysfs_write_file(gpio_sys_fname, value_str);
    }

    log_error("GPIO %d not initialized - Can't set the output.", gpio.pin);
    return GS_ERROR_ACCESS;
}

void gs_gpio_sysfs_set_nc(gs_gpio_t gpio, bool value, void * driver_data)
{
    const char *value_str = value ? "1" : "0";

    char gpio_sys_fname[128];
    snprintf(gpio_sys_fname, sizeof(gpio_sys_fname), "/sys/class/gpio/gpio%d/value", gpio.pin);
    if (access(gpio_sys_fname, W_OK) == 0)
    {
        gs_sysfs_write_file(gpio_sys_fname, value_str);
        return;
    }
    log_error("GPIO %d not initialized - Can't set the output.", gpio.pin);
}

gs_error_t gs_gpio_sysfs_init_as_interrupt(gs_gpio_t gpio, const gs_interrupt_conf_t * conf, void * driver_data)
{
    return GS_ERROR_NOT_IMPLEMENTED;
}

const gs_gpio_driver_t gs_gpio_sysfs_driver = {
    .get_handler = gs_gpio_sysfs_get,
    .get_nc_handler = gs_gpio_sysfs_get_nc,
    .set_handler = gs_gpio_sysfs_set,
    .set_nc_handler = gs_gpio_sysfs_set_nc,
    .init_as_interrupt_handler = gs_gpio_sysfs_init_as_interrupt,
};

