/* bee_dal_crc32.h
 *
 */
#ifndef BEE_DAL_CRC32_H
#define BEE_DAL_CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t bee_dal_crc32_init(void);
uint32_t bee_dal_crc32_update(uint32_t crc, const void *data, size_t len);
uint32_t bee_dal_crc32_finalize(uint32_t crc);

/** Convenience: full CRC-32 of one contiguous buffer. */
uint32_t bee_dal_crc32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* BEE_DAL_CRC32_H */
