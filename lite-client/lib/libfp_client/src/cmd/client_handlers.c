/* Copyright (c) 2013-2017 GomSpace A/S. All rights reserved. */

#include <gs/csp/port.h>
#include <gs/fp/fp_client.h>

#define GS_FP_INTERNAL_USE 1
#include "gs/fp/internal/types.h"

#include "fp.h"
#include <gs/util/error.h>
#include <gs/util/gosh/command.h>
#include <gs/util/timestamp.h>
#include <gs/util/clock.h>
#include <gs/util/stdio.h>
#include <csp/csp.h>
#include <csp/csp_endian.h>
#include <stdlib.h>

int cmd_handler_fp_server(gs_command_context_t *ctx)
{
    uint8_t server = atoi(ctx->argv[1]);

    return fp_client_host(server, GS_CSP_PORT_FP) < 0 ? GS_ERROR_UNKNOWN : GS_OK;
}

int cmd_handler_fp_load(gs_command_context_t *ctx)
{
    // Path too long
    if (strnlen(ctx->argv[1], FP_MAX_PATH_LENGTH) == FP_MAX_PATH_LENGTH) {
        return GS_ERROR_ARG;
    }

    char path[FP_MAX_PATH_LENGTH];
    strcpy(path, ctx->argv[1]);

    return fp_client_fp_load(path) < 0 ? GS_ERROR_UNKNOWN : GS_OK;
}

int cmd_handler_fp_store(gs_command_context_t *ctx)
{
    // Path too long
    if (strnlen(ctx->argv[1], FP_MAX_PATH_LENGTH) == FP_MAX_PATH_LENGTH) {
        return GS_ERROR_ARG;
    }

    char path[FP_MAX_PATH_LENGTH];
    strcpy(path, ctx->argv[1]);

    return fp_client_fp_store(path) < 0 ? GS_ERROR_UNKNOWN : GS_OK;
}

int cmd_handler_fp_flush(gs_command_context_t *ctx)
{
    return fp_client_fp_flush() < 0 ? GS_ERROR_UNKNOWN : GS_OK;
}

int cmd_handler_fp_create(gs_command_context_t *ctx)
{
    char *timer = ctx->argv[1];
    char *when = ctx->argv[2];
    char *cmd = ctx->argv[3];

    // Check if key or command is too long
    if ((strnlen(timer, FP_KEY_SIZE) == FP_KEY_SIZE) || (strnlen(cmd, FP_CMD_SIZE) == FP_CMD_SIZE)) {
        return GS_ERROR_ARG;
    }

    uint16_t repeat = 1;
    if (ctx->argc > 4) {
        repeat = atoi(ctx->argv[4]);
    }

    fp_timer_state_t s = FP_TIME_ACTIVE;
    if (ctx->argc > 5) {
        if (!strcmp(ctx->argv[5], "active")) {
            s = FP_TIME_ACTIVE;
        }
        else if (!strcmp(ctx->argv[5], "dormant")) {
            s = FP_TIME_DORMANT;
        }
        else {
            return GS_ERROR_ARG;
        }
    }

    fp_timer_basis_t b;

    uint32_t sec;
    if (when[0] == '+') {
        b = FP_TIME_RELATIVE;
        sec = atoi(when + 1);
    } else {
        b = FP_TIME_ABSOLUTE;
        sec = atoi(when);
    }

    gs_timestamp_t t;
    t.tv_sec = sec;
    t.tv_nsec = 0;

    return fp_client_timer_create(timer, cmd, b, s, &t, repeat) < 0 ? GS_ERROR_UNKNOWN : GS_OK;
}

int cmd_handler_fp_delete(gs_command_context_t *ctx)
{
    char *timer = ctx->argv[1];
    if (strnlen(timer, FP_KEY_SIZE) == FP_KEY_SIZE) {
        return GS_ERROR_ARG;
    }

    return fp_client_timer_delete(timer) < 0 ? GS_ERROR_UNKNOWN : GS_OK;
}

int cmd_handler_fp_active(gs_command_context_t *ctx)
{
    char * timer = ctx->argv[1];
    if (strnlen(timer, FP_KEY_SIZE) == FP_KEY_SIZE) {
        return GS_ERROR_ARG;
    }

    return fp_client_timer_set_active(timer) < 0 ? GS_ERROR_UNKNOWN : GS_OK;
}

int cmd_handler_fp_allactive(gs_command_context_t *ctx)
{
    return fp_client_timer_set_all_active() < 0 ? GS_ERROR_UNKNOWN : GS_OK;
}

int cmd_handler_fp_dormant(gs_command_context_t *ctx)
{
    char * timer = ctx->argv[1];
    if (strnlen(timer, FP_KEY_SIZE) == FP_KEY_SIZE) {
        return GS_ERROR_ARG;
    }

    return fp_client_timer_set_dormant(timer) < 0 ? GS_ERROR_UNKNOWN : GS_OK;
}

int cmd_handler_fp_repeat(gs_command_context_t *ctx)
{
    char * timer = ctx->argv[1];
    if (strnlen(timer, FP_KEY_SIZE) == FP_KEY_SIZE) {
        return GS_ERROR_ARG;
    }

    uint16_t count = atoi(ctx->argv[2]);

    return fp_client_timer_set_repeat(timer, count) < 0 ? GS_ERROR_UNKNOWN : GS_OK;
}

int cmd_handler_fp_time(gs_command_context_t *ctx)
{
    char * timer = ctx->argv[1];
    if (strnlen(timer, FP_KEY_SIZE) == FP_KEY_SIZE) {
        return GS_ERROR_ARG;
    }

    char * when = ctx->argv[2];

    fp_timer_basis_t b;

    uint32_t sec;
    if (when[0] == '+') {
        b = FP_TIME_RELATIVE;
        sec = atoi(when + 1);
    } else {
        b = FP_TIME_ABSOLUTE;
        sec = atoi(when);
    }

    gs_timestamp_t t;
    t.tv_sec = sec;
    t.tv_nsec = 0;

    return fp_client_timer_set_time(timer, b, &t) < 0 ? GS_ERROR_UNKNOWN : GS_OK;
}

int cmd_handler_fp_list(gs_command_context_t *ctx)
{
    return fp_client_timer_list() < 0 ? GS_ERROR_UNKNOWN : GS_OK;
}
