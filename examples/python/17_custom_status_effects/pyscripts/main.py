"""Custom monster status effects: Burning (player hits) and Chilled (random)."""

import biaseddoom as bd


BURN_TICK_TICS = 10  # tics between burn damage ticks
BURN_DURATION = 3 * bd.TICRATE  # burn length in tics
BURN_TICK_DAMAGE = 2  # damage per burn tick
BURN_DAMAGE_TYPE = "PythonBurn"  # custom type so burn ticks don't re-ignite
CHILL_INTERVAL = 6 * bd.TICRATE  # tics between random chill waves
CHILL_DURATION = 4 * bd.TICRATE  # chill length in tics
CHILL_SLOW = 0.35  # speed multiplier while chilled

BAR_ID_BASE = 1000  # per-monster world bar ids (slot-counter based)
LABEL_ID_BASE = 100000  # per-monster floating status label ids
ICON_ID_BASE = 200000  # per-monster floating status icon ids
BEAM_ID_BASE = 300000  # per-monster player->monster tether beam ids
# The active-status readout is a bd.ui panel (toolkit-owned ids >= 900000).

BURN_FG = (40, 220, 80)  # burning bar fill (static green override)
BURN_LABEL_COLOR = (255, 140, 20)  # burning label orange
CHILL_FG = (80, 180, 255)  # chilled bar fill (static cyan override)
CHILL_LABEL_COLOR = (140, 210, 255)  # chilled label ice blue

# Status icons: Doom II sprite lumps (verified to exist in doom2.wad),
# rendered as solid-color silhouettes via tint=.
BURN_ICON = "BON1A0"  # health-bonus potion silhouette
BURN_ICON_TINT = (255, 60, 20)  # red-hot silhouette
CHILL_ICON = "ARM1A0"  # armor-bonus helmet silhouette
CHILL_ICON_TINT = (120, 220, 255)  # cyan silhouette
ICON_OFFSET_Z = 46.0  # floats above the status label (label is at 26.0)
ICON_SIZE = 14.0  # map units; the engine auto-scales it by distance
BEAM_ALPHA = 0.8

monsters = {}  # live Actor handle -> {bar, label, burn_task, chill_task, orig_speed}
next_slot = 0  # monotonic per-map id source; handles hash by identity, not TID
picker = bd.rng(0xC011)  # deterministic stream for choosing chill victims
status_panel = None  # bd.ui readout panel, recreated per map
panel_visible = False


def safe_draw(func, *args, **kwargs):
    # Draw calls raise RuntimeError while the world mutates (e.g. map
    # unload) and ValueError for unknown fonts on non-Doom II IWADs.
    try:
        func(*args, **kwargs)
    except (RuntimeError, ValueError):
        pass


def refresh_readout():
    """Top-left bd.ui panel counting active statuses; hidden while both
    counters are zero."""
    global status_panel, panel_visible
    burning = sum(1 for r in monsters.values() if r["burn_task"] is not None)
    chilled = sum(1 for r in monsters.values() if r["chill_task"] is not None)
    if status_panel is None:
        status_panel = bd.ui.panel(x=0.02, y=0.02, w=0.24, title="STATUS")
        panel_visible = True
    status_panel.row("BURNING", str(burning),
                     value_color=BURN_LABEL_COLOR if burning
                     else bd.ui.theme.dim)
    status_panel.row("CHILLED", str(chilled),
                     value_color=CHILL_LABEL_COLOR if chilled
                     else bd.ui.theme.dim)
    if burning or chilled:
        if not panel_visible:
            status_panel.show()
            panel_visible = True
    elif panel_visible:
        status_panel.hide()
        panel_visible = False


def player_actor():
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
        return None  # world mutating (e.g. map unload); skip the beam


def apply_visuals(ref, record):
    """Repaint the bar, label, icon, and tether beam from the live status."""
    if record["burn_task"] is not None:  # burning wins over chilled
        fg, text, color = BURN_FG, "BURNING", BURN_LABEL_COLOR
        icon, tint = BURN_ICON, BURN_ICON_TINT
        seconds = BURN_DURATION / bd.TICRATE
    elif record["chill_task"] is not None:
        fg, text, color = CHILL_FG, "CHILLED", CHILL_LABEL_COLOR
        icon, tint = CHILL_ICON, CHILL_ICON_TINT
        seconds = CHILL_DURATION / bd.TICRATE
    else:
        fg, text, color = None, "", None
        icon, tint, seconds = None, None, 0.0
    # fg=None restores the engine's automatic green/yellow/red gradient.
    safe_draw(bd.draw_world_bar, ref,
              id=record["bar"], offset_z=8.0, width=0.05, height=0.008,
              track="health", fg=fg, max_distance=1500.0, label=True)
    if text:
        # duration= is a self-cleaning backstop: label and icon vanish on
        # their own when the status ends even if the manual clear below is
        # never reached (e.g. an orphaned task). The manual draw_clear in
        # the else branch still handles the burn<->chill handoff, which can
        # happen BEFORE the duration would elapse.
        safe_draw(bd.draw_world_text, ref,
                  id=record["label"], text=text, offset_z=26.0,
                  font="bigfont", color=color, scale=1.0, duration=seconds)
        # Floating silhouette icon above the label: draw_world_texture
        # anchors a sprite lump to the actor (size in map units, scaled by
        # distance; tint renders a solid silhouette).
        safe_draw(bd.draw_world_texture, ref, icon,
                  id=record["icon"], offset_z=ICON_OFFSET_Z, size=ICON_SIZE,
                  tint=tint, max_distance=1500.0, duration=seconds)
        # Tether beam: both endpoints are live Actor handles, so the beam
        # follows the player and the monster per frame with NO
        # re-registration; it makes status ranges/causes readable.
        anchor = player_actor()
        if anchor is not None:
            safe_draw(bd.draw_world_line, anchor, ref,
                      id=record["beam"], color=color, alpha=BEAM_ALPHA)
    else:
        safe_draw(bd.draw_clear, record["label"])
        safe_draw(bd.draw_clear, record["icon"])
        safe_draw(bd.draw_clear, record["beam"])


