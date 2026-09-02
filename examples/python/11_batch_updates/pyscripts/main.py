"""Animate many live handles with one native batch call per update.

Try it: load a map — a bd.ui panel reports each wave's actors and average cost.
"""

import math

import biaseddoom as bd


MARKER_COUNT = 48
WAVE_TICS = 2 * bd.TICRATE  # one announcement wave every two seconds

markers = []
origins = []
wave = 0
last_calls = 0
last_total_us = 0
batch_panel = None  # bd.ui panel for the wave readout


def create_markers():
    global markers, origins
    player = bd.player()
    pawn = player.actor if player else None
    if pawn is None:
        return False  # scheduled task retries next tic until a pawn exists

    markers = []
    origins = []
    for index in range(MARKER_COUNT):
        angle = index * (2 * math.pi / MARKER_COUNT)
        origin = (
            pawn.x + math.cos(angle) * 128,
            pawn.y + math.sin(angle) * 128,
            pawn.z + 24,
        )
        origins.append(origin)
        markers.append(bd.spawn("PythonBatchMarker", *origin, force=True))
    bd.log(f"batch: created {len(markers)} native handles")
    return False


def batch_stats():
    """(calls, total_us) the profiler accumulated for this tick callback."""
    for callback in bd.profile()["callbacks"]:
        if callback["event"] == "tick":
            return callback["calls"], callback["total_us"]
    return 0, 0


def announce_wave(applied):
    """Show actors mutated and the profiler-measured average batch cost."""
    global wave, last_calls, last_total_us, batch_panel
    wave += 1
    calls, total_us = batch_stats()
    batches = calls - last_calls
    elapsed_us = total_us - last_total_us
    last_calls, last_total_us = calls, total_us
    if batch_panel is None:
        batch_panel = bd.ui.panel(x=0.02, y=0.02, w=0.26, title="BATCH")
    batch_panel.row("ACTORS", str(applied))
    # Average wall time of one update (build operations + one C crossing).
    if batches:
        per_batch_ms = elapsed_us / batches / 1000.0
        batch_panel.row("AVG TIME", f"{per_batch_ms:.2f} ms")
    else:  # first wave: no completed batches since the baseline yet
        batch_panel.row("AVG TIME", "measuring...", value_color=bd.ui.theme.dim)
    batch_panel.row("WAVE", str(wave), flash=True)
    bd.log(f"batch: {applied} actors, wave {wave}")
    bd.play_ui_sound("switches/normbutn", volume=0.5)


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
    if event["level_time"] % WAVE_TICS == 0:
        announce_wave(applied)


@bd.on("map_load")
def map_loaded(event):
    global wave, last_calls, last_total_us
    wave = 0
    last_calls, last_total_us = batch_stats()  # baseline for this map's waves
    try:
        bd.hud_text(
            "Batch demo: 48 handles, one native call per update - watch the BATCH panel",
            id=2, y=0.15, color="cyan", hold=4.0,
        )
    except RuntimeError:
        pass
    bd.schedule(create_markers, delay=1)


@bd.on("map_unload")
def map_unloaded(event):
    markers.clear()
    origins.clear()
