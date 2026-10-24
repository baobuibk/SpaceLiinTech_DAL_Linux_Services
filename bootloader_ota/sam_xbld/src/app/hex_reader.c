/*
 * hex_reader.c 
 */

#include "hex_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* =========================================================================
 * HexMem - sparse address map
 * =========================================================================*/

#define MEM_INIT_CAP  4096U

void hex_mem_init(HexMem_t *m)
{
    memset(m, 0, sizeof(*m));
}

void hex_mem_free(HexMem_t *m)
{
    free(m->addrs);
    free(m->bytes);
    memset(m, 0, sizeof(*m));
}

static int mem_grow(HexMem_t *m)
{
    size_t newcap = m->cap ? m->cap * 2 : MEM_INIT_CAP;
    uint32_t *na  = realloc(m->addrs, newcap * sizeof(uint32_t));
    uint8_t  *nb  = realloc(m->bytes, newcap * sizeof(uint8_t));
    if (!na || !nb) { free(na); free(nb); return -1; }
    m->addrs = na;
    m->bytes = nb;
    m->cap   = newcap;
    return 0;
}

static int mem_put(HexMem_t *m, uint32_t addr, uint8_t b)
{
    if (m->count >= m->cap && mem_grow(m) < 0) return -1;
    m->addrs[m->count] = addr;
    m->bytes[m->count] = b;
    m->count++;
    return 0;
}

size_t hex_mem_read_range(const HexMem_t *m, uint32_t addr, uint8_t *out, size_t len)
{
    /* m->addrs is not guaranteed sorted, so scan linearly. */
    size_t found = 0;
    for (size_t i = 0; i < m->count; i++) {
        uint32_t a = m->addrs[i];
        if (a >= addr && (a - addr) < len) {
            out[a - addr] = m->bytes[i];
            found++;
        }
    }
    return found;
}

/* =========================================================================
 * Intel HEX parser
 * =========================================================================*/

/* Parse two hex digits -> byte.  Returns -1 on bad char. */
static int parse_hex_byte(const char *s)
{
    int hi, lo;
    hi = (s[0] >= '0' && s[0] <= '9') ? s[0]-'0' :
         (s[0] >= 'A' && s[0] <= 'F') ? s[0]-'A'+10 :
         (s[0] >= 'a' && s[0] <= 'f') ? s[0]-'a'+10 : -1;
    lo = (s[1] >= '0' && s[1] <= '9') ? s[1]-'0' :
         (s[1] >= 'A' && s[1] <= 'F') ? s[1]-'A'+10 :
         (s[1] >= 'a' && s[1] <= 'f') ? s[1]-'a'+10 : -1;
    if (hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
}

int hex_load(HexMem_t *m, const char *path,
             uint32_t flash_start, uint32_t flash_end)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "hex_reader: cannot open '%s': %s\n",
                      path, strerror(errno)); return -1; }

    char     line[600];
    uint32_t base    = 0;
    int      loaded  = 0;
    int      lineno  = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        /* Trim CR/LF */
        for (int i = (int)strlen(line)-1; i >= 0 && (line[i]=='\r'||line[i]=='\n'); i--)
            line[i] = '\0';

        if (line[0] != ':') continue;

        /* Minimum record length: :LLAAAATT = 9 chars */
        if (strlen(line) < 11) continue;

        int byte_count = parse_hex_byte(line + 1);
        if (byte_count < 0) { fprintf(stderr, "hex_reader: bad record line %d\n", lineno); continue; }

        int addr_hi = parse_hex_byte(line + 3);
        int addr_lo = parse_hex_byte(line + 5);
        int rec_type = parse_hex_byte(line + 7);
        if (addr_hi < 0 || addr_lo < 0 || rec_type < 0) continue;

        uint16_t offset = (uint16_t)((addr_hi << 8) | addr_lo);

        /* Verify checksum */
        uint8_t  chk = 0;
        for (int i = 0; i < byte_count + 4; i++) {
            int v = parse_hex_byte(line + 1 + i*2);
            if (v < 0) goto next_line;
            chk += (uint8_t)v;
        }
        {
            int stored_chk = parse_hex_byte(line + 1 + (byte_count+4)*2);
            if (stored_chk < 0) goto next_line;
            chk += (uint8_t)stored_chk;
            if (chk != 0) {
                fprintf(stderr, "hex_reader: checksum error line %d\n", lineno);
                goto next_line;
            }
        }

        if (rec_type == 0x00) {
            /* Data record */
            for (int i = 0; i < byte_count; i++) {
                int b = parse_hex_byte(line + 9 + i*2);
                if (b < 0) continue;
                uint32_t a = base + offset + (uint32_t)i;
                if (a >= flash_start && a < flash_end) {
                    if (mem_put(m, a, (uint8_t)b) < 0) {
                        fclose(f); return -1;
                    }
                    loaded++;
                }
            }
        } else if (rec_type == 0x04) {
            /* Extended linear address */
            if (byte_count < 2) goto next_line;
            int hi = parse_hex_byte(line + 9);
            int lo = parse_hex_byte(line + 11);
            if (hi < 0 || lo < 0) goto next_line;
            base = (uint32_t)((hi << 8) | lo) << 16;
        } else if (rec_type == 0x02) {
            /* Extended segment address */
            if (byte_count < 2) goto next_line;
            int hi = parse_hex_byte(line + 9);
            int lo = parse_hex_byte(line + 11);
            if (hi < 0 || lo < 0) goto next_line;
            base = (uint32_t)((hi << 8) | lo) << 4;
        } else if (rec_type == 0x01) {
            /* EOF */
            break;
        }
