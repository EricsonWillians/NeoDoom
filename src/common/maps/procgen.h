#pragma once

#include "tarray.h"
#include "zstring.h"
#include "m_random.h"

struct MapData;

struct RoomInfo
{
	int id = -1;
	int minI = 0, maxI = 0; // inclusive column bounds
	int minJ = 0, maxJ = 0; // inclusive row bounds
	int cellCount = 0;
	int sectorIdx = -1;
	double floorZ = 0.0;
	double ceilZ = 128.0;
	FString floorTex;
	FString ceilTex;
	FString wallTex;
	FString accentTex;
	FString detailTex;
	double halfWidth = 176.0;
	double halfHeight = 176.0;
	double cornerCut = 24.0;
	int visualVariant = 0;
	// Internal composition descriptors. Spatial class is 0=connector, 1=small,
	// 2=medium, 3=major; shape family is 0=compact, 1=horizontal,
	// 2=vertical, 3=compound/bent. They are serialized only through geometry.
	int spatialClass = 1;
	int shapeFamily = 0;
	int light = 160;
	int lightColor = 0xffffff;
	int fadeColor = 0;
	bool hasPlayerStart = false;
	bool hasExit = false;
	bool hasBoss = false;
	bool hasKey = false;
	int keyType = 0;
	bool isLocked = false;
	int lockType = 0;
	int enemyCount = 0;
	int monsterTier = 1;
	bool hasWeapon = false;
	int weaponType = 0;
	bool hasAmmo = false;
	int ammoType = 0;
	int ammoCount = 0;
	bool hasHealth = false;
	int healthType = 0;
	int healthCount = 0;
	int healthBonusCount = 0;
	bool hasArmor = false;
	int armorType = 0;
	TArray<int> powerups; // progression-aware optional/secret rewards
	int distFromStart = 0; // BFS distance from player start
	int progressionRank = 9999;
	int lockStage = 0;
	int branchDepth = 0;
	bool isDeadEnd = false; // room has only one connection to other rooms
	bool reservedSecret = false; // planner-owned leaf that must remain a secret
	bool isSecret = false;  // optional dead-end reward hidden behind a secret door
	bool hasDoor = false;   // entrance has a door (monster closet)
	bool onMainPath = false;
	bool isArena = false;
	bool isHub = false;
};

struct ProcGenCell
{
	bool present = false;
	bool conn[4] = { false, false, false, false }; // N, S, W, E
	int sectorIdx = -1;
	int roomId = -1;
	int neighborCount = 0;  // present neighbors (geometric adjacency)
	int connectionCount = 0; // open connections
	double floorZ = 0.0;
	double ceilZ = 128.0;
	FString floorTex;
	FString ceilTex;
	FString wallTex;
	int light = 160;
	bool hasPlayerStart = false;
	bool hasExit = false;
	bool hasBoss = false;
	bool hasKey = false;
	int keyType = 0;    // 1=red, 2=blue, 3=yellow
	bool isLocked = false;
	int lockType = 0;   // 1=red, 2=blue, 3=yellow
	int lockDir = -1;   // boundary direction that owns the lock; -1 = legacy/all
	int enemyCount = 0;
	int monsterTier = 1;
	bool hasWeapon = false;
	int weaponType = 0;
	bool hasAmmo = false;
	int ammoType = 0;
	bool hasHealth = false;
	int healthType = 0;
	bool hasArmor = false;
	int armorType = 0;
	int pathRank = -1;
	int lockStage = 0;
	int branchDepth = 0;
	bool onMainPath = false;
	bool isArena = false;
	bool isHub = false;
	bool reservedSecret = false; // protected optional-branch endpoint
};

class FProceduralMapGenerator
{
public:
	static constexpr int MinMapSize = 1;
	// The engine accepts coordinates through +/-262144 (MAX_MAP_COORD in
	// doomdef.h); the generator keeps its own wider safety margin below.
	static constexpr int MaxMapSize = 160;
	static constexpr int DefaultMapSize = 3;
	static constexpr int DefaultStyleSetting = 1;

	FProceduralMapGenerator();

	void SetSeed(int seed);
	void SetTheme(const char* theme);
	void SetDifficulty(int difficulty); // 1-5
	void SetSize(int size);             // 1-160, affects grid dimensions
	void SetLayout(int layout);         // 0=directed, 1=balanced, 2=exploratory
	void SetVerticality(int verticality); // 0=gentle, 1=varied, 2=dramatic
	void SetDetail(int detail);         // 0=sparse, 1=detailed, 2=lavish
	void SetOutdoors(int outdoors);     // 0=enclosed, 1=mixed, 2=open
	int GetSeed() const { return Seed; }
	const FString& GetTheme() const { return Theme; }
	int GetDifficulty() const { return Difficulty; }
	int GetSize() const { return Size; }
	int GetLayout() const { return Layout; }
	int GetVerticality() const { return Verticality; }
	int GetDetail() const { return Detail; }
	int GetOutdoors() const { return Outdoors; }

	bool Generate();
	const FString& GetUDMFText() const { return UDMFBuffer; }
	const char* GetLastError() const { return LastError.GetChars(); }

	static FProceduralMapGenerator& GetInstance();

private:
	bool BuildUDMF(int W, int H);
	void MergeRooms(int W, int H);
	void PlaceWeapons(int W, int H);
	void ApplyCoherence(int W, int H);

	FRandom RNG;
	int Seed;
	FString Theme;
	int Difficulty;
	int Size;
	int Layout;
	int Verticality;
	int Detail;
	int Outdoors;

	TArray<TArray<ProcGenCell>> Grid;
	TArray<RoomInfo> Rooms;
	FString UDMFBuffer;
	FString LastError;

	static FProceduralMapGenerator Instance;
};

// A savegame carries both the deterministic recipe and the exact generated
// TEXTMAP. Keeping the TEXTMAP makes procedural saves resilient to later
// generator changes, while the recipe remains available for diagnostics and
// subsequent regeneration.
struct FProceduralMapArchiveData
{
	int Seed = 0;
	FString Theme;
	int Difficulty = 3;
	int Size = FProceduralMapGenerator::DefaultMapSize;
	int Layout = FProceduralMapGenerator::DefaultStyleSetting;
	int Verticality = FProceduralMapGenerator::DefaultStyleSetting;
	int Detail = FProceduralMapGenerator::DefaultStyleSetting;
	int Outdoors = FProceduralMapGenerator::DefaultStyleSetting;
	FString UDMF;
};

MapData* P_OpenProceduralMapData(const char* mapname);
bool P_IsProceduralMapName(const char* mapname);
FString P_GetProceduralMusic();
const FProceduralMapArchiveData* P_GetCurrentProceduralMapArchive();
bool P_StageProceduralMapArchive(int seed, const char* theme, int difficulty,
	int size, int layout, int verticality, int detail, int outdoors, FString udmf);
