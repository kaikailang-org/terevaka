/* tui_shim.c — terevaka spike: terminal raw-mode + key reading shim.
 *
 * kaikai FFI v1 cannot pass `struct termios` by value or take
 * pointer-to-T arguments (docs/ffi.md §"What FFI v1 does NOT
 * support"). So this shim flattens the termios dance into a few
 * functions with primitive (int) args/returns, holding the saved
 * original terminal state in a static here — exactly the C-shim
 * pattern kohau uses for libsqlite3.
 *
 * Surface exposed to kaikai (all `extern "C"`):
 *   kai_tui_raw_enable()  -> int   (0 ok, -1 fail)
 *   kai_tui_raw_disable() -> int   (restores the saved termios)
 *   kai_tui_read_key()    -> int   (one byte 0..255, or -1 on EOF/err)
 *
 * read_key BLOCKS on one byte. Arrow keys arrive as the 3-byte
 * sequence ESC '[' 'A'/'B'/'C'/'D'; the kaikai side reads them
 * byte-by-byte and assembles the key. Raw mode (no canonical, no
 * echo) is what makes byte-at-a-time reads possible — without it
 * the terminal waits for Enter.
 */

#include <termios.h>
#include <unistd.h>
#include <stdint.h>

static struct termios kai_tui_saved;
static int kai_tui_saved_valid = 0;

int64_t kai_tui_raw_enable(void) {
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &kai_tui_saved) != 0) return -1;
    kai_tui_saved_valid = 1;
    raw = kai_tui_saved;
    /* cfmakeraw-equivalent, but keep it explicit and portable:
     * disable canonical mode (ICANON) so reads return per-byte,
     * disable echo (ECHO) so keystrokes don't print, and disable
     * signal generation (ISIG) so Ctrl-C reaches us as a byte
     * (the demo handles 'q'/Ctrl-C itself). */
    raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN]  = 1;   /* read returns after 1 byte */
    raw.c_cc[VTIME] = 0;   /* no inter-byte timeout */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return -1;
    return 0;
}

int64_t kai_tui_raw_disable(void) {
    if (!kai_tui_saved_valid) return 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &kai_tui_saved) != 0) return -1;
    return 0;
}

/* Read exactly one byte from stdin (blocking). Returns the byte
 * value 0..255, or -1 on EOF / error. */
int64_t kai_tui_read_key(void) {
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1) return (int64_t) c;
    return -1;
}

/* Poll stdin for one byte, waiting at most `timeout_ms`. Returns the
 * byte 0..255 if a key arrived, -1 if the timeout elapsed with no
 * input, -2 on error/EOF. This is what lets the spike's single
 * loop tick a clock while waiting for input WITHOUT blocking the
 * whole program on read().
 *
 * NOTE (honesty): this is the spike's workaround. The terevaka
 * design wants input parked on the kaikai reactor (R3 stdin) so it
 * interleaves with timers and tasks as real fibers — but the reactor
 * parks Stdin.read_line/read_bytes, which are line-buffered, not the
 * raw byte-at-a-time reads a TUI needs. Raw-mode-on-the-reactor is a
 * stdlib/runtime gap; poll() is the honest bridge until it closes. */
#include <poll.h>
int64_t kai_tui_poll_key(int64_t timeout_ms) {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    int r = poll(&pfd, 1, (int) timeout_ms);
    if (r == 0) return -1;            /* timeout, no input */
    if (r < 0)  return -2;            /* error */
    if (pfd.revents & POLLIN) {
        unsigned char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 1) return (int64_t) c;
    }
    return -2;                        /* EOF or unexpected */
}

/* Write a string to stdout WITHOUT a trailing newline, then flush.
 * kaikai's stdlib `print` always appends '\n', which a TUI cannot
 * use for absolute cursor positioning. terevaka's terminal package
 * will own a primitive like this; the spike binds it directly.
 * `s` is a NUL-terminated kaikai-allocated C string (FFI v1 String
 * ABI). */
#include <stdio.h>
#include <string.h>
int64_t kai_tui_write(const char *s) {
    size_t len = strlen(s);
    fwrite(s, 1, len, stdout);
    fflush(stdout);
    return (int64_t) len;
}
