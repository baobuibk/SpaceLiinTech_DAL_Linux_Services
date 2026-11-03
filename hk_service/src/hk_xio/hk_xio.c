/*
 * hk_xio.c - Query OBC-driven power switches via the xio tool
 */

#include "hk_xio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <sys/wait.h>

/* Bounded subprocess capture — see hk_can.c for the twin implementation.
 * Kept as a local copy so this module stays self-contained, same as the
 * rest of this codebase (temp_fs/imu_fs each keep their own atomic-write
 * helpers rather than sharing a common util). */
static int run_capture(char *const argv[], int timeout_ms,
                        char *out, size_t out_sz, int *exit_code)
{
    int pipefd[2];
    if (pipe(pipefd) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);

    size_t used = 0;
    int    timed_out = 0;
    struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000L
                        + (now.tv_nsec - start.tv_nsec) / 1000000L;
        int remaining = timeout_ms - (int)elapsed_ms;
        if (remaining <= 0) { timed_out = 1; break; }

        int pr = poll(&pfd, 1, remaining);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) { timed_out = 1; break; }

        if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
            if (used + 1 < out_sz) {
                ssize_t r = read(pipefd[0], out + used, out_sz - 1 - used);
                if (r > 0) { used += (size_t)r; continue; }
                if (r == 0) break;
                if (errno == EINTR) continue;
                break;
            } else {
                char scratch[256];
                ssize_t r = read(pipefd[0], scratch, sizeof(scratch));
                if (r <= 0) break;
                continue;
            }
        }
    }
    out[used] = '\0';
    close(pipefd[0]);

    if (timed_out) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        if (exit_code) *exit_code = -1;
        return -1;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (exit_code) *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return 0;
}

int hk_xio_pin_state(const HkXioCfg *cfg, const char *pin_name)
{
    char *argv[] = { (char *)cfg->xio_path, "list", NULL };

    char out[4096];
    int  exit_code = -1;

    if (run_capture(argv, cfg->timeout_ms, out, sizeof(out), &exit_code) < 0)
        return -1;
    if (exit_code != 0)
        return -1;

    char *saveptr = NULL;
    for (char *line = strtok_r(out, "\n", &saveptr); line;
         line = strtok_r(NULL, "\n", &saveptr)) {

        char name_tok[64], state_tok[16];
        if (sscanf(line, "%63s %15s", name_tok, state_tok) != 2)
            continue;
        if (strcmp(name_tok, pin_name) != 0)
            continue;

        return (strcmp(state_tok, "ON") == 0) ? 1 : 0;
    }

    return -1; /* pin not found in xio's output */
}

int hk_xio_input_active(const HkXioCfg *cfg, const char *pin_name)
{
    char *argv[] = { (char *)cfg->xio_path, (char *)cfg->input_subcmd, NULL };

    char out[4096];
    int  exit_code = -1;

    if (run_capture(argv, cfg->timeout_ms, out, sizeof(out), &exit_code) < 0)
        return -1;
    if (exit_code != 0)
        return -1;

    char *saveptr = NULL;
    for (char *line = strtok_r(out, "\n", &saveptr); line;
         line = strtok_r(NULL, "\n", &saveptr)) {

        if (strstr(line, pin_name) == NULL)
            continue;

        /* Case-insensitive: format not confirmed on real hardware,
         * see the ASSUMED note in hk_xio.h. */
        return (strcasestr(line, "active") != NULL) ? 1 : 0;
    }

    return -1; /* pin not found in xio's output */
}
