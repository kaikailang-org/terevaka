# terevaka design

Living design document for the kaikai terminal-UI (TUI) framework.
terevaka is the terminal face of the lnds ecosystem — the way a
kaikai program presents an interactive interface in a terminal,
the same way `manutara` presents one over HTTP.

> **Status:** not started. This document pins the *direction*
> before any code lands. The load-bearing claims (structured
> concurrency for UI tasks, the reactor parking stdin/clock fibers)
> are verified against `kai 0.84.0` — see §*The reactor* and
> §*The concurrency gate*.

## The name

`terevaka` is the highest volcano of Rapa Nui — the point from
which you can see the whole island. The metaphor is the lookout:
a TUI is the vantage from which the user sees their whole
application in the terminal. The name follows the ecosystem's
Rapa Nui convention (`kaikai`, `ahu`, `kohau`, `henua`,
`manutara`, `hopu`), chosen as a *neutral geographic* site — not
a ceremonial one, not a language edition name (those use Rapa Nui
geography too: Tongariki → Hanga Roa → Orongo → Anakena), and with
no Spanish double-reading. Registered in
`kaikai-docs/framework-naming.md`.

## Context

terevaka sits beside `manutara` as a presentation-layer product —
not part of the `kaikailang-org/*` infrastructure stack, a layer-5
consumer of it:

```
kaikai      (the language — effects, fibers, Perceus, structured concurrency)
   ↓
ahu         (concurrency and fault-tolerance — cells, restart, nurseries)
   ↓
(kohau / henua as needed by the app)
   ↓
terevaka    (this project — terminal UI)     manutara (web UI)
```

terevaka builds on **ahu**, not on raw kaikai primitives, wherever
both apply — its stateful widgets are ahu cells, its supervision is
ahu's restart helpers, its lifecycle is `ahu.app.run_app`.

## Thesis: the structured-concurrency TUI

Every TUI framework treats concurrency as the hard, secondary
problem — the thing bolted onto a fundamentally sequential core.

- **The Elm Architecture / TEA** (Bubble Tea in Go) exiles
  concurrency to `Cmd` — a reified sub-language of commands the
  runtime interprets — *because the pure `Model`/`update`/`view`
  loop cannot itself hold a running task*. A spinner is a
  self-rescheduling `tea.Tick`; an in-flight request is a `Cmd`
  that eventually sends a `Msg`; cancelling it is a
  `context.Context` you thread by hand. Three concepts for "spin
  while you wait".
- **ratatui** (Rust) gives you the render loop and leaves
  concurrency entirely to you: open your own threads, sync your own
  channels, cancel by hand.
- **Textual** (Python) recognised the problem and added `@work`
  workers — but on asyncio, with best-effort cancellation and no
  real supervision tree.

**None of them has structured concurrency with cancellation
guaranteed by lexical scope. kaikai does.** That is the property no
other framework can copy without rewriting its runtime, and it is
terevaka's center — the same move `manutara` makes with effect
rows.

> **terevaka: the first TUI where the UI tree *is* a supervision
> tree. Every live concurrent task — a spinner, a `tail -f`, an
> in-flight request, an animation, a panel refreshing at its own
> rate — is a fiber supervised by a nursery, not a callback and not
> a reified `Cmd`. Cancellation, supervision, and "what this part of
> the UI is allowed to touch" all fall out of the type system, not
> the author's discipline.**

This dissolves TEA's central event loop. TEA *must* have one,
because the `Model` is a single value; terevaka does not, because
state is distributed across isolated fibers (BEAM-style private
heaps). TEA was the right answer in a world without cheap fibers or
effects. terevaka is what you write when you have both.

## Why not just port TEA (or immediate-mode)

- **TEA / Bubble Tea** — pure, predictable, but the `Cmd` escape
  hatch is a tax paid on every concurrent thing, and the single
  central `update` becomes a state machine that must disambiguate
  "spinner tick" from "result arrived" from "user hit Ctrl-C while
  waiting". The pain is structural, not incidental. terevaka keeps
  TEA's good idea (state transition as a pure function:
  `(State, Event) -> State`) but moves it *inside* a fiber, where it
  is already `ahu.cell`'s `step` function, and drops the central
  loop.
