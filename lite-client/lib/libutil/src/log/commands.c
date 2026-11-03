/* Copyright (c) 2013-2017 GomSpace A/S. All rights reserved. */

#include <gs/util/bytebuffer.h>
#include <gs/util/gosh/command.h>
#include <gs/util/log/log.h>
#include <gs/util/log/appender/appender.h>
#include "local.h"
#include <stdlib.h>


// iterator context
typedef struct {
    gs_command_context_t * ctx;
    gs_log_group_t * first;
    gs_log_appender_t * first_appender;
    bool completer;
    bool detailed;
} iter_group_t;

#define FORMAT_BUF_SIZE 10
static const char * format_mask(uint8_t mask, char * buf)
{
    snprintf(buf, FORMAT_BUF_SIZE, "%c%c%c%c%c%c",
             (mask & LOG_ERROR_MASK)    ? 'E' : '.',
             (mask & LOG_WARNING_MASK)  ? 'W' : '.',
             (mask & LOG_NOTICE_MASK)   ? 'N' : '.',
             (mask & LOG_INFO_MASK)     ? 'I' : '.',
             (mask & LOG_DEBUG_MASK)    ? 'D' : '.',
             (mask & LOG_TRACE_MASK)    ? 'T' : '.');
    return buf;
}

static bool iter_print_group_appenders(void * ctx_in, gs_log_appender_t * appender)
{
    gs_bytebuffer_t *bb = ctx_in;
    gs_bytebuffer_printf(bb, "%s,", appender->name);
    return true;
}

static bool iter_print_group(void * ctx_in, gs_log_group_t * group)
{
    iter_group_t * ctx = ctx_in;
    char level_mask[FORMAT_BUF_SIZE];
    if (!ctx->completer) {
        char appender_str[128] = "\0";
        gs_bytebuffer_t bb;
        gs_bytebuffer_init(&bb, appender_str, sizeof(appender_str));
        gs_log_group_appender_iterate(group, &bb, iter_print_group_appenders);
        if (ctx->detailed) {
            gs_command_set_output_printf(ctx->ctx, group->name, "category", "0x%08x", group->category);
            gs_command_set_output_printf(ctx->ctx, group->name, "mask", "%-6s (0x%02x)", format_mask(group->mask, level_mask), group->mask);
            gs_command_set_output_printf(ctx->ctx, group->name, "appenders", appender_str);
        } else {
            gs_command_set_output_printf(ctx->ctx, NULL, NULL, "%-15s %-6s  %s", group->name, format_mask(group->mask, level_mask), appender_str);
        }
    } else {
        fprintf(ctx->ctx->out, "  %-15s %-6s\r\n",
                group->name,
                format_mask(group->mask, level_mask));
    }
    return true;
}

static int cmd_log_group_list(gs_command_context_t * ctx)
{
    iter_group_t iter = {.ctx = ctx, .completer = false};

    if (ctx->argc > 1) {
        iter.detailed = true;
        gs_log_group_iterate(ctx->argv[1], &iter, iter_print_group);
    } else {
        fprintf(ctx->out, "Group           Mask    Appenders\r\n");
        gs_log_group_iterate("*", &iter, iter_print_group);
    }
    return GS_OK;
}

static bool iter_print_appender(void * ctx_in, gs_log_appender_t * appender)
{
    iter_group_t * ctx = ctx_in;
    char level_mask[FORMAT_BUF_SIZE];
    if (!ctx->completer) {
        if (ctx->detailed) {
            gs_command_set_output_printf(ctx->ctx, appender->name, "mask", "%-6s (0x%02x)", format_mask(appender->mask, level_mask), appender->mask);

            if (appender->drv->info) {
                char info_str[100];
                appender->drv->info(appender, info_str, sizeof(info_str));
                gs_command_set_output(ctx->ctx, appender->name, "info", info_str);
            }
        } else {
            gs_command_set_output_printf(ctx->ctx, NULL, NULL, "%-15s %-6s", appender->name, format_mask(appender->mask, level_mask));
        }
    } else {
        fprintf(ctx->ctx->out, "  %-15s %-6s\r\n",
                appender->name,
                format_mask(appender->mask, level_mask));
    }
    return true;
}

static int cmd_log_appender_list(gs_command_context_t * ctx)
{
    iter_group_t iter = {.ctx = ctx, .completer = false};

    if (ctx->argc > 1) {
        iter.detailed = true;
        gs_log_appender_iterate(ctx->argv[1], &iter, iter_print_appender);
    } else {
        fprintf(ctx->out, "Appender        Mask\r\n");
        gs_log_appender_iterate("*", &iter, iter_print_appender);
    }
    return GS_OK;
}

