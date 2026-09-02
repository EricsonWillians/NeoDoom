# Combat And Inventory

Three cooldown-gated combat abilities driven by the engine's User buttons.

## Try it

Bind the User buttons first: Options > Customize Controls > Weapons section
(they are labeled "Weapon State 1-4"), or from the console:
`bind q +user1`, `bind e +user2`, `bind f +user3`.

Then, in any Doom II map:

- **User1** — SUPPLY: give 10 Clip ammo and heal 10 hp;
- **User2** — STRIKE: spawn a practice ZombieMan, fire a native hitscan at it,
  and launch an aimed Rocket;
- **User3** — NOVA: direct + radius damage on the practice target, with a
  screen-flash impact.

Each ability has a readiness bar on an `ABILITIES` bd.ui panel at the
bottom-left of the screen: the bar fills (in the ability's color) as the
cooldown recovers and sits full when ready. Pressing a recharging ability
toasts how many seconds are left instead of firing.

## What it demonstrates

- `Actor.give_inventory` / `inventory_count` / `heal` through the native
  pickup and healing paths;
- `bd.line_attack` hitscan and `bd.spawn_missile` aimed projectiles;
- `Actor.damage` and `bd.radius_damage` with the player as damage source;
- edge-triggered button reading in `pre_tick` (`pressed = buttons & ~prev`);
- cooldown UI with `bd.ui.panel` bar rows (readiness 0..1 per ability);
- feedback via `bd.play_ui_sound`, `bd.hud_text`, `bd.ui.toast`, and
  `bd.screen_flash`.

## Notes

The example deliberately uses Doom II class, inventory, sound, and missile
names. Adapt those names for other games.