next_line:;
    }

    fclose(f);
    return loaded;
}

/* =========================================================================
 * mem_to_chunks  -  mirrors Python mem_to_chunks() exactly
 * =========================================================================*/

void chunk_list_init(ChunkList_t *cl) { memset(cl, 0, sizeof(*cl)); }

void chunk_list_free(ChunkList_t *cl)
{
    for (size_t i = 0; i < cl->count; i++) free(cl->chunks[i].data);
    free(cl->chunks);
    memset(cl, 0, sizeof(*cl));
}

static int cl_add(ChunkList_t *cl, uint32_t addr, uint8_t *data, uint32_t len)
{
    if (cl->count >= cl->cap) {
        size_t nc = cl->cap ? cl->cap * 2 : 64;
        HexChunk_t *nb = realloc(cl->chunks, nc * sizeof(HexChunk_t));
        if (!nb) return -1;
        cl->chunks = nb;
        cl->cap    = nc;
    }
    cl->chunks[cl->count].addr = addr;
    cl->chunks[cl->count].data = data;
    cl->chunks[cl->count].len  = len;
    cl->count++;
    return 0;
}

/* Round up x to next multiple of 16 */
static uint32_t round16(uint32_t x) { return (x + 15U) & ~15U; }

int mem_to_chunks(const HexMem_t *m, ChunkList_t *cl, uint32_t chunk_size)
{
    if (!m->count) return 0;

    /* ------------------------------------------------------------------ *
     * Step 1: Build contiguous regions (gap > chunk_size -> new region)   *
     * Mirrors Python: if a - prev > chunk_size -> append region           *
     * ------------------------------------------------------------------ */
    typedef struct { uint32_t start; uint8_t *buf; uint32_t len; } Region;
    Region   *regions = NULL;
    size_t    nreg = 0, regcap = 0;

#define REG_PUSH(rs, rb, rl) do { \
    if (nreg >= regcap) { \
        size_t _nc = regcap ? regcap*2 : 16; \
        Region *_nb = realloc(regions, _nc*sizeof(Region)); \
        if (!_nb) { free(rb); goto oom; } \
        regions = _nb; regcap = _nc; \
    } \
    regions[nreg].start = (rs); \
    regions[nreg].buf   = (rb); \
    regions[nreg].len   = (rl); \
    nreg++; \
} while(0)

    /* m->addrs are NOT guaranteed sorted - sort a copy */
    size_t   n   = m->count;
    size_t  *idx = malloc(n * sizeof(size_t));
    if (!idx) goto oom;
    for (size_t i = 0; i < n; i++) idx[i] = i;
    /* Simple insertion sort (data is usually nearly sorted from hex file) */
    for (size_t i = 1; i < n; i++) {
        size_t key = idx[i];
        uint32_t ka = m->addrs[key];
        ssize_t j = (ssize_t)i - 1;
        while (j >= 0 && m->addrs[idx[j]] > ka) { idx[j+1] = idx[j]; j--; }
        idx[j+1] = key;
    }

    uint32_t rstart = m->addrs[idx[0]];
    uint8_t *rbuf   = NULL;
    uint32_t rbuf_cap = 0, rbuf_len = 0;
    uint32_t prev   = m->addrs[idx[0]];

    for (size_t i = 0; i < n; i++) {
        uint32_t a = m->addrs[idx[i]];
        uint8_t  b = m->bytes[idx[i]];

        /* Python: if a - prev > chunk_size and rdata: push region */
        if (i > 0 && (a - prev) > chunk_size && rbuf_len > 0) {
            REG_PUSH(rstart, rbuf, rbuf_len);
            rbuf = NULL; rbuf_cap = 0; rbuf_len = 0;
            rstart = a; prev = a;
        }

        /* Fill gap with 0xFF */
        while (rbuf_len < (a - rstart)) {
            if (rbuf_len >= rbuf_cap) {
                uint32_t nc = rbuf_cap ? rbuf_cap*2 : 256;
                uint8_t *nb = realloc(rbuf, nc);
                if (!nb) { free(rbuf); free(idx); goto oom; }
                rbuf = nb; rbuf_cap = nc;
            }
            rbuf[rbuf_len++] = 0xFF;
        }

        /* Append byte */
        if (rbuf_len >= rbuf_cap) {
            uint32_t nc = rbuf_cap ? rbuf_cap*2 : 256;
            uint8_t *nb = realloc(rbuf, nc);
            if (!nb) { free(rbuf); free(idx); goto oom; }
            rbuf = nb; rbuf_cap = nc;
        }
        rbuf[rbuf_len++] = b;
        prev = a;
    }
    if (rbuf_len > 0) REG_PUSH(rstart, rbuf, rbuf_len);
    free(idx);

    /* ------------------------------------------------------------------ *
     * Step 2: Align each region to 16 bytes, split into chunks           *
     * Mirrors Python exactly:                                             *
     *   aligned = rstart & ~0xF                                          *
     *   prefix  = rstart - aligned                                       *
     *   buf     = [0xFF]*prefix + rdata                                  *
     *   tail    = len(buf) % 16 -> pad to 0 (16-align)                   *
     *   split into chunk_size slices (each also padded to 16B)           *
     * ------------------------------------------------------------------ */
    for (size_t r = 0; r < nreg; r++) {
        uint32_t rs   = regions[r].start;
        uint8_t *rbf  = regions[r].buf;
        uint32_t rlen = regions[r].len;

        uint32_t aligned = rs & ~0xFU;
        uint32_t prefix  = rs - aligned;

        /* Build aligned buffer: [0xFF]*prefix + data + [0xFF]*tail */
        uint32_t total = round16(prefix + rlen);
        uint8_t *buf   = malloc(total);
        if (!buf) { free(rbf); goto oom; }
        memset(buf, 0xFF, total);
        memcpy(buf + prefix, rbf, rlen);
        free(rbf);
        regions[r].buf = NULL;

        /* Split into chunk_size slices */
        uint32_t off = 0;
        while (off < total) {
            uint32_t sz = (total - off < chunk_size) ? (total - off) : chunk_size;
            /* Pad slice to 16-byte multiple */
            uint32_t sz16 = round16(sz);
            uint8_t *chunk = malloc(sz16);
            if (!chunk) { free(buf); goto oom; }
            memset(chunk, 0xFF, sz16);
            memcpy(chunk, buf + off, sz);

            if (cl_add(cl, aligned + off, chunk, sz16) < 0) {
                free(chunk); free(buf); goto oom;
            }
            off += sz;
        }
        free(buf);
    }

    free(regions);
    return 0;

oom:
    fprintf(stderr, "hex_reader: out of memory\n");
    for (size_t r = 0; r < nreg; r++) free(regions[r].buf);
    free(regions);
    return -1;
}

uint32_t chunk_list_total_bytes(const ChunkList_t *cl)
{
    uint32_t t = 0;
    for (size_t i = 0; i < cl->count; i++) t += cl->chunks[i].len;
    return t;
}
