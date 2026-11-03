#ifndef GS_UTIL_DRIVERS_SYS_MEMORY_H
#define GS_UTIL_DRIVERS_SYS_MEMORY_H
/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */
/**
   @file

   Cross platform memory status API.
*/

#include <gs/util/stdio.h>
#include <gs/util/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
   RAM status
   Containing different size parameters describing RAM usage.
   All sizes are in bytes.
   If a parameter is not available/supported on a specific platform, the parameter is set to -1.
 */
typedef struct {
    //! total size of RAM
    long total;
    //! max available RAM for allocation after initialization of of global/static variables
    long max_available;
    //! available RAM at runtime for dynamic allocation
    long available;
    //! Lowest registered available RAM since boot
    long min_available;
} gs_mem_ram_stat_t;

/**
   RAM types
   Defines the different RAM types (external/internal) supported on
   the various platforms.
 */
typedef enum {
    GS_MEM_RAM_TYPE_INTERNAL = 0,//!< Internal RAM type
    GS_MEM_RAM_TYPE_EXTERNAL     //!< External RAM type
} gs_mem_ram_type_t;

/**
   Get status of internal RAM

   @param[out] ram_stat RAM status, each member of struct is -1 if not supported on specific platform
   @return_gs_error_t
 */
gs_error_t gs_mem_get_int_ram_stat(gs_mem_ram_stat_t * ram_stat);

/**
   Get status of external RAM

   @param[out] ram_stat RAM status, each member of struct is -1 if not supported on specific platform
   @return_gs_error_t
 */
gs_error_t gs_mem_get_ext_ram_stat(gs_mem_ram_stat_t * ram_stat);


/**
   Get status of selected RAM

   @param[in]  type RAM type to query status for
   @param[out] ram_stat RAM status, each member of struct is -1 if not supported on specific platform
   @return_gs_error_t
 */
gs_error_t gs_mem_get_ram_stat(gs_mem_ram_type_t type, gs_mem_ram_stat_t * ram_stat);


/**
   Get default RAM type

   returns the default RAM type used for allocations (Heap).
   @return gs_mem_ram_type_t
 */
gs_mem_ram_type_t gs_mem_get_ram_default();


/**
   Print RAM status.

   @param[in] ram_stat RAM status
   @param[in] out output stream
   @return_gs_error_t
 */
gs_error_t gs_mem_print_ram_stat(gs_mem_ram_stat_t * ram_stat, FILE * out);

#ifdef __cplusplus
}
#endif
#endif
