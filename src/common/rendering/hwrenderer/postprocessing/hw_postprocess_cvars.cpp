/*
**  Postprocessing framework
**  Copyright (c) 2016-2020 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
*/

#include "hw_postprocess_cvars.h"
#include "v_video.h"

static bool GApplyingGraphicsPreset = false;
static bool GApplyingLightingPreset = false;
static constexpr int MaxGraphicsPreset = 30;
static constexpr int MaxLightingPreset = 7;
static constexpr int MaxSelectableTonemap = 14;

static void SetPresetDirtyFromFeatureChange() {
  if (!GApplyingGraphicsPreset && !bd_preset_locked && bd_graphics_preset != 0) {
    bd_graphics_preset = 0;
  }
}

static void SetLightingPresetDirtyFromFeatureChange()
{
  if (!GApplyingLightingPreset && bd_lighting_preset != 0)
    bd_lighting_preset = 0;
}

static void EnsurePostFxActive() {
  if (GApplyingGraphicsPreset)
    return;

  if (!bd_postfx_enable)
    bd_postfx_enable = true;

  if (bd_postfx_quality <= 0)
    bd_postfx_quality = 3;
}

static void OnPresetFeatureChanged(FIntCVar &)
{
  SetPresetDirtyFromFeatureChange();
}

static void OnPresetFeatureChanged(FFloatCVar &)
{
  SetPresetDirtyFromFeatureChange();
}

static void OnPresetFeatureChanged(FBoolCVar &)
{
  SetPresetDirtyFromFeatureChange();
}

static void OnPresetFeatureChanged(FColorCVar &)
{
  SetPresetDirtyFromFeatureChange();
}

static void OnLightingFeatureChanged(FIntCVar &self)
{
  SetLightingPresetDirtyFromFeatureChange();
  OnPresetFeatureChanged(self);
}

static void OnLightingFeatureChanged(FFloatCVar &self)
{
  SetLightingPresetDirtyFromFeatureChange();
  OnPresetFeatureChanged(self);
}

static void OnLightingFeatureChanged(FBoolCVar &self)
{
  SetLightingPresetDirtyFromFeatureChange();
  OnPresetFeatureChanged(self);
}

static void SetFogPresetColor(int color)
{
  bd_fog_color->SetGenericRep(CVarValue<CVAR_Color>(color), CVAR_Color);
}

static void SetLightingValues(int falloffMode, float falloffExponent, float intensity, float saturation,
                              float temperature, float ambientFloor, float specularScale, float emissiveBoost,
                              bool giAmbient, float giAmbientStrength, bool refineSprites,
                              float rangeScale = 1.0f, float falloffSoftness = 0.0f, float wrap = 0.0f,
                              float indirect = 0.0f, float shadowStrength = 1.0f)
{
  bd_dynlight_falloff_mode = falloffMode;
  bd_dynlight_falloff_exponent = falloffExponent;
  bd_dynlight_intensity = intensity;
  bd_dynlight_saturation = saturation;
  bd_dynlight_range_scale = rangeScale;
  bd_dynlight_falloff_softness = falloffSoftness;
  bd_dynlight_wrap = wrap;
  bd_dynlight_indirect = indirect;
  bd_dynlight_shadow_strength = shadowStrength;
  bd_light_temperature = temperature;
  bd_light_ambient_floor = ambientFloor;
  bd_light_specular_scale = specularScale;
  bd_emissive_boost = emissiveBoost;
  bd_gi_ambient_enable = giAmbient;
  bd_gi_ambient_strength = giAmbientStrength;
  bd_sprite_lighting_refine = refineSprites;
}

static void ApplyLightingPreset(int preset)
{
  switch (preset)
  {
  case 0: // Custom
    return;
  case 1: // Classic Balanced
    SetLightingValues(0, 2.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, false, 0.0f, false);
    return;
  case 2: // Modern Pretty
    SetLightingValues(1, 2.0f, 1.20f, 1.12f, 0.08f, 0.03f, 1.20f, 0.22f, true, 0.22f, true,
                      1.15f, 0.25f, 0.16f, 0.08f, 0.88f);
    return;
  case 3: // Warm Cinematic
    SetLightingValues(2, 2.35f, 1.32f, 1.10f, 0.32f, 0.05f, 1.35f, 0.35f, true, 0.26f, true,
                      1.25f, 0.35f, 0.22f, 0.12f, 0.82f);
    return;
  case 4: // Horror Contrast
    SetLightingValues(2, 2.65f, 0.88f, 0.78f, -0.08f, 0.025f, 0.82f, 0.10f, true, 0.18f, true,
                      1.05f, 0.18f, 0.08f, 0.04f, 1.0f);
    return;
  case 5: // Neon Glow
    SetLightingValues(1, 1.65f, 1.65f, 1.60f, -0.25f, 0.04f, 1.55f, 0.65f, true, 0.24f, true,
                      1.38f, 0.42f, 0.28f, 0.18f, 0.72f);
    return;
  case 6: // PBR Showcase
    SetLightingValues(1, 1.80f, 1.35f, 1.20f, 0.0f, 0.06f, 1.80f, 0.55f, true, 0.30f, true,
                      1.25f, 0.30f, 0.14f, 0.10f, 0.84f);
    return;
  case 7: // Bright Playable
    SetLightingValues(1, 1.70f, 1.18f, 1.0f, 0.04f, 0.09f, 1.05f, 0.15f, true, 0.36f, true,
                      1.20f, 0.36f, 0.20f, 0.12f, 0.68f);
    return;
  default:
    return;
  }
}

static void SetGraphicsPresetLightingStyle(int preset)
{
  SetLightingValues(1, 2.0f, 1.05f, 1.0f, 0.0f, 0.02f, 1.05f, 0.08f, true, 0.28f, true,
                    1.10f, 0.20f, 0.10f, 0.05f, 0.92f);

  switch (preset)
  {
  case 1:
  case 6:
    SetLightingValues(0, 2.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, false, 0.0f, false);
    break;
  case 3:
  case 23:
    SetLightingValues(1, 1.80f, 1.16f, 1.10f, 0.03f, 0.04f, 1.12f, 0.18f, true, 0.40f, true,
                      1.18f, 0.28f, 0.18f, 0.08f, 0.78f);
    break;
  case 4:
  case 16:
  case 24:
  case 25:
  case 26:
  case 27:
  case 28:
    SetLightingValues(2, 2.55f, 0.90f, 0.82f, -0.08f, 0.035f, 0.85f, 0.12f, true, 0.30f, true,
                      1.08f, 0.22f, 0.12f, 0.06f, 1.0f);
    break;
  case 29:
    SetLightingValues(2, 2.30f, 1.42f, 1.24f, 0.45f, 0.045f, 1.40f, 0.48f, true, 0.30f, true,
                      1.25f, 0.35f, 0.22f, 0.14f, 0.84f);
    break;
  case 30:
    SetLightingValues(2, 2.70f, 0.78f, 0.58f, -0.30f, 0.055f, 0.72f, 0.18f, true, 0.34f, true,
                      1.18f, 0.30f, 0.10f, 0.08f, 1.0f);
    break;
  case 7:
  case 8:
  case 10:
  case 18:
  case 22:
    SetLightingValues(2, 2.40f, 0.95f, 0.86f, -0.10f, 0.04f, 0.90f, 0.12f, true, 0.32f, true);
    break;
  case 9:
  case 14:
    SetLightingValues(1, 1.60f, 1.62f, 1.58f, -0.22f, 0.04f, 1.55f, 0.60f, true, 0.28f, true);
    break;
  case 11:
  case 13:
  case 20:
    SetLightingValues(2, 2.20f, 1.32f, 1.12f, 0.30f, 0.05f, 1.35f, 0.38f, true, 0.32f, true);
    break;
  case 12:
  case 17:
  case 21:
    SetLightingValues(1, 1.85f, 1.18f, 1.12f, -0.18f, 0.05f, 1.25f, 0.25f, true, 0.34f, true);
    break;
  default:
    break;
  }
}

