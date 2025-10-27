//
//---------------------------------------------------------------------------
//
// Copyright(C) 2025 NeoDoom Team
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//
/*
** model_gltf_render.cpp
**
** Vertex buffer and rendering implementation for glTF models
**
**/

#include "model_gltf.h"

#ifdef NEODOOM_GLTF_SUPPORT

#include "printf.h"
#include "doomdef.h"
#include "modelrenderer.h"
#include "texturemanager.h"
#include "i_time.h"
#include "hw_renderstate.h"
#include "hw_material_pbr.h"
#include "bitmap.h"
#include "image.h"
#include "textures.h"

//===========================================================================
//
// Colored Image Source for Materials Without Textures
//
//===========================================================================

class FGLTFColoredImage : public FImageSource
{
    PalEntry color;

public:
    FGLTFColoredImage(int r, int g, int b, int a = 255)
    {
        Width = 8;
        Height = 8;
        color = PalEntry(a, r, g, b);
        bUseGamePalette = false;
    }

    PalettedPixels CreatePalettedPixels(int conversion, int frame = 0) override
    {
        // Create an 8x8 colored image
        PalettedPixels pixels(Width * Height);
        memset(pixels.Data(), 255, Width * Height); // White in palette
        return pixels;
    }

    int CopyPixels(FBitmap *bmp, int conversion, int frame = 0) override
    {
        // Create an 8x8 bitmap with the solid color
        bmp->Create(Width, Height);

        // Get direct access to pixel data (BGRA format, 4 bytes per pixel)
        uint8_t* pixels = bmp->GetPixels();
        int pitch = Width * 4;

        for (int y = 0; y < Height; y++) {
            for (int x = 0; x < Width; x++) {
                int offset = y * pitch + x * 4;
                pixels[offset + 0] = color.b;  // Blue
                pixels[offset + 1] = color.g;  // Green
                pixels[offset + 2] = color.r;  // Red
                pixels[offset + 3] = color.a;  // Alpha
            }
        }

        return 0;
    }
};

namespace
{
static VSMatrix TRSToMatrix(const TRS& transform)
{
    VSMatrix mat;
    mat.loadIdentity();
    mat.translate(transform.translation.X, transform.translation.Y, transform.translation.Z);
    mat.multQuaternion(transform.rotation);
    mat.scale(transform.scaling.X != 0.0f ? transform.scaling.X : 1.0f,
              transform.scaling.Y != 0.0f ? transform.scaling.Y : 1.0f,
              transform.scaling.Z != 0.0f ? transform.scaling.Z : 1.0f);
    return mat;
}

// Helper function to create a colored texture for materials without textures
static FGameTexture* CreateColoredTexture(const FVector4& color)
{
    // Convert 0-1 range to 0-255
    int r = clamp(int(color.X * 255.0f), 0, 255);
    int g = clamp(int(color.Y * 255.0f), 0, 255);
    int b = clamp(int(color.Z * 255.0f), 0, 255);
    int a = clamp(int(color.W * 255.0f), 0, 255);

    // Create a unique name for this color
    FString texName;
    texName.Format("GLTFColor_%02X%02X%02X%02X", r, g, b, a);

    // Check if we already created this texture
    FTextureID existingID = TexMan.CheckForTexture(texName.GetChars(), ETextureType::Override, 0);
    if (existingID.isValid()) {
        return TexMan.GetGameTexture(existingID);
    }

    // Create new colored image source
    FImageSource* imgSrc = new FGLTFColoredImage(r, g, b, a);

    // Create texture from image source
    FImageTexture* tex = new FImageTexture(imgSrc, 0);

    // Wrap in FGameTexture
    FGameTexture* gameTex = new FGameTexture(tex, texName.GetChars());

    // Add to texture manager
    FTextureID texID = TexMan.AddGameTexture(gameTex);

    DPrintf(DMSG_NOTIFY, "Created colored texture '%s' for glTF material (RGB: %d,%d,%d)\n",
           texName.GetChars(), r, g, b);

    return gameTex;
}

} // namespace

