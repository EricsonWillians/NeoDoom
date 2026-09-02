"""Call public ZScript methods on a custom actor synchronously from Python.

Try it: load a map — every bridge call's return value appears on the HUD.
"""

import biaseddoom as bd


results_panel = None  # bd.ui panel listing every bridge result


def show_result(label, value):
    """One panel row per bridge result."""
    global results_panel
    if results_panel is None:
        results_panel = bd.ui.panel(x=0.02, y=0.02, w=0.34,
                                    title="ZSCRIPT BRIDGE")
    results_panel.row(label, value)


def run_bridge_example():
    player = bd.player()
    pawn = player.actor if player else None
    if pawn is None:
        return False  # scheduled task retries next tic until a pawn exists

    actor = bd.spawn(
        "PythonExampleBridgeActor", pawn.x + 128, pawn.y, pawn.z, force=True
    )
    # Typed round-trips: int, Vector3, and Actor in; int/Vector3/Actor/str out.
    health = actor.call_zscript("ConfigureFromPython", 150, (1.0, 2.0, 3.0), pawn)
    velocity = actor.call_zscript("CurrentVelocity")
    owner = actor.call_zscript("CurrentOwner")
    echoed = actor.call_zscript("Echo", "typed bridge works")

    try:
        actor.call_zscript("HiddenMethod")
        hidden_rejected = False
    except PermissionError:
        hidden_rejected = True

    bd.log(
        f"zscript: health={health} velocity={velocity} owner_matches={owner == pawn} "
        f"echo={echoed!r} private_rejected={hidden_rejected}"
    )

    # Show each returned value instead of only logging it.
    shown_velocity = tuple(round(component, 1) for component in velocity)
    show_result("ConfigureFromPython", f"health {health}")
    show_result("CurrentVelocity", str(shown_velocity))
    show_result("CurrentOwner", f"is your pawn -> {owner == pawn}")
    show_result("Echo", repr(echoed))
    show_result("HiddenMethod", f"private call rejected -> {hidden_rejected}")
    bd.play_ui_sound("misc/chat")
    bd.center_message(echoed)
    return False


@bd.on("map_load")
def map_loaded(event):
    bd.ui.toast("Python -> ZScript bridge: returned values incoming...",
                color=bd.ui.theme.accent, duration=3.0)
    bd.schedule(run_bridge_example, delay=1)