static void SetFogGradientPreset(int mode, int color, float strength, float scale, float yaw, float pitch)
{
  bd_fog_gradient_mode = mode;
  bd_fog_gradient_color->SetGenericRep(CVarValue<CVAR_Color>(color), CVAR_Color);
  bd_fog_gradient_strength = strength;
  bd_fog_gradient_scale = scale;
  bd_fog_direction_yaw = yaw;
  bd_fog_direction_pitch = pitch;
}

static void SetCrtPreset(int mode, float distortion, float zoom, float scanline, float density, float sharpness, float mask)
{
  gl_crt_mode = mode;
  gl_crt_distortion = distortion;
  gl_crt_zoom = zoom;
  gl_crt_scanline = scanline;
  gl_crt_scanline_density = density;
  gl_crt_scanline_sharpness = sharpness;
  gl_crt_mask_intensity = mask;
}

static float ClampPresetFloat(float value, float minValue, float maxValue)
{
  if (value < minValue)
    return minValue;
  if (value > maxValue)
    return maxValue;
  return value;
}

static void KeepPresetPlayable(int preset)
{
  if (preset <= 0)
    return;

  SetGraphicsPresetLightingStyle(preset);

  if (bd_bloom_enable)
    bd_bloom_strength = ClampPresetFloat(bd_bloom_strength, 0.45f, 2.0f);

  if (bd_vignette_enable)
    bd_vignette_strength = ClampPresetFloat(bd_vignette_strength, 0.0f, 0.42f);

  if (bd_chromatic_enable)
    bd_chromatic_strength = ClampPresetFloat(bd_chromatic_strength, 0.0f, 0.22f);

  if (bd_filmgrain_enable)
  {
    bd_filmgrain_strength = ClampPresetFloat(bd_filmgrain_strength, 0.0f, 0.22f);
    bd_filmgrain_scale = ClampPresetFloat(bd_filmgrain_scale, 1.0f, 3.0f);
  }

  if (bd_vhs_enable)
  {
    bd_vhs_strength = ClampPresetFloat(bd_vhs_strength, 0.0f, 0.30f);
    bd_vhs_scanline = ClampPresetFloat(bd_vhs_scanline, 0.0f, 0.28f);
    bd_vhs_jitter = ClampPresetFloat(bd_vhs_jitter, 0.0f, 0.14f);
    bd_vhs_tracking = ClampPresetFloat(bd_vhs_tracking, 0.0f, 0.22f);
    bd_vhs_ghosting = ClampPresetFloat(bd_vhs_ghosting, 0.0f, 0.24f);
    bd_vhs_noise = ClampPresetFloat(bd_vhs_noise, 0.0f, 0.18f);
    bd_vhs_evil = ClampPresetFloat(bd_vhs_evil, 0.0f, 0.12f);
    bd_vhs_panic_enable = false;
  }

  bd_colorgrade_strength = ClampPresetFloat(bd_colorgrade_strength, 0.0f, 0.55f);
  gl_atmosphere_intensity = ClampPresetFloat(gl_atmosphere_intensity, 0.0f, 0.68f);
  gl_atmosphere_contrast = ClampPresetFloat(gl_atmosphere_contrast, 0.90f, 1.18f);
  bd_dynlight_falloff_exponent = ClampPresetFloat(bd_dynlight_falloff_exponent, 1.35f, 2.80f);
  bd_dynlight_intensity = ClampPresetFloat(bd_dynlight_intensity, 0.60f, 1.80f);
  bd_dynlight_saturation = ClampPresetFloat(bd_dynlight_saturation, 0.40f, 1.80f);
  bd_dynlight_range_scale = ClampPresetFloat(bd_dynlight_range_scale, 0.50f, 2.0f);
  bd_dynlight_falloff_softness = ClampPresetFloat(bd_dynlight_falloff_softness, 0.0f, 0.80f);
  bd_dynlight_wrap = ClampPresetFloat(bd_dynlight_wrap, 0.0f, 0.55f);
  bd_dynlight_indirect = ClampPresetFloat(bd_dynlight_indirect, 0.0f, 0.35f);
  bd_dynlight_shadow_strength = ClampPresetFloat(bd_dynlight_shadow_strength, 0.45f, 1.0f);
  bd_light_temperature = ClampPresetFloat(bd_light_temperature, -0.75f, 0.75f);
  bd_light_ambient_floor = ClampPresetFloat(bd_light_ambient_floor, 0.0f, 0.16f);
  bd_light_specular_scale = ClampPresetFloat(bd_light_specular_scale, 0.50f, 2.0f);
  bd_emissive_boost = ClampPresetFloat(bd_emissive_boost, 0.0f, 0.60f);

  if (bd_gi_ambient_enable)
    bd_gi_ambient_strength = ClampPresetFloat(bd_gi_ambient_strength, 0.24f, 0.42f);

  if (gl_crt_mode != 0)
  {
    gl_crt_distortion = ClampPresetFloat(gl_crt_distortion, 0.0f, 0.08f);
    gl_crt_zoom = ClampPresetFloat(gl_crt_zoom, 1.0f, 1.04f);
    gl_crt_scanline = ClampPresetFloat(gl_crt_scanline, 0.0f, 0.28f);
    gl_crt_scanline_density = ClampPresetFloat(gl_crt_scanline_density, 0.8f, 1.4f);
    gl_crt_scanline_sharpness = ClampPresetFloat(gl_crt_scanline_sharpness, 0.5f, 1.6f);
    gl_crt_mask_intensity = ClampPresetFloat(gl_crt_mask_intensity, 0.0f, 0.18f);
  }

  if (bd_fog_mode != 0)
  {
    bd_sector_fog_scale = ClampPresetFloat(bd_sector_fog_scale, 0.65f, 1.30f);
    bd_fog_density = ClampPresetFloat(bd_fog_density, 60.0f, 210.0f);
    bd_fog_color_strength = ClampPresetFloat(bd_fog_color_strength, 0.0f, 0.78f);
    bd_fog_sky_strength = ClampPresetFloat(bd_fog_sky_strength, 0.0f, 0.80f);
    bd_fog_thick_distance = ClampPresetFloat(bd_fog_thick_distance, 260.0f, 2048.0f);
    bd_fog_thick_multiplier = ClampPresetFloat(bd_fog_thick_multiplier, 1.0f, 8.0f);
    bd_fog_gradient_strength = ClampPresetFloat(bd_fog_gradient_strength, 0.0f, 0.45f);
    bd_fog_gradient_scale = ClampPresetFloat(bd_fog_gradient_scale, 0.2f, 1.4f);
  }

  if (preset == 7)
  {
    SetFogPresetColor(0x5a6259);
    bd_fog_color_strength = 0.78f;
    bd_fog_density = 210.0f;
    bd_sector_fog_scale = 1.30f;
    bd_fog_thick_distance = 340.0f;
    bd_fog_thick_multiplier = 8.0f;
    SetFogGradientPreset(2, 0x202820, 0.45f, 1.35f, 0.0f, -8.0f);
  }
  else if (preset == 16)
  {
    bd_vignette_strength = 0.34f;
    bd_gi_ambient_strength = 0.32f;
    SetFogPresetColor(0x4b443e);
    SetFogGradientPreset(2, 0x120f0d, 0.30f, 1.15f, 12.0f, -6.0f);
  }
}

