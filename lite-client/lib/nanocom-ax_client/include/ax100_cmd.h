/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */

#ifndef LIB_NANOCOM_AX_CLIENT_INCLUDE_AX100_CMD_H_
#define LIB_NANOCOM_AX_CLIENT_INCLUDE_AX100_CMD_H_

#include <stdint.h>

#define HMAC_MAX_DELAY_SEC 60*60*24

typedef struct __attribute__((__packed__)) hmac_cmd_s {
    uint32_t delay;
    uint8_t idx;
} hmac_cmd_t;


#endif /* LIB_NANOCOM_AX_CLIENT_INCLUDE_AX100_CMD_H_ */
