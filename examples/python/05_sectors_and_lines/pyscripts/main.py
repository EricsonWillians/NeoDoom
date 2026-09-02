"""Inspect and mutate Sector/Line handles through interactive controls.

Try it: USER2 pulse sector light, USER3 raise the floor, USER4 trigger the line.
"""

import biaseddoom as bd

selected_sector = None
selected_line = None
previous_buttons = 0


def announce(message, sound=None, color=None):
    """Short toast plus an optional UI click; toasts self-guard their draw."""
    bd.ui.toast(message, color=bd.ui.theme.accent if color is None else color)
    if sound:
        bd.play_ui_sound(sound)


def pulse_light():
    """USER2: dip the sector's light level for one second, then restore it."""
    if selected_sector is None:
        announce("No tagged sector here - nothing to pulse",
                 sound="switches/normbutn", color=bd.ui.theme.warn)
        return
    original = selected_sector.light
    selected_sector.light = max(0, original - 64)
    announce(f"Sector {selected_sector.index}: light {original} -> "
             f"{selected_sector.light} for 1s", sound="misc/secret")

    def restore():
        if selected_sector is not None:
            selected_sector.light = original

    bd.schedule(restore, delay=bd.TICRATE)


def raise_floor():
    """USER3: move the floor up 8 units via scheduled native move_floor steps."""
    if selected_sector is None:
        announce("No tagged sector here - no floor to raise",
                 sound="switches/normbutn", color=bd.ui.theme.warn)
        return
    destination = selected_sector.floor_height + 8.0
    steps = 0
    announce(f"Sector {selected_sector.index}: raising floor "
             f"{selected_sector.floor_height:.0f} -> {destination:.0f}",
             sound="switches/normbutn")

    def move_step():
        nonlocal steps
        if selected_sector is None:  # map unloaded mid-move: stop repeating
            return False
        steps += 1
        selected_sector.move_floor(destination, speed=0.5)
        return abs(selected_sector.floor_height - destination) > 0.01 and steps < 64

    bd.schedule(move_step, delay=1, repeat=1)


def activate_line(pawn):
    """USER4: run the selected line's action special with the player as activator."""
    if selected_line is None:
        announce("No line here - nothing to activate",
                 sound="switches/normbutn", color=bd.ui.theme.warn)
        return
    if selected_line.special == 0:
        announce(f"Line {selected_line.index} has no special (plain wall)",
                 sound="switches/normbutn", color=bd.ui.theme.warn)
        return
    selected_line.activate(activator=pawn)
    announce(f"Line {selected_line.index}: triggered special "
             f"{selected_line.special} args={selected_line.args}",
             sound="misc/chat")


@bd.on("map_load")
def select_world_objects(event):
    global selected_sector, selected_line, previous_buttons
    previous_buttons = 0
    sectors = bd.sectors()
    lines = bd.lines()
    selected_sector = sectors[0] if sectors else None
    selected_line = lines[0] if lines else None
    if selected_sector:
        bd.log(
            f"world: sector={selected_sector.index} tags={selected_sector.tags} "
            f"floor={selected_sector.floor_height} ceiling={selected_sector.ceiling_height}"
        )
    if selected_line:
        bd.log(
            f"world: line={selected_line.index} special={selected_line.special} "
            f"args={selected_line.args}"
        )
    try:
        bd.hud_text("USER2: pulse light | USER3: raise floor | USER4: trigger line",
                    id=9, x=0.5, y=0.12, color="gold", hold=4.0, fade=1.0)
        if selected_sector and selected_line:
            bd.hud_text(f"Demo targets: sector {selected_sector.index} / "
                        f"line {selected_line.index}",
                        id=8, x=0.5, y=0.19, color="cyan", hold=4.0, fade=1.0)
    except RuntimeError:
        pass


@bd.on("pre_tick")
def world_controls(event):
    global previous_buttons
    player = bd.player()
    if player is None:
        return
    pressed = player.buttons & ~previous_buttons
    previous_buttons = player.buttons
    if pressed & bd.BT_USER2:
        pulse_light()
    if pressed & bd.BT_USER3:
        raise_floor()
    if pressed & bd.BT_USER4:
        activate_line(player.actor)


@bd.on("map_unload")
def release_world_objects(event):
    global selected_sector, selected_line
    selected_sector = None
    selected_line = None
