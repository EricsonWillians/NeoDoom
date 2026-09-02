"""Endless horde mode: quota waves, map-native spawns, loot, bosses.

A true endless experience on EVERY map: on arrival the mode commandeers
the map itself, capturing every designer-placed monster position as its
spawn pool and removing the originals (destroyed outright — no carcasses;
boss-death map specials still fire later, when same-class horde monsters
die). Waves spawn from that pool with fit-checked placement, so nothing
ever materializes inside walls or the void. Monster-less maps fall back
to a player-relative spawn ring. Every map is a FRESH RUN: wave, score
and progression reset on any level change (exit, warp, savegame,
procedural maps) — only your best wave/score records persist.

Monsters have RARITY, Diablo-2-style, with Doom-lore names: champions
("Molten Doom Imp of the Pit") roll per spawn at a wave-scaled chance
from 14 prefixes x 12 suffixes, and uniques are crowned from every wave
from wave 4 (two from wave 12) — half of them hand-crafted NAMED
UNIQUES (The Maledict, Babelspawn, The Broodmatron, Gatekeeper of Dis,
The Argent Husk). Affixes carry real stat mods AND real mechanics:
Molten/of Detonation monsters explode on death, Brood monsters birth
two lesser defenders from their corpse, Leeching monsters heal from the
damage they deal, Barbed monsters reflect damage at their attacker, and
Hoarding monsters drop double loot. All of it multiplies on top of a
per-wave difficulty curve (health +6%/wave uncapped, speed and damage
dealt ramping with caps) and per-wave MUTATORS (Frenzy, Glass Cannons,
Fortified, Spectral Host, Swarm) announced in the wave banner — stacking
to two from wave 20 and three from wave 30, so the deep game keeps
changing. The roster widens with depth (Lost Souls, Chaingunners, Pain
Elementals — whose summoned souls join the wave marked), boss waves go
twin from wave 20, milestone waves pay double every 10th, and the quota
keeps growing to 40. Kills feed the economy: normal kills drop minor
pickups at a wave-scaled chance (plus a lucky hit whose window re-rolls
per kill — 1-10% — with 15% jackpots into the major table, toasted in
gold), champions and uniques can drop the best weapon you're missing
off the unlock ladder (Shotgun w2 ... BFG9000 w14) and otherwise always
drop medium/major loot (Backpack w10, Soulsphere w12, powerups from w8:
Berserk, BlurSphere, BlueArmor, InvulnerabilitySphere); bosses burst
into a weapon or a Soulsphere (Megasphere from wave 15), plus a Medikit
and an ammo restock. On top of all physical drops, EVERY marked kill
feeds ammo straight into your pool, matched to the weapons you own and
growing with the wave — the endless ration that keeps even MAP01's bare
armory sustainable. Loot lands at floor level even when a flier dies in
the sky, and ammo only drops for weapons you actually own. All rolls run
on entropy-mixed streams fed by kill timing: same map, same wave,
different run.

The visual language stays readable: tint marks membership (crimson =
rank-and-file, affix colors = champions, gold = uniques), rare monsters
carry an overhead health bar hugging the head (manual-fraction — it
divides by the monster's real buffed max health, not the class default)
and a readable floating name whose height above the bar adapts to your
distance, so the two never overlap; loot glints gold briefly. All world
visuals stay sight-occluded — nothing shines through walls. Finding the
horde is intuitive: a compass needle under the HUD always points at the
nearest defender, and when only the last few remain, a motion tracker
calls out bearing and distance to each. Champion and unique kills toast
and sound off. Death shows OVERWHELMED and your best; respawn restarts
at wave 1 with a clean arena.

Loop: every wave has a KILL QUOTA and a CONCURRENT CAP scaled to the
pool; a trickle task replaces fallen defenders at the map's own spawn
places until the quota is exhausted. Clearing the wave earns a 5s
breather and a supply drop near you (3-5 items, always including the
best weapon you're missing, and bullet boxes to keep the ammo economy
alive even on weaponless maps). Every 5th wave is a BOSS wave: a tinted, scaled boss
with a Doom epithet ("Baron of Hell the Defiler"), a big distance-
adaptive health bar and
a trash escort, entering at the farthest spawn point — twin bosses from
wave 20 — and archvile bosses can raise fallen defenders, which re-join
the wave with their affix identity intact. A straggler sweep relocates
far-away, pit-fallen or stuck defenders to another pool point — a point
whose spawns end up stuck twice is retired, so sealed teleport closets
eliminate themselves.

This file is only the bootstrap. The mode is split into focused modules,
loaded in dependency order (each bd.import_script registers the module in
sys.modules, so siblings use plain `import`):

    config     every tunable and table in one place
    runstate   run state, bd.state accessors, entropy streams, guards
    affixes    Doom-lore naming and the combat stat math
    visuals    HUD, popups, rarity rings/bars/names, tracker, compass
    monsters   spawn pool, spawning/marking/crowning, tasks, actor events
    loot       drop tables, weapon ladder, smart ammo, supply drops
    waves      the wave state machine, mutators, bosses, milestones
    lifecycle  fresh-run-per-map reset, player death/respawn

Teaches: modular script architecture with bd.import_script, bd.spawn at
scale with map-authored placement, apply_actor_batch stat scaling /
tinting / batch teleports / batch clears, actor_refs enumeration,
Diablo-style affix records with ratio-safe upgrades, rarity-driven
visuals with id pooling and manual-fraction health bars, loot tables,
handle-set membership tracking (including actor_revived re-enlistment and
actor_spawned summons), scheduled state machines with guards against
stale/duplicate tasks, persistent bd.state, and update-on-change HUD.
"""

import biaseddoom as bd

bd.import_script("pyscripts/config.py", module_name="horde_config")
bd.import_script("pyscripts/runstate.py", module_name="horde_runstate")
bd.import_script("pyscripts/affixes.py", module_name="horde_affixes")
bd.import_script("pyscripts/visuals.py", module_name="horde_visuals")
bd.import_script("pyscripts/loot.py", module_name="horde_loot")
bd.import_script("pyscripts/monsters.py", module_name="horde_monsters")
bd.import_script("pyscripts/waves.py", module_name="horde_waves")
bd.import_script("pyscripts/lifecycle.py", module_name="horde_lifecycle")

import horde_runstate as runstate


def on_engine_start(event):
    # Conventional startup callback, fires after every module is loaded.
    # Event handlers self-registered in the modules above (@bd.on). Here:
    # run-state defaults, and the hell palette retune of the ui theme.
    runstate.init_defaults()
    theme = bd.ui.theme
    theme.accent = (255, 88, 48)     # blood-orange HUD accents
    theme.warn = (255, 150, 40)      # ember warnings
    theme.gold = (255, 190, 60)      # ember-gold loot and bests
