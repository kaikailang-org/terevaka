# terevaka spike — a real TUI in kaikai

A working spike proving that the terminal surface terevaka needs is
reachable **today** on `kai 0.84.0`: raw-mode input, byte-at-a-time
key reading (including arrow-key escape sequences), ANSI render, a
**live clock that ticks on its own while you navigate**, and a
**modal popup** — through a thin C shim for the parts kaikai's stdlib
does not yet expose.

It is a small interactive app: a **navigable menu** (↑/↓ or `j`/`k`),
a **text-input field**, a **live UTC clock** in the corner, and an
**About popup**.

```
  terevaka spike  -  a tiny TUI in kaikai          20:58:19 UTC

    > [ Edit name ]          +--------------------------+
      Greet                  |   About terevaka         |
      About...               |   A TUI framework for     |
      Quit                   |   kaikai. The UI tree IS  |
                             |   a supervision tree.     |
  name: ana                  |   [ Enter / Esc to close ]|
                             +--------------------------+
  saved: ana
```

The clock is the point: leave the app idle and the time keeps
advancing — the UI has its own time that progresses without input.
Verified: idle for 3 seconds with no keys, the clock ticked
`20:58:17 → 18 → 19 → 20` across ~17 repaints.

## Run it

Needs a real terminal (TTY) — raw mode does not work through a pipe
for interactive use.

```sh
make
./build/demo
```

- `↑`/`↓` or `j`/`k` — move the menu cursor
- `Enter` — select: *Edit name* → text field; *Greet* → greets you;
  *About...* → opens the modal popup
- while editing: type, `Backspace` to delete, `Enter` to save, `Esc`
  to cancel
- in the popup: `Enter`/`Esc`/`q` closes it (the clock keeps ticking
  under it)
- `q` — quit (raw mode is restored and the screen cleared on exit)

## What is real here (verified)

Every piece below compiles and runs on `kai 0.84.0`:

- **Raw terminal mode** via FFI to `termios` (`c/tui_shim.c`):
  `tcgetattr`/`tcsetattr` with `ICANON`/`ECHO`/`ISIG` cleared, so
  reads return one byte at a time and keystrokes don't echo.
- **Non-blocking key reading** — `kai_tui_poll_key(ms)` waits up to
  `ms` for a byte and returns a synthetic `Tick` on timeout, so the
  loop refreshes (and the clock advances) even when idle. The kaikai
  side assembles arrow keys from the 3-byte `ESC [ A/B/C/D` sequences
  into a `Key` sum type. Verified: piping `\033[B\033[B` moves the
  cursor down twice.
- **A live clock** — `time.now()` (the `Clock` effect) formatted to
  `HH:MM:SS UTC` with integer arithmetic, repainted every frame.
  Verified: idle for 3 s with no keys, the clock ticked
  `20:58:17 → 18 → 19 → 20` across ~17 repaints. The UI has its own
  time that advances without input — the whole reason a TUI needs
  non-blocking input.
- **A modal popup** — `Screen = Menu | Editing | Popup`; the popup is
  a bordered box painted *last*, as an overlay on top of the UI. The
  clock keeps ticking underneath it. Verified: navigate to *About...*,
  Enter opens it, Esc closes it.
- **ANSI render** — cursor positioning (`ESC[r;cH`), screen clear,
  colors, inverse video — written through `kai_tui_write()` (a
  no-trailing-newline stdout write, since stdlib `print` always
  appends `\n`, which a TUI cannot use for absolute positioning).
- **The TEA-shaped core** — `view(state)` paints, `update(state, key)`
  is a pure transition over a three-screen state machine. Menu
  navigation, text editing, and the popup are exercised end-to-end by
  piping keystrokes.

## Why a C shim

