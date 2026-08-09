# Embedded Python Scripting

This is the complete authoring, API, security, persistence, interoperability,
packaging, debugging, and testing guide for BiasedDoom's third scripting
runtime.

Python does **not** replace ACS or ZScript. The engine initializes and runs all
three independently:

- ACS remains the map-special and compiled legacy scripting path.
- ZScript remains the engine-native object, actor, event-handler, UI, and
  gameplay extension path.
- Python is a trusted, embedded CPython path for synchronous real-time gameplay,
  orchestration, stateful callbacks, and integration with the other two systems.

The public Python API version documented here is `2`.

## Read The Security Rule First

> [!CAUTION]
> A Python mod is arbitrary native-equivalent code. It is **not sandboxed**.
> It can use modules such as `os`, access the user's files, start processes if
> the platform permits it, open network connections, or otherwise do anything
> the account running BiasedDoom can do. Load Python only from authors you
> trust as much as an executable program.

For that reason, finding a `PYTHON` manifest is not enough to execute it.
Python stays inactive unless the player explicitly opts in with `-python` or
has persisted `py_enabled=true`. `-nopython` always overrides the archived
setting.

The opt-in is process-wide. It approves every Python manifest in the loaded
resource set; it is not a per-PK3 permission prompt.

The isolated interpreter configuration disables `site`, user-site packages,
CPython signal handlers, command-line parsing by CPython, and `.pyc` writes.
Those settings make startup reproducible. They do **not** create a security
sandbox.

## Choose The Right Language

| Need | Usually choose | Why |
|------|----------------|-----|
| Existing map scripts and line specials | ACS | It is the established map scripting contract. |
| Defining new actor/state/weapon classes, event handlers, menus, or renderer UI | ZScript | It owns the engine's class model, state compiler, and UI scopes. |
| Real-time single-player game logic, orchestration, data processing, or bundled Python libraries | Python | API v2 exposes live native handles, mutation, attacks, specials, scheduling, and a typed ZScript method bridge. |
| Deterministic multiplayer gameplay | ACS or ZScript | Python gameplay mutation is deliberately blocked in multiplayer and demos. |
| Compatibility with unmodified GZDoom | ACS or ZScript | BiasedDoom's Python contract is engine-specific. |

A mod can use all three. A PK3 may contain `PYTHON`, `ZSCRIPT`, and compiled ACS
`BEHAVIOR`/`LOADACS` content at the same time.

## Ten-Minute First Mod

### 1. Create the directory tree

```text
my-python-mod/
├── PYTHON
└── pyscripts/
    └── main.py
```

`PYTHON` must be at the resource root. Its name has no extension.
Use a differently named directory such as `pyscripts/` for the source files:
Windows and default macOS filesystems treat `PYTHON` and `python` as the same
name, so placing a `python/` directory beside the manifest is not portable.

### 2. Write the manifest

Put this in `my-python-mod/PYTHON`:

```text
pyscripts/main.py
```

### 3. Write the script

Put this in `my-python-mod/pyscripts/main.py`:

```python
import biaseddoom as bd


def on_engine_start(event):
    bd.log(f"Python API {bd.API_VERSION} started through {bd.RUNTIME}")


@bd.on("map_load")
def entered_map(event):
    bd.state["visits"] = bd.state.get("visits", 0) + 1
    bd.log(
        f"Entered {event['map']}; "
        f"savegame={event['from_savegame']}; visits={bd.state['visits']}"
    )


def on_tick(event):
    if event["level_time"] % (10 * bd.TICRATE) == 0:
        bd.log(f"Ten-second heartbeat at tic {event['level_time']}")
```

The example uses both callback styles:

- A conventional function name such as `on_engine_start` or `on_tick` is
  discovered after the module executes.
- `@bd.on("map_load")` explicitly registers any callable for an event.

### 4. Package it

From inside `my-python-mod`:

```bash
cmake -E tar cf ../my-python-mod.pk3 --format=zip PYTHON python
```

A PK3 is a ZIP file. Ordinary ZIP tools work too; make sure `PYTHON` is at the
archive root rather than inside an extra `my-python-mod/` directory.

During development, an unpacked directory can also be passed to `-file` if the
normal resource loader accepts it in the current build.

### 5. Run it

```bash
./build/biaseddoom \
    -iwad /path/to/DOOM2.WAD \
    -file /path/to/my-python-mod.pk3 \
    -python \
    -stdout \
    +logfile python-mod.log
```

The important flag is `-python`. `-stdout` and `+logfile` are strongly
recommended while developing.

### 6. Verify it

Open the console and run:

```text
py_status
```

A successful status reports that Python is compiled, active, and lists the
loaded module and callback counts.

## The `PYTHON` Manifest In Detail

BiasedDoom discovers one root-level `PYTHON` manifest from each loaded resource
container. A container is one WAD, PK3, or loaded resource file—not the merged
global VFS as a whole.

Each nonblank line names one Python source file in that same container:

```text
# Comments and blank lines are allowed.
pyscripts/main.py
pyscripts/monsters.py       # Inline comments are allowed.
"pyscripts/save support.py"
'pyscripts/quoted.py'
```

Rules:

1. The manifest is UTF-8. A UTF-8 BOM on its first line is accepted.
2. Leading and trailing whitespace is removed.
3. Text after `#` is removed before quote handling.
4. Matching single or double quotes around the entire remaining path are
   removed.
5. The path must be relative and end in lowercase `.py`.
6. Forward slashes are required.
7. Absolute paths, backslashes, and any path containing `..` are rejected.
8. The file must exist in the same resource container as the manifest.
9. Entries run in manifest order. Avoid listing the same file twice.
10. A bad entry is diagnosed and skipped; it does not suppress valid entries.

The same-container rule prevents accidental PK3 override behavior from making
one mod's helper resolve to another mod's file. It is a VFS isolation rule, not
a security boundary: trusted Python can still use normal operating-system file
APIs.

### Multiple mods

If three PK3s each contain a valid manifest, all three are loaded after the
player opts in. Their entry modules are independent Python module objects, and
their callbacks are appended in resource/manifest discovery order.

Do not design correctness around order between unrelated mods. In particular:

- Give CVar names a mod-specific prefix.
- Namespace persistent state under a unique key.
- Do not mutate an event dictionary received by a callback.
- Do not assume another mod has already run unless it is a declared part of
  the same package and manifest.

## Runtime Lifecycle

BiasedDoom embeds one CPython interpreter in the process. It executes on the
main engine thread under the normal GIL. Callbacks are synchronous: the engine
waits for each callback to return.

At startup, the engine:

1. Loads resources.
2. Parses existing actor definitions and ZScript.
3. Initializes the play simulation, including existing ACS/ZScript machinery.
4. Discovers root `PYTHON` manifests.
5. Checks the trust opt-in.
6. Initializes isolated CPython and the built-in `biaseddoom` module.
7. Executes each manifest entry.
8. Registers conventional callback names.
9. Dispatches `engine_start`.

If one module has a top-level syntax or runtime error, its traceback is logged,
that module is skipped, and other modules continue loading.

At normal engine cleanup, `engine_shutdown` runs before the interpreter is
finalized. Cleanup is not a safe time to create actors, change maps, or assume
level data still exists.

## Registering Callbacks

### Conventional names

These module-level names are recognized:

