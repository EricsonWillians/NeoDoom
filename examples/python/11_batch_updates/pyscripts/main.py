"""Animate many live handles with one native batch call per update."""

import math

import biaseddoom as bd


markers = []
origins = []


def create_markers():
    global markers, origins
    player = bd.player()
    pawn = player.actor if player else None
    if pawn is None:
        return False

    markers = []
    origins = []
    for index in range(48):
        angle = index * (2 * math.pi / 48)
        origin = (
            pawn.x + math.cos(angle) * 128,
            pawn.y + math.sin(angle) * 128,
            pawn.z + 24,
        )
        origins.append(origin)
        markers.append(bd.spawn("PythonBatchMarker", *origin, force=True))
    bd.log(f"batch: created {len(markers)} native handles")
    return False


@bd.on("tick", every=2)
def animate_markers(event):
    if not markers:
        return
    phase = event["level_time"] / 12.0
    operations = []
    for index, (marker, origin) in enumerate(zip(markers, origins)):
        if marker.valid:
            operations.append(
                (
                    "position",
                    marker,
                    origin[0],
                    origin[1],
                    origin[2] + math.sin(phase + index * 0.25) * 16,
                )
            )
    applied = bd.apply_actor_batch(operations)
    if event["level_time"] % (5 * bd.TICRATE) == 0:
        profile = bd.profile()
        maximum = max(
            (
                callback["max_us"]
                for callback in profile["callbacks"]
                if callback["event"] == "tick"
            ),
            default=0,
        )
        bd.log(f"batch: applied={applied} tick_max_us={maximum}")


@bd.on("map_load")
def map_loaded(event):
    bd.schedule(create_markers, delay=1)


@bd.on("map_unload")
def map_unloaded(event):
    markers.clear()
    origins.clear()
