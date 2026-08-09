# Python API & Heresy Editor Integration — Analysis and Roadmap

This document is a critical analysis of BiasedDoom's embedded Python API
and its integration with Heresy Editor: what hurts today, why, and what
to do about it. Each topic is structured as **Current state → Problem →
Proposal → Priority/Effort**. Items marked *(done)* shipped after this
analysis and are kept for context.

---

## 1. The silent-failure problem (event coverage)

**Current state.** `line_activated` fires only when the line's special
*succeeds* (`P_ActivateLine`: `if (buttonSuccess) WorldLineActivated(...)`).
A marker special like `ACS_Execute` with no backing script fails, and
nothing observable happens — no event, no log line, no console warning.
Discovering this rule required reading `p_spec.cpp` after a map appeared
to "do nothing".

**Problem.** The most common beginner failure — a trigger that does
nothing — produces zero diagnostic signal at the exact moment it happens.
The editor now auto-generates BEHAVIOR stub scripts to keep specials
succeeding, but maps from any other source can still hit this, and stubs
only cover `ACS_Execute`.

**Proposal.** *(done)* A `line_activation_failed` event carrying
`line_index`, `special`, `args`, `activation_type`, and `actor_ref`.
Future extensions in the same vein:

- **Reason codes.** Distinguish "script not found", "locked (wrong key)",
  "not enough mana", "special unknown". `P_ExecuteSpecial` only returns
  a bool today; a reason out-param would make editor diagnostics
  dramatically better. *Effort: medium. Impact: high.*
- **ZScript parity.** `WorldLineActivationFailed` as a static event
  handler virtual, so ZScript mods get the same signal. *Effort: low.*
- **Pickup/inventory events.** `item_picked_up`, `item_dropped`,
  `weapon_changed` — frequently requested for gameplay scripting and
  currently only pollable per-tick (wasteful at 35 Hz per actor).
  *Effort: medium. Impact: high for gameplay mods.*
- **Sector events.** `sector_entered`/`sector_exited` per player —
  the classic "trigger when the player enters a room" idiom currently
  needs a grid of linedefs or per-tick point-in-sector checks.
  *Effort: medium-high (blockmap hook). Impact: high.*

**Priority.** The failure event is shipped; reason codes are the next
best investment.

## 2. Snapshot vs live-handle duality

**Current state.** Two parallel object models: `bd.actors()` /
`bd.spawn_actor()` return plain **snapshot dicts** (stale the moment they
are made), while `bd.actor_ref()` / `bd.spawn()` return **live handles**
(`Actor`, `Line`, `Sector`, `Player`) with properties and methods. The
snapshots predate the handles; the handles were "API v2".

