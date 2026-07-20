# Python Hello World / Integration Fixture

This is a playable embedded-Python example and the fixture used by
`tools/test-python-scripting.sh`.

Build and run it:

```bash
./tools/build-python-example.sh
./build/biaseddoom -iwad /path/to/DOOM2.WAD \
    -file ./build/python-hello-world.pk3 -python -stdout
```

The root `PYTHON` manifest loads `pyscripts/main.py`. That entry imports
`pyscripts/helper.py` from the same PK3, registers all lifecycle callbacks, reads
players/actors/CVars, and demonstrates JSON-persisted `biaseddoom.state`.

`pyscripts/autotest_failure.py` is imported only when the repository harness sets
its test environment variable. It deliberately registers a callback and then
raises, proving that a failed helper import rolls its callbacks back.

The root `ZSCRIPT` file defines a harmless marker and a `PythonBridgeProbe`
actor. The test invokes the probe's public methods through `Actor.call_zscript`,
including primitive, vector, string, and actor values, and verifies that a
private method remains inaccessible. Normal compiled ACS content can coexist
in the same package in the same way.

Autotest mode also schedules a one-shot task and deliberately overruns a 1 ms
tick budget once. The fixture verifies that later work is skipped for that
tic, the configured repeat offender is disabled, and normal work resumes.

The `BIASEDDOOM_PYTHON_*AUTOTEST` environment variables in `main.py` are for the
repository integration harness. Without them, the example does not spawn its
test monster, save/load automatically, or quit the game.

See [the complete Python guide](../../../docs/scripting/python.md) for the
security model, manifest rules, callback and API reference, ACS/ZScript
interoperability, and test procedures.
