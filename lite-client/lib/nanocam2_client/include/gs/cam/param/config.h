#ifndef GS_CAM_PARAM_CONFIG_H
#define GS_CAM_PARAM_CONFIG_H

/* Copyright (c) 2013 GomSpace A/S. All rights reserved. (AUTO GENERATED) */
#include <gs/param/table.h>

#ifdef __cplusplus
extern "C" {
#endif

// Table parameters address/offset
#define GS_CAM_CONFIG_CSP_ADDR                                           0                // 0x0000 uint8
#define GS_CAM_CONFIG_CSP_GATEWAY                                        1                // 0x0001 uint8
#define GS_CAM_CONFIG_CSP_RTABLE                                         2                // 0x0002 string
#define GS_CAM_CONFIG_CSP_RTABLE_LENGTH                                 96
#define GS_CAM_CONFIG_CAN_BITRATE                                      100                // 0x0064 uint32
#define GS_CAM_CONFIG_I2C_BITRATE                                      104                // 0x0068 uint32
#define GS_CAM_CONFIG_KISS_BITRATE                                     108                // 0x006c uint32
#define GS_CAM_CONFIG_KISS_IF                                          112                // 0x0070 string
#define GS_CAM_CONFIG_KISS_IF_LENGTH                                    16

#ifndef __cplusplus
static const gs_param_table_row_t gs_cam_config_table[] = {
    {.name = "csp-addr",      .addr = GS_CAM_CONFIG_CSP_ADDR,                            .type = GS_PARAM_UINT8,   .size =   1, .array_size =  1, .flags = 0},
    {.name = "csp-gateway",   .addr = GS_CAM_CONFIG_CSP_GATEWAY,                         .type = GS_PARAM_UINT8,   .size =   1, .array_size =  1, .flags = 0},
    {.name = "csp-rtable",    .addr = GS_CAM_CONFIG_CSP_RTABLE,                          .type = GS_PARAM_STRING,  .size =  96, .array_size =  1, .flags = 0},
    {.name = "can-bitrate",   .addr = GS_CAM_CONFIG_CAN_BITRATE,                         .type = GS_PARAM_UINT32,  .size =   4, .array_size =  1, .flags = 0},
    {.name = "i2c-bitrate",   .addr = GS_CAM_CONFIG_I2C_BITRATE,                         .type = GS_PARAM_UINT32,  .size =   4, .array_size =  1, .flags = 0},
    {.name = "kiss-bitrate",  .addr = GS_CAM_CONFIG_KISS_BITRATE,                        .type = GS_PARAM_UINT32,  .size =   4, .array_size =  1, .flags = 0},
    {.name = "kiss-if",       .addr = GS_CAM_CONFIG_KISS_IF,                             .type = GS_PARAM_STRING,  .size =  16, .array_size =  1, .flags = 0},
};
#endif

#define GS_CAM_CONFIG_TABLE_NAME                         "config"
#define GS_CAM_CONFIG_TABLE_ID                           0
#define GS_CAM_CONFIG_TABLE_MEM_ID                       0 // legacy definition for table id
#define GS_CAM_CONFIG_TABLE_SIZE                         128 // bytes
#define GS_CAM_CONFIG_TABLE_ROWS                         (sizeof(gs_cam_config_table) / sizeof(gs_cam_config_table[0]))

#define GS_CAM_CONFIG_CSP_ADDR_SET(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CSP_ADDR, value, 0)
#define GS_CAM_CONFIG_CSP_ADDR_SET_NO_CALLBACK(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CSP_ADDR, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_CONFIG_CSP_ADDR_GET(value) gs_param_get_uint8(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CSP_ADDR, value, 0)
#define GS_CAM_CONFIG_CSP_ADDR_GET_NC() gs_param_get_uint8_nc(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CSP_ADDR, 0)
#define GS_CAM_CONFIG_CSP_GATEWAY_SET(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CSP_GATEWAY, value, 0)
#define GS_CAM_CONFIG_CSP_GATEWAY_SET_NO_CALLBACK(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CSP_GATEWAY, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_CONFIG_CSP_GATEWAY_GET(value) gs_param_get_uint8(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CSP_GATEWAY, value, 0)
#define GS_CAM_CONFIG_CSP_GATEWAY_GET_NC() gs_param_get_uint8_nc(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CSP_GATEWAY, 0)
#define GS_CAM_CONFIG_CSP_RTABLE_SET(value) gs_param_set_string(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CSP_RTABLE, value, 0)
#define GS_CAM_CONFIG_CSP_RTABLE_SET_NO_CALLBACK(value) gs_param_set_string(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CSP_RTABLE, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_CONFIG_CSP_RTABLE_GET(buf, buf_size) gs_param_get_string(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CSP_RTABLE, buf, buf_size, 0)
#define GS_CAM_CONFIG_CAN_BITRATE_SET(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CAN_BITRATE, value, 0)
#define GS_CAM_CONFIG_CAN_BITRATE_SET_NO_CALLBACK(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CAN_BITRATE, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_CONFIG_CAN_BITRATE_GET(value) gs_param_get_uint32(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CAN_BITRATE, value, 0)
#define GS_CAM_CONFIG_CAN_BITRATE_GET_NC() gs_param_get_uint32_nc(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_CAN_BITRATE, 0)
#define GS_CAM_CONFIG_I2C_BITRATE_SET(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_I2C_BITRATE, value, 0)
#define GS_CAM_CONFIG_I2C_BITRATE_SET_NO_CALLBACK(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_I2C_BITRATE, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_CONFIG_I2C_BITRATE_GET(value) gs_param_get_uint32(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_I2C_BITRATE, value, 0)
#define GS_CAM_CONFIG_I2C_BITRATE_GET_NC() gs_param_get_uint32_nc(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_I2C_BITRATE, 0)
#define GS_CAM_CONFIG_KISS_BITRATE_SET(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_KISS_BITRATE, value, 0)
#define GS_CAM_CONFIG_KISS_BITRATE_SET_NO_CALLBACK(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_KISS_BITRATE, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_CONFIG_KISS_BITRATE_GET(value) gs_param_get_uint32(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_KISS_BITRATE, value, 0)
#define GS_CAM_CONFIG_KISS_BITRATE_GET_NC() gs_param_get_uint32_nc(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_KISS_BITRATE, 0)
#define GS_CAM_CONFIG_KISS_IF_SET(value) gs_param_set_string(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_KISS_IF, value, 0)
#define GS_CAM_CONFIG_KISS_IF_SET_NO_CALLBACK(value) gs_param_set_string(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_KISS_IF, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_CONFIG_KISS_IF_GET(buf, buf_size) gs_param_get_string(gs_param_table_by_id(GS_CAM_CONFIG_TABLE_ID), GS_CAM_CONFIG_KISS_IF, buf, buf_size, 0)

#ifdef __cplusplus
}
#endif
#endif
