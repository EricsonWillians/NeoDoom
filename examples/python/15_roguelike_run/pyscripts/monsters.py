"""Monster affixes in the world: rolling, application, uniques, visuals,
kill scoring and XP.

Two application paths share one record format (affixes.py):

1. actor_spawned — fires for map-placed and mid-map spawns. The monster
   rolls affixes from a deterministic per-spawn stream, the record is
   packed into args[4] (survives checkpoints) and attributes are applied
   immediately. Visuals are deferred: world drawing is impossible during
   level load, so a one-shot task registers them a couple of tics later.

2. map_load rescan — after a checkpoint reload actors are deserialized,
   not spawned, so actor_spawned never fires. Attributes (health, speed,
   ...) are serialized with the actor; only the idempotent tint and the
   in-memory canvas visuals need re-application here.

Elites are gone; the per-map crown now creates UNIQUES (tier 3): two
prefixes + a suffix, x3 health, a gold ground ring and x5 XP.
"""

import biaseddoom as bd

import rogue_config as config
import rogue_runstate as runstate
import rogue_affixes as affixes
import rogue_mutators as mutators
import rogue_hud as hud
import rogue_playerfx as playerfx


spawn_counter = 0   # deterministic per-map spawn index (module state)
tracked = {}        # live Actor handle -> canvas slot (map-local)
visual_slot = 0


# --- rolling -------------------------------------------------------------------

def roll_record(stream):
    """Decide one spawned monster's affixes from its deterministic stream."""
    chance = (config.AFFIX_CHANCE_BASE
              + config.AFFIX_CHANCE_PER_DEPTH * runstate.depth())
    if stream.float() >= chance:
        return None
    p1 = stream.choice(range(len(affixes.PREFIXES)))
    suffix = (stream.choice(range(len(affixes.SUFFIXES)))
              if stream.float() < config.SUFFIX_CHANCE else None)
    return {"tier": 2 if suffix is not None else 1,
            "p1": p1, "p2": None, "s": suffix}


def roll_unique(stream, base_record):
    """Upgrade a monster's record to a unique: keep its prefix, add a
    distinct second prefix, guarantee a suffix."""
    prefixes = list(range(len(affixes.PREFIXES)))
    p1 = base_record["p1"] if base_record is not None else None
    if p1 is None:
        p1 = stream.choice(prefixes)
    p2 = stream.choice([i for i in prefixes if i != p1])
    suffix = (base_record["s"] if base_record is not None else None)
    if suffix is None:
        suffix = stream.choice(range(len(affixes.SUFFIXES)))
    return {"tier": 3, "p1": p1, "p2": p2, "s": suffix}


# --- attribute application --------------------------------------------------------

def attribute_ops(ref, mods, health_extra=1.0):
    """Batch ops applying multiplicative mods to the live actor."""
    ops = []
    health_mult = mods.get("health", 1.0) * health_extra
    if health_mult != 1.0:
        ops.append(("health", ref, max(1, int(ref.health * health_mult))))
    if "speed" in mods:
        ops.append(("speed", ref, ref.speed * mods["speed"]))
    if "dealt" in mods:
        ops.append(("damage_multiply", ref,
                    ref.damage_multiply * mods["dealt"]))
    if "taken" in mods:
        ops.append(("damage_factor", ref,
                    ref.damage_factor * mods["taken"]))
    if "scale" in mods:
        ops.append(("scale", ref, ref.scale_x * mods["scale"]))
    if "alpha" in mods:
        ops.append(("alpha", ref, ref.alpha * mods["alpha"]))
    return ops


def tint_op(ref, record):
    if record["tier"] == 3:
        color = config.UNIQUE_COLOR
    else:
        color = affixes.color(record)
    return ("tint", ref, color[0], color[1], color[2])


def apply_record(ref, record, previous=None, health_extra=1.0):
    """Apply a record's attributes. When `previous` is given (unique
    upgrade), only the mods' ratio is applied so nothing compounds."""
    mods = affixes.combined_mods(record)
    if previous is not None:
        old = affixes.combined_mods(previous)
        mods = {key: mods.get(key, 1.0) / old.get(key, 1.0)
                for key in set(mods) | set(old)}
    ops = attribute_ops(ref, mods, health_extra)
    ops.append(tint_op(ref, record))
    if ops:
        bd.apply_actor_batch(ops)


