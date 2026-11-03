#pragma once

/* sys_info TM: 12-byte header followed by three 28-byte board records. */
#define SYS_INFO_SCHEMA 1U
#define SYS_INFO_HEADER_SIZE 12U
#define SYS_INFO_RECORD_SIZE 28U
#define SYS_INFO_BUILD_SIZE 20U
#define SYS_INFO_PAYLOAD_SIZE 96U
#define SYS_INFO_PDU 1U
#define SYS_INFO_EXP 2U
#define SYS_INFO_OBC 3U

/* Record: board, online, HW major/minor/patch, FW major/minor/patch,
 * 20-byte zero-terminated build ID (zero padded). Unknown FW = FF.FF.FF. */
