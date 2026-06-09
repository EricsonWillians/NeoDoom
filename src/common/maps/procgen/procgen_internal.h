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

// Theme tables
inline constexpr const char* TechFloors[] = { "FLOOR0_1", "FLOOR4_8", "FLOOR0_3", "FLOOR1_1" };
inline constexpr const char* TechCeils[]  = { "CEIL1_1", "CEIL3_3", "FLAT1", "FLAT18" };
inline constexpr const char* TechWalls[]  = { "STARTAN3", "STARG2", "TEKWALL4", "COMPUTE1" };

inline constexpr const char* HellFloors[] = { "FLOOR6_1", "FLAT5_1", "FLOOR7_2", "FLOOR6_2" };
inline constexpr const char* HellCeils[]  = { "FLAT5_1", "FLOOR7_2", "FLAT10", "CEIL5_1" };
inline constexpr const char* HellWalls[]  = { "MARBLE1", "GSTVINE1", "SP_HOT1", "SKINMET1" };

inline constexpr const char* GenFloors[] = { "FLOOR0_1", "FLOOR6_1", "FLOOR4_8", "FLAT5_1" };
inline constexpr const char* GenCeils[]  = { "CEIL1_1", "FLAT5_1", "FLAT1", "FLAT10" };
inline constexpr const char* GenWalls[]  = { "STARTAN3", "MARBLE1", "STARG2", "GSTVINE1" };

// Enemy ednums by tier
inline constexpr int EnemiesEasy[] = { 3004, 3004, 3001, 3001, 3002 };
inline constexpr int EnemiesMed[]  = { 3001, 3002, 3003, 3005, 3006 };
inline constexpr int EnemiesHard[] = { 3003, 3005, 66, 67, 69, 64 };

// Boss ednums by difficulty
inline constexpr int BossesEasy[] = { 3003 };
inline constexpr int BossesMed[]  = { 3005, 69 };
inline constexpr int BossesHard[] = { 16, 68, 69 };

// Key thing ednums (index by lockType: 1=red, 2=blue, 3=yellow)
inline constexpr int KeyThings[] = { 0, 38, 40, 39 };
inline constexpr int HealthDrops[] = { 2011, 2012, 2014 };
inline constexpr int ArmorDrops[] = { 2015, 2018, 2019 };

} // namespace ProcGen