# --- world visuals ------------------------------------------------------------------

def display_color(record):
    return config.UNIQUE_COLOR if record["tier"] == 3 else affixes.color(record)


def register_visuals(ref, record):
    """Ground ring + health bar (champion/unique) and the overhead title.
    World items hide themselves while the actor is dead and vanish when it
    is destroyed, so no per-frame upkeep is needed."""
    base = config.MONSTER_VISUAL_BASE + tracked[ref] * 3
    tier = record["tier"]
    color = display_color(record)
    if tier >= 2:
        radius = (config.UNIQUE_RING_RADIUS if tier == 3
                  else config.CHAMPION_RING_RADIUS)
        hud.safe_draw(bd.draw_world_ring, ref, id=base, radius=radius,
                      color=color, alpha=config.RING_ALPHA, offset_z=2.0,
                      segments=24)
        hud.safe_draw(bd.draw_world_bar, ref, id=base + 1, offset_z=8.0,
                      width=0.06, height=0.008, fg=color)
    # Resolution-independent title sizes (height=); bigger per tier.
    text_z = 26.0 if tier >= 2 else 12.0
    title_h = (config.TITLE_H_UNIQUE if tier == 3 else
               config.TITLE_H_CHAMPION if tier == 2 else
               config.TITLE_H_AFFIXED)
    hud.safe_draw(bd.draw_world_text, ref, id=base + 2,
                  text=affixes.title(ref.class_name, record),
                  offset_z=text_z, color=color, height=title_h,
                  outline=True)


def track(ref, record):
    global visual_slot
    if ref in tracked:
        return
    visual_slot += 1
    tracked[ref] = visual_slot
    register_visuals(ref, record)


def untrack(ref):
    tracked.pop(ref, None)


# --- spawn / load paths --------------------------------------------------------------

@bd.on("actor_spawned")
def monster_spawned(event):
    """Roll affixes for every spawned monster (map-placed and mid-map)."""
    global spawn_counter
    snapshot = event["actor"]
    if snapshot is None or not snapshot["is_monster"] or snapshot["is_player"]:
        return
    ref = event["actor_ref"]
    if ref is None or not ref.valid:
        return
    if affixes.read(ref) is not None:
        return  # already rolled (engine re-dispatch, resurrect edge cases)
    spawn_counter += 1
    record = roll_record(runstate.monster_stream(spawn_counter))
    if record is None:
        return
    affixes.write(ref, record)
    apply_record(ref, record)

    def late_visuals(ref=ref, record=record):
        # World drawing is impossible during level load; a 2-tic delay lands
        # us in-level for mid-map spawns (load-time spawns are covered by the
        # map_load rescan below). Re-reading the record is deliberate: the
        # unique crowning may have upgraded it in the meantime.
        if ref.valid and ref.alive:
            fresh = affixes.read(ref)
            if fresh is not None:
                track(ref, fresh)
        return False  # one-shot

    try:
        bd.schedule(late_visuals, delay=2)
    except RuntimeError:
        pass


def rescan():
    """Register visuals + re-apply the (idempotent) tint for every live
    monster carrying a packed affix record. Covers checkpoint reloads and
    load-time spawns whose deferred visuals were dropped."""
    for ref in mutators.live_monsters():
        record = affixes.read(ref)
        if record is None:
            continue
        bd.apply_actor_batch([tint_op(ref, record)])
        track(ref, record)


def crown_uniques(stream, count):
    """Pick up to `count` distinct live monsters and crown them uniques:
    record upgraded to tier 3, x3 health, gold ring/bar/title, x5 XP."""
    candidates = [ref for ref in mutators.live_monsters()
                  if (affixes.read(ref) or {"tier": 0})["tier"] < 3]
    for _ in range(min(count, len(candidates))):  # safe on empty maps
        ref = stream.choice(candidates)
        candidates.remove(ref)
        previous = affixes.read(ref)
        record = roll_unique(stream, previous)
        affixes.write(ref, record)
        apply_record(ref, record, previous=previous,
                     health_extra=config.UNIQUE_HEALTH_MULT)
        track(ref, record)
        bd.log(f"run: unique {affixes.title(ref.class_name, record)} crowned")


