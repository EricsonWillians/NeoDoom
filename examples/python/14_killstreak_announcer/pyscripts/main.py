"""Kill-streak combo meter with HUD milestones, sounds, and screen flashes.

Milestones are announced via bd.ui.announce (big outlined title, flavor
subtitle, color escalation); a small themed bd.ui panel tracks the current
and best streak.
"""

import biaseddoom as bd


STREAK_WINDOW = 3 * bd.TICRATE  # tics allowed between kills

# milestone -> (flavor subtitle, announcement color)
MILESTONES = {
    3: ("warming up", (255, 200, 80)),    # gold
    5: ("rampage", (255, 140, 40)),       # orange
    8: ("unstoppable", (235, 70, 60)),    # red
    10: ("GODLIKE", (235, 70, 60)),       # red
}

streak = 0
best_streak = 0
last_kill_tic = -STREAK_WINDOW
streak_panel = None  # bd.ui panel, recreated per map


def update_panel():
    """Refresh the persistent STREAK counter panel (created lazily so the
    first render happens in-level, where the HUD exists)."""
    global streak_panel
    if streak_panel is None:
        streak_panel = bd.ui.panel(x=0.02, y=0.02, w=0.22, title="STREAK")
    streak_panel.row("CURRENT", str(streak),
                     value_color=bd.ui.theme.accent if streak
                     else bd.ui.theme.dim)
    streak_panel.row("BEST", str(best_streak))


@bd.on("actor_died")
def count_kill(event):
    global streak, best_streak, last_kill_tic
    victim = event["actor"]  # snapshot dict of the dying actor
    if victim is None or victim["is_player"] or not victim["is_monster"]:
        return  # streaks only count monsters
    # The engine points the dying actor's target at its killer just before
    # this event fires; a live player target means a player-credited kill.
    victim_ref = event["actor_ref"]
    killer = victim_ref.target if victim_ref is not None and victim_ref.valid else None
    if killer is None or not killer.valid or not killer.is_player:
        return

    now = bd.level_time()
    if now - last_kill_tic > STREAK_WINDOW:
        streak = 0  # the combo window expired before this kill
    streak += 1
    last_kill_tic = now
    if streak > best_streak:
        best_streak = streak
    update_panel()

    milestone = MILESTONES.get(streak)
    if milestone is None:
        return
    flavor, color = milestone
    bd.ui.announce(f"{streak} KILL STREAK!", subtitle=flavor, color=color)
    bd.play_ui_sound("misc/chat")
    if streak >= 8:
        bd.screen_flash(255, 200, 50, 0.15)


def sweep_streak():
    """Repeating watchdog: reset the counter once the window ends."""
    global streak
    if streak > 0 and bd.level_time() - last_kill_tic > STREAK_WINDOW:
        streak = 0
        update_panel()
    return True  # keep repeating


@bd.on("map_load")
def map_loaded(event):
    global streak, last_kill_tic, streak_panel
    streak = 0
    last_kill_tic = -STREAK_WINDOW
    if streak_panel is not None:
        streak_panel.close()  # drop the previous map's panel
        streak_panel = None
    # Map-local so the sweeper is cancelled on unload and never duplicates.
    bd.schedule(sweep_streak, delay=5, repeat=5, map_local=True)
