/*
 * xtp_crc_table.h
 * Description:
 *
 * Algorithm parameters:
 *
 *   CRC-16/XMODEM - poly 0x1021, init 0x0000, no final XOR, MSB-first.
 *   CRC-32        - poly 0x04C11DB7, init 0xFFFFFFFF, final XOR 0xFFFFFFFF,
 *                   MSB-first (non-reflected/"direct" form)
 *
 * ROM cost:
 *   CRC-16 table > 256 x2 bytes =  512 bytes
 *   CRC-32 table > 256 x4 bytes = 1024 bytes
 *
 * Usage:
 *   Selected automatically when XTP_CRC_USE_TABLE = 1 in xtp_config.h.
 *   Direct use of these symbols is not normally needed by application code.
 *                                                  C.H
 */

#ifndef XTP_CRC_TABLE_H
#define XTP_CRC_TABLE_H

#include <stdint.h>
#include "xtp_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XTP_USE_CRC && XTP_CRC_USE_TABLE

#if (XTP_USE_CRC_TYPE == 32)

/**
 * @brief CRC-32 MSB-first lookup table.
 *
 * Polynomial : 0x04C11DB7 (direct / non-reflected form)
 * Init value : 0xFFFFFFFF
 * Final XOR  : 0xFFFFFFFF
 *
 * Entry i is the CRC-32 of the single byte i shifted into the MSB of a
 * 32-bit register, processed for 8 bit-clock cycles.
 *
 * ROM cost: 256 x 4 = 1024 bytes.
 */
extern const uint32_t xtp_crc32_table[256];

#else /* CRC-16 */

/**
 * @brief CRC-16/XMODEM MSB-first lookup table.
 *
 * Polynomial : 0x1021
 * Init value : 0x0000
 * Final XOR  : none
 *
 * Entry i is the CRC-16 of the single byte i shifted into the MSB of a
 * 16-bit register, processed for 8 bit-clock cycles.
 *
 * ROM cost: 256 x 2 = 512 bytes.
 */
extern const uint16_t xtp_crc16_table[256];

#endif /* XTP_USE_CRC_TYPE */

#endif /* XTP_USE_CRC && XTP_CRC_USE_TABLE */

#ifdef __cplusplus
}
#endif

#endif /* XTP_CRC_TABLE_H */
