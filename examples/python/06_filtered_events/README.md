# Filtered Gameplay Events

Native class_name/tid/priority event filters, watched on one tagged probe monster.

This example creates one probe zombie with TID 9301, then damages it, kills
it, and destroys the corpse on a schedule. Every handler is decorated with
`class_name`/`tid` filters so only the probe's events ever reach Python; a
labeled world health bar lets you find the probe, and a status toast shows
which filtered event fired last.

## Try it

1. Run any map with this mod loaded.
2. Find the probe: it carries a health bar with its name above it.
3. Shoot it during the first two seconds to fire the `actor_damaged` filter
   yourself — a status toast names each event as it arrives.
4. Stand back and watch the scripted sequence finish: damage, death, and
   corpse destruction, each announced by its own filtered callback.

## What it demonstrates

- `@bd.on(..., class_name="ZombieMan", tid=9301)` native filtering — the
  filter arguments are the whole point, kept front and center;
- `priority=100` ordering on the spawn handler;
- a `player=0` slot filter for contrast;
- synchronous dispatch: the scripted `probe.damage()` call fires its callback
  before the next Python line runs;
- `bd.draw_world_bar(..., label=True)` to tag the probe in the world, and a
  per-event-colored `bd.ui.toast` for the last filtered event.
