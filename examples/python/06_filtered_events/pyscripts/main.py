"""Use native event filters, priorities, and synchronous actor event handles."""

import biaseddoom as bd


PROBE_TID = 9301
probe = None


@bd.on("actor_spawned", class_name="ZombieMan", tid=PROBE_TID, priority=100)
def probe_spawned(event):
    bd.log(f"events: filtered spawn {event['actor_ref']}")


@bd.on("actor_damaged", class_name="ZombieMan", tid=PROBE_TID)
def probe_damaged(event):
    bd.log(
        f"events: damage={event['damage']} type={event['damage_type']} "
        f"source={event['source_ref']}"
    )


@bd.on("actor_died", class_name="ZombieMan", tid=PROBE_TID)
def probe_died(event):
    bd.log(f"events: filtered death for TID {event['actor_ref'].tid}")


@bd.on("actor_destroyed", tid=PROBE_TID)
def probe_destroyed(event):
    bd.log(f"events: filtered destruction snapshot={event['actor']}")


@bd.on("player_spawned", player=0)
def player_spawned(event):
    bd.log(f"events: player filter matched slot {event['player_index']}")


def create_probe():
    global probe
    player = bd.player()
    pawn = player.actor if player else None
    if pawn is None:
        return False
    probe = bd.spawn("ZombieMan", pawn.x + 160, pawn.y, pawn.z, tid=PROBE_TID, force=True)
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
