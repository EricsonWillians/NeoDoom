# glTF Player Replacement Workflow

This workflow is for Blender-authored `.gltf`/`.glb` player models that should
replace the classic player sprite in third-person while still following the
normal Doom actor-state and MODELDEF animation model.

## Blender export rules

- Put each gameplay motion in a named Blender Action: `Idle`, `Run`, `Pain`,
  `Death`, `Fire`, etc.
- Export as glTF 2.0 with skinning, animations, normals, and tangents.
- Prefer `.gltf + .bin + external textures` for textured player models. `.glb`
  works for geometry and animation, but embedded image texture decoding is still
  limited.
- Keep the armature under 256 bones for the current hardware model vertex format.
- Use stable bone names if ZScript bone overrides or attachments will target
  them later.

## PK3 layout

```text
models/players/marine/marine.gltf
models/players/marine/marine.bin
models/players/marine/textures/marine_basecolor.png
MODELDEF
ZSCRIPT
```

## MODELDEF

Every actor that uses decoupled model animation still needs a MODELDEF base
frame. For glTF, `Model` and `Animation` can point at the same `.gltf` when the
Blender actions are embedded in that file.

```text
Model BDMarinePlayer
{
    Path "models/players/marine"
    Model 0 "marine.gltf"
    Animation 0 "marine.gltf"
    Scale 1.0 1.0 1.0
    Offset 0 0 0
    BaseFrame

    // Classic sprite-state mapping still works.
    // "Run:3" means sample frame 3 from the Blender action named "Run".
    Frame PLAY A 0 "Idle"
    Frame PLAY B 0 "Idle:1"
    Frame PLAY C 0 "Run:0"
    Frame PLAY D 0 "Run:1"
    Frame PLAY E 0 "Run:2"
    Frame PLAY F 0 "Run:3"
}
```

## ZScript player pattern

`GLTFModel.InitGLTFModel` accepts a full model path, splits it into MODELDEF's
`modelpath` and `model` pieces, and uses the same glTF file for animation unless
you pass a separate animation model name.

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

## What the engine does

- glTF animation names are exposed through the same `FModel` calls as IQM:
  `FindFrame`, `FindFirstFrame`, `FindLastFrame`, and `FindFramerate`.
- The loader samples each Blender action into model frames, so MODELDEF can use
  `"ActionName"` or `"ActionName:offset"` just like traditional named frames.
- Skeletal poses are calculated through `CalculateBones`, which means actor
  state timing, `SetAnimation`, interpolation, bone overrides, and rendering use
  one shared animation path.
- `JOINTS_0` is decoded as four real joint indices, including Blender's common
  unsigned-byte and unsigned-short layouts.
