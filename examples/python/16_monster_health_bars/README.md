# Monster Health Bars

This example floats a persistent world-space health bar over every live
monster using the display-list API: the script registers each bar once and
the engine re-renders it every HUD frame, so there is no per-frame drawing
loop in Python at all.

What you see in game:

- **Names** — every bar carries the monster's `GetTag()` name above it
  (`label=True`, bumped to `label_scale=2.0` so names stay clearly readable
  at distance).
- **Health gradient** — with no explicit `fg`, the engine colors the fill by
  health per frame: green above 60%, yellow down to 30%, red below. Bars
  also get a black border and padded dark backdrop for free.
- **Current/max HP text** — the built-in label only renders the name, so a
  second `bd.draw_world_text` item floats just below each bar showing
  `hp: 13/20`, refreshed on every `actor_damaged` event.
- **Wall occlusion** — `occlude=True` (the default) hides a bar whenever the
  player has no line of sight to the monster; bars also fade out over the
  last 20% of `max_distance`.
- **CRITICAL flash** — under 30% health the bar stops following the gradient
  and flashes red/amber, driven by a 6-tic repeating task that re-calls
  `draw_world_bar` with explicit `fg` colors. On the bright phase the task
  also floats a `CRITICAL!` label above the bar with
  `duration=FLASH_TICS / TICRATE`: the label auto-expires exactly when the
  flash phase ends, so the blink needs **no manual `draw_clear` and no
  cleanup task** — healing or death simply stops re-registering it and the
  last one vanishes on its own within 6 tics. Healing back above 30%
  restores the gradient (`fg=None`).

Bars are created on `actor_spawned` plus an initial `bd.actor_refs()` sweep
one tic after `map_load`. Update-by-id is the whole trick: bar ids come from
a per-map slot counter keyed by the live Actor handle (handles are hashable,
most map monsters have no TID), and re-calling a draw function with the same
id replaces the item. Dead monsters' bars hide themselves, and everything
vanishes automatically on destruction or map unload.

## Run it

```sh
tools/play-python-example.py   # pick 16_monster_health_bars
```

or manually:

```sh
./tools/build-python-examples.sh 16_monster_health_bars
./build/biaseddoom -iwad /path/to/DOOM2.WAD \
    -file ./build/python-examples/16_monster_health_bars.pk3 \
    -python +map map01
```

Shoot any monster: its named bar drains live through green/yellow/red, the
hp text under the bar ticks down, and under 30% the bar flashes red/amber
with a blinking `CRITICAL!` label. Walk behind a wall and the bar
disappears.
