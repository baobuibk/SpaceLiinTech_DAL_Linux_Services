/*
 * xtp_ecc.h  - CA-based single-byte error-correcting code
 * Algorithm: LFSR+XOR, ref. Chowdhury et al. IEEE Trans. Computers 1996.
 * Corrects exactly 1 byte error in a block of up to 255 bytes.
 * Detects (but cannot correct) 2+ byte errors.
 *
 * Usage:
 *   uint8_t e0, e1;
 *   xTP_ECC_Encode(data, len, &e0, &e1);   // compute check bytes
 *   xTP_ECC_Result_t r = xTP_ECC_Decode(data, len, e0_rx, e1_rx);
 *                                                  C.H
 */

#ifndef XTP_ECC_H
#define XTP_ECC_H

#include <stdint.h>
#include "xtp_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XTP_USE_ECC

typedef enum {
    XTP_ECC_OK               = 0,  /**< No error detected. */
    XTP_ECC_CORRECTED        = 1,  /**< 1-byte error detected and corrected. */
    XTP_ECC_FAIL_PARITY      = 2,  /**< Error in ECC check bytes themselves. */
    XTP_ECC_FAIL_UNCORRECTED = 3,  /**< 2+ byte errors; cannot correct. */
} xTP_ECC_Result_t;

/**
 * @brief Compute ECC check bytes for a data block.
 * @param data      Data buffer (must not be NULL).
 * @param len       Number of bytes (1..255).
 * @param ecc0_out  Output: XOR parity byte.
 * @param ecc1_out  Output: LFSR-weighted byte.
 */
void xTP_ECC_Encode(const uint8_t *data, uint8_t len,
                    uint8_t *ecc0_out, uint8_t *ecc1_out);

/**
 * @brief Verify and optionally correct a data block in-place.
 * @param data     Data buffer (modified in-place if correction applied).
 * @param len      Number of bytes (1..255).
 * @param ecc0_rx  Received ECC0 byte.
 * @param ecc1_rx  Received ECC1 byte.
 * @return xTP_ECC_Result_t.
 */
xTP_ECC_Result_t xTP_ECC_Decode(uint8_t *data, uint8_t len,
                                  uint8_t ecc0_rx, uint8_t ecc1_rx);

#endif /* XTP_USE_ECC */

#ifdef __cplusplus
}
#endif
#endif /* XTP_ECC_H */
