#ifndef LIBGSCSP_INCLUDE_GS_CSP_DRIVERS_KISS_KISS_H
#define LIBGSCSP_INCLUDE_GS_CSP_DRIVERS_KISS_KISS_H
/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */

/**
   @file

   GomSpace KISS API for CSP.
*/

#include <gs/util/error.h>
#include <csp/interfaces/csp_if_kiss.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
   Initialize CSP for KISS device.
   @note KISS/UART device must be initialized all ready.

   @param[in] device KISS/UART device.
   @return_gs_error_t
*/
gs_error_t gs_csp_kiss_init(uint8_t device);

#ifdef __cplusplus
}
#endif
#endif
