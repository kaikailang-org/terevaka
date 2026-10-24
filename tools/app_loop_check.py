# Drives tools/app_loop_check under a real pty, playing the terminal on
# the other end, and compares every byte the runtime writes against the
# stream it should write.
#
#   python3 tools/app_loop_check.py build/app_loop_check plain
#   python3 tools/app_loop_check.py build/app_loop_check overlay
#   python3 tools/app_loop_check.py build/app_loop_check eof
#
# `plain` and `overlay` send keys with idle gaps between them, so the
# loop ticks while nothing changes: a frame painted twice is a repaint
# the runtime should have skipped. `eof` hangs the terminal up after
# entry and expects both loops to end on their own instead of spinning.
#
# The pty is not made the child's controlling terminal, so the hangup
# reaches the child as end of input rather than as SIGHUP.
import os, select, subprocess, sys, time

ESC = "\x1b"
ENTRY = ESC + "[?25l" + ESC + "[2J" + ESC + "[H"
EXIT = ESC + "[?25h" + ESC + "[2J" + ESC + "[H"
GAP = 0.12        # several 30 ms ticks between keys
ESC_GAP = 0.4     # a lone ESC must not be read with the next key


def spawn(app, mode):
    master, slave = os.openpty()
    proc = subprocess.Popen([app, mode], stdin=slave, stdout=slave,
                            stderr=slave, start_new_session=True,
                            close_fds=True)
    os.close(slave)
    return proc, master


def pump(fd, out, seconds):
    end = time.time() + seconds
    while True:
        left = end - time.time()
        if left <= 0:
            return out
        r, _, _ = select.select([fd], [], [], left)
        if r:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                return out
            if not chunk:
                return out
            out += chunk


def wait_for(fd, out, needle, seconds):
    end = time.time() + seconds
    while needle not in out and time.time() < end:
        out = pump(fd, out, 0.05)
    return out


def lines_at(lines, row, col):
    return "".join("%s[%d;%dH%s[K%s" % (ESC, row + i, col, ESC, l)
                   for i, l in enumerate(lines))


def frame(log):
    return [] if not log else ["keys %d" % len(log), " ".join(log)]


def plain_paint(log):
    return lines_at(frame(log), 2, 4) + ESC + "[J"


def modal_paint(log):
    return (ESC + "[2J" + ESC + "[H" + lines_at(frame(log), 2, 4)
            + lines_at(["modal %d" % len(log)], 6, 16))


def drive(app, mode, keys):
    proc, fd = spawn(app, mode)
    out = wait_for(fd, b"", ENTRY.encode(), 5)
    out = pump(fd, out, GAP)
    for raw, _, gap in keys:
        os.write(fd, raw)
        out = pump(fd, out, gap)
    out = pump(fd, out, 0.5)
    try:
        code = proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        code = "still running"
    os.close(fd)
    return out.decode(errors="replace"), code


def expect_stream(mode, keys):
    log, modal = [], False
    stream = ENTRY + plain_paint(log)
    for _, name, _ in keys:
        if name is None:
            break
        log.append(name)
        if name == "P111":
            modal = not modal
        stream += modal_paint(log) if modal else plain_paint(log)
    return stream + EXIT


PLAIN_KEYS = [
    (b"\x1b[A", "Up", GAP),
    (b"\x1b[B", "Down", GAP),
    (b"\x1b[C", "Right", GAP),
    (b"\x1b[D", "Left", GAP),
    (b"\x1b[Z", "Unknown", GAP),     # an escape sequence this layer does not decode
    (b"\x1b", "Esc", ESC_GAP),
    (b"\r", "Enter", GAP),
    (b"\n", "Enter", GAP),
    (b"\t", "Tab", GAP),
    (b"\x7f", "Backspace", GAP),
    (b"\x08", "Backspace", GAP),
    (b" ", "P32", GAP),              # the first printable byte
    (b"a", "P97", GAP),
    (b"\x01", "Unknown", GAP),       # a control byte
    (b"\x00", "Unknown", GAP),
    (b"q", None, GAP),
]

OVERLAY_KEYS = [
    (b"a", "P97", GAP),
    (b"o", "P111", GAP),
    (b"b", "P98", GAP),
    (b"o", "P111", GAP),
    (b"c", "P99", GAP),
    (b"q", None, GAP),
]


def check_stream(app, mode, keys):
    got, code = drive(app, mode, keys)
    want = expect_stream(mode, keys)
    if got == want and code == 0:
        return True
    i = next((n for n in range(min(len(got), len(want))) if got[n] != want[n]),
             min(len(got), len(want)))
    print("FAIL %s: exit %s; streams differ at byte %d" % (mode, code, i))
    print("  want: %r" % want[max(0, i - 40):i + 80])
    print("  got:  %r" % got[max(0, i - 40):i + 80])
    return False


def check_eof(app, mode):
    proc, fd = spawn(app, mode)
    out = wait_for(fd, b"", ENTRY.encode(), 5)
    out = pump(fd, out, GAP)
    os.close(fd)
    try:
        code = proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        print("FAIL eof %s: still running after the terminal hung up" % mode)
        return False
    if code != 0:
        print("FAIL eof %s: exited %s, expected 0" % (mode, code))
        return False
    return True


app, mode = sys.argv[1], sys.argv[2]
if mode == "plain":
    ok = check_stream(app, "plain", PLAIN_KEYS)
elif mode == "overlay":
    ok = check_stream(app, "overlay", OVERLAY_KEYS)
elif mode == "eof":
    ok = check_eof(app, "plain") and check_eof(app, "overlay")
else:
    sys.exit("unknown mode %r" % mode)
# Quiet on a pass, so `make test` shows only the summary line; a
# failure has already said what differed.
sys.exit(0 if ok else 1)
