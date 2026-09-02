"""Every tunable, table and canvas id for the endless horde, in one place."""

import biaseddoom as bd

# --- pacing ------------------------------------------------------------------

FIRST_WAVE_TICS = bd.TICRATE * 4     # first wave ~4s into the map
BREATHER_SECONDS = 5
TRICKLE_EVERY = 10                   # tics between reinforcement passes
TRICKLE_PER_PASS = 2                 # max spawns per pass
QUOTA_MAX = 40                       # quota keeps growing into the deep game

# --- spawn pool / placement ----------------------------------------------------

SPAWN_MIN_RADIUS = 500.0             # ring fallback band (monster-less maps)
SPAWN_MAX_RADIUS = 750.0
SPAWN_FACE_MIN = 256.0               # spawn points closer than this are
                                     # used only when nothing else is left
SUPPLY_RADIUS = (120.0, 220.0)       # supply drop ring band
FORCE_FALLBACK_PASSES = 10           # ring fallback: fit-checks before force

# --- straggler sweep / pool self-pruning ---------------------------------------

STRAGGLER_EVERY = bd.TICRATE * 6     # sweep for stuck defenders every 6s
STRAGGLER_RADIUS = 1700.0            # beyond this: relocate to a pool point
STRAGGLER_Z_DROP = 128.0             # pit/ledge fall that strands a defender
STRAGGLER_STUCK_TOL = 24.0           # moved less than this in 6s: stuck
STRAGGLER_MELEE_EXEMPT = 300.0       # stuck check skips defenders this close
POINT_STRIKE_RADIUS = 300.0          # strike the pool point nearest a stray
POINT_MAX_STRIKES = 2                # strikes before a sealed point retires

# --- finder aids -----------------------------------------------------------------

TRACKER_REMAINING = 3                # motion tracker appears at this many left
COMPASS_EVERY = 4                    # tics between compass needle updates
COMPASS_SPREAD = 0.22                # half-width of the needle strip (x norm)

# --- score -------------------------------------------------------------------------

SCORE_PER_KILL = 10                  # x wave number x rarity
SCORE_PER_CLEAR = 100                # x wave number (x2 on milestone waves)
SCORE_PER_BOSS = 500                 # x boss index

# --- roster --------------------------------------------------------------------------

# Class tiers unlock with the wave number (Doom II roster).
CLASS_TIERS = (
    (1, ("Zombieman", "DoomImp")),
    (2, ("LostSoul",)),
    (3, ("ShotgunGuy", "Demon")),
    (4, ("ChaingunGuy",)),
    (5, ("Spectre", "Cacodemon")),
    (8, ("HellKnight", "Revenant")),
    (10, ("PainElemental",)),
    (12, ("BaronOfHell", "Mancubus", "Arachnotron")),
)
NEW_THREATS = {minimum: " + ".join(tier).upper()
               for minimum, tier in CLASS_TIERS}

BOSS_POOL = ("BaronOfHell", "Mancubus", "Arachnotron", "Archvile")
BOSS_TINT = (175, 60, 200)
BOSS_EPITHETS = ("the Defiler", "the Butcher", "the Tormentor",
                 "the Worldbreaker")
BOSS_EVERY = 5                       # every 5th wave is a boss wave
TWIN_BOSSES_FROM = 4                 # boss index for twin bosses (wave 20)
BOSS_HEALTH_BASE = 1.2               # boss hp: (base + per-index x index)
BOSS_HEALTH_PER_INDEX = 0.5          #   x wave health curve
MEGASPHERE_BOSS_INDEX = 3            # bosses from wave 15 drop Megaspheres

DEF_TINT = (255, 92, 64)             # crimson-ember: tinted = counts
UNIQUE_COLOR = (255, 200, 40)

# --- difficulty curve ------------------------------------------------------------

# Per-wave difficulty curve, applied to EVERY defender from wave 1;
# affix mods, mutators and rarity bonuses multiply on top.
HEALTH_PER_WAVE = 0.06             # uncapped: deep waves get tanky
SPEED_PER_WAVE = 0.03
SPEED_MULT_MAX = 1.7
DEALT_PER_WAVE = 0.05              # monsters hit harder every wave
DEALT_MULT_MAX = 2.5

# --- affixes -------------------------------------------------------------------------