**Problem.** Two ways to do everything confuses exactly the users the
Python API is for. `bd.actors(class_name="ZombieMan")` looks like it
returns usable objects; it returns dicts that silently go stale. The
documentation has to teach both models and when each is safe, which is
cognitive overhead that shows up as bugs ("I stored the dict and health
never changes").

**Proposal.**

- **Make handles the documented default.** Every guide example should
  use handles; snapshot docs get a "legacy / serialization-only" note.
  *Effort: docs-only. Impact: medium-high.*
- **Snapshot ergonomics for save/load.** Snapshots remain genuinely
  useful for JSON persistence (`bd.state`). Document that as *the*
  snapshot use case, and add `Actor.snapshot()` parity notes (already
  exists) so the mapping is explicit.
- **Long-term (breaking, defer):** `bd.actors()` returning handles
  directly, snapshots available via `.snapshot()`. Defer until a major
  API bump; the dual model is tolerable if documented crisply.

**Priority.** Docs-first clarification now; no breaking change before
API v3.

## 3. Testing infrastructure

**Current state.** *(done)* `-scripttest <tics>` runs a level for N
tics, prints `SCRIPT TEST: PASS/FAIL` with the Python error count, and
exits 0/1/2. `-pyerrorlog <file>` emits JSON-lines errors. Drive scripts
inject input through `bd.execute("+forward")` or `Player.set_input`.

**Problem.** PASS/FAIL on "no Python errors" catches crashes but not
wrong behavior: the ambush that doesn't spawn, the door that opens the
wrong sector. And real-time pacing (35 tics/s) makes long tests slow.

**Proposal.**

- **Assertions in scripts.** A tiny convention: scripts call
  `bd.assert_true(cond, "message")` (new API), failures count as script
  test failures with file/line. Composes perfectly with `-scripttest`
  exit codes. *Effort: low. Impact: high.*
- **Fast-forward.** Run N tics per frame during `-scripttest` (the net
  loop already supports catch-up); a 10,000-tic soak test currently
  takes ~5 minutes of wall time for no reason. *Effort: medium
  (touch `TryRunTics` pacing carefully). Impact: medium.*
- **Golden screenshots.** `bd.execute("screenshot x")` already works
  (used heavily in developing this feature set); add an opt-in pixel
  comparison helper in the test harness rather than the engine.
  *Effort: low (external). Impact: medium.*
- **Headless rendering.** A null video driver for CI runners without X.
  All current "headless" runs still open a window. *Effort: high.
  Impact: medium (CI ergonomics).*

**Priority.** `bd.assert_true` next; it multiplies the value of
`-scripttest`.

## 4. Error ergonomics

**Current state.** Tracebacks print to console/stdout/logfile in red;
identical errors are deduplicated with periodic "repeated N times"
summaries; `print()` output is flushed before tracebacks so ordering is
natural. `-pyerrorlog` *(done)* gives tools a JSON feed; Heresy *(done)*
surfaces that feed in its log window via Script → Show Python Errors.

**Problem.** Console-first errors are fine in-game but weak in the
authoring loop: the mapper edits in VSCode, tests from Heresy, and the
error lives in a third place (the game console).

**Proposal.**

- **In-editor markers.** Heresy parses the `-pyerrorlog` JSON (it has
  file/line in the traceback) and jumps the external editor to the
  failing line, or annotates the status bar on sync. *Effort: medium.
  Impact: high.*
- **Structured error records.** Tracebacks as structured data
  (exception type, file, line, function) rather than formatted text —
  the JSON feed currently embeds preformatted text that tools must
  re-parse. *Effort: low. Impact: medium.*
- **Runtime warnings channel.** Non-fatal API misuse (stale handle
  access, blocked mutation attempts) currently raise or no-op; a
  rate-limited warning channel would teach correct usage without
  breaking scripts. *Effort: medium. Impact: medium.*

**Priority.** In-editor error markers close the loop; everything else
is polish on an already-decent pipeline.

## 5. Performance and the C-crossing budget

**Current state.** Every event dispatch, query, and mutation crosses
the C++/CPython boundary. Whole-tic budgets with per-callback profiling
exist (`bd.profile()`), and `apply_actor_batch` batches mutations into
one crossing.

**Problem.** Query patterns are per-actor crossings: a script scanning
500 actors for a custom condition costs 500 crossings per tick. The
batch API covers mutations only.

**Proposal.**

- **Query batching / filtering in C.** `bd.actor_refs(class_name=..., 
  sphere=(x, y, r))` — push the common filters down; combined with
  blockmap queries this eliminates most full-level scans.
  *Effort: low-medium. Impact: high for horde scripts.*
- **Vectorized reads.** `bd.actor_field_batch(refs, ["health", "x"])`
  returning arrays; complements `apply_actor_batch`. *Effort: medium.
  Impact: medium.*
- **Document the budget model.** `bd.profile()` data is the key to
  healthy scripts and is currently a power-user secret. A "performance"
  guide page with measured examples (crossing cost, per-tic budget
  behavior) would prevent most perf bugs. *Effort: docs. Impact: medium.*

**Priority.** Sphere/tag/class push-down filters first; they are cheap
and eliminate the worst pattern.

## 6. Determinism, savegames, and multiplayer

**Current state.** `bd.state` is JSON-persisted into savegames with
`save`/`load` events for (de)hydration. Mutation APIs are hard-blocked
in multiplayer and demo playback. Actor handles can go stale across
load; `.valid` exists for detection.

**Problem.** `random()` from Python's stdlib is outside the engine's
deterministic RNG, so save/load and demo sync drift is possible for
gameplay-affecting scripts. Stale-handle detection exists but nothing
helps rebind handles after a load.

**Proposal.**

- **Engine-seeded RNG helper.** `bd.random()`/`bd.randrange()` backed by
  the level's RNG (or a documented, state-saved seed), so scripts are
  deterministic by default. *Effort: low. Impact: medium.*
- **TID-based rebinding recipe.** Document the canonical pattern: keep
  TIDs in `bd.state`, re-resolve handles on `load`. Consider
  `bd.on_load_rebind(mapping)` sugar later. *Effort: docs now.*
- **Read-only observer mode for MP/demo.** Allow queries + center
  messages during multiplayer/demos instead of blocking everything;
  useful for spectator overlays and replay analysis. *Effort: medium
  (careful audit of mutation entry points). Impact: medium.*

**Priority.** The RNG helper is the only correctness item; do it early.

## 7. Security and trust model

**Current state.** Python manifests are trusted same-container code with
full CPython (file I/O, `os`, sockets) — the model is explicitly
"trusted mod code", documented in the security section of the Python
docs.

**Problem.** Full stdlib access means a malicious or careless script can
do anything the user can. There is no middle ground between "no Python"
and "full Python".

**Proposal.**

- **Document, don't restrict (for now).** The audience is mappers
  running their own scripts; over-restriction kills the feature. Keep
  the loud documentation and the `-python` opt-in.
- **Optional sandbox profile (future).** A `trusted=false` manifest
  mode stripping `os`/`socket`/etc. for downloaded maps, surfaced as a
  user prompt on first load. *Effort: high (CPython sandboxing is
  genuinely hard to get right). Impact: medium; revisit when third-party
  script distribution is common.*

**Priority.** Documentation is sufficient at current adoption.

## 8. Heresy Editor integration

**Current state.** Sidecar `<map>.scripts/` workspace with ACS/ZScript/
Python parity templates; auto-sync on save; acc compilation; BEHAVIOR
stub generation for trigger args; script-actor introspection into the
thing browser; Draw Trigger Line; test-map auto-args (`-file`, `-python`,
`-debug`); *(done)* typings auto-install and Python error surfacing.

**Problem / next steps.**

- **Project-aware script docs.** Multi-map projects share patterns but
  each map gets an isolated sidecar; a project-level `scripts/` shared
  folder option would help campaigns. *Effort: medium. Impact: medium.*
- **In-editor error markers** (see §4). *Effort: medium. Impact: high.*
- **One-click "run script test"** driving `-scripttest` and reporting
  PASS/FAIL in the editor, reusing H2's plumbing. *Effort: low (the
  pieces exist). Impact: high — turns CI mode into a button.*
