# JRPG Combat Text

Damage numbers that **jump out** of the monster: every hit you land
launches its number on a parabolic arc — scale pop at launch, lateral
scatter, gravity pull, fade-out — colored and worded by severity:

| Tier | Damage | Text | Style |
|---|---|---|---|
| Graze | < 12 | `7` | small, pale white |
| Solid | 12-34 | `25` | warm yellow |
| Heavy | 35-69 | `POW!` + number | bigger, orange |
| Critical | 70-119 | `CRITICAL!` + number | bigfont, red-orange |
| Devastating | 120+ | `DEVASTATED!` + number | biggest, gold, screen flash |

The killing blow plays its full arc over the corpse — the canvas keeps
transient world text (`duration=`) alive at the anchor's position after
death, because the lethal hit is exactly when feedback matters.

Only player-caused damage is shown — infighting would flood the screen.

## What it teaches

- The `actor_damaged` payload: `actor_ref` (victim), `source_ref`
  (attacker), `inflictor_ref`, `damage`, `damage_type`, `angle`
- Professional pooling: 48 canvas ids recycled round-robin, each entry
  carrying its full animation state (victim handle, t, launch vector, tier)
- Per-tic animation by re-registering the pooled id (replacing the item in
  place): parabolic `offset_z`, lateral `offset_x`/`offset_y` scatter,
  `height=` scale pop, `alpha` fade, `duration=` backstop expiry
- Severity tiers mixing fonts (`smallfont`/`bigfont`), sizes and colors
- Multiline world text (`"POW!\n47"`) for word + number in one item

## Running it

```bash
tools/play-python-example.py     # choose 23_damage_numbers
```

Or directly:

```bash
./build/biaseddoom -python -iwad ~/games/doom2.wad \
    -file examples/python/23_damage_numbers +map map01
```
