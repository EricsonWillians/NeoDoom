"""Treasure goblin: a rare, fast, gold-tinted zombie that flees the player.

Teaches mid-map spawning with `bd.spawn`, per-tick steering (velocity
writes on a live handle), gold tint + ground ring visuals, kill rewards
and escape timers. A while after the map starts (and again after each
goblin dies or escapes), a TREASURE GOBLIN appears near the player: it
sprints away whenever you get close and escapes if you cannot catch it.
Kill it for a score shower. One self-contained module, toasts only — no
persistent panel chrome.
"""

import math

import biaseddoom as bd


# --- tunables ---------------------------------------------------------------

GOBLIN_CLASS = "Zombieman"
GOBLIN_TITLE = "TREASURE GOBLIN"
GOBLIN_TINT = (255, 205, 55)
GOBLIN_SPEED_MULT = 3.0
GOBLIN_RING_RADIUS = 14.0

FIRST_SPAWN_TICS = bd.TICRATE * 8    # first goblin ~8s into the map
RESPAWN_TICS = bd.TICRATE * 25       # next goblin after a death/escape
ESCAPE_TICS = bd.TICRATE * 30        # untouched for this long -> despawn
FLEE_RADIUS = 512.0                  # start running when the player is near
ESCAPE_RADIUS = 1600.0               # far enough to count as gotten away
SPAWN_DISTANCE = 700.0
SCORE_REWARD = 500

# Canvas ids (bd.ui owns >= 900000).
GOBLIN_RING = 570
GOBLIN_TITLE_ID = 571
POPUP_BASE = 580  # 580..589 round-robin reward popups

# --- module state (map-local) -------------------------------------------------

goblin = None
born_tic = 0        # map time at spawn, for the escape timer
popup_counter = 0
spawn_task = None


def safe_draw(func, *args, **kwargs):
    # Draw calls raise RuntimeError while the world mutates.
    try:
        func(*args, **kwargs)
    except (RuntimeError, ValueError):
        pass


def player_body():
    try:
        player = bd.player(0)
        if player is None or not player.valid:
            return None
        body = player.actor
        return body if body is not None and body.valid else None
    except RuntimeError:
        return None


def goblin_alive():
    return goblin is not None and goblin.valid and goblin.alive


def current_tic():
    # Level time in 35 Hz tics; timers only compare deltas within one map.
    return bd.level_time()


# --- spawning -----------------------------------------------------------------

def spawn_goblin():
    """Spawn the goblin on a ring around the player, facing outward."""
    global goblin, born_tic, spawn_task
    spawn_task = None
    if goblin_alive():
        return False
    body = player_body()
    if body is None:
        return False
    stream = bd.rng(current_tic() + 137)
    angle = stream.float() * 360.0
    radians = math.radians(angle)
    x = body.x + math.cos(radians) * SPAWN_DISTANCE
    y = body.y + math.sin(radians) * SPAWN_DISTANCE
    try:
        ref = bd.spawn(GOBLIN_CLASS, x, y, body.z, angle=angle + 180.0)
    except RuntimeError:
        return False
    if ref is None or not ref.valid:
        return False
    goblin = ref
    born_tic = current_tic()
    bd.apply_actor_batch([
        ("speed", ref, ref.speed * GOBLIN_SPEED_MULT),
        ("tint", ref, GOBLIN_TINT[0], GOBLIN_TINT[1], GOBLIN_TINT[2]),
    ])
    safe_draw(bd.draw_world_ring, ref, id=GOBLIN_RING,
              radius=GOBLIN_RING_RADIUS, color=GOBLIN_TINT, alpha=0.9,
              offset_z=2.0, segments=20)
    safe_draw(bd.draw_world_text, ref, id=GOBLIN_TITLE_ID, text=GOBLIN_TITLE,
              offset_z=12.0, color=GOBLIN_TINT, height=0.016, outline=True)
    bd.ui.toast("A TREASURE GOBLIN APPEARS!", color=GOBLIN_TINT,
                duration=2.0)
    bd.play_ui_sound("misc/chat", volume=0.7)
    bd.log("goblin: spawned")
    return False  # one-shot task


def schedule_spawn(delay):
    global spawn_task
    if spawn_task is None and not goblin_alive():
        spawn_task = bd.schedule(spawn_goblin, delay=delay)


# --- flee / escape steering ----------------------------------------------------

@bd.on("pre_tick", every=5)
def goblin_steering(event):
    """Every 5 tics: run from a close player, escape from a far one."""
    global goblin
    if not goblin_alive():
        return
    body = player_body()
    if body is None:
        return
    dx = goblin.x - body.x
    dy = goblin.y - body.y
    distance = math.hypot(dx, dy)
    if distance < 1.0:
        return
    if distance < FLEE_RADIUS:
        # Sprint directly away, preserving the vertical velocity the
        # physics engine is managing (falls, lifts).
        push = goblin.speed * 1.5
        try:
            goblin.velocity = (dx / distance * push, dy / distance * push,
                               goblin.velocity_z)
        except RuntimeError:
            pass
    elif distance > ESCAPE_RADIUS and \
            current_tic() - born_tic > ESCAPE_TICS:
        # Far away for long enough: it got away with the loot.
        safe_draw(bd.draw_clear, GOBLIN_RING)
        safe_draw(bd.draw_clear, GOBLIN_TITLE_ID)
        try:
            goblin.destroy()
        except RuntimeError:
            pass
        goblin = None
        bd.ui.toast("IT GOT AWAY...", color=bd.ui.theme.dim, duration=1.5)
        bd.log("goblin: escaped")
        schedule_spawn(RESPAWN_TICS)


# --- reward / cleanup -----------------------------------------------------------

def score_shower():
    """A fountain of small +N popups around the crosshair."""
    global popup_counter
    for chunk in range(5):
        popup_counter += 1
        stream = bd.rng(current_tic() + popup_counter * 313)
        x = 0.5 + (stream.float() - 0.5) * 0.3
        y = 0.35 + (stream.float() - 0.5) * 0.2
        safe_draw(bd.draw_text, f"+{SCORE_REWARD // 5}",
                  id=POPUP_BASE + popup_counter % 10, x=x, y=y,
                  color=GOBLIN_TINT, height=0.02, outline=True,
                  align="center", duration=1.0 + chunk * 0.15)


@bd.on("actor_died")
def goblin_died(event):
    global goblin
    if goblin is None:
        return
    ref = event["actor_ref"]
    if ref is None:
        return
    try:
        if ref != goblin:  # handles compare by underlying actor
            return
    except RuntimeError:
        return
    goblin = None  # ring/title world items vanish with the actor
    score_shower()
    bd.ui.toast(f"GOBLIN LOOT  +{SCORE_REWARD}", color=GOBLIN_TINT,
                duration=2.0)
    bd.screen_flash(255, 220, 120, 0.2)
    bd.play_ui_sound("misc/secret")
    bd.log("goblin: looted")
    schedule_spawn(RESPAWN_TICS)


# --- map lifecycle -----------------------------------------------------------------

@bd.on("map_load")
def map_loaded(event):
    global goblin, spawn_task
    goblin = None
    if spawn_task is not None:
        try:
            bd.cancel_task(spawn_task)
        except RuntimeError:
            pass
        spawn_task = None
    safe_draw(bd.draw_clear, GOBLIN_RING)
    safe_draw(bd.draw_clear, GOBLIN_TITLE_ID)
    schedule_spawn(FIRST_SPAWN_TICS)
