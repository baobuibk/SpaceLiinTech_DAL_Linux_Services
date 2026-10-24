#include "programmer.h"
#include <stdio.h>
#include <string.h>

static int status(struct backend *b, uint8_t *value)
{
    uint8_t tx[2] = {0x05, 0}, rx[2] = {0};
    if (b->transfer(b->ctx, tx, rx, sizeof(tx))) return -1;
    *value = rx[1];
    return 0;
}

static int ready(struct backend *b, unsigned timeout_ms)
{
    for (unsigned elapsed = 0; elapsed <= timeout_ms; elapsed += 10) {
        uint8_t sr;
        if (status(b, &sr)) return -1;
        if (!(sr & 1)) return 0;
        if (b->pause_ms(b->ctx, 10)) return -1;
    }
    fprintf(stderr, "Flash busy timeout\n");
    return -1;
}

static int write_enable(struct backend *b)
{
    uint8_t tx = 0x06, rx = 0, sr;
    if (ready(b, 5000) || b->transfer(b->ctx, &tx, &rx, 1) || status(b, &sr))
        return -1;
    if (!(sr & 2)) {
        fprintf(stderr, "Flash rejected write-enable\n");
        return -1;
    }
    return 0;
}

/* Dedicated four-byte opcodes avoid dependence on the current address mode. */
static void address(uint8_t *tx, uint8_t opcode, uint32_t offset)
{
    tx[0] = opcode;
    tx[1] = (uint8_t)(offset >> 24);
    tx[2] = (uint8_t)(offset >> 16);
    tx[3] = (uint8_t)(offset >> 8);
    tx[4] = (uint8_t)offset;
}

static int read_data(struct backend *b, uint32_t offset, uint8_t *data, size_t size)
{
    uint8_t tx[PAGE_SIZE + 5] = {0}, rx[PAGE_SIZE + 5] = {0};
    if (size > PAGE_SIZE) return -1;
    address(tx, 0x13, offset);
    if (b->transfer(b->ctx, tx, rx, size + 5)) return -1;
    memcpy(data, rx + 5, size);
    return 0;
}

static int verify(struct backend *b, const uint8_t *image, size_t size)
{
    for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint8_t data[PAGE_SIZE];
        size_t n = size - offset < PAGE_SIZE ? size - offset : PAGE_SIZE;
        if (read_data(b, (uint32_t)offset, data, n)) return -1;
        if (memcmp(data, image + offset, n)) {
            fprintf(stderr, "Readback mismatch at page 0x%zx\n", offset);
            return -1;
        }
    }
    return 0;
}

int program_run(struct backend *b, enum operation op, const uint8_t *image, size_t size)
{
    if (op < OP_ID || op > OP_PROGRAM ||
        (op != OP_ID && (!image || !size || size > FLASH_SIZE))) return -1;
    /* Do not power-cycle: Flash and FPGA rail relationships need board validation. */
    if (b->gpio(b->ctx, 0, 1)) return -1;
    if (b->pause_ms(b->ctx, 100) || b->gpio(b->ctx, 1, 0) ||
        b->pause_ms(b->ctx, 10)) goto fail;
    if (ready(b, 30000)) goto fail;
    uint8_t tx[4] = {0x9f, 0, 0, 0}, rx[4] = {0};
    if (b->transfer(b->ctx, tx, rx, 4)) goto fail;
    printf("JEDEC ID: %02x %02x %02x\n", rx[1], rx[2], rx[3]);
    if (rx[1] != 0x01 || rx[2] != 0x60 || rx[3] != 0x18) {
        fprintf(stderr, "Expected S25FL128L JEDEC ID 01 60 18\n");
        goto fail;
    }
    if (op == OP_PROGRAM) {
        size_t extent = (size + SECTOR_SIZE - 1) / SECTOR_SIZE * SECTOR_SIZE;
        for (size_t offset = 0; offset < extent; offset += SECTOR_SIZE) {
            uint8_t cmd[5], reply[5] = {0};
            address(cmd, 0x21, (uint32_t)offset);
            if (write_enable(b) || b->transfer(b->ctx, cmd, reply, 5) ||
                ready(b, 30000)) goto fail;
            /* Verify erase before programming; catches protection and failed erases. */
            for (size_t p = offset; p < offset + SECTOR_SIZE; p += PAGE_SIZE) {
                uint8_t data[PAGE_SIZE];
                if (read_data(b, (uint32_t)p, data, sizeof(data))) goto fail;
                for (size_t j = 0; j < sizeof(data); ++j) {
                    if (data[j] != 0xff) {
                        fprintf(stderr, "Erase failed at 0x%zx; check protection\n", p+j);
                        goto fail;
                    }
                }
            }
        }
        for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
            uint8_t cmd[PAGE_SIZE + 5], reply[PAGE_SIZE + 5] = {0};
            size_t n = size - offset < PAGE_SIZE ? size - offset : PAGE_SIZE;
            address(cmd, 0x12, (uint32_t)offset);
            memcpy(cmd + 5, image + offset, n);
            if (write_enable(b) || b->transfer(b->ctx, cmd, reply, n + 5) ||
                ready(b, 5000)) goto fail;
        }
    }
    if (op != OP_ID && verify(b, image, size)) goto fail;
    if (b->gpio(b->ctx, 1, 1) || b->pause_ms(b->ctx, 10) ||
        b->gpio(b->ctx, 0, op == OP_ID ? b->initial_reset : 0)) goto fail;
    puts(op == OP_ID && b->initial_reset ? "Flash returned to FPGA; reset remains held." :
         "Flash returned to FPGA; configuration reset released. FPGA readiness is not monitored.");
    return 0;
fail:
    fprintf(stderr, "Operation failed: reset remains asserted (unless GPIO access failed). "
                    "Do not release to an unverified image; retry with a known-good image.\n");
    return -1;
}
