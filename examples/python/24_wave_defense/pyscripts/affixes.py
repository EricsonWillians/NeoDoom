"""Affix naming and stat math: Doom-lore Diablo-2-style prefixes/suffixes,
the per-wave difficulty curve, and the combat package builders.

`combat_ops` expects PRE-MERGED mods (see `merged_mods`). The monster's
max health is read back from the actor after the batch (ground truth),
never derived here — health bars depend on it.
"""

import horde_config as config
import horde_runstate as runstate


def prettify(class_name):
    """'BaronOfHell' -> 'Baron of Hell'."""
    out = []
    for i, ch in enumerate(class_name):
        if i and ch.isupper() and not class_name[i - 1].isupper():
            out.append(" ")
        out.append(ch)
    words = "".join(out).split()
    return " ".join(w.lower() if i and w in ("Of", "The") else w
                    for i, w in enumerate(words))


def affix_title(class_name, record):
    """'Burning Doom Imp of the Pit' — or the fixed identity of a named
    unique ('The Maledict')."""
    if record.get("title"):
        return record["title"]
    parts = []
    if record.get("prefix") is not None:
        parts.append(record["prefix"]["name"])
    parts.append(prettify(class_name))
    if record.get("suffix") is not None:
        parts.append(record["suffix"]["name"])
    return " ".join(parts)


def find_affix(table, name):
    return next(a for a in table if a["name"] == name)


def record_effects(record):
    """The union of all mechanical effects carried by a record's affixes
    (volatile / splitting / vampiric / thorned / hoarding)."""
    effects = set()
    if record is not None:
        for key in ("prefix", "prefix2", "suffix"):
            affix = record.get(key)
            if affix is not None:
                effects |= affix.get("effects", set())
    return effects


# --- per-wave difficulty curve ---------------------------------------------------------

def wave_health_mult():
    return 1.0 + config.HEALTH_PER_WAVE * (runstate.wave - 1)


def wave_speed_mult():
    return min(config.SPEED_MULT_MAX,
               1.0 + config.SPEED_PER_WAVE * (runstate.wave - 1))


def wave_dealt_mult():
    return min(config.DEALT_MULT_MAX,
               1.0 + config.DEALT_PER_WAVE * (runstate.wave - 1))


def champion_chance():
    return min(config.CHAMPION_CHANCE_MAX,
               config.CHAMPION_CHANCE_BASE
               + config.CHAMPION_CHANCE_PER_WAVE * runstate.wave)


def drop_chance():
    return min(config.DROP_CHANCE_MAX,
               config.DROP_CHANCE_BASE
               + config.DROP_CHANCE_PER_WAVE * runstate.wave)


# --- mods ----------------------------------------------------------------------------

def merge_mods(into, extra):
    for key, value in extra.items():
        into[key] = into.get(key, 1.0) * value


def merged_mods(mods):
    """Affix/rarity mods x the wave mutators."""
    merged = dict(mods)
    for mut in runstate.mutators:
        merge_mods(merged, mut["mods"])
    return merged


def roll_champion(stream):
    """Returns a champion record, or None for a normal defender."""
    if stream.float() >= champion_chance():
        return None
    prefix = stream.choice(config.PREFIXES)
    suffix = (stream.choice(config.SUFFIXES)
              if stream.float() < config.SUFFIX_CHANCE else None)
    mods = dict(prefix["mods"])
    if suffix is not None:
        merge_mods(mods, suffix["mods"])
    merge_mods(mods, {"health": config.CHAMPION_HEALTH})
    return {"tier": 2, "prefix": prefix, "prefix2": None,
            "suffix": suffix, "mods": mods, "score": config.CHAMPION_SCORE}


def roll_unique_identity(stream, prefix=None, suffix=None):
    """Pick a unique's affix identity: a hand-crafted NAMED UNIQUE half
    the time from wave 6, otherwise a random second prefix + guaranteed
    suffix. Returns (prefix, prefix2, suffix, title, extra_mods)."""
    if (runstate.wave >= config.NAMED_UNIQUE_FROM_WAVE
            and stream.float() < 0.5):
        named = config.NAMED_UNIQUES[
            (runstate.wave + len(runstate.defender_info))
            % len(config.NAMED_UNIQUES)]
        return (find_affix(config.PREFIXES, named["prefix"]),
                None,
                find_affix(config.SUFFIXES, named["suffix"]),
                named["title"], dict(named["mods"]))
    first = prefix or stream.choice(config.PREFIXES)
    second = stream.choice([p for p in config.PREFIXES if p is not first])
    return (first, second, suffix or stream.choice(config.SUFFIXES),
            None, {})


# --- combat package ------------------------------------------------------------------

def combat_ops(ref, merged):
    """Batch ops for a PRE-MERGED mod set: per-wave curve x mods."""
    ops = [("health", ref, max(1, int(ref.health * wave_health_mult()
                                      * merged.get("health", 1.0)))),
           ("speed", ref, ref.speed * wave_speed_mult()
            * merged.get("speed", 1.0)),
           ("damage_multiply", ref, ref.damage_multiply * wave_dealt_mult()
            * merged.get("dealt", 1.0))]
    if "taken" in merged:
        ops.append(("damage_factor", ref,
                    ref.damage_factor * merged["taken"]))
    if "scale" in merged:
        ops.append(("scale", ref, max(ref.scale_x, 0.05) * merged["scale"]))
    if "alpha" in merged:
        ops.append(("alpha", ref, ref.alpha * merged["alpha"]))
    return ops
