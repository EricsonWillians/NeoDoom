# Embedded Python Example Suite

These are small, independently packageable mods for BiasedDoom Python API v2.
They complement the exhaustive `hello_world` integration fixture by teaching
one idea at a time.

> Python mods are trusted native-equivalent code. Review them and launch with
> `-python`; merely loading a PK3 does not opt in.

| Example | Capability |
|---------|------------|
| [`01_lifecycle_events`](01_lifecycle_events/) | Startup, map, pre/tick/post, player, save/load, unload, shutdown |
| [`02_live_actor_handles`](02_live_actor_handles/) | GC-safe Actor handles, properties, relationships, flags, scheduled steering |
| [`03_combat_and_inventory`](03_combat_and_inventory/) | Inventory, healing, sounds, hitscan, missiles, direct/radius damage |
| [`04_player_controller`](04_player_controller/) | Pre-tick input override, button constants, FOV, native player filters |
| [`05_sectors_and_lines`](05_sectors_and_lines/) | Sector/Line handles, light, plane movement, line activation |
| [`06_filtered_events`](06_filtered_events/) | Class/TID/player filters, priorities, spawn/damage/death/destroy events |
| [`07_scheduled_tasks`](07_scheduled_tasks/) | One-shot/repeating tasks, self-cancel, explicit cancel, task counts |
| [`08_persistent_state`](08_persistent_state/) | Namespaced JSON state, save/load callbacks, reload-safe initialization |
| [`09_vfs_and_helpers`](09_vfs_and_helpers/) | Same-PK3 JSON, `read_text`, `import_script`, standard-library parsing |
| [`10_zscript_bridge`](10_zscript_bridge/) | Typed public ZScript methods, vectors/strings/actors, access rejection |
| [`11_batch_updates`](11_batch_updates/) | Lightweight handles, filtered ticks, bulk mutation, profiling |
| [`12_level_ui_audio`](12_level_ui_audio/) | Messages, sounds, music, action specials, console, level transitions |
| [`hello_world`](hello_world/) | Full lifecycle/savegame integration fixture used by CI-style testing |

## Build and run

Build every example:

```bash
./tools/build-python-examples.sh
```

Or select examples:

```bash
./tools/build-python-examples.sh 02_live_actor_handles 10_zscript_bridge
```

Then run one against a Doom II-compatible IWAD:

```bash
./build/biaseddoom -iwad /path/to/DOOM2.WAD \
    -file ./build/python-examples/02_live_actor_handles.pk3 \
    -python -stdout
```

Several interactive examples use the engine's `User1` through `User4` action
buttons. Bind those actions in Customize Controls before trying the indicated
keys. Examples that name `ZombieMan`, `Rocket`, `Clip`, or Doom sound/music
lumps are intentionally Doom II-oriented; the lifecycle, state, task, VFS,
and handle patterns themselves are game-independent.

Validate source and package structure with:

```bash
./tools/test-python-examples.sh
```

Pass `--iwad PATH` to additionally start MAP01 with every PK3 in a real engine
process, run its initial map/tick callbacks, and verify a clean Python-driven
exit.

See the [complete API guide](../../docs/scripting/python.md) for exact contracts,
security, performance budgets, packaging, and savegame behavior.