# Diablo-2-style affixes, Doom-flavored. mods multiply actor stats:
# health / speed / dealt (damage_multiply) / taken (damage_factor) /
# scale / alpha. Optional "effects" add real mechanics, resolved in
# monsters.py: volatile (explodes on death), splitting (births two
# lesser defenders on death), vampiric (heals from damage it deals),
# thorned (reflects damage at its attacker), hoarding (double loot).
PREFIXES = (
    {"name": "Burning",     "color": (255, 100, 40),
     "mods": {"dealt": 1.4}},
    {"name": "Molten",      "color": (255, 70, 20),
     "mods": {"dealt": 1.2}, "effects": {"volatile"}},
    {"name": "Spectral",    "color": (215, 215, 235),
     "mods": {"alpha": 0.6, "taken": 0.75}},
    {"name": "Vile",        "color": (175, 60, 200),
     "mods": {"health": 1.3, "dealt": 1.15}},
    {"name": "Hellforged",  "color": (235, 60, 60),
     "mods": {"health": 1.5}},
    {"name": "Charged",     "color": (255, 240, 90),
     "mods": {"speed": 1.35}},
    {"name": "Stone",       "color": (150, 150, 160),
     "mods": {"health": 1.9, "speed": 0.75}},
    {"name": "Rabid",       "color": (255, 140, 50),
     "mods": {"speed": 1.2, "dealt": 1.2}},
    {"name": "Voidtouched", "color": (120, 90, 220),
     "mods": {"taken": 0.85, "alpha": 0.85}},
    {"name": "Argent",      "color": (100, 220, 255),
     "mods": {"dealt": 1.25, "speed": 1.1}},
    {"name": "Nightmare",   "color": (80, 70, 120),
     "mods": {"dealt": 1.3, "alpha": 0.85}},
    {"name": "Brood",       "color": (120, 200, 90),
     "mods": {"health": 0.9}, "effects": {"splitting"}},
    {"name": "Leeching",    "color": (200, 50, 90),
     "mods": {"dealt": 1.1}, "effects": {"vampiric"}},
    {"name": "Barbed",      "color": (190, 190, 120),
     "mods": {"health": 1.2}, "effects": {"thorned"}},
)
SUFFIXES = (
    {"name": "of the Pit",       "mods": {"health": 1.4}},
    {"name": "of Deimos",        "mods": {"dealt": 1.2}},
    {"name": "of the Icon",      "mods": {"health": 1.6, "scale": 1.1}},
    {"name": "of Babel",         "mods": {"dealt": 1.3}},
    {"name": "of Pandemonium",   "mods": {"speed": 1.25}},
    {"name": "of the Chasm",     "mods": {"speed": 1.15, "taken": 0.9}},
    {"name": "of the UAC",       "mods": {"taken": 0.9, "speed": 1.05}},
    {"name": "of the Brood",     "mods": {"health": 1.1},
     "effects": {"splitting"}},
    {"name": "of Detonation",    "mods": {"dealt": 1.1},
     "effects": {"volatile"}},
    {"name": "of the Leech",     "mods": {"dealt": 1.05},
     "effects": {"vampiric"}},
    {"name": "of Barbs",         "mods": {"health": 1.15},
     "effects": {"thorned"}},
    {"name": "of Hoarding",      "mods": {},
     "effects": {"hoarding"}},
)

# Hand-crafted unique identities, crowned from wave 6 instead of a random
# roll half the time. prefix/suffix reference the tables above; extra mods
# stack on top.
NAMED_UNIQUE_FROM_WAVE = 6
NAMED_UNIQUES = (
    {"title": "The Maledict", "prefix": "Vile", "suffix": "of the Icon",
     "mods": {"dealt": 1.25}},
    {"title": "Babelspawn", "prefix": "Stone", "suffix": "of Babel",
     "mods": {"health": 1.3}},
    {"title": "The Broodmatron", "prefix": "Brood",
     "suffix": "of the Brood", "mods": {"health": 1.2}},
    {"title": "Gatekeeper of Dis", "prefix": "Hellforged",
     "suffix": "of the Pit", "mods": {"dealt": 1.15, "taken": 0.9}},
    {"title": "The Argent Husk", "prefix": "Argent",
     "suffix": "of the Leech", "mods": {"speed": 1.15}},
)

# Mechanics tuning for affix effects.
VOLATILE_DAMAGE_BASE = 32          # + 2 per wave
VOLATILE_DAMAGE_PER_WAVE = 2
VOLATILE_RADIUS = 96.0
SPLIT_COUNT = 2                    # lesser defenders birthed on death
VAMPIRIC_LEECH = 0.5               # fraction of dealt damage healed
THORN_REFLECT = 0.3                # fraction of taken damage reflected

# Rarity: champions roll per spawn, uniques are crowned from the wave.
CHAMPION_CHANCE_BASE = 0.08
CHAMPION_CHANCE_PER_WAVE = 0.012
CHAMPION_CHANCE_MAX = 0.5          # deep waves are champion-heavy
SUFFIX_CHANCE = 0.45
CHAMPION_HEALTH = 1.5
CHAMPION_SCORE = 3
UNIQUE_HEALTH = 2.5
UNIQUE_SCORE = 5
UNIQUE_FROM_WAVE = 4               # one unique crowned per wave from here
UNIQUE_TWO_FROM_WAVE = 12          # two from here

