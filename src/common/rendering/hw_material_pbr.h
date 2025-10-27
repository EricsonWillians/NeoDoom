#pragma once

#include "hw_material.h"
#include "vectors.h"

//===========================================================================
//
// PBR Material Extensions for Hardware Rendering
//
//===========================================================================

enum class PBRShaderMode
{
    None = 0,           // Traditional rendering
    MetallicRoughness,  // glTF 2.0 metallic-roughness workflow
    SpecularGlossiness  // Legacy PBR workflow (for future extension)
};

struct PBRTextureSlots
{
    static constexpr int BaseColor = 0;         // Albedo/Diffuse
    static constexpr int MetallicRoughness = 1; // Metallic(B) + Roughness(G)
    static constexpr int Normal = 2;            // Normal map
    static constexpr int Occlusion = 3;         // Ambient occlusion
    static constexpr int Emissive = 4;          // Self-illumination
    static constexpr int MaxPBRTextures = 5;
};

struct PBRMaterialUniforms
{
    FVector4 baseColorFactor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    FVector4 emissiveFactor = FVector4(0.0f, 0.0f, 0.0f, 1.0f); // w unused
    FVector4 pbrFactors = FVector4(1.0f, 1.0f, 1.0f, 0.5f);     // metallic, roughness, normalScale, alphaCutoff
    FVector4 textureTransforms[PBRTextureSlots::MaxPBRTextures]; // UV scale/offset for each texture

    uint32_t flags = 0;

    // Flag bits
    static constexpr uint32_t HasBaseColorTexture = 1 << 0;
    static constexpr uint32_t HasMetallicRoughnessTexture = 1 << 1;
    static constexpr uint32_t HasNormalTexture = 1 << 2;
    static constexpr uint32_t HasOcclusionTexture = 1 << 3;
    static constexpr uint32_t HasEmissiveTexture = 1 << 4;
    static constexpr uint32_t DoubleSided = 1 << 5;
    static constexpr uint32_t AlphaTest = 1 << 6;
    static constexpr uint32_t AlphaBlend = 1 << 7;
};

//===========================================================================
//
// FPBRMaterial - Extended material class for PBR rendering
//
//===========================================================================

class FPBRMaterial : public FMaterial
{
private:
    PBRShaderMode pbrMode = PBRShaderMode::None;
    PBRMaterialUniforms pbrUniforms;
    FGameTexture* pbrTextures[PBRTextureSlots::MaxPBRTextures];
    bool isDirty = true;

public:
    FPBRMaterial(FGameTexture* tex, int scaleflags, PBRShaderMode mode = PBRShaderMode::None);
    virtual ~FPBRMaterial() = default;

    // PBR-specific interface
    void SetPBRMode(PBRShaderMode mode);
    PBRShaderMode GetPBRMode() const { return pbrMode; }

    void SetBaseColorFactor(const FVector4& color);
    void SetMetallicFactor(float metallic);
    void SetRoughnessFactor(float roughness);
    void SetNormalScale(float scale);
    void SetEmissiveFactor(const FVector3& emissive);
    void SetAlphaCutoff(float cutoff);
    void SetDoubleSided(bool enabled);

    void SetPBRTexture(int slot, FGameTexture* texture, const FVector4& transform = FVector4(1,1,0,0));
    FGameTexture* GetPBRTexture(int slot) const;

    const PBRMaterialUniforms& GetPBRUniforms();
    bool HasPBRTextures() const;
    bool IsPBRMaterial() const { return pbrMode != PBRShaderMode::None; }

    // Override base material functionality
    virtual void SetMaterialTextures(class FRenderState& state);
    virtual void SetMaterialShader(class FRenderState& state);

private:
    void UpdateUniforms();
    void ValidateTextures();
};

//===========================================================================
//
// Global PBR Functions
//
//===========================================================================

// Create PBR material from glTF material properties
FPBRMaterial* CreatePBRMaterial(FGameTexture* baseTexture,
                               const struct PBRMaterialProperties& props,
                               const TArray<FGameTexture*>& textures);

// Register PBR shaders with the renderer
void RegisterPBRShaders();

// Check if hardware supports PBR rendering
bool IsPBRRenderingSupported();