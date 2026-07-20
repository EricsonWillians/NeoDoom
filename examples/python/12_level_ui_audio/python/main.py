"""Center messages, music, sounds, action specials, console, and level flow."""

import biaseddoom as bd


previous_buttons = 0


def on_engine_start(event):
    bd.execute('echo "Python level/UI/audio example loaded"')


def go_to_map(map_name):
    """Reusable explicit transition helper; call it from your own game rules."""
    bd.change_level(
        map_name,
        flags=bd.CHANGELEVEL_KEEPFACING | bd.CHANGELEVEL_NOINTERMISSION,
    )


@bd.on("map_load")
def show_help(event):
    bd.center_message(
        "User1: message/sound  User2: music  User3: special  User4: exit",
        bold=True,
    )


@bd.on("pre_tick")
def controls(event):
    global previous_buttons
    player = bd.player()
    pawn = player.actor if player else None
    if pawn is None:
        return
    pressed = player.buttons & ~previous_buttons
    previous_buttons = player.buttons

    if pressed & bd.BT_USER1:
        bd.center_message("Immediate Python center message", bold=True)
        pawn.play_sound("misc/chat", volume=0.75, local=True)

    if pressed & bd.BT_USER2:
        changed = bd.set_music("D_RUNNIN", looping=True, force=True)
        bd.log(f"level: music change success={changed}")

    if pressed & bd.BT_USER3:
        try:
            result = bd.execute_special(
                "Light_ChangeToValue", [0, 160], activator=pawn
            )
            bd.log(f"level: action special returned {result}")
        except (ValueError, RuntimeError) as error:
            bd.log(f"level: this map cannot run the sample special: {error}", level="warning")

    if pressed & bd.BT_USER4:
        bd.exit_level(keep_facing=True)