static void ApplyGraphicsPreset(int preset) {
  switch (preset) {
  case 0: // Custom
    return;
  case 1: // Vanilla+
    bd_postfx_enable = true;
    bd_postfx_quality = 1;
    bd_bloom_enable = false;
    bd_bloom_strength = 1.4f;
    bd_vignette_enable = false;
    bd_vignette_strength = 0.0f;
    gl_crt_mode = 0;
    bd_chromatic_enable = false;
    bd_chromatic_strength = 0.0f;
    bd_filmgrain_enable = false;
    bd_filmgrain_strength = 0.0f;
    bd_filmgrain_scale = 1.0f;
    bd_sharpen_enable = false;
    bd_sharpen_strength = 0.0f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 0;
    bd_colorgrade_strength = 0.0f;
    bd_colorgrade_lut = 0;
    gl_tonemap = 0;
    gl_atmosphere = 0;
    gl_atmosphere_intensity = 1.0f;
    gl_atmosphere_contrast = 1.0f;
    bd_dynlight_falloff_mode = 0;
    bd_dynlight_falloff_exponent = 2.0f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = false;
    bd_gi_ambient_strength = 0.0f;
    bd_sprite_lighting_refine = false;
    bd_fog_mode = 0;
    bd_sector_fog_scale = 1.0f;
    bd_fog_density = 150.0f;
    SetFogPresetColor(0xc8c8be);
    bd_fog_color_mode = 0;
    bd_fog_color_strength = 0.65f;
    bd_fog_sky_strength = 0.85f;
    bd_fog_thick_distance = 384.0f;
    bd_fog_thick_multiplier = 8.0f;
    SetFogGradientPreset(0, 0x6b746b, 0.0f, 1.0f, 0.0f, 0.0f);
    return;
  case 2: // Modern Crisp
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.5f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.25f;
    gl_crt_mode = 0;
    bd_chromatic_enable = false;
    bd_chromatic_strength = 0.0f;
    bd_filmgrain_enable = false;
    bd_filmgrain_strength = 0.0f;
    bd_filmgrain_scale = 1.0f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.35f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 0;
    bd_colorgrade_strength = 0.0f;
    bd_colorgrade_lut = 0;
    gl_tonemap = 14;
    gl_atmosphere = 0;
    gl_atmosphere_intensity = 1.0f;
    gl_atmosphere_contrast = 1.0f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 2.0f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.28f;
    bd_sprite_lighting_refine = false;
    bd_fog_mode = 0;
    bd_sector_fog_scale = 1.0f;
    bd_fog_density = 145.0f;
    SetFogPresetColor(0xd0d4cf);
    bd_fog_color_mode = 0;
    bd_fog_color_strength = 0.45f;
    bd_fog_sky_strength = 0.55f;
    bd_fog_thick_distance = 512.0f;
    bd_fog_thick_multiplier = 6.0f;
    SetFogGradientPreset(0, 0x6b746b, 0.0f, 1.0f, 0.0f, 0.0f);
    return;
  case 3: // CRT Arcade
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.15f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.12f;
    SetCrtPreset(1, 0.04f, 1.0f, 0.20f, 1.10f, 1.05f, 0.10f);
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.04f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.03f;
    bd_filmgrain_scale = 1.2f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.16f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 7;
    bd_colorgrade_strength = 0.18f;
    bd_colorgrade_lut = 5;
    gl_tonemap = 12;
    gl_atmosphere = 6;
    gl_atmosphere_intensity = 0.12f;
    gl_atmosphere_contrast = 1.08f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 2.0f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.42f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 0;
    bd_sector_fog_scale = 0.80f;
    bd_fog_density = 80.0f;
    SetFogPresetColor(0xc8c8be);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.12f;
    bd_fog_sky_strength = 0.18f;
    bd_fog_thick_distance = 900.0f;
    bd_fog_thick_multiplier = 1.5f;
    SetFogGradientPreset(0, 0x58625b, 0.0f, 0.75f, 0.0f, 0.0f);
    return;
  case 4: // VHS Horror
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 0.95f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.24f;
    SetCrtPreset(0, 0.02f, 1.0f, 0.10f, 1.0f, 1.0f, 0.0f);
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.07f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.16f;
    bd_filmgrain_scale = 2.2f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.06f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.26f;
    bd_vhs_scanline = 0.12f;
    bd_vhs_jitter = 0.08f;
    bd_vhs_tracking = 0.20f;
    bd_vhs_ghosting = 0.16f;
    bd_vhs_noise = 0.18f;
    bd_vhs_evil = 0.04f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 5;
    bd_colorgrade_strength = 0.30f;
    bd_colorgrade_lut = 4;
    gl_tonemap = 11;
    gl_atmosphere = 7;
    gl_atmosphere_intensity = 0.30f;
    gl_atmosphere_contrast = 1.00f;
    bd_dynlight_falloff_mode = 2;
    bd_dynlight_falloff_exponent = 2.10f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.36f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 0.95f;
    bd_fog_density = 125.0f;
    SetFogPresetColor(0x9b9d92);
    bd_fog_color_mode = 1;
    bd_fog_color_strength = 0.42f;
    bd_fog_sky_strength = 0.44f;
    bd_fog_thick_distance = 620.0f;
    bd_fog_thick_multiplier = 3.0f;
    SetFogGradientPreset(1, 0x22251f, 0.14f, 0.85f, 0.0f, -3.0f);
    return;
  case 5: // Industrial Hell
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.3f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.28f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.08f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.08f;
    bd_filmgrain_scale = 1.8f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.22f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 8;
    bd_colorgrade_strength = 0.42f;
    bd_colorgrade_lut = 6;
    gl_tonemap = 13;
    gl_atmosphere = 10;
    gl_atmosphere_intensity = 0.42f;
    gl_atmosphere_contrast = 1.12f;
    bd_dynlight_falloff_mode = 2;
    bd_dynlight_falloff_exponent = 2.20f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.32f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.15f;
    bd_fog_density = 150.0f;
    SetFogPresetColor(0xa49a82);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.58f;
    bd_fog_sky_strength = 0.56f;
    bd_fog_thick_distance = 360.0f;
    bd_fog_thick_multiplier = 5.0f;
    SetFogGradientPreset(2, 0x2a1510, 0.28f, 1.0f, 35.0f, -4.0f);
    return;
  case 6: // Low-End Performance
    bd_postfx_enable = false;
    bd_postfx_quality = 1;
    bd_bloom_enable = false;
    bd_bloom_strength = 1.4f;
    bd_vignette_enable = false;
    bd_vignette_strength = 0.0f;
    gl_crt_mode = 0;
    bd_chromatic_enable = false;
    bd_chromatic_strength = 0.0f;
    bd_filmgrain_enable = false;
    bd_filmgrain_strength = 0.0f;
    bd_filmgrain_scale = 1.0f;
    bd_sharpen_enable = false;
    bd_sharpen_strength = 0.0f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 0;
    bd_colorgrade_strength = 0.0f;
    bd_colorgrade_lut = 0;
    gl_tonemap = 0;
    gl_atmosphere = 0;
    gl_atmosphere_intensity = 1.0f;
    gl_atmosphere_contrast = 1.0f;
    bd_dynlight_falloff_mode = 0;
    bd_dynlight_falloff_exponent = 2.0f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = false;
    bd_gi_ambient_strength = 0.0f;
    bd_sprite_lighting_refine = false;
    bd_fog_mode = 0;
    bd_sector_fog_scale = 1.0f;
    bd_fog_density = 150.0f;
    SetFogPresetColor(0xc8c8be);
    bd_fog_color_mode = 0;
    bd_fog_color_strength = 0.65f;
    bd_fog_sky_strength = 0.0f;
    bd_fog_thick_distance = 0.0f;
    bd_fog_thick_multiplier = 1.0f;
    SetFogGradientPreset(0, 0x6b746b, 0.0f, 1.0f, 0.0f, 0.0f);
    return;
  case 7: // Silent Hill Fog
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.15f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.38f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.06f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.13f;
    bd_filmgrain_scale = 2.2f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.08f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 5;
    bd_colorgrade_strength = 0.42f;
    bd_colorgrade_lut = 4;
    gl_tonemap = 11;
    gl_atmosphere = 7;
    gl_atmosphere_intensity = 0.62f;
    gl_atmosphere_contrast = 0.96f;
    bd_dynlight_falloff_mode = 2;
    bd_dynlight_falloff_exponent = 2.45f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.34f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.30f;
    bd_fog_density = 210.0f;
    SetFogPresetColor(0x747c72);
    bd_fog_color_mode = 1;
    bd_fog_color_strength = 0.78f;
    bd_fog_sky_strength = 0.80f;
    bd_fog_thick_distance = 340.0f;
    bd_fog_thick_multiplier = 8.0f;
    SetFogGradientPreset(2, 0x202820, 0.42f, 1.35f, 0.0f, -8.0f);
    return;
  case 8: // Ashen Graveyard
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.25f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.30f;
    gl_crt_mode = 0;
    bd_chromatic_enable = false;
    bd_chromatic_strength = 0.0f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.10f;
    bd_filmgrain_scale = 1.7f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.08f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 4;
    bd_colorgrade_strength = 0.44f;
    bd_colorgrade_lut = 3;
    gl_tonemap = 9;
    gl_atmosphere = 6;
    gl_atmosphere_intensity = 0.40f;
    gl_atmosphere_contrast = 0.96f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 2.4f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.30f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.15f;
    bd_fog_density = 170.0f;
    SetFogPresetColor(0x9a9a90);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.60f;
    bd_fog_sky_strength = 0.68f;
    bd_fog_thick_distance = 420.0f;
    bd_fog_thick_multiplier = 5.0f;
    SetFogGradientPreset(1, 0x353934, 0.30f, 1.0f, 0.0f, 0.0f);
    return;
  case 9: // Toxic Reactor
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.45f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.22f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.08f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.12f;
    bd_filmgrain_scale = 1.5f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.18f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 6;
    bd_colorgrade_strength = 0.42f;
    bd_colorgrade_lut = 7;
    gl_tonemap = 14;
    gl_atmosphere = 5;
    gl_atmosphere_intensity = 0.38f;
    gl_atmosphere_contrast = 1.10f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 1.8f;
    bd_emissive_boost = 0.55f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.34f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.30f;
    bd_fog_density = 150.0f;
    SetFogPresetColor(0x7dae53);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.55f;
    bd_fog_sky_strength = 0.50f;
    bd_fog_thick_distance = 440.0f;
    bd_fog_thick_multiplier = 4.5f;
    SetFogGradientPreset(2, 0x183d17, 0.25f, 0.95f, -35.0f, -2.0f);
    return;
  case 10: // Moonlit Noir
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.15f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.36f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.10f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.10f;
    bd_filmgrain_scale = 1.8f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.12f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 2;
    bd_colorgrade_strength = 0.46f;
    bd_colorgrade_lut = 2;
    gl_tonemap = 10;
    gl_atmosphere = 4;
    gl_atmosphere_intensity = 0.42f;
    gl_atmosphere_contrast = 1.12f;
    bd_dynlight_falloff_mode = 2;
    bd_dynlight_falloff_exponent = 2.45f;
    bd_emissive_boost = 0.05f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.28f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.20f;
    bd_fog_density = 150.0f;
    SetFogPresetColor(0x2d3950);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.56f;
    bd_fog_sky_strength = 0.62f;
    bd_fog_thick_distance = 560.0f;
    bd_fog_thick_multiplier = 4.5f;
    SetFogGradientPreset(2, 0x07101f, 0.32f, 1.1f, 18.0f, -6.0f);
    return;
  case 11: // Inferno Bloom
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.55f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.38f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.10f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.12f;
    bd_filmgrain_scale = 1.7f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.20f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 8;
    bd_colorgrade_strength = 0.46f;
    bd_colorgrade_lut = 6;
    gl_tonemap = 13;
    gl_atmosphere = 10;
    gl_atmosphere_intensity = 0.45f;
    gl_atmosphere_contrast = 1.12f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 1.5f;
    bd_emissive_boost = 0.50f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.30f;
    bd_sprite_lighting_refine = false;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.18f;
    bd_fog_density = 145.0f;
    SetFogPresetColor(0xb35b2b);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.52f;
    bd_fog_sky_strength = 0.50f;
    bd_fog_thick_distance = 440.0f;
    bd_fog_thick_multiplier = 4.5f;
    SetFogGradientPreset(2, 0x401006, 0.28f, 1.0f, 42.0f, -4.0f);
    return;
  case 12: // Frozen Wasteland
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.45f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.22f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.04f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.05f;
    bd_filmgrain_scale = 1.3f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.30f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 2;
    bd_colorgrade_strength = 0.32f;
    bd_colorgrade_lut = 2;
    gl_tonemap = 14;
    gl_atmosphere = 3;
    gl_atmosphere_intensity = 0.35f;
    gl_atmosphere_contrast = 1.08f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 2.2f;
    bd_emissive_boost = 0.18f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.38f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.05f;
    bd_fog_density = 160.0f;
    SetFogPresetColor(0xb7d0d5);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.56f;
    bd_fog_sky_strength = 0.62f;
    bd_fog_thick_distance = 480.0f;
    bd_fog_thick_multiplier = 5.0f;
    SetFogGradientPreset(1, 0x5e7e8c, 0.28f, 1.0f, 0.0f, 0.0f);
    return;
  case 13: // Sodium Streets
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.40f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.25f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.06f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.08f;
    bd_filmgrain_scale = 1.6f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.10f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 8;
    bd_colorgrade_strength = 0.36f;
    bd_colorgrade_lut = 6;
    gl_tonemap = 12;
    gl_atmosphere = 8;
    gl_atmosphere_intensity = 0.40f;
    gl_atmosphere_contrast = 1.05f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 2.0f;
    bd_emissive_boost = 0.28f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.32f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.05f;
    bd_fog_density = 150.0f;
    SetFogPresetColor(0xc79a54);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.52f;
    bd_fog_sky_strength = 0.56f;
    bd_fog_thick_distance = 500.0f;
    bd_fog_thick_multiplier = 5.0f;
    SetFogGradientPreset(2, 0x3d2b11, 0.25f, 1.0f, -25.0f, -4.0f);
    return;
  case 14: // Cyberpunk Rain
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.70f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.24f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.12f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.10f;
    bd_filmgrain_scale = 1.6f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.24f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.12f;
    bd_vhs_scanline = 0.10f;
    bd_vhs_jitter = 0.06f;
    bd_vhs_tracking = 0.08f;
    bd_vhs_ghosting = 0.14f;
    bd_vhs_noise = 0.08f;
    bd_vhs_evil = 0.10f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 7;
    bd_colorgrade_strength = 0.50f;
    bd_colorgrade_lut = 8;
    gl_tonemap = 14;
    gl_atmosphere = 9;
    gl_atmosphere_intensity = 0.46f;
    gl_atmosphere_contrast = 1.15f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 1.7f;
    bd_emissive_boost = 0.55f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.30f;
    bd_sprite_lighting_refine = false;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.22f;
    bd_fog_density = 140.0f;
    SetFogPresetColor(0x43536b);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.45f;
    bd_fog_sky_strength = 0.45f;
    bd_fog_thick_distance = 620.0f;
    bd_fog_thick_multiplier = 4.0f;
    SetFogGradientPreset(2, 0x141b3a, 0.25f, 0.9f, 80.0f, -3.0f);
    return;
  case 15: // Bleach Bunker
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.10f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.12f;
    gl_crt_mode = 0;
    bd_chromatic_enable = false;
    bd_chromatic_strength = 0.0f;
    bd_filmgrain_enable = false;
    bd_filmgrain_strength = 0.0f;
    bd_filmgrain_scale = 1.0f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.26f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 3;
    bd_colorgrade_strength = 0.26f;
    bd_colorgrade_lut = 1;
    gl_tonemap = 14;
    gl_atmosphere = 1;
    gl_atmosphere_intensity = 0.24f;
    gl_atmosphere_contrast = 1.18f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 2.3f;
    bd_emissive_boost = 0.14f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.34f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 0.95f;
    bd_fog_density = 105.0f;
    SetFogPresetColor(0xd8d7c5);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.30f;
    bd_fog_sky_strength = 0.35f;
    bd_fog_thick_distance = 620.0f;
    bd_fog_thick_multiplier = 4.0f;
    SetFogGradientPreset(1, 0x807d68, 0.14f, 0.70f, 0.0f, 0.0f);
    return;
  case 16: // Analog Horror
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.10f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.36f;
    SetCrtPreset(2, 0.04f, 1.0f, 0.20f, 1.15f, 1.05f, 0.08f);
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.12f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.16f;
    bd_filmgrain_scale = 2.4f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.08f;
    bd_retro_pixel_enable = true;
    bd_retro_pixel_scale = 1.10f;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.28f;
    bd_vhs_scanline = 0.26f;
    bd_vhs_jitter = 0.12f;
    bd_vhs_tracking = 0.18f;
    bd_vhs_ghosting = 0.20f;
    bd_vhs_noise = 0.16f;
    bd_vhs_evil = 0.12f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 5;
    bd_colorgrade_strength = 0.42f;
    bd_colorgrade_lut = 5;
    gl_tonemap = 11;
    gl_atmosphere = 7;
    gl_atmosphere_intensity = 0.46f;
    gl_atmosphere_contrast = 0.98f;
    bd_dynlight_falloff_mode = 2;
    bd_dynlight_falloff_exponent = 2.40f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.30f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.12f;
    bd_fog_density = 170.0f;
    SetFogPresetColor(0x3a3430);
    bd_fog_color_mode = 1;
    bd_fog_color_strength = 0.62f;
    bd_fog_sky_strength = 0.58f;
    bd_fog_thick_distance = 420.0f;
    bd_fog_thick_multiplier = 6.0f;
    SetFogGradientPreset(2, 0x120f0d, 0.34f, 1.20f, 12.0f, -6.0f);
    return;
  case 17: // Dream Decay
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.25f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.28f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.10f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.08f;
    bd_filmgrain_scale = 1.8f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.08f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.08f;
    bd_vhs_scanline = 0.06f;
    bd_vhs_jitter = 0.05f;
    bd_vhs_tracking = 0.06f;
    bd_vhs_ghosting = 0.10f;
    bd_vhs_noise = 0.06f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 7;
    bd_colorgrade_strength = 0.34f;
    bd_colorgrade_lut = 8;
    gl_tonemap = 8;
    gl_atmosphere = 6;
    gl_atmosphere_intensity = 0.45f;
    gl_atmosphere_contrast = 0.95f;
    bd_dynlight_falloff_mode = 2;
    bd_dynlight_falloff_exponent = 2.6f;
    bd_emissive_boost = 0.20f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.32f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.10f;
    bd_fog_density = 160.0f;
    SetFogPresetColor(0x6f6a7f);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.48f;
    bd_fog_sky_strength = 0.56f;
    bd_fog_thick_distance = 500.0f;
    bd_fog_thick_multiplier = 5.0f;
    SetFogGradientPreset(2, 0x211a30, 0.30f, 1.05f, -60.0f, -5.0f);
    return;
  case 18: // Low Light Realism
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 0.95f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.24f;
    gl_crt_mode = 0;
    bd_chromatic_enable = false;
    bd_chromatic_strength = 0.0f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.04f;
    bd_filmgrain_scale = 1.2f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.16f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 1;
    bd_colorgrade_strength = 0.18f;
    bd_colorgrade_lut = 1;
    gl_tonemap = 14;
    gl_atmosphere = 2;
    gl_atmosphere_intensity = 0.24f;
    gl_atmosphere_contrast = 1.05f;
    bd_dynlight_falloff_mode = 2;
    bd_dynlight_falloff_exponent = 2.40f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.30f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.05f;
    bd_fog_density = 110.0f;
    SetFogPresetColor(0x5e635d);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.36f;
    bd_fog_sky_strength = 0.40f;
    bd_fog_thick_distance = 650.0f;
    bd_fog_thick_multiplier = 3.5f;
    SetFogGradientPreset(1, 0x20251f, 0.18f, 0.80f, 0.0f, 0.0f);
    return;
  case 19: // Clean Visibility
    ApplyGraphicsPreset(2);
    bd_bloom_enable = true;
    bd_bloom_strength = 0.65f;
    bd_vignette_enable = false;
    bd_vignette_strength = 0.0f;
    bd_chromatic_enable = false;
    bd_filmgrain_enable = false;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.28f;
    bd_colorgrade_mode = 0;
    bd_colorgrade_strength = 0.0f;
    gl_tonemap = 14;
    gl_atmosphere = 0;
    gl_atmosphere_intensity = 1.0f;
    gl_atmosphere_contrast = 1.0f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 1.8f;
    bd_emissive_boost = 0.12f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.38f;
    bd_fog_mode = 0;
    SetFogGradientPreset(0, 0x6b746b, 0.0f, 1.0f, 0.0f, 0.0f);
    return;
  case 20: // Warm Cinematic
    ApplyGraphicsPreset(13);
    bd_bloom_strength = 1.25f;
    bd_vignette_strength = 0.28f;
    bd_chromatic_strength = 0.08f;
    bd_filmgrain_strength = 0.08f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.12f;
    bd_colorgrade_strength = 0.42f;
    gl_atmosphere_intensity = 0.42f;
    gl_atmosphere_contrast = 1.08f;
    bd_emissive_boost = 0.24f;
    bd_gi_ambient_strength = 0.30f;
    bd_sector_fog_scale = 1.05f;
    bd_fog_density = 140.0f;
    SetFogPresetColor(0xb8894d);
    bd_fog_color_strength = 0.52f;
    bd_fog_sky_strength = 0.58f;
    bd_fog_thick_distance = 520.0f;
    bd_fog_thick_multiplier = 5.0f;
    SetFogGradientPreset(2, 0x49321a, 0.28f, 1.0f, -18.0f, -3.0f);
    return;
  case 21: // Cool Clarity
    ApplyGraphicsPreset(12);
    bd_bloom_strength = 1.10f;
    bd_vignette_strength = 0.20f;
    bd_chromatic_enable = false;
    bd_filmgrain_strength = 0.05f;
    bd_sharpen_strength = 0.28f;
    bd_colorgrade_strength = 0.34f;
    gl_atmosphere_intensity = 0.36f;
    gl_atmosphere_contrast = 1.10f;
    bd_emissive_boost = 0.10f;
    bd_gi_ambient_strength = 0.36f;
    bd_sector_fog_scale = 0.95f;
    bd_fog_density = 135.0f;
    SetFogPresetColor(0xaec6d0);
    bd_fog_color_strength = 0.50f;
    bd_fog_sky_strength = 0.55f;
    bd_fog_thick_distance = 620.0f;
    bd_fog_thick_multiplier = 4.0f;
    SetFogGradientPreset(1, 0x607985, 0.22f, 0.85f, 0.0f, 0.0f);
    return;
  case 22: // Dense Playable Fog
    ApplyGraphicsPreset(7);
    bd_bloom_strength = 1.15f;
    bd_vignette_strength = 0.34f;
    bd_chromatic_strength = 0.08f;
    bd_filmgrain_strength = 0.14f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.12f;
    bd_vhs_scanline = 0.10f;
    bd_vhs_jitter = 0.08f;
    bd_vhs_tracking = 0.12f;
    bd_vhs_ghosting = 0.10f;
    bd_vhs_noise = 0.12f;
    bd_vhs_evil = 0.05f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_strength = 0.36f;
    gl_atmosphere_intensity = 0.62f;
    gl_atmosphere_contrast = 0.96f;
    bd_gi_ambient_strength = 0.30f;
    bd_sector_fog_scale = 1.25f;
    bd_fog_density = 200.0f;
    SetFogPresetColor(0x80887e);
    bd_fog_color_strength = 0.68f;
    bd_fog_sky_strength = 0.72f;
    bd_fog_thick_distance = 360.0f;
    bd_fog_thick_multiplier = 7.0f;
    SetFogGradientPreset(2, 0x2f372f, 0.38f, 1.20f, 0.0f, -5.0f);
    return;
  case 23: // Readable CRT
    ApplyGraphicsPreset(3);
    bd_bloom_strength = 1.05f;
    bd_vignette_strength = 0.08f;
    SetCrtPreset(1, 0.03f, 1.0f, 0.16f, 1.05f, 1.0f, 0.06f);
    bd_chromatic_strength = 0.03f;
    bd_filmgrain_strength = 0.02f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.18f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = false;
    bd_vhs_strength = 0.0f;
    bd_vhs_scanline = 0.0f;
    bd_vhs_jitter = 0.0f;
    bd_vhs_tracking = 0.0f;
    bd_vhs_ghosting = 0.0f;
    bd_vhs_noise = 0.0f;
    bd_vhs_evil = 0.0f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_strength = 0.14f;
    gl_atmosphere_intensity = 0.08f;
    gl_atmosphere_contrast = 1.08f;
    bd_gi_ambient_strength = 0.42f;
    bd_fog_mode = 0;
    bd_fog_density = 80.0f;
    bd_sector_fog_scale = 0.80f;
    bd_fog_color_strength = 0.10f;
    bd_fog_sky_strength = 0.15f;
    bd_fog_thick_distance = 900.0f;
    bd_fog_thick_multiplier = 1.5f;
    SetFogGradientPreset(0, 0x4d5851, 0.0f, 0.75f, 0.0f, 0.0f);
    return;
  case 24: // Action Horror
    ApplyGraphicsPreset(4);
    bd_bloom_strength = 1.20f;
    bd_vignette_strength = 0.36f;
    bd_chromatic_strength = 0.12f;
    bd_filmgrain_strength = 0.14f;
    bd_filmgrain_scale = 2.2f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.10f;
    bd_retro_pixel_enable = false;
    bd_vhs_strength = 0.24f;
    bd_vhs_scanline = 0.22f;
    bd_vhs_jitter = 0.10f;
    bd_vhs_tracking = 0.16f;
    bd_vhs_ghosting = 0.18f;
    bd_vhs_noise = 0.14f;
    bd_vhs_evil = 0.08f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_strength = 0.48f;
    gl_atmosphere_intensity = 0.52f;
    gl_atmosphere_contrast = 1.00f;
    bd_dynlight_falloff_exponent = 2.45f;
    bd_gi_ambient_strength = 0.30f;
    bd_sector_fog_scale = 1.18f;
    bd_fog_density = 170.0f;
    SetFogPresetColor(0x787b70);
    bd_fog_color_strength = 0.62f;
    bd_fog_sky_strength = 0.64f;
    bd_fog_thick_distance = 420.0f;
    bd_fog_thick_multiplier = 5.5f;
    SetFogGradientPreset(2, 0x24251f, 0.30f, 1.0f, 0.0f, -4.0f);
    return;
  case 25: // VHS Found Footage
    ApplyGraphicsPreset(4);
    bd_bloom_strength = 0.85f;
    bd_vignette_strength = 0.22f;
    SetCrtPreset(0, 0.015f, 1.0f, 0.06f, 1.0f, 1.0f, 0.0f);
    bd_chromatic_strength = 0.05f;
    bd_filmgrain_strength = 0.18f;
    bd_filmgrain_scale = 2.6f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.04f;
    bd_retro_pixel_enable = false;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.28f;
    bd_vhs_scanline = 0.10f;
    bd_vhs_jitter = 0.07f;
    bd_vhs_tracking = 0.18f;
    bd_vhs_ghosting = 0.14f;
    bd_vhs_noise = 0.18f;
    bd_vhs_evil = 0.02f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 5;
    bd_colorgrade_strength = 0.28f;
    bd_colorgrade_lut = 4;
    gl_tonemap = 11;
    gl_atmosphere = 7;
    gl_atmosphere_intensity = 0.28f;
    gl_atmosphere_contrast = 1.00f;
    bd_gi_ambient_strength = 0.36f;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 0.90f;
    bd_fog_density = 118.0f;
    SetFogPresetColor(0x969888);
    bd_fog_color_strength = 0.40f;
    bd_fog_sky_strength = 0.42f;
    bd_fog_thick_distance = 680.0f;
    bd_fog_thick_multiplier = 2.8f;
    SetFogGradientPreset(1, 0x1e231b, 0.12f, 0.80f, 0.0f, -2.0f);
    return;
  case 26: // VHS Tape Rot
    ApplyGraphicsPreset(4);
    bd_bloom_strength = 0.95f;
    bd_vignette_strength = 0.30f;
    SetCrtPreset(0, 0.02f, 1.0f, 0.08f, 1.0f, 1.0f, 0.0f);
    bd_chromatic_strength = 0.11f;
    bd_filmgrain_strength = 0.20f;
    bd_filmgrain_scale = 2.8f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.06f;
    bd_retro_pixel_enable = false;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.30f;
    bd_vhs_scanline = 0.18f;
    bd_vhs_jitter = 0.12f;
    bd_vhs_tracking = 0.22f;
    bd_vhs_ghosting = 0.24f;
    bd_vhs_noise = 0.18f;
    bd_vhs_evil = 0.08f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 8;
    bd_colorgrade_strength = 0.44f;
    bd_colorgrade_lut = 6;
    gl_tonemap = 11;
    gl_atmosphere = 9;
    gl_atmosphere_intensity = 0.40f;
    gl_atmosphere_contrast = 0.96f;
    bd_gi_ambient_strength = 0.32f;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.05f;
    bd_fog_density = 150.0f;
    SetFogPresetColor(0x7e6953);
    bd_fog_color_strength = 0.56f;
    bd_fog_sky_strength = 0.52f;
    bd_fog_thick_distance = 500.0f;
    bd_fog_thick_multiplier = 4.5f;
    SetFogGradientPreset(2, 0x21120c, 0.26f, 1.0f, 18.0f, -4.0f);
    return;
  case 27: // VHS Night Vision
    ApplyGraphicsPreset(4);
    bd_bloom_strength = 1.00f;
    bd_vignette_strength = 0.26f;
    SetCrtPreset(0, 0.015f, 1.0f, 0.08f, 1.0f, 1.0f, 0.0f);
    bd_chromatic_strength = 0.06f;
    bd_filmgrain_strength = 0.16f;
    bd_filmgrain_scale = 2.4f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.08f;
    bd_retro_pixel_enable = false;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.24f;
    bd_vhs_scanline = 0.14f;
    bd_vhs_jitter = 0.08f;
    bd_vhs_tracking = 0.16f;
    bd_vhs_ghosting = 0.16f;
    bd_vhs_noise = 0.17f;
    bd_vhs_evil = 0.04f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 4;
    bd_colorgrade_strength = 0.42f;
    bd_colorgrade_lut = 4;
    gl_tonemap = 13;
    gl_atmosphere = 4;
    gl_atmosphere_intensity = 0.38f;
    gl_atmosphere_contrast = 1.05f;
    bd_gi_ambient_strength = 0.40f;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 0.95f;
    bd_fog_density = 135.0f;
    SetFogPresetColor(0x8aa27b);
    bd_fog_color_strength = 0.50f;
    bd_fog_sky_strength = 0.46f;
    bd_fog_thick_distance = 620.0f;
    bd_fog_thick_multiplier = 3.5f;
    SetFogGradientPreset(1, 0x173011, 0.22f, 0.85f, 0.0f, -2.0f);
    return;
  case 28: // Possessed VHS
    ApplyGraphicsPreset(4);
    bd_bloom_strength = 1.05f;
    bd_vignette_strength = 0.38f;
    SetCrtPreset(0, 0.025f, 1.0f, 0.12f, 1.0f, 1.0f, 0.0f);
    bd_chromatic_strength = 0.14f;
    bd_filmgrain_strength = 0.20f;
    bd_filmgrain_scale = 2.8f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.06f;
    bd_retro_pixel_enable = false;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.30f;
    bd_vhs_scanline = 0.22f;
    bd_vhs_jitter = 0.14f;
    bd_vhs_tracking = 0.22f;
    bd_vhs_ghosting = 0.22f;
    bd_vhs_noise = 0.18f;
    bd_vhs_evil = 0.12f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 5;
    bd_colorgrade_strength = 0.50f;
    bd_colorgrade_lut = 8;
    gl_tonemap = 11;
    gl_atmosphere = 9;
    gl_atmosphere_intensity = 0.52f;
    gl_atmosphere_contrast = 0.95f;
    bd_gi_ambient_strength = 0.30f;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.18f;
    bd_fog_density = 175.0f;
    SetFogPresetColor(0x3c302d);
    bd_fog_color_strength = 0.68f;
    bd_fog_sky_strength = 0.62f;
    bd_fog_thick_distance = 420.0f;
    bd_fog_thick_multiplier = 6.0f;
    SetFogGradientPreset(2, 0x120706, 0.36f, 1.10f, 16.0f, -6.0f);
    return;
  case 29: // Blood Moon Evil
    ApplyGraphicsPreset(11);
    bd_bloom_strength = 1.45f;
    bd_vignette_strength = 0.36f;
    bd_chromatic_strength = 0.10f;
    bd_filmgrain_strength = 0.12f;
    bd_filmgrain_scale = 2.0f;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.16f;
    bd_vhs_scanline = 0.08f;
    bd_vhs_jitter = 0.06f;
    bd_vhs_tracking = 0.12f;
    bd_vhs_ghosting = 0.12f;
    bd_vhs_noise = 0.10f;
    bd_vhs_evil = 0.10f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 8;
    bd_colorgrade_strength = 0.48f;
    bd_colorgrade_lut = 6;
    gl_tonemap = 13;
    gl_atmosphere = 5;
    gl_atmosphere_intensity = 0.54f;
    gl_atmosphere_contrast = 1.08f;
    bd_gi_ambient_strength = 0.30f;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.12f;
    bd_fog_density = 165.0f;
    SetFogPresetColor(0x5b1412);
    bd_fog_color_strength = 0.62f;
    bd_fog_sky_strength = 0.58f;
    bd_fog_thick_distance = 440.0f;
    bd_fog_thick_multiplier = 5.5f;
    SetFogGradientPreset(2, 0x1d0303, 0.34f, 1.05f, -25.0f, -5.0f);
    return;
  case 30: // Void Ritual
    ApplyGraphicsPreset(10);
    bd_bloom_strength = 0.90f;
    bd_vignette_strength = 0.40f;
    bd_chromatic_strength = 0.08f;
    bd_filmgrain_strength = 0.14f;
    bd_filmgrain_scale = 2.2f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.08f;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.18f;
    bd_vhs_scanline = 0.10f;
    bd_vhs_jitter = 0.08f;
    bd_vhs_tracking = 0.14f;
    bd_vhs_ghosting = 0.14f;
    bd_vhs_noise = 0.12f;
    bd_vhs_evil = 0.12f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 5;
    bd_colorgrade_strength = 0.52f;
    bd_colorgrade_lut = 5;
    gl_tonemap = 11;
    gl_atmosphere = 9;
    gl_atmosphere_intensity = 0.58f;
    gl_atmosphere_contrast = 0.96f;
    bd_gi_ambient_strength = 0.34f;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.22f;
    bd_fog_density = 185.0f;
    SetFogPresetColor(0x08070a);
    bd_fog_color_strength = 0.72f;
    bd_fog_sky_strength = 0.68f;
    bd_fog_thick_distance = 380.0f;
    bd_fog_thick_multiplier = 6.5f;
    SetFogGradientPreset(2, 0x000000, 0.38f, 1.20f, 0.0f, -6.0f);
    return;
  default:
    return;
  }
}

