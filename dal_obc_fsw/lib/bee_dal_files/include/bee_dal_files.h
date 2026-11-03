/* bee_dal_files.h
 *
 */
#ifndef BEE_DAL_FILES_H
#define BEE_DAL_FILES_H

#include <stdint.h>
#include <stddef.h>
#include "utils/error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BEE_DAL_FILES_MAX_HK   65536
#define BEE_DAL_FILES_NAME_MAX 48
#define BEE_DAL_FILES_REL_PATH_MAX 64
#define BEE_DAL_FILES_MARK_ALL 0xFFU /* scan() mark_filt: no filter, list everything */

typedef struct {
    char     name[BEE_DAL_FILES_NAME_MAX];
    char     relpath[BEE_DAL_FILES_REL_PATH_MAX]; /* trusted scan result */
    uint32_t size;
} bee_dal_file_entry_t;

xerr_t bee_dal_files_init(const char *data_dir);

xerr_t bee_dal_files_scan(uint8_t mark_filt,
                          uint32_t *out_snap_id,
                          uint32_t *out_hk_count,
                          uint32_t *out_sci1_count,
                          uint32_t *out_sci2_count);

xerr_t bee_dal_files_get_entry(uint32_t snap_id, uint8_t data_type, uint32_t idx,
                                bee_dal_file_entry_t *out);

xerr_t bee_dal_files_info(uint32_t snap_id, uint8_t data_type, uint32_t idx,
                          uint32_t *out_size, uint32_t *out_crc32);

xerr_t bee_dal_files_read_chunk(uint32_t snap_id, uint8_t data_type, uint32_t idx,
                                uint32_t chunk_index, uint16_t chunk_size,
                                uint8_t *out_buf, uint16_t *out_len);

xerr_t bee_dal_files_delete(uint32_t snap_id, uint8_t data_type, uint32_t idx,
                            uint32_t *out_hk_count, uint32_t *out_sci1_count,
                            uint32_t *out_sci2_count);

xerr_t bee_dal_files_mark(uint32_t snap_id, uint8_t data_type, uint32_t idx, uint8_t mark);

#ifdef __cplusplus
}
#endif
#endif /* BEE_DAL_FILES_H */
