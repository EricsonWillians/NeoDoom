"""Lifecycle event observer with a live on-screen ticker of the last 3 events.

Concept: the complete embedded-Python lifecycle (engine, map, tick phases,
save/load) made visible — bd.log is the ground truth, a bd.ui panel
shows what fired most recently.

Try it: run any map and watch the top-left ticker; then save, load, and exit
the level to see more events fire.
"""

import biaseddoom as bd


TICKER_SIZE = 3  # how many recent events the panel keeps on screen

reported_phases = set()  # tick phases already logged this map (they fire constantly)
recent_events = []  # newest-first event names shown by the ticker
ticker_panel = None  # bd.ui panel, created on the first event


def show_ticker():
    """Refresh the ticker panel. bd.ui panels self-guard their draws with
    RuntimeError and re-render on map_load, so pre-HUD calls are safe."""
    global ticker_panel
    if ticker_panel is None:
        ticker_panel = bd.ui.panel(x=0.02, y=0.02, w=0.30, title="EVENTS")
    for slot in range(TICKER_SIZE):
        name = recent_events[slot] if slot < len(recent_events) else "-"
        # Newest row on top in the accent color; older rows dimmed.
        color = None if slot == 0 else bd.ui.theme.dim
        ticker_panel.row(f"{slot + 1}.", name, value_color=color,
                         flash=slot == 0)


def note(name):
    """Push an event onto the on-screen ticker."""
    recent_events.insert(0, name)
    del recent_events[TICKER_SIZE:]
    show_ticker()


def show_help():
    """First-time guidance; hud_text needs an active status bar, so guard it."""
    try:
        bd.hud_text("Watch the top-left ticker: lifecycle events appear as they fire",
                    id=1, x=0.5, y=0.2, color="cyan", hold=5.0, fade=1.0)
    except RuntimeError:
        pass


def on_engine_start(event):
    bd.log(f"lifecycle: engine started with Python API {bd.API_VERSION}")
    note("engine_start")


def on_map_load(event):
    reported_phases.clear()
    recent_events.clear()
    bd.log(
        f"lifecycle: loaded {event['map']} "
        f"from_savegame={event['from_savegame']}"
    )
    note("map_load")
    show_help()


def on_pre_tick(event):
    if "pre" not in reported_phases:
        reported_phases.add("pre")
        bd.log("lifecycle: pre_tick runs before native player thinking")
        note("pre_tick")


def on_tick(event):
    if "tick" not in reported_phases:
        reported_phases.add("tick")
        bd.log("lifecycle: tick runs before actor thinkers")
        note("tick")


def on_post_tick(event):
    if "post" not in reported_phases:
        reported_phases.add("post")
        bd.log(
            "lifecycle: post_tick runs after world simulation; "
            f"Python used {event['python_time_us']} us so far"
        )
        note("post_tick")


def on_map_unload(event):
    bd.log(f"lifecycle: unloading {event['map']} for {event['next_map']}")
    note("map_unload")


def on_save(event):
    bd.log(f"lifecycle: save callback for {event['map']}")
    note("save")


def on_load(event):
    bd.log(f"lifecycle: restored Python state in {event['map']}")
    note("load")


def on_engine_shutdown(event):
    bd.log("lifecycle: engine shutdown")
    note("engine_shutdown")


@bd.on("player_entered")
def player_entered(event):
    bd.log(
        f"lifecycle: player {event['player_index']} entered; "
        f"from_hub={event['from_hub']}"
    )
    note("player_entered")
