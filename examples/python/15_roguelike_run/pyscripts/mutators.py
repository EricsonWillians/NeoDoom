"""Map mutators: depth-scaled run modifiers rolled once per map.

Pure logic layer (config, runstate). The map orchestration in monsters.py
calls roll() + apply() and reveal(); hud.py reads `active` for its context
line. Mutations that touch actor health go through bd.apply_actor_batch.
"""

import biaseddoom as bd

import rogue_config as config
import rogue_runstate as runstate


active = []  # mutator keys rolled for the current map


def live_monsters():
    return [ref for ref in bd.actor_refs()
            if ref.valid and ref.is_monster and ref.alive]


def scale_monster_health(factor):
    operations = [("health", ref, max(1, int(ref.health * factor)))
                  for ref in live_monsters()]
    if operations:  # smoke-harness safe: empty maps apply nothing
        bd.apply_actor_batch(operations)
    return len(operations)


def apply_glass_cannon(stream, depth):
    factor = max(0.10, 0.50 - 0.05 * depth)
    bd.log(f"run: glass_cannon x{factor:.2f} on "
           f"{scale_monster_health(factor)} monsters")


def apply_swift(stream, depth):
    scale = 1.15 + 0.05 * depth
    try:
        bd.set_timescale(scale)  # restored to 1.0 on map_unload
        bd.log(f"run: swift timescale {scale:.2f}")
    except RuntimeError:
        pass


def apply_fragile(stream, depth):
    bd.log(f"run: fragile x0.75 on {scale_monster_health(0.75)} monsters")


def apply_tank(stream, depth):
    factor = 1.5 + 0.25 * depth
    bd.log(f"run: tank x{factor:.2f} on {scale_monster_health(factor)} monsters")


def apply_flavor(stream, depth):
    # vampire / rich_pickup / elite_hunt take effect in the kill, pickup,
    # and unique-crowning paths respectively; nothing to mutate on load.
    pass


MUTATORS = {
    "glass_cannon": {"title": "GLASS CANNON", "color": (255, 90, 60),
                     "sub": "monsters weaken as depth rises",
                     "apply": apply_glass_cannon},
    "swift": {"title": "SWIFT", "color": (90, 255, 140),
              "sub": "time flows faster", "apply": apply_swift},
    "fragile": {"title": "FRAGILE", "color": (255, 170, 70),
                "sub": "monsters are brittle", "apply": apply_fragile},
    "tank": {"title": "TANK", "color": (100, 150, 255),
             "sub": "monsters harden as depth rises", "apply": apply_tank},
    "vampire": {"title": "VAMPIRE", "color": (210, 60, 90),
                "sub": "your kills feed you 2 hp", "apply": apply_flavor},
    "rich_pickup": {"title": "RICH PICKUP", "color": (255, 200, 80),
                    "sub": "rare pickups score double", "apply": apply_flavor},
    "elite_hunt": {"title": "ELITE HUNT", "color": (255, 240, 160),
                   "sub": "an extra unique stalks this map",
                   "apply": apply_flavor},
}


def roll(stream):
    """Roll this map's mutators from the given stream and remember them."""
    global active
    first = stream.choice(list(MUTATORS))
    rolled = [first]
    if runstate.depth() >= config.SECOND_MUTATOR_MIN_DEPTH and \
            stream.float() < config.SECOND_MUTATOR_CHANCE:
        rolled.append(stream.choice([k for k in MUTATORS if k != first]))
    active = rolled
    for key in rolled:
        MUTATORS[key]["apply"](stream, runstate.depth())
    return rolled


def reveal(keys):
    """Center-screen mutator reveal via bd.ui.announce (big outlined title,
    subtitle, screen-fade accent and sound built in), plus a 3-tic
    timescale dip (skipped when swift is active)."""
    first = MUTATORS[keys[0]]
    title = " + ".join(MUTATORS[key]["title"] for key in keys)
    subtitle = " + ".join(MUTATORS[key]["sub"] for key in keys)
    bd.ui.announce(title, subtitle=subtitle, color=first["color"],
                   duration=2.5)
    if "swift" in keys:
        return  # dipping on top of swift would fight the mutator
    try:
        previous = bd.get_timescale()
        bd.set_timescale(0.85)

        def restore():
            try:
                bd.set_timescale(previous)
            except RuntimeError:
                pass
            return False  # one-shot

        bd.schedule(restore, delay=3)
    except RuntimeError:
        pass


@bd.on("map_unload")
def map_unloaded(event):
    # i_timescale is a CVar and survives map changes; undo a swift mutator.
    # Gameplay mutations are blocked while the world unloads, hence the guard.
    global active
    active = []
    try:
        bd.set_timescale(1.0)
    except RuntimeError:
        pass
