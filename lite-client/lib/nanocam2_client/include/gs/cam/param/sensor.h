#ifndef GS_CAM_PARAM_SENSOR_H
#define GS_CAM_PARAM_SENSOR_H

/* Copyright (c) 2013 GomSpace A/S. All rights reserved. (AUTO GENERATED) */
#include <gs/param/table.h>

#ifdef __cplusplus
extern "C" {
#endif

// Table parameters address/offset
#define GS_CAM_SENSOR_CHIP_VERSION                                       0                // 0x0000 uint16
#define GS_CAM_SENSOR_ROW_START                                          2                // 0x0002 uint16
#define GS_CAM_SENSOR_COL_START                                          4                // 0x0004 uint16
#define GS_CAM_SENSOR_ROW_SIZE                                           6                // 0x0006 uint16
#define GS_CAM_SENSOR_COL_SIZE                                           8                // 0x0008 uint16
#define GS_CAM_SENSOR_HORZ_BLANK                                        10                // 0x000a uint16
#define GS_CAM_SENSOR_VERT_BLANK                                        12                // 0x000c uint16
#define GS_CAM_SENSOR_OUTPUT_CTRL                                       14                // 0x000e uint16
#define GS_CAM_SENSOR_SH_WIDTH_UP                                       16                // 0x0010 uint16
#define GS_CAM_SENSOR_SH_WIDTH                                          18                // 0x0012 uint16
#define GS_CAM_SENSOR_PIXCLK_CTRL                                       20                // 0x0014 uint16
#define GS_CAM_SENSOR_FRAME_RST                                         22                // 0x0016 uint16
#define GS_CAM_SENSOR_SHUTTER_DELAY                                     24                // 0x0018 uint16
#define GS_CAM_SENSOR_RESET                                             26                // 0x001a uint16
#define GS_CAM_SENSOR_READ_MODE1                                        28                // 0x001c uint16
#define GS_CAM_SENSOR_READ_MODE2                                        30                // 0x001e uint16
#define GS_CAM_SENSOR_READ_MODE3                                        32                // 0x0020 uint16
#define GS_CAM_SENSOR_ROW_ADDR_MODE                                     34                // 0x0022 uint16
#define GS_CAM_SENSOR_COL_ADDR_MODE                                     36                // 0x0024 uint16
#define GS_CAM_SENSOR_GREEN1_GAIN                                       38                // 0x0026 uint16
#define GS_CAM_SENSOR_BLUE_GAIN                                         40                // 0x0028 uint16
#define GS_CAM_SENSOR_RED_GAIN                                          42                // 0x002a uint16
#define GS_CAM_SENSOR_GREEN2_GAIN                                       44                // 0x002c uint16
#define GS_CAM_SENSOR_TESTDATA                                          46                // 0x002e uint16
#define GS_CAM_SENSOR_GLOBAL_GAIN                                       48                // 0x0030 uint16
#define GS_CAM_SENSOR_BLACK_LEVEL                                       50                // 0x0032 uint16
#define GS_CAM_SENSOR_ROW_BLK_OFF                                       52                // 0x0034 uint16
#define GS_CAM_SENSOR_BLC_DELTA                                         54                // 0x0036 uint16
#define GS_CAM_SENSOR_CAL_THRES                                         56                // 0x0038 uint16
#define GS_CAM_SENSOR_GREEN1_OFFSET                                     58                // 0x003a uint16
#define GS_CAM_SENSOR_GREEN2_OFFSET                                     60                // 0x003c uint16
#define GS_CAM_SENSOR_BLC                                               62                // 0x003e uint16
#define GS_CAM_SENSOR_RED_OFFSET                                        64                // 0x0040 uint16
#define GS_CAM_SENSOR_BLUE_OFFSET                                       66                // 0x0042 uint16
#define GS_CAM_SENSOR_CHIP_ENABLE                                       68                // 0x0044 uint16
#define GS_CAM_SENSOR_CHIP_VERSION2                                     70                // 0x0046 uint16

#ifndef __cplusplus
static const gs_param_table_row_t gs_cam_sensor_table[] = {
    {.name = "chip-version",  .addr = GS_CAM_SENSOR_CHIP_VERSION,                        .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "row-start",     .addr = GS_CAM_SENSOR_ROW_START,                           .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "col-start",     .addr = GS_CAM_SENSOR_COL_START,                           .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "row-size",      .addr = GS_CAM_SENSOR_ROW_SIZE,                            .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "col-size",      .addr = GS_CAM_SENSOR_COL_SIZE,                            .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "horz-blank",    .addr = GS_CAM_SENSOR_HORZ_BLANK,                          .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "vert-blank",    .addr = GS_CAM_SENSOR_VERT_BLANK,                          .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "output-ctrl",   .addr = GS_CAM_SENSOR_OUTPUT_CTRL,                         .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "sh-width-up",   .addr = GS_CAM_SENSOR_SH_WIDTH_UP,                         .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "sh-width",      .addr = GS_CAM_SENSOR_SH_WIDTH,                            .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "pixclk-ctrl",   .addr = GS_CAM_SENSOR_PIXCLK_CTRL,                         .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "frame-rst",     .addr = GS_CAM_SENSOR_FRAME_RST,                           .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "shutter-delay", .addr = GS_CAM_SENSOR_SHUTTER_DELAY,                       .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "reset",         .addr = GS_CAM_SENSOR_RESET,                               .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "read-mode1",    .addr = GS_CAM_SENSOR_READ_MODE1,                          .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "read-mode2",    .addr = GS_CAM_SENSOR_READ_MODE2,                          .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "read-mode3",    .addr = GS_CAM_SENSOR_READ_MODE3,                          .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "row-addr-mode", .addr = GS_CAM_SENSOR_ROW_ADDR_MODE,                       .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "col-addr-mode", .addr = GS_CAM_SENSOR_COL_ADDR_MODE,                       .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "green1-gain",   .addr = GS_CAM_SENSOR_GREEN1_GAIN,                         .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "blue-gain",     .addr = GS_CAM_SENSOR_BLUE_GAIN,                           .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "red-gain",      .addr = GS_CAM_SENSOR_RED_GAIN,                            .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "green2-gain",   .addr = GS_CAM_SENSOR_GREEN2_GAIN,                         .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "testdata",      .addr = GS_CAM_SENSOR_TESTDATA,                            .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "global-gain",   .addr = GS_CAM_SENSOR_GLOBAL_GAIN,                         .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "black-level",   .addr = GS_CAM_SENSOR_BLACK_LEVEL,                         .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "row-blk-off",   .addr = GS_CAM_SENSOR_ROW_BLK_OFF,                         .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "blc-delta",     .addr = GS_CAM_SENSOR_BLC_DELTA,                           .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "cal-thres",     .addr = GS_CAM_SENSOR_CAL_THRES,                           .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "green1-offset", .addr = GS_CAM_SENSOR_GREEN1_OFFSET,                       .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "green2-offset", .addr = GS_CAM_SENSOR_GREEN2_OFFSET,                       .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "blc",           .addr = GS_CAM_SENSOR_BLC,                                 .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "red-offset",    .addr = GS_CAM_SENSOR_RED_OFFSET,                          .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "blue-offset",   .addr = GS_CAM_SENSOR_BLUE_OFFSET,                         .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "chip-enable",   .addr = GS_CAM_SENSOR_CHIP_ENABLE,                         .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
    {.name = "chip-version2", .addr = GS_CAM_SENSOR_CHIP_VERSION2,                       .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = GS_PARAM_F_SHOW_HEX},
};
#endif

#define GS_CAM_SENSOR_TABLE_NAME                         "sensor"
#define GS_CAM_SENSOR_TABLE_ID                           2
#define GS_CAM_SENSOR_TABLE_MEM_ID                       2 // legacy definition for table id
#define GS_CAM_SENSOR_TABLE_SIZE                         72 // bytes
#define GS_CAM_SENSOR_TABLE_ROWS                         (sizeof(gs_cam_sensor_table) / sizeof(gs_cam_sensor_table[0]))

#define GS_CAM_SENSOR_CHIP_VERSION_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CHIP_VERSION, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_CHIP_VERSION_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CHIP_VERSION, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_CHIP_VERSION_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CHIP_VERSION, value, 0)
#define GS_CAM_SENSOR_CHIP_VERSION_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CHIP_VERSION, 0)
#define GS_CAM_SENSOR_ROW_START_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_START, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_ROW_START_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_START, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_ROW_START_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_START, value, 0)
#define GS_CAM_SENSOR_ROW_START_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_START, 0)
#define GS_CAM_SENSOR_COL_START_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_COL_START, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_COL_START_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_COL_START, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_COL_START_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_COL_START, value, 0)
#define GS_CAM_SENSOR_COL_START_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_COL_START, 0)
#define GS_CAM_SENSOR_ROW_SIZE_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_SIZE, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_ROW_SIZE_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_SIZE, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_ROW_SIZE_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_SIZE, value, 0)
#define GS_CAM_SENSOR_ROW_SIZE_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_SIZE, 0)
#define GS_CAM_SENSOR_COL_SIZE_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_COL_SIZE, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_COL_SIZE_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_COL_SIZE, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_COL_SIZE_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_COL_SIZE, value, 0)
#define GS_CAM_SENSOR_COL_SIZE_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_COL_SIZE, 0)
#define GS_CAM_SENSOR_HORZ_BLANK_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_HORZ_BLANK, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_HORZ_BLANK_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_HORZ_BLANK, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_HORZ_BLANK_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_HORZ_BLANK, value, 0)
#define GS_CAM_SENSOR_HORZ_BLANK_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_HORZ_BLANK, 0)
#define GS_CAM_SENSOR_VERT_BLANK_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_VERT_BLANK, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_VERT_BLANK_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_VERT_BLANK, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_VERT_BLANK_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_VERT_BLANK, value, 0)
#define GS_CAM_SENSOR_VERT_BLANK_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_VERT_BLANK, 0)
#define GS_CAM_SENSOR_OUTPUT_CTRL_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_OUTPUT_CTRL, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_OUTPUT_CTRL_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_OUTPUT_CTRL, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_OUTPUT_CTRL_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_OUTPUT_CTRL, value, 0)
#define GS_CAM_SENSOR_OUTPUT_CTRL_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_OUTPUT_CTRL, 0)
#define GS_CAM_SENSOR_SH_WIDTH_UP_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_SH_WIDTH_UP, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_SH_WIDTH_UP_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_SH_WIDTH_UP, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_SH_WIDTH_UP_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_SH_WIDTH_UP, value, 0)
#define GS_CAM_SENSOR_SH_WIDTH_UP_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_SH_WIDTH_UP, 0)
#define GS_CAM_SENSOR_SH_WIDTH_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_SH_WIDTH, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_SH_WIDTH_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_SH_WIDTH, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_SH_WIDTH_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_SH_WIDTH, value, 0)
#define GS_CAM_SENSOR_SH_WIDTH_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_SH_WIDTH, 0)
#define GS_CAM_SENSOR_PIXCLK_CTRL_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_PIXCLK_CTRL, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_PIXCLK_CTRL_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_PIXCLK_CTRL, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_PIXCLK_CTRL_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_PIXCLK_CTRL, value, 0)
#define GS_CAM_SENSOR_PIXCLK_CTRL_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_PIXCLK_CTRL, 0)
#define GS_CAM_SENSOR_FRAME_RST_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_FRAME_RST, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_FRAME_RST_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_FRAME_RST, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_FRAME_RST_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_FRAME_RST, value, 0)
#define GS_CAM_SENSOR_FRAME_RST_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_FRAME_RST, 0)
#define GS_CAM_SENSOR_SHUTTER_DELAY_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_SHUTTER_DELAY, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_SHUTTER_DELAY_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_SHUTTER_DELAY, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_SHUTTER_DELAY_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_SHUTTER_DELAY, value, 0)
#define GS_CAM_SENSOR_SHUTTER_DELAY_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_SHUTTER_DELAY, 0)
#define GS_CAM_SENSOR_RESET_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_RESET, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_RESET_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_RESET, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_RESET_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_RESET, value, 0)
#define GS_CAM_SENSOR_RESET_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_RESET, 0)
#define GS_CAM_SENSOR_READ_MODE1_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_READ_MODE1, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_READ_MODE1_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_READ_MODE1, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_READ_MODE1_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_READ_MODE1, value, 0)
#define GS_CAM_SENSOR_READ_MODE1_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_READ_MODE1, 0)
#define GS_CAM_SENSOR_READ_MODE2_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_READ_MODE2, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_READ_MODE2_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_READ_MODE2, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_READ_MODE2_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_READ_MODE2, value, 0)
#define GS_CAM_SENSOR_READ_MODE2_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_READ_MODE2, 0)
#define GS_CAM_SENSOR_READ_MODE3_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_READ_MODE3, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_READ_MODE3_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_READ_MODE3, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_READ_MODE3_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_READ_MODE3, value, 0)
#define GS_CAM_SENSOR_READ_MODE3_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_READ_MODE3, 0)
#define GS_CAM_SENSOR_ROW_ADDR_MODE_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_ADDR_MODE, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_ROW_ADDR_MODE_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_ADDR_MODE, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_ROW_ADDR_MODE_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_ADDR_MODE, value, 0)
#define GS_CAM_SENSOR_ROW_ADDR_MODE_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_ADDR_MODE, 0)
#define GS_CAM_SENSOR_COL_ADDR_MODE_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_COL_ADDR_MODE, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_COL_ADDR_MODE_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_COL_ADDR_MODE, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_COL_ADDR_MODE_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_COL_ADDR_MODE, value, 0)
#define GS_CAM_SENSOR_COL_ADDR_MODE_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_COL_ADDR_MODE, 0)
#define GS_CAM_SENSOR_GREEN1_GAIN_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN1_GAIN, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_GREEN1_GAIN_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN1_GAIN, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_GREEN1_GAIN_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN1_GAIN, value, 0)
#define GS_CAM_SENSOR_GREEN1_GAIN_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN1_GAIN, 0)
#define GS_CAM_SENSOR_BLUE_GAIN_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLUE_GAIN, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_BLUE_GAIN_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLUE_GAIN, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_BLUE_GAIN_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLUE_GAIN, value, 0)
#define GS_CAM_SENSOR_BLUE_GAIN_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLUE_GAIN, 0)
#define GS_CAM_SENSOR_RED_GAIN_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_RED_GAIN, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_RED_GAIN_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_RED_GAIN, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_RED_GAIN_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_RED_GAIN, value, 0)
#define GS_CAM_SENSOR_RED_GAIN_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_RED_GAIN, 0)
#define GS_CAM_SENSOR_GREEN2_GAIN_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN2_GAIN, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_GREEN2_GAIN_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN2_GAIN, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_GREEN2_GAIN_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN2_GAIN, value, 0)
#define GS_CAM_SENSOR_GREEN2_GAIN_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN2_GAIN, 0)
#define GS_CAM_SENSOR_TESTDATA_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_TESTDATA, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_TESTDATA_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_TESTDATA, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_TESTDATA_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_TESTDATA, value, 0)
#define GS_CAM_SENSOR_TESTDATA_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_TESTDATA, 0)
#define GS_CAM_SENSOR_GLOBAL_GAIN_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GLOBAL_GAIN, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_GLOBAL_GAIN_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GLOBAL_GAIN, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_GLOBAL_GAIN_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GLOBAL_GAIN, value, 0)
#define GS_CAM_SENSOR_GLOBAL_GAIN_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GLOBAL_GAIN, 0)
#define GS_CAM_SENSOR_BLACK_LEVEL_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLACK_LEVEL, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_BLACK_LEVEL_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLACK_LEVEL, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_BLACK_LEVEL_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLACK_LEVEL, value, 0)
#define GS_CAM_SENSOR_BLACK_LEVEL_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLACK_LEVEL, 0)
#define GS_CAM_SENSOR_ROW_BLK_OFF_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_BLK_OFF, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_ROW_BLK_OFF_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_BLK_OFF, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_ROW_BLK_OFF_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_BLK_OFF, value, 0)
#define GS_CAM_SENSOR_ROW_BLK_OFF_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_ROW_BLK_OFF, 0)
#define GS_CAM_SENSOR_BLC_DELTA_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLC_DELTA, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_BLC_DELTA_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLC_DELTA, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_BLC_DELTA_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLC_DELTA, value, 0)
#define GS_CAM_SENSOR_BLC_DELTA_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLC_DELTA, 0)
#define GS_CAM_SENSOR_CAL_THRES_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CAL_THRES, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_CAL_THRES_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CAL_THRES, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_CAL_THRES_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CAL_THRES, value, 0)
#define GS_CAM_SENSOR_CAL_THRES_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CAL_THRES, 0)
#define GS_CAM_SENSOR_GREEN1_OFFSET_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN1_OFFSET, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_GREEN1_OFFSET_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN1_OFFSET, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_GREEN1_OFFSET_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN1_OFFSET, value, 0)
#define GS_CAM_SENSOR_GREEN1_OFFSET_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN1_OFFSET, 0)
#define GS_CAM_SENSOR_GREEN2_OFFSET_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN2_OFFSET, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_GREEN2_OFFSET_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN2_OFFSET, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_GREEN2_OFFSET_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN2_OFFSET, value, 0)
#define GS_CAM_SENSOR_GREEN2_OFFSET_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_GREEN2_OFFSET, 0)
#define GS_CAM_SENSOR_BLC_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLC, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_BLC_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLC, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_BLC_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLC, value, 0)
#define GS_CAM_SENSOR_BLC_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLC, 0)
#define GS_CAM_SENSOR_RED_OFFSET_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_RED_OFFSET, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_RED_OFFSET_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_RED_OFFSET, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_RED_OFFSET_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_RED_OFFSET, value, 0)
#define GS_CAM_SENSOR_RED_OFFSET_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_RED_OFFSET, 0)
#define GS_CAM_SENSOR_BLUE_OFFSET_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLUE_OFFSET, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_BLUE_OFFSET_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLUE_OFFSET, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_BLUE_OFFSET_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLUE_OFFSET, value, 0)
#define GS_CAM_SENSOR_BLUE_OFFSET_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_BLUE_OFFSET, 0)
#define GS_CAM_SENSOR_CHIP_ENABLE_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CHIP_ENABLE, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_CHIP_ENABLE_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CHIP_ENABLE, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_CHIP_ENABLE_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CHIP_ENABLE, value, 0)
#define GS_CAM_SENSOR_CHIP_ENABLE_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CHIP_ENABLE, 0)
#define GS_CAM_SENSOR_CHIP_VERSION2_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CHIP_VERSION2, value, GS_PARAM_F_SHOW_HEX)
#define GS_CAM_SENSOR_CHIP_VERSION2_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CHIP_VERSION2, value, GS_PARAM_F_SHOW_HEX | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_SENSOR_CHIP_VERSION2_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CHIP_VERSION2, value, 0)
#define GS_CAM_SENSOR_CHIP_VERSION2_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_SENSOR_TABLE_ID), GS_CAM_SENSOR_CHIP_VERSION2, 0)

#ifdef __cplusplus
}
#endif
#endif
