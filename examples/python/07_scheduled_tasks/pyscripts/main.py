"""Scheduled tasks you can see and hear: a live countdown HUD plus sounds.

Concept: bd.schedule (one-shot and repeating), self-cancellation by return
value, bd.cancel_task, and bd.task_count.

Try it: run any map and watch the countdown panel (top-left) — a heartbeat sounds
five times, one per second, and a center message lands exactly two seconds in.
"""

import biaseddoom as bd


HEARTBEAT_PERIOD = bd.TICRATE  # one heartbeat per second
HEARTBEAT_LIMIT = 5  # beats before the heartbeat self-cancels
MESSAGE_DELAY = 2 * bd.TICRATE  # the one-shot message lands two seconds in

HELP_HUD_ID = 2

heartbeat_count = 0
next_fires = {}  # task name -> tic of its next run; drives the countdown panel
countdown_panel = None  # bd.ui panel, created on first refresh


def hud(text, **kwargs):
    """hud_text needs an active status bar; guard it like the draw_* calls."""
    try:
        bd.hud_text(text, **kwargs)
    except RuntimeError:
        pass


def refresh_countdown():
    """Recompute the 'next task in N tics' row from next_fires. Called by the
    tasks themselves whenever their schedule changes."""
    global countdown_panel
    if countdown_panel is None:
        countdown_panel = bd.ui.panel(x=0.02, y=0.02, w=0.30, title="TASKS")
    if not next_fires:
        countdown_panel.row("NEXT", "idle", value_color=bd.ui.theme.dim)
    else:
        name, due = min(next_fires.items(), key=lambda item: item[1])
        remaining = max(0, due - bd.level_time())
        countdown_panel.row("NEXT", f"{name} in {remaining} tics")
    countdown_panel.row("ACTIVE", str(bd.task_count()))


def heartbeat():
    """Repeating task: returning True keeps it alive, False self-cancels it."""
    global heartbeat_count
    heartbeat_count += 1
    bd.play_ui_sound("misc/chat", volume=0.6)  # hear each repeating fire
    bd.log(f"tasks: heartbeat {heartbeat_count}; active={bd.task_count()}")
    if heartbeat_count < HEARTBEAT_LIMIT:
        next_fires["heartbeat"] = bd.level_time() + HEARTBEAT_PERIOD
        refresh_countdown()
        return True
    next_fires.pop("heartbeat", None)
    refresh_countdown()
    return False


def delayed_message():
    """One-shot task: scheduled once with a delay, runs exactly once."""
    bd.center_message("This message was scheduled two seconds ago")
    next_fires.pop("message", None)
    refresh_countdown()


@bd.on("map_load")
def start_tasks(event):
    global heartbeat_count
    heartbeat_count = 0
    next_fires.clear()

    heartbeat_id = bd.schedule(heartbeat, delay=1, repeat=HEARTBEAT_PERIOD)
    next_fires["heartbeat"] = bd.level_time() + 1
    message_id = bd.schedule(delayed_message, delay=MESSAGE_DELAY)
    next_fires["message"] = bd.level_time() + MESSAGE_DELAY

    # A far-future task cancelled immediately by ID: cancel_task is the
    # explicit counterpart to self-cancellation by return value.
    cancelled_id = bd.schedule(lambda: bd.log("tasks: this must not run"), delay=9999)
    bd.cancel_task(cancelled_id)

    bd.log(
        f"tasks: heartbeat={heartbeat_id} message={message_id} "
        f"cancelled={cancelled_id} active={bd.task_count()}"
    )
    refresh_countdown()
    hud("Watch the TASKS panel (top-left): tasks fire on a schedule",
        id=HELP_HUD_ID, x=0.5, y=0.16, color="cyan", hold=4.0, fade=1.0)
