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
static constexpr int MaxGraphicsPreset = 18;
static constexpr int MaxSelectableTonemap = 14;

static void SetPresetDirtyFromFeatureChange() {
  if (!GApplyingGraphicsPreset && !bd_preset_locked && bd_graphics_preset != 0) {
    bd_graphics_preset = 0;
  }
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

static void SetFogPresetColor(int color)
{
  bd_fog_color->SetGenericRep(CVarValue<CVAR_Color>(color), CVAR_Color);
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
    bd_gi_ambient_strength = 0.20f;
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
    bd_bloom_strength = 1.2f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.35f;
    gl_crt_mode = 1;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.20f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.08f;
    bd_filmgrain_scale = 1.4f;
    bd_sharpen_enable = false;
    bd_sharpen_strength = 0.0f;
    bd_retro_pixel_enable = true;
    bd_retro_pixel_scale = 2.0f;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.35f;
    bd_vhs_scanline = 0.35f;
    bd_vhs_jitter = 0.25f;
    bd_vhs_tracking = 0.35f;
    bd_vhs_ghosting = 0.30f;
    bd_vhs_noise = 0.25f;
    bd_vhs_evil = 0.15f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 7;
    bd_colorgrade_strength = 0.36f;
    bd_colorgrade_lut = 5;
    gl_tonemap = 12;
    gl_atmosphere = 6;
    gl_atmosphere_intensity = 0.38f;
    gl_atmosphere_contrast = 1.18f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 2.0f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.30f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 2;
    bd_sector_fog_scale = 1.12f;
    bd_fog_density = 130.0f;
    SetFogPresetColor(0xc8c8be);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.35f;
    bd_fog_sky_strength = 0.50f;
    bd_fog_thick_distance = 512.0f;
    bd_fog_thick_multiplier = 5.0f;
    SetFogGradientPreset(1, 0x58625b, 0.25f, 0.85f, 0.0f, 0.0f);
    return;
  case 4: // VHS Horror
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.65f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.52f;
    gl_crt_mode = 2;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.28f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.24f;
    bd_filmgrain_scale = 2.7f;
    bd_sharpen_enable = false;
    bd_sharpen_strength = 0.0f;
    bd_retro_pixel_enable = true;
    bd_retro_pixel_scale = 1.35f;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.72f;
    bd_vhs_scanline = 0.72f;
    bd_vhs_jitter = 0.52f;
    bd_vhs_tracking = 0.82f;
    bd_vhs_ghosting = 0.62f;
    bd_vhs_noise = 0.78f;
    bd_vhs_evil = 0.95f;
    bd_vhs_panic_enable = true;
    bd_colorgrade_mode = 5;
    bd_colorgrade_strength = 0.72f;
    bd_colorgrade_lut = 4;
    gl_tonemap = 11;
    gl_atmosphere = 7;
    gl_atmosphere_intensity = 0.72f;
    gl_atmosphere_contrast = 1.08f;
    bd_dynlight_falloff_mode = 2;
    bd_dynlight_falloff_exponent = 2.8f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.24f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.25f;
    bd_fog_density = 185.0f;
    SetFogPresetColor(0xb8b7aa);
    bd_fog_color_mode = 1;
    bd_fog_color_strength = 0.90f;
    bd_fog_sky_strength = 0.92f;
    bd_fog_thick_distance = 280.0f;
    bd_fog_thick_multiplier = 10.0f;
    SetFogGradientPreset(2, 0x060806, 0.62f, 1.65f, 0.0f, -8.0f);
    return;
  case 5: // Industrial Hell
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.3f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.45f;
    gl_crt_mode = 3;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.40f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.20f;
    bd_filmgrain_scale = 2.3f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.22f;
    bd_retro_pixel_enable = true;
    bd_retro_pixel_scale = 2.0f;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.50f;
    bd_vhs_scanline = 0.55f;
    bd_vhs_jitter = 0.35f;
    bd_vhs_tracking = 0.55f;
    bd_vhs_ghosting = 0.50f;
    bd_vhs_noise = 0.45f;
    bd_vhs_evil = 0.45f;
    bd_vhs_panic_enable = true;
    bd_colorgrade_mode = 8;
    bd_colorgrade_strength = 0.55f;
    bd_colorgrade_lut = 6;
    gl_tonemap = 13;
    gl_atmosphere = 10;
    gl_atmosphere_intensity = 0.48f;
    gl_atmosphere_contrast = 1.16f;
    bd_dynlight_falloff_mode = 2;
    bd_dynlight_falloff_exponent = 3.0f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.38f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.15f;
    bd_fog_density = 165.0f;
    SetFogPresetColor(0xa49a82);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.75f;
    bd_fog_sky_strength = 0.78f;
    bd_fog_thick_distance = 360.0f;
    bd_fog_thick_multiplier = 8.0f;
    SetFogGradientPreset(2, 0x2a1510, 0.45f, 1.25f, 35.0f, -5.0f);
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
  case 7: // Silent Hill Blackout
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.85f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.72f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.18f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.34f;
    bd_filmgrain_scale = 3.1f;
    bd_sharpen_enable = false;
    bd_sharpen_strength = 0.0f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.40f;
    bd_vhs_scanline = 0.25f;
    bd_vhs_jitter = 0.32f;
    bd_vhs_tracking = 0.48f;
    bd_vhs_ghosting = 0.38f;
    bd_vhs_noise = 0.52f;
    bd_vhs_evil = 0.70f;
    bd_vhs_panic_enable = true;
    bd_colorgrade_mode = 5;
    bd_colorgrade_strength = 0.58f;
    bd_colorgrade_lut = 4;
    gl_tonemap = 11;
    gl_atmosphere = 7;
    gl_atmosphere_intensity = 0.88f;
    gl_atmosphere_contrast = 0.92f;
    bd_dynlight_falloff_mode = 2;
    bd_dynlight_falloff_exponent = 3.6f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.10f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.85f;
    bd_fog_density = 310.0f;
    SetFogPresetColor(0x101210);
    bd_fog_color_mode = 1;
    bd_fog_color_strength = 1.0f;
    bd_fog_sky_strength = 1.0f;
    bd_fog_thick_distance = 180.0f;
    bd_fog_thick_multiplier = 18.0f;
    SetFogGradientPreset(2, 0x000000, 0.95f, 2.4f, 0.0f, -12.0f);
    return;
  case 8: // Ashen Graveyard
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.25f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.46f;
    gl_crt_mode = 0;
    bd_chromatic_enable = false;
    bd_chromatic_strength = 0.0f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.18f;
    bd_filmgrain_scale = 2.0f;
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
    bd_colorgrade_mode = 4;
    bd_colorgrade_strength = 0.44f;
    bd_colorgrade_lut = 3;
    gl_tonemap = 9;
    gl_atmosphere = 6;
    gl_atmosphere_intensity = 0.62f;
    gl_atmosphere_contrast = 0.96f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 2.4f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.18f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.65f;
    bd_fog_density = 245.0f;
    SetFogPresetColor(0x9a9a90);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.86f;
    bd_fog_sky_strength = 0.95f;
    bd_fog_thick_distance = 260.0f;
    bd_fog_thick_multiplier = 13.0f;
    SetFogGradientPreset(1, 0x353934, 0.55f, 1.6f, 0.0f, 0.0f);
    return;
  case 9: // Toxic Reactor
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 2.25f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.34f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.24f;
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
    bd_colorgrade_strength = 0.62f;
    bd_colorgrade_lut = 7;
    gl_tonemap = 14;
    gl_atmosphere = 5;
    gl_atmosphere_intensity = 0.52f;
    gl_atmosphere_contrast = 1.22f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 1.8f;
    bd_emissive_boost = 0.55f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.34f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.30f;
    bd_fog_density = 205.0f;
    SetFogPresetColor(0x7dae53);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.78f;
    bd_fog_sky_strength = 0.70f;
    bd_fog_thick_distance = 330.0f;
    bd_fog_thick_multiplier = 8.0f;
    SetFogGradientPreset(2, 0x183d17, 0.42f, 1.25f, -35.0f, -2.0f);
    return;
  case 10: // Moonlit Noir
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.15f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.64f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.10f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.22f;
    bd_filmgrain_scale = 2.4f;
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
    bd_colorgrade_strength = 0.70f;
    bd_colorgrade_lut = 2;
    gl_tonemap = 10;
    gl_atmosphere = 4;
    gl_atmosphere_intensity = 0.42f;
    gl_atmosphere_contrast = 1.34f;
    bd_dynlight_falloff_mode = 2;
    bd_dynlight_falloff_exponent = 3.2f;
    bd_emissive_boost = 0.05f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.12f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.20f;
    bd_fog_density = 190.0f;
    SetFogPresetColor(0x2d3950);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.74f;
    bd_fog_sky_strength = 0.78f;
    bd_fog_thick_distance = 420.0f;
    bd_fog_thick_multiplier = 7.0f;
    SetFogGradientPreset(2, 0x07101f, 0.58f, 1.7f, 18.0f, -10.0f);
    return;
  case 11: // Inferno Bloom
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 2.65f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.38f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.22f;
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
    bd_colorgrade_strength = 0.64f;
    bd_colorgrade_lut = 6;
    gl_tonemap = 13;
    gl_atmosphere = 10;
    gl_atmosphere_intensity = 0.72f;
    gl_atmosphere_contrast = 1.18f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 1.5f;
    bd_emissive_boost = 0.75f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.28f;
    bd_sprite_lighting_refine = false;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.18f;
    bd_fog_density = 175.0f;
    SetFogPresetColor(0xb35b2b);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.70f;
    bd_fog_sky_strength = 0.66f;
    bd_fog_thick_distance = 360.0f;
    bd_fog_thick_multiplier = 7.0f;
    SetFogGradientPreset(2, 0x401006, 0.45f, 1.35f, 42.0f, -4.0f);
    return;
  case 12: // Frozen Wasteland
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.45f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.30f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.12f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.10f;
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
    bd_colorgrade_strength = 0.48f;
    bd_colorgrade_lut = 2;
    gl_tonemap = 14;
    gl_atmosphere = 3;
    gl_atmosphere_intensity = 0.58f;
    gl_atmosphere_contrast = 1.08f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 2.2f;
    bd_emissive_boost = 0.18f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.32f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.55f;
    bd_fog_density = 235.0f;
    SetFogPresetColor(0xb7d0d5);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.82f;
    bd_fog_sky_strength = 0.88f;
    bd_fog_thick_distance = 300.0f;
    bd_fog_thick_multiplier = 10.0f;
    SetFogGradientPreset(1, 0x5e7e8c, 0.50f, 1.45f, 0.0f, 0.0f);
    return;
  case 13: // Sodium Streets
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 2.05f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.42f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.16f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.16f;
    bd_filmgrain_scale = 2.1f;
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
    bd_colorgrade_mode = 8;
    bd_colorgrade_strength = 0.52f;
    bd_colorgrade_lut = 6;
    gl_tonemap = 12;
    gl_atmosphere = 8;
    gl_atmosphere_intensity = 0.60f;
    gl_atmosphere_contrast = 1.05f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 2.0f;
    bd_emissive_boost = 0.28f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.22f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.35f;
    bd_fog_density = 210.0f;
    SetFogPresetColor(0xc79a54);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.78f;
    bd_fog_sky_strength = 0.74f;
    bd_fog_thick_distance = 340.0f;
    bd_fog_thick_multiplier = 9.0f;
    SetFogGradientPreset(2, 0x3d2b11, 0.46f, 1.3f, -25.0f, -5.0f);
    return;
  case 14: // Cyberpunk Rain
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 2.40f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.36f;
    gl_crt_mode = 1;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.34f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.10f;
    bd_filmgrain_scale = 1.6f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.24f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.26f;
    bd_vhs_scanline = 0.20f;
    bd_vhs_jitter = 0.18f;
    bd_vhs_tracking = 0.22f;
    bd_vhs_ghosting = 0.36f;
    bd_vhs_noise = 0.22f;
    bd_vhs_evil = 0.10f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 7;
    bd_colorgrade_strength = 0.66f;
    bd_colorgrade_lut = 8;
    gl_tonemap = 14;
    gl_atmosphere = 9;
    gl_atmosphere_intensity = 0.64f;
    gl_atmosphere_contrast = 1.28f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 1.7f;
    bd_emissive_boost = 0.90f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.30f;
    bd_sprite_lighting_refine = false;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.22f;
    bd_fog_density = 185.0f;
    SetFogPresetColor(0x43536b);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.66f;
    bd_fog_sky_strength = 0.70f;
    bd_fog_thick_distance = 420.0f;
    bd_fog_thick_multiplier = 6.0f;
    SetFogGradientPreset(2, 0x141b3a, 0.40f, 1.1f, 80.0f, -3.0f);
    return;
  case 15: // Bleach Bunker
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.55f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.18f;
    gl_crt_mode = 0;
    bd_chromatic_enable = false;
    bd_chromatic_strength = 0.0f;
    bd_filmgrain_enable = false;
    bd_filmgrain_strength = 0.0f;
    bd_filmgrain_scale = 1.0f;
    bd_sharpen_enable = true;
    bd_sharpen_strength = 0.42f;
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
    bd_colorgrade_strength = 0.40f;
    bd_colorgrade_lut = 1;
    gl_tonemap = 14;
    gl_atmosphere = 1;
    gl_atmosphere_intensity = 0.38f;
    gl_atmosphere_contrast = 1.42f;
    bd_dynlight_falloff_mode = 1;
    bd_dynlight_falloff_exponent = 2.3f;
    bd_emissive_boost = 0.14f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.26f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 0.95f;
    bd_fog_density = 135.0f;
    SetFogPresetColor(0xd8d7c5);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.44f;
    bd_fog_sky_strength = 0.50f;
    bd_fog_thick_distance = 620.0f;
    bd_fog_thick_multiplier = 4.0f;
    SetFogGradientPreset(1, 0x807d68, 0.22f, 0.75f, 0.0f, 0.0f);
    return;
  case 16: // Analog Nightmare
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.70f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.68f;
    gl_crt_mode = 3;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.46f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.32f;
    bd_filmgrain_scale = 4.6f;
    bd_sharpen_enable = false;
    bd_sharpen_strength = 0.0f;
    bd_retro_pixel_enable = true;
    bd_retro_pixel_scale = 1.65f;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.88f;
    bd_vhs_scanline = 0.82f;
    bd_vhs_jitter = 0.72f;
    bd_vhs_tracking = 0.90f;
    bd_vhs_ghosting = 0.82f;
    bd_vhs_noise = 0.86f;
    bd_vhs_evil = 1.0f;
    bd_vhs_panic_enable = true;
    bd_colorgrade_mode = 5;
    bd_colorgrade_strength = 0.82f;
    bd_colorgrade_lut = 5;
    gl_tonemap = 11;
    gl_atmosphere = 7;
    gl_atmosphere_intensity = 0.82f;
    gl_atmosphere_contrast = 0.92f;
    bd_dynlight_falloff_mode = 2;
    bd_dynlight_falloff_exponent = 4.0f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.08f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.60f;
    bd_fog_density = 260.0f;
    SetFogPresetColor(0x3a3430);
    bd_fog_color_mode = 1;
    bd_fog_color_strength = 0.92f;
    bd_fog_sky_strength = 0.88f;
    bd_fog_thick_distance = 240.0f;
    bd_fog_thick_multiplier = 14.0f;
    SetFogGradientPreset(2, 0x050303, 0.72f, 1.9f, 12.0f, -8.0f);
    return;
  case 17: // Dream Decay
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.95f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.40f;
    gl_crt_mode = 0;
    bd_chromatic_enable = true;
    bd_chromatic_strength = 0.30f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.16f;
    bd_filmgrain_scale = 2.6f;
    bd_sharpen_enable = false;
    bd_sharpen_strength = 0.0f;
    bd_retro_pixel_enable = false;
    bd_retro_pixel_scale = 1.0f;
    bd_vhs_enable = true;
    bd_vhs_strength = 0.18f;
    bd_vhs_scanline = 0.12f;
    bd_vhs_jitter = 0.18f;
    bd_vhs_tracking = 0.16f;
    bd_vhs_ghosting = 0.34f;
    bd_vhs_noise = 0.24f;
    bd_vhs_evil = 0.20f;
    bd_vhs_panic_enable = false;
    bd_colorgrade_mode = 7;
    bd_colorgrade_strength = 0.46f;
    bd_colorgrade_lut = 8;
    gl_tonemap = 8;
    gl_atmosphere = 6;
    gl_atmosphere_intensity = 0.74f;
    gl_atmosphere_contrast = 0.86f;
    bd_dynlight_falloff_mode = 2;
    bd_dynlight_falloff_exponent = 2.6f;
    bd_emissive_boost = 0.20f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.24f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.45f;
    bd_fog_density = 225.0f;
    SetFogPresetColor(0x6f6a7f);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.72f;
    bd_fog_sky_strength = 0.82f;
    bd_fog_thick_distance = 310.0f;
    bd_fog_thick_multiplier = 11.0f;
    SetFogGradientPreset(2, 0x211a30, 0.56f, 1.55f, -60.0f, -6.0f);
    return;
  case 18: // Low Light Realism
    bd_postfx_enable = true;
    bd_postfx_quality = 3;
    bd_bloom_enable = true;
    bd_bloom_strength = 1.05f;
    bd_vignette_enable = true;
    bd_vignette_strength = 0.32f;
    gl_crt_mode = 0;
    bd_chromatic_enable = false;
    bd_chromatic_strength = 0.0f;
    bd_filmgrain_enable = true;
    bd_filmgrain_strength = 0.06f;
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
    bd_colorgrade_strength = 0.24f;
    bd_colorgrade_lut = 1;
    gl_tonemap = 14;
    gl_atmosphere = 2;
    gl_atmosphere_intensity = 0.32f;
    gl_atmosphere_contrast = 1.12f;
    bd_dynlight_falloff_mode = 2;
    bd_dynlight_falloff_exponent = 3.4f;
    bd_emissive_boost = 0.0f;
    bd_gi_ambient_enable = true;
    bd_gi_ambient_strength = 0.14f;
    bd_sprite_lighting_refine = true;
    bd_fog_mode = 1;
    bd_sector_fog_scale = 1.05f;
    bd_fog_density = 150.0f;
    SetFogPresetColor(0x5e635d);
    bd_fog_color_mode = 2;
    bd_fog_color_strength = 0.48f;
    bd_fog_sky_strength = 0.54f;
    bd_fog_thick_distance = 520.0f;
    bd_fog_thick_multiplier = 5.0f;
    SetFogGradientPreset(1, 0x20251f, 0.28f, 1.0f, 0.0f, 0.0f);
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

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_dynlight_falloff_exponent, 2.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.5f)
    self = 0.5f;
  if (self > 8.0f)
    self = 8.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Float, bd_emissive_boost, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 2.0f)
    self = 2.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Bool, bd_gi_ambient_enable, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  OnPresetFeatureChanged(self);
}
CUSTOM_CVAR(Float, bd_gi_ambient_strength, 0.0f,
            CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  if (self < 0.0f)
    self = 0.0f;
  if (self > 1.0f)
    self = 1.0f;

  OnPresetFeatureChanged(self);
}

CUSTOM_CVAR(Bool, bd_sprite_lighting_refine, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
  OnPresetFeatureChanged(self);
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