//===========================================================================
//
// Vertex Buffer Implementation
//
//===========================================================================

void FGLTFModel::BuildVertexBuffer(FModelRenderer* renderer)
{
    Printf("FGLTFModel::BuildVertexBuffer called! renderer=%p, isValid=%d\n", renderer, isValid);

    if (!renderer || !isValid) {
        DPrintf(DMSG_ERROR, "Cannot build vertex buffer: invalid renderer or model\n");
        return;
    }

    framesSinceLoad++;

    try {
        // Get the renderer type to select appropriate vertex buffer
        ModelRendererType rendererType = renderer->GetType();

        if (GetVertexBuffer(rendererType) != nullptr) {
            // Vertex buffer already exists
            Printf("  Vertex buffer already exists for this renderer type\n");
            return;
        }

        Printf("  Building new vertex buffer...\n");

        // Calculate total vertex and index counts
        size_t totalVertices = 0;
        size_t totalIndices = 0;

        for (const auto& mesh : scene.meshes) {
            totalVertices += mesh.vertices.Size();
            totalIndices += mesh.indices.Size();
        }

        if (totalVertices == 0) {
            DPrintf(DMSG_WARNING, "glTF model has no vertices\n");
            return;
        }

        // Check limits
        if (totalVertices > loadOptions.maxVertexCount) {
            DPrintf(DMSG_ERROR, "glTF model vertex count (%zu) exceeds limit (%zu)\n",
                   totalVertices, loadOptions.maxVertexCount);
            return;
        }

        if (totalIndices > loadOptions.maxTriangleCount * 3) {
            DPrintf(DMSG_ERROR, "glTF model triangle count exceeds limit\n");
            return;
        }

        // Create vertex buffer
        bool needIndex = totalIndices > 0;
        bool singleFrame = scene.animations.Size() == 0;

        auto* vb = renderer->CreateVertexBuffer(needIndex, singleFrame);
        SetVertexBuffer(rendererType, vb);

        if (!GetVertexBuffer(rendererType)) {
            DPrintf(DMSG_ERROR, "Failed to create vertex buffer for glTF model\n");
            return;
        }

        // Build vertex data
        BuildVertexData(renderer, rendererType);

        DPrintf(DMSG_NOTIFY, "Built glTF vertex buffer: %zu vertices, %zu indices\n",
               totalVertices, totalIndices);

    } catch (const std::exception& e) {
        DPrintf(DMSG_ERROR, "Exception building glTF vertex buffer: %s\n", e.what());

        // Clean up on failure
        if (GetVertexBuffer(renderer->GetType())) {
            auto* doomed = GetVertexBuffer(renderer->GetType());
            delete doomed;
            SetVertexBuffer(renderer->GetType(), nullptr);
        }
    }
}

void FGLTFModel::BuildVertexData(FModelRenderer* renderer, ModelRendererType rendererType)
{
    auto* buffer = GetVertexBuffer(rendererType);
    if (!buffer) {
        return;
    }

    // Convert glTF vertices to GZDoom format
    TArray<FModelVertex> gzVertices;
    TArray<unsigned int> gzIndices;

    size_t vertexOffset = 0;

    for (const auto& mesh : scene.meshes) {
        // Convert vertices
        for (const auto& gltfVertex : mesh.vertices) {
            FModelVertex gzVertex;

            // Position
            gzVertex.x = gltfVertex.x;
            gzVertex.y = gltfVertex.y;
            gzVertex.z = gltfVertex.z;

            // Normal (already packed in gltf vertex if available)
            gzVertex.packedNormal = gltfVertex.packedNormal;

            // Texture coordinates
            gzVertex.u = gltfVertex.u;
            gzVertex.v = gltfVertex.v;

            gzVertices.Push(gzVertex);
        }

        // Convert indices with offset
        for (unsigned int index : mesh.indices) {
            gzIndices.Push(index + vertexOffset);
        }

        vertexOffset += mesh.vertices.Size();
    }

    // Upload to GPU
    UploadVertexData(buffer, gzVertices, gzIndices);

    // Handle bone data if present
    if (hasSkinning && boneMatrices.Size() != 0) {
        UploadBoneData(renderer);
    }
}

