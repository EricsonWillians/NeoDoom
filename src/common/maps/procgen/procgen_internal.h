#pragma once

#include "../procgen.h"
#include "cmdlib.h"
#include <math.h>

namespace ProcGen {

// A 384-unit module leaves enough lateral distance for projectile dodging and
// crossfire even in the smallest authored chamber. The size-80 canvas is the
// largest one that retains a safety margin inside UDMF's coordinate range.
static const int CELL_SIZE = 384;

static const int DIR_N = 0;
static const int DIR_S = 1;
static const int DIR_W = 2;
static const int DIR_E = 3;

enum ThemeStyle
{
	ThemeTechbase,
	ThemeHell,
	ThemeIndustrial,
	ThemeGothic,
	ThemeCorrupted,
};

inline ThemeStyle GetThemeStyle(const FString& theme)
{
	if (theme.Compare("hell") == 0) return ThemeHell;
	if (theme.Compare("industrial") == 0) return ThemeIndustrial;
	if (theme.Compare("gothic") == 0) return ThemeGothic;
	if (theme.Compare("corrupted") == 0) return ThemeCorrupted;
	return ThemeTechbase;
}

inline bool UsesInfernalArchitecture(ThemeStyle theme)
{
	return theme == ThemeHell || theme == ThemeGothic || theme == ThemeCorrupted;
}

inline constexpr int DX[4] = { 0, 0, -1, 1 };
inline constexpr int DY[4] = { -1, 1, 0, 0 };
inline constexpr int OPP[4] = { 1, 0, 3, 2 };

// Boss ednums by difficulty. The Spider Mastermind's 128-unit radius still
// needs more authored clearance than a single coarse cell, so heavyweight
// finales use the substantially smaller Cyberdemon after arena-capacity validation.
inline constexpr int BossesEasy[] = { 3003 };
inline constexpr int BossesMed[]  = { 3003, 69 };
inline constexpr int BossesHard[] = { 16 };

inline constexpr int HealthDrops[] = { 2011, 2012, 2014 };
inline constexpr int ArmorDrops[] = { 2015, 2018, 2019 };

} // namespace ProcGen
