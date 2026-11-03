/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */

#include <gs/util/drivers/sys/memory.h>
#include <gs/util/check.h>

static const char * long_to_string(char * buf, size_t buf_size, long lvalue)
{
    if (lvalue >= 0) {
        snprintf(buf, buf_size, "%ld", lvalue);
        return buf;
    }
    return "Unknown";
}

gs_error_t gs_mem_get_ram_stat(gs_mem_ram_type_t type, gs_mem_ram_stat_t * ram_stat)
{
    if (type == GS_MEM_RAM_TYPE_INTERNAL) {
        return gs_mem_get_int_ram_stat(ram_stat);
    } else if (type == GS_MEM_RAM_TYPE_EXTERNAL) {
        return gs_mem_get_ext_ram_stat(ram_stat);
    }

    /* Unsupported memory type */
    return GS_ERROR_NOT_SUPPORTED;
}

gs_error_t gs_mem_print_ram_stat(gs_mem_ram_stat_t * ram_stat, FILE * out)
{
    GS_CHECK_ARG(ram_stat != NULL);
    char buf[20];

    fprintf(out, "Total:             %s\r\n", long_to_string(buf, sizeof(buf), ram_stat->total));
    fprintf(out, "Max available:     %s\r\n", long_to_string(buf, sizeof(buf), ram_stat->max_available));
    fprintf(out, "Min available:     %s\r\n", long_to_string(buf, sizeof(buf), ram_stat->min_available));
    fprintf(out, "Available:         %s\r\n", long_to_string(buf, sizeof(buf), ram_stat->available));
    return GS_OK;
}
