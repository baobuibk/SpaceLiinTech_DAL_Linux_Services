#ifndef PROGRAMMER_H
#define PROGRAMMER_H
#include <stddef.h>
#include <stdint.h>
#define FLASH_SIZE (16u * 1024u * 1024u)
#define SECTOR_SIZE 4096u
#define PAGE_SIZE 256u
/* gpio: 0 = reset (PD0, high asserts), 1 = owner (PB18, high FPGA). */
struct backend {
    void *ctx;
    int initial_reset; /* Preserve a held reset for ID-only operations. */
    int (*transfer)(void *, const uint8_t *, uint8_t *, size_t);
    int (*gpio)(void *, unsigned, int);
    int (*pause_ms)(void *, unsigned);
};
enum operation { OP_ID, OP_VERIFY, OP_PROGRAM, OP_RELOAD };
/* On any failure after reset is asserted, do not release it or hand back SPI. */
int program_run(struct backend *, enum operation, const uint8_t *, size_t);
#endif
