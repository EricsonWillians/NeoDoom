# Custom Status Effects

This example adds two custom status effects to monsters, each with a clear
visual representation in the world: a recolored floating health bar, a
`bd.draw_world_text` status label above it, a `bd.draw_world_texture`
silhouette **status icon** above the label, and a `bd.draw_world_line`
**tether beam** from the player to the afflicted monster in the status
color. A top-left `bd.ui` **STATUS** panel counts active statuses
(BURNING / CHILLED rows) and hides itself while both are zero.

## How the statuses trigger

- **Burning** — any player-credited damage (`actor_damaged`'s `source_ref`
  is a live player handle) ignites the monster: the bar fill is overridden
  with a static green `fg`, an orange `BURNING` label in `bigfont` floats
  above the bar, a pistol crack plays (`bd.play_ui_sound`), and a repeating
  `bd.schedule` task deals 2 damage every 10 tics for 3 seconds. The ticks
  use the custom damage type `PythonBurn`, which the `actor_damaged` filter
  excludes, so burn damage never re-ignites the monster. Hitting a burning
  monster again cancels the old task (`bd.cancel_task`) and refreshes the
  timer. On burn-out the label is cleared and the bar's `fg=None` restores
  the engine's automatic green/yellow/red gradient.
- **Chilled** — every 6 seconds a repeating map-local task picks one random
  live monster (`bd.actor_refs` + a deterministic `bd.rng` stream) and
  chills it for 4 seconds: cyan bar tint, an ice-blue `CHILLED` label, and
  a real movement slow — `Actor.speed` is a writable property, so the
  script captures the original value, multiplies it by 0.35, and restores
  it when the chill thaws. A re-chill while already chilled just refreshes
  the duration without stacking the slow.

If both statuses are active on the same monster, Burning wins the bar tint,
label, icon, and beam color; when it burns out, `apply_visuals` falls back
to the Chilled look automatically.

## Status icons and tether beams (canvas v2)

- **Icons** — `bd.draw_world_texture` anchors a Doom II sprite lump to the
  monster at `offset_z=46` (above the label at 26): Burning uses `BON1A0`
  (health-bonus potion), Chilled uses `ARM1A0` (armor-bonus helmet), both
  verified to exist in `doom2.wad`. `tint=` renders the sprite as a solid
  silhouette — red `(255, 60, 20)` for Burning, cyan `(120, 220, 255)` for
  Chilled — and `size=` is in map units, auto-scaled by distance. Both the
  icon and the label are registered with `duration=` equal to the status
  length as a **self-cleaning backstop**: they vanish on their own even if
  the manual `draw_clear` is never reached. The manual clear is still kept
  because the burn↔chill handoff (or an early thaw) can happen *before* the
  duration elapses.
- **Tether beams** — while any status is active, `bd.draw_world_line`
  connects `bd.player(0).actor` to the monster in the status color, one
  beam per afflicted monster with its own id (`BEAM_ID_BASE + slot`). Both
  endpoints are live Actor handles, so the beam follows both actors per
  frame with no re-registration — status ranges and causes are readable at
  a glance. Beams are cleared in `apply_visuals` when the status expires
  and explicitly on `actor_died`: unlike bars/labels/icons, a world line
  has no health gate, so without the clear it would keep pointing at the
  corpse.

## APIs used

`bd.on` (`actor_spawned` / `actor_damaged` / `actor_died` / `map_load`),
`bd.schedule` / `bd.cancel_task` (one-shot, repeating, and map-local
tasks), `bd.actor_refs`, `bd.rng`, live Actor handles (`damage` with a
custom `damage_type`, writable `speed`), `bd.player` (beam endpoint),
`bd.draw_world_bar` (explicit `fg` override vs. the automatic gradient),
`bd.draw_world_text`, `bd.draw_world_texture` (tinted silhouette icons),
`bd.draw_world_line` (actor-to-actor tether beams), `layer` / `duration`
item lifecycle, `bd.ui.panel` rows + `hide`/`show`, `bd.draw_clear`,
`bd.play_ui_sound`, `bd.TICRATE`.

## Run it

```sh
tools/play-python-example.py   # pick 17_custom_status_effects
```

or manually:

```sh
./tools/build-python-examples.sh 17_custom_status_effects
./build/biaseddoom -iwad /path/to/DOOM2.WAD \
    -file ./build/python-examples/17_custom_status_effects.pk3 \
    -python +map map01
```

Shoot any monster to set it Burning (green bar, orange label, red potion
icon, orange tether beam, ticking damage). Every few seconds a random
monster turns Chilled (cyan bar, ice-blue label, cyan helmet icon, cyan
beam) and crawls until it thaws.