- **Actor registry awareness.** The thing browser's script-actor group
  could expose "insert spawn call" snippets using `bd.actors.*`
  constants. *Effort: low. Impact: medium.*
- **Stub sync policy.** Heresy bundles `common/biaseddoom.pyi`; refresh
  it from BiasedDoom's `docs/scripting/biaseddoom.pyi` at release time
  (the file is `dumppystub`-regenerable in-engine).

## 9. Single-sourcing: stub, docs, website

**Current state.** *(done)* `biaseddoom.pyi` ships in
`docs/scripting/`, its actor-constant block is engine-generated via
`dumppystub`, and the website's API reference is generated from the same
file.

**Problem.** Three audiences (engine docs, editor completions, website)
historically meant three copies to rot.

**Proposal.** Keep the pipeline strict: engine docstrings → `dumppystub`
→ `.pyi` → (Pylance, website API pages). The rule: *prose lives in
docstrings, never in generated files.* When a function's docstring is
too thin for the website, enrich the C++ docstring, not the .pyi.

**Priority.** This is now the invariant; guard it in release checklists.

---

## Summary table

| Item | Effort | Impact | Status |
|------|--------|--------|--------|
| `line_activation_failed` event | low | high | done |
| `-scripttest` CI mode | medium | high | done |
| `-pyerrorlog` JSON feed | low | medium | done |
| `dumppystub` + single-sourcing | medium | medium | done |
| Heresy typings auto-install | low | medium | done |
| Heresy error surfacing | medium | high | done |
| Activation failure reason codes | medium | high | proposed |
| `bd.assert_true` + test conventions | low | high | proposed |
| Query push-down filters (sphere/class) | low-medium | high | proposed |
| Engine-seeded `bd.random()` | low | medium | proposed |
| In-editor error markers | medium | high | proposed |
| One-click script test in Heresy | low | high | proposed |
| Pickup/inventory events | medium | high | proposed |
| Sector enter/exit events | medium-high | high | proposed |
| Read-only MP/demo observer mode | medium | medium | proposed |
| Fast-forward for `-scripttest` | medium | medium | proposed |
| Headless video driver | high | medium | proposed |
| Sandbox profile for untrusted mods | high | medium | deferred |
