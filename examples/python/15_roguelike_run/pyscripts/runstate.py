"""Run-persistent state (bd.state) and deterministic-stream helpers.

The run survives map changes through bd.state and checkpoint save/load:
on_engine_start installs defaults with setdefault, so a checkpoint load
restores the saved values over the fresh ones.

Pure state layer: this module never imports hud/mutators (one-way
dependency). The HUD detects score changes itself and flashes on increase.
"""

import biaseddoom as bd

import rogue_config as config


# --- accessors ---------------------------------------------------------------

def seed():
    return bd.state.get("run_seed", config.DEFAULT_SEED)


def depth():
    return bd.state.get("maps", 0)


def multiplier():
    return 1.0 + depth() * 0.5


def score():
    return bd.state.get("score", 0)


def level():
    return bd.state.get("level", 1)


def map_salt(map_name):
    # Stable per-map offset so each map re-rolls different, reproducible
    # content from the same run seed (str hash() would vary between runs).
    return sum(ord(char) for char in map_name) if map_name else 0


def map_stream(extra=0):
    """One deterministic stream for the current map/depth: same run, same
    map, same depth always rolls the same content."""
    return bd.rng(seed() + map_salt(bd.current_map()) + depth() + extra)


def monster_stream(spawn_index):
    """Deterministic per-monster stream: the Nth monster spawned on this map
    at this depth always rolls the same affixes."""
    return bd.rng(seed() + map_salt(bd.current_map()) * 31
                  + depth() * 977 + spawn_index * 7919)


def init_defaults():
    # Loading a save restores the old state only after fresh modules
    # initialize, so setdefault keeps saved values.
    bd.state.setdefault("run_seed", config.DEFAULT_SEED)
    for key in ("score", "maps", "kills", "secrets", "deaths", "uniques",
                "best_score", "xp"):
        bd.state.setdefault(key, 0)
    bd.state.setdefault("level", 1)
    # A fresh interpreter with no saved run is a new run; a checkpoint load
    # restores the saved "runs" value over this one, continuing the same run.
    bd.state["runs"] = bd.state.get("runs", 0) + 1


# --- savegame-load detection -----------------------------------------------------
# On a checkpoint reload the map_load handlers must NOT re-apply anything:
# actors are deserialized with mutated stats and packed affixes, and the run
# state (including the rolled mutators) comes back with bd.state. The load
# event fires before map_load, so it arms a one-shot flag that the map setup
# consumes.

_loaded_from_save = False


@bd.on("load")
def save_loaded(event):
    global _loaded_from_save
    _loaded_from_save = True


def consume_load_flag():
    """True exactly once per savegame load (read by the map setup)."""
    global _loaded_from_save
    flag = _loaded_from_save
    _loaded_from_save = False
    return flag


# --- score / XP mutation --------------------------------------------------------

def add_score(points):
    """Add to the persistent run score; returns the new total."""
    points = int(points)
    if points <= 0:
        return score()
    bd.state["score"] = score() + points
    return bd.state["score"]


def xp_target():
    """XP needed to go from the current level to the next."""
    return config.XP_PER_LEVEL * level()


def add_xp(amount):
    """Add XP; returns the number of levels gained (caller announces)."""
    amount = int(amount)
    if amount <= 0:
        return 0
    bd.state["xp"] = bd.state.get("xp", 0) + amount
    gained = 0
    while bd.state["xp"] >= xp_target():
        bd.state["xp"] -= xp_target()
        bd.state["level"] = level() + 1
        gained += 1
    return gained


def player_body():
    """Live handle to the local player's body, or None when not in play."""
    try:
        player = bd.player(0)  # None when slot 0 is not in the game
        if player is None or not player.valid:
            return None
        actor = player.actor
        if actor is None or not actor.valid:
            return None
        return actor
    except RuntimeError:
        return None  # world mutating (e.g. map unload)


def heal_player_full():
    body = player_body()
    if body is None:
        return
    try:
        body.heal(999)
    except RuntimeError:
        pass  # world mutating
