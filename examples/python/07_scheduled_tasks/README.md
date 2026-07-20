# Scheduled Tasks

On each map this example starts:

- a repeating one-second heartbeat that returns `False` after five calls;
- a one-shot center message delayed by two seconds;
- a far-future task that is immediately cancelled by ID.

It logs task IDs and `bd.task_count()`. Tasks default to map-local and are
discarded when their map unloads.
