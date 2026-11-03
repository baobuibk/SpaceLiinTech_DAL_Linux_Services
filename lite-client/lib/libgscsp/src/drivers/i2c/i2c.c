/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */

#include <gs/csp/csp.h>
#include <gs/csp/error.h>
#include <gs/csp/drivers/i2c/i2c.h>
#include <csp/interfaces/csp_if_i2c.h>
#include <gs/util/drivers/i2c/master.h>
#include <gs/util/drivers/i2c/slave.h>
#include <gs/util/types.h>
#if !defined(__linux__)
#include <gs/embed/freertos.h>
#endif

#define E_FAIL      -19 // The CSP I2C driver evaluates any other value than -1 as fail

#define I2C_FRAME_OVERHEAD      (sizeof(i2c_frame_t) - sizeof(((i2c_frame_t *)0)->data))

static void gs_csp_i2c_rxdata_callback_isr(uint8_t handle, const uint8_t * rx, size_t rx_length, gs_context_switch_t * cswitch)
{
    i2c_frame_t * frame = (i2c_frame_t *) (rx - I2C_FRAME_OVERHEAD);
    frame->len = rx_length;
#if (__linux__)
    csp_i2c_rx(frame, NULL);
#else
    csp_i2c_rx(frame, &cswitch->task_woken);
#endif
}

static void * gs_csp_i2c_get_buffer(uint8_t handle)
{
    void * buff = csp_buffer_get_isr(I2C_MTU);
    if (buff != NULL) {
        buff = ((uint8_t *)buff) + I2C_FRAME_OVERHEAD;
    }
    return buff;
}

/**
 CSP send function, required by libcsp
 */
int i2c_send(int handle, i2c_frame_t * frame, uint16_t timeout)
{
    int res_tx = gs_i2c_master_transaction(handle, frame->dest, frame->data, frame->len, 0, 0, timeout);
    if (res_tx == GS_OK) {
        csp_buffer_free(frame);
        return E_NO_ERR;
    } else {
        return E_FAIL;
    }
}

/**
 CSP init function, required by libcsp
 */
int i2c_init(int handle, int mode, uint8_t addr, uint16_t speed, int queue_len_tx, int queue_len_rx,
             i2c_callback_t callback)
{
    if (gs_i2c_slave_set_rx(handle, gs_csp_i2c_rxdata_callback_isr) != GS_OK) {
        return E_FAIL;
    }
    if (gs_i2c_slave_set_get_rx_buf(handle, gs_csp_i2c_get_buffer, I2C_MTU) != GS_OK) {
        return E_FAIL;
    }
    if (gs_i2c_slave_start(handle) != GS_OK) {
        return E_FAIL;
    }
    return E_NO_ERR;
}

gs_error_t gs_csp_i2c_init(uint8_t device, uint8_t csp_addr)
{
    int dummy_speed = 0; // Speed not used

    /* Calls CSP I2C init, which has the I2C interface instance
       From here the above "i2c_init" is called */
    return gs_csp_error(csp_i2c_init(csp_addr, device, dummy_speed));
}
