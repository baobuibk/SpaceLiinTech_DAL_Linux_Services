/* Copyright (c) 2013-2017 GomSpace A/S. All rights reserved. */

/*
   Lite variant of csp-client.

   Only registers rparam, rgosh and the default CSP commands (ping, rps,
   memfree, buffree, reboot, shutdown, uptime, cmp, route, ifc, conn,
   rdpopt). Skips FTP, G-script, Flight Planner, Housekeeping, GSSB and all
   the hardware-specific product interfaces (ADCS, AX100, P60*, EPS, BPX,
   GATOSS, NanoCam, z7000, Aardvark) that gs-client registers by default.

   In addition to the normal interactive console, a command can be passed
   directly on the command line (after the CSP options). If present, it is
   executed once, the result is printed, and the process exits - e.g.:

       csp-client-lite -c=can1 ping 12
       csp-client-lite -c=can1 rparam getall 12 4
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gs/param/rparam.h>
#include <gs/rgosh/rgosh.h>
#include <gs/csp/csp.h>
#include <gs/csp/command.h>
#include <gs/csp/service_dispatcher.h>
#include <gs/csp/linux/command_line.h>
#include <gs/csp/router.h>
#include <gs/util/linux/command_line.h>
#include <gs/util/gosh/command.h>
#include <gs/util/gosh/console.h>
#include <gs/util/error.h>

static void csp_service_dispatcher(void)
{
    static const gs_csp_service_handler_t handlers[31] = {
        GS_CSP_BASIC_SERVICE_HANDLERS,
    };

    // Configuration for service handler - must remain valid as long as service handler is running
    static const gs_csp_service_dispatcher_conf_t conf = {
        .name = "CSPCLIENTLITESRV",
        .bind_any = true, // bind to any un-bounded ports, used for logging unexpected connections
        .disable_watchdog = true,
        .handler_array = handlers,
        .handler_array_size = GS_ARRAY_SIZE(handlers),
    };

    gs_csp_service_dispatcher_create(&conf, 0, GS_THREAD_PRIORITY_NORMAL, NULL);
}

// Initialize command-framework and register the trimmed command set (GOSH).
static void init_and_register_commands(void)
{
    gs_command_init(0);

    gs_rparam_register_commands();
    gs_csp_register_commands();
    gs_rgosh_register_commands();
}

// Join argv[start..argc) into a single command line, quoting tokens that contain spaces.
static bool build_command_line(int argc, char ** argv, int start, char * out, size_t out_size)
{
    out[0] = '\0';
    for (int i = start; i < argc; i++) {
        char token[256];
        if (strchr(argv[i], ' ')) {
            snprintf(token, sizeof(token), "\"%s\"", argv[i]);
        } else {
            snprintf(token, sizeof(token), "%s", argv[i]);
        }
        if ((strlen(out) + strlen(token) + 2) > out_size) {
            return false;
        }
        if (out[0]) {
            strcat(out, " ");
        }
        strcat(out, token);
    }
    return true;
}

// Run a single command, report the result the way the interactive console would, and return a process exit code.
static int run_one_shot(const char * command_line)
{
    gs_error_t result = GS_OK;
    gs_error_t error = gs_command_execute_stdio(command_line, &result);

    if (error == GS_ERROR_NOT_FOUND) {
        fprintf(stderr, "Unknown command '%s'\n", command_line);
        return 1;
    } else if ((error == GS_ERROR_TYPE) || (error == GS_ERROR_ARG)) {
        fprintf(stderr, "Invalid arguments for '%s'\n", command_line);
        return 1;
    } else if (error) {
        fprintf(stderr, "Command '%s' did not execute: %s (%d)\n", command_line, gs_error_string(error), error);
        return 1;
    } else if (result) {
        fprintf(stderr, "Command '%s' returned error: %s (%d)\n", command_line, gs_error_string(result), result);
        return 1;
    }
    return 0;
}

int main(int argc, char * argv[])
{
    // Add command line options for sub-modules and parse command line.
    // arg_index is set to the first argument that isn't consumed as an option,
    // i.e. the start of an (optional) one-shot command.
    const struct argp_child argp_childern[] = {
        gs_help_command_line_argp,
        gs_csp_command_line_options,
        {0},
    };
    static const char doc[] =
        "Lite Product Interface Application - only rparam, rgosh and the default CSP commands "
        "(ping, rps, memfree, buffree, reboot, shutdown, uptime, cmp, route, ifc, conn, rdpopt)."
        "\v"
        "One-shot mode:\n"
        "  Pass a command after the options and it runs once, prints the result, and exits -\n"
        "  no need to enter the interactive console. Without a command, it falls back to the\n"
        "  normal interactive console (type 'exit' to quit).\n"
        "\n"
        "Shortcuts (combine normally-separate steps into a single self-contained one-shot\n"
        "command - plain 'rparam get'/'set' only work after 'download'/'init' in the SAME\n"
        "process, i.e. inside the interactive console, so they need these instead):\n"
        "  rparam getall [node] [table-id]            download spec (if given), then read the whole table\n"
        "  rparam getval <node> <table-id> <param>    download spec, get, and send in one call\n"
        "  rparam setval <node> <table-id> <param> <value> [value...]\n"
        "                                              download spec, set, and send in one call\n"
        "  rgosh run [node] <command>                 set the rgosh server (if given), then run\n"
        "\n"
        "Examples:\n"
        "  csp-client-lite -c=can1 ping 12\n"
        "  csp-client-lite -c=can1 rparam getall 12 4\n"
        "  csp-client-lite -c=can1 rparam getval 12 4 bme_temp\n"
        "  csp-client-lite -c=can1 rparam setval 11 4 ft_chunk_size 512\n"
        "  csp-client-lite -c=can1 rgosh run 12 \"ntc_read_all\"\n"
        "\n"
        "Run '<binary> [options] help' for the full list of registered commands, or\n"
        "'<binary> [options] help <command>' (e.g. 'help rparam getall') for a command's usage.\n"
        "See also the 'lite-client' wrapper script for shorter verbs (e.g. 'getall' instead\n"
        "of 'rparam getall').";
    const struct argp argp = {
        .doc = doc, .children = argp_childern,
    };
    int arg_index = argc;
    gs_argp_parse(&argp, argc, argv, 0, &arg_index, GS_BUILD_INFO_REVISION);

    // Initialize command-framework and register commands (GOSH).
    init_and_register_commands();

    // Initialize CSP
    {
        gs_csp_conf_t conf;
        gs_csp_conf_get_defaults_server(&conf);

        conf.hostname = "csp-client-lite";
        conf.model = "CSP-client-lite";
        conf.revision = GS_BUILD_INFO_REVISION;

        csp_rdp_set_opt(6, 30000, 16000, 1, 8000, 3);

        gs_csp_init(&conf);

        // Configure routes - add default routes or use command line option
        gs_csp_rtable_load(NULL, true, true);

        // Start CSP router task
        gs_csp_router_task_start(0, GS_THREAD_PRIORITY_HIGH);
    }

    // Start service dispatcher -> invokes CSP handlers based on port in in-coming request.
    csp_service_dispatcher();

    // One-shot mode: a command was given on the command line - run it once and exit.
    if (arg_index < argc) {
        char command_line[512];
        if (build_command_line(argc, argv, arg_index, command_line, sizeof(command_line)) == false) {
            fprintf(stderr, "Command line too long\n");
            return 1;
        }
        return run_one_shot(command_line);
    }

    // No command given - fall back to the interactive console.
    gs_console_start("csp-client-lite", 0);
    gs_thread_block();

    return 0;
}
