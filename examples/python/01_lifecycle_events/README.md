# Lifecycle Events

Every BiasedDoom lifecycle event, shown on an on-screen ticker as it fires.

This read-only example changes no gameplay state. Each lifecycle callback logs
to the console (the ground truth) and pushes its name onto a themed `bd.ui`
ticker panel (top-left) that always shows the last three events, newest on
top.

## Try it

1. Run any map with this mod loaded.
2. Watch the top-left ticker: `map_load`, `pre_tick`, `tick`, `post_tick`, and
   `player_entered` appear within the first moments.
3. Save, load, exit the level, and quit to see `save`, `load`, `map_unload`,
   and `engine_shutdown` fire (run with `-stdout` to follow the console).

## What it demonstrates

- conventional `on_*` callbacks alongside the `@bd.on` decorator;
- the full lifecycle set: engine start/shutdown, map load/unload, save/load,
  `pre_tick`/`tick`/`post_tick` ordering, and `player_entered`;
- the `python_time_us` post-tick measurement;
- a persistent `bd.ui.panel` ticker with dimmed older rows, guarded draws and
  automatic re-render on map load;
- `bd.hud_text` for first-time on-screen guidance.
