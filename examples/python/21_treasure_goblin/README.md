# Treasure Goblin

A rare **TREASURE GOBLIN** — a gold-tinted, 3x-speed Zombieman with a gold
ground ring and an overhead title — appears near you ~8 seconds into each
map. Get close and it **sprints away**; corner it and kill it for a score
shower of `+N` popups and a toast. If it stays far from you for 30 seconds
straight, it escapes with the loot. Either way, another one spawns 25
seconds later.

## What it teaches

- Mid-map spawning with `bd.spawn` (returns a live Actor handle)
- Per-tick steering: writing `velocity` on a live handle to flee the
  player, while preserving engine-managed vertical motion
- `tint` + `draw_world_ring` + `height=`-sized `draw_world_text` as a
  complete "special monster" visual package
- Escape timers from `bd.level_time()` deltas
- One-shot `bd.schedule` respawn tasks and map-load cleanup
- Kill detection by comparing event handles (`ref == goblin`)

## Running it

```bash
tools/play-python-example.py     # choose 21_treasure_goblin
```

Or directly:

```bash
./build/biaseddoom -python -iwad ~/games/doom2.wad \
    -file examples/python/21_treasure_goblin +map map01
```
