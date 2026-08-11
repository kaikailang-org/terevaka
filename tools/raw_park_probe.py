import os, pty, time, sys, select
app = sys.argv[1]
pid, fd = pty.fork()              # child's stdin AND stdout are a real tty
if pid == 0:
    os.execvp(app, [app])

out = b""
t0 = time.time()
sent = False
while time.time() - t0 < 8:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            break
        if not chunk: break
        out += chunk
    # after the ticker has had time to run, send one keystroke
    if not sent and time.time() - t0 > 2.0:
        os.write(fd, b"K"); sent = True
    p, st = os.waitpid(pid, os.WNOHANG)
    if p == pid: break
try: os.kill(pid, 9)
except OSError: pass
print(out.decode(errors="replace"))
