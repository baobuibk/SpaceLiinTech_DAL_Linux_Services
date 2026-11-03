/* bee_dal_crc32.c  */
#include "bee_dal_crc32.h"

uint32_t bee_dal_crc32_init(void)
{
    return 0xFFFFFFFFU;
}

uint32_t bee_dal_crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return crc;
}

uint32_t bee_dal_crc32_finalize(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFU;
}

uint32_t bee_dal_crc32(const void *data, size_t len)
{
    return bee_dal_crc32_finalize(bee_dal_crc32_update(bee_dal_crc32_init(), data, len));
}
