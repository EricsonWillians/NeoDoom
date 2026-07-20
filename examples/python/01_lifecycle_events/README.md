# Lifecycle Events

This read-only example shows conventional `on_*` callbacks and `@bd.on` for:

- engine startup/shutdown;
- map load/unload and save/load;
- `pre_tick`, `tick`, and `post_tick` ordering;
- the `python_time_us` post-tick measurement;
- a player-entry event.

It is a good first example because it changes no gameplay state. Watch the
console with `-stdout` while entering a map, saving, loading, and quitting.