void FGLTFModel::UploadVertexData(IModelVertexBuffer* buffer,
                                  const TArray<FModelVertex>& vertices,
                                  const TArray<unsigned int>& indices)
{
    if (!buffer) {
        DPrintf(DMSG_ERROR, "UploadVertexData: null buffer\n");
        return;
    }

    if (vertices.Size() == 0) {
        DPrintf(DMSG_WARNING, "UploadVertexData: no vertices to upload\n");
        return;
    }

    DPrintf(DMSG_NOTIFY, "Uploading glTF vertex data: %d vertices, %d indices\n",
            vertices.Size(), indices.Size());

    // Lock buffers and get pointers (following MD3 pattern)
    FModelVertex* vertptr = buffer->LockVertexBuffer(vertices.Size());
    unsigned int* indxptr = buffer->LockIndexBuffer(indices.Size());

    if (!vertptr) {
        DPrintf(DMSG_ERROR, "Failed to lock vertex buffer\n");
        return;
    }

    if (!indxptr) {
        DPrintf(DMSG_ERROR, "Failed to lock index buffer\n");
        buffer->UnlockVertexBuffer();
        return;
    }

    // Copy vertex data
    memcpy(vertptr, vertices.Data(), vertices.Size() * sizeof(FModelVertex));

    // Copy index data
    memcpy(indxptr, indices.Data(), indices.Size() * sizeof(unsigned int));

    // Unlock buffers to commit data to GPU
    buffer->UnlockVertexBuffer();
    buffer->UnlockIndexBuffer();

    DPrintf(DMSG_NOTIFY, "glTF vertex data uploaded successfully\n");
}

void FGLTFModel::UploadBoneData(FModelRenderer* renderer)
{
    if (!hasSkinning || boneMatrices.Size() == 0) {
        return;
    }

    DPrintf(DMSG_NOTIFY, "TODO: Implement bone data upload for glTF skinning\n");
    DPrintf(DMSG_NOTIFY, "  Bones: %d\n", boneMatrices.Size());

    // This would upload bone matrices to a uniform buffer or texture
    // for use by vertex shaders during skinning
}

//===========================================================================
//
// Rendering Implementation
//
//===========================================================================

void FGLTFModel::RenderFrame(FModelRenderer* renderer, FGameTexture* skin,
                            int frame, int frame2, double inter,
                            FTranslationID translation, const FTextureID* surfaceskinids,
                            int boneStartPosition)
{
    Printf("FGLTFModel::RenderFrame called! renderer=%p, isValid=%d, meshes=%d\n",
           renderer, isValid, scene.meshes.Size());

    if (!renderer || !isValid) {
        Printf("  ABORTED: renderer=%p, isValid=%d\n", renderer, isValid);
        return;
    }

    framesSinceLoad++;

    try {
        // Update animation if needed
        if (currentAnimationIndex >= 0 && currentAnimationIndex < (int)scene.animations.Size()) {
            UpdateAnimationState(I_GetTime() * (1.0 / TICRATE));
        }

        // Set up PBR materials if supported
        bool usesPBR = HasPBRMaterials() && renderer->GetType() == GLModelRendererType;

        // Render each mesh
        size_t vertexOffset = 0;
        for (size_t meshIndex = 0; meshIndex < scene.meshes.Size(); ++meshIndex) {
            const auto& mesh = scene.meshes[meshIndex];

            // Set up material
            FGameTexture* meshSkin = skin;
            if (mesh.materialIndex >= 0 && mesh.materialIndex < textures.Size()) {
                meshSkin = textures[mesh.materialIndex];
            }

            // Apply surface skin overrides if provided
            if (surfaceskinids && meshIndex < static_cast<size_t>(INT_MAX)) {
                FTextureID overrideSkin = surfaceskinids[meshIndex];
                if (overrideSkin.isValid()) {
                    meshSkin = TexMan.GetGameTexture(overrideSkin);
                }
            }

            if (usesPBR) {
                RenderMeshWithPBR(renderer, mesh, meshSkin, translation, vertexOffset);
            } else {
                RenderMeshStandard(renderer, mesh, meshSkin, translation, vertexOffset);
            }

            vertexOffset += mesh.vertices.Size();
        }

    } catch (const std::exception& e) {
        DPrintf(DMSG_ERROR, "Exception rendering glTF frame: %s\n", e.what());
    }
}

