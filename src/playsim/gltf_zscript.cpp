/*
** gltf_zscript.cpp
**
** Native C++ implementation of glTF ZScript interface
** Simplified stub implementation for BiasedDoom
**
**---------------------------------------------------------------------------
**
** Copyright 2025 BiasedDoom Contributors
** All rights reserved.
**
*/

#include "actor.h"
#include "doomdef.h"
#include "g_levellocals.h"
#include "r_defs.h"
#include "vm.h"

#include <algorithm>

bool SetAnimationInternal(AActor *self, FName animName, double framerate,
                          int startFrame, int loopFrame, int endFrame,
                          int interpolateTics, int flags, double ticFrac,
                          AnimInfo *anims = nullptr,
                          TArray<TRS> *prevAnimOld = nullptr);
void SetAnimationFrameRateInternal(AActor *self, double framerate,
                                   double ticFrac,
                                   AnimInfo *anims = nullptr);

enum EGLTFSetAnimationFlags {
  GLTF_SAF_INSTANT = 1 << 0,
  GLTF_SAF_LOOP = 1 << 1,
  GLTF_SAF_NOOVERRIDE = 1 << 2,
};

static int BlendSecondsToTics(double blendTime) {
  return std::max(1, static_cast<int>(blendTime * TICRATE));
}

// Helper native implementations (called from VM wrappers)
static void GLTFPlayAnimationImpl(AActor *self, int i_name, bool loop,
                                  double blendTime) {
  FName name{ENamedName(i_name)};
  if (!self || name == NAME_None) {
    return;
  }

  self->flags9 |= MF9_DECOUPLEDANIMATIONS;
  SetAnimationInternal(self, name, -1, -1, -1, -1,
                       BlendSecondsToTics(blendTime),
                       (loop ? GLTF_SAF_LOOP : 0) | GLTF_SAF_NOOVERRIDE, 1);
}

static void GLTFStopAnimationImpl(AActor *self) {
  if (!self) {
    return;
  }

  SetAnimationInternal(self, NAME_None, -1, -1, -1, -1, 1,
                       GLTF_SAF_INSTANT, 1);
}

static void GLTFPauseAnimationImpl(AActor *self) {
  if (self) {
    self->flags9 |= MF9_DECOUPLEDANIMATIONS;
    SetAnimationFrameRateInternal(self, 0.0, 1);
  }
}

static void GLTFResumeAnimationImpl(AActor *self) {}

static void GLTFSetAnimationSpeedImpl(AActor *self, double speed) {
  if (self && self->modelData &&
      !(self->modelData->anims.curAnim.flags & MODELANIM_NONE)) {
    const double framerate = self->modelData->anims.curAnim.framerate * speed;
    SetAnimationFrameRateInternal(self, std::max(0.0, framerate), 1);
  }
}

static void GLTFSetPBREnabledImpl(AActor *self, bool enable) {
  Printf("GLTF_SetPBREnabled: %d\n", enable);
}

static void GLTFSetMetallicFactorImpl(AActor *self, double metallic) {
  Printf("GLTF_SetMetallicFactor: %f\n", metallic);
}

static void GLTFSetRoughnessFactorImpl(AActor *self, double roughness) {
  Printf("GLTF_SetRoughnessFactor: %f\n", roughness);
}

static void GLTFSetEmissiveImpl(AActor *self, unsigned color, double strength) {
  Printf("GLTF_SetEmissive: color=%08x strength=%f\n", color, strength);
}

static void GLTFUpdateModelImpl(AActor *self, double deltaTime) {
  // Stub: would update animation state
}

//===========================================================================
//
// GLTFModel mixin - native function implementations
//
// NOTE: These are simplified stub implementations to allow the game to
// compile and run. Full implementation requires deeper integration with
// the model rendering system.
//
//===========================================================================

//===========================================================================
//
// GLTF_PlayAnimation
//
//===========================================================================

