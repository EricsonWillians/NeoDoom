"""Spawn and steer a GC-safe live Actor handle for five seconds."""

import math

import biaseddoom as bd


companion = None
spawn_tick = 0


def create_companion():
    global companion, spawn_tick
    player = bd.player()
    pawn = player.actor if player else None
    if pawn is None:
        return False

    radians = math.radians(pawn.angle)
    companion = bd.spawn(
        "ZombieMan",
        pawn.x + math.cos(radians) * 96.0,
        pawn.y + math.sin(radians) * 96.0,
        pawn.z,
        force=True,
    )
    companion.health = 125
    companion.target = pawn
    companion.master = pawn
    companion.args = (1, 2, 3, 4, 5)
    companion.set_flag("FRIENDLY", True)
    spawn_tick = bd.level_time()

    snapshot = companion.snapshot()
    bd.log(
        f"handles: spawned {snapshot['class_name']} at {snapshot['position']} "
        f"valid={companion.valid} friendly={companion.get_flag('FRIENDLY')}"
    )
    return False


def steer_companion():
    if companion is None or not companion.valid:
        return False
    elapsed = bd.level_time() - spawn_tick
    if elapsed >= 5 * bd.TICRATE:
        companion.set_velocity(0, 0, 0)
        bd.log("handles: five-second steering task finished")
        return False

    phase = elapsed / bd.TICRATE
    companion.set_velocity(math.cos(phase) * 2, math.sin(phase) * 2, 0)
    return True


@bd.on("map_load")
def map_loaded(event):
    bd.schedule(create_companion, delay=1)
    bd.schedule(steer_companion, delay=2, repeat=1)


@bd.on("map_unload")
def map_unloaded(event):
    global companion
    companion = None