| Python name | Event |
|-------------|-------|
| `on_engine_start` | `engine_start` |
| `on_map_load` | `map_load` |
| `on_map_unload` | `map_unload` |
| `on_pre_tick` | `pre_tick` |
| `on_tick` | `tick` |
| `on_post_tick` | `post_tick` |
| `on_actor_spawned` | `actor_spawned` |
| `on_actor_died` | `actor_died` |
| `on_actor_damaged` | `actor_damaged` |
| `on_actor_destroyed` | `actor_destroyed` |
| `on_actor_revived` | `actor_revived` |
| `on_line_activated` | `line_activated` |
| `on_line_activation_failed` | `line_activation_failed` |
| `on_player_entered` | `player_entered` |
| `on_player_spawned` | `player_spawned` |
| `on_player_respawned` | `player_respawned` |
| `on_player_died` | `player_died` |
| `on_player_disconnected` | `player_disconnected` |
| `on_save` | `save` |
| `on_load` | `load` |
| `on_engine_shutdown` | `engine_shutdown` |

Each must be callable and accept one event dictionary.

### Decorator registration

```python
import biaseddoom as bd


@bd.on("actor_died", class_name="ZombieMan", tid=7001, priority=50)
def score_python_kill(event):
    actor = event["actor"]
    bd.log(f"{actor['class_name']} with TID {actor['tid']} died")
```

Unknown event names raise `ValueError` while the module loads. Passing a
non-callable raises `TypeError`.

The decorator accepts `every` (dispatch every N matching events), `priority`
(higher runs first), and native `class_name`, `tid`, and `player` filters. The
filters are applied before Python is called, which avoids spending the frame
budget on irrelevant high-frequency events.

The same callable registered twice for the same event is deduplicated. Two
different functions are two callbacks even if they have the same name.

### Failure isolation

If a callback raises an exception:

1. The full Python traceback is written to the engine log.
2. That one callback is marked failed.
3. It is skipped on later dispatches.
4. Other callbacks and other mods continue.
5. `py_reload` clears the failed status by rebuilding the interpreter.

This prevents a failing 35 Hz callback from flooding the log every tic.

## Common Event Fields

Every event dictionary contains:

| Key | Type | Meaning |
|-----|------|---------|
| `name` | `str` | Canonical event name. |
| `map` | `str` | Current map lump name, or `""` when no map is active. |
| `level_time` | `int` | Current level time in 35 Hz tics, or `0` outside a map. |

Treat the dictionary and nested snapshots as read-only input. Mutating them
does not mutate the engine and could affect later callbacks that receive the
same event object.

## Event Reference

### `engine_start`

Extra fields: none.

Runs once after all manifest entry modules have executed. Use it to initialize
keys in `bd.state`, validate required CVars/classes, or write a startup log.

It runs before the first playable map. Actor mutation functions therefore
raise `RuntimeError` here because no active level exists.

### `map_load`

Extra field:

| Key | Type | Meaning |
|-----|------|---------|
| `from_savegame` | `bool` | `True` when the map was entered by restoring a save. |

Runs after existing static and local ZScript `WorldLoaded` handlers and before
deferred ACS scripts are processed for an ordinary map entry.

On a savegame restore, the Python `load` callback runs after actor restoration
and before this `map_load` callback. This means `map_load` can immediately see
the restored `bd.state` and actors.

### `map_unload`

Extra field:

| Key | Type | Meaning |
|-----|------|---------|
| `next_map` | `str \| None` | Destination map when known; otherwise `None`. |

On an ordinary level transition, ACS unloading scripts and ZScript unload
handlers run before Python's callback. A savegame load can produce an unload
with `next_map=None` before it rebuilds the saved map.

Actor mutation and `execute_acs` raise `RuntimeError` during this teardown
callback. Record state or queue non-gameplay work instead.

Do not rely on `map_unload` being an engine-shutdown notification; use
`engine_shutdown` for process cleanup.

### `pre_tick`, `tick`, and `post_tick`

Extra field:

| Key | Type | Meaning |
|-----|------|---------|
| `paused` | `bool` | Engine pause state at dispatch time. |
| `python_time_us` | `int` | `post_tick` only: Python time already consumed this tic. |

BiasedDoom normally runs at `bd.TICRATE == 35` tics per second. `pre_tick`
runs before native player thinking, so `Player.set_input()` can affect the
current tic. `tick` runs after player thinking and local ZScript `WorldTick`,
but before actor thinkers. `post_tick` runs after actors and world specials.
The three phases share one whole-tic Python budget.

