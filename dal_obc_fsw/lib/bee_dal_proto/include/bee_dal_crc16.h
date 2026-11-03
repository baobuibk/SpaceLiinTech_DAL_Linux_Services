/* bee_dal_crc16.h
 *
 */

#ifndef BEE_DAL_CRC16_H
#define BEE_DAL_CRC16_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BEE_DAL_CRC16_INIT 0xFFFFU

uint16_t bee_dal_crc16_init(void);
uint16_t bee_dal_crc16_update(uint16_t crc, uint8_t byte);
uint16_t bee_dal_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* BEE_DAL_CRC16_H */
