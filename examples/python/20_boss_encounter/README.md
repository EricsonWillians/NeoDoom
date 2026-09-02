# Boss Encounter

The map's toughest monster is upgraded into **THE WARDEN** — 4x health,
1.5x size, 1.5x damage, purple tint, a gold `bd.draw_world_ring` aura and
an overhead name. On a monster-free map a BaronOfHell is conjured in
front of the player with `bd.spawn` instead, so the encounter always works.

A screen-space **boss bar** (top center: name, framed gradient fill) tracks
the fight and only redraws when the fraction changes. At 66% health the
Warden **hastens** (speed x1.4); at 33% it **rages** (damage x1.8) — each
phase summons two Imp reinforcements with `bd.spawn(..., force=True)` and
a `bd.ui.announce` warning. Killing it clears the bar and pays out a gold
announcement + screen flash.

## What it teaches

- Upgrading a live actor mid-map: `scale`, `health`, `damage_multiply`
  and `tint` in one `bd.apply_actor_batch` call
- `bd.spawn` for fallback bosses and mid-fight reinforcements
- `bd.draw_world_ring` as a boss aura
- A boss HP bar from `draw_rect` (gradient `color2`), `draw_frame` and
  `height=`-sized `draw_text`
- Phase scripting from a per-tick health poll, and boss-death detection
  by comparing event handles (`ref == boss`)

## Running it

```bash
tools/play-python-example.py     # choose 20_boss_encounter
```

Or directly:

```bash
./build/biaseddoom -python -iwad ~/games/doom2.wad \
    -file examples/python/20_boss_encounter +map map01
```
