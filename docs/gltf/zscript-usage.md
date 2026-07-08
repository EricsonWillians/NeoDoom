# BiasedDoom glTF ZScript Usage Guide

This guide shows how to use the `GLTFModel` ZScript mixin from a mod and how it
fits with MODELDEF. For a full third-person player example, start with the
[player replacement workflow](player-replacement-workflow.md).

## Recommended Pattern

Use MODELDEF to bind a glTF model to an actor, then use ZScript only for gameplay
state decisions such as choosing `Idle`, `Run`, `Fire`, or `Death`.

```text
Model BDMarinePlayer
{
    Path "models/players/marine"
    Model 0 "marine.gltf"
    Animation 0 "marine.gltf"
    Scale 1.0 1.0 1.0
    BaseFrame

    Frame PLAY A 0 "Idle"
    Frame PLAY B 0 "Run:0"
    Frame PLAY C 0 "Run:1"
}
```

```c
class BDMarinePlayer : DoomPlayer
{
    mixin GLTFModel;

    Default
    {
        +DECOUPLEDANIMATIONS
        Player.DisplayName "glTF Marine";
    }

    override void PostBeginPlay()
    {
        Super.PostBeginPlay();
        InitGLTFModel("models/players/marine/marine.gltf", 'BDMarinePlayer');
        PlayAnimation('Idle', true, 0.0);
    }

    override void Tick()
    {
        Super.Tick();

        if (health <= 0)
        {
            PlayAnimation('Death', false, 0.1);
        }
        else if (Vel.XY.Length() > 1.0)
        {
            PlayAnimation('Run', true, 0.12);
        }
        else
        {
            PlayAnimation('Idle', true, 0.12);
        }
    }
}
```

## Making The Mixin Visible To Your Mod

BiasedDoom includes the mixin at `zscript/models/gltf_model.zs` inside the game
PK3. If your mod defines its own `ZSCRIPT.zs`, include the mixin before actors
that use it:

```text
YourMod.pk3/
├── ZSCRIPT.zs
└── zscript/
    ├── gltf_model.zs
    └── marine_player.zs
```

```c
version "4.15"

#include "zscript/gltf_model.zs"
#include "zscript/marine_player.zs"
```

You can copy the engine mixin into an unpacked mod folder with:

```bash
./tools/copy_gltf_mixin_to_mod.sh /path/to/YourMod
```

If you are experimenting inside this source tree instead of building an external
PK3, you can include your actor from the engine-side `wadsrc/static/zscript.txt`
instead.

## Current Native Helper Status

These helpers drive the existing model animation path:

- `PlayAnimation(name, loop, blendTime)` starts the named glTF action through
  decoupled model animation.
- `StopAnimation()` clears the active model animation.
- `PauseAnimation()` sets the current animation framerate to zero.
- `SetAnimationSpeed(speed)` scales the active animation framerate.
- `GetCurrentAnimation()` returns the last animation requested through the
  mixin.

These helpers are available but still limited:

- `ResumeAnimation()` is currently a no-op because the mixin does not store the
  previous framerate yet.
- PBR convenience helpers such as `SetupPBRMetal()` and `PulseEmissive()` are
  API placeholders until renderer-side material control is finished.
- Procedural bone helper calls are exposed by the mixin API, but authoring
  workflows should treat them as experimental and test in-game.

## Export And Packaging Notes

- Prefer `.gltf + .bin + external textures` for textured assets. `.glb` loads
  geometry and animations, but embedded image texture decoding is still limited.
- Name Blender Actions exactly as you call them from ZScript: `Idle`, `Run`,
  `Fire`, `Pain`, `Death`, and so on.
- Keep skinned models under 256 bones for the current hardware model vertex
  format.
- Use relative paths under `models/...` so PK3 packaging and MODELDEF paths stay
  predictable.

## Quick Test

```bash
./build/biaseddoom -iwad doom2.wad -file YourMod.pk3 +developer 1 +map map01
```

Check the console for missing model, missing animation, or texture path warnings.
