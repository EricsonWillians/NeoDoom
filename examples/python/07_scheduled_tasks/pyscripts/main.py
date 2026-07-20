"""One-shot, repeating, self-cancelling, and explicitly cancelled tasks."""

import biaseddoom as bd


heartbeat_count = 0


def heartbeat():
    global heartbeat_count
    heartbeat_count += 1
    bd.log(f"tasks: heartbeat {heartbeat_count}; active={bd.task_count()}")
    return heartbeat_count < 5


def delayed_message():
    bd.center_message("This message was scheduled two seconds ago")


@bd.on("map_load")
def start_tasks(event):
    global heartbeat_count
    heartbeat_count = 0
    heartbeat_id = bd.schedule(heartbeat, delay=1, repeat=bd.TICRATE)
    message_id = bd.schedule(delayed_message, delay=2 * bd.TICRATE)
    cancelled_id = bd.schedule(lambda: bd.log("tasks: this must not run"), delay=9999)
    cancelled = bd.cancel_task(cancelled_id)
    bd.log(
        f"tasks: heartbeat={heartbeat_id} message={message_id} "
        f"explicit_cancel={cancelled} active={bd.task_count()}"
    )
