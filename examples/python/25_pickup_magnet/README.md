# Pickup Magnet

While the magnet is on, every pickup within 640 units **slides toward
you** — health bonuses drift around corners, ammo slides down stairs. The
nearest pulled items get a faint blue ground ring so you can see the pull
working. Items still use the native pickup path when you touch them; the
magnet never collects anything by itself. Press **User3** to toggle it.

## What it teaches

- Pickup detection with `ref.get_flag("SPECIAL")` (the engine's
  touch-pickup flag) combined with `distance_to` range checks
- Gentle steering: horizontal velocity writes that leave the vertical
  component to the physics engine
- A bounded visual pool: eight `draw_world_ring` slots reassigned to the
  nearest pulled items every steering pass
- User-button toggles with edge detection (`BT_USER3`)

## Running it

```bash
tools/play-python-example.py     # choose 25_pickup_magnet
```

Or directly:

```bash
./build/biaseddoom -python -iwad ~/games/doom2.wad \
    -file examples/python/25_pickup_magnet +map map01
```

Bind User3 in *Options -> Customize Controls* or with `bind h +user3`.
