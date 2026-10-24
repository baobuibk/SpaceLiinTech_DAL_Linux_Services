/*
 * xtp_crc.c
 *                                                  C.H
 */

#include "xtp_crc.h"
#include "xtp_crc_table.h"

#if XTP_USE_CRC

#if !XTP_CRC_USE_TABLE
#if (XTP_USE_CRC_TYPE == 32)
static uint32_t crc32_bitserial(uint32_t crc, uint8_t data)
{
    uint8_t i;
    crc ^= (uint32_t)data << 24u;
    for (i = 0u; i < 8u; i++) {
        crc = (crc & 0x80000000UL) ? (crc << 1u) ^ XTP_CRC32_POLY : (crc << 1u);
    }
    return crc;
}
#else
static uint16_t crc16_bitserial(uint16_t crc, uint8_t data)
{
    uint8_t i;
    crc ^= (uint16_t)data << 8u;
    for (i = 0u; i < 8u; i++) {
        crc = (crc & 0x8000U) ? (uint16_t)((crc << 1u) ^ XTP_CRC16_XMODEM_POLY) : (uint16_t)(crc << 1u);
    }
    return crc;
}
#endif
#endif /* !XTP_CRC_USE_TABLE */

void xTP_CRC_Init(xTP_CRC_t *crc)
{
    if (crc == NULL) { return; }
#if (XTP_USE_CRC_TYPE == 32)
    crc->current_crc = XTP_CRC32_INIT;
#else
    crc->current_crc = XTP_CRC16_XMODEM_INIT;
#endif
}

void xTP_CRC_Update(xTP_CRC_t *crc, uint8_t data)
{
    if (crc == NULL) { return; }
#if XTP_CRC_USE_TABLE
#if (XTP_USE_CRC_TYPE == 32)
    { uint8_t idx = (uint8_t)((crc->current_crc >> 24u) ^ data);
      crc->current_crc = (crc->current_crc << 8u) ^ xtp_crc32_table[idx]; }
#else
    { uint8_t idx = (uint8_t)((crc->current_crc >> 8u) ^ data);
      crc->current_crc = (uint16_t)((crc->current_crc << 8u) ^ xtp_crc16_table[idx]); }
#endif
#else
#if (XTP_USE_CRC_TYPE == 32)
    crc->current_crc = crc32_bitserial(crc->current_crc, data);
#else
    crc->current_crc = crc16_bitserial(crc->current_crc, data);
#endif
#endif
}

#if (XTP_USE_CRC_TYPE == 32)
uint32_t xTP_CRC_Finish(const xTP_CRC_t *crc)
{
    if (crc == NULL) { return 0UL; }
    return crc->current_crc ^ XTP_CRC32_FINAL_XOR;
}
#else
uint16_t xTP_CRC_Finish(const xTP_CRC_t *crc)
{
    if (crc == NULL) { return 0U; }
    return crc->current_crc;
}
#endif

#endif /* XTP_USE_CRC */
