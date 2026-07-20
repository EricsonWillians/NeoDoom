"""Inspect and mutate Sector/Line handles through interactive controls."""

import biaseddoom as bd


selected_sector = None
selected_line = None
previous_buttons = 0


def pulse_light():
    if selected_sector is None:
        return False
    original = selected_sector.light
    selected_sector.light = max(0, original - 64)

    def restore():
        if selected_sector is not None:
            selected_sector.light = original

    bd.schedule(restore, delay=bd.TICRATE)
    bd.log(f"world: sector {selected_sector.index} light {original} -> {selected_sector.light}")
    return False


def raise_floor():
    if selected_sector is None:
        return False
    destination = selected_sector.floor_height + 8.0
    steps = 0

    def move_step():
        nonlocal steps
        steps += 1
        selected_sector.move_floor(destination, speed=0.5)
        return abs(selected_sector.floor_height - destination) > 0.01 and steps < 64

    bd.schedule(move_step, delay=1, repeat=1)
    return False


@bd.on("map_load")
def select_world_objects(event):
    global selected_sector, selected_line
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
    bd.center_message("User2: pulse light  User3: raise floor  User4: activate line")


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
    if pressed & bd.BT_USER4 and selected_line:
        result = selected_line.activate(activator=player.actor)
        bd.log(f"world: line activation returned {result}")


@bd.on("map_unload")
def release_world_objects(event):
    global selected_sector, selected_line
    selected_sector = None
    selected_line = None
