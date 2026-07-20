# Live Actor Handles

After map entry this example spawns a friendly `ZombieMan`, retains its live
`Actor` handle, changes fields and relationships, reads a snapshot, and steers
it for five seconds from a repeating scheduled task.

The handle is rooted through the engine GC while Python owns it and is released
on map unload. Live handles are for transient runtime control; save TIDs or
plain data in `bd.state`, never handle objects.
