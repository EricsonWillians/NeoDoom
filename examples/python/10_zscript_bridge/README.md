# Typed ZScript Bridge

`ZSCRIPT` defines a `PythonExampleBridgeActor` with public methods accepting
and returning integers, vectors, strings, and actors. Python spawns the class
and invokes those methods synchronously through `Actor.call_zscript`.

It then attempts a private method and verifies `PermissionError`. The bridge
also rejects actions, statics, UI/unsafe methods, refs/out params, arbitrary
objects, containers, and multi-return signatures.
