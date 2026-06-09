/*
** procgen.cpp
**
** Procedural map generation for BiasedDoom
** Generates UDMF TEXTMAP data in memory for runtime map loading.
**
** This file now contains only console commands, CVars, and the MapData factory.
** The generation logic lives in src/common/maps/procgen/.
**
**---------------------------------------------------------------------------
*/

#include "procgen.h"
#include "p_setup.h"
#include "filesystem.h"
#include "g_levellocals.h"
#include "c_cvars.h"
#include "c_dispatch.h"

// ---------------------------------------------------------------------------
// MapData factory
// ---------------------------------------------------------------------------

EXTERN_CVAR(Int, procgen_seed)
EXTERN_CVAR(String, procgen_theme)
EXTERN_CVAR(Int, procgen_difficulty)
EXTERN_CVAR(Int, procgen_size)

bool P_IsProceduralMapName(const char* mapname)
{
	if (!mapname) return false;
	return !stricmp(mapname, "PROCMAP") || !strnicmp(mapname, "PROC", 4);
}

MapData* P_OpenProceduralMapData(const char* mapname)
{
	if (!P_IsProceduralMapName(mapname))
		return nullptr;

	FProceduralMapGenerator& gen = FProceduralMapGenerator::GetInstance();

	// Re-seed and reconfigure from CVars to ensure deterministic generation
	// regardless of any previous Generate() calls that may have advanced the RNG.
	gen.SetSeed(procgen_seed);
	gen.SetTheme(procgen_theme);
	gen.SetDifficulty(procgen_difficulty);
	gen.SetSize(procgen_size);

	if (!gen.Generate())
	{
		Printf(TEXTCOLOR_RED "Procedural map generation failed: %s\n", gen.GetLastError());
		return nullptr;
	}

	const FString& udmf = gen.GetUDMFText();
	if (udmf.Len() == 0)
	{
		Printf(TEXTCOLOR_RED "Procedural map generation produced empty UDMF.\n");
		return nullptr;
	}

	MapData* map = new MapData;
	map->isText = true;

	FileSys::FileData data(udmf.GetChars(), udmf.Len(), true);
	map->MapLumps[ML_TEXTMAP].Reader.OpenMemoryArray(data);
	strncpy(map->MapLumps[ML_TEXTMAP].Name, "TEXTMAP", 8);

	return map;
}

//===========================================================================
//
// Console commands
//
//===========================================================================

CVAR(Int, procgen_seed, 0, CVAR_ARCHIVE);
CVAR(String, procgen_theme, "techbase", CVAR_ARCHIVE);
CVAR(Int, procgen_difficulty, 3, CVAR_ARCHIVE);
CVAR(Int, procgen_size, 3, CVAR_ARCHIVE);

CCMD(dumpprocudmf)
{
	FProceduralMapGenerator& gen = FProceduralMapGenerator::GetInstance();
	gen.SetSeed(argv.argc() > 1 ? atoi(argv[1]) : 0);
	gen.SetTheme(argv.argc() > 2 ? argv[2] : "techbase");
	gen.SetDifficulty(argv.argc() > 3 ? atoi(argv[3]) : 3);
	gen.SetSize(argv.argc() > 4 ? atoi(argv[4]) : 3);
	if (gen.Generate())
	{
		FILE* f = fopen("/tmp/procmap_test.udmf", "w");
		if (f)
		{
			fputs(gen.GetUDMFText().GetChars(), f);
			fclose(f);
			Printf("Dumped UDMF to /tmp/procmap_test.udmf (%lu bytes)\n", (unsigned long)gen.GetUDMFText().Len());
		}
	}
	else
	{
		Printf(TEXTCOLOR_RED "Generation failed: %s\n", gen.GetLastError());
	}
}

CCMD(procmap)
{
	if (argv.argc() > 1)
	{
		procgen_seed = atoi(argv[1]);
	}

	FProceduralMapGenerator& gen = FProceduralMapGenerator::GetInstance();
	gen.SetSeed(procgen_seed);
	gen.SetTheme(procgen_theme);
	gen.SetDifficulty(procgen_difficulty);
	gen.SetSize(procgen_size);

	Printf("Generating procedural map (seed=%d, theme=%s, diff=%d, size=%d)...\n",
		(int)procgen_seed, (const char*)procgen_theme, (int)procgen_difficulty, (int)procgen_size);

	// Do NOT call Generate() here. P_OpenProceduralMapData will generate
	// the map when the engine loads PROCMAP, ensuring a single generation
	// and proper MapData construction.
	 FString cmd;
	 cmd.Format("map PROCMAP");
	 C_DoCommand(cmd.GetChars());
}
