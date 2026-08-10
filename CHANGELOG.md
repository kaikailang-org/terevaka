# Changelog

All notable changes to terevaka are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project versions track Semantic Versioning loosely while
the surface is pre-1.0 (every release may break shape).

## [Unreleased]

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