static void SetGraphicsPreset(FIntCVar &self) {
  if (self < 0)
    self = 0;
  if (self > MaxGraphicsPreset)
    self = MaxGraphicsPreset;

  GApplyingGraphicsPreset = true;
  ApplyGraphicsPreset(self);
  KeepPresetPlayable(self);
  GApplyingGraphicsPreset = false;
}

//==========================================================================
//
// CVARs
//
//==========================================================================
CUSTOM_CVAR(Bool, gl_bloom, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (bd_bloom_enable != self)
    bd_bloom_enable = self;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, gl_bloom_amount, 1.4f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.1f)
    self = 0.1f;
  if (self > 4.0f)
    self = 4.0f;

  if (bd_bloom_strength != self)
    bd_bloom_strength = self;

  OnPresetFeatureChanged(self);
}

CVAR(Float, gl_exposure_scale, 1.3f, CVAR_ARCHIVE)
CVAR(Float, gl_exposure_min, 0.35f, CVAR_ARCHIVE)
CVAR(Float, gl_exposure_base, 0.35f, CVAR_ARCHIVE)
CVAR(Float, gl_exposure_speed, 0.05f, CVAR_ARCHIVE)

CUSTOM_CVAR(Int, gl_tonemap, 0, CVAR_ARCHIVE) {
  if (self < 0 || self > MaxSelectableTonemap)
    self = 0;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Int, gl_atmosphere, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0)
    self = 0;
  if (self > 10)
    self = 10;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, gl_atmosphere_intensity, 1.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 2.0f)
    self = 2.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, gl_atmosphere_contrast, 1.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 2.0f)
    self = 2.0f;

  OnPresetFeatureChanged(self);
}

