"""Monsters: the commandeered spawn pool, defender spawning and marking,
unique crowning, the trickle and straggler-sweep tasks, summon enlistment,
and every actor event handler (died / destroyed / revived / damaged /
spawned).
"""

import math

import biaseddoom as bd

import horde_affixes as affixes
import horde_config as config
import horde_loot as loot
import horde_runstate as runstate
import horde_visuals as visuals

# horde_waves is imported lazily inside the kill handlers: waves imports
# this module (pool geometry), so a top-level import would cycle before
# both are registered in sys.modules.


# --- spawn pool ------------------------------------------------------------------

def ring_position(body, stream, min_radius, max_radius):
    angle = stream.float() * 2 * math.pi
    radius = min_radius + stream.float() * (max_radius - min_radius)
    return (body.x + math.cos(angle) * radius,
            body.y + math.sin(angle) * radius, body.z,
            math.degrees(angle) + 180.0)


def unlocked_classes():
    classes = []
    for minimum, tier in config.CLASS_TIERS:
        if runstate.wave >= minimum:
            classes.extend(tier)
    return classes


def pool_size():
    return sum(1 for point in runstate.spawn_points if point is not None)


def next_pool_point(body):
    """Next spawn point from the wave's shuffled order, preferring points
    outside the player's face range. None when the pool is empty."""
    fallback = None
    for _ in range(len(runstate.spawn_order)):
        index = runstate.spawn_order[
            runstate.spawn_cursor % len(runstate.spawn_order)]
        runstate.spawn_cursor += 1
        point = runstate.spawn_points[index]
        if point is None:      # retired sealed point
            continue
        if fallback is None:
            fallback = point
        dx, dy = point[0] - body.x, point[1] - body.y
        if dx * dx + dy * dy > config.SPAWN_FACE_MIN * config.SPAWN_FACE_MIN:
            return point
    return fallback


def random_pool_point(stream, body):
    """Random live pool point (straggler relocation target)."""
    live = [p for p in runstate.spawn_points if p is not None]
    if not live:
        return None
    far = [p for p in live
           if (p[0] - body.x) ** 2 + (p[1] - body.y) ** 2
           > config.SPAWN_FACE_MIN * config.SPAWN_FACE_MIN]
    return stream.choice(far or live)


def boss_spawn_point(body, rank):
    """Boss entrance: the pool point farthest (rank 0) or second-farthest
    (rank 1) from the player."""
    points = sorted((p for p in runstate.spawn_points if p is not None),
                    key=lambda p: (p[0] - body.x) ** 2 + (p[1] - body.y) ** 2,
                    reverse=True)
    if not points:
        return None
    return points[min(rank, len(points) - 1)]


def strike_nearest_point(ref):
    """A stranded defender blames the pool point it came from; a point
    whose spawns get stuck repeatedly is a sealed closet — retire it."""
    best_i, best_d = None, config.POINT_STRIKE_RADIUS**2
    for i, point in enumerate(runstate.spawn_points):
        if point is None:
            continue
        d = (point[0] - ref.x) ** 2 + (point[1] - ref.y) ** 2
        if d < best_d:
            best_i, best_d = i, d
    if best_i is None:
        return
    strikes = runstate.point_strikes.get(best_i, 0) + 1
    if strikes >= config.POINT_MAX_STRIKES:
        runstate.spawn_points[best_i] = None
        bd.log(f"horde: retired a sealed spawn point ({pool_size()} left)")
    else:
        runstate.point_strikes[best_i] = strikes


