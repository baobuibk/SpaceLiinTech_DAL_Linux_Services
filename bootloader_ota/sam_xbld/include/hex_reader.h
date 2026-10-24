/*
 * hex_reader.h  Intel HEX file parser
 */

#ifndef HEX_READER_H
#define HEX_READER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Sparse memory map (addr -> byte)
 * Backed by a flat sorted array of (addr, byte) pairs.
 * =========================================================================*/
typedef struct {
    uint32_t *addrs;  /**< sorted array of addresses */
    uint8_t  *bytes;  /**< byte at each address       */
    size_t    count;  /**< number of entries           */
    size_t    cap;    /**< allocated capacity          */
} HexMem_t;

void hex_mem_init(HexMem_t *m);
void hex_mem_free(HexMem_t *m);

/**
 * Copy bytes in [addr, addr+len) out of an already-loaded sparse map.
 * Missing addresses are left untouched in `out` (not zeroed).
 * @return  number of bytes actually found in that range (0..len).
 */
size_t hex_mem_read_range(const HexMem_t *m, uint32_t addr, uint8_t *out, size_t len);

/**
 * Parse an Intel HEX file into the sparse map.
 * Only addresses in [flash_start, flash_end) are retained.
 * @return  number of bytes loaded, or -1 on error.
 */
int hex_load(HexMem_t *m, const char *path,
             uint32_t flash_start, uint32_t flash_end);

/* =========================================================================
 * Aligned chunk list
 * =========================================================================*/
typedef struct {
    uint32_t  addr;  /**< 16-byte aligned start address */
    uint8_t  *data;  /**< heap-allocated, len is multiple of 16 */
    uint32_t  len;
} HexChunk_t;

typedef struct {
    HexChunk_t *chunks;
    size_t      count;
    size_t      cap;
} ChunkList_t;

void chunk_list_init(ChunkList_t *cl);
void chunk_list_free(ChunkList_t *cl);

/**
 * Convert sparse map -> aligned chunks ready for xBLD write.
 * Mirrors Python mem_to_chunks() exactly:
 *   - Gaps > chunk_size between records start a new region.
 *   - Each region is 16-byte aligned (prefix + suffix padded 0xFF).
 *   - Each region is split into chunk_size slices (also padded to 16B).
 *
 * @param chunk_size  Max data bytes per chunk (e.g. 240).
 * @return 0 on success, -1 on OOM.
 */
int mem_to_chunks(const HexMem_t *m, ChunkList_t *cl, uint32_t chunk_size);

/** Total data bytes across all chunks. */
uint32_t chunk_list_total_bytes(const ChunkList_t *cl);

#ifdef __cplusplus
}
#endif
#endif /* HEX_READER_H */