@bd.on("map_load")
def map_loaded(event):
    global spawn_counter, tracked, visual_slot
    spawn_counter = 0
    tracked = {}
    visual_slot = 0

    def setup_map():
        # Runs in-level, one tic after load: actor_refs are valid and the
        # HUD exists, so draws cannot be swallowed.
        if runstate.consume_load_flag():
            # Checkpoint reload: actors were deserialized with mutated stats,
            # tints and packed affixes; the rolled mutators ride along in the
            # saved run state. Re-applying anything would compound it, so
            # only restore module state and the in-memory visuals.
            mutators.active = list(bd.state.get("mutators", []))
            rescan()
            hud.update(force=True)
            return False
        stream = runstate.map_stream()
        rolled = mutators.roll(stream)
        bd.state["mutators"] = rolled  # survives the checkpoint save
        rescan()
        crown_uniques(stream, config.UNIQUES_PER_MAP + (
            config.UNIQUE_PER_ELITE_HUNT if "elite_hunt" in rolled else 0))
        bd.state["maps"] = runstate.depth() + 1  # depth++ after 1 tic
        hud.update(force=True)
        mutators.reveal(rolled)
        return False  # one-shot

    bd.schedule(setup_map, delay=1)


# --- kill scoring / XP -------------------------------------------------------------------

def level_up(levels):
    runstate.heal_player_full()
    runstate.add_score(config.SCORE_PER_LEVEL * levels)
    playerfx.level_up_fx()
    bd.ui.announce(f"LEVEL {runstate.level()}",
                   subtitle="power surges through you",
                   color=bd.ui.theme.good, duration=2.0)


@bd.on("actor_died")
def actor_died(event):
    victim = event["actor"]  # snapshot dict of the dying actor
    if victim is None or victim["is_player"] or not victim["is_monster"]:
        return
    ref = event["actor_ref"]
    record = (affixes.read(ref)
              if ref is not None and ref.valid else None)
    untrack(ref)
    is_unique = record is not None and record["tier"] == 3
    if is_unique:
        # A unique's bounty is a world event: paid regardless of the killer.
        bd.state["uniques"] = bd.state.get("uniques", 0) + 1
        gained_xp = int(config.SCORE_PER_KILL * config.UNIQUE_XP_MULT
                        * affixes.xp_multiplier(record)
                        * runstate.multiplier())
        runstate.add_score(int(gained_xp / config.UNIQUE_XP_MULT))
        levels = runstate.add_xp(gained_xp)
        hud.popup(gained_xp, suffix=" XP", color=config.UNIQUE_COLOR)
        hud.safe_draw(bd.hud_text, f"UNIQUE SLAIN!  +{gained_xp} XP",
                      id=config.HUD_ANNOUNCE, x=0.5, y=0.2, color="gold")
        bd.play_ui_sound("misc/secret")
        if levels:
            level_up(levels)
    # The engine points the dying actor's target at its killer just before
    # this event fires; a live player target means a player-credited kill.
    killer = ref.target if ref is not None and ref.valid else None
    if killer is None or not killer.valid or not killer.is_player:
        hud.update()
        return
    bd.state["kills"] = bd.state.get("kills", 0) + 1
    if is_unique:
        hud.update()
        return  # unique bounty already paid above
    xp_mult = affixes.xp_multiplier(record) if record is not None else 1.0
    runstate.add_score(int(config.SCORE_PER_KILL * runstate.multiplier()))
    gained_xp = int(config.SCORE_PER_KILL * xp_mult * runstate.multiplier())
    levels = runstate.add_xp(gained_xp)
    color = display_color(record) if record is not None else None
    hud.popup(gained_xp, suffix=" XP", color=color)
    if levels:
        level_up(levels)
    if "vampire" in mutators.active:
        body = runstate.player_body()
        if body is not None:
            try:
                body.heal(2)
                playerfx.heal_proc_fx()
            except RuntimeError:
                pass
    hud.update()