def capture_spawn_points():
    """Commandeer the map's authored monster positions as this arena's
    spawn pool, then remove the originals — destroyed outright, so no
    carcasses litter the arena. Boss-death map specials (MAP07-style)
    don't fire now, but they still fire later when same-class horde
    monsters die.
    """
    refs = runstate.safe_call(bd.actor_refs) or []
    originals = [ref for ref in refs
                 if ref.valid and ref.alive and ref.is_monster
                 and not ref.is_player and ref not in runstate.defenders]
    runstate.spawn_points = [(ref.x, ref.y, ref.z, ref.angle)
                             for ref in originals]
    runstate.point_strikes = {}
    if originals:
        runstate.safe_call(bd.apply_actor_batch,
                           [("destroy", ref) for ref in originals])
        bd.ui.toast("ARENA CLAIMED · KILL THE TINTED",
                    color=bd.ui.theme.warn, duration=2.0)
    bd.log(f"horde: commandeered {len(runstate.spawn_points)}"
           " spawn point(s)")


# --- marking ---------------------------------------------------------------------------

def mark_defender(ref, record=None):
    """Apply the full combat package and the visual language: every wave
    monster is tinted (crimson, or its affix color); only champions and
    uniques get an overhead name and health bar. All world visuals stay
    sight-occluded — nothing shines through walls."""
    if record is not None:
        runstate.defender_info[ref] = record
    record = runstate.defender_info.get(ref)
    mods = record["mods"] if record is not None else {}
    merged = affixes.merged_mods(mods)
    runstate.safe_call(bd.apply_actor_batch,
                       [("tint", ref, *visuals.ring_color(ref))]
                       + affixes.combat_ops(ref, merged))
    if record is not None and ref.valid:
        # Ground truth: the batch is synchronous and the monster is
        # undamaged at mark time, so post-batch health IS the max.
        record["max_health"] = max(1, ref.health)
    if (record is not None and record["tier"] >= 2
            and ref not in visuals.ring_slots):
        if visuals.alloc_ring(ref) is not None:
            visuals.draw_rarity_marks(ref)


def crown_unique(ref):
    """Promote a spawned defender to UNIQUE: from wave 6, half the crowns
    are hand-crafted NAMED UNIQUES ('The Maledict', 'Babelspawn'...), the
    rest roll a second distinct prefix + guaranteed suffix. Extra health,
    gold visuals, an entrance toast. Only the stat RATIO vs the mods
    already applied at spawn is batched, so crowning a champion never
    double-dips."""
    old = runstate.defender_info.get(ref) or {}
    old_mods = old.get("mods", {})
    stream = runstate.fresh_stream(991)
    prefix, prefix2, suffix, title, extra = affixes.roll_unique_identity(
        stream, prefix=old.get("prefix"), suffix=old.get("suffix"))
    new_mods = {}
    for src in (prefix, prefix2, suffix):
        if src is not None:
            affixes.merge_mods(new_mods, src["mods"])
    affixes.merge_mods(new_mods, extra)
    affixes.merge_mods(new_mods,
                       {"health": config.CHAMPION_HEALTH
                        * config.UNIQUE_HEALTH})
    ratio = {key: value / old_mods.get(key, 1.0)
             for key, value in new_mods.items()}
    for key, value in old_mods.items():
        if key not in ratio:
            ratio[key] = 1.0 / value
    record = {"tier": 3, "prefix": prefix, "prefix2": prefix2,
              "suffix": suffix, "mods": new_mods,
              "score": config.UNIQUE_SCORE}
    if title is not None:
        record["title"] = title
    runstate.defender_info[ref] = record
    ops = []
    if "health" in ratio:
        ops.append(("health", ref, max(1, int(ref.health
                                              * ratio["health"]))))
    if "speed" in ratio:
        ops.append(("speed", ref, ref.speed * ratio["speed"]))
    if "dealt" in ratio:
        ops.append(("damage_multiply", ref,
                    ref.damage_multiply * ratio["dealt"]))
    if "taken" in ratio:
        ops.append(("damage_factor", ref, ref.damage_factor
                    * ratio["taken"]))
    if "scale" in ratio:
        ops.append(("scale", ref, max(ref.scale_x, 0.05) * ratio["scale"]))
    if "alpha" in ratio:
        ops.append(("alpha", ref, ref.alpha * ratio["alpha"]))
    ops.append(("tint", ref, *config.UNIQUE_COLOR))
    runstate.safe_call(bd.apply_actor_batch, ops)
    if ref.valid:
        # Same ground-truth rule as spawn: post-batch health is the max.
        record["max_health"] = max(1, ref.health)
    if ref not in visuals.ring_slots:
        visuals.alloc_ring(ref)
    visuals.draw_rarity_marks(ref)
    runstate.uniques_pending -= 1
    title = affixes.affix_title(ref.class_name, record)
    bd.ui.toast(f"{title} has risen", color=config.UNIQUE_COLOR,
                duration=2.5)
    bd.log(f"horde: unique crowned: {title}")


