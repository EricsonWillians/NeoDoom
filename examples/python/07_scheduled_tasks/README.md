# Scheduled Tasks

One-shot, repeating, self-cancelling, and cancelled tasks — with a live countdown.

On each map this example starts a repeating one-second heartbeat that plays a
UI sound and self-cancels after five beats, a one-shot center message delayed
by two seconds, and a far-future task that is cancelled by ID before it can
run. The tasks themselves keep a top-left `bd.ui` countdown panel showing the
next task's remaining tics and the active task count.

## Try it

1. Run any map with this mod loaded.
2. Watch the TASKS panel (top-left) switch between `message` and
   `heartbeat` as their due tics count down.
3. Listen: each heartbeat fires a UI sound, five in total, then stops on its
   own.
4. Exactly two seconds in, the scheduled center message appears; afterwards
   the countdown reports the queue is idle.

## What it demonstrates

- `bd.schedule(cb, delay=N)` for one-shots and `repeat=M` for repeaters;
- returning `True`/`False` from a repeating task to continue or self-cancel;
- `bd.cancel_task(id)` for explicit cancellation by task ID;
- `bd.task_count()` and `bd.level_time()`/`bd.TICRATE` for tic arithmetic;
- a `bd.ui.panel` countdown and `bd.play_ui_sound` as task feedback.

## Notes

Task IDs and counts are logged for cross-checking. Tasks default to map-local
and are discarded when their map unloads.