def ensure_record(ref):
    record = monsters.get(ref)
    if record is not None:
        return record
    global next_slot
    next_slot += 1
    record = {"bar": BAR_ID_BASE + next_slot,
              "label": LABEL_ID_BASE + next_slot,
              "icon": ICON_ID_BASE + next_slot,
              "beam": BEAM_ID_BASE + next_slot,
              "burn_task": None, "chill_task": None, "orig_speed": None}
    monsters[ref] = record
    apply_visuals(ref, record)
    return record


def ignite(ref, record, source):
    """Start (or refresh) Burning: 3s of damage ticks, green tint, label."""
    if record["burn_task"] is not None:
        bd.cancel_task(record["burn_task"])  # re-igniting refreshes the timer
    else:
        bd.play_ui_sound("misc/pistol")  # only on a fresh ignition
    ticks_left = [BURN_DURATION // BURN_TICK_TICS]  # list = mutable closure cell

    def burn_out():
        record["burn_task"] = None
        if ref.valid and ref.alive:
            apply_visuals(ref, record)
        refresh_readout()

    def burn_tick():
        if not ref.valid or not ref.alive:
            burn_out()
            return False  # stop repeating
        ref.damage(
            BURN_TICK_DAMAGE, damage_type=BURN_DAMAGE_TYPE,
            source=source if source is not None and source.valid else None,
        )
        ticks_left[0] -= 1
        if ticks_left[0] <= 0:
            burn_out()
            return False
        return True  # keep repeating

    record["burn_task"] = bd.schedule(
        burn_tick, delay=BURN_TICK_TICS, repeat=BURN_TICK_TICS, map_local=True)
    apply_visuals(ref, record)
    refresh_readout()


def chill(ref, record):
    """Start (or refresh) Chilled: slowed speed, cyan tint, label."""
    if record["chill_task"] is not None:
        bd.cancel_task(record["chill_task"])  # re-chilling refreshes duration
    elif ref.valid:
        # Actor.speed is a writable scalar; slowing it slows A_Chase movement.
        record["orig_speed"] = ref.speed
        ref.speed = ref.speed * CHILL_SLOW

    def thaw():
        record["chill_task"] = None
        if ref.valid and record["orig_speed"] is not None:
            ref.speed = record["orig_speed"]  # restore the captured speed
            record["orig_speed"] = None
            apply_visuals(ref, record)
        refresh_readout()
        return False  # one-shot

    record["chill_task"] = bd.schedule(thaw, delay=CHILL_DURATION, map_local=True)
    apply_visuals(ref, record)
    refresh_readout()


def chill_wave():
    """Repeating wave: chill one random live monster."""
    candidates = [ref for ref in bd.actor_refs()
                  if ref.valid and ref.is_monster and ref.alive]
    if candidates:
        ref = picker.choice(candidates)
        chill(ref, ensure_record(ref))
    return True  # keep repeating


@bd.on("actor_spawned")
def actor_spawned(event):
    ref = event["actor_ref"]
    if ref is not None and ref.valid and ref.is_monster:
        ensure_record(ref)


@bd.on("actor_damaged")
def actor_damaged(event):
    ref = event["actor_ref"]
    if ref is None or not ref.valid or not ref.is_monster or not ref.alive:
        return
    # Ignite only on player-credited damage; burn ticks use BURN_DAMAGE_TYPE
    # so they never re-ignite the monster they are burning.
    source = event.get("source_ref")
    if (
        event.get("damage_type") != BURN_DAMAGE_TYPE
        and source is not None and source.valid and source.is_player
    ):
        ignite(ref, ensure_record(ref), source)


@bd.on("actor_died")
def actor_died(event):
    ref = event["actor_ref"]
    record = monsters.pop(ref, None) if ref is not None else None
    if record is not None:
        if record["burn_task"] is not None:
            bd.cancel_task(record["burn_task"])
        if record["chill_task"] is not None:
            bd.cancel_task(record["chill_task"])
        # Bars/labels/icons hide themselves on death, but a world LINE has
        # no health gate, so clear the tether beam explicitly (otherwise it
        # would keep pointing at the corpse until the actor is destroyed).
        safe_draw(bd.draw_clear, record["beam"])
        refresh_readout()


@bd.on("map_load")
def map_loaded(event):
    global next_slot
    monsters.clear()  # world items from the old map are already purged
    next_slot = 0

    def initial_sweep():
        for ref in bd.actor_refs():
            if ref.valid and ref.is_monster and ref.alive:
                ensure_record(ref)
        refresh_readout()
        return False

    bd.schedule(initial_sweep, delay=1)  # run in-level, not during the load
    bd.schedule(chill_wave, delay=CHILL_INTERVAL, repeat=CHILL_INTERVAL,
                map_local=True)