def spawn_defender(stream):
    """Spawn one defender at a map-authored spawn point; returns the handle
    or None (the next trickle pass retries the next point). Placement is
    fit-checked. Monster-less maps fall back to the player-relative ring,
    forcing spawns only after a grace period of failed fit-checks. Each
    spawn rolls for champion rarity; uniques are crowned from the roll."""
    body = runstate.player_body()
    if body is None or runstate.quota_left <= 0:
        return None
    point = next_pool_point(body)
    if point is not None:
        x, y, z, angle = point
        force = False
    else:
        x, y, z, angle = ring_position(body, stream,
                                       config.SPAWN_MIN_RADIUS,
                                       config.SPAWN_MAX_RADIUS)
        force = runstate.fit_failures >= config.FORCE_FALLBACK_PASSES
    ref = runstate.safe_call(bd.spawn,
                             runstate.fresh_stream(7331).choice(
                                 unlocked_classes()),
                             x, y, z, angle=angle, force=force)
    if ref is None or not ref.valid:
        return None
    runstate.quota_left -= 1
    runstate.defenders.add(ref)
    mark_defender(ref, affixes.roll_champion(runstate.fresh_stream(9173)))
    if (runstate.uniques_pending > 0
            and runstate.defender_info.get(ref, {}).get("tier", 0) < 3):
        crown_unique(ref)
    return ref


# --- tasks ------------------------------------------------------------------------------

