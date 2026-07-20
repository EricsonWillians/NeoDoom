"""Store namespaced JSON-compatible state in savegames and across py_reload."""

import biaseddoom as bd


STATE_KEY = "org.biaseddoom.examples.persistent_state"


def example_state():
    return bd.state[STATE_KEY]


def on_engine_start(event):
    bd.state.setdefault(
        STATE_KEY,
        {
            "maps_visited": 0,
            "seconds_played": 0,
            "history": [],
        },
    )


@bd.on("map_load")
def map_loaded(event):
    state = example_state()
    state["maps_visited"] += 1
    state["history"].append(event["map"])
    state["history"] = state["history"][-8:]
    bd.center_message(
        f"Python visits: {state['maps_visited']}  seconds: {state['seconds_played']}"
    )


@bd.on("tick", every=bd.TICRATE)
def count_seconds(event):
    example_state()["seconds_played"] += 1


def on_save(event):
    bd.log(f"state: serializing {example_state()}")


def on_load(event):
    bd.log(f"state: restored {example_state()}")
