#pragma once

#include "c_cvars.h"
#include "v_palette.h"

#include "r_utility.h"

struct Colormap;
struct FLevelLocals;

inline int hw_ClampLight(int lightlevel)
{
	return clamp(lightlevel, 0, 255);
}

EXTERN_CVAR(Int, gl_weaponlight);

bool IsBiasedGlobalFogActive();
PalEntry GetBiasedFogColor(PalEntry fogcolor, bool forcecustomcolor);
float BiasedVisibilityScale(FLevelLocals *Level);

inline	int getExtraLight()
{
	return r_viewpoint.extralight * gl_weaponlight;
}