- **Immediate-mode** (Dear ImGui, ratatui's draw loop) — simple
  mental model, but couples logic to painting, makes the view
  untestable (you cannot diff a tree that never existed as a value),
  and fights Perceus (re-allocating the whole frame each tick).
  terevaka keeps immediate-mode available — but as *one handler of
  the `Render` effect*, not as the primary surface (see §*Render*).
- **Retained-mode widget trees with callbacks** (tview, classic
  GUIs) — familiar, but a pile of hidden mutable state and callback
  spaghetti. terevaka's stateful widgets have their state *inside a
  fiber*, addressable only by typed message — the opposite of hidden
  shared mutation.

## The three levels of a widget

The trap — the one that looks elegant and ends in an ergonomics
disaster — is "every widget is a fiber" or "every widget is an
effect". Nobody wants to write a fiber for a `label`, and they
won't. The right model has **three levels**, each using the kaikai
mechanism that fits, and the author picks the level the component
actually needs. This respects *few forms, each with clear intent*
and *approachability beats one-canonical-form*: the cost of
concurrency is proportional to the concurrency you actually use.

### Level 1 — stateless leaf → a VALUE

A `Text`, a `Box`, a `Row` is an immutable value. It composes, it is
tested by structural equality, Perceus recycles it for free. This is
the HTML-as-values shape from manutara, applied to the terminal.
**The 90% case. No fiber, no effect, no ceremony.**

```kai
type Style = { fg: Color, bg: Color, bold: Bool }
type Ui =
  | Text(Style, String)
  | Row([Ui])
  | Col([Ui])
  | Box(Border, Ui)
  | Focusable(FocusId, Ui)
  | Dynamic(Pid[ViewReq])      # a hole fed by a live cell (Level 2)

# A view is a pure function of state — testable, composable.
fn header(s: AppState) : Ui =
  box(single, row([ text(title_style, s.title), spacer(), clock(s.now) ]))
```

### Level 2 — live, stateful widget → an ahu CELL (fiber + mailbox)

A `TextInput` that remembers its cursor, a `List` with a selection,
a panel doing `tail -f` — state that lives over time and may run
concurrently. This **is `ahu.cell`**, reused, not reinvented:
`step: (State, Msg) -> StepResult[State] / e`. The state lives
*inside* the fiber (no global `Model`); the mailbox type is the set
of events the widget accepts; supervision means a crashed widget is
revived by `restartable_cell` without taking down its siblings.

```kai
type InputMsg = Key(Char) | Backspace | GetValue(Pid[InputReply])

fn input_step(s: InputState, msg: InputMsg) : StepResult[InputState] / Render =
  match msg {
    Key(c)          -> cell.keep(insert(s, c))
    Backspace       -> cell.keep(delete_back(s))
    GetValue(reply) -> { Actor.send(reply, Value(s.text)); cell.keep(s) }
  }

# the widget is a cell; its Pid is how the rest of the UI talks to it
fn text_input(initial: String) : Pid[InputMsg] / Spawn + Render =
  with_cell(InputState(initial, 0), input_step, (pid) => pid)
```

That `step` function *is* TEA's `update`, but scoped to one widget's
private state inside one fiber — not a single global `update` that
must handle every message in the program.

### Level 3 — what a fragment may touch → the ROW (capability)

A function that draws declares `/ Render`. One that reads keys,
`/ Input`. One that requests focus, `/ Focus`. One that starts a
child task, `/ Spawn`. The compiler **rejects** a "read-only" view
that tries to start a fiber or paint outside its region. This is the
TUI analogue of manutara's capability-typed routes: least-privilege
by construction, testing by swapping effect handlers, and a
capability audit derivable from the rows.

```kai
fn view(s: AppState)   : Ui                     # pure — touches nothing
fn key_handler(k: Key) : Unit / Input + Focus   # may read input + move focus, nothing else
fn fetch_panel(u: Str) : Ui / Spawn + Render + Cancel   # may spawn tasks + paint
```

This is the *secondary* thesis (capability-typed UI) — real and
kaikai-native, but it serves the view layer; the framework's heart
is the structured concurrency above it.

## Render: a value AND an effect, in clean layers

The view produces a **value**; a handler of the `Render` effect
paints it. This is not a compromise — it is the only shape that
survives Perceus *and* gives testing-without-mocks.

```kai
fn view(s: State)  : Ui                # pure value: testable, composable, diffable
fn paint(ui: Ui)   : Unit / Render     # effect: the runtime does it
```

- `view` returns the `Ui` tree. The runtime **diffs it against the
  previous frame** (double-buffer; emit only the cells that changed
  — what every serious TUI does internally) and paints the delta.
- The `Render` **handler is the seam**: in production it writes ANSI
  to the terminal; in test it captures to a string buffer you assert
  against. *The effect handler is the injection — testing without
  mocks, identical to manutara's property 2.*
- **Immediate-mode falls out for free.** It is just a *different
  handler* of the same `Render` effect — one that executes draws
  immediately instead of accumulating a tree. The same `/ Render`
  signature serves retained-mode and immediate-mode depending on who
  handles it. Reach for the immediate handler only for the rare case
  (a game canvas, a 10k-point plot where building the tree does not
  pay). One effect, two handlers — a thing no other TUI can say.

## The concurrency gate (verified)

terevaka's thesis requires that a fiber sleeping on `Clock` or
blocked on `Input` does **not** block the scheduler — otherwise the
spinner-during-request freezes. This is the reactor (R1 file/sleep/
process, R2 TCP, R3 stdin, R4 signal), and it is **already shipped**
in kaikai (`kai info fibers`). The gate — two fibers progressing
concurrently, one sleeping, one working — passes on `kai 0.84.0`:

```kai
import time
import spawn

fn main() : Unit / Spawn + Stdout + Clock + Cancel = {
  nursery { n ->
    let _spin = n.spawn(() => spin_frames(0))      # a spinner
    let work  = n.spawn(() => slow_work())          # an in-flight "request"
    let result = n.await(work)                      # awaits work, spinner keeps spinning
    Stdout.print("got result: #{result}")
    # leaving the nursery cancels the spinner automatically
  }
}
```

Observed output (the spinner interleaves with the work — they run
concurrently, neither blocks the other):

```
  spin frame 0
work: starting (will 'fetch' for ~100ms)
  spin frame 1
  spin frame 2
  spin frame 3
work: done
got result: 42
```

This is the spinner-during-request that is painful in Bubble Tea
(`tea.Tick` + `Cmd` + a `context.Context`) and native here: two
sibling fibers in a nursery, cancellation on scope exit. No tick
scheduler, no command reification, no manual context threading.

## The application shape

A terevaka app is a tree of nurseries = a supervision tree. The
state is distributed across fibers; there is no central event loop.

```kai
fn run(initial: AppState)
  : Unit / Spawn + Input + Render + Clock + Signal + Cancel = {
  nursery { root ->
    root.spawn(() => input_pump())          # reads keys, routes to widget mailboxes
    root.spawn(() => render_loop(initial))  # diffs Ui vs previous frame, paints delta
    # concurrent panels, each at its own rate, all supervised:
    root.spawn(() => clock_panel())
    root.spawn(() => log_tail_panel("/var/log/app.log"))
    # Ctrl-C → Signal → Cancel propagates through the whole tree,
    # cleanup handlers run in order, every fiber drains. No survivors.
  }
}
```

Four panels at four rates = four cells, four mailboxes, one nursery.
Each advances on its own clock. Non-blocking input is *another*
fiber routing keys to mailboxes. **No central event loop is the
bottleneck** — the conceptual break from TEA, made possible because
state is in isolated fibers, not one `Model`.

## What v1 ships

1. **The `Ui` value tree** (Level 1) — `Text` / `Row` / `Col` /
   `Box` / `Focusable`, with style, and a pure `view`.
2. **The `Render` effect + a diffing terminal handler** — double-
   buffer, emit-only-changed-cells, plus a string-capture handler
   for tests.
3. **The `Input` effect + an `input_pump` fiber** over the reactor's
   R3 stdin parking (keys, eventually resize via R4 `SIGWINCH`).
4. **Live widgets as ahu cells** (Level 2) — a `TextInput` and a
   `List` reference widget, each a `with_cell`.
5. **The nursery-rooted app shell** — `run(...)`, signal-driven
   graceful shutdown via `ahu.app.run_app`, the spinner-during-task
   pattern as the headline example.
6. **Capability-typed view functions** (Level 3) — `view` pure,
   handlers row-annotated, demonstrated in the reference app.

## What v1 explicitly does NOT ship

- **A retained widget tree with hidden mutable state.** Widgets are
  values (Level 1) or cells (Level 2); there is no third
  mutable-object model.
- **A central `Model`/`update` loop.** Deliberately dissolved; see
  the thesis.
- **Mouse support beyond basics.** Keyboard-first v1; mouse events
  layer on the same `Input` effect later.
- **A macro/DSL for layout.** kaikai has no macros; layout is `Ui`
  values built by function composition (same discipline as
  manutara's view layer).
- **Animations framework.** An animation is just a fiber that emits
  frames on a `Clock`; a helper may land once the pattern recurs.

## Foundational principle: terevaka builds on ahu

Like the rest of the stack, terevaka builds on **ahu**: stateful
widgets are cells, supervision is restart helpers, the app shell is
`ahu.app.run_app`, and concurrent panels are nursery-scoped fibers.
Where a use case cannot be expressed through ahu/kaikai primitives,
the gap is filed against the right layer — not worked around inside
terevaka.

## Open questions (to close before/with the first code lane)

1. **Input encoding** — how keys/escape-sequences are modeled as the
   `Input` effect's op result (a `Key` sum type; mapping terminfo /
   ANSI escape sequences). Leaning: a `Key = Char(Char) | Enter |
   Tab | Arrow(Dir) | Ctrl(Char) | ...` sum type, parsed by the
   input pump.
2. **Render diffing granularity** — cell-level vs line-level diff,
   and how the `Dynamic(Pid)` holes (Level-1 leaves fed by Level-2
   cells) are spliced into the frame without copying the whole tree
   across the fiber boundary (the BEAM copy cost — see Risks).
3. **Focus model** — `Focus` as an effect (request/release/next) vs
   focus as state in a root cell. Leaning: a `Focus` effect handled
   by a focus-manager fiber.
4. **`SIGWINCH` / resize** — via reactor R4 signal parking; how a
   resize event reaches every panel (broadcast to mailboxes).

## Risks / dependencies (verified where possible)

- **BEAM copy cost in the view.** Messages copy across fiber heaps.
  A large `Ui` crossing a fiber boundary each frame copies in full.
  Mitigation: *one* fiber (the renderer) assembles the final tree;
  cells send small deltas / sub-trees, not whole frames. Gate: an
  N-panel dashboard must not scale copy cost with total frame size.
  This is a real perf dependency to measure, not a deferral.
- **The reactor.** *Resolved* — R1–R4 shipped (file/sleep/process,
  TCP, stdin, signal); the concurrency gate passes on 0.84.0. This
  was the #1 technical blocker and it is closed.
- **One-shot resume.** kaikai's resume is one-shot, which fits input
  (read a key, resume once). Widgets that "listen to a stream of
  keys" are loop-in-fiber receiving from a mailbox, never multi-shot
  resume — designed with the grain of the language.

## References

- `kaikai/docs/structured-concurrency.md`, `actors.md`,
  `effects.md` — the substrate (nursery, Spawn, Cancel, Clock,
  Signal, the reactor).
- `ahu/docs/design.md` — `with_cell` / `restartable_cell`, the
  Level-2 component model terevaka reuses.
- `manutara/docs/design.md` — the sibling presentation layer;
  shared lineage (view-as-values, capability-typed, testing via
  effect handlers).
- The Elm Architecture / Bubble Tea (Go) — the `Model`/`update`/
  `view` + `Cmd` lineage terevaka keeps the good half of and
  dissolves the central loop of.
- ratatui (Rust), Textual (Python), tview (Go) — immediate-mode,
  worker-based, and retained-tree prior art, each lacking structured
  concurrency.
- Erlang `wx` / OTP — supervised UI processes, the closest prior
  art to "UI tree = supervision tree", but without lexical-scope
  cancellation.
- SwiftUI / FRP — view-as-function-of-state, the reactive lineage
  terevaka approximates with cells rather than a signal graph.
