/*
** procgen_zscript.cpp
**
** ZScript native function bindings for ProceduralMapGenerator
**
**---------------------------------------------------------------------------
**
** Copyright 2025 BiasedDoom Contributors
** All rights reserved.
**
*/

#include "vm.h"
#include "common/maps/procgen.h"

static void ZSF_SetSeed(int seed)
{
	FProceduralMapGenerator::GetInstance().SetSeed(seed);
}

static void ZSF_SetTheme(const FString& theme)
{
	FProceduralMapGenerator::GetInstance().SetTheme(theme.GetChars());
}

static void ZSF_SetDifficulty(int diff)
{
	FProceduralMapGenerator::GetInstance().SetDifficulty(diff);
}

static void ZSF_SetSize(int size)
{
	FProceduralMapGenerator::GetInstance().SetSize(size);
}

static int ZSF_Generate()
{
	return FProceduralMapGenerator::GetInstance().Generate() ? 1 : 0;
}

static const char* ZSF_GetLastError()
{
	return FProceduralMapGenerator::GetInstance().GetLastError();
}

static int ZSF_GenerateAndLoad(int seed, const FString& theme, int diff, int size)
{
	FProceduralMapGenerator& gen = FProceduralMapGenerator::GetInstance();
	gen.SetSeed(seed);
	gen.SetTheme(theme.GetChars());
	gen.SetDifficulty(diff);
	gen.SetSize(size);
	return gen.Generate() ? 1 : 0;
}

DEFINE_ACTION_FUNCTION_NATIVE(ProceduralMapGenerator, SetSeed, ZSF_SetSeed)
{
	PARAM_PROLOGUE;
	PARAM_INT(seed);
	ZSF_SetSeed(seed);
	return 0;
}

DEFINE_ACTION_FUNCTION_NATIVE(ProceduralMapGenerator, SetTheme, ZSF_SetTheme)
{
	PARAM_PROLOGUE;
	PARAM_STRING(theme);
	ZSF_SetTheme(theme);
	return 0;
}

DEFINE_ACTION_FUNCTION_NATIVE(ProceduralMapGenerator, SetDifficulty, ZSF_SetDifficulty)
{
	PARAM_PROLOGUE;
	PARAM_INT(diff);
	ZSF_SetDifficulty(diff);
	return 0;
}

DEFINE_ACTION_FUNCTION_NATIVE(ProceduralMapGenerator, SetSize, ZSF_SetSize)
{
	PARAM_PROLOGUE;
	PARAM_INT(size);
	ZSF_SetSize(size);
	return 0;
}

DEFINE_ACTION_FUNCTION_NATIVE(ProceduralMapGenerator, Generate, ZSF_Generate)
{
	PARAM_PROLOGUE;
	ACTION_RETURN_INT(ZSF_Generate());
}

DEFINE_ACTION_FUNCTION_NATIVE(ProceduralMapGenerator, GetLastError, ZSF_GetLastError)
{
	PARAM_PROLOGUE;
	ACTION_RETURN_STRING(ZSF_GetLastError());
}

DEFINE_ACTION_FUNCTION_NATIVE(ProceduralMapGenerator, GenerateAndLoad, ZSF_GenerateAndLoad)
{
	PARAM_PROLOGUE;
	PARAM_INT(seed);
	PARAM_STRING(theme);
	PARAM_INT(diff);
	PARAM_INT(size);
	ACTION_RETURN_INT(ZSF_GenerateAndLoad(seed, theme, diff, size));
}
