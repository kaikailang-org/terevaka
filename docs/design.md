# terevaka design

Living design document for the kaikai terminal-UI (TUI) framework.
terevaka is the terminal face of the lnds ecosystem — the way a
kaikai program presents an interactive interface in a terminal,
the same way `manutara` presents one over HTTP.

> **Status:** partly built. v0.1 ships Level 1 (the `Ui` value tree,
> the terminal layer, eight widgets, a Model/update/view runtime) with
> widgets as plain values threaded by the app; Levels 2 and 3 — widgets
> as actors, capabilities as rows — are the v0.2 target.
>
> The load-bearing claims were first verified against `kai 0.84.0` and
> re-verified on **0.111.0**, which is what §*The concurrency model
> this design targets* now describes. That section is the one to read
> first: parallel-by-default fibers, cooperative cancellation and
> non-selective receive all constrain the design in ways the original
> draft did not account for.

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

### Level 2 — live, stateful widget → an ACTOR (fiber + mailbox)

A `TextInput` that remembers its cursor, a `List` with a selection,
a panel doing `tail -f` — state that lives over time and may run
concurrently. The state lives *inside* the fiber (no global `Model`);
the mailbox type is the set of events the widget accepts; a crashed
widget is restarted without taking down its siblings.

Two layers supply this, and the split matters:

- **The stdlib supplies the actor.** `Actor[Msg]` is an effect with
  `send` / `receive` / `self`, a private mailbox per fiber, and
  BEAM-style heap isolation — messages are copied, never aliased.
  `spawn_actor` / `with_mailbox` install it, `MailboxPolicy` sets
  bounds and backpressure (`Bounded(cap, BlockSender)` parks the
  sender). Supervision primitives — `Monitor`, `Link`, trap-exit —
  are stdlib too. None of this needs a dependency.
- **`ahu` supplies what sits on top**: `with_cell` wraps the
  receive-loop-over-state as `step: (State, Msg) -> StepResult[State]`,
  `ask` is request/reply, and `restartable_cell` adds restart policies
  (`Permanent` / `Transient` / `Temporary`, limits, backoff). These
  are the parts the stdlib deliberately leaves out.

So a widget is an actor; `ahu` is how we avoid hand-rolling its loop
and its restart policy.

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

## The concurrency model this design targets

Four properties of kaikai's runtime shape everything below. They are
current as of **kai 0.111.0**; re-read `kai info fibers` and
`kai info actors` before trusting this section, since the earlier
version of this document was written against 0.84.0 and drifted.

1. **Fibers park, they do not block.** The reactor (R1 file/sleep/
   process, R2 TCP, R3 stdin, R4 signal) parks a fiber on I/O without
   freezing the OS thread. Verified for the case terevaka needs — raw
   mode, byte at a time, on a real tty — by `tools/raw_park_probe`.
2. **Fibers run in PARALLEL by default**, across as many OS threads as
   the host has cores, with work stealing (`KAI_THREADS` to override;
   `=1` restores the cooperative single-thread scheduler). Two
   consequences the old design missed: messages crossing a thread
   boundary are physically copied, and the *order* independent fibers
   interleave is not stable — only causally ordered output is. A
   golden-frame test must therefore compare a frame assembled by one
   renderer, never the interleaving of panels that produced it.
3. **Cancellation is COOPERATIVE — there is no preemption.**
   `Spawn.cancel(f)` marks the target; the scheduler injects
   `Cancel.raise()` at its next yield point. A widget that computes in
   a tight loop with no yield point cannot be cancelled, and no resize
   or quit will reach it. Widget authors must yield, and the framework
   should say so rather than assume the scheduler will intervene.
4. **`receive()` is not selective.** Pattern-selective receive
   (`receive_match`) is deferred to a future edition, so a widget
   cannot pick a message out of its mailbox and leave the rest. Its
   `Msg` type must be a sum it can always handle in arrival order —
   which rules out the "wait here for the reply, ignore keys meanwhile"
   shape an actor might otherwise reach for.

## The concurrency gate (verified)

terevaka's thesis requires that a fiber sleeping on `Clock` or
blocked on `Input` does **not** block the scheduler — otherwise the
spinner-during-request freezes. The gate — two fibers progressing
concurrently, one sleeping, one working — passed originally on
`kai 0.84.0`:

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

Re-verified on **0.111.0**, where the gate is stronger than when it
was first written: the two fibers now run on different OS threads
rather than interleaving cooperatively, and the terminal read parks
in raw mode rather than only line-buffered. The property the design
depends on holds in both scheduler modes.

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

> **Reality check.** This list is the *original* v1 target, written
> before any code landed. What shipped as v0.1 is item 1 plus a
> non-fiber runtime: the `Ui` tree, the terminal layer, eight widgets
> as plain values, and a poll-loop `app.run`. Items 3–6 describe the
> v0.2 target, and item 2's diffing handler became a simpler
> frame-comparison repaint. Kept here as the direction, not as a
> claim about the current release — see the README for what exists.