void FGLTFModel::RenderMeshWithPBR(FModelRenderer* renderer, const GLTFMesh& mesh,
                                   FGameTexture* skin, FTranslationID translation,
                                   size_t vertexOffset)
{
    // Set up PBR material
    const auto& pbrProps = mesh.material;

    DPrintf(DMSG_NOTIFY, "TODO: Implement PBR mesh rendering\n");
    DPrintf(DMSG_NOTIFY, "  Metallic: %.2f, Roughness: %.2f\n",
           pbrProps.metallicFactor, pbrProps.roughnessFactor);

    // This would:
    // 1. Create/get FPBRMaterial for this mesh
    // 2. Bind PBR textures and uniforms
    // 3. Select appropriate PBR shader
    // 4. Render with PBR lighting

    // Fall back to standard rendering for now
    RenderMeshStandard(renderer, mesh, skin, translation, vertexOffset);
}

void FGLTFModel::RenderMeshStandard(FModelRenderer* renderer, const GLTFMesh& mesh,
                                    FGameTexture* skin, FTranslationID translation,
                                    size_t vertexOffset)
{
    Printf("RenderMeshStandard: vertices=%d, indices=%d, vertexOffset=%d\n",
           mesh.vertices.Size(), mesh.indices.Size(), vertexOffset);

    // Validate material before rendering
    // glTF models may not have textures embedded, so we need to handle NULL skin
    if (!skin) {
        Printf("  No skin provided, creating colored texture from baseColorFactor\n");
        // Create a colored texture from the material's baseColorFactor
        // This properly renders materials that use only vertex colors
        skin = CreateColoredTexture(mesh.material.baseColorFactor);

        if (!skin) {
            DPrintf(DMSG_ERROR, "Cannot render glTF mesh: failed to create colored texture\n");
            return;
        }
        Printf("  Created colored texture: %p\n", skin);
    }

    // Set material
    Printf("  SetMaterial: skin=%p\n", skin);
    renderer->SetMaterial(skin, false, translation);

    // Setup vertex/index buffers - CRITICAL for rendering!
    // This binds the vertex and index buffers to the rendering state
    Printf("  SetupFrame: vertexOffset=%d, vertexCount=%d\n", vertexOffset, mesh.vertices.Size());
    renderer->SetupFrame(this, vertexOffset, vertexOffset, mesh.vertices.Size(), -1);

    // Render geometry
    if (mesh.indices.Size() == 0) {
        // Non-indexed rendering
        Printf("  DrawArrays: offset=%d, count=%d\n", vertexOffset, mesh.vertices.Size());
        renderer->DrawArrays(vertexOffset, mesh.vertices.Size());
    } else {
        // Indexed rendering
        size_t indexOffset = 0;
        // Calculate proper index offset based on previous meshes
        for (size_t i = 0; i < scene.meshes.Size() && &scene.meshes[i] != &mesh; ++i) {
            indexOffset += scene.meshes[i].indices.Size();
        }

        Printf("  DrawElements: indexCount=%d, indexOffset=%d bytes\n",
               mesh.indices.Size(), indexOffset * sizeof(unsigned int));
        renderer->DrawElements(mesh.indices.Size(), indexOffset * sizeof(unsigned int));
    }
    Printf("  RenderMeshStandard complete\n");
}

