# Changelog

All notable changes to terevaka are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project versions track Semantic Versioning loosely while
the surface is pre-1.0 (every release may break shape).

## [Unreleased]

### Added

- **The terminal shim travels with the package.** `kai.toml` declares
  `c/terevaka_term.c` in a `[native]` table, so the driver compiles and
  links it for every verb — `build`, `run`, `test`, `install` — on both
  backends. A consumer now writes `kai add
  github.com/kaikailang-org/terevaka` followed by `kai build .` and is
  done: no `Makefile`, no `CFLAGS`, and `kai install
  github.com/owner/app` works on an app that depends on terevaka, which
  it could not before. The header is not declared alongside the source
  because the compiler emits its own declarations from the FFI
  signatures; `include = ["c"]` stays for the `.c` itself.

  Requires kai 0.112.0 or newer, where `[native]` landed. Verified
  against 0.112.1 with an external consumer (`lnds/mark`): bare `kai
  build .` produces a working binary and `kai test .` runs its 53 tests,
  both with an empty `CFLAGS`.

### Changed

- **BREAKING: passing the shim in `CFLAGS` now breaks the link.** There
  is no deduplication between the two channels — the same translation
  unit arriving from both `[native]` and `CFLAGS` is compiled and linked
  twice, and the build dies with `ld: 6 duplicate symbols` naming
  `kai_tvk_raw_enable` and its neighbours.

  Every consumer written against 0.1.4 or earlier carries exactly that,
  since it was the only way to link at all. **To migrate: delete the
  shim from your `CFLAGS` and build with plain `kai build .`** — in
  practice the whole `Makefile` goes, along with the awk that dug
  terevaka's sha out of `kai.lock` to locate the shim in the package
  cache. Setting `KAI_NATIVE_DEPS=0` turns the `[native]` channel off
  and restores the old behaviour for a build that cannot be changed
  yet.

  terevaka's own `Makefile` no longer passes it either; it now only
  wires the dependency graph and the example binaries.

### Fixed

- **The status bar rendered empty on kai 0.117.0.** Its private helper
  was named `one`. In package mode, kai 0.117.0 binds a call made from
  inside a lambda body to a stdlib function of the same name rather
  than the module's own, which here is a zero-argument `one()`. So
  `hs | (h) => one(h)` built and ran with no diagnostic and produced an
  empty string for every hint. The two statusbar tests that pin the
  separator layout caught it. The helper is now `hint_text`, passed
  point-free (`hs | hint_text`), which keeps the module clear of the
  collision. A direct call, a point-free reference and `list.map` all
  resolve correctly even with a colliding name; only the lambda body is
  misbound. Filed upstream as kaikai #1964.

### Removed

- **The `spike/` directory.** It held the original raw-FFI prototype
  that proved the terminal surface was reachable before the framework
  existed — a single-file app with its own C shim (`kai_tui_*`), its
  own `Makefile`, and prose pinned to `kai 0.84.0`. `examples/demo`
  has been the same app on the real framework since v0.1, so the
  repository shipped two implementations of raw mode, key decoding
  and ANSI rendering, one of them dead. The measurement that earned
  the spike its keep — a blocking FFI `read()` freezing the whole
  scheduler, which is why v0.1 polls — now lives in `docs/design.md`
  under the concurrency gate. The code stays in git history at
  `46075f3`.

## [0.1.4] - 2026-08-10

### Fixed

- **End of input no longer spins the loop at full CPU.** `poll(2)`
  reports a closed descriptor as ready forever, and `term.poll_key`
  mapped end-of-input to `Unknown` — the same value it returns for any
  byte it does not decode. Most apps answer `Unknown` with
  `keep(model)`, so `app.run` re-polled a dead descriptor with no delay
  and no keystroke could ever arrive: only SIGKILL ended it. Measured
  at 138% CPU before the fix, exiting in 0.10s after.

  It reaches ordinary setups — `cat doc.md | app`, an ssh session whose
  terminal goes away, a parent that exits.

  The shim was part of the problem: `-2` meant EOF, a `poll` error, and
  "ready but not readable" all at once. It now returns `-2` only for
  end of input and `-3` for errors, and reports a hangup on the write
  end as end of input too. Both loops (`run` and `run_overlay`) end on
  `Eof` in the runtime rather than leaving it to the app, since an app
  that ignores it burns a core.

