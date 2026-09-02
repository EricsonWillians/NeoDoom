# Persistent State

Keep per-session stats (visits, playtime, deaths) in `bd.state` across maps,
saves, reloads, and death — and watch them live on an on-screen panel.

`bd.state` is a JSON-serialized dictionary shared by all Python scripts. It is
written into savegames and restored by `py_reload`, so anything stored under a
unique key outlives the current map, the current life, and the module itself.

## Try it

Load any map and look at the top-right **session panel**: visits, seconds
played, deaths, and the previously visited map. Change maps, save and reload,
or die — the counters keep going, and each death flashes a big red
`STATE SURVIVES DEATH` announcement while the panel's death count ticks up.

## What it demonstrates

- Namespacing mod data under one unique key in `bd.state`.
- `setdefault` at `engine_start`, because reload restores old state only
  after fresh modules initialize.
- A persistent `bd.ui.panel` (top-right, themed rows: visits, seconds,
  deaths, last map) fed by persistent state on map load and a slow 1 Hz tick.
- The `player_died` event proving state survives death; `save`/`load`
  callbacks logging the exact data written and restored.

## Notes

- Only JSON-compatible values belong in `bd.state` — never live actor handles.
- `bd.ui` panels self-guard their draws: before a level and status bar exist
  the draw fails silently and is retried on map load.