typedef gs_error_t (*log_get_mask_t)(const char *name, uint8_t* mask);
typedef gs_error_t (*log_set_mask_t)(const char *name, uint8_t mask);

static int cmd_log_mask_handler(gs_command_context_t * ctx, log_get_mask_t get_mask, log_set_mask_t set_mask)
{
    /* strtok writes to the string, so we need to duplicate it to avoid writing to read-only memory */
    char strbuf[100];
    GS_STRNCPY(strbuf, ctx->argv[1]);

    char * saveptr = NULL;
    char * token = strtok_r(strbuf, ",", &saveptr);
    gs_error_t error = GS_OK;
    while (token && (error == GS_OK)) {

        uint8_t old_mask = 0;
        if (gs_log_is_group_all(token) == false) {
            error = get_mask(token, &old_mask);
        }
        if (error == GS_OK) {
            uint8_t new_mask = 0;
            error = gs_log_string_to_mask(ctx->argv[2], old_mask, &new_mask);
            if (error == GS_OK) {
                error = set_mask(token, new_mask);
            }
        }

        token = strtok_r(NULL, ",", &saveptr);
    }

    return error;
}

static int cmd_log_group_mask(gs_command_context_t * ctx)
{
    return cmd_log_mask_handler(ctx, gs_log_group_get_level_mask, gs_log_group_set_level_mask);
}

static int cmd_log_appender_mask(gs_command_context_t * ctx)
{
    return cmd_log_mask_handler(ctx, gs_log_appender_get_level_mask, gs_log_appender_set_level_mask);
}


#ifndef __AVR__
static bool iter_log_completer(void *ctx_in, gs_log_group_t * group)
{
    iter_group_t * ctx = ctx_in;
    unsigned int hits = gs_command_completer_add_token(ctx->ctx, group->name, false);
    if (hits == 1) {
        ctx->first = group;
    } else {
        if (hits == 2) {
            fprintf(ctx->ctx->out, "\r\n");
            iter_print_group(ctx, ctx->first);
        }
        iter_print_group(ctx, group);
    }
    return true;
}

static gs_error_t cmd_log_group_completer(gs_command_context_t * ctx, int arg_to_complete)
{
    if (arg_to_complete == 1) {
        iter_group_t iter = {.ctx = ctx, .completer = true};
        char name[50];
        snprintf(name, sizeof(name), "%s*", (ctx->argc > 1) ? ctx->argv[1] : "");
        gs_log_group_iterate(name, &iter, iter_log_completer);
        return GS_OK;
    }
    return GS_ERROR_AMBIGUOUS;
}

static bool iter_log_appender_completer(void *ctx_in, gs_log_appender_t * appender)
{
    iter_group_t * ctx = ctx_in;
    unsigned int hits = gs_command_completer_add_token(ctx->ctx, appender->name, false);
    if (hits == 1) {
        ctx->first_appender = appender;
    } else {
        if (hits == 2) {
            fprintf(ctx->ctx->out, "\r\n");
            iter_print_appender(ctx, ctx->first_appender);
        }
        iter_print_appender(ctx, appender);
    }
    return true;
}

static gs_error_t cmd_log_appender_completer(gs_command_context_t * ctx, int arg_to_complete)
{
    if (arg_to_complete == 1) {
        iter_group_t iter = {.ctx = ctx, .completer = true};
        char name[50];
        snprintf(name, sizeof(name), "%s*", (ctx->argc > 1) ? ctx->argv[1] : "");
        gs_log_appender_iterate(name, &iter, iter_log_appender_completer);
        return GS_OK;
    }
    return GS_ERROR_AMBIGUOUS;
}
#endif

typedef struct {
    gs_command_context_t *cmd_ctx;
    unsigned int count;
} hist_ctx_t;

static bool appender_history_iter(void *ctx, gs_log_level_t level, const gs_timestamp_t *ts, const char *group, const char *msg)
{
    hist_ctx_t * hist_ctx = ctx;

    /* Break iteration if history record count is reached. */
    if (hist_ctx->count-- == 0) {
        return false;
    }

    gs_command_set_output_printf(hist_ctx->cmd_ctx, NULL, NULL,
            "%s%04"PRIu32".%06"PRIu32" %c %s: %s%s",
            gs_log_level_to_color_begin(level),
            ts->tv_sec, ts->tv_nsec/1000,
            gs_log_level_to_char(level),
            group,
            msg,
            gs_log_level_to_color_end());

    return true;
}

