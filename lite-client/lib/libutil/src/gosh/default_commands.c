/* Copyright (c) 2013-2017 GomSpace A/S. All rights reserved. */

#include "command_local.h"
#include "console_local.h"

#include <stdlib.h>

#if defined(__linux__)
#include <unistd.h>
#include <termios.h>
#endif

#include <gs/util/stdio.h>
#include <gs/util/clock.h>
#include <gs/util/time.h>
#include <gs/util/string.h>
#include <conf_util.h>

static int cmd_help(gs_command_context_t * context)
{
    return gs_command_show_help(gs_command_args(context), context->out);
}

static int cmd_sleep(gs_command_context_t * context)
{
    uint32_t sleep_ms;
    gs_error_t error = gs_string_to_uint32(context->argv[1], &sleep_ms);
    if (error) {
        return error;
    }

    gs_time_sleep_ms(sleep_ms);

    return GS_OK;
}

static int cmd_watch(gs_command_context_t * context, bool check_error)
{
    uint32_t sleep_ms;
    gs_error_t error = gs_string_to_uint32(context->argv[1], &sleep_ms);
    if (error) {
        return error;
    }

    fprintf(context->out, "Execution delay: %" PRIu32 "\r\n", sleep_ms);

    char * new_command = strstr(gs_command_args(context), " ");
    if (new_command == NULL) {
        return GS_ERROR_ARG;
    } else {
        new_command = new_command + 1;
    }

    fprintf(context->out, "Command: %s\r\n", new_command);

    while(1) {

        gs_error_t cmd_result;
        error = gs_command_execute(new_command, &cmd_result, context->out, context->io_functions, context->io_ctx);
        if (error) {
            return error;
        }
        if (check_error && cmd_result) {
            return cmd_result;
        }

        if (gs_stdio_getchar_timed(sleep_ms, NULL) != GS_ERROR_TIMEOUT) {
            break;
        }
    }

    return GS_OK;
}

static int cmd_watch_nocheck(gs_command_context_t * context)
{
    return cmd_watch(context, false);
}

static int cmd_watch_check(gs_command_context_t * context)
{
    return cmd_watch(context, true);
}

#define CONTROL(X)  ((X) - '@')

static int cmd_batch(gs_command_context_t * ctx)
{
    char c;
    int quit = 0, execute = 0;
    unsigned int batch_size = 100;
    unsigned int batch_input = 0;
    unsigned int batch_count = 0;
    char * batch[20] = {};
    printf("Type each command followed by enter, hit ctrl+e to end typing, ctrl+x to cancel:\r\n");

    /* Wait for ^q to quit. */
    while (quit == 0) {

        /* Get character */
        c = getchar();

        switch (c) {

            /* CTRL + X */
            case 0x18:
                quit = 1;
                break;

		/* CTRL + E */
            case 0x05:
                execute = 1;
                quit = 1;
                break;

		/* Backspace */
            case CONTROL('H'):
            case 0x7f:
                if (batch_input > 0) {
                    putchar('\b');
                    putchar(' ');
                    putchar('\b');
                    batch_input--;
                }
                break;

            case '\r':
                putchar('\r');
                putchar('\n');
                if ((batch[batch_count] != NULL) && (batch_input < batch_size))
                    batch[batch_count][batch_input++] = '\r';
                if ((batch[batch_count] != NULL) && (batch_input < batch_size))
                    batch[batch_count][batch_input++] = '\0';
                batch_count++;
                batch_input = 0;
                if (batch_count == 20)
                    quit = 1;
                break;

            default:
                putchar(c);
                if (batch[batch_count] == NULL) {
                    batch[batch_count] = calloc(GS_CONSOLE_INPUT_LEN+1, 1);
                }

                if ((batch[batch_count] != NULL) && (batch_input < batch_size))
                    batch[batch_count][batch_input++] = c;
                break;
        }
    }

    if (execute) {
        printf("\r\n");
        for (unsigned int i = 0; i <= batch_count; i++) {
            if (batch[i])
                printf("[%02u] %s\r\n", i, batch[i]);
        }
        printf("Press ctrl+e to execute, or any key to abort\r\n");
        c = getchar();
        if (c != 0x05)
            execute = 0;
    }

    /* Run/Free batch job */
    for (unsigned int i = 0; i <= batch_count; i++) {
        if (execute && batch[i]) {
            printf("EXEC [%02u] %s\r\n", i, batch[i]);
            gs_command_run(batch[i], NULL);
        }
        free(batch[i]);
    }

    return GS_OK;
}

