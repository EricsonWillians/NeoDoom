"""Seeded roguelike run: Diablo-2-style affixed monsters, depth-scaled
mutators, per-map uniques, XP levels and permadeath.

One run = one deterministic seed. Monsters roll named affixes ("Burning
Zombieman of the Bear") from a stream seeded by run seed + map salt +
spawn index: the affix tints the sprite, changes real attributes (health,
speed, damage dealt/taken, scale, alpha) and scales the XP bounty.
Champions (prefix + suffix) get a colored ground ring; each map crowns
gold-ringed UNIQUES worth x5 XP. Kill XP feeds player levels; map mutators
roll per map from the same seed. Death ends the run: a stats panel counts
down and reloads the 'roguelike' checkpoint (delete roguelike.zds from the
save directory to start a fresh run).

This file is only the bootstrap. The run is split into focused modules,
loaded in dependency order (each bd.import_script registers the module in
sys.modules, so siblings use plain `import`):

    config     every tunable in one place
    affixes    prefix/suffix tables, args[4] packing, titles
    runstate   bd.state accessors, deterministic streams, score/XP mutation
    mutators   per-map run modifiers
    hud        slim strip, stats panel, popups, scoring feedback events
    playerfx   player aura rings, level-up burst, affix damage feedback
    monsters   affix rolling/application, uniques, world visuals, kills
    deathloop  checkpoints, RUN ENDED panel, permadeath countdown

All HUD chrome is built on the embedded `bd.ui` toolkit plus the canvas
primitives, with resolution-independent height= text throughout.
"""

import biaseddoom as bd

bd.import_script("pyscripts/config.py", module_name="rogue_config")
bd.import_script("pyscripts/affixes.py", module_name="rogue_affixes")
bd.import_script("pyscripts/runstate.py", module_name="rogue_runstate")
bd.import_script("pyscripts/mutators.py", module_name="rogue_mutators")
bd.import_script("pyscripts/hud.py", module_name="rogue_hud")
bd.import_script("pyscripts/playerfx.py", module_name="rogue_playerfx")
bd.import_script("pyscripts/monsters.py", module_name="rogue_monsters")
bd.import_script("pyscripts/deathloop.py", module_name="rogue_deathloop")

import rogue_runstate as runstate


def on_engine_start(event):
    # Conventional startup callback, fires after every module is loaded.
    # Event handlers self-registered in the modules above (@bd.on); only
    # run-state defaults are left to install here.
    runstate.init_defaults()