- **Backspace deleted a byte, not a glyph.** `input.drop_last` sliced
  the last byte off the field with `string.length` / `string.slice`,
  which are byte-indexed — so backspacing over `你好` left a broken
  half-codepoint behind instead of `你`. It now drops the last of
  `string.chars`, which iterates real codepoints. Found by writing the
  input widget's first test, not by using it.

### Changed

- **BREAKING: `term.Key` gains an `Eof` variant.** An exhaustive
  `match` over `Key` in a downstream app stops compiling until it
  handles (or wildcards) the new case. Apps driven by `app.run` need no
  change beyond that: the runtime ends the loop on `Eof` before
  `update` is called.
- **`make test` now runs `test-eof` first**, a regression that starts
  the demo with a closed stdin under a five-second alarm. It cannot be
  a `kai test` case — it needs a real process whose stdin is closed —
  and the alarm is load-bearing: on regression the app never returns.
- **v0.2 is unblocked, and the README says so with a measurement
  behind it.** The reactor's stdin phase closed the upstream gap
  (kaikai #620), and `tools/raw_park_probe` now demonstrates the case
  terevaka actually needs: under a real pty with raw mode on, a reader
  fiber on `Stdin.read_bytes(1)` parks while a ticker fiber keeps
  running, and still receives the keystroke afterwards. What stands
  between here and the fiber architecture is the design work, not a
  dependency.

### Added

- **Every module now has tests** — `app`, `clock`, `term` and the five
  widgets that had none (confirm, form, input, popup, statusbar), on
  top of the ui/menu/listbox/board files: 51 test blocks across 12
  files, up from 17 across 4.

  Two of them are worth calling out. `clock` handles the `Clock` effect
  with a fixed instant instead of reading the wall clock, so it asserts
  exact output (`at(3661) == "01:01:01 UTC"`) rather than a shape —
  which is what carrying the effect in the row buys you. And `term`
  covers only the ANSI builders: raw mode, geometry and `poll_key` need
  a real terminal, so the loop's end-of-input path is covered by
  `test-eof` instead, and the rest is exercised by the examples
  building on every `make`.

## [0.1.3] - 2026-08-10

### Changed

- **The sources follow `kai info idiomatic`.** Three passes, no
  behaviour change: ladders of `if`/`else` over one scrutinee become
  `match` (term.poll_key was four deep with a match buried inside it,
  and every widget dispatching on `Printable(c)` had the same shape);
  fourteen private reimplementations of stdlib functions are deleted in
  favour of `list.length`, `list.nth`, `list.take`/`drop` and
  `math.int.min`/`max`; and every list-building recursion becomes a
  pipe, `list.flat_map` or `list.map_indexed`, which also drops the
  index accumulators they threaded purely to count.
- **Every `pub` symbol carries `#[doc]`** — 115 of them, previously
  documented with plain `#` comments that no tooling could see. They
  now surface in `kai doc`.

### Added

- **Tests for the widgets**: menu (cursor wrap, empty list), listbox
  (the scrolling viewport, both window/list length orders) and board
  (the card-moving operations, which cross two lists). 25 tests total,
  up from 5.

### Fixed

- **`kai test` runs the suite in package mode again.** kaikai 0.111.0
  lifted the entry-point requirement for libraries (#1721, filed from
  here as #1718), so `kai test` and `kai test ./...` both work without
  the manifest naming an entry that a library does not have.

### Known issue

- `|` will not unify an unannotated stage that applies a list-generic
  to a list element — `blocks | (b) => list.length(b)` is rejected
  while `list.map` with the same lambda compiles. Filed upstream as
  kaikai #1743; the point-free section (`blocks | .length()`) is both
  the workaround and the form the idiomatic guide prefers, so nothing
  here is left waiting on it.

## [0.1.2] - 2026-08-09

### Fixed

- **`truncate` cut multibyte text wrong on kai 0.109.2 and earlier.**
  `take_cols` rebuilds the kept prefix with `"#{c}"`, and interpolating
  a multibyte `Char` produced mis-encoded bytes, so a truncated string
  carrying emoji or CJK came out corrupted. kai 0.110.0 fixes the
  encoding upstream (#1671, "encode Show for Char output as UTF-8") and
  nothing in terevaka had to change — but the 0.1.1 test suite had
  frozen the broken behaviour as expected (`truncate("🎉🎉🎉", 5)`
  asserted 3 columns where the correct answer is 5, two whole emoji
  plus the ellipsis). The assertion is corrected and the case pinned by
  string equality rather than width alone.

### Changed

- **Tests move to `terevaka/ui_test.kai`.** Since 0.110.0 package mode
  runs every `*_test.kai` a package owns whether or not anything
  imports it, while `test` blocks inside a module the entry graph does
  not reach are only warned about. `make test` now loops over
  `*_test.kai`, so covering another module is a matter of adding a file.
- **The stated compiler version is now 0.110.0**, verified by
  rebuilding every example and the spike from clean.

### Known issue

- Package mode still cannot run a library's tests: every spec resolves
  the manifest entry (`main.kai`) before discovery, so `kai test` errors
  and `kai test ./...` reports `SKIP terevaka (.) (no main.kai)` and
  exits 1. Filed upstream as kaikai #1718; `make test` is the workaround
  and stays until it closes.

## [0.1.1] - 2026-08-09

### Fixed

- **Display width is measured through the stdlib's width table.**
  `visible_len` decoded UTF-8 by hand from lead bytes, on the premise
  that `string.chars` returns bytes. It returns real codepoints, so the
  decoder ran over already-decoded values and undercounted every wide
  glyph: `🎉` measured 1 column instead of 2, which pushes a box's
  right border out by one cell per emoji on the line. `take_cols` had
  the matching bug, counting one column per non-continuation char, so
  `truncate` cut to *n codepoints* rather than *n columns*.

  Both now delegate the per-codepoint width to `text.char_width` — the
  East Asian Width table the local approximation was standing in for —
  and `take_cols` drops a wide glyph that would straddle the limit
  instead of splitting it. The ANSI-skipping wrapper stays: the stdlib
  measures the codepoints it is given and leaves escapes to the caller.
  This removes `decode_advance` and the local width table.

  No API change — the call sites are the same, they just come out right
  on emoji and CJK.

### Added

- **A `make test` target.** `kai test` with no argument resolves the
  manifest's default entry (`main.kai`), which a library does not have,
  so the target names the module and supplies the shim `CFLAGS`. Tests
  cover columns vs codepoints, ANSI-carrying strings, and truncation on
  wide glyphs.
- **A "Using terevaka as a dependency" section in the README.** `kai add`
  resolves the import but cannot link the C shim, so the consumer has to
  pass it in `CFLAGS` pointing into the package cache — undiscoverable
  before, since what you got was a linker error naming `kai_tvk_*`
  symbols. The recipe is verified end-to-end against a scratch package.

### Changed

- **The stated compiler version is now 0.109.2**, verified by rebuilding
  every example and the spike from clean. The status line claimed
  0.86.1, twenty releases behind what the code builds against.
- **The known-limitation entry on width is narrowed to what remains.**
  Codepoint-level width is now handled; what stays out of scope is
  UAX #29 segmentation (emoji ZWJ sequences, flags, skin-tone
  modifiers), which is the stdlib's limit rather than terevaka's.

## [0.1.0] - 2026-06-06

### Added

- Initial release: the `Ui` value tree (`Text`/`Row`/`Col`/`Box`/`Pad`)
  with a pure `render(Ui) : [String]`, the terminal layer over a C shim
  (raw mode, non-blocking key reads, ANSI builders, geometry), eight
  widgets (menu, listbox, input, form, popup, confirm, statusbar,
  board), and a Model/update/view runtime with a flicker-free in-place
  repaint. The fiber/nursery architecture from `docs/design.md` is v0.2,
  blocked on raw-mode-on-the-reactor upstream.
