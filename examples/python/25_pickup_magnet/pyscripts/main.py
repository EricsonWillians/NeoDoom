"""Pickup magnet: nearby items slide toward you while the magnet is on.

Teaches item detection with `get_flag("SPECIAL")` (the engine's pickup
flag), gentle per-tick velocity steering on live handles, and a small
pooled visual: the nearest magnetized items get a faint ground ring so you
can SEE the pull working. USER3 toggles the magnet; a toast confirms.

The pull respects gravity (vertical velocity is never touched) and never
steals: items still use the native pickup path when you touch them.
"""

import biaseddoom as bd


# --- tunables ---------------------------------------------------------------

MAGNET_RADIUS = 640.0    # items within this range start sliding
PULL_SPEED = 3.0         # horizontal map units per steering tic
STEER_EVERY = 3          # pre_tick interval
RING_POOL = 8            # ground rings for the nearest pulled items
RING_RADIUS = 9.0
RING_COLOR = (120, 200, 255)

# Canvas ids (bd.ui owns >= 900000).
RING_BASE = 720  # 720..727 ring pool

# --- module state ---------------------------------------------------------------

magnet_on = True
prev_toggle_key = False
ring_slots = {}  # live Actor handle -> ring pool slot (0..RING_POOL-1)


def safe_call(func, *args, **kwargs):
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


# --- magnet ----------------------------------------------------------------------

def pullable_items(body):
    """Live pickup-flagged actors in range, nearest first."""
    items = []
    for ref in bd.actor_refs():
        if not (ref.valid and ref.alive):
            continue
        if ref.is_monster or ref.is_player:
            continue
        distance = ref.distance_to(body)
        if distance > MAGNET_RADIUS or distance < 1.0:
            continue
        if not safe_call(ref.get_flag, "SPECIAL"):
            continue
        items.append((distance, ref))
    items.sort(key=lambda pair: pair[0])
    return items


@bd.on("pre_tick", every=STEER_EVERY)
def steer_items(event):
    if not magnet_on:
        return
    body = player_body()
    if body is None:
        return
    items = pullable_items(body)
    update_rings(items)
    for distance, ref in items:
        dx, dy = body.x - ref.x, body.y - ref.y
        flat = (dx * dx + dy * dy) ** 0.5
        if flat < 1.0:
            continue
        step = min(PULL_SPEED, flat)
        safe_call(setattr_velocity, ref,
                  (dx / flat * step, dy / flat * step, ref.velocity_z))


def setattr_velocity(ref, velocity):
    ref.velocity = velocity


# --- ring pool (visual feedback) ---------------------------------------------------

def update_rings(items):
    """Assign the RING_POOL ground rings to the nearest pulled items."""
    nearest = {ref for _, ref in items[:RING_POOL]}
    # Release rings whose item left the pool (picked, died, out of range).
    for ref, slot in list(ring_slots.items()):
        if ref not in nearest or not ref.valid or not ref.alive:
            safe_call(bd.draw_clear, RING_BASE + slot)
            del ring_slots[ref]
    used = set(ring_slots.values())
    for _, ref in items[:RING_POOL]:
        if ref in ring_slots:
            continue
        free = next((slot for slot in range(RING_POOL) if slot not in used),
                    None)
        if free is None:
            break
        ring_slots[ref] = free
        used.add(free)
        safe_call(bd.draw_world_ring, ref, id=RING_BASE + free,
                  radius=RING_RADIUS, color=RING_COLOR, alpha=0.5,
                  offset_z=1.5, segments=12, max_distance=MAGNET_RADIUS)


# --- toggle (USER3) --------------------------------------------------------------------

@bd.on("pre_tick")
def toggle_watch(event):
    global magnet_on, prev_toggle_key
    try:
        player = bd.player(0)
        pressed = bool(player and player.valid and
                       (player.buttons & bd.BT_USER3))
    except RuntimeError:
        return  # world mutating
    if pressed and not prev_toggle_key:
        magnet_on = not magnet_on
        if not magnet_on:
            update_rings([])  # release every ring immediately
        bd.ui.toast(f"MAGNET {'ON' if magnet_on else 'OFF'}",
                    color=bd.ui.theme.accent)
        bd.play_ui_sound("switches/normbutn", volume=0.6)
    prev_toggle_key = pressed


# --- map lifecycle -----------------------------------------------------------------------

@bd.on("map_load")
def map_loaded(event):
    ring_slots.clear()  # ring items vanish with the map's actors


@bd.on("map_unload")
def map_unloaded(event):
    ring_slots.clear()
