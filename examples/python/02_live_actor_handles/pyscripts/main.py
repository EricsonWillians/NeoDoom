"""Live Actor handles: a friendly ZombieMan whose label and health update live.

Concept: bd.spawn returns a GC-safe live Actor handle; retaining it lets you
read and steer the actor for as long as it stays valid.

Try it: run any map — a FRIEND zombie spawns in front of you and circles for
five seconds; shoot it and watch the health toast update on the HUD.
"""

import math

import biaseddoom as bd


FRIEND_LABEL_ID = 10  # display-list id for the floating world label
HELP_HUD_ID = 2
POLL_TICS = bd.TICRATE // 2  # health poll rate (twice a second)

companion = None  # the retained live handle
spawn_tick = 0
last_health = None  # last reported health, for change detection


def say(text, **kwargs):
    """hud_text needs an active status bar; guard it like the draw_* calls."""
    try:
        bd.hud_text(text, **kwargs)
    except RuntimeError:
        pass


def show_friend_label():
    """Anchor a floating label to the live handle; it follows the actor,
    hides while dead, and vanishes when the actor is destroyed."""
    try:
        bd.draw_world_text(companion, id=FRIEND_LABEL_ID, text="FRIEND",
                           offset_z=8.0, color="green", scale=0.9)
    except (RuntimeError, ValueError):
        pass


def create_companion():
    global companion, spawn_tick, last_health
    player = bd.player()
    pawn = player.actor if player else None
    if pawn is None:
        return False

    radians = math.radians(pawn.angle)
    try:
        companion = bd.spawn(  # bd.spawn returns the live handle we retain
            "ZombieMan",
            pawn.x + math.cos(radians) * 96.0,
            pawn.y + math.sin(radians) * 96.0,
            pawn.z,
            force=True,
        )
    except RuntimeError:
        bd.ui.toast("Friend spawn failed (no room?) — reload the map to retry",
                    color=bd.ui.theme.bad)
        return False

    companion.health = 125  # live field write through the handle
    companion.target = pawn  # live relationship: the zombie chases you...
    companion.master = pawn  # ...but FRIENDLY + master keep it on your side
    companion.args = (1, 2, 3, 4, 5)
    companion.set_flag("FRIENDLY", True)
    spawn_tick = bd.level_time()
    last_health = companion.health

    snapshot = companion.snapshot()  # serialization-friendly copy of the handle
    bd.log(
        f"handles: spawned {snapshot['class_name']} at {snapshot['position']} "
        f"valid={companion.valid} friendly={companion.get_flag('FRIENDLY')}"
    )
    show_friend_label()
    bd.ui.toast(f"FRIEND HP: {last_health}", color=bd.ui.theme.good, y=0.62)
    return False


def poll_health():
    """Slow repeating task proving the handle stays live: whenever the zombie's
    health changes (shoot it!), the HUD line updates."""
    global last_health
    if companion is None:
        return False
    if not companion.valid:  # handles go invalid when the actor dies/destroys
        bd.ui.toast("Your friend was destroyed. RIP.", color=bd.ui.theme.bad,
                    y=0.62)
        return False
    if companion.health != last_health:
        old_health = last_health
        last_health = companion.health
        # Green when the friend healed, red when it took a hit.
        color = (bd.ui.theme.good if last_health > old_health
                 else bd.ui.theme.bad)
        bd.ui.toast(f"FRIEND HP: {last_health}", color=color, y=0.62)
        bd.log(f"handles: companion health changed to {last_health}")
    return True


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
    bd.schedule(poll_health, delay=POLL_TICS, repeat=POLL_TICS)
    say("A FRIEND zombie spawns in front of you — shoot it and watch its health",
        id=HELP_HUD_ID, x=0.5, y=0.2, color="cyan", hold=5.0, fade=1.0)


@bd.on("map_unload")
def map_unloaded(event):
    global companion, last_health
    companion = None  # release the handle; it must never outlive its map
    last_health = None
