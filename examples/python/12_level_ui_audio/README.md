# Level, UI, And Audio Operations

A tiny on-screen menu panel for level flow, music, specials, and messages.

## Try it

Bind the User buttons first: Options > Customize Controls > Weapons section
(labeled "Weapon State 1-4"), or from the console: `bind q +user1`,
`bind e +user2`, `bind f +user3`, `bind g +user4`.

A themed `bd.ui` menu panel is drawn on the HUD at map load:

- **User1 / User2** — move the selection marker up/down (with a navigate click);
- **User3** — run the highlighted item (with a confirm click);
- **User4** — exit the level immediately, from anywhere.

Menu items: center message + local actor sound, immediate music change to
`D_RUNNIN`, the named action special `Light_ChangeToValue [0, 160]`, and a
normal level exit.

## What it demonstrates

- building a simple interactive menu from `bd.ui.panel` rows (the selected
  row shows an accent-colored `<` marker);
- `bd.center_message` and local `Actor.play_sound`;
- `bd.set_music` for immediate music switches;
- `bd.execute_special` with a named special plus arguments, with error
  handling for maps that cannot run it;
- `bd.exit_level(keep_facing=True)` for level flow;
- `bd.play_ui_sound` navigate/confirm feedback.

## Notes

For explicit map transitions (rather than normal exits), use
`bd.change_level(map_name, flags=bd.CHANGELEVEL_KEEPFACING |
bd.CHANGELEVEL_NOINTERMISSION)` in your own game rules. The Doom sound and
music names should be changed for other games.
