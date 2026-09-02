"""Filtered events: class_name/tid/priority filters on one tagged probe monster.

Concept: @bd.on filters run natively, before Python is called — only events
matching class_name/tid/player ever reach your callback.

Try it: run any map — a probe zombie with a labeled health bar spawns nearby;
it is damaged, killed, and destroyed on a schedule. Shoot it yourself to fire
the actor_damaged filter early.
"""

import biaseddoom as bd


PROBE_TID = 9301  # the TID every actor filter below matches on
BAR_ID = 10  # display-list id for the probe's world health bar
HELP_HUD_ID = 2

probe = None


def status(text, color=None):
    """Show which filtered event fired last; the console keeps full detail."""
    bd.log(f"events: {text}")
    bd.ui.toast(f"filtered: {text}",
                color=bd.ui.theme.accent if color is None else color, y=0.66)


# Filter: only ZombieMan spawns carrying TID 9301 reach this callback,
# and priority=100 makes it run before lower-priority spawn handlers.
@bd.on("actor_spawned", class_name="ZombieMan", tid=PROBE_TID, priority=100)
def probe_spawned(event):
    ref = event["actor_ref"]
    status(f"actor_spawned tid={ref.tid} (priority 100)")
    # Label the probe so you can FIND it: a world bar with the actor's tag
    # drawn above it; it follows the actor and hides once it dies.
    try:
        bd.draw_world_bar(ref, id=BAR_ID, offset_z=8.0, track="health",
                          label=True, label_color="gold", max_distance=2048.0)
    except (RuntimeError, ValueError):
        pass


# Filter: same class_name/tid pair — other zombies' damage never calls this.
@bd.on("actor_damaged", class_name="ZombieMan", tid=PROBE_TID)
def probe_damaged(event):
    status(
        f"actor_damaged damage={event['damage']} type={event['damage_type']}",
        color=bd.ui.theme.warn,
    )


@bd.on("actor_died", class_name="ZombieMan", tid=PROBE_TID)
def probe_died(event):
    status(f"actor_died tid={event['actor_ref'].tid}", color=bd.ui.theme.bad)


# Filter: tid only — any class with TID 9301 would match.
@bd.on("actor_destroyed", tid=PROBE_TID)
def probe_destroyed(event):
    status(f"actor_destroyed snapshot={event['actor']}", color=bd.ui.theme.gold)


# Filter: player slot only — unrelated to the probe, shown for contrast.
@bd.on("player_spawned", player=0)
def player_spawned(event):
    status(f"player_spawned slot={event['player_index']}", color=bd.ui.theme.good)


def create_probe():
    global probe
    player = bd.player()
    pawn = player.actor if player else None
    if pawn is None:
        return False
    probe = bd.spawn("ZombieMan", pawn.x + 160, pawn.y, pawn.z,
                     tid=PROBE_TID, force=True)
    # Synchronous dispatch: this damage call fires probe_damaged before
    # the next line runs — watch the console order.
    probe.damage(7, damage_type="EventExample", source=pawn)

    def kill_probe():
        if probe and probe.valid:
            probe.damage(10000, damage_type="EventExample", source=pawn)
            bd.schedule(destroy_probe, delay=bd.TICRATE)

    bd.schedule(kill_probe, delay=2 * bd.TICRATE)
    return False


def destroy_probe():
    if probe and probe.valid:
        probe.destroy()


@bd.on("map_load")
def map_loaded(event):
    bd.schedule(create_probe, delay=1)
    try:
        bd.hud_text("Find the labeled probe zombie (TID 9301) — shoot it!",
                    id=HELP_HUD_ID, x=0.5, y=0.2, color="cyan", hold=5.0, fade=1.0)
    except RuntimeError:
        pass