void FGLTFModel::UpdateAnimationState(double currentTime)
{
    if (currentAnimationIndex < 0 || currentAnimationIndex >= scene.animations.Size()) {
        return;
    }

    // Simple time-based animation update
    const auto& anim = scene.animations[currentAnimationIndex];

    if (anim.duration > 0.0f) {
        float animTime = fmod(currentTime - lastAnimationTime, anim.duration);

        // Update bone matrices if we have skinning
        if (hasSkinning && scene.skins.Size() > 0) {
            TArray<TRS> boneTransforms;
            if (SampleAnimation(anim, animTime, boneTransforms)) {
                // Convert TRS to matrices
                for (int i = 0; i < boneTransforms.Size() && i < boneMatrices.Size(); ++i) {
                    boneMatrices[i] = TRSToMatrix(boneTransforms[i]);
                }
            }
        }
    }

    lastAnimationTime = currentTime;
}

//===========================================================================
//
// Animation and Skinning Interface
//
//===========================================================================

int FGLTFModel::FindFrame(const char* name, bool nodefault)
{
    if (!name || !*name) {
        return nodefault ? -1 : 0;
    }

    // Search animations by name
    for (int i = 0; i < scene.animations.Size(); ++i) {
        if (scene.animations[i].name.CompareNoCase(name) == 0) {
            return i;
        }
    }

    return nodefault ? -1 : 0;
}

void FGLTFModel::AddSkins(uint8_t* hitlist, const FTextureID* surfaceskinids)
{
    if (!hitlist) {
        return;
    }

    // Add all textures used by this model to the hitlist
    for (const auto& texture : textures) {
        if (texture && texture->GetID().isValid()) {
            int index = texture->GetID().GetIndex();
            if (index >= 0 && index < INT_MAX) {
                hitlist[index] = 1;
            }
        }
    }

    // Add surface skin overrides
    if (surfaceskinids) {
        for (size_t i = 0; i < scene.meshes.Size(); ++i) {
            if (surfaceskinids[i].isValid()) {
                int index = surfaceskinids[i].GetIndex();
                if (index >= 0 && index < INT_MAX) {
                    hitlist[index] = 1;
                }
            }
        }
    }
}

bool FGLTFModel::HasPBRMaterials() const
{
    for (const auto& mesh : scene.meshes) {
        const auto& material = mesh.material;

        // Check if this material uses PBR features
        if (material.metallicFactor != 1.0f ||
            material.roughnessFactor != 1.0f ||
            material.baseColorTextureIndex != -1 ||
            material.metallicRoughnessTextureIndex != -1 ||
            material.normalTextureIndex != -1 ||
            material.emissiveTextureIndex != -1) {
            return true;
        }
    }
    return false;
}

const char* FGLTFModel::GetAnimationName(int index) const
{
    if (index < 0 || index >= scene.animations.Size()) {
        return "";
    }
    return scene.animations[index].name.GetChars();
}

float FGLTFModel::GetAnimationDuration(int index) const
{
    if (index < 0 || index >= scene.animations.Size()) {
        return 0.0f;
    }
    return scene.animations[index].duration;
}

void FGLTFModel::SetCurrentAnimation(int index)
{
    if (index >= 0 && index < scene.animations.Size()) {
        currentAnimationIndex = index;
        lastAnimationTime = I_GetTime() * (1.0 / TICRATE);
    } else {
        currentAnimationIndex = -1;
    }
}

void FGLTFModel::UpdateAnimation(double currentTime, TArray<VSMatrix>& outBoneMatrices)
{
    if (currentAnimationIndex < 0 || !hasSkinning || scene.skins.Size() == 0) {
        return;
    }

    UpdateAnimationState(currentTime);

    // Copy current bone matrices
    outBoneMatrices.Resize(boneMatrices.Size());
    for (int i = 0; i < boneMatrices.Size(); ++i) {
        outBoneMatrices[i] = boneMatrices[i];
    }
}

#endif // NEODOOM_GLTF_SUPPORT
