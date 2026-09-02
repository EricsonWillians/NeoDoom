# Patrol Routes

Up to six monsters per map get a **patrol circuit**: a dim octagon of
`bd.draw_world_line` segments around their home post, orbited at walk
speed. The custom steering only runs while the monster has **no live
player target** — the moment it spots you, native Doom AI takes over; lose
it, and it drifts back to its circuit. Dead patrollers release their
circuit slots for reuse.

## What it teaches

- Waypoint steering on live handles: per-tick `velocity` writes, with the
  engine's collision and vertical physics untouched
- `draw_world_line` between static `(x, y, z)` points for path
  visualization
- Clean handoff between scripted steering and native AI via `ref.target`
- Slot recycling for per-monster canvas id pools

## Running it

```bash
tools/play-python-example.py     # choose 22_patrol_routes
```

Or directly:

```bash
./build/biaseddoom -python -iwad ~/games/doom2.wad \
    -file examples/python/22_patrol_routes +map map01
```
