# Elemental Squads

Every monster joins one of three elemental squads — **FIRE**, **FROST** or
**VENOM** — assigned per class from a deterministic per-map stream. The
squad is visible instantly: the monster's sprite is **tinted** in the
squad color via the `Actor.tint` property (a palette-remap translation,
so it works in both the hardware and software renderers).

| Squad | Tint | Effect |
|---|---|---|
| FIRE | orange | damage dealt x1.4 (`damage_multiply`) |
| FROST | ice blue | speed x0.8, damage taken x0.85 (`damage_factor`) |
| VENOM | green | health x1.35 |

A small `bd.ui` legend panel (top-right) tracks live monster counts per
squad, refreshing once a second. Press **User2** to toggle all tints off
and back on — attributes stay in place, demonstrating that the tint is
pure presentation and that assigning `tint = None` resets a sprite to its
class default.

## What it teaches

- `actor.tint = (r, g, b)` to paint a monster, `actor.tint = None` to reset
- `damage_multiply` / `damage_factor` for damage dealt/taken scaling
- Applying everything through one `bd.apply_actor_batch` crossing
- `actor_spawned` for immediate application to mid-map spawns
- Deterministic per-map assignment from a seeded `bd.rng` stream

## Running it

```bash
tools/play-python-example.py     # choose 19_elemental_squads
```

Or directly:

```bash
./build/biaseddoom -python -iwad ~/games/doom2.wad \
    -file examples/python/19_elemental_squads +map map01
```

Bind User2 in *Options -> Customize Controls* or with `bind g +user2`.
