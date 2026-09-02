"""Loot: kill drops with wave-gated rarity tables, the weapon unlock
ladder, smart ammo filtering, and the between-wave supply drops.
"""

import math

import biaseddoom as bd

import horde_affixes as affixes
import horde_config as config
import horde_runstate as runstate

drop_ring_counter = 0


def owns(body, class_name):
    return bool(runstate.safe_call(body.inventory_count, class_name))


def feed_ammo(body):
    """The endless ration: every marked kill feeds ammo straight into the
    pool (no pickup logistics), matched to the weapons you actually own.
    This is what keeps a horde run sustainable on maps like MAP01 that
    place barely any ammo. Grants grow slowly with the wave."""
    bonus = 1 + runstate.wave // 8
    grants = [("Clip", 2 * bonus)]                # pistol is always there
    if owns(body, "Shotgun") or owns(body, "SuperShotgun"):
        grants.append(("Shell", 2 * bonus))
    if owns(body, "Chaingun"):
        grants.append(("Clip", 2 * bonus))
    if owns(body, "RocketLauncher") and runstate.kill_counter % 2 == 0:
        grants.append(("RocketAmmo", bonus))
    if (owns(body, "PlasmaRifle") or owns(body, "BFG9000")) \
            and runstate.kill_counter % 2 == 0:
        grants.append(("Cell", 2 * bonus))
    for class_name, amount in grants:
        runstate.safe_call(body.give_inventory, class_name, amount)


def best_unowned_weapon(body):
    """Best ladder weapon the wave has reached that the player lacks."""
    best = None
    for minimum, class_name in config.WEAPON_LADDER:
        if (runstate.wave >= minimum
                and runstate.safe_call(body.inventory_count,
                                       class_name) == 0):
            best = class_name
    return best


def ammo_filtered(table, body):
    """Ammo drops only for weapons the player actually owns; shell and
    clip weight goes up once the SuperShotgun/Chaingun are in hand."""
    out = [c for c in table
           if config.AMMO_WEAPONS.get(c) is None
           or any(runstate.safe_call(body.inventory_count, w)
                  for w in config.AMMO_WEAPONS[c])]
    if runstate.safe_call(body.inventory_count, "SuperShotgun"):
        out.append("ShellBox" if "ShellBox" in out else "Shell")
    if runstate.safe_call(body.inventory_count, "Chaingun"):
        out.append("Clip")
    return out or ["Stimpack"]


def spawn_pickup(class_name, x, y, z, big=False):
    """Spawn one pickup with a gold glint ring — bigger and longer-lived
    for weapons and major loot."""
    global drop_ring_counter
    item = runstate.safe_call(bd.spawn, class_name, x, y, z, force=True)
    if item is None or not item.valid:
        bd.log(f"horde: loot drop failed for class {class_name}")
        return None
    drop_ring_counter += 1
    runstate.safe_call(bd.draw_world_ring, item,
                       id=config.DROP_RING_BASE
                       + drop_ring_counter % config.DROP_RING_SLOTS,
                       radius=11.0 if big else 8.0, color=(255, 200, 40),
                       alpha=0.9 if big else 0.7, offset_z=1.0,
                       segments=10, duration=8.0 if big else 5.0)
    return item


