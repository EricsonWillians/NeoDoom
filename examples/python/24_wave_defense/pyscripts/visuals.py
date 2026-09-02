"""All screen and world visuals: HUD strip, popups, rarity rings / name
texts / health bars (with id pooling), the compass needle and the
end-of-wave motion tracker.

Anchoring notes (from the display-list renderer): world text/bars anchor
at actor->Top() + offset_z — already the top of the head. Bars grow UP
from their anchor; text hangs DOWN from its own. Health bars are
manual-fraction (track=None, frac=...) because engine health tracking
divides by the CLASS DEFAULT health, which is meaningless for buffed
monsters; redraw-by-id replaces the persistent item.
"""

import math

import biaseddoom as bd

import horde_affixes as affixes
import horde_config as config
import horde_runstate as runstate

# module-local visual state
hud_cache = (None, None)
popup_counter = 0
tracker_cache = None
compass_cache = None
ring_slots = {}        # Actor -> visual slot (rarity ring + name + bar)


def reset_caches():
    global hud_cache, tracker_cache, compass_cache
    hud_cache = (None, None)
    tracker_cache = None
    compass_cache = None


# --- HUD -----------------------------------------------------------------------------

def render_hud():
    """Top-center status lines; redraw only when the text changes."""
    global hud_cache
    remaining = runstate.quota_left + len(runstate.live_defenders())
    main = (f"WAVE {runstate.wave}  ·  LEFT {remaining}  ·  "
            f"SCORE {runstate.score()}") if runstate.wave else ""
    sub = (f"NEXT WAVE IN {runstate.breather_left}"
           if runstate.phase == "breather"
           else (f"BEST WAVE {runstate.best_wave()}"
                 if runstate.best_wave() > 0 else ""))
    if hud_cache == (main, sub):
        return
    hud_cache = (main, sub)
    if main:
        runstate.safe_call(bd.draw_text, main, id=config.HUD_MAIN, x=0.5,
                           y=0.012, height=0.022, color=bd.ui.theme.accent,
                           outline=True, align="center")
    else:
        runstate.safe_call(bd.draw_clear, config.HUD_MAIN)
    if sub:
        color = (bd.ui.theme.good if runstate.phase == "breather"
                 else bd.ui.theme.dim)
        runstate.safe_call(bd.draw_text, sub, id=config.HUD_SUB, x=0.5,
                           y=0.042, height=0.014, color=color, outline=True,
                           align="center")
    else:
        runstate.safe_call(bd.draw_clear, config.HUD_SUB)


def popup(points, color=(255, 205, 70)):
    """Floating score feedback: Doom's fiery bigfont, colored by rarity."""
    global popup_counter
    popup_counter += 1
    stream = bd.rng(popup_counter * 613 + runstate.wave)
    runstate.safe_call(bd.draw_text, f"+{int(points)}",
                       id=config.POPUP_BASE + popup_counter
                       % config.POPUP_SLOTS,
                       x=0.5 + (stream.float() - 0.5) * 0.2,
                       y=0.38 + (stream.float() - 0.5) * 0.12,
                       font="bigfont", color=color, height=0.024,
                       outline=True, align="center", duration=0.9)


# --- rarity visuals: tint denotes membership; overhead stack denotes rarity -------

# Name/bar stacking: world text hangs DOWN from its anchor, bars grow UP
# from theirs, and both anchor at actor->Top() + offset_z. Fixed
# screen-size glyphs out-project any fixed world gap with distance, so
# the name offset is recomputed from the player distance on every redraw.
NAME_GAP_BASE = 2.0
NAME_GAP_PER_UNIT = 0.035            # ~30px of glyphs, focal ~927px


def alloc_ring(ref):
    used = set(ring_slots.values())
    for slot in range(config.DEF_RING_SLOTS):
        if slot not in used:
            ring_slots[ref] = slot
            return slot
    return None


def ring_color(ref):
    record = runstate.defender_info.get(ref)
    if record is None:
        return config.DEF_TINT
    if record["tier"] == 3:
        return config.UNIQUE_COLOR
    return record["prefix"]["color"]


def update_overhead(ref):
    """(Re)draw a rare monster's name + health bar: bar fraction from the
    real buffed max_health, name offset recomputed from the current
    player distance so the two never overlap at any range."""
    record = runstate.defender_info.get(ref)
    if record is None:
        return
    bar = record.get("bar")
    name = record.get("name")
    if bar is None and name is None:
        return
    body = runstate.player_body()
    dist = (math.hypot(ref.x - body.x, ref.y - body.y)
            if body is not None else 0.0)
    if bar is not None:
        bar_id, offset, width, height = bar
        max_health = record.get("max_health", 0)
        if max_health <= 0 and ref.valid and ref.alive:
            # Self-heal any path that forgot to set it: first sight of
            # the undamaged monster defines full.
            max_health = record["max_health"] = max(1, ref.health)
        frac = (min(1.0, max(0.0, ref.health / max_health))
                if max_health > 0 else 0.0)
        runstate.safe_call(bd.draw_world_bar, ref, id=bar_id,
                           offset_z=offset, width=width, height=height,
                           track=None, frac=frac)
    if name is not None:
        name_id, text_height, font = name
        base = bar[1] if bar is not None else 2.0
        offset = base + NAME_GAP_BASE + NAME_GAP_PER_UNIT * dist
        runstate.safe_call(bd.draw_world_text, ref, id=name_id,
                           text=affixes.affix_title(ref.class_name, record),
                           offset_z=offset, color=ring_color(ref),
                           font=font, height=text_height, outline=True)


