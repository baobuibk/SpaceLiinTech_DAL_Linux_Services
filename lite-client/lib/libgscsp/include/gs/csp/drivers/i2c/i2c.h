#ifndef LIBGSCSP_INCLUDE_GS_CSP_DRIVERS_I2C_I2C_H
#define LIBGSCSP_INCLUDE_GS_CSP_DRIVERS_I2C_I2C_H
/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */
/**
   @file

   GomSpace I2C API for CSP.
*/

#include <gs/util/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
   Initialize CSP for I2C device.
   @note I2C device must be initialized allready with the same I2C address as the CSP address.

   @param[in] device I2C device.
   @param[in] csp_addr CSP address.
   @return_gs_error_t
*/
gs_error_t gs_csp_i2c_init(uint8_t device, uint8_t csp_addr);

#ifdef __cplusplus
}
#endif
#endif