kaikai's FFI v1 cannot pass `struct termios` by value or take
pointer-to-T arguments (`kaikai/docs/ffi.md`). So, exactly like
`kohau` does for libsqlite3, the shim flattens the termios dance and
the no-newline write into a few `extern "C"` functions with
primitive (`Int`/`String`) signatures. This is the seed of the
**terminal package** terevaka will eventually ship — the spike binds
it inline; the package would own it.

The shim surface (`c/tui_shim.{c,h}`):

```c
int64_t kai_tui_raw_enable(void);      // termios → raw, save original
int64_t kai_tui_raw_disable(void);     // restore saved termios
int64_t kai_tui_read_key(void);        // read one byte (blocking), or -1
int64_t kai_tui_poll_key(int64_t ms);  // poll up to ms; byte, -1 timeout, -2 eof
int64_t kai_tui_write(const char*);    // write to stdout, no newline, flush
```

## The blocking-FFI finding (why poll, not fibers)

The honest reason the live clock uses `poll()` in one loop instead of
the design's two fibers (an input-pump fiber + a clock cell): **a
blocking FFI `read()` freezes the whole kaikai scheduler.** Measured
directly — spawn a ticker fiber (`time.sleep` + print) and a reader
fiber blocked on a C `read()`; the ticker prints `tick 0`, then
*stalls* for the entire time the reader is blocked, and only resumes
after a key arrives:

```
tick 0
reader: blocking on read()...
reader: got 122          ← 2 seconds later
tick 1                   ← ticker only now resumes
```

A blocking FFI call is opaque to the cooperative scheduler — it parks
the OS thread, not the fiber. terevaka's real architecture needs
input parked on the **reactor** (R3 stdin) so the input fiber yields
like `Clock.sleep` does. But the reactor parks `Stdin.read_line` /
`read_bytes`, which are *line-buffered*, not the raw byte-at-a-time
reads a TUI needs. **Raw-mode-on-the-reactor is a stdlib/runtime gap.**
`poll()` with a timeout is the honest bridge: it lets one loop check
for input without committing to a blocking read, so the clock ticks.

## What this spike is NOT (yet)

This proves the *terminal surface* and the *idle-tick* property. It
does **not** yet use the fiber architecture in `../docs/design.md`:

- **No fibers / nursery.** The loop is one recursive poll/timeout
  function (`run_loop`), not the design's *render fiber + input-pump
  fiber + widget cells under one nursery*. The whole app is one
  `update`; the design splits each widget into its own `ahu.cell`.
- **The clock is poll-driven, not a fiber.** It advances because the
  loop wakes every 200 ms and re-reads `time.now()`, not because a
  clock cell ticks on `Clock.sleep` as a sibling fiber. The *effect*
  is the same (a clock that advances without input); the *mechanism*
  is the spike's shortcut. The fiber version is blocked on the
  raw-mode-on-reactor gap above.
- **No spinner-during-task.** The design's headline — a spinner
  spinning while a request is in flight, as sibling fibers — is
  verified separately in `../docs/design.md` §*The concurrency gate*,
  but not wired into this interactive demo (same gap).
- **No `Input`/`Render` effects.** The design models input and render
  as effects (capability-typed UI, Level 3). Here they are plain FFI
  calls in the `/ Ffi` row. Lifting them to dedicated effects (so a
  read-only view *cannot* read keys or paint) is the next step.

## Files

```
spike/
├── README.md            # this file
├── Makefile             # shim in CFLAGS → kai build (kohau pattern)
├── demo.kai             # the TUI: Key, view, update, the loop
└── c/
    ├── tui_shim.c       # termios raw mode + key read + no-newline write
    └── tui_shim.h       # extern "C" prototypes
```

## The honest takeaway

The terminal substrate terevaka needs — raw mode, keys, ANSI,
arrow-key sequences — is reachable now with ~60 lines of C shim and
plain kaikai. The interesting design work (fibers as widgets,
structured-concurrency UI, capability-typed input/render) sits on
top of a substrate that already works. This spike de-risks the floor
so the design can build the ceiling.
