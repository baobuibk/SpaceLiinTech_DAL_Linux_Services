#ifndef GS_CAM_PARAM_TAG_H
#define GS_CAM_PARAM_TAG_H

/* Copyright (c) 2013 GomSpace A/S. All rights reserved. (AUTO GENERATED) */
#include <gs/param/table.h>

#ifdef __cplusplus
extern "C" {
#endif

// Table parameters address/offset
#define GS_CAM_TAG_TAG_ADCS                                              0                // 0x0000 bool
#define GS_CAM_TAG_TAG_ADCS_HOST                                         1                // 0x0001 uint8
#define GS_CAM_TAG_TAG_ADCS_PORT                                         2                // 0x0002 uint8
#define GS_CAM_TAG_TAG_ADCS_TOUT                                         4                // 0x0004 uint16

#ifndef __cplusplus
static const gs_param_table_row_t gs_cam_tag_table[] = {
    {.name = "tag-adcs",      .addr = GS_CAM_TAG_TAG_ADCS,                               .type = GS_PARAM_BOOL,    .size =   1, .array_size =  1, .flags = 0},
    {.name = "tag-adcs-host", .addr = GS_CAM_TAG_TAG_ADCS_HOST,                          .type = GS_PARAM_UINT8,   .size =   1, .array_size =  1, .flags = 0},
    {.name = "tag-adcs-port", .addr = GS_CAM_TAG_TAG_ADCS_PORT,                          .type = GS_PARAM_UINT8,   .size =   1, .array_size =  1, .flags = 0},
    {.name = "tag-adcs-tout", .addr = GS_CAM_TAG_TAG_ADCS_TOUT,                          .type = GS_PARAM_UINT16,  .size =   2, .array_size =  1, .flags = 0},
};
#endif

#define GS_CAM_TAG_TABLE_NAME                            "tag"
#define GS_CAM_TAG_TABLE_ID                              3
#define GS_CAM_TAG_TABLE_MEM_ID                          3 // legacy definition for table id
#define GS_CAM_TAG_TABLE_SIZE                            6 // bytes
#define GS_CAM_TAG_TABLE_ROWS                            (sizeof(gs_cam_tag_table) / sizeof(gs_cam_tag_table[0]))

#define GS_CAM_TAG_TAG_ADCS_SET(value) gs_param_set_bool(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS, value, 0)
#define GS_CAM_TAG_TAG_ADCS_SET_NO_CALLBACK(value) gs_param_set_bool(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TAG_TAG_ADCS_GET(value) gs_param_get_bool(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS, value, 0)
#define GS_CAM_TAG_TAG_ADCS_GET_NC() gs_param_get_bool_nc(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS, 0)
#define GS_CAM_TAG_TAG_ADCS_HOST_SET(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS_HOST, value, 0)
#define GS_CAM_TAG_TAG_ADCS_HOST_SET_NO_CALLBACK(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS_HOST, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TAG_TAG_ADCS_HOST_GET(value) gs_param_get_uint8(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS_HOST, value, 0)
#define GS_CAM_TAG_TAG_ADCS_HOST_GET_NC() gs_param_get_uint8_nc(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS_HOST, 0)
#define GS_CAM_TAG_TAG_ADCS_PORT_SET(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS_PORT, value, 0)
#define GS_CAM_TAG_TAG_ADCS_PORT_SET_NO_CALLBACK(value) gs_param_set_uint8(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS_PORT, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TAG_TAG_ADCS_PORT_GET(value) gs_param_get_uint8(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS_PORT, value, 0)
#define GS_CAM_TAG_TAG_ADCS_PORT_GET_NC() gs_param_get_uint8_nc(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS_PORT, 0)
#define GS_CAM_TAG_TAG_ADCS_TOUT_SET(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS_TOUT, value, 0)
#define GS_CAM_TAG_TAG_ADCS_TOUT_SET_NO_CALLBACK(value) gs_param_set_uint16(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS_TOUT, value, GS_PARAM_F_NO_CALLBACK)
#define GS_CAM_TAG_TAG_ADCS_TOUT_GET(value) gs_param_get_uint16(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS_TOUT, value, 0)
#define GS_CAM_TAG_TAG_ADCS_TOUT_GET_NC() gs_param_get_uint16_nc(gs_param_table_by_id(GS_CAM_TAG_TABLE_ID), GS_CAM_TAG_TAG_ADCS_TOUT, 0)

#ifdef __cplusplus
}
#endif
#endif
