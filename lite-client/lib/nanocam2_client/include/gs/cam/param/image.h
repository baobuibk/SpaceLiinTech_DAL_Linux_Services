#ifndef GS_CAM_PARAM_IMAGE_H
#define GS_CAM_PARAM_IMAGE_H

/* Copyright (c) 2013 GomSpace A/S. All rights reserved. (AUTO GENERATED) */
#include <gs/param/table.h>

#ifdef __cplusplus
extern "C" {
#endif

// Table parameters address/offset
#define GS_CAM_IMAGE_EXPOSURE_US                                         0                // 0x0000 uint32
#define GS_CAM_IMAGE_HIST_SKIP                                           4                // 0x0004 uint8
#define GS_CAM_IMAGE_GAIN_TARGET                                         5                // 0x0005 uint8
#define GS_CAM_IMAGE_GAIN_SLACK                                          6                // 0x0006 uint8
#define GS_CAM_IMAGE_GAIN_ITERS                                          7                // 0x0007 uint8
#define GS_CAM_IMAGE_GAIN_GLOBAL                                         8                // 0x0008 uint16
#define GS_CAM_IMAGE_GAIN_RED                                           10                // 0x000a uint16
#define GS_CAM_IMAGE_GAIN_GREEN                                         12                // 0x000c uint16
#define GS_CAM_IMAGE_GAIN_BLUE                                          14                // 0x000e uint16
#define GS_CAM_IMAGE_GAIN_MAX                                           16                // 0x0010 uint16
#define GS_CAM_IMAGE_JPEG_QUAL                                          18                // 0x0012 uint8
#define GS_CAM_IMAGE_JPEG_PROG                                          19                // 0x0013 bool
#define GS_CAM_IMAGE_BAYER_ALGO                                         20                // 0x0014 uint8
#define GS_CAM_IMAGE_COLOR_CORRECT                                      21                // 0x0015 bool
#define GS_CAM_IMAGE_GAMMA_CORRECT                                      22                // 0x0016 bool
#define GS_CAM_IMAGE_WHITE_BALANCE                                      23                // 0x0017 bool
#define GS_CAM_IMAGE_GAMMA                                              24                // 0x0018 float
#define GS_CAM_IMAGE_GAMMA_BREAK                                        28                // 0x001c float
#define GS_CAM_IMAGE_CCM_RED(idx)                                   (   32 + ( 4 * idx))  // 0x0020 float
#define GS_CAM_IMAGE_CCM_RED_ARRAY_SIZE                                  3
#define GS_CAM_IMAGE_CCM_GREEN(idx)                                 (   44 + ( 4 * idx))  // 0x002c float
#define GS_CAM_IMAGE_CCM_GREEN_ARRAY_SIZE                                3
#define GS_CAM_IMAGE_CCM_BLUE(idx)                                  (   56 + ( 4 * idx))  // 0x0038 float
#define GS_CAM_IMAGE_CCM_BLUE_ARRAY_SIZE                                 3
#define GS_CAM_IMAGE_THUMB_SCALE                                        68                // 0x0044 uint8

#ifndef __cplusplus
static const gs_param_table_row_t gs_cam_image_table[] = {
    {.name = "exposure-us",   .addr = GS_CAM_IMAGE_EXPOSURE_US,                          .type = GS_PARAM_UINT32,  .size =   4, .array_size =  1, .flags = 0},
    {.name = "hist-skip",     .addr = GS_CAM_IMAGE_HIST_SKIP,                            .type = GS_PARAM_UINT8,   .size =   1, .array_size =  1, .flags = 0},
    {.name = "gain-target",   .addr = GS_CAM_IMAGE_GAIN_TARGET,                          .type = GS_PARAM_UINT8,   .size =   1, .array_size =  1, .flags = 0},
    {.name = "gain-slack",    .addr = GS_CAM_IMAGE_GAIN_SLACK,                           .type = GS_PARAM_UINT8,   .size =   1, .array_size =  1, .flags = 0},
    {.name = "gain-iters",    .addr = GS_CAM_IMAGE_GAIN_ITERS,                           .type = GS_PARAM_UINT8,   .size =   1, .array_size =  1, .flags = 0},
    {.name = "gain-global",   .addr = GS_CAM_IMAGE_GAIN_GLOBAL,                          .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = 0},
    {.name = "gain-red",      .addr = GS_CAM_IMAGE_GAIN_RED,                             .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = 0},
    {.name = "gain-green",    .addr = GS_CAM_IMAGE_GAIN_GREEN,                           .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = 0},
    {.name = "gain-blue",     .addr = GS_CAM_IMAGE_GAIN_BLUE,                            .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = 0},
    {.name = "gain-max",      .addr = GS_CAM_IMAGE_GAIN_MAX,                             .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = 0},
    {.name = "jpeg-qual",     .addr = GS_CAM_IMAGE_JPEG_QUAL,                            .type = GS_PARAM_UINT8,   .size =   1, .array_size =  1, .flags = 0},
    {.name = "jpeg-prog",     .addr = GS_CAM_IMAGE_JPEG_PROG,                            .type = GS_PARAM_BOOL,    .size =   1, .array_size =  1, .flags = 0},
    {.name = "bayer-algo",    .addr = GS_CAM_IMAGE_BAYER_ALGO,                           .type = GS_PARAM_UINT8,   .size =   1, .array_size =  1, .flags = 0},
    {.name = "color-correct", .addr = GS_CAM_IMAGE_COLOR_CORRECT,                        .type = GS_PARAM_BOOL,    .size =   1, .array_size =  1, .flags = 0},
    {.name = "gamma-correct", .addr = GS_CAM_IMAGE_GAMMA_CORRECT,                        .type = GS_PARAM_BOOL,    .size =   1, .array_size =  1, .flags = 0},
    {.name = "white-balance", .addr = GS_CAM_IMAGE_WHITE_BALANCE,                        .type = GS_PARAM_BOOL,    .size =   1, .array_size =  1, .flags = 0},
    {.name = "gamma",         .addr = GS_CAM_IMAGE_GAMMA,                                .type = GS_PARAM_FLOAT,   .size =   4, .array_size =  1, .flags = 0},
    {.name = "gamma-break",   .addr = GS_CAM_IMAGE_GAMMA_BREAK,                          .type = GS_PARAM_FLOAT,   .size =   4, .array_size =  1, .flags = 0},
    {.name = "ccm-red",       .addr = GS_CAM_IMAGE_CCM_RED(0),                           .type = GS_PARAM_FLOAT,   .size =   4, .array_size =  3, .flags = 0},
    {.name = "ccm-green",     .addr = GS_CAM_IMAGE_CCM_GREEN(0),                         .type = GS_PARAM_FLOAT,   .size =   4, .array_size =  3, .flags = 0},
    {.name = "ccm-blue",      .addr = GS_CAM_IMAGE_CCM_BLUE(0),                          .type = GS_PARAM_FLOAT,   .size =   4, .array_size =  3, .flags = 0},
    {.name = "thumb-scale",   .addr = GS_CAM_IMAGE_THUMB_SCALE,                          .type = GS_PARAM_UINT8,   .size =   1, .array_size =  1, .flags = 0},
};
#endif

#define GS_CAM_IMAGE_TABLE_NAME                          "image"
#define GS_CAM_IMAGE_TABLE_ID                            1
#define GS_CAM_IMAGE_TABLE_MEM_ID                        1 // legacy definition for table id
#define GS_CAM_IMAGE_TABLE_SIZE                          69 // bytes
#define GS_CAM_IMAGE_TABLE_ROWS                          (sizeof(gs_cam_image_table) / sizeof(gs_cam_image_table[0]))

#define GS_CAM_IMAGE_EXPOSURE_US_SET(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_EXPOSURE_US, value, 0)
#define GS_CAM_IMAGE_EXPOSURE_US_SET_NO_CALLBACK(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_EXPOSURE_US, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_EXPOSURE_US_GET(value) gs_param_get_uint32(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_EXPOSURE_US, value, 0)
#define GS_CAM_IMAGE_EXPOSURE_US_GET_NC() gs_param_get_uint32_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_EXPOSURE_US, 0)
#define GS_CAM_IMAGE_HIST_SKIP_SET(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_HIST_SKIP, value, 0)
#define GS_CAM_IMAGE_HIST_SKIP_SET_NO_CALLBACK(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_HIST_SKIP, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_HIST_SKIP_GET(value) gs_param_get_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_HIST_SKIP, value, 0)
#define GS_CAM_IMAGE_HIST_SKIP_GET_NC() gs_param_get_uint8_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_HIST_SKIP, 0)
#define GS_CAM_IMAGE_GAIN_TARGET_SET(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_TARGET, value, 0)
#define GS_CAM_IMAGE_GAIN_TARGET_SET_NO_CALLBACK(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_TARGET, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_GAIN_TARGET_GET(value) gs_param_get_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_TARGET, value, 0)
#define GS_CAM_IMAGE_GAIN_TARGET_GET_NC() gs_param_get_uint8_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_TARGET, 0)
#define GS_CAM_IMAGE_GAIN_SLACK_SET(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_SLACK, value, 0)
#define GS_CAM_IMAGE_GAIN_SLACK_SET_NO_CALLBACK(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_SLACK, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_GAIN_SLACK_GET(value) gs_param_get_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_SLACK, value, 0)
#define GS_CAM_IMAGE_GAIN_SLACK_GET_NC() gs_param_get_uint8_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_SLACK, 0)
#define GS_CAM_IMAGE_GAIN_ITERS_SET(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_ITERS, value, 0)
#define GS_CAM_IMAGE_GAIN_ITERS_SET_NO_CALLBACK(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_ITERS, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_GAIN_ITERS_GET(value) gs_param_get_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_ITERS, value, 0)
#define GS_CAM_IMAGE_GAIN_ITERS_GET_NC() gs_param_get_uint8_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_ITERS, 0)
#define GS_CAM_IMAGE_GAIN_GLOBAL_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_GLOBAL, value, 0)
#define GS_CAM_IMAGE_GAIN_GLOBAL_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_GLOBAL, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_GAIN_GLOBAL_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_GLOBAL, value, 0)
#define GS_CAM_IMAGE_GAIN_GLOBAL_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_GLOBAL, 0)
#define GS_CAM_IMAGE_GAIN_RED_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_RED, value, 0)
#define GS_CAM_IMAGE_GAIN_RED_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_RED, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_GAIN_RED_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_RED, value, 0)
#define GS_CAM_IMAGE_GAIN_RED_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_RED, 0)
#define GS_CAM_IMAGE_GAIN_GREEN_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_GREEN, value, 0)
#define GS_CAM_IMAGE_GAIN_GREEN_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_GREEN, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_GAIN_GREEN_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_GREEN, value, 0)
#define GS_CAM_IMAGE_GAIN_GREEN_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_GREEN, 0)
#define GS_CAM_IMAGE_GAIN_BLUE_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_BLUE, value, 0)
#define GS_CAM_IMAGE_GAIN_BLUE_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_BLUE, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_GAIN_BLUE_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_BLUE, value, 0)
#define GS_CAM_IMAGE_GAIN_BLUE_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_BLUE, 0)
#define GS_CAM_IMAGE_GAIN_MAX_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_MAX, value, 0)
#define GS_CAM_IMAGE_GAIN_MAX_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_MAX, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_GAIN_MAX_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_MAX, value, 0)
#define GS_CAM_IMAGE_GAIN_MAX_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAIN_MAX, 0)
#define GS_CAM_IMAGE_JPEG_QUAL_SET(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_JPEG_QUAL, value, 0)
#define GS_CAM_IMAGE_JPEG_QUAL_SET_NO_CALLBACK(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_JPEG_QUAL, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_JPEG_QUAL_GET(value) gs_param_get_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_JPEG_QUAL, value, 0)
#define GS_CAM_IMAGE_JPEG_QUAL_GET_NC() gs_param_get_uint8_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_JPEG_QUAL, 0)
#define GS_CAM_IMAGE_JPEG_PROG_SET(value) gs_param_set_bool(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_JPEG_PROG, value, 0)
#define GS_CAM_IMAGE_JPEG_PROG_SET_NO_CALLBACK(value) gs_param_set_bool(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_JPEG_PROG, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_JPEG_PROG_GET(value) gs_param_get_bool(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_JPEG_PROG, value, 0)
#define GS_CAM_IMAGE_JPEG_PROG_GET_NC() gs_param_get_bool_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_JPEG_PROG, 0)
#define GS_CAM_IMAGE_BAYER_ALGO_SET(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_BAYER_ALGO, value, 0)
#define GS_CAM_IMAGE_BAYER_ALGO_SET_NO_CALLBACK(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_BAYER_ALGO, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_BAYER_ALGO_GET(value) gs_param_get_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_BAYER_ALGO, value, 0)
#define GS_CAM_IMAGE_BAYER_ALGO_GET_NC() gs_param_get_uint8_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_BAYER_ALGO, 0)
#define GS_CAM_IMAGE_COLOR_CORRECT_SET(value) gs_param_set_bool(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_COLOR_CORRECT, value, 0)
#define GS_CAM_IMAGE_COLOR_CORRECT_SET_NO_CALLBACK(value) gs_param_set_bool(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_COLOR_CORRECT, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_COLOR_CORRECT_GET(value) gs_param_get_bool(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_COLOR_CORRECT, value, 0)
#define GS_CAM_IMAGE_COLOR_CORRECT_GET_NC() gs_param_get_bool_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_COLOR_CORRECT, 0)
#define GS_CAM_IMAGE_GAMMA_CORRECT_SET(value) gs_param_set_bool(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAMMA_CORRECT, value, 0)
#define GS_CAM_IMAGE_GAMMA_CORRECT_SET_NO_CALLBACK(value) gs_param_set_bool(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAMMA_CORRECT, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_GAMMA_CORRECT_GET(value) gs_param_get_bool(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAMMA_CORRECT, value, 0)
#define GS_CAM_IMAGE_GAMMA_CORRECT_GET_NC() gs_param_get_bool_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAMMA_CORRECT, 0)
#define GS_CAM_IMAGE_WHITE_BALANCE_SET(value) gs_param_set_bool(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_WHITE_BALANCE, value, 0)
#define GS_CAM_IMAGE_WHITE_BALANCE_SET_NO_CALLBACK(value) gs_param_set_bool(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_WHITE_BALANCE, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_WHITE_BALANCE_GET(value) gs_param_get_bool(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_WHITE_BALANCE, value, 0)
#define GS_CAM_IMAGE_WHITE_BALANCE_GET_NC() gs_param_get_bool_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_WHITE_BALANCE, 0)
#define GS_CAM_IMAGE_GAMMA_SET(value) gs_param_set_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAMMA, value, 0)
#define GS_CAM_IMAGE_GAMMA_SET_NO_CALLBACK(value) gs_param_set_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAMMA, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_GAMMA_GET(value) gs_param_get_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAMMA, value, 0)
#define GS_CAM_IMAGE_GAMMA_GET_NC() gs_param_get_float_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAMMA, 0)
#define GS_CAM_IMAGE_GAMMA_BREAK_SET(value) gs_param_set_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAMMA_BREAK, value, 0)
#define GS_CAM_IMAGE_GAMMA_BREAK_SET_NO_CALLBACK(value) gs_param_set_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAMMA_BREAK, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_GAMMA_BREAK_GET(value) gs_param_get_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAMMA_BREAK, value, 0)
#define GS_CAM_IMAGE_GAMMA_BREAK_GET_NC() gs_param_get_float_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_GAMMA_BREAK, 0)
#define GS_CAM_IMAGE_CCM_RED_SET(idx, value) gs_param_set_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_CCM_RED(idx), value, 0)
#define GS_CAM_IMAGE_CCM_RED_SET_NO_CALLBACK(idx, value) gs_param_set_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_CCM_RED(idx), value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_CCM_RED_GET(idx, value) gs_param_get_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_CCM_RED(idx), value, 0)
#define GS_CAM_IMAGE_CCM_RED_GET_NC(idx) gs_param_get_float_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_CCM_RED(idx), 0)
#define GS_CAM_IMAGE_CCM_GREEN_SET(idx, value) gs_param_set_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_CCM_GREEN(idx), value, 0)
#define GS_CAM_IMAGE_CCM_GREEN_SET_NO_CALLBACK(idx, value) gs_param_set_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_CCM_GREEN(idx), value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_CCM_GREEN_GET(idx, value) gs_param_get_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_CCM_GREEN(idx), value, 0)
#define GS_CAM_IMAGE_CCM_GREEN_GET_NC(idx) gs_param_get_float_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_CCM_GREEN(idx), 0)
#define GS_CAM_IMAGE_CCM_BLUE_SET(idx, value) gs_param_set_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_CCM_BLUE(idx), value, 0)
#define GS_CAM_IMAGE_CCM_BLUE_SET_NO_CALLBACK(idx, value) gs_param_set_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_CCM_BLUE(idx), value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_CCM_BLUE_GET(idx, value) gs_param_get_float(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_CCM_BLUE(idx), value, 0)
#define GS_CAM_IMAGE_CCM_BLUE_GET_NC(idx) gs_param_get_float_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_CCM_BLUE(idx), 0)
#define GS_CAM_IMAGE_THUMB_SCALE_SET(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_THUMB_SCALE, value, 0)
#define GS_CAM_IMAGE_THUMB_SCALE_SET_NO_CALLBACK(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_THUMB_SCALE, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_IMAGE_THUMB_SCALE_GET(value) gs_param_get_uint8(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_THUMB_SCALE, value, 0)
#define GS_CAM_IMAGE_THUMB_SCALE_GET_NC() gs_param_get_uint8_nc(gs_param_table_by_id(GS_CAM_IMAGE_TABLE_ID), GS_CAM_IMAGE_THUMB_SCALE, 0)

#ifdef __cplusplus
}
#endif
#endif