def draw_rarity_marks(ref):
    """Rarity = tint + overhead name + health bar (no ground rings)."""
    slot = ring_slots.get(ref)
    if slot is None:
        return
    record = runstate.defender_info.get(ref)
    if record is None or record["tier"] < 2:
        return
    record["bar"] = (config.BAR_TEXT_BASE + slot, 2.0, 0.05, 0.006)
    record["name"] = (config.NAME_TEXT_BASE + slot,
                      0.024 if record["tier"] == 3 else 0.02,
                      "smallfont")  # long affix names overflow bigfont
    update_overhead(ref)


def update_health_bar(ref):
    """Alias kept for the damage/sweep call sites: refreshes the whole
    overhead stack (bar fraction + distance-adaptive name offset)."""
    update_overhead(ref)


def free_ring(ref):
    slot = ring_slots.pop(ref, None)
    if slot is not None:
        runstate.safe_call(bd.draw_clear, config.DEF_RING_BASE + slot)
        runstate.safe_call(bd.draw_clear, config.NAME_TEXT_BASE + slot)
        runstate.safe_call(bd.draw_clear, config.BAR_TEXT_BASE + slot)


def free_all_rings():
    for slot in ring_slots.values():
        runstate.safe_call(bd.draw_clear, config.DEF_RING_BASE + slot)
        runstate.safe_call(bd.draw_clear, config.NAME_TEXT_BASE + slot)
        runstate.safe_call(bd.draw_clear, config.BAR_TEXT_BASE + slot)
    ring_slots.clear()


# --- finder aids: compass needle + end-of-wave tracker --------------------------------

def compass(dx, dy):
    """8-way bearing; Doom map coords: +x = E, +y = N."""
    angle = math.degrees(math.atan2(dy, dx))
    dirs = ("E", "NE", "N", "NW", "W", "SW", "S", "SE")
    return dirs[int((angle + 22.5) // 45) % 8]


def update_tracker():
    """Last-few finder as a HUD motion tracker: bearing + distance per
    survivor. Screen-space text only — no through-walls world visuals."""
    global tracker_cache
    live = (runstate.live_defenders()
            if runstate.phase == "fight" and runstate.quota_left <= 0
            else [])
    body = runstate.player_body()
    text = ""
    if 0 < len(live) <= config.TRACKER_REMAINING and body is not None:
        hints = []
        for ref in live:
            dx, dy = ref.x - body.x, ref.y - body.y
            hints.append((math.hypot(dx, dy), compass(dx, dy)))
        hints.sort()
        text = "TRACKER: " + "  ·  ".join(
            f"{direction} {int(distance)}" for distance, direction in hints)
    if text == tracker_cache:
        return
    tracker_cache = text
    if text:
        runstate.safe_call(bd.draw_text, text, id=config.TRACKER_TEXT,
                           x=0.5, y=0.072, height=0.014,
                           color=config.DEF_TINT, outline=True,
                           align="center")
    else:
        runstate.safe_call(bd.draw_clear, config.TRACKER_TEXT)


@bd.on("pre_tick", every=config.COMPASS_EVERY)
def compass_needle(event):
    """Subtle always-on direction cue: a 'v' gliding along a strip under
    the HUD, pointing at the nearest defender relative to the view. When
    it's behind you the needle becomes a gold edge arrow. Redraws only
    when the quantized bearing changes."""
    global compass_cache
    bearing = None
    if runstate.phase == "fight":
        body = runstate.player_body()
        live = runstate.live_defenders()
        if body is not None and live:
            nearest = min(live, key=lambda r: (r.x - body.x) ** 2
                          + (r.y - body.y) ** 2)
            world = math.degrees(math.atan2(nearest.y - body.y,
                                            nearest.x - body.x))
            bearing = int(((world - body.angle + 180) % 360 - 180) // 6) * 6
    if bearing == compass_cache:
        return
    compass_cache = bearing
    if bearing is None:
        runstate.safe_call(bd.draw_clear, config.COMPASS_TEXT)
        return
    if abs(bearing) <= 90:
        marker, color = "v", config.DEF_TINT
        x = 0.5 - bearing / 90 * config.COMPASS_SPREAD
    else:
        # Behind: point the way to turn from the strip's edge.
        marker = "<" if bearing > 0 else ">"
        color = config.UNIQUE_COLOR
        x = (0.5 - config.COMPASS_SPREAD if bearing > 0
             else 0.5 + config.COMPASS_SPREAD)
    runstate.safe_call(bd.draw_text, marker, id=config.COMPASS_TEXT, x=x,
                       y=0.056, height=0.014, color=color, outline=True,
                       align="center")
