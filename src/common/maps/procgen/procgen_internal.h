#pragma once

#include "../procgen.h"
#include "cmdlib.h"
#include <math.h>

namespace ProcGen {

static const int CELL_SIZE = 256;

static const int DIR_N = 0;
static const int DIR_S = 1;
static const int DIR_W = 2;
static const int DIR_E = 3;

inline constexpr int DX[4] = { 0, 0, -1, 1 };
inline constexpr int DY[4] = { -1, 1, 0, 0 };
inline constexpr int OPP[4] = { 1, 0, 3, 2 };

// Boss ednums by difficulty
inline constexpr int BossesEasy[] = { 3003 };
inline constexpr int BossesMed[]  = { 3003, 69 };
inline constexpr int BossesHard[] = { 7, 16 };

inline constexpr int HealthDrops[] = { 2011, 2012, 2014 };
inline constexpr int ArmorDrops[] = { 2015, 2018, 2019 };

} // namespace ProcGen
