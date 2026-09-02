"""Elemental squads: every monster belongs to FIRE, FROST or VENOM.

Teaches the sprite-tint API (`actor.tint`), the damage scalars
(`damage_multiply` / `damage_factor`) and batch application. Each monster
class on the map is assigned a squad from a deterministic map stream, so a
class keeps its color for the whole map: FIRE hits harder, FROST is slow
but sturdy, VENOM has more health. A bd.ui legend panel tracks live counts
per squad. Press USER2 to toggle the tints off and back on (attributes
stay — the tint is pure presentation, and `tint = None` resets it).

Squad application rides `actor_spawned`, so mid-map spawns (pain
elemental summons, script spawns) join their squad immediately.
"""

import biaseddoom as bd


# --- tunables ---------------------------------------------------------------

# mods: dealt = damage_multiply, taken = damage_factor, health/speed mults.
SQUADS = {
    "FIRE":  {"color": (255, 100, 40),
              "mods": {"dealt": 1.4}},
    "FROST": {"color": (90, 200, 255),
              "mods": {"speed": 0.8, "taken": 0.85}},
    "VENOM": {"color": (90, 220, 100),
              "mods": {"health": 1.35}},
}

LEGEND_X = 0.985
LEGEND_Y = 0.02
LEGEND_W = 0.17

# --- module state -------------------------------------------------------------

class_squad = {}   # class name -> squad key (rolled once per map)
tints_on = True
prev_toggle_key = False
legend_panel = None
row_cache = {}       # legend row -> last rendered text


def safe_call(func, *args, **kwargs):
    # Property writes and draws raise RuntimeError while the world mutates
    # (map load/unload); ValueError covers unexpected value problems.
    try:
        return func(*args, **kwargs)
    except (RuntimeError, ValueError):
        return None


# --- squad assignment -----------------------------------------------------------

def map_stream():
    # One deterministic stream per map: sum-of-ords is a stable map salt
    # (str hash() would vary between processes).
    name = bd.current_map() or ""
    return bd.rng(sum(ord(char) for char in name) + 77)


def squad_for(class_name, stream):
    if class_name not in class_squad:
        class_squad[class_name] = stream.choice(list(SQUADS))
    return class_squad[class_name]


def apply_squad(ref, squad_key):
    """Tint + attribute mods for one monster, in a single batch crossing."""
    squad = SQUADS[squad_key]
    mods = squad["mods"]
    ops = []
    if "health" in mods:
        ops.append(("health", ref, max(1, int(ref.health * mods["health"]))))
    if "speed" in mods:
        ops.append(("speed", ref, ref.speed * mods["speed"]))
    if "dealt" in mods:
        ops.append(("damage_multiply", ref, ref.damage_multiply * mods["dealt"]))
    if "taken" in mods:
        ops.append(("damage_factor", ref, ref.damage_factor * mods["taken"]))
    if tints_on:
        color = squad["color"]
        ops.append(("tint", ref, color[0], color[1], color[2]))
    if ops:
        bd.apply_actor_batch(ops)


@bd.on("actor_spawned")
def monster_spawned(event):
    snapshot = event["actor"]
    if snapshot is None or not snapshot["is_monster"] or snapshot["is_player"]:
        return
    ref = event["actor_ref"]
    if ref is None or not ref.valid:
        return
    safe_call(apply_squad, ref, squad_for(snapshot["class_name"], map_stream()))


# --- legend panel ----------------------------------------------------------------

def live_counts():
    counts = {key: 0 for key in SQUADS}
    for ref in bd.actor_refs():
        if ref.valid and ref.is_monster and ref.alive:
            squad = class_squad.get(ref.class_name)
            if squad is not None:
                counts[squad] += 1
    return counts


@bd.on("tick", every=35)  # once per second: counts drift as monsters die
def refresh_legend(event):
    global legend_panel
    if legend_panel is None:
        legend_panel = bd.ui.panel(x=LEGEND_X, y=LEGEND_Y, w=LEGEND_W,
                                   title="SQUADS", anchor="tr")
    for key, squad in SQUADS.items():
        text = str(live_counts()[key])
        if row_cache.get(key) == text:
            continue
        row_cache[key] = text
        legend_panel.row(key, text, value_color=squad["color"])


# --- tint toggle (USER2) ------------------------------------------------------------

def set_all_tints(enabled):
    """Paint or clear every live monster's tint. Attributes never change:
    the tint is presentation only, and None resets to the class default."""
    for ref in bd.actor_refs():
        if not (ref.valid and ref.is_monster and ref.alive):
            continue
        if enabled:
            squad = class_squad.get(ref.class_name)
            if squad is None:
                continue
            color = SQUADS[squad]["color"]
            safe_call(setattr_tint, ref, color)
        else:
            safe_call(setattr_tint, ref, None)


def setattr_tint(ref, color):
    ref.tint = color  # (r, g, b) tuple paints; None resets


@bd.on("pre_tick")
def toggle_watch(event):
    """USER2 edge-toggles all squad tints."""
    global tints_on, prev_toggle_key
    try:
        player = bd.player(0)
        pressed = bool(player and player.valid and
                       (player.buttons & bd.BT_USER2))
    except RuntimeError:
        return  # world mutating
    if pressed and not prev_toggle_key:
        tints_on = not tints_on
        set_all_tints(tints_on)
        bd.ui.toast(f"SQUAD TINTS {'ON' if tints_on else 'OFF'}",
                    color=bd.ui.theme.accent)
        bd.play_ui_sound("switches/normbutn", volume=0.6)
    prev_toggle_key = pressed


# --- map lifecycle -----------------------------------------------------------------

@bd.on("map_load")
def map_loaded(event):
    global class_squad, legend_panel, row_cache
    class_squad = {}  # squads re-roll per map (same map = same squads)
    row_cache = {}
    if legend_panel is not None:
        legend_panel.close()
        legend_panel = None

    def initial_paint():
        # Map-placed monsters spawned during the load already carry their
        # squad mods via actor_spawned; this pass catches anything spawned
        # before tints could be drawn and refreshes the legend.
        set_all_tints(tints_on)
        refresh_legend(None)
        return False  # one-shot

    bd.schedule(initial_paint, delay=1)
