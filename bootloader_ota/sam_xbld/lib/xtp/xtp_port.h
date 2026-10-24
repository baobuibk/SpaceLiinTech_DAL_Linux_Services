/*
 * xtp_port.h  —  xTP port layer (Linux/POSIX)
 *
 * xTP_Port_t field names MUST match the MCU definition exactly
 * (xtp.c accesses them by name at compile time).
 *
 * MCU fields used in xtp.c / xtp_arq.c:
 *   send_byte     void     (*)(const uint8_t *b)
 *   read_byte     int      (*)(uint8_t *b)    0=got byte, -1=empty/err
 *   get_tick      uint32_t (*)(void)           milliseconds
 *   get_tx_space  uint16_t (*)(void)           0 = unlimited
 *   tx_lock/unlock / rx_lock/unlock
 */

#ifndef XTP_PORT_H
#define XTP_PORT_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include "xtp_config.h"

/* MCU compat: xTP_MemSet is used in xtp.c (maps to standard memset) */
#ifndef xTP_MemSet
#define xTP_MemSet  memset
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Port struct — field names MUST match MCU xtp_port.h exactly
 * =========================================================================*/
typedef struct {
    void     (*send_byte   )(const uint8_t *b);
    int      (*read_byte   )(uint8_t *b);  /* 0=got byte, -1=empty/err */
    uint32_t (*get_tick    )(void);        /* milliseconds              */
    uint16_t (*get_tx_space)(void);        /* 0 = no TX FIFO limit      */
    int      (*tx_lock     )(void);
    void     (*tx_unlock   )(void);
    int      (*rx_lock     )(void);
    void     (*rx_unlock   )(void);
} xTP_Port_t;

/* =========================================================================
 * Linux port context
 * =========================================================================*/
typedef struct {
    int             fd;
    char            dev[64];
    uint32_t        baud;
    pthread_mutex_t tx_mtx;
    pthread_mutex_t rx_mtx;
} xTP_PortCtx_t;

/* =========================================================================
 * API
 * =========================================================================*/
int        xTP_Port_Open (xTP_PortCtx_t *ctx, const char *dev, uint32_t baud);
void       xTP_Port_Close(xTP_PortCtx_t *ctx);

/*
 * xTP_GetDefaultPort — two signatures for compatibility:
 *
 *   No-arg version: used by MCU xtp.c xTP_InitDefault()
 *     (MCU calls xTP_GetDefaultPort() with no arguments)
 *
 *   xTP_GetDefaultPortCtx: used by xbld_client.c host code
 *     (passes the open xTP_PortCtx_t so callbacks find the fd)
 *
 * The no-arg version uses the global s_ctx set by xTP_Port_Open().
 */
xTP_Port_t xTP_GetDefaultPort(void);
xTP_Port_t xTP_GetDefaultPortCtx(xTP_PortCtx_t *ctx);

/* Blocking read for rx_thread (50ms timeout typical).
 * Returns 0=got byte, -1=timeout/error. */
int xTP_Port_ReadBlocking(uint8_t *b, uint32_t timeout_ms);

void xTP_LogImpl(const char *fmt, ...);

#if XTP_ENABLE_LOG
#  include <stdio.h>
#  define xTP_Log(...)  xTP_LogImpl(__VA_ARGS__)
#else
#  define xTP_Log(...)  ((void)0)
#endif

#ifdef __cplusplus
}
#endif
#endif /* XTP_PORT_H */
