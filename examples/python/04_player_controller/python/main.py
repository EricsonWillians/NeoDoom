"""Modify the native user command in pre_tick to implement a sprint toggle."""

import biaseddoom as bd


sprinting = False
previous_buttons = 0


@bd.on("pre_tick", priority=100)
def sprint_controller(event):
    global sprinting, previous_buttons
    player = bd.player(0)
    if player is None or player.actor is None:
        return

    buttons = player.buttons
    if (buttons & bd.BT_USER1) and not (previous_buttons & bd.BT_USER1):
        sprinting = not sprinting
        bd.center_message(f"Python sprint {'ON' if sprinting else 'OFF'}")
    previous_buttons = buttons

    if sprinting:
        boosted = max(-32768, min(32767, player.forward_move * 2))
        player.set_input(forward=boosted)
        player.fov = min(110.0, player.fov + 1.5)
    else:
        player.fov = max(90.0, player.fov - 1.5)


@bd.on("map_load")
def show_help(event):
    bd.center_message("Bind and press User1 to toggle Python sprint")


@bd.on("player_spawned", player=0)
def console_player_spawned(event):
    bd.log("controller: native player filter matched slot 0")
