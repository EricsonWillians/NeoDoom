"""Lifecycle: every map — including procedurally generated ones — starts a
completely fresh run (wave, score, arena pool all reset; only the best
wave/score RECORDS persist in bd.state across maps and sessions). Also
player death/respawn handling.
"""

import biaseddoom as bd

import horde_config as config
import horde_monsters as monsters
import horde_runstate as runstate
import horde_visuals as visuals
import horde_waves as waves


def reset_attempt():
    # Sweep leftovers from the previous attempt so players get a clean
    # arena instead of deep-wave veterans camping the spawn.
    leftovers = [ref for ref in runstate.defenders if ref.valid
                 and ref.alive]
    runstate.defenders = set()
    runstate.defender_info.clear()
    visuals.free_all_rings()
    if leftovers:
        runstate.safe_call(bd.apply_actor_batch,
                           [("destroy", ref) for ref in leftovers])
    runstate.wave = 0
    runstate.quota_left = 0
    runstate.uniques_pending = 0
    runstate.mutators = ()
    runstate.bosses.clear()
    runstate.phase = "idle"
    runstate.breather_left = 0
    runstate.supply_count = 0
    runstate.kill_counter = 0
    bd.state["hd_score"] = 0
    visuals.reset_caches()
    for canvas_id in (config.HUD_MAIN, config.HUD_SUB, config.CLEAR_TEXT,
                      config.TRACKER_TEXT, config.COMPASS_TEXT,
                      config.BOSS_RING, config.BOSS_TITLE, config.BOSS_BAR,
                      config.BOSS2_RING, config.BOSS2_TITLE,
                      config.BOSS2_BAR):
        runstate.safe_call(bd.draw_clear, canvas_id)
    visuals.update_tracker()


@bd.on("map_load")
def map_loaded(event):
    # A fresh arena and a fresh run, always — regardless of how we got
    # here (exit, warp, savegame, procedural map).
    reset_attempt()
    bd.schedule(waves.start_wave, delay=config.FIRST_WAVE_TICS)
    monsters.capture_spawn_points()


@bd.on("player_died")
def player_died(event):
    runstate.phase = "idle"
    runstate.save_best_score()
    bd.screen_fade(255, 0, 0, 0.5, 2.5)
    bd.ui.announce("OVERWHELMED",
                   subtitle=f"wave {runstate.wave}  ·  "
                            f"best {runstate.best_wave()}  ·  "
                            f"score {runstate.score()}",
                   color=bd.ui.theme.bad, duration=3.5)


@bd.on("player_respawned")
def player_respawned(event):
    reset_attempt()
    bd.schedule(waves.start_wave, delay=config.FIRST_WAVE_TICS)
