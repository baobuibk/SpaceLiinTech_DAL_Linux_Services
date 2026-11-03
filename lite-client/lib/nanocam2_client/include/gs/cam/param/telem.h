#ifndef GS_CAM_PARAM_TELEM_H
#define GS_CAM_PARAM_TELEM_H

/* Copyright (c) 2013 GomSpace A/S. All rights reserved. (AUTO GENERATED) */
#include <gs/param/table.h>

#ifdef __cplusplus
extern "C" {
#endif

// Table parameters address/offset
#define GS_CAM_TELEM_BOOT_COUNT                                          0                // 0x0000 uint32
#define GS_CAM_TELEM_IMAGE_COUNT                                         4                // 0x0004 uint32
#define GS_CAM_TELEM_UNIXTIME                                            8                // 0x0008 uint64
#define GS_CAM_TELEM_UPTIME                                             16                // 0x0010 uint32
#define GS_CAM_TELEM_LOADS(idx)                                     (   20 + ( 1 * idx))  // 0x0014 uint8
#define GS_CAM_TELEM_LOADS_ARRAY_SIZE                                    3
#define GS_CAM_TELEM_FREERAM                                            24                // 0x0018 uint32
#define GS_CAM_TELEM_PROCS                                              28                // 0x001c uint16
#define GS_CAM_TELEM_TEMP1                                              30                // 0x001e int16
#define GS_CAM_TELEM_TEMP2                                              32                // 0x0020 int16
#define GS_CAM_TELEM_ICORE                                              34                // 0x0022 uint16
#define GS_CAM_TELEM_IDDR                                               36                // 0x0024 uint16
#define GS_CAM_TELEM_IVCC                                               38                // 0x0026 uint16
#define GS_CAM_TELEM_VDDCORE                                            40                // 0x0028 uint16
#define GS_CAM_TELEM_VDDIODDR                                           42                // 0x002a uint16
#define GS_CAM_TELEM_VCC                                                44                // 0x002c uint16

#ifndef __cplusplus
static const gs_param_table_row_t gs_cam_telem_table[] = {
    {.name = "boot-count",    .addr = GS_CAM_TELEM_BOOT_COUNT,                           .type = GS_PARAM_UINT32,  .size =   4, .array_size =  1, .flags = GS_PARAM_F_AUTO_PERSIST},
    {.name = "image-count",   .addr = GS_CAM_TELEM_IMAGE_COUNT,                          .type = GS_PARAM_UINT32,  .size =   4, .array_size =  1, .flags = GS_PARAM_F_AUTO_PERSIST},
    {.name = "unixtime",      .addr = GS_CAM_TELEM_UNIXTIME,                             .type = GS_PARAM_UINT64,  .size =   8, .array_size =  1, .flags = 0},
    {.name = "uptime",        .addr = GS_CAM_TELEM_UPTIME,                               .type = GS_PARAM_UINT32,  .size =   4, .array_size =  1, .flags = 0},
    {.name = "loads",         .addr = GS_CAM_TELEM_LOADS(0),                             .type = GS_PARAM_UINT8,   .size =   1, .array_size =  3, .flags = 0},
    {.name = "freeram",       .addr = GS_CAM_TELEM_FREERAM,                              .type = GS_PARAM_UINT32,  .size =   4, .array_size =  1, .flags = 0},
    {.name = "procs",         .addr = GS_CAM_TELEM_PROCS,                                .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = 0},
    {.name = "temp1",         .addr = GS_CAM_TELEM_TEMP1,                                .type = GS_PARAM_INT16,   .size =   2, .array_size =  1, .flags = 0},
    {.name = "temp2",         .addr = GS_CAM_TELEM_TEMP2,                                .type = GS_PARAM_INT16,   .size =   2, .array_size =  1, .flags = 0},
    {.name = "icore",         .addr = GS_CAM_TELEM_ICORE,                                .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = 0},
    {.name = "iddr",          .addr = GS_CAM_TELEM_IDDR,                                 .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = 0},
    {.name = "ivcc",          .addr = GS_CAM_TELEM_IVCC,                                 .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = 0},
    {.name = "vddcore",       .addr = GS_CAM_TELEM_VDDCORE,                              .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = 0},
    {.name = "vddioddr",      .addr = GS_CAM_TELEM_VDDIODDR,                             .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = 0},
    {.name = "vcc",           .addr = GS_CAM_TELEM_VCC,                                  .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = 0},
};
#endif

#define GS_CAM_TELEM_TABLE_NAME                          "telem"
#define GS_CAM_TELEM_TABLE_ID                            4
#define GS_CAM_TELEM_TABLE_MEM_ID                        4 // legacy definition for table id
#define GS_CAM_TELEM_TABLE_SIZE                          46 // bytes
#define GS_CAM_TELEM_TABLE_ROWS                          (sizeof(gs_cam_telem_table) / sizeof(gs_cam_telem_table[0]))

#define GS_CAM_TELEM_BOOT_COUNT_SET(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_BOOT_COUNT, value, GS_PARAM_F_AUTO_PERSIST)
#define GS_CAM_TELEM_BOOT_COUNT_SET_NO_CALLBACK(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_BOOT_COUNT, value, GS_PARAM_F_AUTO_PERSIST | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_BOOT_COUNT_GET(value) gs_param_get_uint32(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_BOOT_COUNT, value, 0)
#define GS_CAM_TELEM_BOOT_COUNT_GET_NC() gs_param_get_uint32_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_BOOT_COUNT, 0)
#define GS_CAM_TELEM_IMAGE_COUNT_SET(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_IMAGE_COUNT, value, GS_PARAM_F_AUTO_PERSIST)
#define GS_CAM_TELEM_IMAGE_COUNT_SET_NO_CALLBACK(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_IMAGE_COUNT, value, GS_PARAM_F_AUTO_PERSIST | GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_IMAGE_COUNT_GET(value) gs_param_get_uint32(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_IMAGE_COUNT, value, 0)
#define GS_CAM_TELEM_IMAGE_COUNT_GET_NC() gs_param_get_uint32_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_IMAGE_COUNT, 0)
#define GS_CAM_TELEM_UNIXTIME_SET(value) gs_param_set_uint64(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_UNIXTIME, value, 0)
#define GS_CAM_TELEM_UNIXTIME_SET_NO_CALLBACK(value) gs_param_set_uint64(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_UNIXTIME, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_UNIXTIME_GET(value) gs_param_get_uint64(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_UNIXTIME, value, 0)
#define GS_CAM_TELEM_UNIXTIME_GET_NC() gs_param_get_uint64_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_UNIXTIME, 0)
#define GS_CAM_TELEM_UPTIME_SET(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_UPTIME, value, 0)
#define GS_CAM_TELEM_UPTIME_SET_NO_CALLBACK(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_UPTIME, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_UPTIME_GET(value) gs_param_get_uint32(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_UPTIME, value, 0)
#define GS_CAM_TELEM_UPTIME_GET_NC() gs_param_get_uint32_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_UPTIME, 0)
#define GS_CAM_TELEM_LOADS_SET(idx, value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_LOADS(idx), value, 0)
#define GS_CAM_TELEM_LOADS_SET_NO_CALLBACK(idx, value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_LOADS(idx), value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_LOADS_GET(idx, value) gs_param_get_uint8(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_LOADS(idx), value, 0)
#define GS_CAM_TELEM_LOADS_GET_NC(idx) gs_param_get_uint8_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_LOADS(idx), 0)
#define GS_CAM_TELEM_FREERAM_SET(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_FREERAM, value, 0)
#define GS_CAM_TELEM_FREERAM_SET_NO_CALLBACK(value) gs_param_set_uint32(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_FREERAM, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_FREERAM_GET(value) gs_param_get_uint32(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_FREERAM, value, 0)
#define GS_CAM_TELEM_FREERAM_GET_NC() gs_param_get_uint32_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_FREERAM, 0)
#define GS_CAM_TELEM_PROCS_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_PROCS, value, 0)
#define GS_CAM_TELEM_PROCS_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_PROCS, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_PROCS_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_PROCS, value, 0)
#define GS_CAM_TELEM_PROCS_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_PROCS, 0)
#define GS_CAM_TELEM_TEMP1_SET(value) gs_param_set_int16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_TEMP1, value, 0)
#define GS_CAM_TELEM_TEMP1_SET_NO_CALLBACK(value) gs_param_set_int16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_TEMP1, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_TEMP1_GET(value) gs_param_get_int16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_TEMP1, value, 0)
#define GS_CAM_TELEM_TEMP1_GET_NC() gs_param_get_int16_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_TEMP1, 0)
#define GS_CAM_TELEM_TEMP2_SET(value) gs_param_set_int16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_TEMP2, value, 0)
#define GS_CAM_TELEM_TEMP2_SET_NO_CALLBACK(value) gs_param_set_int16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_TEMP2, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_TEMP2_GET(value) gs_param_get_int16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_TEMP2, value, 0)
#define GS_CAM_TELEM_TEMP2_GET_NC() gs_param_get_int16_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_TEMP2, 0)
#define GS_CAM_TELEM_ICORE_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_ICORE, value, 0)
#define GS_CAM_TELEM_ICORE_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_ICORE, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_ICORE_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_ICORE, value, 0)
#define GS_CAM_TELEM_ICORE_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_ICORE, 0)
#define GS_CAM_TELEM_IDDR_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_IDDR, value, 0)
#define GS_CAM_TELEM_IDDR_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_IDDR, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_IDDR_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_IDDR, value, 0)
#define GS_CAM_TELEM_IDDR_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_IDDR, 0)
#define GS_CAM_TELEM_IVCC_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_IVCC, value, 0)
#define GS_CAM_TELEM_IVCC_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_IVCC, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_IVCC_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_IVCC, value, 0)
#define GS_CAM_TELEM_IVCC_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_IVCC, 0)
#define GS_CAM_TELEM_VDDCORE_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_VDDCORE, value, 0)
#define GS_CAM_TELEM_VDDCORE_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_VDDCORE, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_VDDCORE_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_VDDCORE, value, 0)
#define GS_CAM_TELEM_VDDCORE_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_VDDCORE, 0)
#define GS_CAM_TELEM_VDDIODDR_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_VDDIODDR, value, 0)
#define GS_CAM_TELEM_VDDIODDR_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_VDDIODDR, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_VDDIODDR_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_VDDIODDR, value, 0)
#define GS_CAM_TELEM_VDDIODDR_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_VDDIODDR, 0)
#define GS_CAM_TELEM_VCC_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_VCC, value, 0)
#define GS_CAM_TELEM_VCC_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_VCC, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TELEM_VCC_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_VCC, value, 0)
#define GS_CAM_TELEM_VCC_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_TELEM_TABLE_ID), GS_CAM_TELEM_VCC, 0)

#ifdef __cplusplus
}
#endif
#endif
