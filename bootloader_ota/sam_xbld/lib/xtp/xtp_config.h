/*
 * xtp_config.h  —  xTP v2 host configuration (Linux / POSIX)
 * This file is the ONLY difference between the MCU and host builds.
 * All other .c files are copied verbatim from the MCU source tree.
 *
 * Must match the MCU-side xtp_config.h for these fields:
 *   XTP_USE_CRC / XTP_USE_CRC_TYPE
 *   XTP_USE_ECC
 *   XTP_USE_ARQ
 *   XTP_USE_TARGET / XTP_USE_SEG / XTP_USE_LEN_16
 *
 * Linux-specific changes vs MCU:
 *   XTP_USE_TX_LOCK / XTP_USE_RX_LOCK  →  pthread mutexes (in xtp_port.c)
 *   XTP_ARQ_TIMEOUT_MS                 →  can be larger (no RTOS tick overhead)
 *   XTP_ENABLE_LOG                     →  routes to fprintf(stderr, ...)
 */

#ifndef XTP_CONFIG_H
#define XTP_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Protocol version — MUST match MCU side
 * =========================================================================*/
#define XTP_PROTOCOL_VERSION  0U

/* =========================================================================
 * Frame sync bytes — MUST match MCU side
 * =========================================================================*/
#define XTP_START1_BYTE     (0xC0U | (XTP_PROTOCOL_VERSION & 0x0FU))
#define XTP_START2_BYTE     0xDEU
#define XTP_STOP1_BYTE      0xDAU
#define XTP_STOP2_BYTE      0xEDU
#define XTP_BROADCAST_ADDR  0xFFU

/* =========================================================================
 * CRC  — MUST match MCU side
 * =========================================================================*/
#define XTP_USE_CRC         1
#define XTP_USE_CRC_TYPE    16      /* 16 = CRC-16/XMODEM,  32 = CRC-32 */
#define XTP_CRC_USE_TABLE   1

/* =========================================================================
 * ECC  — MUST match MCU side
 * =========================================================================*/
#define XTP_USE_ECC         1

/* =========================================================================
 * HMAC  — MUST match MCU side (disabled by default)
 * =========================================================================*/
#define XTP_USE_HMAC        0

/* =========================================================================
 * LEN field width  — MUST match MCU side
 * =========================================================================*/
#define XTP_USE_LEN_16      0
#if XTP_USE_LEN_16 && XTP_USE_ECC
#error "XTP_USE_LEN_16=1 requires XTP_USE_ECC=0"
#endif

/* =========================================================================
 * SEG  — MUST match MCU side (disabled for xBLD)
 * =========================================================================*/
#define XTP_USE_SEG         0

/* =========================================================================
 * ARQ  — MUST match MCU side
 * =========================================================================*/
#define XTP_USE_ARQ         1
#if XTP_USE_ARQ
#define XTP_ARQ_TIMEOUT_MS  500U    /* slightly more generous than MCU 300ms */
#define XTP_ARQ_MAX_RETRIES 3U
#endif

/* =========================================================================
 * Addressing  — MUST match MCU side
 * =========================================================================*/
#define XTP_USE_TARGET      0

/* =========================================================================
 * Payload buffer
 * =========================================================================*/
#if XTP_USE_LEN_16
#define XTP_DATA_MAX_LEN    5120U
#else
#define XTP_DATA_MAX_LEN    255U
#endif

/* =========================================================================
 * Inter-byte timeout
 * =========================================================================*/
#define XTP_TIMEOUT_MS      500U

/* =========================================================================
 * Command dispatch table
 * =========================================================================*/
#define XTP_MAX_COMMANDS    8U

/* =========================================================================
 * TX / RX locks  — pthread_mutex on Linux
 * =========================================================================*/
#define XTP_USE_TX_LOCK     1
#define XTP_USE_RX_LOCK     1

/* =========================================================================
 * Logging  — routes to stderr on Linux
 * =========================================================================*/
#define XTP_ENABLE_LOG      0
#define XTP_LOG_BUFFER_SIZE 256U

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifdef __cplusplus
}
#endif
#endif /* XTP_CONFIG_H */
