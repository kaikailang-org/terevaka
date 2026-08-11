# tools/

## `raw_park_probe` — the v0.2 gate

Answers one question: **does a fiber reading the terminal park itself,
or park the scheduler?**

The whole reason v0.1 threads widgets as plain values instead of fibers
is that a blocking raw read used to freeze every other fiber, so the
input pump could not be a fiber. The design (`docs/design.md`) calls
this the #1 technical blocker for v0.2.

The probe runs a reader fiber (`Stdin.read_bytes(1)`) alongside a
ticker fiber, with `term.raw_enable()` in effect, under a real pty. If
the reader parks, the ticker keeps ticking while it waits and the
reader still receives the keystroke sent afterwards. If it parks the
scheduler instead, the ticker produces nothing.

```sh
CFLAGS="-std=c99 -O2 -include c/terevaka_term.h c/terevaka_term.c" \
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
Re-run it when the compiler moves, before betting v0.2 work on it.
