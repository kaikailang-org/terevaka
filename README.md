# terevaka

A terminal-UI (TUI) framework for [kaikai](https://github.com/lnds/kaikai).
The terminal face of the lnds ecosystem — what `manutara` is for the
web, terevaka is for the terminal.

> **Status:** v0.1 — a working framework. The `Ui` value tree, the
> terminal layer (with robust size detection), eight widgets (menu,
> listbox, input, form, popup, confirm, statusbar, board), and a
> Model/update/view runtime with a flicker-free in-place repaint all
> compile and run on `kai 0.109.2`. The full-screen kanban example
> exercises the lot. The fiber/nursery architecture from
> `docs/design.md` (each live widget a supervised `ahu.cell`) is
> **v0.2**, blocked on raw-mode-on-the-reactor upstream — see
> §*What's a value vs what's deferred*.

The name is the highest volcano of Rapa Nui — the point from which
you see the whole island. The metaphor: a TUI is the vantage from
which the user sees their whole application.

## What it looks like

The demo (`examples/demo/`): a navigable menu, a text-input field, a
modal popup, and a live UTC clock that ticks on its own.

```
╭────────────────────────────────────────────────╮
│ terevaka · demo            21:19:42 UTC         │
│                                                 │
│ ▸  Edit name                                    │
│    Greet                                         │
│    About...                                      │
│    Quit                                          │
│                                                  │
│ name ⟨ (empty) ⟩                                 │
│                                                  │
│ ↑↓/jk move · Enter select · q quit               │
╰────────────────────────────────────────────────╯
```

```sh
make          # builds the shim + all examples (demo, gallery, kanban)
./build/demo  # needs a real TTY
```

## The model

terevaka is TEA-shaped (Elm Architecture): you write a `Model`, an
`update(model, key) -> Step`, and a `view(model) -> Ui`; the runtime
owns raw mode, the input loop, painting, and teardown. The demo's
core is ~100 lines of model + update + view — **no terminal
plumbing, no manual ANSI, no box drawing**.

```kai
import terevaka.app
import terevaka.ui
import terevaka.widget.menu

type Model = { menu: menu.Menu, done: Bool }

fn update(m: Model, k: term.Key) : app.Step[Model] / Ffi + Clock =
  match k {
    Printable(c) -> if c == 'q' { app.quit() } else { app.keep(...) }
    _            -> app.keep(Model { menu: menu.update(m.menu, k), done: m.done })
  }

fn view(m: Model) : Ui / Clock =
  ui.box(48, "menu", menu.view(m.menu))

fn main() : Int / Ffi + Clock = {
  app.run(initial(), update, view, 200)   # 200ms idle tick
  0
}
```

## The pieces

| Module | What it is |
|---|---|
| `terevaka.term` | The terminal layer: raw mode, non-blocking key reads (assembles arrow keys), no-newline writes, ANSI builders, geometry. The only module carrying `Ffi`. |
| `terevaka.ui` | The `Ui` value tree (`Text`/`Row`/`Col`/`Box`/`Pad`) + `render(Ui) : [String]`. Pure: building and rendering a view is effect-free, so views are testable by structural equality. `Box` takes an inner width *and* an inner height (`box_filled`, 0 = auto) so panels can fill the screen. `visible_len` measures true display columns: it skips ANSI codes and sums `text.char_width` over the remaining codepoints (2 for emoji/CJK/fullwidth, 0 for combining marks, 1 otherwise). |
| `terevaka.app` | The Model/update/view runtime. `run` and `run_overlay` (for modals). Non-blocking poll loop so a clock/spinner advances on idle. **Flicker-free**: clears once on entry, then repaints in place (cursor home + erase-to-end-of-line per row, never a whole-screen clear per frame) and only when the rendered frame actually changes. |
| `terevaka.clock` | `hhmmss_utc()` — a tiny clock helper over the `Clock` effect. |
| `terevaka.widget.menu` | A navigable list: cursor, Up/Down + j/k, highlighted selection. |
| `terevaka.widget.listbox` | Like menu, but **scrolls**: shows a viewport of N rows, the window follows the cursor, with ▲/▼ indicators and an `n-m/total` counter. |
| `terevaka.widget.input` | A single-line text field: typing, backspace, Enter/Esc → `Done`/`Cancelled`. |
| `terevaka.widget.form` | Form controls: **checkbox** (Space toggles), **radio** group (one of N), **button** (Enter/Space presses). |
| `terevaka.widget.popup` | A titled modal box, drawn as an overlay over the base UI. |
| `terevaka.widget.confirm` | A Yes/No modal dialog: Left/Right or h/l moves, Enter answers, Esc = No, y/n shortcuts. |
| `terevaka.widget.statusbar` | A bottom help bar: `key action · key action · …` from a list of hints. |
| `terevaka.widget.board` | A **kanban board**: N colored columns of typed cards (`{ref, kind, title}`), sized to a given inner width and height (so it fills the screen), with the move-card-between-columns and reorder-within-column operations the leaf widgets can't express (moving a card crosses two lists). Card type markers are ASCII (`+`/`!`/`~`/`.`) so the column widths are exact on every terminal. |

Layout primitives in `ui`: `Row` composes children **side by side,
multi-line** (so panels align); `split(lw,lt,left, rw,rt,right)`
builds a two-pane layout; `columns([Ui])` places N panels in a row
(the general form `split` is the 2-case of).

## Examples

- **`examples/demo`** — menu + text input + popup + live clock.
- **`examples/gallery`** — split-pane, scrolling listbox, form
  controls (checkbox/radio/button), status bar, confirm overlay,
  Tab-cycled focus.
- **`examples/kanban`** — a working **full-screen** kanban board: five
  workflow columns (backlog/todo/doing/done/cancelled) sized to fill
  the terminal width *and* height, cards shown as `[REF] <type-marker>`
  + title. Move cards between columns (`<` `>`), reorder within a
  column (`K` `J`), add cards (text input), delete (confirm dialog).
  Proof the widget set composes for a real application.

Every widget exposes the same shape: a `State` type, an `update(State,
Key) -> State` (or an `Outcome`), and a `view(State) -> Ui`. That is
the component contract.

## What's a value vs what's deferred (honesty)

The design (`docs/design.md`) has three levels: **L1** stateless
leaves as values, **L2** live widgets as `ahu.cell` fibers, **L3**
capabilities as effect rows. v0.1 ships **L1 in full** (the `Ui`
tree) and the widgets as **plain-value state machines threaded by the
app** — *not yet* as fibers.

Why: the design's headline is "every live concurrent task is a
supervised fiber", which needs the input pump to be a fiber parked on
the reactor. But the kaikai reactor parks *line-buffered* stdin
(`Stdin.read_line`), while a TUI needs *raw byte-at-a-time* reads.
**Raw-mode-on-the-reactor is a stdlib gap.** Until it closes, a
blocking raw read would freeze the whole scheduler (measured — see
`spike/README.md`), so v0.1 uses a non-blocking `poll()` loop in one
fiber. The *effect* the design promises (a clock that ticks without
input, concurrent panels) is reachable — the clock demo proves it —
but via poll, not fibers.

v0.2, once the reactor gap closes: widgets become `ahu.cell`s, the
input pump and render become sibling fibers under a nursery, and the
spinner-during-task pattern (verified in `docs/design.md`) wires into
a real interactive app.

## Known limitations

- **Width is measured per codepoint, not per grapheme cluster.**
  `visible_len` delegates to the stdlib's `text.char_width`, so the
  East Asian Width table and zero-width marks are handled. What remains
  out of scope is UAX #29 segmentation: an emoji ZWJ sequence, a flag,
  or a skin-tone modifier renders as one wide glyph but is measured as
  its parts. That limit is the stdlib's, and closing it belongs
  upstream. (The bundled examples sidestep the question by using ASCII
  type markers.)
- **No real per-widget concurrency.** Widgets are value state machines
  threaded by the app, not fibers (see *What's a value vs deferred*).
  The clock/spinner tick via the poll loop; a spinner *during a real
  in-flight request*, a live `tail -f`, or panels at independent rates
  need the fiber architecture — v0.2, blocked on raw-mode-on-reactor.
- **No mouse, no resize handling (`SIGWINCH`).** Keyboard-first.
- **UTC clock only** (no localtime in stdlib yet).

## Build

terevaka binds the terminal through a C shim (`c/terevaka_term.{c,h}`)
the way `kohau` binds libsqlite3. `kai build` does not inject link
flags, so the `Makefile` passes the shim through `CFLAGS`: the header
via `-include`, the source as a plain translation unit the driver
hands to the C compiler. Requirements: `kai` on `PATH` (verified
against 0.109.2), a C compiler.

```sh
make            # build all examples (demo, gallery, kanban)
make run        # build + run the demo
make run-kanban # build + run the kanban board
make test       # the framework's own tests
make clean
```

`make test` runs `kai test` per module rather than package-wide:
terevaka is a library with no entry point, and a bare `kai test`
resolves the manifest's default entry (`main.kai`) and fails.

## Using terevaka as a dependency

`kai add github.com/kaikailang-org/terevaka` resolves the import, but
that is not enough to link: the C shim is not part of the package as
far as `kai build` is concerned, so the build fails with undefined
`kai_tvk_*` symbols. The consumer passes the shim in `CFLAGS` the same
way this repo does, pointing at the copy inside the package cache:

```make
# Package cache root. `kai` honours $KAIKAI_CACHE; the default is
# ~/Library/Caches/kai/pkg on macOS, ~/.cache/kai/pkg on Linux.
KAIKAI_CACHE ?= $(HOME)/Library/Caches/kai/pkg

TVK_SHA  := $(shell awk '/^name = "terevaka"/{f=1} f && /^sha = /{gsub(/[",]/,"",$$3); print $$3; exit}' kai.lock)
TVK_ROOT := $(KAIKAI_CACHE)/github.com/kaikailang-org/terevaka/$(TVK_SHA)
KAI_CFLAGS := -std=c99 -O2 -include $(TVK_ROOT)/c/terevaka_term.h $(TVK_ROOT)/c/terevaka_term.c

build/app: $(SRC) kai.lock
	mkdir -p build
	CFLAGS="$(KAI_CFLAGS)" kai build . -o $@
```

Two details that bite. Derive the sha from `kai.lock` rather than
hardcoding the path — a hardcoded one breaks on the next `kai update`.
And give `KAIKAI_CACHE` a default: the variable is not exported unless
you set it, so a recipe that reads it bare expands to an empty prefix
and the include path silently points at `/github.com/...`.

If `kai build` ever grows a way for a package to declare its own link
inputs, that supersedes all of this.

## Layout

```
terevaka/
├── kai.toml
├── Makefile                  # shim in CFLAGS → kai build
├── docs/design.md            # the architecture (incl. the fiber v0.2 plan)
├── c/terevaka_term.{c,h}     # terminal shim (raw mode, poll, write, size)
├── terevaka/                 # the importable modules
│   ├── term.kai
│   ├── ui.kai
│   ├── app.kai
│   ├── clock.kai
│   └── widget/{menu,listbox,input,form,popup,confirm,statusbar,board}.kai
├── examples/
│   ├── demo/main.kai         # menu + input + popup + live clock
│   ├── gallery/main.kai      # split-pane, listbox, form, confirm, focus
│   └── kanban/main.kai       # full-screen kanban board
└── spike/                    # the original raw-FFI spike (kept for reference)
```

## License

TBD — will match the kaikai ecosystem license.