@bd.on("pre_tick", every=config.TRICKLE_EVERY)
def trickle(event):
    """Keep the pressure on: top up defenders to the concurrent cap until
    the wave's kill quota is exhausted. Deep waves trickle faster."""
    if runstate.phase != "fight" or runstate.quota_left <= 0:
        return
    alive = len(runstate.live_defenders())
    cap = min(18, 6 + runstate.wave, max(6, pool_size()))
    per_pass = min(4, config.TRICKLE_PER_PASS + runstate.wave // 10)
    stream = bd.rng(runstate.wave * 91 + runstate.quota_left * 7 + alive)
    spawned = 0
    for _ in range(min(per_pass, cap - alive)):
        if spawn_defender(stream) is None:
            break  # bad spot; next pass retries
        spawned += 1
    if spawned == 0 and alive < cap and runstate.quota_left > 0:
        runstate.fit_failures += 1
        if runstate.fit_failures == config.FORCE_FALLBACK_PASSES:
            bd.log("horde: no valid spawn spots; forcing spawns now")
    elif spawned > 0:
        runstate.fit_failures = 0
    visuals.render_hud()


@bd.on("pre_tick", every=config.STRAGGLER_EVERY)
def straggler_sweep(event):
    """Anti-soft-lock: a defender stuck across the map, in a sealed closet
    or at the bottom of a pit can never be killed, which would stall the
    wave forever. Anything far away, pit-fallen, or simply unmoved since
    the last sweep is relocated to another map-authored pool point — and
    the point it came from collects a strike toward retirement. Also a
    cheap safety refresh for health bars.
    """
    if runstate.phase != "fight":
        return
    body = runstate.player_body()
    if body is None:
        return
    stream = bd.rng(runstate.wave * 733 + event.get("level_time", 0))
    ops = []
    positions = {}
    for ref in runstate.live_defenders():
        visuals.update_health_bar(ref)
        dx, dy = ref.x - body.x, ref.y - body.y
        dist_sq = dx * dx + dy * dy
        prev = runstate.last_positions.get(ref)
        moved = (None if prev is None else
                 math.dist((ref.x, ref.y, ref.z), prev))
        stranded = (
            dist_sq > config.STRAGGLER_RADIUS**2
            or abs(ref.z - body.z) > config.STRAGGLER_Z_DROP
            or (dist_sq > config.STRAGGLER_MELEE_EXEMPT**2
                and moved is not None
                and moved < config.STRAGGLER_STUCK_TOL))
        if stranded:
            strike_nearest_point(ref)
            point = random_pool_point(stream, body)
            if point is not None:
                ops.append(("position", ref, point[0], point[1], point[2]))
            else:
                x, y, z, _ = ring_position(body, stream,
                                           config.SPAWN_MIN_RADIUS,
                                           config.SPAWN_MAX_RADIUS)
                ops.append(("position", ref, x, y, z))
        positions[ref] = (ref.x, ref.y, ref.z)
    runstate.last_positions = positions
    if ops:
        applied = runstate.safe_call(bd.apply_actor_batch, ops)
        if applied:
            bd.log(f"horde: repositioned {applied} straggler(s)")
    visuals.update_tracker()


# --- actor events -------------------------------------------------------------------------

@bd.on("actor_died")
def defender_died(event):
    runstate.feed_entropy(event.get("level_time", 0))
    runstate.kill_counter += 1
    ref = event["actor_ref"]
    if ref is None or ref not in runstate.defenders:
        return
    runstate.defenders.discard(ref)
    visuals.free_ring(ref)
    record = runstate.defender_info.get(ref)  # kept until destroy: revives
    body = runstate.player_body()
    if body is not None:
        loot.feed_ammo(body)  # the endless ration: every marked kill feeds
    loot.drop_loot(ref, record)
    effects = affixes.record_effects(record)
    if "volatile" in effects:
        runstate.safe_call(bd.radius_damage, ref,
                           config.VOLATILE_DAMAGE_BASE
                           + config.VOLATILE_DAMAGE_PER_WAVE
                           * runstate.wave,
                           config.VOLATILE_RADIUS, source=ref)
        bd.log(f"horde: {affixes.prettify(ref.class_name)} detonated")
    if "splitting" in effects and runstate.phase == "fight":
        split_spawn(ref)
    if record is not None and record.get("boss"):
        loot.boss_loot(ref)
    if ref in runstate.bosses:
        runstate.bosses.discard(ref)
        gained = config.SCORE_PER_BOSS * runstate.boss_index()
        runstate.add_score(gained)
        visuals.popup(gained, color=config.UNIQUE_COLOR)
        bd.ui.announce("BOSS DOWN", subtitle=f"+{gained}",
                       color=(255, 200, 40), duration=2.0)
        bd.play_ui_sound("misc/pkup")
    else:
        mult = record["score"] if record is not None else 1
        gained = config.SCORE_PER_KILL * runstate.wave * mult
        runstate.add_score(gained)
        visuals.popup(gained,
                      color=(visuals.ring_color(ref)
                             if record is not None and record["tier"] >= 2
                             else (255, 205, 70)))
        if record is not None and record["tier"] >= 2:
            title = affixes.affix_title(ref.class_name, record)
            bd.ui.toast(f"SLAIN: {title.upper()}",
                        color=visuals.ring_color(ref), duration=1.8)
            bd.play_ui_sound("misc/secret" if record["tier"] == 3
                             else "misc/pkup")
            bd.log(f"horde: slain {title} (+{gained})")
    if runstate.quota_left <= 0 and not runstate.live_defenders():
        import horde_waves as waves
        waves.wave_cleared()
    visuals.update_tracker()
    visuals.render_hud()


@bd.on("actor_destroyed")
def defender_destroyed(event):
    """A defender leaving the world without dying still counts down."""
    ref = event["actor_ref"]
    runstate.defender_info.pop(ref, None)
    if ref is None or ref not in runstate.defenders:
        return
    runstate.defenders.discard(ref)
    visuals.free_ring(ref)
    if (runstate.phase == "fight" and runstate.quota_left <= 0
            and not runstate.live_defenders()):
        import horde_waves as waves
        waves.wave_cleared()


@bd.on("actor_revived")
def defender_revived(event):
    """An archvile boss can raise fallen defenders. A risen monster joins
    the wave again with its affix identity intact, so it must be killed
    twice — and pays out twice."""
    ref = event["actor_ref"]
    if (ref is None or not ref.valid or runstate.phase != "fight"
            or not ref.is_monster or ref.is_player
            or ref in runstate.defenders):
        return
    runstate.defenders.add(ref)
    mark_defender(ref)  # re-applies the record kept since its death
    bd.ui.toast("THE DEAD RISE", color=bd.ui.theme.warn, duration=1.5)
    bd.log("horde: a fallen defender rose again")


def split_spawn(ref):
    """Brood defenders birth lesser defenders from their corpse — enlisted
    and marked like any wave monster (they must die for the clear)."""
    if len(runstate.live_defenders()) > 36:
        return
    classes = (("DoomImp", "LostSoul") if runstate.wave >= 2
               else ("DoomImp",))
    stream = runstate.fresh_stream(2777)
    for _ in range(config.SPLIT_COUNT):
        child = runstate.safe_call(bd.spawn, stream.choice(classes),
                                   ref.x, ref.y, ref.floor_z, force=True)
        if child is None or not child.valid:
            continue
        runstate.defenders.add(child)
        mark_defender(child,
                      affixes.roll_champion(runstate.fresh_stream(9173)))


@bd.on("actor_damaged")
def defender_damaged(event):
    """Health bars are manual-fraction: refresh on every hit. Affix
    mechanics ride along: vampiric attackers leech health from the damage
    they deal, thorned defenders reflect part of the damage back."""
    ref = event["actor_ref"]
    damage = event.get("damage", 0)
    source = event.get("source_ref")
    if (source is not None and source.valid and source.alive
            and source in runstate.defenders):
        source_record = runstate.defender_info.get(source)
        if "vampiric" in affixes.record_effects(source_record):
            runstate.safe_call(source.heal,
                               max(1, int(damage * config.VAMPIRIC_LEECH)),
                               source_record.get("max_health", 0)
                               if source_record else 0)
            visuals.update_health_bar(source)
    if ref is None or ref not in runstate.defenders:
        return
    record = runstate.defender_info.get(ref)
    if ("thorned" in affixes.record_effects(record)
            and source is not None and source.valid and source.alive
            and source.is_player and damage > 0):
        runstate.safe_call(source.damage,
                           max(1, int(damage * config.THORN_REFLECT)),
                           source=ref)
    visuals.update_health_bar(ref)


@bd.on("actor_spawned")
def summon_enlisted(event):
    """Monsters summoned by defenders (Pain Elemental Lost Souls, boss
    spawners on maps like MAP30) join the wave marked, without consuming
    quota — tinted = counts stays true no matter where they came from."""
    if runstate.phase != "fight":
        return
    ref = event["actor_ref"]
    if (ref is None or not ref.valid or not ref.is_monster
            or ref.is_player or ref in runstate.defenders):
        return
    master = runstate.safe_call(lambda: ref.master)
    if master is None or not master.valid or master not in runstate.defenders:
        return
    runstate.defenders.add(ref)
    mark_defender(ref, affixes.roll_champion(runstate.fresh_stream(53)))
    bd.log(f"horde: summon enlisted: {affixes.prettify(ref.class_name)}")