CVAR(Bool, gl_lens, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

CVAR(Float, gl_lens_k, -0.12f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, gl_lens_kcube, 0.1f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, gl_lens_chromatic, 1.12f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

CUSTOM_CVAR(Int, gl_fxaa, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0 || self >= IFXAAShader::Count) {
    self = 0;
  }
}

CUSTOM_CVAR(Int, gl_ssao, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0 || self > 3)
    self = 0;
}

CUSTOM_CVAR(Int, gl_ssao_portals, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0)
    self = 0;
}

CVAR(Float, gl_ssao_strength, 0.7f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, gl_ssao_debug, 0, 0)
CVAR(Float, gl_ssao_bias, 0.2f, 0)
CVAR(Float, gl_ssao_radius, 80.0f, 0)
CUSTOM_CVAR(Float, gl_ssao_blur, 16.0f, 0) {
  if (self < 0.1f)
    self = 0.1f;
}

CUSTOM_CVAR(Float, gl_ssao_exponent, 1.8f, 0) {
  if (self < 0.1f)
    self = 0.1f;
}

CUSTOM_CVAR(Float, gl_paltonemap_powtable, 2.0f,
            CVAR_ARCHIVE | CVAR_NOINITCALL) {
  screen->UpdatePalette();
}

CUSTOM_CVAR(Bool, gl_paltonemap_reverselookup, true,
            CVAR_ARCHIVE | CVAR_NOINITCALL) {
  screen->UpdatePalette();
}

