# Batched Real-Time Updates

Animate 48 live actor handles every two tics with a single
`bd.apply_actor_batch` crossing, and see the measured cost on the HUD.

The example defines an invisible, non-blocking marker actor, keeps one
lightweight handle per marker, and rebuilds a compact operation list per
update. One bulk call then applies all 48 position changes inside the engine,
and `bd.profile()` supplies the timing so the saving is a number on screen,
not a claim in a comment.

## Try it

Load any map and watch the `BATCH` panel in the top-left corner. Every
two-second wave it refreshes — actors updated, profiler-measured average
milliseconds per update, wave number — with a soft switch sound. The
millisecond figure is the profiler-measured average cost of one full update:
building the operation list plus the single native crossing.

## What it demonstrates

- Caching GC-safe live `Actor` handles instead of rebuilding snapshots.
- `bd.apply_actor_batch` applying a whole wave of mutations in one C API
  crossing (it returns how many operations were applied).
- Deriving per-batch cost from `bd.profile()` counters: diffing `calls` and
  `total_us` between waves instead of adding your own timers.
- Map-lifetime hygiene: handles are dropped on `map_unload`.

## Notes

- Use this pattern for dense control loops: native callback filters, compact
  live handles, and one bulk crossing keep Python overhead explicit and
  measurable.
- The markers stay invisible; the `bd.ui` panel is the observable output.
