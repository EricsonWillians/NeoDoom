"""JRPG combat text: damage numbers that JUMP out of the monster.

Every hit you land launches a number on a parabolic arc with a scale pop,
a lateral scatter and a fade-out; severity tiers add words and color
(`POW!`, `CRITICAL!`, `DEVASTATED!`), and the killing blow plays out over
the corpse — the lethal hit is the payoff, after all.

Teaches the `actor_damaged` payload and professional pooling as before,
plus the animation toolkit: `offset_x/y/z` world offsets, per-tier fonts,
`alpha` fades and `height=` size changes driven by re-registering pooled
canvas ids every tic (reusing an id replaces the item in place), with
`duration=` as the backstop expiry.

Only player-caused damage is shown — infighting would flood the screen.
"""

import math

import biaseddoom as bd


# --- severity tiers ---------------------------------------------------------------
# (min damage, word, font, height, color). Checked from the top down.

TIERS = (
    (120, "DEVASTATED!", "bigfont", 0.038, (255, 205, 70)),
    (70, "CRITICAL!", "bigfont", 0.030, (255, 80, 50)),
    (35, "POW!", "smallfont", 0.024, (255, 150, 60)),
    (12, None, "smallfont", 0.020, (255, 225, 130)),
    (0, None, "smallfont", 0.016, (235, 235, 230)),
)

FLASH_ALPHA = 0.18  # subtle screen accent on DEVASTATED hits

# --- motion --------------------------------------------------------------------------

LIFETIME = 30          # tics (~0.85s)
LAUNCH_V = 1.9         # initial upward world units/tic
GRAVITY = 0.12         # downward pull per tic^2
DRIFT = 0.55           # lateral scatter per tic (world units)
POP_TICS = 5           # scale pop-in window
POP_SCALE = 1.45       # initial size multiplier
FADE_TICS = 8          # alpha fade-out window at the end
BASE_Z = 14.0          # launch height above the victim's top

POOL_BASE = 610   # canvas ids 610..657 round-robin (bd.ui owns >= 900000)
POOL_SIZE = 48

# --- module state ---------------------------------------------------------------------

pool_next = 0
pool = {}  # canvas slot -> animation state (see launch())


def safe_draw(func, *args, **kwargs):
    try:
        func(*args, **kwargs)
    except (RuntimeError, ValueError):
        pass


def tier_for(damage):
    for minimum, word, font, height, color in TIERS:
        if damage >= minimum:
            return {"word": word, "font": font, "height": height,
                    "color": color, "devastating": minimum >= 120}
    return TIERS[-1]


def text_for(entry):
    amount = entry["text"]
    word = entry["tier"]["word"]
    return f"{word}\n{amount}" if word else amount


def render(entry, slot):
    """(Re)draw one pool entry at its current animation state."""
    tier = entry["tier"]
    t = entry["t"]
    # Parabolic jump: up fast, gravity pulls back down.
    z = entry["base_z"] + LAUNCH_V * t - GRAVITY * t * t
    # Scale pop: oversized at launch, settling to 1.0.
    pop = 1.0 + (POP_SCALE - 1.0) * max(0.0, 1.0 - t / POP_TICS)
    # Fade-out over the last FADE_TICS.
    alpha = min(1.0, (LIFETIME - t) / FADE_TICS)
    safe_draw(bd.draw_world_text, entry["ref"], id=slot,
              text=text_for(entry), font=tier["font"],
              offset_x=entry["x"], offset_y=entry["y"], offset_z=z,
              color=tier["color"], height=tier["height"] * pop,
              alpha=alpha, outline=True, max_distance=1600.0,
              duration=(LIFETIME - t) / bd.TICRATE)


@bd.on("actor_damaged")
def damage_done(event):
    global pool_next
    source = event["source_ref"]
    if source is None or not source.valid or not source.is_player:
        return
    victim = event["actor_ref"]
    if victim is None or not victim.valid or victim.is_player:
        return
    amount = event["damage"]
    if amount <= 0:
        return
    # Random lateral launch direction from a small per-hit stream.
    stream = bd.rng(amount * 31 + pool_next * 7)
    angle = stream.float() * 2 * math.pi
    slot = POOL_BASE + pool_next % POOL_SIZE
    pool_next += 1
    tier = tier_for(amount)
    entry = {"ref": victim, "t": 0, "tier": tier, "text": str(amount),
             "base_z": BASE_Z, "x": math.cos(angle) * 2.0,
             "y": math.sin(angle) * 2.0,
             "dx": math.cos(angle) * DRIFT, "dy": math.sin(angle) * DRIFT}
    pool[slot] = entry
    render(entry, slot)
    if tier["devastating"]:
        bd.screen_flash(255, 210, 100, FLASH_ALPHA)
        bd.play_ui_sound("misc/pkup", volume=0.5)


@bd.on("pre_tick")
def animate(event):
    """Advance every live number one tic along its arc."""
    for slot, entry in list(pool.items()):
        entry["t"] += 1
        if entry["t"] >= LIFETIME:
            del pool[slot]  # the display item expires via duration=
            continue
        entry["x"] += entry["dx"]
        entry["y"] += entry["dy"]
        render(entry, slot)


@bd.on("map_unload")
def map_unloaded(event):
    pool.clear()
