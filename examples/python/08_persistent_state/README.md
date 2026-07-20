# Persistent State

This example stores map visits, elapsed seconds, and recent map names under a
unique key in `bd.state`. The dictionary is JSON-serialized into savegames and
survives `py_reload`.

`setdefault` runs at engine start because reload restores old state only after
fresh modules initialize. Save/load callbacks log the exact data being written
and restored.
