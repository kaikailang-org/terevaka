/* terevaka_term.c — terminal control shim for terevaka.
 *
 * kaikai FFI v1 cannot pass `struct termios`/`struct winsize` by
 * value or take pointer-to-T arguments (kaikai/docs/ffi.md). So
 * this shim flattens raw-mode setup, non-blocking key reads,
 * no-newline writes, and terminal-size queries into functions with
 * primitive (int / string) signatures, holding the saved termios
 * state in a static — the same C-shim pattern kohau uses for
 * libsqlite3.
 *
 * Surface (all bound from terevaka/term.kai as extern "C"):
 *   kai_tvk_raw_enable()        -> int   (0 ok, -1 fail)
 *   kai_tvk_raw_disable()       -> int   (restore saved termios)
 *   kai_tvk_poll_key(int ms)    -> int   (byte 0..255, -1 timeout, -2 eof)
 *   kai_tvk_write(const char*)  -> int   (write to stdout, no \n, flush)
 *   kai_tvk_term_rows()         -> int   (terminal height, fallback 24)
 *   kai_tvk_term_cols()         -> int   (terminal width,  fallback 80)
 */

#include <termios.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>

static struct termios kai_tvk_saved;
static int kai_tvk_saved_valid = 0;

int64_t kai_tvk_raw_enable(void) {
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &kai_tvk_saved) != 0) return -1;
    kai_tvk_saved_valid = 1;
    raw = kai_tvk_saved;
    /* canonical off (per-byte reads), echo off, signals off so the
     * app handles 'q'/Ctrl-C itself; flow-control + CR translation
     * off so escape sequences arrive intact. */
    raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return -1;
    return 0;
}

int64_t kai_tvk_raw_disable(void) {
    if (!kai_tvk_saved_valid) return 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &kai_tvk_saved) != 0) return -1;
    return 0;
}

/* Poll stdin up to `timeout_ms`. Returns:
 *     0..255  the byte read
 *     -1      timeout, no input (lets a loop tick a clock)
 *     -2      end of input: stdin is closed and never yields again
 *     -3      poll/read error
 *
 * EOF is its own code because it is not recoverable: poll() reports a
 * closed descriptor as ready forever, so a caller that treats it as
 * "nothing I recognise" spins at full speed with no way out. The
 * caller must stop reading on -2.
 *
 * This is the spike's poll bridge: the terevaka design wants input
 * parked on the kaikai reactor as a fiber. The reactor's stdin phase
 * has since shipped, but whether it carries raw byte-at-a-time reads
 * on a tty is unmeasured — until it is, poll() is the honest interim. */
int64_t kai_tvk_poll_key(int64_t timeout_ms) {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    int r = poll(&pfd, 1, (int) timeout_ms);
    if (r == 0) return -1;
    if (r < 0)  return -3;
    if (pfd.revents & POLLIN) {
        unsigned char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 1) return (int64_t) c;
        if (n == 0) return -2;          /* read past end of input */
        return -3;
    }
    /* Ready but not readable: a hangup on the write end is end of
     * input just the same. */
    if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) return -2;
    return -3;
}

/* Write a NUL-terminated string to stdout WITHOUT a trailing
 * newline, then flush. stdlib `print` always appends '\n', which a
 * TUI cannot use for absolute cursor positioning. */
int64_t kai_tvk_write(const char *s) {
    size_t len = strlen(s);
    fwrite(s, 1, len, stdout);
    fflush(stdout);
    return (int64_t) len;
}

/* Query the terminal size robustly. A single ioctl on STDOUT_FILENO
 * fails (and silently falls back to 80x24) whenever stdout is not the
 * controlling terminal — which happens inside tmux/multiplexers and
 * when stdout is piped. Try, in order: /dev/tty (the real controlling
 * terminal, immune to stdio redirection), then stderr/stdin/stdout,
 * then the COLUMNS/LINES environment variables, before the last-resort
 * hardcoded fallback. `want_cols` selects which dimension to return. */
static int64_t kai_tvk_winsize(int want_cols) {
    struct winsize ws;

    /* /dev/tty is the controlling terminal regardless of fd redirection */
    int tty = open("/dev/tty", O_RDONLY | O_NOCTTY);
    if (tty >= 0) {
        int ok = (ioctl(tty, TIOCGWINSZ, &ws) == 0);
        close(tty);
        if (ok) {
            if (want_cols && ws.ws_col > 0) return (int64_t) ws.ws_col;
            if (!want_cols && ws.ws_row > 0) return (int64_t) ws.ws_row;
        }
    }

    /* any of the standard fds may still be the terminal */
    int fds[3] = { STDERR_FILENO, STDIN_FILENO, STDOUT_FILENO };
    for (int i = 0; i < 3; i++) {
        if (ioctl(fds[i], TIOCGWINSZ, &ws) == 0) {
            if (want_cols && ws.ws_col > 0) return (int64_t) ws.ws_col;
            if (!want_cols && ws.ws_row > 0) return (int64_t) ws.ws_row;
        }
    }

    /* environment hint (export COLUMNS/LINES, or a shell that sets them) */
    const char *env = getenv(want_cols ? "COLUMNS" : "LINES");
    if (env && *env) {
        long v = strtol(env, NULL, 10);
        if (v > 0) return (int64_t) v;
    }

    return want_cols ? 80 : 24;
}

int64_t kai_tvk_term_rows(void) { return kai_tvk_winsize(0); }
int64_t kai_tvk_term_cols(void) { return kai_tvk_winsize(1); }
