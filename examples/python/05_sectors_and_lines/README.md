# Sectors And Lines

Live Sector/Line handles inspected on map load and mutated with User buttons.

## Try it

Bind the User buttons first: Options > Customize Controls > Weapons section
(labeled "Weapon State 1-4"), or from the console: `bind e +user2`,
`bind f +user3`, `bind g +user4`.

On map load the example picks sector 0 and line 0 as demo targets and prints
their indices on the HUD. Then:

- **User2** — pulse the selected sector's light level down for one second;
- **User3** — raise the selected sector's floor by 8 map units through
  scheduled native `move_floor` steps;
- **User4** — execute the selected line's action special with the player as
  activator (lines with no special say so instead of doing nothing).

Every action announces the affected sector/line index and what changed via a
themed `bd.ui.toast` (accent for actions, warn for "nothing here" messages),
and plays a UI sound. Maps without suitable geometry get a friendly
"no tagged sector here" style toast instead of an error.

## What it demonstrates

- `bd.sectors()` / `bd.lines()` handle lists and their live properties
  (`light`, `floor_height`, `special`, `args`, ...);
- `Sector.move_floor` driven by a repeating `bd.schedule` task until the
  destination is reached;
- `Line.activate(activator=...)` to fire a line's action special from script;
- map-agnostic guarding of empty/None results;
- action announcements with `bd.ui.toast` plus `bd.play_ui_sound` feedback.

## Notes

Real mods should select sectors by tag and lines by line ID rather than array
index. This sample uses index zero so it works on ordinary unmodified maps.
