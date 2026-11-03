/* bee_dal_crc16.c */
#include "bee_dal_crc16.h"

uint16_t bee_dal_crc16_init(void)
{
    return BEE_DAL_CRC16_INIT;
}

uint16_t bee_dal_crc16_update(uint16_t crc, uint8_t byte)
{
    crc = (uint16_t)(crc ^ ((uint16_t)byte << 8));
    for (int i = 0; i < 8; i++) {
        if (crc & 0x8000U)
            crc = (uint16_t)((crc << 1) ^ 0x1021U);
        else
            crc = (uint16_t)(crc << 1);
    }
    return crc;
}

uint16_t bee_dal_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = bee_dal_crc16_init();
    for (size_t i = 0; i < len; i++)
        crc = bee_dal_crc16_update(crc, data[i]);
    return crc;
}
