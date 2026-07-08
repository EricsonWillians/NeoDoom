/*
** gltf_model.zs
**
** Minimal glTF 2.0 model support for ZScript 4.5
** Provides access to native glTF implementation
**
**---------------------------------------------------------------------------
**
** Copyright 2025 BiasedDoom Contributors
** All rights reserved.
**
*/

// ============================================================================
// GLTFModel - Actor mixin for glTF model support
// ============================================================================

mixin class GLTFModel
{
    // Runtime state
    private bool modelInitialized;
    private String currentModelPath;
    private Name currentAnimationName;
    private double lastUpdateTime;

    // ========================================================================
    // Model Initialization
    // ========================================================================

    /// Initialize glTF model with a full path such as "models/player/marine.glb".
    /// A MODELDEF BaseFrame for this actor or modelDef is still required.
    bool InitGLTFModel(String modelPath, Name modelDef = '', int modelIndex = 0,
                       Name skin = '', Name animationModel = '')
    {
        if (modelInitialized)
        {
            return true;
        }

        if (modelPath.Length() == 0)
        {
            return false;
        }

        int slash = modelPath.RightIndexOf("/");

        String modelDir = "";
        String modelFile = modelPath;
        if (slash >= 0)
        {
            modelDir = modelPath.Left(slash + 1);
            modelFile = modelPath.Mid(slash + 1);
        }

        currentModelPath = modelPath;
        modelInitialized = true;
        lastUpdateTime = level.time;

        if (animationModel == '')
        {
            animationModel = Name(modelFile);
        }

        A_ChangeModel(modelDef, modelIndex, modelDir, Name(modelFile), 0, "",
                      skin, 0, -1, modelIndex, modelDir, animationModel);
        return true;
    }

    /// Check if model is initialized
    bool HasGLTFModel()
    {
        return modelInitialized;
    }

    // ========================================================================
    // Transform Control
    // ========================================================================

    /// Set uniform model scale
    void SetModelScaleUniform(double s)
    {
        if (!HasGLTFModel()) return;
        // Scale will be applied via actor scale property
        A_SetScale(s, s);
    }

    /// Set model offset from actor origin
    void SetModelOffset(double x, double y, double z)
    {
        if (!HasGLTFModel()) return;
        // Offset handled by renderer
    }

    // ========================================================================
    // Animation Control
    // ========================================================================

    /// Play animation by name
    bool PlayAnimation(Name animName, bool loop = true, double blendTime = 0.2)
    {
        if (!HasGLTFModel())
        {
            return false;
        }

        if (animName == '')
        {
            return false;
        }

        if (currentAnimationName == animName)
        {
            return true;
        }

        currentAnimationName = animName;
        GLTF_PlayAnimation(animName, loop, blendTime);
        return true;
    }

    /// Stop current animation
    void StopAnimation()
    {
        if (!HasGLTFModel()) return;
        GLTF_StopAnimation();
    }

    /// Pause animation
    void PauseAnimation()
    {
        if (!HasGLTFModel()) return;
        GLTF_PauseAnimation();
    }

    /// Resume paused animation
    void ResumeAnimation()
    {
        if (!HasGLTFModel()) return;
        GLTF_ResumeAnimation();
    }

    /// Set animation playback speed
    void SetAnimationSpeed(double speed)
    {
        if (!HasGLTFModel()) return;
        GLTF_SetAnimationSpeed(speed);
    }

    /// Get current animation name
    Name GetCurrentAnimation()
    {
        return currentAnimationName;
    }

    // ========================================================================
    // Update and Rendering
    // ========================================================================

    /// Update model state (call every tic)
    void UpdateGLTFModel()
    {
        if (!HasGLTFModel()) return;

        double deltaTime = (level.time - lastUpdateTime) / 35.0; // Convert tics to seconds
        lastUpdateTime = level.time;

        // Notify native code to update
        GLTF_UpdateModel(deltaTime);
    }

    // ========================================================================
    // PBR Material Control
    // ========================================================================

    /// Enable/disable PBR rendering
    void SetPBREnabled(bool enable)
    {
        if (!HasGLTFModel()) return;
        GLTF_SetPBREnabled(enable);
    }

    /// Set metallic factor (0.0 = dielectric, 1.0 = metal)
    void SetMetallicFactor(double metallic)
    {
        if (!HasGLTFModel()) return;
        GLTF_SetMetallicFactor(metallic);
    }

    /// Set roughness factor (0.0 = smooth, 1.0 = rough)
    void SetRoughnessFactor(double roughness)
    {
        if (!HasGLTFModel()) return;
        GLTF_SetRoughnessFactor(roughness);
    }

    /// Set emissive color and strength
    void SetEmissive(Color color, double strength = 1.0)
    {
        if (!HasGLTFModel()) return;
        GLTF_SetEmissive(color, strength);
    }
}