# --- loot ----------------------------------------------------------------------------

DROP_CHANCE_BASE = 0.12            # normal-kill drop chance ...
DROP_CHANCE_PER_WAVE = 0.01
DROP_CHANCE_MAX = 0.35             # endless runs live off these drops
LUCKY_CHANCE_MIN = 0.01          # lucky window is rolled per kill ...
LUCKY_CHANCE_MAX = 0.10          # ... anywhere in this range
LUCKY_JACKPOT = 0.15             # lucky hits can upgrade to the major table
DROP_MINOR = ("Clip", "Clip", "ClipBox", "Shell", "Shell",
              "HealthBonus", "ArmorBonus", "Stimpack")
DROP_MINOR_DEEP = ("RocketAmmo", "Cell", "ShellBox")
DROP_MEDIUM = ("ShellBox", "Stimpack", "Medikit", "ArmorBonus")
DROP_MEDIUM_DEEP = ("RocketBox", "CellPack")
DROP_MAJOR = ("Medikit", "GreenArmor")   # + Backpack (w10), Soulsphere (w12)
# Powerups join the unique table with depth.
DROP_POWERUPS = ((8, "Berserk"), (12, "BlurSphere"), (14, "BlueArmor"),
                 (16, "InvulnerabilitySphere"))

# Supply drop rotation; Medikit every 2nd drop, GreenArmor every 3rd.
# Bullet boxes keep the endless ammo economy alive on weaponless maps.
SUPPLY_BASE = ("ClipBox", "Shell", "Stimpack")
SUPPLY_MED = "Medikit"
SUPPLY_ARMOR = "GreenArmor"

# Weapon unlock ladder: champions/uniques/bosses and supply drops hand out
# the best unowned weapon the wave has reached.
WEAPON_LADDER = ((2, "Shotgun"), (4, "Chainsaw"), (5, "Chaingun"),
                 (6, "SuperShotgun"), (8, "RocketLauncher"),
                 (10, "PlasmaRifle"), (14, "BFG9000"))
WEAPON_DROP_CHAMPION = 0.35
WEAPON_DROP_UNIQUE = 0.60
# Ammo only drops once the player owns a weapon that uses it.
AMMO_WEAPONS = {"RocketAmmo": ("RocketLauncher",),
                "RocketBox": ("RocketLauncher",),
                "Cell": ("PlasmaRifle", "BFG9000"),
                "CellPack": ("PlasmaRifle", "BFG9000")}

# --- mutators ---------------------------------------------------------------------------

# Per-wave mutators (rolled from wave 2): wave-scoped stat mods, and a
# quota multiplier for SWARM. Announced in the wave banner. The endless
# game stacks them: two from wave 20, three from wave 30.
MUTATOR_CHANCE = 0.65
MUTATORS_STACK_FROM = (20, 30)
MUTATORS = (
    {"name": "FRENZY",        "mods": {"speed": 1.25}, "quota": 1.0},
    {"name": "GLASS CANNONS", "mods": {"dealt": 1.4, "health": 0.6},
     "quota": 1.0},
    {"name": "FORTIFIED",     "mods": {"health": 1.6, "speed": 0.85},
     "quota": 1.0},
    {"name": "SPECTRAL HOST", "mods": {"alpha": 0.7, "taken": 0.9},
     "quota": 1.0},
    {"name": "SWARM",         "mods": {"health": 0.8}, "quota": 1.5},
)

# --- canvas ids (bd.ui owns >= 900000) ------------------------------------------------

HUD_MAIN = 690
HUD_SUB = 691
POPUP_BASE = 692        # 692..701 round-robin
POPUP_SLOTS = 10
SUPPLY_RING_BASE = 710  # 710..713
CLEAR_TEXT = 715
BOSS_RING = 716
BOSS_TITLE = 717
TRACKER_TEXT = 718
BOSS_BAR = 719
BOSS2_RING = 728
BOSS2_TITLE = 729
BOSS2_BAR = 731
COMPASS_TEXT = 732
DROP_RING_BASE = 720    # 720..727 short-lived loot glints
DROP_RING_SLOTS = 8
DEF_RING_BASE = 1000    # 1000..1023 rarity rings (champions/uniques)
DEF_RING_SLOTS = 24
NAME_TEXT_BASE = 1030   # 1030..1053 champion/unique name texts
BAR_TEXT_BASE = 1060    # 1060..1083 champion/unique health bars

BOSS_ID_SETS = ((BOSS_RING, BOSS_TITLE, BOSS_BAR),
                (BOSS2_RING, BOSS2_TITLE, BOSS2_BAR))
