/* src/main.c */
/************************************************
 *  @file     : main.c
 *  @brief    : dal_obc_fsw -- BEE-DAL payload OBC firmware
 *-----------------------------------------------
 *  Description:
 *    Bridges the satellite OBC
 ************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <errno.h>

#include "dal_obc_config.h"
#include "utils/error.h"
#include "services/hk_service.h"
#include "services/bee_dal_tmtc_service.h"
#include "services/flight_console_service.h"
#include "services/gui_state.h"
#include "services/gui_hw.h"
#include "services/exp_csp_service.h"
#include "services/obc_hk_collector.h"
#include "services/exp_data_service.h"

/* ============================================================================
 * systemd watchdog -- self-contained, no libsystemd needed.
 * Silent no-op when not running under systemd (NOTIFY_SOCKET unset).
 * ============================================================================ */
static int sd_notify(int unset_env, const char *state)
{
    const char *sock_path = getenv("NOTIFY_SOCKET");
    if (!sock_path || !state) return 0;

    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (sock_path[0] == '@') {
        strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
        addr.sun_path[0] = '\0';
    } else {
        strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    }

    sendto(fd, state, strlen(state), MSG_NOSIGNAL, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);

    if (unset_env) unsetenv("NOTIFY_SOCKET");
    return 1;
}

/* ============================================================================
 * Global state / signal handling
 * ============================================================================ */
static volatile sig_atomic_t g_shutdown = 0;

bool g_verbose = false;
bool g_quiet_housekeeping = false;

static void signal_handler(int sig)
{
    (void)sig;
    if (g_shutdown) return;
    printf("\n[MAIN] Received shutdown signal\n");
    g_shutdown = 1;
}

static void print_banner(void)
{
    printf("\n");
    printf("============================================\n");
    printf("  dal_obc_fsw -- BEE-DAL Payload OBC\n");
    printf("  Version: %d.%d.%d\n", DAL_OBC_VERSION_MAJOR, DAL_OBC_VERSION_MINOR, DAL_OBC_VERSION_PATCH);
    printf("============================================\n");
    printf("Services:\n");
    printf("  - OBC HK snapshot cache\n");
    printf("  - BEE-DAL TMTC     (RS-422 server, %s @ %d bps)\n",
           DAL_OBC_RS422_PORT, DAL_OBC_RS422_BAUD_NUM);
    printf("  - Local console    (%s)\n", DAL_OBC_CONSOLE_SOCKET);
    printf("============================================\n\n");
}

/* ============================================================================
 * Service lifecycle
 * ============================================================================ */
static xerr_t initialize_services(void)
{
    xerr_t err;

    printf("[MAIN] Step 1/2: Initializing HK service...\n");
    err = hk_service_init();
    if (err != XERR_OK) {
        fprintf(stderr, "[MAIN] HK initialization failed: %d\n", err);
        return err;
    }

    printf("[MAIN] Step 2/2: Initializing BEE-DAL TMTC service...\n");
    err = bee_dal_tmtc_service_init();
    if (err != XERR_OK) {
        fprintf(stderr, "[MAIN] BEE-DAL TMTC initialization failed: %d\n", err);
        return err;
    }

    err = flight_console_service_init();
    if (err != XERR_OK) return err;

    printf("[MAIN] ---------> Start\n\n");
    return XERR_OK;
}

static xerr_t start_services(void)
{
    xerr_t err;

    if (obc_hk_collector_start() != 0) return XERR_IO;

    printf("[MAIN] Starting HK service...\n");
    err = hk_service_start();
    if (err != XERR_OK) {
        fprintf(stderr, "[MAIN] HK start failed: %d\n", err);
        obc_hk_collector_stop();
        return err;
    }

    printf("[MAIN] Starting BEE-DAL TMTC service...\n");
    err = bee_dal_tmtc_service_start();
    if (err != XERR_OK) {
        fprintf(stderr, "[MAIN] BEE-DAL TMTC start failed: %d\n", err);
        hk_service_stop();
        obc_hk_collector_stop();
        return err;
    }

    err = flight_console_service_start();
    if (err != XERR_OK) {
        bee_dal_tmtc_service_stop();
        hk_service_stop();
        obc_hk_collector_stop();
        return err;
    }

    gui_state_start();

    if (exp_csp_service_start() != 0) {
        gui_state_stop();
        flight_console_service_stop();
        bee_dal_tmtc_service_stop();
        hk_service_stop();
        obc_hk_collector_stop();
        return XERR_IO;
    }

    if (exp_data_service_start() != 0) {
        exp_csp_service_stop();
        gui_state_stop();
        flight_console_service_stop();
        bee_dal_tmtc_service_stop();
        hk_service_stop();
        obc_hk_collector_stop();
        return XERR_IO;
    }

    printf("[MAIN] ---------> Run\n\n");
    return XERR_OK;
}

static void shutdown_services(void)
{
    printf("\n============================================\n");
    printf("[MAIN] Shutting down services...\n");
    printf("============================================\n");

    exp_data_service_stop();
    exp_csp_service_stop();
    obc_hk_collector_stop();
    flight_console_service_stop();
    gui_state_stop();
    bee_dal_tmtc_service_stop();
    hk_service_stop();

    printf("============================================\n");
    printf("[MAIN] Shutdown complete\n");
    printf("============================================\n\n");
}

