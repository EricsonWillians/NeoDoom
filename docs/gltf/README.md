# glTF Modding Docs

These guides are for mod authors working with Blender, glTF assets, MODELDEF,
and ZScript.

## Fastest Path

1. [Quick start](quick-start.md)
2. [Blender authoring](blender-authoring.md)
3. [Player replacement workflow](player-replacement-workflow.md)
4. [ZScript usage](zscript-usage.md)

## By Task

- New to the pipeline: [beginner tutorial](beginner-tutorial.md)
- Need reliable Blender export settings: [blender authoring](blender-authoring.md)
- Replacing the player model: [player replacement workflow](player-replacement-workflow.md)
- Want a longer player walkthrough: [Blender player replacement tutorial](blender-player-replacement-tutorial.md)
- Need script/API details: [ZScript usage](zscript-usage.md), then [ZScript API](zscript-api.md)
- Generating starter files: [glTF tools](tools.md)
- Building a larger asset pipeline: [workflow reference](workflow-reference.md) and [production workflow guide](production-workflow-guide.md)
- Reviewing recent glTF improvements: [v2 improvements](v2-improvements.md)

## Export Rule

Use `.gltf + .bin + external textures` for textured assets. `.glb` can load
geometry and animation, but external textures are the least surprising path in
current BiasedDoom builds.
