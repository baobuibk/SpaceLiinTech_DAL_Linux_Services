/* Copyright (c) 2013 GomSpace A/S. All rights reserved. (AUTO GENERATED) */

#include "fp.h"

static const gs_command_t GS_COMMAND_SUB fp_commands[] = {
    {
        .name="server",
        .help="Setup CSP address of FP server.",
        .usage="<server>",
        .mandatory_args=1,
        .handler=cmd_handler_fp_server,
    },
    {
        .name="flush",
        .help="Flush current flight plan.",
        .mandatory_args=GS_COMMAND_NO_ARGS,
        .handler=cmd_handler_fp_flush,
    },
    {
        .name="load",
        .help="Load flight plan from file.",
        .usage="<path>",
        .mandatory_args=1,
        .handler=cmd_handler_fp_load,
    },
    {
        .name="store",
        .help="Store current flight plan to file.",
        .usage="<path>",
        .mandatory_args=1,
        .handler=cmd_handler_fp_store,
    },
    {
        .name="create",
        .help="Create new timer.",
        .usage="<timer> <[+]sec> <command> [repeat] [state]",
        .mandatory_args=3,
        .optional_args=2,
        .handler=cmd_handler_fp_create,
    },
    {
        .name="delete",
        .help="Delete timer.",
        .usage="<timer>",
        .mandatory_args=1,
        .handler=cmd_handler_fp_delete,
    },
    {
        .name="active",
        .help="Set timer active.",
        .usage="<timer>",
        .mandatory_args=1,
        .handler=cmd_handler_fp_active,
    },
    {
        .name="allactive",
        .help="Set all timers active.",
        .usage="<timer>",
        .mandatory_args=1,
        .handler=cmd_handler_fp_allactive,
    },
    {
        .name="dormant",
        .help="Set timer dormant.",
        .usage="<timer>",
        .mandatory_args=1,
        .handler=cmd_handler_fp_dormant,
    },
    {
        .name="repeat",
        .help="Set timer repeat.",
        .usage="<timer> <count>",
        .mandatory_args=2,
        .handler=cmd_handler_fp_repeat,
    },
    {
        .name="time",
        .help="Set execution time.",
        .usage="<timer> <[+]sec>",
        .mandatory_args=2,
        .handler=cmd_handler_fp_time,
    },
    {
        .name="list",
        .help="List timers.",
        .mandatory_args=GS_COMMAND_NO_ARGS,
        .handler=cmd_handler_fp_list,
    },
};

static const gs_command_t GS_COMMAND_ROOT root_commands[] = {
    {
        .name="fp",
        .help="Flight planner",
        .chain=GS_COMMAND_INIT_CHAIN(fp_commands),
    },
};

gs_error_t gs_fp_register_commands(void)
{
    return GS_COMMAND_REGISTER(root_commands);
}
