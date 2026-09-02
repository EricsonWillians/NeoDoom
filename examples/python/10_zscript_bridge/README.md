# Typed ZScript Bridge

Spawn a custom ZScript actor and call its public methods synchronously from
Python, with every returned value displayed on the HUD.

`ZSCRIPT` defines a `PythonExampleBridgeActor` whose public methods accept and
return integers, vectors, strings, and actors. Python spawns the class and
invokes those methods through `Actor.call_zscript`; arguments and results
cross the language boundary with their types intact.

## Try it

Load any map. A zombie appears beside you and a `ZSCRIPT BRIDGE` panel lists
what came back across the bridge: the configured health, the velocity vector,
the owner-actor identity check, the echoed string, and the confirmation that
the private method was rejected — plus a chat sound when the calls complete.

## What it demonstrates

- `Actor.call_zscript` round-tripping `int`, `Vector3`, `Actor`, and `str`.
- Passing a live Python `Actor` handle into ZScript (`master = newOwner`) and
  getting the same actor back (`CurrentOwner is your pawn -> True`).
- Access control: the private `HiddenMethod` raises `PermissionError`.
- Scheduling the demo one tic after `map_load` so a player pawn exists.

## Notes

- The ZSCRIPT side needs no glue: any public, supported method is callable.
- The bridge also rejects actions, statics, UI/unsafe methods, refs/out
  params, arbitrary objects, containers, and multi-return signatures.