1. **The `Ui` value tree** (Level 1) — `Text` / `Row` / `Col` /
   `Box` / `Focusable`, with style, and a pure `view`.
2. **The `Render` effect + a diffing terminal handler** — double-
   buffer, emit-only-changed-cells, plus a string-capture handler
   for tests.
3. **The `Input` effect + an `input_pump` fiber** over the reactor's
   R3 stdin parking (keys, eventually resize via R4 `SIGWINCH`).
4. **Live widgets as actors** (Level 2) — a `TextInput` and a `List`
   reference widget, each an `Actor[Msg]` whose loop is an
   `ahu.with_cell`.
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

## Foundational principle: build on the stdlib first, then ahu

Like the rest of the stack, terevaka reuses rather than reinvents —
but the layer to reuse has moved since this was written. The order is:

1. **kaikai's stdlib** for the actor itself: `Actor[Msg]`, mailboxes
   and policies, `Monitor` / `Link` / trap-exit for observing a
   child's death, nurseries for structured scope.
2. **`ahu`** for what the stdlib leaves out: `with_cell`'s
   state-and-step loop, `ask` for request/reply, and restart policies
   with limits and backoff.
3. **terevaka** for what neither has: the terminal, the `Ui` tree,
   layout, and the widget set.

Where a use case cannot be expressed through those primitives, the gap
is filed against the right layer — as terevaka has done twice already
(kaikai #1718, #1743) — not worked around inside terevaka.

## Open questions (to close before/with the first code lane)

1. ~~**Input encoding**~~ — *closed by v0.1.* `term.Key` is the sum
   type, assembled from bytes by `term.poll_key`; v0.1 also added the
   case this section did not anticipate, `Eof`, which the runtime must
   act on rather than pass to a handler. What remains open is only
   whether the fiber form keeps `poll_key` or moves to a parked
   `Stdin.read_bytes(1)` — the probe says either works.
2. **Render diffing granularity** — cell-level vs line-level diff, and
   how the `Dynamic(Pid)` holes (Level-1 leaves fed by Level-2 actors)
   are spliced into the frame without copying the whole tree across the
   fiber boundary. Now sharper than when written: with the multi-thread
   scheduler, a panel and the renderer may sit on different threads, so
   the copy is physical rather than an optimisable same-heap move.
3. **Focus model** — `Focus` as an effect (request/release/next) vs
   focus as state in a root actor. Leaning: a `Focus` effect handled by
   a focus-manager fiber. Note the constraint from the model above: a
   widget cannot selectively receive, so "hold this key until focus
   returns" has to be state in the widget, not a message left unread in
   the mailbox.
4. **`SIGWINCH` / resize** — via reactor R4 signal parking; how a
   resize event reaches every panel (broadcast to mailboxes).
5. **Cancellation discipline** *(new)* — cancellation is cooperative,
   so a widget that computes without yielding cannot be cancelled and
   will not see a resize or a quit. Open: whether the framework
   documents "yield in long work" as a widget-author rule, or the cell
   loop yields on the author's behalf between messages.
6. **Supervision shape** *(new)* — the design predates `Monitor`,
   `Link` and trap-exit being available. Open: whether a crashed panel
   is observed by a supervisor actor via `Monitor`, or wrapped in
   `ahu.restartable_cell`, and how the two compose. Note the trap-exit
   caveat in `kai info fibers`: under trap-exit the runtime bypasses
   the child's own `Cancel` handlers, so a panel's cleanup handler
   provably does not run — which decides where a panel may hold a
   resource it must release.

## Risks / dependencies (verified where possible)

- **BEAM copy cost in the view.** Messages copy across fiber heaps.
  A large `Ui` crossing a fiber boundary each frame copies in full.
  Mitigation: *one* fiber (the renderer) assembles the final tree;
  actors send small deltas / sub-trees, not whole frames. Gate: an
  N-panel dashboard must not scale copy cost with total frame size.
  This is a real perf dependency to measure, not a deferral — and the
  multi-thread scheduler raises the stakes, since a cross-thread send
  is always a physical copy while a same-thread send can transfer
  ownership. Measure at `KAI_THREADS=1` *and* at the default.
- **The reactor.** *Resolved and measured* — R1–R4 shipped
  (file/sleep/process, TCP, stdin, signal); the gate passed on 0.84.0
  and again on 0.111.0, where `tools/raw_park_probe` shows a fiber
  parking on a raw-mode tty read while a sibling keeps running. This
  was the #1 technical blocker and it is closed.
- **No preemption** *(new)*. A widget in a tight loop starves nothing
  else — fibers run on N threads — but it cannot itself be cancelled
  or resized until it yields. The risk is a third-party widget that
  computes without yielding: the app cannot defend against it, so this
  belongs in the widget-author contract.
- **No selective receive** *(new)*. Deferred upstream to a future
  edition. A widget's `Msg` type must be handleable in arrival order;
  any "wait for this specific reply" logic becomes explicit state.
  `ahu.ask` is the supported request/reply shape.
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
