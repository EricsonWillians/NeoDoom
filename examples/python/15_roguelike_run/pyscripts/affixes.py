"""Diablo-2-style monster affixes: prefix/suffix tables, args[4] packing,
title building, and attribute/XP combination.

An affix record is a dict:
    {"tier": 0..3, "p1": index|None, "p2": index|None, "s": index|None}

Tiers: 0 = normal, 1 = affixed (prefix, maybe suffix), 2 = champion
(prefix + suffix), 3 = unique (two prefixes + suffix, crowned per map).

The record is packed into the monster's args[4] with a magic marker:
args survive checkpoint save/load, so the post-load rescan re-applies
visuals without re-rolling. (Maps/mods using the 5th thing arg are safe:
without the magic marker the value is ignored.)

    bits:  30        magic marker
           12..13    tier
           8..11     prefix 1   (0xF = none)
           4..7      prefix 2   (0xF = none)
           0..3      suffix     (0xF = none)
"""

MAGIC = 0x40000000
NONE = 0xF

TIER_NAMES = {1: "affixed", 2: "champion", 3: "unique"}

# mods keys: health, speed, dealt (damage_multiply), taken (damage_factor),
# scale, alpha — all multipliers applied to the live monster.
PREFIXES = (
    {"name": "Burning",  "color": (255, 100, 40),
     "mods": {"dealt": 1.5}, "xp": 1.5},
    {"name": "Frozen",   "color": (90, 200, 255),
     "mods": {"speed": 0.85, "taken": 0.9}, "xp": 1.4},
    {"name": "Venomous", "color": (90, 220, 100),
     "mods": {"health": 1.3}, "xp": 1.3},
    {"name": "Charged",  "color": (255, 240, 90),
     "mods": {"speed": 1.4}, "xp": 1.4},
    {"name": "Spectral", "color": (215, 215, 235),
     "mods": {"alpha": 0.55, "taken": 0.7}, "xp": 1.8},
    {"name": "Stone",    "color": (150, 150, 160),
     "mods": {"health": 1.8, "speed": 0.7}, "xp": 1.6},
    {"name": "Savage",   "color": (235, 60, 60),
     "mods": {"dealt": 1.3, "scale": 1.1}, "xp": 1.4},
    {"name": "Raging",   "color": (255, 140, 50),
     "mods": {"dealt": 1.2, "speed": 1.2}, "xp": 1.5},
)

SUFFIXES = (
    {"name": "of the Bear",  "color": (205, 145, 85),
     "mods": {"health": 1.5}, "xp": 1.3},
    {"name": "of the Wolf",  "color": (160, 195, 255),
     "mods": {"speed": 1.2}, "xp": 1.2},
    {"name": "of the Titan", "color": (255, 190, 90),
     "mods": {"scale": 1.2, "health": 2.0}, "xp": 2.0},
    {"name": "of Ruin",      "color": (215, 85, 95),
     "mods": {"dealt": 1.25}, "xp": 1.3},
    {"name": "of Greed",     "color": (255, 215, 60),
     "mods": {}, "xp": 2.0},
    {"name": "of Mist",      "color": (170, 200, 215),
     "mods": {"alpha": 0.75}, "xp": 1.2},
)

assert len(PREFIXES) <= NONE and len(SUFFIXES) <= NONE


# --- packing --------------------------------------------------------------------

def pack(record):
    def bits(index):
        return NONE if index is None else index & 0xF
    return (MAGIC | (record["tier"] & 0x3) << 12 | bits(record["p1"]) << 8
            | bits(record["p2"]) << 4 | bits(record["s"]))


def unpack(value):
    """args[4] int -> affix record, or None when the marker is absent."""
    if not value & MAGIC:
        return None
    def field(shift):
        index = (value >> shift) & 0xF
        return None if index == NONE else index
    record = {"tier": (value >> 12) & 0x3, "p1": field(8),
              "p2": field(4), "s": field(0)}
    # Defensive: indexes must name real affixes (table could have shrunk
    # between checkpoint save and load).
    for key, table in (("p1", PREFIXES), ("p2", PREFIXES), ("s", SUFFIXES)):
        if record[key] is not None and record[key] >= len(table):
            return None
    return record if record["tier"] > 0 else None


def read(actor):
    """Live Actor handle -> affix record, or None for a plain monster."""
    try:
        return unpack(actor.args[4])
    except (RuntimeError, ReferenceError):
        return None


def write(actor, record):
    args = list(actor.args)
    args[4] = pack(record)
    actor.args = args


# --- combination ------------------------------------------------------------------

def _affix_at(table, index):
    return None if index is None else table[index]


def each_affix(record):
    """Yield every affix entry in the record, prefixes first."""
    for table, key in ((PREFIXES, "p1"), (PREFIXES, "p2"), (SUFFIXES, "s")):
        entry = _affix_at(table, record[key])
        if entry is not None:
            yield entry


def combined_mods(record):
    """Multiply all affix mods into one {key: multiplier} dict."""
    combined = {}
    for entry in each_affix(record):
        for key, mult in entry["mods"].items():
            combined[key] = combined.get(key, 1.0) * mult
    return combined


def xp_multiplier(record):
    mult = 1.0
    for entry in each_affix(record):
        mult *= entry["xp"]
    return mult


def color(record):
    """Representative color: first prefix wins, then suffix (uniques override
    this with gold at the call site)."""
    first = _affix_at(PREFIXES, record["p1"])
    if first is not None:
        return first["color"]
    suffix = _affix_at(SUFFIXES, record["s"])
    return suffix["color"] if suffix is not None else (255, 255, 255)


# --- titles ------------------------------------------------------------------------

def prettify(class_name):
    """'DoomImp' -> 'Doom Imp' so titles read like Diablo monster names."""
    out = []
    for index, char in enumerate(class_name):
        if index > 0 and char.isupper() and not class_name[index - 1].isupper():
            out.append(" ")
        out.append(char)
    return "".join(out)


def title(class_name, record):
    """'Burning Doom Imp of the Bear' for an affixed record."""
    parts = []
    prefix = _affix_at(PREFIXES, record["p1"])
    if prefix is not None:
        parts.append(prefix["name"])
    parts.append(prettify(class_name))
    suffix = _affix_at(SUFFIXES, record["s"])
    if suffix is not None:
        parts.append(suffix["name"])
    return " ".join(parts)
