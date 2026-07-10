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
	double halfWidth = 104.0;
	double halfHeight = 104.0;
	double cornerCut = 24.0;
	int visualVariant = 0;
	int light = 160;
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
	bool hasHealth = false;
	int healthType = 0;
	bool hasArmor = false;
	int armorType = 0;
	int distFromStart = 0; // BFS distance from player start
	int progressionRank = 9999;
	int branchDepth = 0;
	bool isDeadEnd = false; // room has only one connection to other rooms
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
	int branchDepth = 0;
	bool onMainPath = false;
	bool isArena = false;
	bool isHub = false;
};

class FProceduralMapGenerator
{
public:
	FProceduralMapGenerator();

	void SetSeed(int seed);
	void SetTheme(const char* theme);
	void SetDifficulty(int difficulty); // 1-5
	void SetSize(int size);             // 1-5, affects grid dimensions

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
	FString Theme;
	int Difficulty;
	int Size;

	TArray<TArray<ProcGenCell>> Grid;
	TArray<RoomInfo> Rooms;
	FString UDMFBuffer;
	FString LastError;

	static FProceduralMapGenerator Instance;
};

MapData* P_OpenProceduralMapData(const char* mapname);
bool P_IsProceduralMapName(const char* mapname);