static int cmd_log_appender_hist(gs_command_context_t * ctx)
{
    hist_ctx_t hist_ctx = {.cmd_ctx = ctx, .count = 20};
    if (ctx->argc == 3) {
        hist_ctx.count = atoi(ctx->argv[2]);
    }

    return gs_log_appender_history_iterate(ctx->argv[1], &hist_ctx, appender_history_iter);
}


static bool iter_log_group_find(void* ctx_in, gs_log_group_t *group)
{
    gs_log_group_t **grp = ctx_in;
    *grp = group;
    return false;
}

static int cmd_log_insert(gs_command_context_t * ctx)
{
    gs_log_group_t *log_group = NULL;
    gs_error_t error = gs_log_group_iterate(ctx->argv[1], &log_group, iter_log_group_find);
    if (error != GS_OK) {
        return error;
    }

    gs_log_level_t level;
    error = gs_log_string_to_level(ctx->argv[2], &level);
    if (error == GS_OK) {
        gs_log(level, log_group, GS_PGM_STR("%s"), ctx->argv[3]);
    }

    return error;
}

static int cmd_log_color(gs_command_context_t * ctx)
{
    bool color;
    gs_error_t error = gs_string_to_bool(ctx->argv[1], &color);
    if (error == GS_OK) {
        gs_log_set_print_color(color);
    }

    return error;
}

static const gs_command_t GS_COMMAND_SUB cmd_log_group_cmds[] = {
    {
        .name = "list",
        .help = "list log groups",
        .usage = "[group]",
#ifndef __AVR__
        .completer = cmd_log_group_completer,
#endif
        .handler = cmd_log_group_list,
        .mandatory_args = GS_COMMAND_NO_ARGS,
        .optional_args = 1,
    },{
        .name = "mask",
        .help = "Set log group mask(s): e|w|i|d|t|stand|all|non",
        .usage = "<group>[,group] <[+-]level>[,level]",
#ifndef __AVR__
        .completer = cmd_log_group_completer,
#endif
        .handler = cmd_log_group_mask,
        .mandatory_args = 2,
    },{
        .name = "insert",
        .help = "Log message",
        .usage = "<group> <level> <message>",
#ifndef __AVR__
        .completer = cmd_log_group_completer,
#endif
        .handler = cmd_log_insert,
        .mandatory_args = 3,
    },{
        .name = "color",
        .help = "Enable/disable color logs (stdout)",
        .usage = "<true|false>",
        .handler = cmd_log_color,
        .mandatory_args = 1,
    }
};

static const gs_command_t GS_COMMAND_SUB cmd_log_appender_cmds[] = {
    {
        .name  = "list",
        .help  = "list log appenders",
        .usage = "[appender]",
#ifndef __AVR__
        .completer = cmd_log_appender_completer,
#endif
        .handler = cmd_log_appender_list,
        .mandatory_args = GS_COMMAND_NO_ARGS,
        .optional_args = 1,
    }, {
        .name = "mask",
        .help = "Set log appender mask(s): e|w|i|d|t|stand|all|non",
        .usage = "<appender>[,appender] <[+-]level>[,level]",
#ifndef __AVR__
        .completer = cmd_log_appender_completer,
#endif
        .handler = cmd_log_appender_mask,
        .mandatory_args = 2,
    }, {
        .name = "hist",
        .help = "Show log appender history",
        .usage = "<appender> [cnt]",
#ifndef __AVR__
        .completer = cmd_log_appender_completer,
#endif
        .handler = cmd_log_appender_hist,
        .mandatory_args = 1,
        .optional_args = 1,
    }
};

static const gs_command_t GS_COMMAND_SUB cmd_log_cmds[] = {
    {
        .name = "group",
        .help = "log group commands",
        .chain = GS_COMMAND_INIT_CHAIN(cmd_log_group_cmds),
    }, {
        .name = "appender",
        .help = "log appender commands",
        .chain = GS_COMMAND_INIT_CHAIN(cmd_log_appender_cmds),
    }
};

static const gs_command_t GS_COMMAND_ROOT cmd_log[] = {
    {
        .name = "log",
        .help = "log: Log system",
        .chain = GS_COMMAND_INIT_CHAIN(cmd_log_cmds)
    },{
        .name = "debug",
        .help = "Set Log group mask(s): e|w|n|i|d|t|stand|all|off",
        .usage = "<group>[,group] <[+-]level>[,level]",
#ifndef __AVR__
        .completer = cmd_log_group_completer,
#endif
        .handler = cmd_log_group_mask,
        .mandatory_args = 2,
    },
};

gs_error_t gs_log_register_commands(void)
{
    return GS_COMMAND_REGISTER(cmd_log);
}
