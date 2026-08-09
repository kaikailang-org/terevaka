# Changelog

All notable changes to terevaka are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project versions track Semantic Versioning loosely while
the surface is pre-1.0 (every release may break shape).

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
