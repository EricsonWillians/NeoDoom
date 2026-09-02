"""Hold-to-slow-mo: timescale, screen fades, HUD text, and UI sounds."""

import biaseddoom as bd


BULLET_TIMESCALE = 0.35
HUD_ID = 100  # fixed message id; reusing it replaces the previous message

bullet_active = False
previous_buttons = 0


def show_hud(text):
    # hud_text raises RuntimeError before the level's status bar exists.
    try:
        bd.hud_text(text, id=HUD_ID, color="cyan", hold=9999.0, fade=0.25)
    except RuntimeError:
        pass


def engage_bullet_time():
    global bullet_active
    bullet_active = True
    applied = bd.set_timescale(BULLET_TIMESCALE)  # returns the applied value
    bd.screen_fade(30, 60, 120, 0.25, seconds=0.4)  # cool blue tint
    bd.play_ui_sound("switches/normbutn")
    show_hud("BULLET TIME")
    bd.log(f"bullet: engaged at timescale {applied:.2f}")


def release_bullet_time():
    global bullet_active
    bullet_active = False
    bd.set_timescale(1.0)
    bd.screen_fade(30, 60, 120, 0.15, seconds=0.3)  # brief pulse back to normal
    bd.play_ui_sound("misc/chat")
    bd.hud_clear(HUD_ID)
    bd.log("bullet: released, timescale 1.00")


@bd.on("pre_tick")
def bullet_time_controls(event):
    global previous_buttons
    player = bd.player(0)
    if player is None or player.actor is None:
        previous_buttons = 0
        return

    buttons = player.buttons
    held = bool(buttons & bd.BT_USER1)
    was_held = bool(previous_buttons & bd.BT_USER1)
    previous_buttons = buttons

    if held and not was_held:
        engage_bullet_time()
    elif was_held and not held:
        release_bullet_time()


@bd.on("map_load")
def show_help(event):
    bd.center_message("Bind User1 and hold it for Python bullet time")


@bd.on("map_unload")
def restore_timescale(event):
    global bullet_active, previous_buttons
    bullet_active = False
    previous_buttons = 0
    # i_timescale survives map changes, so restore it defensively. Gameplay
    # mutations are blocked while the world unloads, hence the guard.
    try:
        bd.set_timescale(1.0)
    except RuntimeError:
        pass
