"""Sprint toggle that modifies the native user command in pre_tick.

Try it: USER1 toggles sprint (bind +user1); watch the corner badge and FOV.
"""

import biaseddoom as bd

INDICATOR_ID = 910  # canvas id of the persistent corner badge

sprinting = False
previous_buttons = 0


def set_indicator(visible):
    """Persistent 'SPRINT' badge drawn via the canvas display list."""
    try:
        if visible:
            bd.draw_text(">> SPRINT", id=INDICATOR_ID, x=0.02, y=0.02,
                         color=(120, 255, 120), scale=0.75)
        else:
            bd.draw_clear(INDICATOR_ID)
    except RuntimeError:
        pass


@bd.on("pre_tick", priority=100)
def sprint_controller(event):
    global sprinting, previous_buttons
    player = bd.player(0)
    if player is None or player.actor is None:
        return

    buttons = player.buttons
    if (buttons & bd.BT_USER1) and not (previous_buttons & bd.BT_USER1):
        sprinting = not sprinting
        bd.play_ui_sound("switches/normbutn")
        if sprinting:
            bd.ui.toast("SPRINT ON", color=bd.ui.theme.good)
            bd.screen_fade(80, 255, 80, 0.15, seconds=0.4)  # subtle green pulse
        else:
            bd.ui.toast("SPRINT OFF", color=bd.ui.theme.bad)
            bd.screen_fade(255, 80, 80, 0.15, seconds=0.4)  # subtle red pulse
        set_indicator(sprinting)
    previous_buttons = buttons

    if sprinting:
        # set_input rewrites this tic's user command before the player pawn
        # consumes it: double the forward speed the engine asked for.
        boosted = max(-32768, min(32767, player.forward_move * 2))
        player.set_input(forward=boosted)
        player.fov = min(110.0, player.fov + 1.5)
    else:
        player.fov = max(90.0, player.fov - 1.5)


@bd.on("map_load")
def show_help(event):
    global previous_buttons
    previous_buttons = 0
    set_indicator(sprinting)  # re-register the badge for the new map's HUD
    try:
        bd.hud_text("USER1: toggle sprint", id=9, x=0.5, y=0.12,
                    color="gold", hold=4.0, fade=1.0)
    except RuntimeError:
        pass


@bd.on("player_spawned", player=0)
def console_player_spawned(event):
    bd.log("controller: native player filter matched slot 0")