DEFINE_ACTION_FUNCTION_NATIVE(AActor, GLTF_PlayAnimation,
                              GLTFPlayAnimationImpl) {
  PARAM_SELF_PROLOGUE(AActor);
  PARAM_NAME(name);
  PARAM_BOOL(loop);
  PARAM_FLOAT(blendTime);

  GLTFPlayAnimationImpl(self, name.GetIndex(), loop, blendTime);

  return 0;
}

//===========================================================================
//
// GLTF_StopAnimation
//
//===========================================================================

DEFINE_ACTION_FUNCTION_NATIVE(AActor, GLTF_StopAnimation,
                              GLTFStopAnimationImpl) {
  PARAM_SELF_PROLOGUE(AActor);

  GLTFStopAnimationImpl(self);

  return 0;
}

//===========================================================================
//
// GLTF_PauseAnimation
//
//===========================================================================

DEFINE_ACTION_FUNCTION_NATIVE(AActor, GLTF_PauseAnimation,
                              GLTFPauseAnimationImpl) {
  PARAM_SELF_PROLOGUE(AActor);

  GLTFPauseAnimationImpl(self);

  return 0;
}

//===========================================================================
//
// GLTF_ResumeAnimation
//
//===========================================================================

DEFINE_ACTION_FUNCTION_NATIVE(AActor, GLTF_ResumeAnimation,
                              GLTFResumeAnimationImpl) {
  PARAM_SELF_PROLOGUE(AActor);

  GLTFResumeAnimationImpl(self);

  return 0;
}

//===========================================================================
//
// GLTF_SetAnimationSpeed
//
//===========================================================================

DEFINE_ACTION_FUNCTION_NATIVE(AActor, GLTF_SetAnimationSpeed,
                              GLTFSetAnimationSpeedImpl) {
  PARAM_SELF_PROLOGUE(AActor);
  PARAM_FLOAT(speed);

  GLTFSetAnimationSpeedImpl(self, speed);

  return 0;
}

//===========================================================================
//
// GLTF_SetPBREnabled
//
//===========================================================================

DEFINE_ACTION_FUNCTION_NATIVE(AActor, GLTF_SetPBREnabled,
                              GLTFSetPBREnabledImpl) {
  PARAM_SELF_PROLOGUE(AActor);
  PARAM_BOOL(enable);

  GLTFSetPBREnabledImpl(self, enable);

  return 0;
}

//===========================================================================
//
// GLTF_SetMetallicFactor
//
//===========================================================================

DEFINE_ACTION_FUNCTION_NATIVE(AActor, GLTF_SetMetallicFactor,
                              GLTFSetMetallicFactorImpl) {
  PARAM_SELF_PROLOGUE(AActor);
  PARAM_FLOAT(metallic);

  GLTFSetMetallicFactorImpl(self, metallic);

  return 0;
}

//===========================================================================
//
// GLTF_SetRoughnessFactor
//
//===========================================================================

DEFINE_ACTION_FUNCTION_NATIVE(AActor, GLTF_SetRoughnessFactor,
                              GLTFSetRoughnessFactorImpl) {
  PARAM_SELF_PROLOGUE(AActor);
  PARAM_FLOAT(roughness);

  GLTFSetRoughnessFactorImpl(self, roughness);

  return 0;
}

//===========================================================================
//
// GLTF_SetEmissive
//
//===========================================================================

DEFINE_ACTION_FUNCTION_NATIVE(AActor, GLTF_SetEmissive, GLTFSetEmissiveImpl) {
  PARAM_SELF_PROLOGUE(AActor);
  PARAM_COLOR(color);
  PARAM_FLOAT(strength);

  const unsigned packedColor =
      (unsigned(color.a) << 24) | (unsigned(color.r) << 16) |
      (unsigned(color.g) << 8) | unsigned(color.b);
  GLTFSetEmissiveImpl(self, packedColor, strength);

  return 0;
}

//===========================================================================
//
// GLTF_UpdateModel
//
//===========================================================================

DEFINE_ACTION_FUNCTION_NATIVE(AActor, GLTF_UpdateModel, GLTFUpdateModelImpl) {
  PARAM_SELF_PROLOGUE(AActor);
  PARAM_FLOAT(deltaTime);

  GLTFUpdateModelImpl(self, deltaTime);

  return 0;
}