/* ============================================================================
 * Watchdog kick -- called every second from the main loop.
 * Service file (see tool/advanced_setup.sh) has WatchdogSec=60 -- kick
 * every 10s gives a 6x safety margin.
 * ============================================================================ */
#define WATCHDOG_KICK_INTERVAL_SEC 10

static void watchdog_kick(uint32_t loop_tick)
{
    if (loop_tick % WATCHDOG_KICK_INTERVAL_SEC == 0)
        sd_notify(0, "WATCHDOG=1");
}

static void trigger_debug_getty(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[MAIN] fork() failed for systemctl start %s: %s\n",
                DAL_OBC_DEBUG_GETTY_UNIT, strerror(errno));
        return;
    }
    if (pid == 0) {
        execlp("systemctl", "systemctl", "start", DAL_OBC_DEBUG_GETTY_UNIT, (char *)NULL);
        _exit(127); /* only reached if execlp itself failed */
    }

    int status = 0;
    waitpid(pid, &status, 0); /* reap it -- avoid a zombie during our own brief remaining lifetime */
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        fprintf(stderr, "[MAIN] 'systemctl start %s' exited %d -- port handoff may not have happened\n",
                DAL_OBC_DEBUG_GETTY_UNIT, WEXITSTATUS(status));
    }
}

static bool run_main_loop(void)
{
    uint32_t tick = 0;

    while (!g_shutdown) {
        sleep(1);
        tick++;

        watchdog_kick(tick);

        if (bee_dal_tmtc_service_debug_handoff_requested()) {
            printf("[MAIN] Debug handoff requested -- starting %s and exiting cleanly \n",
                   DAL_OBC_DEBUG_GETTY_UNIT);
            trigger_debug_getty();
            return true;
        }

        if (!bee_dal_tmtc_service_is_running()) {
            fprintf(stderr, "[MAIN] BEE-DAL TMTC service died "
                            "- exiting for restart\n");
            return false;
        }
        if (!hk_service_is_running()) {
            fprintf(stderr, "[MAIN] HK service died - exiting for restart\n");
            return false;
        }
        if (!flight_console_service_is_running()) {
            fprintf(stderr, "[MAIN] local console service died - exiting for restart\n");
            return false;
        }

        if (tick % SYSTEM_STATUS_INTERVAL_SEC == 0) {
            printf("[STATUS] dal_obc_fsw alive, tick=%u\n", tick);
        }
    }

    return true;
}

/* ============================================================================
 * Main entry point
 * ============================================================================ */
int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "--gui-hw") == 0)
        return gui_hw_main(argc, argv);
    const char *verbose_env = getenv("DAL_OBC_VERBOSE");
    const char *quiet_env = getenv("DAL_OBC_QUIET_HOUSEKEEPING");
    g_verbose = verbose_env && strcmp(verbose_env, "1") == 0;
    g_quiet_housekeeping = quiet_env && strcmp(quiet_env, "1") == 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_verbose = true;
        } else if (strcmp(argv[i], "--quiet-housekeeping") == 0) {
            g_quiet_housekeeping = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            puts("Usage: dal_obc_fsw [-v|--verbose] [--quiet-housekeeping] [--help]");
            puts("  --quiet-housekeeping  hide routine telemetry frame dumps in verbose mode");
            puts("  Environment: DAL_OBC_VERBOSE=1 DAL_OBC_QUIET_HOUSEKEEPING=1");
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "[MAIN] Unknown argument: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    /* Force line-buffered stdout/stderr so journald gets text lines instead
     * of '[NNB blob data]' -- glibc otherwise block-buffers non-TTY stdout. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    print_banner();

    if (g_verbose)
        printf("[MAIN] Verbose logging enabled frame dumps\n\n");
    if (g_verbose && g_quiet_housekeeping)
        printf("[MAIN] Routine housekeeping frame dumps hidden\n\n");

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    /* SIGSEGV intentionally NOT caught: let systemd see exit code
     * killed/SEGV and trigger Restart=on-failure. */

    sd_notify(0, "STATUS=Initializing services...");

    xerr_t err = initialize_services();
    if (err != XERR_OK) {
        fprintf(stderr, "[MAIN] Initialization failed: %d\n", err);
        sd_notify(0, "STATUS=Init failed");
        return EXIT_FAILURE;
    }

    err = start_services();
    if (err != XERR_OK) {
        fprintf(stderr, "[MAIN] Service startup failed: %d\n", err);
        sd_notify(0, "STATUS=Start failed");
        shutdown_services();
        return EXIT_FAILURE;
    }

    sd_notify(0, "READY=1\nSTATUS=All services running");
    printf("[MAIN] sd_notify READY=1 sent to systemd\n\n");

    bool clean_shutdown = run_main_loop();

    sd_notify(0, clean_shutdown ? "STOPPING=1\nSTATUS=Shutting down"
                                : "STOPPING=1\nSTATUS=Internal fault detected");
    shutdown_services();

    printf("[MAIN] EXIT dal_obc_fsw (%s)\n\n", clean_shutdown ? "clean" : "FAULT");
    return clean_shutdown ? EXIT_SUCCESS : EXIT_FAILURE;
}
