"""Patrol routes: idle monsters orbit their home post on a visible circuit.

Teaches waypoint-style steering on live actor handles, `draw_world_line`
path visualization, and clean handoff between custom steering and native
AI: a patroller only orbits while it has NO live player target — the
moment it spots you, native Doom AI takes over; when it loses you again,
it drifts back to its circuit.

Up to MAX_PATROLLERS monsters per map get an octagonal circuit drawn
around their post (static world-line segments, dimmed) and orbit it at
walk speed. Everything is map-local and cleans itself up on death or map
change.
"""

import math

import biaseddoom as bd


# --- tunables ---------------------------------------------------------------

MAX_PATROLLERS = 6          # circuits per map (visual clarity)
ORBIT_RADIUS = 64.0         # map units around the home post
ORBIT_SPEED = 2.0           # map units per steering tic
STEER_EVERY = 2             # pre_tick interval
ANGLE_STEP = 0.055          # radians per steering tic
CIRCUIT_SIDES = 8           # octagon segments per circuit visualization
CIRCUIT_COLOR = (110, 130, 160)
CIRCUIT_ALPHA = 0.55

# Canvas ids (bd.ui owns >= 900000): per patroller slot, ids
# CIRCUIT_BASE + slot*8 .. +7 (one per octagon segment).
CIRCUIT_BASE = 660

# --- module state (map-local) -------------------------------------------------

patrollers = {}  # live Actor handle -> {"home": (x, y, z), "angle": float, "slot": int}
free_slots = []  # recycled circuit slots of dead patrollers


def safe_call(func, *args, **kwargs):
    # Actor writes and draws raise RuntimeError while the world mutates.
    try:
        return func(*args, **kwargs)
    except (RuntimeError, ValueError):
        return None


# --- circuit visualization -----------------------------------------------------

def draw_circuit(home, slot):
    """Static octagon of world lines around the patroller's home post."""
    hx, hy, hz = home
    z = hz + 2.0
    points = [(hx + math.cos(i * 2 * math.pi / CIRCUIT_SIDES) * ORBIT_RADIUS,
               hy + math.sin(i * 2 * math.pi / CIRCUIT_SIDES) * ORBIT_RADIUS, z)
              for i in range(CIRCUIT_SIDES)]
    for side in range(CIRCUIT_SIDES):
        safe_call(bd.draw_world_line, points[side],
                  points[(side + 1) % CIRCUIT_SIDES],
                  id=CIRCUIT_BASE + slot * CIRCUIT_SIDES + side,
                  color=CIRCUIT_COLOR, alpha=CIRCUIT_ALPHA)


def clear_circuit(slot):
    for side in range(CIRCUIT_SIDES):
        safe_call(bd.draw_clear, CIRCUIT_BASE + slot * CIRCUIT_SIDES + side)


# --- patrol management -----------------------------------------------------------

def add_patroller(ref):
    """Register one idle monster with a home post at its current spot."""
    if ref in patrollers or len(patrollers) >= MAX_PATROLLERS:
        return
    slot = free_slots.pop() if free_slots else len(patrollers)
    patrollers[ref] = {"home": (ref.x, ref.y, ref.z), "angle": 0.0,
                       "slot": slot}
    draw_circuit(patrollers[ref]["home"], slot)


def drop_patroller(ref):
    entry = patrollers.pop(ref, None)
    if entry is not None:
        clear_circuit(entry["slot"])
        free_slots.append(entry["slot"])


def is_hunting(ref):
    """True when native AI has a live player target — patrol must yield."""
    try:
        target = ref.target
        return target is not None and target.valid and target.alive
    except RuntimeError:
        return True  # world mutating: stay hands-off


@bd.on("pre_tick", every=STEER_EVERY)
def steer(event):
    """Advance each idle patroller along its circuit. Velocity-based, so
    engine collision (walls, stairs) keeps working; the vertical component
    is left to the physics engine."""
    for ref, entry in list(patrollers.items()):
        if not ref.valid or not ref.alive:
            drop_patroller(ref)
            continue
        if is_hunting(ref):
            continue
        entry["angle"] += ANGLE_STEP
        hx, hy, hz = entry["home"]
        tx = hx + math.cos(entry["angle"]) * ORBIT_RADIUS
        ty = hy + math.sin(entry["angle"]) * ORBIT_RADIUS
        dx, dy = tx - ref.x, ty - ref.y
        distance = math.hypot(dx, dy)
        if distance < 4.0:
            continue
        step = min(ORBIT_SPEED, distance)
        safe_call(setattr_velocity, ref,
                  (dx / distance * step, dy / distance * step,
                   ref.velocity_z))


def setattr_velocity(ref, velocity):
    ref.velocity = velocity


@bd.on("actor_died")
def patroller_died(event):
    ref = event["actor_ref"]
    if ref is not None:
        drop_patroller(ref)


@bd.on("map_load")
def map_loaded(event):
    global patrollers, free_slots
    for entry in patrollers.values():
        clear_circuit(entry["slot"])
    patrollers = {}
    free_slots = []

    def enlist():
        # One tic in: actor refs are valid, the HUD can draw.
        for ref in bd.actor_refs():
            if len(patrollers) >= MAX_PATROLLERS:
                break
            if ref.valid and ref.is_monster and ref.alive:
                add_patroller(ref)
        count = len(patrollers)
        if count:
            bd.ui.toast(f"{count} PATROL CIRCUITS ACTIVE",
                        color=bd.ui.theme.accent, duration=1.5)
        return False  # one-shot

    bd.schedule(enlist, delay=1)
