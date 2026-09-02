# Live Actor Handles

A friendly ZombieMan whose retained live handle reports its health in real time.

After map entry this example spawns a friendly `ZombieMan`, retains its live
`Actor` handle, changes fields and relationships, reads a snapshot, and steers
it for five seconds. A floating `FRIEND` label is anchored to the handle, and
a slow repeating task polls the handle so any health change pops up as a
themed `bd.ui.toast`.

## Try it

1. Run any map with this mod loaded.
2. A zombie tagged `FRIEND` spawns in front of you and circles for five
   seconds.
3. Shoot it: a health toast appears with every hit (red when it loses HP,
   green when it gains), proving the handle stays live.
4. Kill it: the label vanishes and the handle reports invalid.

## What it demonstrates

- `bd.spawn` returning a GC-safe live `Actor` handle;
- live field writes (`health`, `target`, `master`, `args`) and
  `set_flag`/`get_flag`;
- `Actor.snapshot()` for serialization-friendly reads;
- handle liveness via `companion.valid`, polled from a repeating
  `bd.schedule` task;
- `bd.draw_world_text` anchored to a live actor, plus `bd.ui.toast` for
  health-change and spawn-failure feedback.

## Notes

The handle is rooted through the engine GC while Python owns it and is
released on map unload. Live handles are for transient runtime control; save
TIDs or plain data in `bd.state`, never handle objects.
