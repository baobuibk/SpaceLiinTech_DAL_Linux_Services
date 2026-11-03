/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */
/**
 * NanoCom firmware
 *
 */

#include <alloca.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <command/command.h>
#include <param/param_string.h>
#include <util/byteorder.h>
#include <ax100.h>
#include <csp/csp.h>
#include <csp/csp_endian.h>
#include <ax100_cmd.h>

int node_com = 5;

int cmd_ax100_node(struct command_context * ctx) {
	if (ctx->argc != 2)
		return CMD_ERROR_SYNTAX;
	node_com = atoi(ctx->argv[1]);
	return CMD_ERROR_NONE;
}

int cmd_ax100_hk(struct command_context * ctx) {
	param_index_t com_hk;
        memset(&com_hk, 0, sizeof(com_hk));
	com_hk.physaddr = alloca(AX100_TELEM_SIZE);
	com_hk.size = AX100_TELEM_SIZE;
	if (ax100_get_hk(&com_hk, node_com, 1000) < 0)
		return CMD_ERROR_FAIL;
	param_list(&com_hk, 1);
	return CMD_ERROR_NONE;
}

int cmd_ax100_gndwdt_clear(struct command_context * ctx) {
	csp_transaction(CSP_PRIO_HIGH, node_com, AX100_PORT_GNDWDT_RESET, 1000, NULL, 0, NULL, 0);
	return CMD_ERROR_NONE;
}

int cmd_ax100_set_hmac(struct command_context * ctx) {
    if (ctx->argc != 3) {
        return CMD_ERROR_SYNTAX;
    }
    int hmac_idx = atoi(ctx->argv[1]);
    if (hmac_idx < 0 || hmac_idx >= AX100_HMACS_TABLE_COUNT) {
        return CMD_ERROR_SYNTAX;
    }
    int hmac_delay = atoi(ctx->argv[2]);
    if (hmac_delay < 0 || hmac_delay > HMAC_MAX_DELAY_SEC) {
        return CMD_ERROR_SYNTAX;
    }

    hmac_cmd_t cmd;
    cmd.delay = util_hton32((uint32_t)hmac_delay);
    cmd.idx = hmac_idx;
    csp_transaction(CSP_PRIO_NORM, node_com, AX100_PORT_HMAC_SET, 1000, &cmd, sizeof(hmac_cmd_t), NULL, 0);

    return CMD_ERROR_NONE;
}

command_t ax100_commands[] = {
	{
		.name = "node",
		.help = "Set node",
		.usage = "<node>",
		.handler = cmd_ax100_node,
	},{
		.name = "hk",
		.help = "Get HK",
		.handler = cmd_ax100_hk,
	},{
		.name = "gndwdt_clear",
		.help = "Clear",
		.handler = cmd_ax100_gndwdt_clear,
	},{
        .name = "hmac",
        .help = "Set HMAC + time delay",
        .usage = "<id> <delay_sec>",
        .handler = cmd_ax100_set_hmac,
    }
};

command_t __root_command ax100_root_command[] = {
	{
		.name = "ax100",
		.help = "client: NanoCom AX100",
		.chain = INIT_CHAIN(ax100_commands),
	},
};

void cmd_ax100_setup(void) {
	command_register(ax100_root_command);
}

