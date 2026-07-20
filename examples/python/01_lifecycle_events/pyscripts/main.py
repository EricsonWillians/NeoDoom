"""Observe the complete embedded-Python lifecycle without changing gameplay."""

import biaseddoom as bd


reported_phases = set()


def on_engine_start(event):
    bd.log(f"lifecycle: engine started with Python API {bd.API_VERSION}")


def on_map_load(event):
    reported_phases.clear()
    bd.log(
        f"lifecycle: loaded {event['map']} "
        f"from_savegame={event['from_savegame']}"
    )


def on_pre_tick(event):
    if "pre" not in reported_phases:
        reported_phases.add("pre")
        bd.log("lifecycle: pre_tick runs before native player thinking")


def on_tick(event):
    if "tick" not in reported_phases:
        reported_phases.add("tick")
        bd.log("lifecycle: tick runs before actor thinkers")


def on_post_tick(event):
    if "post" not in reported_phases:
        reported_phases.add("post")
        bd.log(
            "lifecycle: post_tick runs after world simulation; "
            f"Python used {event['python_time_us']} us so far"
        )


def on_map_unload(event):
    bd.log(f"lifecycle: unloading {event['map']} for {event['next_map']}")


def on_save(event):
    bd.log(f"lifecycle: save callback for {event['map']}")


def on_load(event):
    bd.log(f"lifecycle: restored Python state in {event['map']}")


def on_engine_shutdown(event):
    bd.log("lifecycle: engine shutdown")


@bd.on("player_entered")
def player_entered(event):
    bd.log(
        f"lifecycle: player {event['player_index']} entered; "
        f"from_hub={event['from_hub']}"
    )
