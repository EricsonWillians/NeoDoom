"""Permadeath loop: checkpoints, the RUN ENDED panel and the reload
countdown. Death ends the run: a stats panel counts down and reloads the
'roguelike' checkpoint (delete roguelike.zds from the save directory to
start a fresh run).
"""

import biaseddoom as bd

import rogue_config as config
import rogue_runstate as runstate
import rogue_hud as hud


run_ended = False
death_task = None
death_countdown = 0
death_panel = None


def cancel_death_task():
    global death_task
    if death_task is not None:
        try:
            bd.cancel_task(death_task)
        except RuntimeError:
            pass
        death_task = None


def clear_death_panel():
    global death_panel
    if death_panel is not None:
        death_panel.close()
        death_panel = None
    hud.safe_draw(bd.draw_clear, config.DEATH_BG)


def save_run_checkpoint():
    bd.save_checkpoint(config.CHECKPOINT_NAME,
                       f"depth {runstate.depth()} entering "
                       f"{bd.current_map() or 'unknown'}")
    bd.ui.toast("CHECKPOINT SAVED", color=bd.ui.theme.good)
    bd.play_ui_sound("misc/chat", volume=0.5)


@bd.on("map_load")
def map_loaded(event):
    global run_ended
    run_ended = False
    cancel_death_task()
    clear_death_panel()
    bd.schedule(save_run_checkpoint, delay=5)


@bd.on("map_unload")
def map_unloaded(event):
    # Map-clear bonus (skipped when the unload is a death reload).
    if not run_ended:
        bonus = runstate.add_score(
            int(config.SCORE_PER_MAP_CLEAR * runstate.multiplier()))
        bd.log(f"run: map clear, score {bonus}")
        hud.update()


def draw_countdown(text=None, color=None):
    if death_panel is None:
        return
    death_panel.row("COUNTDOWN",
                    text or f"LOADING CHECKPOINT IN {death_countdown}",
                    value_color=color or bd.ui.theme.gold)


@bd.on("player_died")
def player_died(event):
    global run_ended, death_task, death_countdown, death_panel
    if run_ended:
        return
    run_ended = True
    bd.state["deaths"] = bd.state.get("deaths", 0) + 1
    total = runstate.score()
    if total > bd.state.get("best_score", 0):
        bd.state["best_score"] = total
    bd.screen_fade(255, 0, 0, 0.5, 2.5)
    hud.safe_draw(bd.draw_rect, id=config.DEATH_BG, x=0.0, y=0.0, w=1.0,
                  h=1.0, color=(25, 0, 0), color2=(0, 0, 0), alpha=0.7,
                  layer=0)
    death_panel = bd.ui.panel(x=0.33, y=0.28, w=0.34, title="RUN ENDED")
    death_panel.row("SCORE", f"{total}   BEST {bd.state['best_score']}",
                    value_color=bd.ui.theme.gold)
    death_panel.row("LEVEL", f"{runstate.level()}   "
                             f"DEPTH {runstate.depth()}")
    death_panel.row("KILLS", f"{bd.state.get('kills', 0)}   "
                             f"UNIQUES {bd.state.get('uniques', 0)}")
    death_panel.row("SECRETS", f"{bd.state.get('secrets', 0)}   "
                               f"DEATHS {bd.state['deaths']}")
    death_countdown = config.DEATH_COUNTDOWN_SECONDS
    draw_countdown()
    death_task = bd.schedule(countdown_tick, delay=bd.TICRATE,
                             repeat=bd.TICRATE)


def countdown_tick():
    global death_task, death_countdown
    death_countdown -= 1
    if death_countdown > 0:
        draw_countdown()
        return True  # keep repeating
    death_task = None
    try:
        bd.load_checkpoint(config.CHECKPOINT_NAME)
    except (FileNotFoundError, RuntimeError):
        draw_countdown("NO CHECKPOINT - RESTART THE MAP", bd.ui.theme.bad)
    return False  # one-shot


@bd.on("player_respawned")
def player_respawned(event):
    global run_ended
    run_ended = False
    cancel_death_task()
    clear_death_panel()
