# Player Controller

This example implements a User1 sprint toggle in `pre_tick`, before native
player thinking consumes the user command. While enabled it doubles forward
movement and widens FOV; disabling smoothly returns FOV to 90.

Tick events are world-wide, so the callback explicitly selects `bd.player(0)`.
The separate `player_spawned` callback demonstrates the native `player=0`
event filter. Multiplayer gameplay mutation remains intentionally rejected by
API v2, so this is a single-player controller example.
