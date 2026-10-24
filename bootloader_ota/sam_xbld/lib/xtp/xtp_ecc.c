/*
 * xtp_ecc.c 
 *
 * Primitive polynomial: x^8 + x^4 + x^3 + x^2 + 1  (Galois LFSR tap = 0x8E)
 * Period of LFSR: 255 (maximal-length sequence over GF(2^8))...
 *
 * ECC0 = XOR of all data bytes (parity).
 * ECC1 = LFSR-folded XOR: each byte m[i] is weighted by position i,
 *        giving each position a unique LFSR "signature"
 *                                                  C.H
 */

#include "xtp_ecc.h"

#if XTP_USE_ECC

/* Galois LFSR step: right-shift, XOR tap mask 0x8E if bit 0 was set */
static uint8_t lf_sr(uint8_t x)
{
    return (uint8_t)((x >> 1u) ^ ((uint8_t)(0u - (x & 1u)) & 0x8Eu));
}

void xTP_ECC_Encode(const uint8_t *data, uint8_t len,
                    uint8_t *ecc0_out, uint8_t *ecc1_out)
{
    uint8_t ecc0 = 0u, ecc1 = 0u, i;
    for (i = 0u; i < len; i++) {
        ecc0 ^= data[i];
        ecc1 = lf_sr((uint8_t)(ecc1 ^ data[i]));
    }
    *ecc0_out = ecc0;
    *ecc1_out = ecc1;
}

xTP_ECC_Result_t xTP_ECC_Decode(uint8_t *data, uint8_t len,
                                  uint8_t ecc0_rx, uint8_t ecc1_rx)
{
    uint8_t scc0, scc1;
    int     idx, steps;

    if (data == NULL || len == 0u) { return XTP_ECC_FAIL_UNCORRECTED; }

    xTP_ECC_Encode(data, len, &scc0, &scc1);
    scc0 ^= ecc0_rx;
    scc1 ^= ecc1_rx;

    /* Case 1: no error. */
    if (scc0 == 0u && scc1 == 0u) { return XTP_ECC_OK; }

    /* Case 2: error in one ECC byte (only one syndrome is non-zero) */
    if (scc0 == 0u || scc1 == 0u) { return XTP_ECC_FAIL_PARITY; }

    /* Case 3: exactly 1 data byte error locate via LFSR rotation.
     * The error is at position k where scc1 = lf_sr^(len-k)(scc0).
     * We rotate scc1 until it equals scc0 and count steps.
     * BOUNDED to 255 iterations (LFSR period) */
    idx   = (int)len - 255;
    steps = 0;
    while (scc1 != scc0) {
        scc1 = lf_sr(scc1);
        idx++;
        steps++;
        if (steps >= 255) {
            /* Never converged 2+ byte errors, cannot correct */
            return XTP_ECC_FAIL_UNCORRECTED;
        }
    }

    /* Validate computed position. */
    if (idx < 0 || idx >= (int)len) { return XTP_ECC_FAIL_UNCORRECTED; }

    /* Correct the byte (scc0 is the error pattern).*/
    data[idx] ^= scc0;
    return XTP_ECC_CORRECTED;
}

#endif /* XTP_USE_ECC */