#if defined(__linux__)
static int cmd_exit(gs_command_context_t * context)
{
    gs_console_exit();
    exit(EXIT_SUCCESS);
    return GS_OK;
}
#endif

static int cmd_clock(gs_command_context_t * ctx)
{
    if (ctx->argc > 1) {
        gs_timestamp_t ts;
        gs_error_t error = gs_clock_from_string(ctx->argv[1], &ts);
        if (error) {
            return GS_ERROR_ARG;
        }
        error = gs_clock_set_time(&ts);
        if (error) {
            fprintf(ctx->out, "Failed to set time, error=%s\r\n", gs_error_string(error));
            return GS_ERROR_DATA;
        }
    }

    timestamp_t clock;
    gs_clock_get_monotonic(&clock);
    fprintf(ctx->out, "monotonic: %10"PRIu32".%09"PRIu32" sec\r\n", clock.tv_sec, clock.tv_nsec);
    gs_command_set_output_printf(ctx, "", "monotonic", "%10"PRIu32".%09"PRIu32"", clock.tv_sec, clock.tv_nsec);

    char tbuf[25];
    gs_clock_get_time(&clock);
    gs_clock_to_iso8601_string(&clock, tbuf, sizeof(tbuf));
    fprintf(ctx->out, "realtime:  %10"PRIu32".%09"PRIu32" sec -> %s\r\n", clock.tv_sec, clock.tv_nsec, tbuf);
    gs_command_set_output_printf(ctx, "", "realtime", "%10"PRIu32".%09"PRIu32"", clock.tv_sec, clock.tv_nsec);

    return GS_OK;
}

static int cmd_console_mode(gs_command_context_t * ctx)
{
    return gs_console_change_mode(ctx->argv[1]);
}

static const gs_command_t GS_COMMAND_ROOT cmd_default[] = {
    {
        .name = "help",
        .help = "Show help",
        .usage = "[command[ subcommand[ arg ...]]]",
        .handler = cmd_help,
        .optional_args = 100,
    },{
        .name = "sleep",
        .help = "Sleep X ms",
        .usage = "<mS>",
        .handler = cmd_sleep,
        .mandatory_args = 1,
    },{
        .name = "watch",
        .help = "Run commands at intervals (abort with key)",
        .usage = "<interval mS> <command> [arg ...]",
        .handler = cmd_watch_nocheck,
        .mandatory_args = 2,
        .optional_args = 100,
    },{
        .name = "watch_check",
        .help = "Like 'watch', but abort if command fails",
        .usage = "<interval mS> <command [arg ...]>",
        .handler = cmd_watch_check,
        .mandatory_args = 2,
        .optional_args = 100,
    },{
        .name = "batch",
        .help = "Run multiple commands",
        .handler = cmd_batch,
        .mode = GS_COMMAND_FLAG_HIDDEN,
    },{
        .name = "clock",
        .help = "Get/set system clock",
        .usage = "[<sec.nsec> | <YYYY-MM-DDTHH:MM:SSZ>]",
        .handler = cmd_clock,
        .optional_args = 1,
    },{
        .name = "console_mode",
        .help = "Console mode(s): cci",
        .usage = "<mode>",
        .handler = cmd_console_mode,
        .mode = GS_COMMAND_FLAG_HIDDEN,
        .mandatory_args = 1,
    },
#if defined(__linux__)
    {
        .name = "exit",
        .help = "Exit program",
        .handler = cmd_exit,
    },
#endif
};

gs_error_t gs_command_register_default_commands(void)
{
    return GS_COMMAND_REGISTER(cmd_default);
}
