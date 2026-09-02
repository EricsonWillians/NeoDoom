"""Boss encounter: the map's toughest monster becomes THE WARDEN.

Teaches mid-fight actor upgrades through the batch API (scale, health,
speed, damage_multiply), the gold `bd.draw_world_ring` aura, a
screen-space boss health bar built from draw_rect/draw_frame/draw_text
with resolution-independent `height=` text, and phase scripting: the boss
hastens at 66% health and rages at 33%, summoning Imp reinforcements with
`bd.spawn`. On a monster-free map a BaronOfHell is spawned in front of the
player instead, so the encounter works everywhere.
"""

import math

import biaseddoom as bd


# --- tunables ---------------------------------------------------------------

BOSS_NAME = "THE WARDEN"
BOSS_CLASS = "BaronOfHell"  # spawned fallback when the map has no monsters
BOSS_TINT = (175, 60, 200)  # purple
BOSS_HEALTH_MULT = 4.0
BOSS_SCALE = 1.5
BOSS_DEALT = 1.5            # damage_multiply at pull
BOSS_RING_RADIUS = 30.0
PHASES = ((0.66, "hastens"), (0.33, "rages"))  # health frac -> phase name
MINION_CLASS = "DoomImp"
MINIONS_PER_PHASE = 2

# Canvas ids (bd.ui owns >= 900000).
BAR_BG = 550
BAR_FILL = 551
BAR_FRAME = 552
BAR_NAME = 553
BOSS_RING = 556
BOSS_TITLE = 557

# --- module state -------------------------------------------------------------

boss = None          # live Actor handle
boss_max_health = 1
phase_index = 0      # next phase threshold to cross
bar_cache = None     # last rendered (frac, name) to skip unchanged redraws


def safe_draw(func, *args, **kwargs):
    # Draw calls raise RuntimeError while the world mutates.
    try:
        func(*args, **kwargs)
    except (RuntimeError, ValueError):
        pass


def boss_alive():
    return boss is not None and boss.valid and boss.alive


# --- boss creation ---------------------------------------------------------------

def make_boss(ref):
    """Upgrade an existing or freshly spawned actor into the boss."""
    global boss, boss_max_health, phase_index
    boss = ref
    phase_index = 0
    bd.apply_actor_batch([
        ("scale", ref, BOSS_SCALE),
        ("health", ref, int(ref.health * BOSS_HEALTH_MULT)),
        ("damage_multiply", ref, ref.damage_multiply * BOSS_DEALT),
        ("tint", ref, BOSS_TINT[0], BOSS_TINT[1], BOSS_TINT[2]),
    ])
    boss_max_health = max(1, ref.health)
    safe_draw(bd.draw_world_ring, ref, id=BOSS_RING, radius=BOSS_RING_RADIUS,
              color=(255, 200, 40), alpha=0.9, offset_z=2.0, segments=28)
    safe_draw(bd.draw_world_text, ref, id=BOSS_TITLE, text=BOSS_NAME,
              offset_z=30.0, color=(255, 200, 40), height=0.022,
              outline=True)
    bd.ui.announce(BOSS_NAME, subtitle="an ancient evil stirs",
                   color=BOSS_TINT, duration=2.5)
    bd.log(f"boss: {ref.class_name} upgraded, hp {boss_max_health}")


def spawn_fallback_boss():
    """Monster-free map: conjure a Baron in front of the player."""
    try:
        player = bd.player(0)
        body = player.actor if player is not None and player.valid else None
    except RuntimeError:
        body = None
    if body is None:
        return None
    radians = math.radians(body.angle)
    x = body.x + math.cos(radians) * 400.0
    y = body.y + math.sin(radians) * 400.0
    try:
        return bd.spawn(BOSS_CLASS, x, y, body.z, angle=body.angle + 180.0)
    except RuntimeError:
        return None


