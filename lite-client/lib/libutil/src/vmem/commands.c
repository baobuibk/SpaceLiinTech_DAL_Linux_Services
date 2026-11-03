/* Copyright (c) 2013-2017 GomSpace A/S. All rights reserved. */

#include <gs/util/gosh/command.h>
#include <gs/util/vmem.h>
#include <gs/util/hexdump.h>

static int cmd_vmem_read(gs_command_context_t * ctx)
{
    if (gs_vmem_get_map() == NULL) {
        return GS_ERROR_NOT_FOUND;
    }
    
    void * addr;
    if (gs_string_to_pointer(ctx->argv[1], &addr)) {
        return GS_ERROR_ARG;
    }

    uint32_t length;
    if (gs_string_to_uint32(ctx->argv[2], &length)) {
        return GS_ERROR_ARG;
    }

    char data[length];
    void* to = gs_vmem_cpy(data, addr, length);
    if (to == NULL) {
        return GS_ERROR_ARG;
    }

    gs_hexdump_to_stream(data, length, addr, ctx->out);

    return GS_OK;
}

static unsigned int to_int(char c)
{
    if (c >= '0' && c <= '9') return      c - '0';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    return -1;
}

static int cmd_vmem_write(gs_command_context_t * ctx)
{
    if (gs_vmem_get_map() == NULL) {
        return GS_ERROR_NOT_FOUND;
    }

    void * addr;
    if (gs_string_to_pointer(ctx->argv[1], &addr)) {
        return GS_ERROR_ARG;
    }

    int len = strlen(ctx->argv[2]) / 2;
    char data[len];

    for (int i = 0; (i < len); ++i) {
        data[i] = 16 * to_int(ctx->argv[2][2*i]) + to_int(ctx->argv[2][2*i+1]);
    }

    gs_vmem_cpy(addr, data, len);
    return GS_OK;
}

static int cmd_vmem_list(gs_command_context_t * ctx)
{
    return gs_vmem_list(ctx->out);
}

static int cmd_vmem_lock(gs_command_context_t * context)
{
    return gs_vmem_lock_by_name(context->argv[1], true);
}

static int cmd_vmem_unlock(gs_command_context_t * context)
{
    return gs_vmem_lock_by_name(context->argv[1], false);
}

static const gs_command_t GS_COMMAND_SUB cmd_vmem_sub[] = {
    {
        .name = "read",
        .help = "Read from virtual memory",
        .usage = "<addr> <length>",
        .handler = cmd_vmem_read,
        .mandatory_args = 2,
    },{
        .name = "write",
        .help = "Write to virtual memory",
        .usage = "<addr> <data>",
        .handler = cmd_vmem_write,
        .mandatory_args = 2,
    },{
        .name = "lock",
        .help = "Lock the virtual memory",
        .usage = "<entry>",
        .handler = cmd_vmem_lock,
        .mandatory_args = 1,
    },{
        .name = "unlock",
        .help = "Unlock the virtual memory",
        .usage = "<entry>",
        .handler = cmd_vmem_unlock,
        .mandatory_args = 1,
    },{
        .name = "list",
        .help = "Show virtual memory mappings",
        .handler = cmd_vmem_list,
        .mandatory_args = GS_COMMAND_NO_ARGS,
    }
};

static const gs_command_t GS_COMMAND_ROOT cmd_vmem[] = {
    {
        .name = "vmem",
        .help = "Virtual memory",
        .chain = GS_COMMAND_INIT_CHAIN(cmd_vmem_sub),
    },
};

gs_error_t gs_vmem_register_commands(void)
{
    return GS_COMMAND_REGISTER(cmd_vmem);
}
