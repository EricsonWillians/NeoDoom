- **Smart ammo**: rockets/cells only drop once you own a weapon that uses
  them, and shell/clip weight goes up with the SuperShotgun/Chaingun — no
  useless drops. Bosses restock you (Medikit + ClipBox + an ammo box for
  your best gun) — the boss fight is the ammo sink, so it pays it back.# Endless Horde Mode

A true endless survival mode that works on **any** map. The mode
**commandeers the map itself**: on arrival
it captures every designer-placed monster position as its spawn pool and
removes the original monsters (destroyed outright — **no carcasses**;
boss-death map specials like MAP07's still fire later, when same-class
horde monsters die). Waves then spawn from that pool with fit-checked
placement — **nothing ever materializes inside walls or the void**,
because the map's own design guarantees valid spots. Monster-less maps
fall back to a player-relative spawn ring, so the mode truly works
regardless of the level.

**Every map is a fresh run.** Changing levels — exit, warp, savegame, or
a procedurally generated map — completely resets the wave system: wave,
score, mutators and the arena pool all restart at wave 1. Only your
**best wave / best score records** persist across maps and sessions.

**You always know what counts, and rarity is readable.** Every wave
defender is tinted — crimson for the rank-and-file, affix colors for
**champions**, gold for **uniques** — and rare monsters carry an
overhead **health bar** (manual-fraction: it divides by the monster's
real buffed max health, ground-truthed from the actor right after its
stat batch is applied — so crowned uniques, champions and bosses all
drain from the first hit) with a readable floating name whose height
adapts to distance so the two never overlap. Loot glints gold where it falls. If
it isn't tinted, it isn't part of the wave. Champion and unique kills
get a **colored toast and a sound**. All world visuals are sight-occluded
— nothing draws through walls. A **compass needle** under the HUD always
points at the nearest defender (`v` glides along a strip; a gold `<`/`>`
at the edge means it's behind you), and when only the last few defenders
of a wave remain, a **HUD motion tracker** calls out bearing and
distance to each survivor (`TRACKER: NE 240 · W 96`) — no more hunting
for the final imp.

Health bars are **manual-fraction**: they divide by the monster's real
buffed max health, not the class default, so a champion's bar drains
from the first hit — and the name's height above the bar adapts to your
distance, so the two never overlap. Boss HP follows the mode's own curve
(`(1.2 + 0.5 x boss index) x wave health multiplier`) — no more
wave-5 slog against a bar that never moves.

### Color language

The whole UI runs on a hell palette (blood-orange accents, ember
warnings, ember-gold loot) with Doom's fiery **bigfont** for score
popups (colored by kill rarity), the WAVE CLEAR banner and boss titles.

- **Crimson tint** — rank-and-file wave monster (counts toward the quota)
- **Affix-colored tint + overhead name/bar** — champion (the color tells
  you its prefix: Burning is orange, Spectral is pale, Vile is purple...)
- **Gold** — uniques, bosses, and all loot/supply glints
- **Green** — breather countdowns and wave-clear banners
- **Red/purple banners** — wave and boss announcements

There is no win state: survive as deep as you can, chase your best wave.
Leaving the map — exit, warp, savegame, procedural level — **resets the
run entirely** (wave, score, mutators, arena pool); only best records
persist. Only death ends a run mid-map.

## Wave mutators

From wave 2, each wave rolls a **mutator** (~65% chance per roll,
entropy-mixed so runs diverge), announced in the wave banner so every
wave plays differently:

| Mutator | Effect |
|---|---|
| FRENZY | speed x1.25 |
| GLASS CANNONS | damage dealt x1.4, health x0.6 |
| FORTIFIED | health x1.6, speed x0.85 |
| SPECTRAL HOST | alpha x0.7, damage taken x0.9 |
| SWARM | quota x1.5, health x0.8 |

Mutators **stack** in the deep game: two from wave 20, three from wave
30. All rolls use entropy-mixed streams fed by kill timing — same map,
same wave, different run.

## The endless deep game

Nothing stops scaling: the kill quota grows to 40, champion chance keeps
climbing to 50%, **twin bosses** arrive from wave 20, mutators stack,
milestone waves pay **double clear bonus** every 10th wave, and the
health curve never caps. The run only ends when you die.

## Rarity and loot

- **Champions** roll per spawn (`8% + 1.2%/wave`, capped at 50%): one
  prefix, maybe a suffix, +50% health, 3x score, and on death either the
  **best weapon you're missing** (35%) or guaranteed **medium loot**
  (ShellBox / Stimpack / Medikit / ArmorBonus; RocketBox and CellPack
  join from wave 8).
- **Uniques** are crowned from each wave's spawns: one from wave 4, two
  from wave 12. Random crowns get two prefixes + a suffix; named uniques
  get their fixed identity. 3.75x total affix health (plus named bonuses),
  gold visuals, 5x score, an entrance toast, and on death a weapon (60%)
  or
  guaranteed **major loot** (Medikit / GreenArmor; Backpack from wave 10,
  Soulsphere from wave 12, **powerups** with depth: Berserk w8,
  BlurSphere w12, BlueArmor w14, InvulnerabilitySphere w16).
- **Normal kills** drop minor pickups (clips, shells, bonuses, stims)
  at a wave-scaled chance (`12% + 1%/wave`, capped at 35%) — plus a
  **lucky hit** whose window is itself re-rolled per kill (1–10%):
  lucky kills roll the medium table, and 15% of them jackpot into the
  **major table**, announced with a gold `LUCKY DROP` toast. All loot lands at floor level, even
  when a flier dies in the sky. **Bosses**
  burst into the best missing ladder weapon (or a Soulsphere —
  **Megasphere** from wave 15 — with a full arsenal) plus a Medikit.
- **The endless ration**: every marked kill feeds ammo straight into
  your pool — no pickup logistics — matched to the weapons you actually
  own (bullets always; shells once you have a shotgun; rockets/cells on
  alternating kills once you have the launchers), growing +100% every
  8 waves. This is what keeps horde runs sustainable on maps like MAP01
  that place barely any ammo.
- **Weapon ladder**: Shotgun (w2), Chainsaw (w4), Chaingun (w5),
  SuperShotgun (w6), RocketLauncher (w8), PlasmaRifle (w10), BFG9000
  (w14). Wave-clear supply drops always include the best weapon you're
  missing, so progression never depends on champion luck.
- Prefixes (14): Burning, Molten, Spectral, Vile, Hellforged, Charged,
  Stone, Rabid, Voidtouched, Argent, Nightmare, Brood, Leeching, Barbed.
  Suffixes (12): of the Pit, of Deimos, of the Icon, of Babel, of
  Pandemonium, of the Chasm, of the UAC, of the Brood, of Detonation,
  of the Leech, of Barbs, of Hoarding. Every affix carries real stat
  mods (damage dealt/taken, speed, health, scale, alpha) — and some
  carry **mechanics**: volatile monsters explode on death, Brood
  monsters birth two lesser defenders from their corpse, Leeching
  monsters heal from the damage they deal, Barbed monsters reflect 30%
  of damage taken at their attacker, Hoarding monsters drop double loot.
- **Named uniques**: from wave 6, half of all crowns are hand-crafted
  identities with fixed affix combos and extra mods — The Maledict,
  Babelspawn, The Broodmatron, Gatekeeper of Dis, The Argent Husk.

## Difficulty curve

Every defender scales with the wave, on top of its affixes:
**health +6%/wave** (uncapped), **speed +3%/wave** (cap 1.7x) and
**damage dealt +5%/wave** (cap 2.5x) — deep waves both sponge *and*
threaten. Champion chance and loot quality scale too, and the trickle
quickens in deep waves (up to 4 spawns/pass).

## The loop

- Every wave has a **kill quota** (4 + 2/wave, growing to 40) and a
  **concurrent cap** (6 + wave, capped at 18 and by the map's pool size).
  A trickle task replaces fallen defenders every 10 tics **at the map's
  own spawn places** (shuffled fresh each wave, reused as defenders die —
  increasing quantities from the same designed spots) until the quota is
  exhausted. The pressure never lets up, unlike spawn-once waves.
- Clear the wave (quota exhausted AND every defender dead) for a 5-second
  breather, a clear bonus and a **supply drop**: ammo/health (Medikit
  every 2nd drop, GreenArmor every 3rd) ringed in gold near you, plus the
  best ladder weapon you're missing.
- Every 5th wave is a **boss wave**: a tinted, scaled boss with a Doom
  epithet (`Baron of Hell the Defiler`), a big health bar under its
  title, and a trash escort, **entering at the spawn point farthest from
  you** — **twin bosses from wave 20**. Bosses pay 500 x boss index
  — and an Archvile boss can raise fallen defenders, which **re-join the
  wave with their affix identity intact** and must be killed again.
- Monsters summoned by defenders (Pain Elemental Lost Souls, boss
  spawners) **join the wave marked** without consuming quota.
- Past wave 8 the loot tables widen (rockets/cells, boxes/packs), and
  uniques start carrying Backpacks (wave 10) and Soulspheres (wave 12).
- A **straggler sweep** runs every 6 seconds: defenders stranded across
  the map, dropped in a pit, or simply stuck (moved < 24 units since the
  last sweep while beyond melee range) are relocated to another pool
  point — and a point whose spawns end up stuck twice is **retired**, so
  sealed teleport closets eliminate themselves. No map layout can
  soft-lock a wave.

## Escalation tiers

| Wave | New defenders |
|---|---|
| 1 | Zombieman, DoomImp |
| 2 | LostSoul (mutators begin) |
| 3 | ShotgunGuy, Demon (weapon ladder begins) |
| 4 | ChaingunGuy (uniques begin) |
| 5 | Spectre, Cacodemon (bosses begin) |
| 8 | HellKnight, Revenant (deep loot tables open) |
| 10 | PainElemental (summons join the wave) |
| 12 | BaronOfHell, Mancubus, Arachnotron (two uniques per wave) |

## Score and bests

Kills pay `10 x wave x rarity` (champions x3, uniques x5), clears pay
`100 x wave`, bosses pay `500 x boss index`, all with floating `+N`
popups. `bd.state` persists your **best wave** and **best score** across
maps and sessions; passing your best wave earns a `NEW BEST` toast, and
your best score is saved even if you die mid-wave. Death shows
`OVERWHELMED` with your final standing; respawning restarts at wave 1
(bests kept).

## Module layout

The mode is a proper package, wired by `bd.import_script` from a thin
`main.py` bootstrap (siblings use plain `import horde_x as x`):

| Module | Owns |
|---|---|
| `config.py` | every tunable and table |
| `runstate.py` | run state, `bd.state` accessors, entropy streams, guards |
| `affixes.py` | Doom-lore naming and combat stat math |
| `visuals.py` | HUD, popups, rarity names/bars, tracker, compass |
| `monsters.py` | spawn pool, spawning/marking/crowning, tasks, actor events |
| `loot.py` | drop tables, weapon ladder, smart ammo, supply drops |
| `waves.py` | wave state machine, mutators, bosses, milestones |
| `lifecycle.py` | fresh-run-per-map reset, player death/respawn |

## What it teaches

- Modular script architecture with `bd.import_script` and a shared
  runstate module (`runstate.wave += 1` works cross-module)

- `bd.actor_refs()` enumeration to commandeer map-authored spawn points,
  and `apply_actor_batch` for everything: stat scaling, tint marking,
  batch teleports (straggler recovery) and batch clears
- Diablo-2-style affix records with ratio-safe unique upgrades, and
  rarity-driven visuals: tint = membership, overhead name +
  distance-adaptive health bar = rarity
- Per-wave mutators merged into every combat package, and summon
  enlistment through `actor_spawned` + `master` tracking
- Weapon-ladder progression: `inventory_count` ownership checks drive
  weapon drops and ammo-aware loot tables
- `bd.spawn` with fit-checked placement plus a measured `force=True`
  fallback for monster-less maps
- Quota + trickle state machine: `bd.schedule` tasks, handle-set tracking
  through `actor_died`/`actor_destroyed`/`actor_revived`/`actor_spawned`
- Self-pruning data: per-spawn-point strike counters retire sealed closets
- Per-actor visuals with id pooling (`draw_world_ring`/`draw_world_text`/
  `draw_world_bar` + `draw_clear`) kept sight-occluded — manual-fraction
  health bars (`track=None, frac=...`) refreshed on `actor_damaged` —
  plus a compass needle and a motion tracker for stragglers
- Boss packaging: tint + scale + `damage_multiply` + bar + epithet
- Run state in module globals outliving map-local tasks and actors, with
  guards (`phase` checks) against stale/duplicate scheduled tasks
- Persistent bests through `bd.state`, attempt state in module globals
- Update-on-change HUD text; no panels, so the view stays clear

## Running it

```bash
tools/play-python-example.py     # choose 24_wave_defense
```

Or directly:

```bash
./build/biaseddoom -python -iwad ~/games/doom2.wad \
    -file examples/python/24_wave_defense +map map01
```
