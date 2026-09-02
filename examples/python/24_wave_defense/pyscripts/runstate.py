"""Run state for the endless horde: every mutable global lives here, so
sibling modules share one namespace (`runstate.wave += 1` works anywhere).
Also home to the small guards and bd.state accessors everyone needs.
"""

import biaseddoom as bd

import horde_config as config

# --- attempt state (reset on death and on every map load) -------------------------

wave = 0
quota_left = 0         # defenders still allowed to spawn this wave
defenders = set()      # live Actor handles of the current wave
defender_info = {}     # Actor -> affix record (tier, mods, score, max_health)
uniques_pending = 0    # uniques still to crown from this wave's spawns
bosses = set()         # live boss handles (twin bosses in the deep game)
mutators = ()          # wave-scoped mutator dicts (name/mods/quota)
phase = "idle"         # "idle" | "fight" | "breather"
breather_left = 0
supply_count = 0       # drops so far (drives the medkit/armor rotation)

# --- spawn pool (map-local) ---------------------------------------------------------

spawn_points = []      # map-authored (x, y, z, angle) pool; None = retired
point_strikes = {}     # pool index -> strikes from stuck spawns
spawn_order = []       # shuffled pool indices for the current wave
spawn_cursor = 0
fit_failures = 0       # ring fallback: consecutive failed spawn passes
last_positions = {}    # Actor -> (x, y, z) at the previous sweep

# --- session entropy ----------------------------------------------------------------

entropy = 1            # kill timing feeds it, so runs diverge
kill_counter = 0       # defender kills this attempt (paces rocket/cell grants)


# --- bd.state accessors -------------------------------------------------------------

def init_defaults():
    # Conventional startup defaults; setdefault survives save loads.
    bd.state.setdefault("hd_score", 0)
    bd.state.setdefault("hd_best_wave", 0)
    bd.state.setdefault("hd_best_score", 0)


def score():
    return bd.state.get("hd_score", 0)


def best_wave():
    return bd.state.get("hd_best_wave", 0)


def best_score():
    return bd.state.get("hd_best_score", 0)


def add_score(points):
    bd.state["hd_score"] = score() + points


def save_best_score():
    if score() > best_score():
        bd.state["hd_best_score"] = score()


# --- small guards -----------------------------------------------------------------

def safe_call(func, *args, **kwargs):
    # Spawns/writes/draws raise RuntimeError while the world mutates.
    try:
        return func(*args, **kwargs)
    except (RuntimeError, ValueError):
        return None


def player_body():
    try:
        player = bd.player(0)
        if player is None or not player.valid:
            return None
        body = player.actor
        return body if body is not None and body.valid else None
    except RuntimeError:
        return None


def live_defenders():
    return [ref for ref in defenders if ref.valid and ref.alive]


def boss_index():
    return max(1, wave // config.BOSS_EVERY)


def fresh_stream(salt):
    """Entropy-mixed RNG stream: same map + same wave no longer means the
    same rolls. Kill timing feeds the entropy, so runs diverge."""
    global entropy
    entropy = (entropy * 31 + 1) & 0x7FFFFFFF
    return bd.rng((salt * 2654435761 + entropy) % 2147483647)


def feed_entropy(value):
    global entropy
    entropy = (entropy + value) & 0x7FFFFFFF
