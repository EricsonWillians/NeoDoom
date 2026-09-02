# Player Controller

A sprint toggle built by rewriting the player's input command every tic.

## Try it

Bind **User1** first: Options > Customize Controls > Weapons section (labeled
"Weapon State 1-4"), or from the console: `bind q +user1`.

Press **User1** to toggle sprint. While sprinting you get:

- doubled forward movement (`Player.set_input` rewrites the user command
  before the pawn consumes it, at `pre_tick` priority 100);
- a smooth FOV widen to 110, easing back to 90 when disabled;
- a persistent green `>> SPRINT` badge in the top-left corner;
- green/red `bd.ui` toasts, a subtle screen-fade pulse, and a UI click on
  toggle.

## What it demonstrates

- `Player.buttons` edge detection and `Player.set_input(forward=...)` to
  implement a movement modifier without touching actor physics;
- `Player.fov` easing for game-feel polish;
- `bd.screen_fade`, `bd.play_ui_sound`, and colored `bd.ui.toast` as
  state-change feedback;
- a persistent on-screen indicator via `bd.draw_text` / `bd.draw_clear`;
- the native `player=0` event filter on `player_spawned`.

## Notes

Tick events are world-wide, so the callback explicitly selects `bd.player(0)`.
Multiplayer gameplay mutation is intentionally rejected by API v2, so this is
a single-player controller example.
