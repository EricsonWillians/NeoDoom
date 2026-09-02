"""Persistent session stats in bd.state: they survive maps, saves, and death.

Try it: watch the top-right panel while changing maps, saving/loading, and dying.
"""

import biaseddoom as bd


STATE_KEY = "org.biaseddoom.examples.persistent_state"

session_panel = None  # bd.ui panel in the top-right corner, created lazily


def example_state():
    """Return this example's namespaced slice of the shared state dict."""
    return bd.state[STATE_KEY]


def on_engine_start(event):
    # Reload restores old state only after fresh modules initialize, so use
    # setdefault: first run creates the record, reload keeps the old one.
    bd.state.setdefault(
        STATE_KEY,
        {
            "maps_visited": 0,
            "seconds_played": 0,
            "deaths": 0,
            "history": [],
        },
    )


def update_panel(flash_deaths=False):
    """(Re)draw the session panel from bd.state; bd.ui panels self-guard
    their draws, so pre-HUD calls are safe and re-rendered on map_load."""
    global session_panel
    if session_panel is None:
        session_panel = bd.ui.panel(x=0.98, y=0.02, w=0.30, title="SESSION",
                                    anchor="tr")
    state = example_state()
    history = state["history"]
    last_map = history[-2] if len(history) > 1 else "-"
    session_panel.row("VISITS", str(state["maps_visited"]))
    session_panel.row("SECONDS", f"{state['seconds_played']}s")
    session_panel.row("DEATHS", str(state["deaths"]),
                      value_color=bd.ui.theme.bad if flash_deaths else None,
                      flash=flash_deaths)
    session_panel.row("LAST MAP", last_map)


@bd.on("map_load")
def map_loaded(event):
    state = example_state()
    state["maps_visited"] += 1
    state["history"].append(event["map"])
    state["history"] = state["history"][-8:]
    update_panel()
    try:
        bd.hud_text(
            "bd.state panel top-right: it survives maps, saves, and death",
            id=1, y=0.25, color="cyan", hold=4.0,
        )
    except RuntimeError:
        pass


@bd.on("tick", every=bd.TICRATE)
def count_seconds(event):
    example_state()["seconds_played"] += 1
    update_panel()  # slow 1 Hz refresh keeps the persistent panel live


@bd.on("player_died")
def death_does_not_erase(event):
    state = example_state()
    state["deaths"] += 1
    update_panel(flash_deaths=True)  # the death is already counted before any respawn
    bd.play_ui_sound("misc/secret")
    bd.ui.announce("STATE SURVIVES DEATH", color=bd.ui.theme.bad, duration=2.0)


def on_save(event):
    bd.log(f"state: serializing {example_state()}")


def on_load(event):
    bd.log(f"state: restored {example_state()}")