@bd.on("map_load")
def map_loaded(event):
    global boss, bar_cache
    boss = None
    bar_cache = None

    def setup():
        # One tic in: actor refs are valid and the HUD exists.
        candidates = [ref for ref in bd.actor_refs()
                      if ref.valid and ref.is_monster and ref.alive]
        if candidates:
            make_boss(max(candidates, key=lambda ref: ref.health))
        else:
            ref = spawn_fallback_boss()
            if ref is not None and ref.valid:
                make_boss(ref)
        return False  # one-shot

    bd.schedule(setup, delay=1)


# --- boss bar (screen-space, top center) --------------------------------------------

BAR_X, BAR_Y, BAR_W, BAR_H = 0.30, 0.035, 0.40, 0.016


def draw_bar(frac):
    """Boss health bar: name above a framed gradient fill. Only redraws
    when the visible fraction actually changes."""
    global bar_cache
    key = round(frac, 3)
    if bar_cache == key:
        return
    bar_cache = key
    safe_draw(bd.draw_text, BOSS_NAME, id=BAR_NAME, x=0.5, y=BAR_Y - 0.024,
              height=0.018, color=(255, 200, 40), outline=True,
              align="center")
    safe_draw(bd.draw_rect, id=BAR_BG, x=BAR_X, y=BAR_Y, w=BAR_W, h=BAR_H,
              color=(10, 8, 16), alpha=0.8)
    if frac > 0.0:
        safe_draw(bd.draw_rect, id=BAR_FILL, x=BAR_X, y=BAR_Y,
                  w=BAR_W * frac, h=BAR_H, color=(200, 40, 40),
                  color2=(120, 10, 30), alpha=0.95)
    else:
        safe_draw(bd.draw_clear, BAR_FILL)
    safe_draw(bd.draw_frame, id=BAR_FRAME, x=BAR_X, y=BAR_Y, w=BAR_W,
              h=BAR_H, color=(255, 200, 40), thickness=2, alpha=0.9)


def clear_bar():
    global bar_cache
    bar_cache = None
    for canvas_id in (BAR_BG, BAR_FILL, BAR_FRAME, BAR_NAME):
        safe_draw(bd.draw_clear, canvas_id)


# --- phases ---------------------------------------------------------------------------

def summon_minions():
    if not boss_alive():
        return
    for slot in range(MINIONS_PER_PHASE):
        angle = slot * 180.0 + 90.0
        try:
            ref = bd.spawn(MINION_CLASS, boss.x + 64.0 * (slot * 2 - 1),
                           boss.y, boss.z, angle=angle, force=True)
            if ref is not None and ref.valid:
                ref.target = None  # lets the imp acquire the player natively
        except RuntimeError:
            pass


@bd.on("pre_tick")
def phase_watch(event):
    """Poll the boss health fraction once per tic; cross a threshold ->
    announce, buff and summon. One actor, so this is cheap."""
    global phase_index
    if not boss_alive():
        return
    frac = boss.health / boss_max_health
    draw_bar(frac)
    if phase_index >= len(PHASES) or frac > PHASES[phase_index][0]:
        return
    threshold, name = PHASES[phase_index]
    phase_index += 1
    if name == "hastens":
        bd.apply_actor_batch([("speed", boss, boss.speed * 1.4)])
    else:
        bd.apply_actor_batch(
            [("damage_multiply", boss, boss.damage_multiply * 1.8)])
    summon_minions()
    bd.ui.announce(f"{BOSS_NAME} {name.upper()}",
                   subtitle="the air grows heavy",
                   color=BOSS_TINT, duration=2.0)
    bd.play_ui_sound("misc/secret")


@bd.on("actor_died")
def boss_died(event):
    global boss
    if boss is None:
        return
    ref = event["actor_ref"]
    if ref is None:
        return
    try:
        if ref != boss:  # handles compare by underlying actor
            return
    except RuntimeError:
        return
    boss = None
    clear_bar()
    bd.ui.announce(f"{BOSS_NAME} FALLS", subtitle="the map breathes again",
                   color=(255, 200, 40), duration=3.0)
    bd.screen_flash(255, 220, 120, 0.35)
    bd.play_ui_sound("misc/pkup")