CVAR(Float, gl_menu_blur, -1.0f, CVAR_ARCHIVE)

CUSTOM_CVAR(Int, bd_graphics_preset, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  SetGraphicsPreset(self);
}

CUSTOM_CVAR(Bool, bd_bloom_enable, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (gl_bloom != self)
    gl_bloom = self;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_bloom_strength, 1.4f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.1f)
    self = 0.1f;
  if (self > 4.0f)
    self = 4.0f;

  if (gl_bloom_amount != self)
    gl_bloom_amount = self;

  OnPresetFeatureChanged(self);
}

CVAR(Bool, bd_preset_locked, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

CUSTOM_CVAR(Bool, bd_postfx_enable, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Int, bd_postfx_quality, 3, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0)
    self = 0;
  if (self > 3)
    self = 3;

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Bool, bd_vignette_enable, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_vignette_strength, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  if (self > 0.0f)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Bool, bd_chromatic_enable, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_chromatic_strength, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  if (self > 0.0f)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Bool, bd_filmgrain_enable, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_filmgrain_strength, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  if (self > 0.0f)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_filmgrain_scale, 1.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 1.0f)
    self = 1.0f;
  if (self > 8.0f)
    self = 8.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Bool, bd_sharpen_enable, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_sharpen_strength, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  if (self > 0.0f)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Bool, bd_retro_pixel_enable, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_retro_pixel_scale, 1.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 1.0f)
    self = 1.0f;
  if (self > 16.0f)
    self = 16.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Bool, bd_vhs_enable, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_vhs_strength, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  if (self > 0.0f)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_vhs_scanline, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_vhs_jitter, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_vhs_tracking, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  if (self > 0.0f)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_vhs_ghosting, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  if (self > 0.0f)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_vhs_noise, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  if (self > 0.0f)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_vhs_evil, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  if (self > 0.0f)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Bool, bd_vhs_panic_enable, false,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Int, bd_colorgrade_mode, 0,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0)
    self = 0;
  if (self > 8)
    self = 8;

  if (self > 0)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_colorgrade_strength, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  if (self > 0.0f)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Int, bd_colorgrade_lut, 0,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0)
    self = 0;
  if (self > 8)
    self = 8;

  if (self > 0)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Int, bd_dynlight_falloff_mode, 0,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0)
    self = 0;
  if (self > 2)
    self = 2;

  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_dynlight_falloff_exponent, 2.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.5f)
    self = 0.5f;
  if (self > 8.0f)
    self = 8.0f;

  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Int, bd_lighting_preset, 0,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0)
    self = 0;
  if (self > MaxLightingPreset)
    self = MaxLightingPreset;

  GApplyingLightingPreset = true;
  ApplyLightingPreset(self);
  GApplyingLightingPreset = false;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_dynlight_intensity, 1.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 3.0f)
    self = 3.0f;

  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_dynlight_saturation, 1.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 2.0f)
    self = 2.0f;

  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_dynlight_range_scale, 1.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.10f)
    self = 0.10f;
  if (self > 4.0f)
    self = 4.0f;

  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_dynlight_falloff_softness, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_dynlight_wrap, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 0.95f)
    self = 0.95f;

  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_dynlight_indirect, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_dynlight_shadow_strength, 1.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_light_temperature, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < -1.0f)
    self = -1.0f;
  if (self > 1.0f)
    self = 1.0f;

  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_light_ambient_floor, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 0.5f)
    self = 0.5f;

  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_light_specular_scale, 1.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 3.0f)
    self = 3.0f;

  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_emissive_boost, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 2.0f)
    self = 2.0f;

  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Bool, bd_gi_ambient_enable, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  OnLightingFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_gi_ambient_strength, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Bool, bd_sprite_lighting_refine, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  OnLightingFeatureChanged(self);
}

