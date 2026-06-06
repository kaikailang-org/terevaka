# terevaka

A terminal-UI (TUI) framework for [kaikai](https://github.com/lnds/kaikai).
The terminal face of the lnds ecosystem — what `manutara` is for the
web, terevaka is for the terminal.

> **Status:** v0.1 — a working minimal framework. The `Ui` value
> tree, the terminal layer, three widgets (menu, text input, popup),
> and a Model/update/view runtime with a non-blocking input loop all
> compile and run on `kai 0.84.0`. The fiber/nursery architecture
> from `docs/design.md` (each widget a supervised `ahu.cell`) is
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
make          # builds the shim + the demo
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
| `terevaka.ui` | The `Ui` value tree (`Text`/`Row`/`Col`/`Box`/`Pad`) + `render(Ui) : [String]`. Pure: building and rendering a view is effect-free, so views are testable by structural equality. Includes `visible_len` (counts display columns, skipping ANSI codes). |
| `terevaka.app` | The Model/update/view runtime. `run` and `run_overlay` (for modals). Non-blocking poll loop so a clock/spinner advances on idle. |
| `terevaka.clock` | `hhmmss_utc()` — a tiny clock helper over the `Clock` effect. |
| `terevaka.widget.menu` | A navigable list: cursor, Up/Down + j/k, highlighted selection. |
| `terevaka.widget.listbox` | Like menu, but **scrolls**: shows a viewport of N rows, the window follows the cursor, with ▲/▼ indicators and an `n-m/total` counter. |
| `terevaka.widget.input` | A single-line text field: typing, backspace, Enter/Esc → `Done`/`Cancelled`. |
| `terevaka.widget.form` | Form controls: **checkbox** (Space toggles), **radio** group (one of N), **button** (Enter/Space presses). |
| `terevaka.widget.popup` | A titled modal box, drawn as an overlay over the base UI. |
| `terevaka.widget.confirm` | A Yes/No modal dialog: Left/Right or h/l moves, Enter answers, Esc = No, y/n shortcuts. |
| `terevaka.widget.statusbar` | A bottom help bar: `key action · key action · …` from a list of hints. |
| `terevaka.widget.board` | A **kanban board**: N columns of cards, with the move-card-between-columns and reorder-within-column operations the leaf widgets can't express (moving a card crosses two lists). |

Layout primitives in `ui`: `Row` composes children **side by side,
multi-line** (so panels align); `split(lw,lt,left, rw,rt,right)`
builds a two-pane layout; `columns([Ui])` places N panels in a row
(the general form `split` is the 2-case of).

## Examples

- **`examples/demo`** — menu + text input + popup + live clock.
- **`examples/gallery`** — split-pane, scrolling listbox, form
  controls (checkbox/radio/button), status bar, confirm overlay,
  Tab-cycled focus.
- **`examples/kanban`** — a working kanban board: 3 columns, move
  cards between columns (`<` `>`), reorder within a column (`K` `J`),
  add cards (text input), delete (confirm dialog). Proof the widget
  set composes for a real application.

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

- **Fine alignment with wide glyphs.** `visible_len` counts codepoints
  as width 1; double-width (CJK, emoji) glyphs render wider, so frames
  around rows mixing them can be off by a column. ASCII + the
  box-drawing/arrow set used here align cleanly. The real fix is
  `string.display_width` upstream — filed as kaikai #745 (depends on
  #744, the String/Char byte-vs-codepoint model). terevaka's
  `visible_len` is the interim approximation.
- **No real per-widget concurrency.** Widgets are value state machines
  threaded by the app, not fibers (see *What's a value vs deferred*).
  The clock/spinner tick via the poll loop; a spinner *during a real
  in-flight request*, a live `tail -f`, or panels at independent rates
  need the fiber architecture — v0.3, blocked on raw-mode-on-reactor.
- **No mouse, no resize handling (`SIGWINCH`).** Keyboard-first.
- **UTC clock only** (no localtime in stdlib yet).

## Build

terevaka binds the terminal through a C shim (`c/terevaka_term.{c,h}`)
the way `kohau` binds libsqlite3. `kai build` does not inject link
flags, so the `Makefile` drives `kaic2` (emit C) then `cc` (link the
shim). Requirements: `kai` 0.84.0+ on `PATH`, a C compiler.

```sh
make          # build the demo
make run      # build + run
make clean
```

## Layout

```
terevaka/
├── kai.toml
├── Makefile                  # shim → kaic2 → cc
├── docs/design.md            # the architecture (incl. the fiber v0.2 plan)
├── c/terevaka_term.{c,h}     # terminal shim (raw mode, poll, write, size)
├── terevaka/                 # the importable modules
│   ├── term.kai
│   ├── ui.kai
│   ├── app.kai
│   ├── clock.kai
│   └── widget/{menu,input,popup}.kai
├── examples/demo/main.kai    # the spike, rebuilt on the framework
└── spike/                    # the original raw-FFI spike (kept for reference)
```

## License

TBD — will match the kaikai ecosystem license.