Keep this callback small. See [Performance](#performance-and-determinism).

### `actor_spawned`

Extra field:

| Key | Type | Meaning |
|-----|------|---------|
| `actor` | `dict` | Snapshot of the actor after the normal ZScript spawn handlers. |
| `actor_ref` | `Actor` | Live, lightweight handle to the same actor. |

Spawn/event timing inside the engine can be reentrant. If a Python callback
spawns and immediately damages an actor, do not assume its spawn and death log
messages will have intuitive wall-clock ordering. Use TIDs and explicit state,
not log order, for correctness.

### `actor_died`

Extra fields:

| Key | Type | Meaning |
|-----|------|---------|
| `actor` | `dict` | Snapshot of the dying actor. |
| `inflictor` | `dict \| None` | Snapshot of the inflicting actor when available. |
| `actor_ref` | `Actor` | Live handle to the dying actor while it remains valid. |
| `inflictor_ref` | `Actor \| None` | Live inflictor handle when available. |

Python's `damage_actor` supplies no source or inflictor, so `inflictor` may be
`None` for a death caused by that API.

### Other real-time gameplay events

| Event | Extra fields |
|-------|--------------|
| `actor_damaged` | `actor_ref`, `inflictor_ref`, `source_ref`, `damage`, `damage_type`, `flags`, `angle` |
| `actor_destroyed` | `actor_ref`, final `actor` snapshot |
| `actor_revived` | `actor_ref` |
| `line_activated` | `line_index`, `actor_ref`, `activation_type` |
| `line_activation_failed` | `line_index`, `special`, `args` (5 ints), `actor_ref`, `activation_type` |
| `player_entered`, `player_spawned`, `player_respawned`, `player_died`, `player_disconnected` | `player_index`, `from_hub`, `actor_ref` |

`line_activated` only fires when the line's special **succeeds** — a marker
special like `ACS_Execute` with no backing script fails silently. Subscribe
to `line_activation_failed` to debug dead triggers: it fires for any line
with a nonzero special whose execution failed, and reports the special
number and arguments so you can see exactly what the map asked for.

`Actor` values can become invalid during or after destruction/unload. Check
`.valid` when retaining a handle and expect `ReferenceError` from operations
on stale handles.

### `save`

Extra fields: none.

Runs immediately before `bd.state` is JSON-encoded into the primary level's
save data. Use it to copy last-minute values into the persistent dictionary or
remove transient/non-JSON objects.

Actor mutation and `execute_acs` are blocked while this snapshot is being
prepared. Use `bd.state` for save bookkeeping here.

### `load`

Extra fields: none.

Runs only when a nonempty valid Python state dictionary was restored. Actors,
players, and other level objects have been deserialized before it runs.

For a normal savegame restore, the observed order is:

```text
map_unload (old world, when one exists)
load       (JSON state and actors restored)
map_load   (from_savegame=True)
tick
```

Old saves without `pythonstate` simply do not dispatch `load`.

### `engine_shutdown`

Extra fields: none.

Use this for final logging or Python-owned cleanup. Do not depend on a live
map, renderer, menu, or mutable playsim here.

## The `biaseddoom` Module

Import the engine module with:

```python
import biaseddoom as bd
```

### Editor completions (VSCode)

The `biaseddoom` module only exists inside the running engine, so editors
cannot resolve the import on their own. BiasedDoom ships a type stub at
`docs/scripting/biaseddoom.pyi` describing the full API: every function
signature, the `Actor`/`Line`/`Sector`/`Player` handle members, the
`bd.actors` registry, and all built-in actor class constants
(`bd.actors.DOOM_IMP` and friends).

To enable completions in VSCode, copy the stub into a `typings/` folder at
the root of the workspace you edit (Pylance's default stub path):

```
test.scripts/
    typings/
        biaseddoom.pyi
    scripts/
        main.py
```

Open the sidecar folder as the workspace, reload the window if prompted,
and `import biaseddoom as bd` gains full completions and inline
documentation. Mod-defined classes are not in the static stub; the
registry resolves them at runtime, and the stub's `__getattr__` keeps
them type-check clean.

The stub's actor-constant block is generated. With the game running,
`dumppystub <path>` refreshes that block in place from the live class
registry (or writes a full skeleton when the file does not exist) and
warns if any public API is missing from the stub.

### Constants and attributes

| Name | Type | Value/meaning |
|------|------|---------------|
| `bd.API_VERSION` | `int` | Public API revision; currently `2`. |
| `bd.TICRATE` | `int` | Engine tic rate; currently `35`. |
| `bd.RUNTIME` | `str` | Runtime label; currently `"CPython"`. |
| `bd.state` | `dict` | Shared JSON-persisted state dictionary. |
| `bd.on(name)` | decorator | Registers a callback. |

## Logging And Output

### `bd.log(message, level="info") -> None`

Converts `message` with `str()` and writes it to the engine console/log.

```python
bd.log("Loaded")
bd.log("That setting is suspicious", level="warning")
bd.log("Required resource missing", level="error")
bd.log({"structured": "values are stringified"}, level="debug")
```

`error` is red, and `warning`/`warn` is yellow. Other labels are printed as
`[Python:<label>]`.

### `print`, `sys.stdout`, and `sys.stderr`

The runtime replaces Python's stdout and stderr with line-buffered engine
writers:

```python
print("This appears as [Python] ...")
print("This is stderr", file=sys.stderr)
```

Partial lines are buffered until a newline, explicit flush, reload, or
shutdown. Prefer `bd.log` when severity matters.

### Automated testing (CI)

Two command-line options make scripts testable without a human:

- `-scripttest <tics>` — after the level loads, the engine runs it for the
  given number of tics, prints `SCRIPT TEST: PASS` or
  `SCRIPT TEST: FAIL (N Python error(s) in M tics)`, and exits with status
  0 (pass), 1 (errors), or 2 (no level loaded). Every reported Python
  error counts, including deduplicated repeats.
- `-pyerrorlog <file>` — appends each reported Python error as a JSON line
  (`time_ms`, `map`, `context`, `source`, `repeats_suppressed`,
  `heartbeat`, `traceback`) for editors and CI tooling.

```bash
biaseddoom -iwad doom2.wad -file mymap.wad mymap.scripts -python \
    -warp 1 -scripttest 700 -pyerrorlog /tmp/pyerrors.jsonl
```

Pair with a drive script that injects input through `bd.execute` (for
example `bd.execute("+forward")`) or `Player.set_input` to exercise
triggers unattended.

### Where output goes, and copying it out

Everything above lands in the **in-game console** (`~`), the OS terminal
stdout, the DAP log event stream, and the `+logfile` file when one is
active. To copy console text to the OS clipboard (Windows / Linux / macOS):

- `copyconsole` — copy the whole scrollback; `copyconsole 50` copies the
  last 50 lines.
- With the console open, **drag the mouse** over the scrollback to select
  text (the selection is highlighted), then **Ctrl+C** copies just the
  selection. **Ctrl+A** selects the entire scrollback (you see everything
  highlighted) and copies it in one step. **Ctrl+C** with no selection
  copies the input line instead. Click once, press **Escape**, or start
  typing to clear the selection.

### Errors and tracebacks

Uncaught exceptions in callbacks print a full red traceback to the console.
Pending `print()` output is flushed first, so it appears before the error.
Identical consecutive errors are printed once and then summarized
(`... repeated N times; duplicates suppressed`), so a failing `tick`
handler cannot flood the console at 35 tracebacks per second.

## Map And Time Queries

### `bd.current_map() -> str | None`

Returns the active map lump name, such as `"MAP01"`, or `None` outside a map.

### `bd.level_time() -> int`

Returns current level time in tics. It returns `0` when no level is active.

The callback event already contains these values. The functions are useful in
helpers called outside the immediate event function.

## Player Queries

### `bd.players() -> list[dict]`

Returns one snapshot per active player:

```python
[
    {
        "index": 0,
        "name": "Player",
        "in_game": True,
        "actor": { ... actor snapshot ... },
    }
]
```

`actor` can be `None` during lifecycle windows where the player has no pawn.

```python
for player in bd.players():
    pawn = player["actor"]
    if pawn is not None:
        bd.log(f"{player['name']} has {pawn['health']} health")
```

## Actor Snapshots

The legacy `actors`, `actor`, `spawn_actor`, and TID mutation functions return
plain dictionaries. They are snapshots, not live objects. API v2 also exposes
the `Actor` handle described below for real-time work.

| Key | Type | Meaning |
|-----|------|---------|
| `class_name` | `str` | Runtime actor class after replacement. |
| `tid` | `int` | Thing ID; `0` means it cannot be targeted by TID APIs. |
| `health` | `int` | Health at snapshot time. |
| `x`, `y`, `z` | `float` | World position in map units. |
| `angle` | `float` | Yaw in degrees. |
| `pitch` | `float` | Pitch in degrees. |
| `velocity_x`, `velocity_y`, `velocity_z` | `float` | Current velocity components. |
| `alive` | `bool` | Whether health was greater than zero. |
| `is_monster` | `bool` | Actor has the engine monster flag. |
| `is_player` | `bool` | Actor is a player pawn. |
| `player_index` | `int` | Player slot or `-1`. |

Never retain a snapshot and assume the engine actor is unchanged. Query again
by TID when you need current data.

## Actor Queries

### `bd.actors(class_name=None, tid=0, limit=1024) -> list[dict]`

Returns actor snapshots from the primary level.

```python
all_zombies = bd.actors(class_name="ZombieMan")
objective = bd.actors(tid=7001, limit=1)
boss_zombies = bd.actors(class_name="ZombieMan", tid=7001)
```

Details:

- `class_name` uses engine class lookup and accepts derived classes through
  the normal `IsKindOf` relationship.
- `tid=0` means no TID filter; it does not mean “find actors whose TID is 0.”
- `limit` must be from `1` through `100000`, otherwise `ValueError` is raised.
- An unknown class raises `ValueError`.
- Outside a level, the function returns an empty list.

### `bd.actor(tid) -> dict | None`

Returns the first actor with a nonzero TID or `None`.

```python
door_guard = bd.actor(500)
if door_guard is not None and door_guard["alive"]:
    bd.log("The guard still lives")
```

Maps often contain actors with TID `0`. Assign important targets a TID in the
map, ACS, ZScript, or Python spawn call.

## Actor Mutation

All actor mutation functions require an active primary level and a
single-player, non-demo session. Violations raise `RuntimeError`.

### `bd.spawn_actor(class_name, x, y, z, angle=0.0, tid=0, force=False) -> dict`

Spawns an actor with normal class replacement enabled.

```python
spawned = bd.spawn_actor(
    "ZombieMan",
    128.0,
    -64.0,
    0.0,
    angle=180.0,
    tid=9001,
)
```

Details:

- An unknown class raises `ValueError`.
- With `force=False`, an actor that does not fit is destroyed and
  `RuntimeError` is raised.
- `force=True` skips the fit rejection; use it carefully.
- With `tid=0`, the engine allocates an unused TID starting in the
  `10000..99999` search range.
- If no TID can be allocated, the actor is destroyed and `RuntimeError` is
  raised.
- The returned snapshot's `class_name` may reflect actor replacement.

## Actor Class Registry (`bd.actors`)

`bd.actors` doubles as a registry of every actor class the engine knows
about — including classes defined by loaded mods, ZScript, DECORATE, and
MAPINFO `doomednums`. Calling it still queries live actors (see above);
accessing attributes on it gives you named constants so you never have to
hardcode class strings:

```python
bd.spawn_actor(bd.actors.DOOM_IMP, SPOT_X, SPOT_Y, 0.0)
```

Constants are `UPPER_SNAKE` versions of the engine class names
(`DOOM_IMP` → `"DoomImp"`, `MBF_HELPER_DOG` → `"MBFHelperDog"`). An
unknown constant raises `AttributeError` with a hint. Use
`dir(bd.actors)` or `bd.actors.constants()` to list every constant, and
`bd.actors.names()` for the class-name strings.

Discovery helpers:

```python
bd.actors.names()              # all actor class names, sorted
bd.actors.constants()          # all CONST names, sorted
bd.actors.resolve("DOOM_IMP")   # "DoomImp" (accepts either form, None if unknown)
bd.actors.children_of("Weapon")  # ["BFG9000", "Chaingun", "Pistol", ...]
bd.actors.monsters()           # shootable, kill-counted actors
bd.actors.projectiles()        # missile actors
bd.actors.weapons()            # Weapon descendants
bd.actors.items()              # Inventory descendants
bd.actors.players()            # PlayerPawn descendants
```

Random selection and spawning:

```python
bd.actors.random()                    # any actor class
bd.actors.random("monsters")          # category: monsters, projectiles,
                                      # weapons, items, players
bd.actors.random("DOOM_IMP")          # among a class and its descendants
bd.actors.spawn_random(x, y, z)       # random monster at (x, y, z)
bd.actors.spawn_random(x, y, z, kind="items", angle=90.0)
```

`random()` raises `ValueError` for an unknown category or class.
`spawn_random()` forwards extra keyword arguments (`angle`, `tid`,
`force`) to `bd.spawn_actor`.

### `bd.damage_actor(tid, damage, damage_type="None") -> int`

Damages the first actor with the TID and returns the engine damage result.

```python
applied = bd.damage_actor(9001, 25, damage_type="Fire")
```

No Python actor is supplied as source or inflictor. A missing TID raises
`LookupError`.

### `bd.set_actor_velocity(tid, x, y, z) -> dict`

Sets velocity components and returns the new snapshot:

```python
after = bd.set_actor_velocity(9001, 4.0, 0.0, 6.0)
```

A missing TID raises `LookupError`.

### `bd.destroy_actor(tid) -> bool`

Destroys the first actor with the TID, clears its kill/item counters, and
returns `True`. Returns `False` when no actor exists.

Use damage when gameplay credit, death states, or death events matter. Direct
destruction is removal, not a normal kill.

## Live Real-Time API (API v2)

The snapshot/TID functions above remain for compatibility and serialization.
New real-time code should normally use native handles:

```python
pawn = bd.player().actor
monster = bd.spawn(
    "ZombieMan", pawn.x + 128, pawn.y, pawn.z, tid=9001, force=True
)
monster.target = pawn
monster.health = 75
monster.set_velocity(4, 0, 2)
```

`Actor`, `Player`, `Sector`, and `Line` are small C++-backed Python objects;
property reads and writes cross directly into the playsim instead of rebuilding
dictionaries. They may only be used on the engine callback thread. Mutating
operations have the same active-map and single-player/non-demo guard as the
legacy mutation API.

### Handle lookup and lifetime

| Function | Result |
|----------|--------|
| `bd.actor_ref(tid)` | First live `Actor` for a nonzero TID, or `None`. |
| `bd.actor_refs(class_name=None, tid=0, limit=4096)` | Filtered live actors; `limit` is `0..1000000`. |
| `bd.spawn(class_name, x, y, z, angle=0, tid=0, force=False)` | Newly spawned `Actor`. |
| `bd.player(index=consoleplayer)` / `bd.player_refs()` | One/all in-game `Player` handles. |
| `bd.sector(index)` / `bd.sectors(tag=None)` | Sector by array index or all/by tag. |
| `bd.line(index)` / `bd.lines(line_id=None)` | Line by array index or all/by line ID. |

Actor handles are GC-aware and safe to retain across tics. `.valid` becomes
false after native destruction or map unload; using stale actors raises
`ReferenceError`. Player handles become invalid when the player leaves.
Sector/line handles are generation-checked and raise `ReferenceError` after
their map unloads. Do not place handles in `bd.state`; save snapshots, TIDs,
player indices, tags, or line IDs instead.

### `Actor` properties and methods

Writable scalar properties are `tid`, `health`, `x`, `y`, `z`,
`velocity_x/y/z`, `angle`, `pitch`, `roll`, `radius`, `height`, `speed`,
`gravity`, `mass`, `alpha`, `scale_x/y`, `tics`, `score`, and `special`.
Writable tuple/reference properties are `position`, `velocity`, `angles`,
`args`, `target`, `master`, and `tracer`. Read-only properties include
`valid`, `class_name`, `alive`, `is_player`, `is_monster`, `water_level`,
`floor_z`, and `ceiling_z`.

The gameplay-aware methods are:

| Method | Purpose |
|--------|---------|
| `snapshot()` | Return a JSON-friendly current snapshot. |
| `set_position(x, y, z, check=True, fog=False)` | Move now, optionally collision-checking or using teleport fog. |
| `set_velocity(x, y, z, add=False)` / `thrust(angle, force, vertical=0, replace=False)` | Native movement control. |
| `damage(amount, damage_type="None", inflictor=None, source=None, flags=0)` / `heal(amount, maximum=0)` | Use damage/healing paths and return their result. |
| `destroy()` | Remove the actor and invalidate the handle. |
| `distance_to(other)` / `check_sight(other, flags=0)` | Native spatial queries. |
| `get_flag(name)` / `set_flag(name, enabled)` | Read or modify accessible actor flags. |
| `set_state(label, call_actions=True)` | Enter a named state. |
| `inventory_count`, `give_inventory`, `take_inventory`, `use_inventory`, `clear_inventory` | Native inventory operations. |
| `play_sound(...)` / `stop_sound(channel=...)` | Actor-attached audio control. |
| `activate(activator=None, deactivate=False)` | Call the actor activation path. |
| `call_zscript(method, *args)` | Invoke a supported public ZScript actor method synchronously. |

Direct scalar assignment is deliberately raw. Prefer `damage`, `heal`,
`set_position`, and inventory methods when engine side effects, event dispatch,
collision, or gameplay credit matter.

### `Player`, `Sector`, and `Line`

`Player` exposes `valid`, `index`, `name`, `actor`, input fields
(`buttons`, `input_pitch/yaw/roll`, `forward_move`, `side_move`, `up_move`),
`fov`, and frag/kill/item/secret counts. `set_input(...)` changes the current
native user command; use it from `pre_tick`. `set_weapon(class_name)` switches
to an owned weapon. `BT_ATTACK`, `BT_USE`, `BT_JUMP`, `BT_CROUCH`,
`BT_ALTATTACK`, `BT_RELOAD`, `BT_ZOOM`, and `BT_USER1..4` are exported button
bits.

`Sector` exposes `index`, `tags`, writable `light`, `gravity`, `special`,
`damage`, `damage_interval`, and `leakiness`, plus read-only center
`floor_height`/`ceiling_height`. `move_floor(height, speed=0, crush=-1)` and
`move_ceiling(...)` perform one native plane movement step; use an action
special when a persistent mover thinker is desired.

`Line` exposes `index`, `front_sector`, `back_sector`, and writable `args`,
`special`, `flags`, `activation`, `alpha`, and `health`.
`activate(activator=None, back_side=False, clear=False)` executes its special.

### Native world/gameplay operations

| Function | Purpose |
|----------|---------|
| `execute_special(special, arguments=None, activator=None, line=None, back_side=False)` | Run any numeric or named action special with up to five arguments. |
| `radius_damage(spot, damage, distance, source=None, damage_type="Explosion", hurt_source=True)` | Native radius attack. |
| `spawn_missile(source, target, class_name, position=None, owner=None, check=True)` | Native aimed missile spawn. |
| `line_attack(source, angle=None, distance=8192, pitch=None, damage=5, damage_type="None", puff_class="BulletPuff", flags=0)` | Native hitscan; returns `target`, `puff`, and applied `damage`. |
| `exit_level(position=0, secret=False, keep_facing=False)` | Normal/secret level exit. |
| `change_level(map_name, position=0, flags=0, next_skill=-1)` | Explicit map transition. |
| `center_message(message, bold=False)` | Immediate center-screen message. |
| `set_music(name, order=0, looping=True, force=False)` | Change music and return success. |

`CHANGELEVEL_KEEPFACING`, `CHANGELEVEL_RESETINVENTORY`,
`CHANGELEVEL_NOMONSTERS`, `CHANGELEVEL_NOINTERMISSION`, and
`CHANGELEVEL_RESETHEALTH` are exported flag constants.

For large homogeneous changes, `bd.apply_actor_batch(operations)` reduces
Python/C crossings. Supported tuples are `("velocity", actor, x, y, z)`,
`("add_velocity", ...)`, `("position", ...)`, `("health", actor, value)`,
`("damage", actor, amount)`, and `("destroy", actor)`. It returns the number
applied and stops at the first invalid operation; validate generated batches
before submitting them.

## CVars

### `bd.get_cvar(name) -> bool | int | float | str`

Returns the CVar in its native Python representation:

```python
gravity = bd.get_cvar("sv_gravity")
```

Integer/color CVars become `int`, float CVars become `float`, bool CVars become
`bool`, and other types become `str`. An unknown name raises `KeyError`.

### `bd.set_cvar(name, value) -> bool | int | float | str`

Converts the value to the target CVar type, applies it, and returns the applied
value:

```python
actual = bd.set_cvar("sv_gravity", 700.0)
```

An unknown name raises `KeyError`; bad numeric conversion or a value outside
the engine's 32-bit integer range raises the corresponding Python conversion
or `OverflowError`. Write-protected, system-only, ignored, and currently
cheat-locked CVars raise `PermissionError`. Writes are blocked in multiplayer
and demos.

For inter-mod communication, define a uniquely prefixed mod CVar with the
normal `CVARINFO` mechanism, then let ZScript and Python read it. Remember that
players can also change user CVars manually.

## Console Commands

### `bd.execute(command) -> None`

Queues an engine console command:

```python
bd.execute('echo "queued from Python"')
bd.execute("quit")
```

The command is not executed inside the Python call. It runs when the engine
drains its command queue, so do not read state immediately and assume the
command has taken effect.

Because console commands can mutate gameplay, `execute` is blocked in
multiplayer and demo sessions.

Never concatenate untrusted text into a console command. Python itself is
trusted, but mod data or user input may still need quoting and validation.

## Calling ACS

### `bd.execute_acs(script, arguments=None, always=False, want_result=False)`

Starts numeric or named ACS in the primary level with the console player's pawn
as activator when one exists.

Numeric script:

```python
started = bd.execute_acs(80, arguments=[10, 20])
```

Named script:

```python
started = bd.execute_acs("OpenArena", arguments=(1,), always=True)
```

Synchronous result request:

```python
result = bd.execute_acs("CalculateReward", arguments=[3], want_result=True)
```

Rules:

- `script` must be an `int` or `str`; other values raise `TypeError`.
- `arguments` is `None` or a sequence of at most four integers.
- More than four arguments raises `ValueError`.
- Non-integer arguments raise Python conversion errors.
- With `want_result=False`, the return is a `bool` indicating whether the
  script was started.
- With `want_result=True`, ACS is requested with result semantics and an `int`
  is returned.
- `always=True` adds ACS's `ACS_ALWAYS` behavior.
- The call is blocked outside a level, in multiplayer, and in demos.

This API does not compile ACS. Package normal compiled ACS lumps as before.

## VFS Text And Helper Modules

### `bd.read_text(path) -> str`

Reads any UTF-8 resource from the current callback/module's own container:

```python
settings = bd.read_text("pyscripts/data/defaults.json")
```

The path must be relative, use forward slashes, and contain no `..`. Missing or
invalid paths raise `FileNotFoundError`. Invalid UTF-8 raises
`UnicodeDecodeError`.

It can only be called while a manifest module or one of that module's callbacks
is executing. Calling it without current-mod context raises `RuntimeError`.

### `bd.import_script(path, module_name=None) -> module`

Executes another `.py` resource from the same container and returns its module:

```python
helper = bd.import_script(
    "pyscripts/lib/rewards.py",
    module_name="my_mod_rewards",
)

reward = helper.reward_for_skill(3)
```

Important differences from normal `import`:

- The path must end in lowercase `.py`.
- Resolution is inside the current mod container.
- The helper is executed on each call.
- It is returned directly and is not promised as a normal `sys.modules`
  package import.
- Store the returned module instead of calling `import_script` every tic.
- Conventional `on_*` names inside a helper are not auto-registered. The
  helper can explicitly use `@bd.on(...)` if it intentionally owns callbacks.

Normal imports such as `import json`, `import collections`, and `import os` use the
bundled CPython standard library. PK3 source directories are deliberately not
added to `sys.path`; use `import_script` for packaged helpers.

## Persistent State And Savegames

`bd.state` is one shared dictionary created before mod modules execute:

```python
bd.state.setdefault("com.example.my_mod", {})
mine = bd.state["com.example.my_mod"]
mine["bosses_defeated"] = mine.get("bosses_defeated", 0) + 1
```

Always namespace your data. A reverse-domain name, repository slug, or another
globally distinctive key prevents collisions.

Mutate this dictionary in place. Do not assign a new object to `bd.state` or
delete the attribute; save/load then raises `TypeError` rather than persisting
ambiguous state. Use `bd.state.clear()` when a deliberate full reset is needed.

### JSON-compatible values

The entire dictionary is serialized with Python's `json` module. Store only:

- dictionaries with string keys;
- lists;
- strings;
- integers and finite floats;
- booleans;
- `None`.

Do not store modules, functions, sets, bytes, actor snapshots that you expect
to stay live, open files, custom class instances, or other non-JSON objects.

Tuples encode as JSON arrays and return as lists. Avoid `NaN` and infinities
for portability even though a particular Python JSON implementation may emit
them.

### Save sequence

When saving the primary level:

1. BiasedDoom dispatches `save`.
2. It runs `json.dumps(bd.state, sort_keys=True, ensure_ascii=False)`.
3. The resulting UTF-8 JSON string is written as `pythonstate` in level data.
4. Existing ACS module/deferred/global serialization and ZScript thinker/event
   serialization continue through their normal paths.

If encoding fails, the traceback is logged. Fix the bad state type; do not
assume a save contains Python state merely because the rest of the game saved.

### Load sequence

When reading valid nonempty `pythonstate`:

1. JSON is parsed.
2. The result must be a dictionary.
3. The existing `bd.state` object is cleared and updated in place. References
   to the dictionary itself remain valid.
4. Actors and players finish restoration.
5. `load` runs.
6. The later `map_load` event has `from_savegame=True`.

Use stable identifiers—TIDs, class names, and your own IDs—in persistent data.
Never attempt to serialize a pointer or treat an old snapshot as a live actor.

### Reload behavior

The console command:

```text
py_reload
```

does the following:

1. JSON-encodes the current `bd.state`.
2. Dispatches `engine_shutdown` and finalizes CPython.
3. Rediscovers manifests and starts a fresh interpreter.
4. Executes modules and dispatches `engine_start` with a fresh dictionary.
5. Restores the encoded state.
6. Dispatches `map_load` if a level was active.

Therefore `engine_start` during reload does not see the old state yet. Put
default initialization in `setdefault` calls so restoration can safely replace
the fresh values afterward.

If reload fails after shutdown, the Python runtime remains inactive; inspect
the traceback, correct the source, and run `py_reload` again.

## ACS And ZScript Coexistence

No ACS or ZScript loader was removed or redirected. Python integration is made
at lifecycle points after the established handlers.

### One PK3 using all three

```text
hybrid-mod.pk3
├── PYTHON
├── ZSCRIPT
├── LOADACS               # when the mod uses library ACS
├── acs/
│   └── mylibrary.o
└── pyscripts/
    └── main.py
```

Or a map WAD can retain its `BEHAVIOR` lump while a containing/companion PK3
adds Python.

### Python to ACS

Use `bd.execute_acs` for a direct script start. The ACS script keeps its normal
number/name, activation behavior, map variables, and save serialization.

### Python to ZScript

API version 2 lets a live `Actor` call supported methods on its runtime ZScript
class. This keeps class-specific behavior in ZScript while Python orchestrates
it without a console command, polling CVar, or one-tic queue:

```c
class PythonDrivenImp : DoomImp
{
    int BoostFromPython(int healthGain, Vector3 impulse, Actor newTarget)
    {
        health += healthGain;
        Vel += impulse;
        target = newTarget;
        return health;
    }
}
```

```python
imp = bd.spawn("PythonDrivenImp", x, y, z, force=True)
new_health = imp.call_zscript("BoostFromPython", 10, (2, 0, 1), pawn)
```

The bridge accepts integer-compatible values, floats, strings, 2/3/4-component
floating vectors, `Actor` subclasses, and `None` for nullable actor arguments.
It supports zero or one return value of the same categories and dispatches
virtual overrides. Arguments are checked against the reflected prototype
before entering the VM.

For safety and ABI clarity it rejects private/protected/internal, static,
action, abstract, UI-scope, unsafe, vararg, `out`/`ref`, multi-return, and
unsupported pointer/container signatures. A rejected signature raises
`PermissionError` or `TypeError`; a VM abort becomes `RuntimeError`. It does
not expose arbitrary `DObject` references, static functions, state actions, or
raw reflection data.

CVars, ACS, TIDs, and independent events remain useful looser boundaries when
the two scripts should not share a direct actor-class contract.

### Callback ordering that matters

Current integration intentionally preserves established behavior:

- ZScript actor-spawn/death handlers run before the matching Python event.
- Local ZScript `WorldTick` runs before Python `tick`.
- Normal ZScript world-load handlers run before Python `map_load`.
- Transition ACS unloading and ZScript unload handlers run before Python
  `map_unload`.
- Python state is added alongside, not instead of, existing ACS/ZScript save
  data.

Ordering is useful for observation, but avoid tightly coupling unrelated mods
to it.

## Performance And Determinism

Python runs synchronously on the engine thread. A slow callback delays the
game, rendering, input processing, and every other script runtime.

When Python is not opted in, tic/gameplay hooks take only the inactive native
fast path: CPython is not initialized and no event dictionaries or handles are
allocated. With Python active, a cached per-event presence bit keeps
unsubscribed hooks allocation-free. For subscribed hot paths, live handles,
native decorator filters, and `apply_actor_batch` avoid snapshot construction
and excessive Python/C crossings.

Every `biaseddoom` function must be called on that scripting thread. Do not
call the API from `threading.Thread`, executor workers, or callbacks owned by a
third-party background thread: the function raises `RuntimeError`. Background
work must hand plain data back for a later engine callback to consume, and the
mod remains responsible for making that handoff safe. Direct `bd.state`
access is still ordinary Python dictionary access, but keeping all mod state
on the callback thread is strongly recommended.

`py_tick_budget_ms` defaults to `3`. Scheduled tasks plus `pre_tick`, `tick`,
and `post_tick` share that whole-tic wall-clock budget. With
`py_tick_hard_budget=true`, once the budget is consumed the dispatcher skips
remaining Python callables until the next tic. This limits cumulative Python
work without adding tracing overhead to every Python line.

An individual callable cannot be interrupted safely while it is executing.
It may exceed the limit once; the runtime records and warns about that
overrun, then prevents later work in the same tic. By default,
`py_tick_overrun_limit=3` disables a callback (or cancels a repeating task)
after three consecutive individual overruns. `0` disables repeat-offender
removal. `py_tick_budget_ms=0` disables budget enforcement and warnings.
`py_reload` re-enables budget-disabled callbacks.

`bd.profile()` returns per-callback/task call counts, total/max microseconds,
budget skips/overruns, failure/disable state, and current budget settings.
`bd.reset_profile()` clears measurements but does not re-enable callbacks.

### Synchronous task scheduling

`bd.schedule(callback, delay=1, repeat=0, map_local=True)` returns a task ID.
Tasks run at the start of a future `pre_tick` under the same engine-thread and
budget rules. `repeat=0` is one-shot; a repeating callable may also return
`False` to cancel itself. Map-local tasks are cancelled on unload.
`bd.cancel_task(id)` returns whether it cancelled an active task, and
`bd.task_count()` returns the active count. `py_max_tasks` bounds the queue.

Recommended tick patterns:

```python
def on_tick(event):
    # Once per second rather than every tic.
    if event["level_time"] % bd.TICRATE != 0:
        return
    update_objectives()
```

Guidelines:

- Do not scan `bd.actors(limit=100000)` every tic.
- Filter by class or TID and cache only stable IDs.
- Move rare work to map/spawn/death callbacks.
- Break long work across tics with explicit state.
- Load/parse static VFS data once at module load or `engine_start`.
- Do not perform blocking network, subprocess, or disk operations in callbacks.
- Profile release builds as well as debug builds.

### Multiplayer and demos

API version 2 does not define a deterministic Python networking protocol.
All live-handle gameplay mutation and these legacy functions reject calls
while `netgame`, `multiplayer`, demo playback, or demo recording is active:

- `spawn_actor`
- `damage_actor`
- `set_actor_velocity`
- `destroy_actor`
- `set_cvar`
- `execute`
- `execute_acs`

Queries, logging, and VFS reads can still run. Do not use Python to implement
multiplayer-authoritative gameplay in this API version.

## Console And Configuration Reference

### Command-line switches

| Switch | Meaning |
|--------|---------|
| `-python` | Opt into all discovered trusted Python mods for this process. |
| `-nopython` | Force Python off even when `py_enabled` is archived true. |

### CVars

| CVar | Default | Meaning |
|------|---------|---------|
| `py_enabled` | `false` | Archived, user-owned global trust opt-in. Prefer `-python` while testing individual mods. |
| `py_tick_budget_ms` | `3` | Whole-tic Python budget in milliseconds; `0` disables enforcement/warnings. |
| `py_tick_hard_budget` | `true` | Skip later tasks/callbacks after the current tic consumes its budget. |
| `py_tick_overrun_limit` | `3` | Consecutive individual overruns before disabling/cancelling; `0` disables this containment. |
| `py_max_tasks` | `4096` | Scheduled-task ceiling, clamped internally to `1..100000`. |

Be conservative with `py_enabled=true`: any future command line that loads a
Python-bearing PK3 will then execute it unless `-nopython` is supplied.

### Console commands

| Command | Meaning |
|---------|---------|
| `py_status` | Show compiled/active/requested state, manifests, modules, and callback count. |
| `py_reload` | Rebuild the interpreter and scripts while preserving JSON-compatible state. |

`py_reload` is an unsafe console command under the engine's normal command
security classification.

## Building Python Support

Python support is enabled by default when CPython development files version
3.10 or newer are found.

| CMake option | Default | Meaning |
|--------------|---------|---------|
| `BIASEDDOOM_ENABLE_PYTHON` | `ON` | Attempt to build the embedded runtime. |
| `BIASEDDOOM_REQUIRE_PYTHON` | `OFF` | Fail configuration instead of compiling stubs when CPython is unavailable. |

Recommended verification configure:

```bash
cmake -S . -B build \
    -DBIASEDDOOM_ENABLE_PYTHON=ON \
    -DBIASEDDOOM_REQUIRE_PYTHON=ON
cmake --build build --target zdoom --parallel
```

Look for:

```text
-- Embedded Python scripting enabled with CPython 3.x.y
```

### Linux

Install the development package. Debian/Ubuntu example:

```bash
sudo apt install python3-dev
```

Fedora:

```bash
sudo dnf install python3-devel
```

Arch:

```bash
sudo pacman -S python
```

The build stages a private standard library under
`python/lib/python<major>.<minor>/` and the matching `libpython` SONAME beside
`biaseddoom`. CPython's test suite, bytecode caches, and build configuration
directory are excluded from packages because they are not runtime libraries.
The redistributed CPython terms are retained at `python/LICENSE.txt`.

Keep the executable, `libpython*.so*`, and `python/` directory together in a
portable package.

### Native Windows (MSVC)

The native vcpkg build automatically enables the `vcpkg-python` feature for
the static `x64-windows-static` triplet. The standard library is staged as:

```text
biaseddoom.exe
python/
├── LICENSE.txt
└── Lib/
    └── encodings/
        └── __init__.py
```

The normal helper builds and validates it:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1 `
    -Configuration Release -Package
```

Use `-NoPython` only when intentionally producing a stub build. The package
validator requires both `python\Lib\encodings\__init__.py` and
`python\LICENSE.txt` otherwise.

### Windows MinGW

The vcpkg CPython port used by this project does not support MinGW. The
cross-MinGW helper explicitly builds the stub path:

```bash
./tools/build-windows-mingw.sh --package
```

That package supports ACS and ZScript normally but cannot run Python mods. Use
the native MSVC Windows package when Python is required.

### macOS

The manifest automatically enables the pinned vcpkg CPython port on macOS.
The build requests static linkage so the application does not retain a
dependency on the build machine's Homebrew prefix. The private stdlib is staged
beside the executable inside the app bundle. Bootstrap/use the repository's
vcpkg toolchain as shown in the normal build instructions.

### Stub behavior

With `BIASEDDOOM_ENABLE_PYTHON=OFF`, missing development files, or MinGW, the
engine compiles lightweight stubs. ACS and ZScript remain enabled. If the user
requests Python, the engine logs that the executable lacks CPython support.

Use `BIASEDDOOM_REQUIRE_PYTHON=ON` in CI/release configurations so an accidental
stub build fails at configure time.

## Packaged Examples

The [example suite](../../examples/python/) contains twelve focused mods. Each
has its own root `PYTHON` manifest, source, and README, and can be packaged and
loaded independently. Together they cover lifecycle events, live handles,
combat and inventory, player input, sectors and lines, native event filters,
scheduling, save state, VFS helpers, typed ZScript calls, batched updates,
profiling, UI/audio, and level flow.

Build the complete suite or a selected subset:

```bash
./tools/build-python-examples.sh
./tools/build-python-examples.sh 02_live_actor_handles 10_zscript_bridge
```

Packages are written to `build/python-examples/`. Validate every source and
archive with `./tools/test-python-examples.sh`; pass `--iwad PATH` to also load
MAP01 with each package, exercise its initial callbacks, and require a clean
Python-driven exit through a Python-enabled BiasedDoom executable.

### Full integration fixture

The repository contains a complete hybrid example at:

```text
examples/python/hello_world/
├── PYTHON
├── ZSCRIPT
└── pyscripts/
    ├── autotest_failure.py
    ├── helper.py
    └── main.py
```

Its ZScript marker and `PythonBridgeProbe` prove that the engine parses both
languages and that Python can invoke a public class method through the typed
bridge. The Python source demonstrates:

- same-container helper import;
- conventional and decorator callbacks;
- stdout redirection and logging;
- legacy snapshots plus live actor/player/sector/line handles;
- CVar reads;
- direct fields, relationships, inventory, batch mutation, action specials,
  actor spawn/velocity/damage/destruction events, and ZScript invocation;
- pre/tick/post phases, profiling, hard budget skipping, and overrun disable;
- shared state;
- save and load callbacks;
- clean command-queue shutdown in test mode.
- worker-thread API rejection and failed-import callback rollback in test mode.

Build it with:

```bash
./tools/build-python-example.sh
```

Output:

```text
build/python-hello-world.pk3
```

Run it interactively:

```bash
./build/biaseddoom \
    -iwad /path/to/DOOM2.WAD \
    -file ./build/python-hello-world.pk3 \
    -python -stdout \
    +logfile python-example.log
```

The `BIASEDDOOM_PYTHON_AUTOTEST` variables used in the source are test-harness
controls. Ordinary interactive launches do not set them and remain playable.

## Automated Integration Test

After building `build/biaseddoom`, run:

```bash
./tools/test-python-scripting.sh --iwad /path/to/DOOM2.WAD
```

Or:

```bash
BIASEDDOOM_TEST_IWAD=/path/to/DOOM2.WAD \
    ./tools/test-python-scripting.sh
```

Options:

```text
--exe PATH       test a different executable
--timeout SEC    per-run timeout, default 45
--keep-temp      retain logs/config/save/stdout for inspection
```

Prerequisites:

- a built Python-enabled BiasedDoom;
- a compatible IWAD;
- GNU `timeout`;
- a graphical display, or `xvfb-run` on a headless machine;
- optional host `python3` for the preflight syntax check.

The script performs two actual engine processes.

### Active run coverage

It:

1. Packages the example.
2. Optionally compiles its source with host `py_compile`, redirecting bytecode
   outside the repository.
3. Starts the first map with `-python`.
4. Verifies embedded startup and same-PK3 VFS import.
5. Verifies lifecycle callbacks and pre/tick/post phases.
6. Queries snapshots and native handles, then mutates actors/world data.
7. Verifies spawn, damage, death, destruction, filtering, and invalidation.
8. Calls typed ZScript methods and verifies private-method rejection.
9. Forces one budget overrun and verifies same-tic skipping plus containment.
10. Queues a real engine save.
11. Waits for the save file to exist before queueing a load.
12. Verifies `save`, JSON state restoration, `load`, and
    `map_load(from_savegame=True)`.
13. Queues a clean engine shutdown from Python.
14. Verifies `engine_shutdown` and the created save file.

### Inactive run coverage

It starts the same PK3 without `-python`, verifies the explicit trust warning,
and verifies that `PYTEST engine_start` never appears. This catches accidental
removal of the security gate.

The final success text is:

```text
PASS: Python startup, VFS import, native real-time handles/mutations, callbacks,
      JSON save/load, typed ZScript bridge, shutdown, and trust opt-in all passed.
```

## Manual Test Matrix

Use this matrix for changes that touch lifecycle, serialization, packaging, or
public APIs.

| Test | Procedure | Expected result |
|------|-----------|-----------------|
| Compile-on | Configure with enable+require ON | Configure reports CPython and build links. |
| Compile-off | Configure with enable OFF | Build succeeds with stubs; ACS/ZScript still parse. |
| Trust default | Load example without `-python` | Warning appears; no Python marker executes. |
| Trust opt-in | Add `-python` | Module and callbacks load. |
| Trust override | Archive `py_enabled=true`, launch `-nopython` | No Python executes. |
| Status | Run `py_status` | Compiled/active/manifests/modules/callbacks are accurate. |
| Startup error | Add a syntax error to one entry | Traceback identifies resource/path; other entries continue. |
| Callback error | Raise in `on_tick` | One traceback and disable message; engine and other callbacks continue. |
| Reload | Fix source and run `py_reload` | Failed callback returns; JSON-compatible state survives. |
| VFS isolation | Put same helper path in two PK3s | Each entry reads/imports its own container's helper. |
| VFS traversal | Call `read_text("../secret")` | `FileNotFoundError`; no VFS traversal. |
| Map entry | Start first map | `map_load` has correct map and `from_savegame=False`. |
| Level transition | Exit to next map | `map_unload` then next `map_load`. |
| Actor query | Query/filter known actors | Snapshots contain all documented fields. |
| Spawn fit | Spawn into a blocked point without force | Runtime error and no surviving actor. |
| TID mutation | Spawn with a unique TID, mutate, query | Returned/query snapshots reflect changes. |
| Save/load | Change namespaced state, save, change it, load | Saved JSON state returns before `map_load(True)`. |
| Bad state | Put a set/function in `bd.state`, save | Serialization traceback clearly identifies failure. |
| Old save | Load save with no Python state | Game loads; no Python `load` callback. |
| Multiplayer guard | Try mutation in a network game | `RuntimeError`; synchronization is not changed. |
| Demo guard | Try mutation while recording/playback | `RuntimeError`. |
| Tick budget | Exceed a 1 ms hard budget before a lower-priority callback | Later work is skipped; repeat offender is disabled at its configured limit. |
| ACS bridge | Call packaged numeric and named ACS | Script starts with up to four arguments. |
| ZScript hybrid | Call a probe actor's typed public method and then a private method | Public mutation/return succeeds; private call raises `PermissionError`. |
| Legacy-only mod | Load existing ACS/ZScript mod with no manifest | Behavior is unchanged; CPython need not initialize. |
| Windows package | Run helper with `-Package` | `python/Lib/encodings` is present. |
| Linux package | Inspect executable directory/AppDir | stdlib and matching `libpython` SONAME are present. |
| MinGW package | Run `py_status` | Reports not compiled; ACS/ZScript remain usable. |

## Debugging Workflow

Use this command line while developing:

```bash
./build/biaseddoom \
    -iwad /path/to/DOOM2.WAD \
    -file /path/to/mod.pk3 \
    -python -stdout \
    +developer 1 \
    +logfile /tmp/biaseddoom-python.log
```

Then:

1. Run `py_status` after startup.
2. Search the log for `Python`, the PK3 filename, and the VFS source path.
3. Fix the first traceback, since later failures may be consequences.
4. Run `py_reload` for source-only iteration.
5. Fully restart when testing manifests, resource load order, startup flags,
   native packaging, or save compatibility.

Traceback filenames use the packaged path, and helper module `__file__` values
use a `vfs://` label where applicable.

## Troubleshooting

### “Python scripting was requested, but this executable was built without CPython support”

Reconfigure with Python 3.10+ development files and:

```bash
-DBIASEDDOOM_ENABLE_PYTHON=ON -DBIASEDDOOM_REQUIRE_PYTHON=ON
```

Use the native MSVC package rather than MinGW on Windows.

### “Python script was found but not executed”

This is the trust gate working. Add `-python` only after reviewing/trusting all
loaded Python mods.

### Fatal error mentioning `encodings`

The interpreter library and private stdlib do not match or the `python/`
directory was omitted from the package. Restore the directory produced by the
same build as the executable. Do not mix Python trees from different releases.

### Manifest is ignored

Check that:

- its archive path is exactly `PYTHON` at root;
- it is not `PYTHON.txt`;
- every script uses a same-container relative path;
- `.py` is lowercase;
- the ZIP has no extra enclosing directory;
- `py_status` sees valid entries.

### `ModuleNotFoundError` for another PK3 file

PK3 paths are not normal Python packages. Replace:

```python
import helper
```

with:

```python
helper = bd.import_script("pyscripts/helper.py", module_name="my_helper")
```

### `read_text`/`import_script` says there is no current mod

Call it at manifest module top level or from a registered callback. A detached
function invoked after the callback context ends has no implicit container.

### Actor lookup returns `None`

The actor may have been destroyed, replaced, never assigned that TID, or may
have TID `0`. Re-query from the primary level and assign stable nonzero TIDs to
objects Python must mutate.

### Mutation raises a synchronization error

You are in multiplayer, demo playback, or demo recording. This is an API
contract, not a CVar to bypass. Move deterministic gameplay to ACS/ZScript.

### Mutation says no level is active

Do it from `map_load`, `tick`, or a later level callback—not
`engine_start`/`engine_shutdown`.

### Callback stopped firing

Find the earlier traceback or budget “disabled until py_reload” line. Use
`bd.profile()`/`py_status` to distinguish an exception from consecutive
overruns, fix or split the work, and run `py_reload`.

### Save state is missing after load

Look for a JSON serialization traceback during `save`. Confirm your values are
JSON-compatible and that the save was created after Python became active.

### `py_reload` resets something unexpectedly

Only `bd.state` is preserved. Ordinary module globals are rebuilt. Also,
`engine_start` runs before the old state is restored during reload; use
`setdefault` and perform restored-state work in `map_load`.

### Game stutters every tic

Reduce actor scans, lower callback frequency, move work to event callbacks,
and inspect `bd.profile()`, budget warnings, skips, and disabled callbacks. The
GIL and main-thread execution are intentional in API version 2.

## Versioning And Forward Compatibility

Check the API before depending on future additions:

```python
import biaseddoom as bd

if bd.API_VERSION < 2:
    raise RuntimeError("This mod requires BiasedDoom Python API 2")
```

API version 2 guarantees the names and core semantics documented in this file.
New keys or functions may be added compatibly. Mods should:

- read event dictionaries by named keys;
- tolerate extra keys;
- avoid depending on callback order between unrelated mods;
- avoid importing internal engine modules;
- use `bd.RUNTIME` for diagnostics rather than assuming a particular patch
  version from `sys.version`;
- ship a clear security note telling players why `-python` is needed.

Callbacks registered while another callback is running become eligible on the
next event dispatch. They are never inserted into the event currently being
iterated.

## Current Intentional Limits

- Python is trusted, not sandboxed.
- The opt-in is process-wide, not per mod.
- Gameplay mutation is single-player/non-demo only.
- Live handles cover playsim actors, players, sectors, and lines, not arbitrary
  engine `DObject`, renderer, menu, or VM reflection objects.
- The ZScript bridge is deliberately typed and actor-method-only; actions,
  statics, UI/unsafe/private/ref/out/container/multi-return signatures remain
  unavailable.
- PK3 modules use `import_script`, not automatic `sys.path` mounting.
- The bundled standard library is authoritative, but modules that depend on
  optional native CPython extensions can vary by platform/build; `pip`, user
  site packages, and arbitrary host installations are not exposed.
- State persistence is JSON only.
- Python callbacks are synchronous and single-threaded with engine execution.
- A running Python callable cannot be forcibly preempted; hard budgets skip
  later work and disable consecutive offenders.
- Engine API calls from Python-created background threads are rejected.
- MinGW builds contain stubs because the selected vcpkg CPython port does not
  support that toolchain.

These limits protect engine lifetime, save compatibility, VFS ownership, and
network/demo synchronization while leaving ACS and ZScript available for the
jobs they already perform well.