CUSTOM_CVAR(Int, bd_fog_mode, 1,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0)
    self = 0;
  if (self > 2)
    self = 2;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_sector_fog_scale, 1.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 5.0f)
    self = 5.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_fog_density, 155.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 512.0f)
    self = 512.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Color, bd_fog_color, 0xc8c8be,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Int, bd_fog_color_mode, 0,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0)
    self = 0;
  if (self > 2)
    self = 2;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_fog_color_strength, 0.65f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_fog_sky_strength, 0.85f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_fog_thick_distance, 384.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 8192.0f)
    self = 8192.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_fog_thick_multiplier, 8.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 1.0f)
    self = 1.0f;
  if (self > 64.0f)
    self = 64.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Int, bd_fog_gradient_mode, 1,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0)
    self = 0;
  if (self > 2)
    self = 2;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Color, bd_fog_gradient_color, 0x6b746b,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_fog_gradient_strength, 0.35f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_fog_gradient_scale, 1.15f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 8.0f)
    self = 8.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_fog_direction_yaw, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < -180.0f)
    self = -180.0f;
  if (self > 180.0f)
    self = 180.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_fog_direction_pitch, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < -89.0f)
    self = -89.0f;
  if (self > 89.0f)
    self = 89.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Int, gl_crt_mode, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0)
    self = 0;
  if (self > 3)
    self = 3;
  if (self > 0)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}

CVAR(Float, gl_crt_distortion, 0.1f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, gl_crt_zoom, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, gl_crt_scanline, 0.5f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, gl_crt_scanline_density, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, gl_crt_scanline_sharpness, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, gl_crt_mask_intensity, 0.5f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

CUSTOM_CVAR(Int, gl_ntsc_mode, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0)
    self = 0;
  if (self > 1)
    self = 1;
  if (self > 0)
    EnsurePostFxActive();

  OnPresetFeatureChanged(self);
}
