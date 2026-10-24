# Drives tools/term_probe_check under a real pty, playing the terminal
# on the other end. `answer` replies to the graphics query the way a
# kitty-capable terminal would; `silent` says nothing, which is what an
# emulator that does not speak the protocol does. `cap` replies with
# exactly the 256 bytes `probe` reads at most, a NUL among them; `over`
# sends one byte more, which `probe` must treat as noise, not an answer.
#
#   python3 tools/term_probe_check.py build/term_probe_check answer
#   python3 tools/term_probe_check.py build/term_probe_check silent
#   python3 tools/term_probe_check.py build/term_probe_check cap
#   python3 tools/term_probe_check.py build/term_probe_check over
import os, pty, time, sys, select

app, mode = sys.argv[1], sys.argv[2]
QUERY_HEAD = b"\x1b_G"
REPLY = b"\x1b_Gi=31;OK\x1b\\"
REPLY_CAP = 256


def padded(total):
    head, tail = b"\x1b_Gi=31;OK\x00", b"\x1b\\"
    return head + b"." * (total - len(head) - len(tail)) + tail


reply = {"answer": REPLY, "cap": padded(REPLY_CAP),
         "over": padded(REPLY_CAP + 1)}.get(mode)

pid, fd = pty.fork()                  # child's stdin AND stdout are a real tty
if pid == 0:
    os.execvp(app, [app])

out = b""
replied = False
t0 = time.time()
while time.time() - t0 < 10:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            break
        if not chunk:
            break
        out += chunk
        # answer only once, and only after the query actually arrives
        if reply and not replied and QUERY_HEAD in out:
            os.write(fd, reply)
            replied = True
    p, st = os.waitpid(pid, os.WNOHANG)
    if p == pid:
        break
try:
    os.kill(pid, 9)
except OSError:
    pass

text = out.decode(errors="replace")
print(text)

expected = "probe: answered ok" if mode in ("answer", "cap") else "probe: no answer"
ok = expected in text and "probe: terminal restored" in text
# With the terminal restored, OPOST is back on, so the report lines end
# with CRLF. Raw mode left on would show as bare LF.
crlf = "probe: terminal restored\r\n" in text
print("RESULT:", "PASS" if (ok and crlf) else "FAIL",
      "(expected %r, echo-restored=%s)" % (expected, crlf))
sys.exit(0 if (ok and crlf) else 1)
