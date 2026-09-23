# tools/

## `raw_park_probe` — the fiber gate

Answers one question: **does a fiber reading the terminal park itself,
or park the scheduler?**

The whole reason terevaka threads widgets as plain values instead of
fibers is that a blocking raw read used to freeze every other fiber, so
the input pump could not be a fiber. The design (`docs/design.md`) calls
this the #1 technical blocker for it.

The probe runs a reader fiber (`Stdin.read_bytes(1)`) alongside a
ticker fiber, with `term.raw_enable()` in effect, under a real pty. If
the reader parks, the ticker keeps ticking while it waits and the
reader still receives the keystroke sent afterwards. If it parks the
scheduler instead, the ticker produces nothing.

```sh
kai build tools/raw_park_probe.kai -o build/raw_park_probe
python3 tools/raw_park_probe.py build/raw_park_probe
```

Expected on kai 0.111.0:

```
raw mode on
tick 0
...
tick 5
ticker done: 6 ticks while the reader waited
reader got: [K]
```

`tick` lines before `reader got` are the result: the read parked the
fiber, not the scheduler.

It needs a real pty — hence the Python driver rather than a `kai test`
case. It is not wired into `make test`: this is a probe answering an
upstream question, not a regression guarding terevaka's own behaviour.
Re-run it when the compiler moves, before betting fiber work on it.

## `term_probe_check` — the regression behind `term.probe`

Answers three questions about `term.probe`, none of which a `kai test`
case can reach, because all three need a real terminal on the other end
of the pipe:

1. does it come back with what the terminal answered?
2. does it come back empty, promptly, when nothing answers?
3. does it leave the terminal the way it found it, either way?

The driver forks a pty and plays the terminal itself. `answer` waits
for the graphics query to arrive and replies the way a kitty-capable
terminal would; `silent` says nothing, which is what an emulator that
does not speak the protocol does. Both runs then check that raw mode
was restored — with it left on, the report lines would arrive with bare
LF instead of CRLF, which is what the driver actually asserts.

```sh
make test-probe                        # runs both, or says why it skipped
make test-probe PYTHON=/usr/bin/python3
```

Unlike `raw_park_probe`, this one **is** wired into `make test`: it
guards terevaka's own behaviour rather than asking upstream a question.
It needs a Python with `pty`, and reports a skip rather than failing the
suite when there is none — a missing interpreter is not a regression.
`PYTHON` picks the interpreter, which matters where a version manager
shadows `python3` with a shim that resolves to nothing.

The no-tty path needs no pty at all, and is worth re-checking by hand
when the shim changes, since it is the one users hit by accident:

```sh
./build/term_probe_check < /dev/null   # "no answer", immediately, no query sent
```