def drop_loot(ref, record):
    """Kills feed the economy: normal kills drop minor pickups at a
    wave-scaled chance (with a 2% lucky hit on the medium table);
    champions and uniques can drop the best weapon you're missing off the
    ladder (35%/60%) and otherwise always drop medium/major loot —
    Backpack w10, Soulsphere w12, powerups from w8 (Berserk, BlurSphere,
    BlueArmor, InvulnerabilitySphere). Loot lands at floor level, even
    when a flier dies in the sky."""
    tier = record["tier"] if record is not None else 0
    stream = runstate.fresh_stream(4111)
    body = runstate.player_body()
    z = ref.floor_z
    weapon_chance = (config.WEAPON_DROP_UNIQUE if tier == 3
                     else config.WEAPON_DROP_CHAMPION if tier == 2 else 0.0)
    if body is not None and stream.float() < weapon_chance:
        weapon = best_unowned_weapon(body)
        if weapon is not None:
            spawn_pickup(weapon, ref.x, ref.y, z, big=True)
            return
    if tier == 3:
        table = list(config.DROP_MAJOR)
        if runstate.wave >= 10:
            table.append("Backpack")
        if runstate.wave >= 12:
            table.append("Soulsphere")
        table += [cls for minimum, cls in config.DROP_POWERUPS
                  if runstate.wave >= minimum]
    elif tier == 2:
        table = list(config.DROP_MEDIUM)
        if runstate.wave >= 8:
            table += config.DROP_MEDIUM_DEEP
    else:
        # Lucky drops: the lucky WINDOW itself is re-rolled per kill
        # (1-10%), and a lucky hit can jackpot into the major table.
        lucky_window = (config.LUCKY_CHANCE_MIN + stream.float()
                        * (config.LUCKY_CHANCE_MAX
                           - config.LUCKY_CHANCE_MIN))
        lucky = stream.float() < lucky_window
        if not lucky and stream.float() >= affixes.drop_chance():
            return
        jackpot = lucky and stream.float() < config.LUCKY_JACKPOT
        if jackpot:
            table = list(config.DROP_MAJOR)
            if runstate.wave >= 10:
                table.append("Backpack")
            if runstate.wave >= 12:
                table.append("Soulsphere")
        else:
            table = list(config.DROP_MEDIUM if lucky
                         else config.DROP_MINOR)
            if runstate.wave >= 8:
                table += (config.DROP_MEDIUM_DEEP if lucky
                          else config.DROP_MINOR_DEEP)
        if lucky:
            bd.ui.toast("LUCKY DROP", color=bd.ui.theme.gold,
                        duration=1.5)
            bd.log(f"horde: lucky drop "
                   f"({'jackpot' if jackpot else 'medium'} table)")
    if body is not None:
        table = ammo_filtered(table, body)
    rolls = 2 if "hoarding" in affixes.record_effects(record) else 1
    for _ in range(rolls):
        spawn_pickup(stream.choice(table), ref.x, ref.y, z,
                     big=(tier == 3))


def boss_loot(ref):
    """Bosses burst into major loot: the best missing ladder weapon (or a
    Soulsphere — Megasphere in the deep game), a Medikit, and an ammo
    restock — the boss fight is the ammo sink, so it pays it back."""
    body = runstate.player_body()
    weapon = best_unowned_weapon(body) if body is not None else None
    fallback = ("Megasphere"
                if runstate.boss_index() >= config.MEGASPHERE_BOSS_INDEX
                else "Soulsphere")
    drops = ([weapon] if weapon else [fallback]) + ["Medikit"]
    if body is not None and (owns(body, "Shotgun")
                             or owns(body, "SuperShotgun")):
        drops.append("ShellBox")
    drops.append("ClipBox")
    if body is not None and owns(body, "RocketLauncher"):
        drops.append("RocketBox")
    if body is not None and (owns(body, "PlasmaRifle")
                             or owns(body, "BFG9000")):
        drops.append("CellPack")
    for class_name in drops:
        spawn_pickup(class_name, ref.x, ref.y, ref.floor_z, big=True)


def drop_supplies():
    """2-4 items ringed in gold near you — the endless economy. Always
    includes the best ladder weapon you're missing (once unlocked)."""
    body = runstate.player_body()
    if body is None:
        return
    runstate.supply_count += 1
    classes = list(config.SUPPLY_BASE)
    if runstate.supply_count % 2 == 0:
        classes.append(config.SUPPLY_MED)
    if runstate.supply_count % 3 == 0:
        classes.append(config.SUPPLY_ARMOR)
    weapon = best_unowned_weapon(body)
    if weapon is not None:
        classes.insert(0, weapon)  # slot 0: always part of the drop
    # An ammo box matched to the arsenal keeps the clear bonus relevant.
    if owns(body, "PlasmaRifle") or owns(body, "BFG9000"):
        classes.insert(1, "CellPack")
    elif owns(body, "RocketLauncher"):
        classes.insert(1, "RocketBox")
    elif owns(body, "Shotgun") or owns(body, "SuperShotgun"):
        classes.insert(1, "ShellBox")
    count = min(5, 3 + runstate.wave // 4, len(classes))
    stream = bd.rng(runstate.wave * 1777 + runstate.supply_count)
    for slot in range(count):
        angle = stream.float() * 2 * 3.141592653589793
        radius = (config.SUPPLY_RADIUS[0] + stream.float()
                  * (config.SUPPLY_RADIUS[1] - config.SUPPLY_RADIUS[0]))
        ref = runstate.safe_call(bd.spawn, classes[slot % len(classes)],
                                 body.x + math.cos(angle) * radius,
                                 body.y + math.sin(angle) * radius, body.z,
                                 angle=math.degrees(angle) + 180.0,
                                 force=True)
        if ref is not None and ref.valid:
            runstate.safe_call(bd.draw_world_ring, ref,
                               id=config.SUPPLY_RING_BASE + slot,
                               radius=10.0, color=(255, 200, 40), alpha=0.8,
                               offset_z=1.5, segments=14, duration=6.0)
    bd.ui.toast("SUPPLIES DROPPED", color=bd.ui.theme.gold, duration=1.5)
