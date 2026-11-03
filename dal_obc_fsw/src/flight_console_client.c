#include "dal_obc_config.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

#define CONSOLE_LINE_MAX 4096
#define CONSOLE_HISTORY_MAX 32

typedef struct {
    char *entries[CONSOLE_HISTORY_MAX];
    size_t count;
} console_history_t;

static void history_add(console_history_t *history, const char *line)
{
    if (!*line || (history->count &&
        !strcmp(history->entries[history->count - 1], line))) return;
    char *copy = strdup(line);
    if (!copy) return;
    if (history->count == CONSOLE_HISTORY_MAX) {
        free(history->entries[0]);
        memmove(history->entries, history->entries + 1,
                (CONSOLE_HISTORY_MAX - 1) * sizeof(history->entries[0]));
        --history->count;
    }
    history->entries[history->count++] = copy;
}

static void redraw(const char *line, size_t length, size_t cursor)
{
    printf("\r\033[2Kflight> %s", line);
    if (cursor < length) printf("\033[%zuD", length - cursor);
    fflush(stdout);
}

static int escape_byte(unsigned char *out)
{
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
    if (poll(&pfd, 1, 100) <= 0) return -1;
    return read(STDIN_FILENO, out, 1) == 1 ? 0 : -1;
}

/* Returns 1 for a command, 0 on EOF, and -1 for Ctrl-C or a read error. */
static int read_command(char line[CONSOLE_LINE_MAX], console_history_t *history)
{
    if (!isatty(STDIN_FILENO)) {
        if (!fgets(line, CONSOLE_LINE_MAX, stdin)) return 0;
        line[strcspn(line, "\r\n")] = 0;
        return 1;
    }

    struct termios saved, raw;
    if (tcgetattr(STDIN_FILENO, &saved) != 0) return -1;
    raw = saved;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    raw.c_iflag &= (tcflag_t)~(ICRNL | IXON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return -1;

    size_t length = 0, cursor = 0, selected = history->count;
    char draft[CONSOLE_LINE_MAX] = "";
    line[0] = 0;
    int result = -1;
    for (;;) {
        unsigned char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;

        if (c == '\r' || c == '\n') {
            fputs("\r\n", stdout); fflush(stdout);
            result = 1;
            break;
        }
        if (c == 3) { /* Ctrl-C: cancel this line and restore terminal mode. */
            fputs("^C\r\n", stdout); fflush(stdout);
            break;
        }
        if (c == 4 && !length) { /* Ctrl-D */
            fputs("\r\n", stdout); fflush(stdout);
            result = 0;
            break;
        }
        if (c == 127 || c == 8) {
            if (cursor) {
                memmove(line + cursor - 1, line + cursor, length - cursor + 1);
                --cursor; --length;
                redraw(line, length, cursor);
            }
            continue;
        }
        if (c == 27) {
            unsigned char prefix, key;
            if (escape_byte(&prefix) || prefix != '[' || escape_byte(&key)) continue;
            if (key == 'A' && selected) {
                if (selected == history->count) strcpy(draft, line);
                strcpy(line, history->entries[--selected]);
                cursor = length = strlen(line);
                redraw(line, length, cursor);
            } else if (key == 'B' && selected < history->count) {
                ++selected;
                strcpy(line, selected == history->count ? draft : history->entries[selected]);
                cursor = length = strlen(line);
                redraw(line, length, cursor);
            } else if (key == 'C' && cursor < length) {
                ++cursor; redraw(line, length, cursor);
            } else if (key == 'D' && cursor) {
                --cursor; redraw(line, length, cursor);
            } else if (key == '3') { /* Delete: ESC [ 3 ~ */
                unsigned char end;
                if (!escape_byte(&end) && end == '~' && cursor < length) {
                    memmove(line + cursor, line + cursor + 1, length - cursor);
                    --length;
                    redraw(line, length, cursor);
                }
            }
            continue;
        }
        if (c >= 32 && length + 1 < CONSOLE_LINE_MAX) {
            memmove(line + cursor + 1, line + cursor, length - cursor + 1);
            line[cursor++] = (char)c;
            ++length;
            redraw(line, length, cursor);
        }
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    return result;
}

static int request(const char *path, const char *command, int json)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    if (strlen(path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "Socket path too long\n"); close(fd); return 1;
    }
    strcpy(addr.sun_path, path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))) {
        fprintf(stderr, "Cannot connect to flight software at %s: %s\n", path, strerror(errno));
        close(fd); return 1;
    }
    if (json && dprintf(fd, "json %s\n", command) < 0) { perror("write"); close(fd); return 1; }
    if (!json && dprintf(fd, "%s\n", command) < 0) { perror("write"); close(fd); return 1; }
    shutdown(fd, SHUT_WR);
    char buf[4096];
    ssize_t n;
    int result = 0;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, (size_t)n) != n) { result = 1; break; }
    }
    if (n < 0) { perror("read"); result = 1; }
    close(fd);
    return result;
}

int main(int argc, char **argv)
{
    const char *path = getenv("DAL_FLIGHT_CONSOLE_SOCKET");
    if (!path || !*path) path = DAL_OBC_CONSOLE_SOCKET;
    int json = 0, arg = 1;
    for (; arg < argc; ++arg) {
        if (!strcmp(argv[arg], "--json")) json = 1;
        else if (!strcmp(argv[arg], "--socket") && arg + 1 < argc) path = argv[++arg];
        else if (!strcmp(argv[arg], "--help")) {
            puts("Usage: dal_flight_console [--json] [--socket PATH] [command ...]\n"
                 "With no command, opens an interactive prompt. Type help for commands.\n"
                 "In the prompt, use Up/Down for history and 'json on/off' for format.");
            return 0;
        } else break;
    }
    if (arg < argc) {
        char command[4096] = "";
        size_t used = 0;
        for (int i = arg; i < argc; ++i) {
            size_t n = strlen(argv[i]);
            if (used + n + 2 > sizeof(command)) { fprintf(stderr, "Command too long\n"); return 1; }
            if (used) command[used++] = ' ';
            memcpy(command + used, argv[i], n + 1); used += n;
        }
        return request(path, command, json);
    }
    char line[CONSOLE_LINE_MAX];
    console_history_t history = {0};
    for (;;) {
        fputs("flight> ", stdout); fflush(stdout);
        int status = read_command(line, &history);
        if (status == 0) break;
        if (status < 0) continue;
        if (!strcmp(line, "exit") || !strcmp(line, "quit")) break;
        history_add(&history, line);
        if (!strcmp(line, "json on")) { json = 1; puts("JSON enabled"); continue; }
        if (!strcmp(line, "json off")) { json = 0; puts("Text enabled"); continue; }
        if (*line) request(path, line, json);
    }
    for (size_t i = 0; i < history.count; ++i) free(history.entries[i]);
    return 0;
}
