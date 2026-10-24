/*
 * xtp_crc.h  CRC engine (CRC-16/XMODEM and CRC-32, MSB-first)
 *                                                  C.H
 */

#ifndef XTP_CRC_H
#define XTP_CRC_H

#include <stdint.h>
#include "xtp_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XTP_USE_CRC

#if (XTP_USE_CRC_TYPE == 32)
#define XTP_CRC32_POLY       0x04C11DB7UL
#define XTP_CRC32_INIT       0xFFFFFFFFUL
#define XTP_CRC32_FINAL_XOR  0xFFFFFFFFUL
#else
#define XTP_CRC16_XMODEM_POLY  0x1021U
#define XTP_CRC16_XMODEM_INIT  0x0000U
#endif

typedef struct {
#if (XTP_USE_CRC_TYPE == 32)
    uint32_t current_crc;
#else
    uint16_t current_crc;
#endif
} xTP_CRC_t;

void xTP_CRC_Init(xTP_CRC_t *crc);
void xTP_CRC_Update(xTP_CRC_t *crc, uint8_t data);

#if (XTP_USE_CRC_TYPE == 32)
uint32_t xTP_CRC_Finish(const xTP_CRC_t *crc);
#else
uint16_t xTP_CRC_Finish(const xTP_CRC_t *crc);
#endif

#endif /* XTP_USE_CRC */

#ifdef __cplusplus
}
#endif
#endif /* XTP_CRC_H */
