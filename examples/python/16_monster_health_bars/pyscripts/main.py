"""World-anchored monster health bars with names, gradient, and alerts."""

import biaseddoom as bd


CRITICAL_FRACTION = 0.30  # health fraction that flags a monster as critical
FLASH_TICS = 6  # tics per critical-flash phase

BAR_ID_BASE = 1000  # per-monster world bar ids (slot-counter based)
HP_LABEL_ID_BASE = 100000  # per-monster floating hp-text ids
CRIT_LABEL_ID_BASE = 200000  # per-monster 'CRITICAL!' flash label ids

CRITICAL_FLASH_A = (230, 40, 30)  # critical flash phase A (hot red)
CRITICAL_FLASH_B = (255, 200, 60)  # critical flash phase B (amber)
HP_LABEL_COLOR = (220, 220, 220)  # hp text light gray

monsters = {}  # live Actor handle -> {bar, hp, max_health, critical}
next_slot = 0  # monotonic per-map id source; handles hash by identity, not TID
flash_phase = False


def safe_draw(func, *args, **kwargs):
    # Draw calls are display-list registrations: safe before the HUD exists,
    # but they raise RuntimeError while the world mutates (e.g. map unload).
    try:
        func(*args, **kwargs)
    except (RuntimeError, ValueError):
        pass


def set_bar(ref, record, fg=None):
    # Re-calling draw_world_bar with the same id REPLACES the item, which is
    # how bars are recolored; track="health" re-reads health every frame.
    # fg=None means NO explicit fill, so the engine applies its own
    # green/yellow/red health gradient per frame. label=True draws the
    # actor's GetTag() name above the bar; label_scale=2.0 keeps the name
    # clearly readable at distance. Border, wall occlusion (occlude=True),
    # and the distance fade are all built in.
    safe_draw(
        bd.draw_world_bar, ref,
        id=record["bar"], offset_z=10.0, width=0.05, height=0.008,
        track="health", fg=fg, max_distance=1500.0,
        label=True, label_scale=2.0,
    )


def set_hp_label(ref, record):
    # The bar's built-in label only renders GetTag(), so current/max HP is
    # a second world-text item floating just below the bar.
    hp = max(0, ref.health)
    safe_draw(
        bd.draw_world_text, ref,
        id=record["hp"], text="hp: {}/{}".format(hp, record["max_health"]),
        offset_z=2.0, font="smallfont", color=HP_LABEL_COLOR, scale=2.0,
        max_distance=1500.0,
    )


def ensure_bar(ref):
    record = monsters.get(ref)
    if record is not None:
        return record
    global next_slot
    next_slot += 1
    record = {
        "bar": BAR_ID_BASE + next_slot,
        "hp": HP_LABEL_ID_BASE + next_slot,
        "crit": CRIT_LABEL_ID_BASE + next_slot,
        "max_health": max(1, ref.health),  # captured at creation: the baseline
        "critical": False,
    }
    monsters[ref] = record
    set_bar(ref, record)
    set_hp_label(ref, record)
    return record


@bd.on("actor_spawned")
def actor_spawned(event):
    ref = event["actor_ref"]
    if ref is not None and ref.valid and ref.is_monster:
        ensure_bar(ref)


@bd.on("actor_damaged")
def actor_damaged(event):
    ref = event["actor_ref"]
    if ref is None or not ref.valid or not ref.is_monster:
        return
    record = ensure_bar(ref)
    set_hp_label(ref, record)  # hp text only changes when the monster does
    if ref.alive and ref.health <= CRITICAL_FRACTION * record["max_health"]:
        record["critical"] = True
        set_bar(ref, record, CRITICAL_FLASH_A)  # flasher alternates it


def flash_critical():
    """Repeating flasher: toggles every critical bar's color."""
    global flash_phase
    flash_phase = not flash_phase
    for ref, record in list(monsters.items()):
        if not ref.valid:
            del monsters[ref]  # purge handles invalidated mid-map
            continue
        if not ref.alive:
            record["critical"] = False  # corpse: stop flashing it
            continue
        fraction = ref.health / record["max_health"]
        if record["critical"] and fraction > CRITICAL_FRACTION:
            record["critical"] = False  # healed (rare): restore the gradient
            set_bar(ref, record)
        elif record["critical"]:
            set_bar(ref, record,
                    CRITICAL_FLASH_A if flash_phase else CRITICAL_FLASH_B)
            if flash_phase:
                # The 'CRITICAL!' label is registered only on the bright
                # phase and given duration=FLASH_TICS so it auto-expires
                # exactly when the phase ends -- no manual draw_clear, no
                # cleanup task; healing or death simply stops re-registering
                # it and the last one vanishes on its own within 6 tics.
                safe_draw(
                    bd.draw_world_text, ref,
                    id=record["crit"], text="CRITICAL!", offset_z=24.0,
                    font="smallfont", color=CRITICAL_FLASH_A, scale=2.0,
                    max_distance=1500.0, duration=FLASH_TICS / bd.TICRATE,
                )
    return True  # keep repeating


@bd.on("map_load")
def map_loaded(event):
    global next_slot, flash_phase
    monsters.clear()  # world items from the old map are already purged
    next_slot = 0
    flash_phase = False

    def initial_sweep():
        for ref in bd.actor_refs():
            if ref.valid and ref.is_monster and ref.alive:
                ensure_bar(ref)
        return False

    # Delay so the sweep runs in-level, not during the load itself.
    bd.schedule(initial_sweep, delay=1)
    # Map-local tasks auto-cancel on unload and never duplicate.
    bd.schedule(flash_critical, delay=FLASH_TICS, repeat=FLASH_TICS, map_local=True)
