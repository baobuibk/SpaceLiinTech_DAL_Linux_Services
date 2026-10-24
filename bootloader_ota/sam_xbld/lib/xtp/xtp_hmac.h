/*
 * xtp_hmac.h   SipHash-2-4 truncated-32 message authentication
 *
 * SipHash-2-4 by Aumasson & Bernstein (2012).
 * We truncate the 64-bit output to 32 bits (4-byte tag).
 * This gives 2^32 -> 4 billion brute-force attempts to forge one frame.
 *
 * Overhead: ~150 B code ROM, 32 B RAM working state, ~3 cycles/byte on M4.
 *
 * MAC covers: TYPE|METHOD byte through last CRC byte (inclusive).
 * START and STOP framing bytes are excluded.
 * Including CRC in the MAC input prevents payload-swap attacks where an
 * attacker modifies payload and recomputes CRC without knowing the key.
 *                                                  C.H
 */

#ifndef XTP_HMAC_H
#define XTP_HMAC_H

#include <stdint.h>
#include <stddef.h>
#include "xtp_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XTP_USE_HMAC

/** Size of the MAC tag appended to each frame (bytes). */
#define XTP_HMAC_TAG_LEN  4U

/**
 * @brief Compute a 32-bit MAC tag for a frame.
 *
 * The MAC input is: hdr_buf || data || crc_buf  (concatenated in that order).
 *
 * @param hdr_buf   Header bytes (TYPE|METHOD ... LEN, inclusive).
 * @param hdr_len   Length of hdr_buf.
 * @param data      Payload bytes (may be NULL when data_len=0).
 * @param data_len  Payload length.
 * @param crc_buf   Raw CRC bytes as sent on the wire (little-endian).
 * @param crc_len   2 (CRC-16) or 4 (CRC-32).
 * @return 32-bit MAC tag (little-endian when stored to wire).
 */
uint32_t xTP_HMAC_Compute(const uint8_t *hdr_buf, uint8_t  hdr_len,
                           const uint8_t *data,     uint8_t  data_len,
                           const uint8_t *crc_buf,  uint8_t  crc_len);

#endif /* XTP_USE_HMAC */

#ifdef __cplusplus
}
#endif
#endif /* XTP_HMAC_H */
