"""Call typed, public methods on a custom ZScript actor synchronously."""

import biaseddoom as bd


def run_bridge_example():
    player = bd.player()
    pawn = player.actor if player else None
    if pawn is None:
        return False

    actor = bd.spawn(
        "PythonExampleBridgeActor", pawn.x + 128, pawn.y, pawn.z, force=True
    )
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
    bd.center_message(echoed)
    return False


@bd.on("map_load")
def map_loaded(event):
    bd.schedule(run_bridge_example, delay=1)
