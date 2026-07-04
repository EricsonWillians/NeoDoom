/*
** procgen_rooms.cpp
**
** Room merging, coherence passes (lighting, height smoothing, sky ceilings),
** dead-end monster closets, and weapon/ammo placement.
**
**---------------------------------------------------------------------------
*/

#include "procgen_internal.h"
#include "printf.h"

using namespace ProcGen;

// ---------------------------------------------------------------------------
// Room Merging
// ---------------------------------------------------------------------------

void FProceduralMapGenerator::MergeRooms(int W, int H)
{
	Rooms.Clear();
	int totalPresentCells = 0;

	// Initialize each present cell as its own room
	for (int j = 0; j < H; j++)
	{
		for (int i = 0; i < W; i++)
		{
			if (!Grid[j][i].present) continue;
			++totalPresentCells;
			RoomInfo room;
			room.id = (int)Rooms.Size();
			room.minI = room.maxI = i;
			room.minJ = room.maxJ = j;
			room.cellCount = 1;
			Grid[j][i].roomId = room.id;
			Rooms.Push(room);
		}
	}

	const int targetAvgRoomSize = clamp(9 + (RNG() % 12) + Size + (Difficulty / 2), 6, 28);
	const int roomDensityProfile = RNG() % 5; // 0 compact, 1 tight, 2 balanced, 3 open, 4 sprawling
	const int minRoomCount = clamp((W + H) / 3, 4, 12);
	const int maxRoomCount = clamp(totalPresentCells > 0 ? totalPresentCells / 4 : 8, 6, 32);
	int targetRoomCountBase = totalPresentCells > 0 ? std::max(2, totalPresentCells / targetAvgRoomSize) : minRoomCount;
	targetRoomCountBase += (RNG() % 5) - 2; // -2..+2 jitter
	targetRoomCountBase += (roomDensityProfile == 0) ? 2 : ((roomDensityProfile == 4) ? -2 : 0);
	const int targetRoomCount = clamp(targetRoomCountBase, minRoomCount, maxRoomCount);
	const int roomShapeMode = RNG() % 4;
	const int roomSizeFamily = RNG() % 5; // 0: cathedral (few big landmarks), 1: tight maze, 2: ribbon, 3: contrast, 4: mixed
	const int roomScaleProfile = RNG() % 5; // 0 compact, 1 normal, 2 expansive, 3 alternating, 4 dramatic
	const int roomRouteContrast = RNG() % 4; // 0 balanced, 1 staircase, 2 landmark emphasis, 3 alternating pockets
	const int roomArchetype = RNG() % 6; // 0 cathedral, 1 micro cave, 2 ribbon, 3 contrast, 4 wild parity, 5 landmark pockets
	const int roomSizeVariance = RNG() % 7; // 0-6: macro variance pressure for room sizes
	const int mapRoomQuotient = targetRoomCount > 0 ? (totalPresentCells / targetRoomCount) : 4;
	const int mapLandmarkQuota = clamp(1 + (targetRoomCount / 5) + (roomRouteContrast == 2 ? 1 : 0) + (roomSizeVariance / 3) + (mapRoomQuotient / 10), 1, 6);
	const int mapPocketQuota = clamp((targetRoomCount / 3) + (roomDensityProfile == 0 ? 1 : 0) + (roomRouteContrast == 3 ? 1 : 0), 2, std::max(2, targetRoomCount / 2));
	int mapLandmarkUsed = 0;
	int mapPocketUsed = 0;

	// Cap room size based on grid dimensions
	int smallerAxis = W < H ? W : H;
	int maxRoomSize = 12 + smallerAxis / 3;
	if (maxRoomSize < 10) maxRoomSize = 10;
	if (maxRoomSize > 28) maxRoomSize = 28;

	struct GrowthProfile
	{
		int maxW = 0;
		int maxH = 0;
		int maxArea = 0;
		int targetArea = 0;
		int growthBursts = 1;
		int shapeHint = -1; // -1=none, 0=wide, 1=tall, 2=compact
		int bias = 0; // 0 = wide, 1 = tall, 2 = compact/square
		int sizeClass = 1; // 0 = tiny pocket, 1 = standard, 2 = landmark
		int sizeBandLock = -1; // -1=none, 0=micro, 1=medium, 2=macro
	};

	auto PickGrowthProfile = [&](const ProcGenCell& seed) -> GrowthProfile
	{
		GrowthProfile profile;
		int cap = maxRoomSize;
		const int avgRoomCellTarget = clamp(totalPresentCells > 0 && targetRoomCount > 0 ? totalPresentCells / targetRoomCount : 4, 4, 24);
		const int progressionTier = seed.pathRank >= 0 ? clamp(seed.pathRank, 0, 8) : 4;

		auto roll = [&](int lo, int hi) -> int
		{
			lo = clamp(lo, 2, cap);
			hi = clamp(hi, lo, cap);
			return lo + RNG() % (hi - lo + 1);
		};
		auto rollArea = [&](int lo, int hi) -> int
		{
			lo = std::max(4, lo);
			hi = std::max(lo, hi);
			return lo + (RNG() % (hi - lo + 1));
		};

		auto buildProfile = [&](int minW, int maxW, int minH, int maxH, int bias, int minArea, int maxArea, int bursts) -> GrowthProfile
		{
			GrowthProfile p;
			p.maxW = roll(minW, maxW);
			p.maxH = roll(minH, maxH);
			p.bias = bias;
			p.growthBursts = clamp(bursts, 1, 4);
			int maxCellArea = p.maxW * p.maxH;
			p.maxArea = rollArea(std::max(4, (maxCellArea * std::min(100, std::max(10, minArea))) / 100),
			                    std::max(4, (maxCellArea * std::min(140, std::max(10, maxArea))) / 100));
			p.maxArea = clamp(p.maxArea, 4, maxCellArea);
			p.targetArea = rollArea(std::max(4, p.maxArea / 3), p.maxArea);
			return p;
		};

		const bool isSpecial = seed.hasPlayerStart || seed.hasExit || seed.hasBoss || seed.hasKey || seed.isLocked ||
			seed.isArena || seed.isHub;
		const bool deepBranch = !seed.onMainPath && seed.branchDepth >= 2;
		const bool isBranch = seed.branchDepth >= 1 && !seed.onMainPath;
		const bool isLate = seed.pathRank >= 0 && seed.pathRank >= 6;
		const bool isStart = seed.hasPlayerStart;
		const int openH = (seed.conn[DIR_W] ? 1 : 0) + (seed.conn[DIR_E] ? 1 : 0);
		const int openV = (seed.conn[DIR_N] ? 1 : 0) + (seed.conn[DIR_S] ? 1 : 0);
		const int openTotal = openH + openV;
		const bool isLinearPocket = (openTotal <= 2) && ((openH == 0 || openH == openTotal) || (openV == 0 || openV == openTotal));
		const bool isCrossJunction = openTotal >= 3;
		const bool isTerminalPocket = (openTotal <= 1) || (seed.neighborCount <= 1 && seed.connectionCount <= 1);
		const bool isMainlineNode = seed.onMainPath && (seed.pathRank >= 0);
		const bool isLateMainlineNode = isMainlineNode && seed.pathRank >= 5;
		const bool isMainlinePulseAnchor = seed.onMainPath && (seed.pathRank % 3 == 0) &&
			(seed.pathRank <= clamp(targetRoomCount / 2, 1, 10));
		const int classRoll = (roomSizeVariance * 29 + (seed.pathRank >= 0 ? seed.pathRank * 19 : 0) +
		                       seed.branchDepth * 23 + openTotal * 17 + (seed.onMainPath ? 13 : 7) +
		                       (seed.isHub || seed.isArena ? 31 : 0) + (isCrossJunction ? 11 : 0)) % 100;
		const int sizeVarianceRoll = (roomSizeVariance * 31 +
		                         (seed.pathRank >= 0 ? seed.pathRank * 17 : 0) +
		                         seed.branchDepth * 13 +
		                         openTotal * 9 +
		                         (seed.onMainPath ? 7 : 17) +
		                         (seed.isHub || seed.isArena ? 23 : 0) +
		                         (isLinearPocket ? 11 : 0) +
		                         (isCrossJunction ? 19 : 0)) % 100;
		const int personaBand = clamp((isMainlineNode ? 6 : 2) + (isCrossJunction ? 2 : 0) + (seed.isHub ? 3 : 0) + (seed.isArena ? 2 : 0)
			- (deepBranch ? 2 : 0) - (isTerminalPocket ? 1 : 0) + (seed.pathRank >= 0 ? seed.pathRank / 2 : 0), 0, 12);
		const bool prefersWide = (personaBand % 3 == 0) || seed.onMainPath;

		const bool isClassLandmarkCandidate = seed.hasPlayerStart || seed.hasBoss || seed.hasExit || seed.isHub || seed.isArena ||
		                                     isLateMainlineNode || isMainlinePulseAnchor;
		profile.sizeClass = 1;
		if (isClassLandmarkCandidate)
		{
			profile.sizeClass = 2;
			mapLandmarkUsed = std::min(mapLandmarkQuota, mapLandmarkUsed + 1);
		}
		else if (isTerminalPocket || deepBranch || (classRoll <= 20))
		{
			if (mapPocketUsed < mapPocketQuota)
			{
				profile.sizeClass = 0;
				mapPocketUsed++;
			}
		}
		else if (classRoll >= 86 && mapLandmarkUsed < mapLandmarkQuota)
		{
			profile.sizeClass = 2;
			mapLandmarkUsed++;
		}

		if (profile.sizeClass == 1)
		{
			if (!seed.onMainPath && seed.branchDepth <= 1 && classRoll <= 24 && mapPocketUsed < mapPocketQuota)
			{
				profile.sizeClass = 0;
				mapPocketUsed++;
			}
		}

		const int silhouetteSeed = (seed.pathRank >= 0 ? seed.pathRank : 0) * 31 +
		                          seed.branchDepth * 17 +
		                          (seed.onMainPath ? 29 : 11) +
		                          openTotal * 13 +
		                          (seed.isHub || seed.isArena ? 19 : 0) +
		                          (seed.isLocked ? 23 : 0) +
		                          (seed.hasKey || seed.hasBoss || seed.hasExit || seed.hasPlayerStart ? 41 : 0);
		const int roomSilhouetteClass = silhouetteSeed % 3;

		if (profile.sizeClass == 2)
		{
			if (roomSilhouetteClass == 0)
			{
				profile.maxW = std::min(cap, std::max(profile.maxW, std::max(4, cap / 2)));
				profile.maxH = std::max(3, std::min(profile.maxH, std::max(3, cap / 3)));
				profile.bias = 0;
			}
			else if (roomSilhouetteClass == 1)
			{
				profile.maxH = std::min(cap, std::max(profile.maxH, std::max(4, cap / 2)));
				profile.maxW = std::max(3, std::min(profile.maxW, std::max(3, cap / 3)));
				profile.bias = 1;
			}
			else
			{
				profile.maxW = std::max(4, profile.maxW - (RNG() % 2));
				profile.maxH = std::max(4, profile.maxH - (RNG() % 2));
				profile.bias = 2;
			}
			profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxArea * 90 / 100));
			profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, profile.maxArea * 92 / 100));
		}
		else if (profile.sizeClass == 0)
		{
			if (roomSilhouetteClass == 0)
			{
				profile.maxW = std::max(2, std::min(profile.maxW, std::max(2, cap / 2)));
				profile.maxH = std::max(2, std::min(profile.maxH, 4));
				profile.bias = 0;
			}
			else if (roomSilhouetteClass == 1)
			{
				profile.maxH = std::max(2, std::min(profile.maxH, std::max(2, cap / 2)));
				profile.maxW = std::max(2, std::min(profile.maxW, 4));
				profile.bias = 1;
			}
			else
			{
				profile.maxW = std::max(2, std::min(profile.maxW, std::max(2, cap / 4)));
				profile.maxH = std::max(2, std::min(profile.maxH, std::max(2, cap / 4)));
				profile.bias = 2;
			}
			profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(4, profile.maxArea * 78 / 100));
			profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 60 / 100)));
		}

		if (profile.sizeClass == 2)
		{
			const int minLandmarkW = std::max(4, std::min(cap, std::max(4, mapRoomQuotient - (seed.onMainPath ? 0 : 1))));
			const int minLandmarkH = std::max(4, std::min(cap, std::max(4, (mapRoomQuotient * 3) / 4 + (seed.onMainPath ? 1 : 0))));
			profile.maxW = std::max(profile.maxW, minLandmarkW);
			profile.maxH = std::max(profile.maxH, minLandmarkH);
			profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxW * profile.maxH * 70 / 100));
			profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, profile.maxArea * 92 / 100));
		}
		else if (profile.sizeClass == 0)
		{
			const int maxPocket = std::max(2, std::min(6, std::max(2, mapRoomQuotient / 2)));
			profile.maxW = std::min(profile.maxW, maxPocket);
			profile.maxH = std::min(profile.maxH, maxPocket);
			profile.maxArea = std::min(profile.maxArea, profile.maxW * profile.maxH);
			profile.targetArea = std::min(profile.targetArea, std::max(4, profile.maxArea * 70 / 100));
		}

		if (isSpecial)
		{
			switch (RNG() % 7)
			{
			case 0: profile = buildProfile(6, cap, 3, clamp(cap, 5, 8), 0, 55, 95, 3); break;
			case 1: profile = buildProfile(4, clamp(cap, 5, 8), 5, cap, 1, 55, 95, 3); break;
			case 2: profile = buildProfile(std::max(6, cap - 6), cap, std::max(6, cap - 6), cap, 2, 65, 120, 3); break;
			case 3: profile = buildProfile(3, cap, 3, clamp(cap, 5, 7), 2, 35, 75, 2); break;
			case 4: profile = buildProfile(7, cap, 4, clamp(cap, 5, 9), 0, 50, 100, 2); break;
			case 5: profile = buildProfile(3, clamp(cap, 5, 7), 7, cap, 1, 50, 100, 2); break;
			default: profile = buildProfile(5, clamp(cap, 7, 11), 5, clamp(cap, 7, 11), 2, 45, 85, 2); break;
			}
		}
		else if (isCrossJunction)
		{
			switch (RNG() % 8)
			{
			case 0: profile = buildProfile(std::max(6, avgRoomCellTarget), cap, std::max(6, avgRoomCellTarget), cap, 2, 60, 130, 4); break;
			case 1: profile = buildProfile(5, cap, 5, cap, 0, 55, 115, 3); break;
			case 2: profile = buildProfile(4, clamp(cap, 6, 10), 4, clamp(cap, 6, 10), 2, 55, 95, 3); break;
			case 3: profile = buildProfile(std::max(7, cap - 5), cap, 4, std::max(4, cap - 3), 0, 45, 95, 4); break;
			case 4: profile = buildProfile(4, std::max(4, cap - 3), 6, cap, 1, 50, 110, 4); break;
			case 5: profile = buildProfile(6, clamp(cap, 10, 12), 6, clamp(cap, 10, 12), 2, 60, 120, 3); break;
			default: profile = buildProfile(5, clamp(cap, 8, 12), 3, clamp(cap, 6, 10), 2, 48, 102, 3); break;
			}
		}
		else if (isLinearPocket)
		{
			switch (RNG() % 8)
			{
			case 0: profile = buildProfile(2, cap, 2, 4, 0, 20, 55, 1); break;
			case 1: profile = buildProfile(3, 8, 2, 3, 0, 24, 55, 2); break;
			case 2: profile = buildProfile(2, 3, 2, std::max(3, cap - 4), 1, 22, 65, 2); break;
			case 3: profile = buildProfile(std::max(2, cap - 6), cap, 2, 2, 0, 18, 58, 2); break;
			case 4: profile = buildProfile(2, 4, 2, 5, 2, 18, 60, 1); break;
			case 5: profile = buildProfile(2, 6, 2, 3, 1, 24, 65, 1); break;
			case 6: profile = buildProfile(2, std::max(3, cap - 4), 2, 3, 0, 20, 62, 1); break;
			default: profile = buildProfile(2, std::max(4, cap - 3), 2, std::max(2, cap - 6), 1, 22, 68, 1); break;
			}
		}
		else if (isLate)
		{
			switch (RNG() % 7)
			{
			case 0: profile = buildProfile(6, cap, 6, cap, 2, 55, 130, 3); break;
			case 1: profile = buildProfile(5, cap, 3, clamp(cap, 5, 10), 0, 50, 110, 3); break;
			case 2: profile = buildProfile(3, clamp(cap, 5, 10), 5, cap, 1, 50, 110, 2); break;
			case 3: profile = buildProfile(6, cap, 4, 4, 0, 55, 95, 2); break;
			case 4: profile = buildProfile(4, 4, 6, cap, 1, 55, 95, 2); break;
			default: profile = buildProfile(6, cap, 6, cap, 2, 60, 120, 3); break;
			}
		}
		else if (deepBranch)
		{
			switch (RNG() % 7)
			{
			case 0: profile = buildProfile(2, 3, 2, 3, 2, 28, 55, 1); break;
			case 1: profile = buildProfile(2, 4, 2, 4, 0, 30, 65, 1); break;
			case 2: profile = buildProfile(2, 4, 3, 6, 1, 32, 70, 1); break;
			case 3: profile = buildProfile(3, 7, 2, 4, 0, 35, 75, 2); break;
			case 4: profile = buildProfile(2, 5, 2, 5, 2, 25, 60, 1); break;
			default: profile = buildProfile(2, 6, 2, 3, 1, 30, 70, 1); break;
			}
		}
		else if (seed.onMainPath || isBranch)
		{
			switch (RNG() % 7)
			{
			case 0: profile = buildProfile(4, std::max(6, avgRoomCellTarget), 4, std::max(6, avgRoomCellTarget), 2, 45, 95, 2); break;
			case 1:
			case 2: profile = buildProfile(4, cap, 3, clamp(cap, 4, 7), 0, 45, 95, 2); break;
			case 3:
			case 4: profile = buildProfile(3, clamp(cap, 4, 7), 4, cap, 1, 45, 95, 2); break;
			case 5: profile = buildProfile(5, cap, 5, cap, 2, 55, 105, 3); break;
			default: profile = buildProfile(3, std::max(4, cap - 2), 3, std::max(4, cap - 2), 2, 35, 75, 1); break;
			}
		}
		else
		{
			switch (RNG() % 7)
			{
			case 0: profile = buildProfile(2, clamp(cap, 4, 8), 2, clamp(cap, 4, 8), 2, 35, 75, 1); break;
			case 1: profile = buildProfile(2, clamp(cap, 5, 10), 2, std::max(3, cap - 4), 0, 30, 70, 1); break;
			case 2: profile = buildProfile(2, std::max(3, cap - 4), 2, clamp(cap, 5, 10), 1, 30, 70, 1); break;
			case 3: profile = buildProfile(3, std::max(5, cap - 4), 3, cap, 2, 40, 80, 2); break;
			case 4: profile = buildProfile(2, std::max(3, cap - 3), 2, 6, 0, 30, 65, 1); break;
			default: profile = buildProfile(2, 6, 2, cap, 1, 30, 65, 1); break;
			}
		}

		if (isMainlineNode)
		{
			if (isLateMainlineNode || seed.isHub || seed.isArena)
			{
				profile.maxW = std::min(cap, profile.maxW + 2 + (seed.isHub || seed.isArena));
				profile.maxH = std::min(cap, profile.maxH + 2 + (seed.isHub || seed.isArena));
				profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxArea * 150 / 100));
				profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, profile.maxArea * 90 / 100));
				profile.growthBursts = std::min(4, profile.growthBursts + 1);
			}
			else if (personaBand >= 8)
			{
				profile.maxW = std::min(cap, profile.maxW + 1);
				profile.maxH = std::max(2, profile.maxH - 1);
				profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, profile.maxArea * 85 / 100));
				profile.growthBursts = std::min(4, profile.growthBursts + 1);
				profile.bias = 0;
			}
			else if (personaBand <= 3)
			{
				profile.maxW = std::max(2, profile.maxW - 1);
				profile.maxH = std::max(2, profile.maxH - 1);
				profile.maxArea = std::max(4, profile.maxArea * 72 / 100);
				profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 76 / 100)));
				profile.growthBursts = std::max(1, profile.growthBursts - 1);
				profile.bias = 2;
			}
		}
		else if (deepBranch)
		{
			profile.maxW = std::max(2, profile.maxW - (prefersWide ? 1 : 0));
			profile.maxH = std::max(2, profile.maxH - (prefersWide ? 0 : 1));
			profile.maxArea = std::max(4, profile.maxArea * 72 / 100);
			profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 68 / 100)));
			profile.growthBursts = std::max(1, profile.growthBursts - 1);
			profile.bias = 2;
		}
		else if (isTerminalPocket)
		{
			profile.maxW = std::max(2, profile.maxW - 1);
			profile.maxH = std::max(2, profile.maxH - 1);
			profile.maxArea = std::max(4, profile.maxArea * 60 / 100);
			profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 70 / 100)));
			profile.growthBursts = std::max(1, profile.growthBursts - 1);
		}
		else if (isCrossJunction && seed.pathRank <= 1)
		{
			profile.maxW = std::min(cap, profile.maxW + 1);
			profile.maxH = std::min(cap, profile.maxH + 1);
			profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxArea * 128 / 100));
			profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, profile.maxArea * 88 / 100));
			profile.growthBursts = std::min(4, profile.growthBursts + 1);
		}
		else if (prefersWide)
		{
			profile.bias = ((isCrossJunction ? 0 : RNG() % 2) == 0) ? 0 : 1;
			if (profile.bias == 0)
				profile.maxW = std::min(cap, profile.maxW + 1);
			else
				profile.maxH = std::min(cap, profile.maxH + 1);
		}
		else
		{
			if (profile.maxW > 3 && profile.maxH > 3 && RNG() % 2 == 0)
				profile.bias = 2;
		}

		const int variationRoll = RNG() % 100;
		if (variationRoll >= 82 && !isTerminalPocket)
		{
			profile.maxW = std::min(cap, profile.maxW + 2);
			profile.maxH = std::min(cap, profile.maxH + 2);
			profile.growthBursts = std::min(4, profile.growthBursts + 1);
			profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxArea * 130 / 100));
			profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, profile.maxArea * 85 / 100));
		}
		else if (variationRoll <= 18 && !isStart)
		{
			profile.maxW = std::max(2, profile.maxW - 1);
			profile.maxH = std::max(2, profile.maxH - 1);
			profile.maxArea = std::max(4, profile.maxArea * 60 / 100);
			profile.targetArea = std::max(3, std::min(profile.targetArea, profile.maxArea));
			profile.growthBursts = std::max(1, profile.growthBursts - 1);
			if (isLinearPocket && (variationRoll % 2) == 0)
				profile.bias = (variationRoll % 3 == 0) ? 0 : 1;
		}

		const int shapeRoll = RNG() % 100;
		if (!isTerminalPocket)
		{
			if (shapeRoll >= 86)
			{
				// Expansive, visually distinct rooms.
				if (RNG() % 2 == 0)
				{
					profile.maxW = std::min(cap, std::max(2, profile.maxW + (seed.conn[DIR_W] && seed.conn[DIR_E] ? 2 : 1)));
					profile.maxH = std::max(2, profile.maxH - 1);
					profile.bias = 0;
				}
				else
				{
					profile.maxH = std::min(cap, std::max(2, profile.maxH + (seed.conn[DIR_N] && seed.conn[DIR_S] ? 2 : 1)));
					profile.maxW = std::max(2, profile.maxW - 1);
					profile.bias = 1;
				}
				profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxArea * 140 / 100));
				profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, profile.maxArea * 90 / 100));
				profile.growthBursts = std::min(4, profile.growthBursts + 1);
			}
			else if (shapeRoll <= 22 && !isStart)
			{
				// Compact variants create smaller, irregular pockets to increase contrast.
				profile.maxArea = std::max(4, profile.maxArea * 75 / 100);
				profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 85 / 100)));
				profile.maxW = std::max(2, profile.maxW - (RNG() % 2));
				profile.maxH = std::max(2, profile.maxH - (RNG() % 2));
				if (profile.maxW <= 3 || profile.maxH <= 3)
					profile.bias = 2;
			}
		}

		if (isStart)
		{
			// Give the player a less uniform start room baseline.
			profile.maxW = std::min(cap, profile.maxW + 1 + (RNG() % 2));
			profile.maxH = std::min(cap, profile.maxH + 1);
			profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxArea * 150 / 100));
			profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, profile.maxArea * 88 / 100));
			profile.growthBursts = std::min(4, profile.growthBursts + 1);
		}

		// Additional global size-band pass to enforce meaningful room scale contrast.
		if (!isStart && !isSpecial)
		{
			if (sizeVarianceRoll >= 95)
			{
				profile.maxW = std::min(cap, profile.maxW + 3 + (seed.onMainPath ? 1 : 0));
				profile.maxH = std::min(cap, profile.maxH + 3);
				profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxArea * 190 / 100));
				profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, profile.maxArea * 97 / 100));
				profile.growthBursts = std::min(4, profile.growthBursts + 2);
				profile.bias = ((seed.conn[DIR_W] && seed.conn[DIR_E]) ? 0 : (seed.conn[DIR_N] && seed.conn[DIR_S] ? 1 : RNG() % 2));
			}
			else if (sizeVarianceRoll >= 78)
			{
				profile.maxW = std::min(cap, profile.maxW + 2);
				profile.maxH = std::min(cap, profile.maxH + 2);
				profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxArea * 150 / 100));
				profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, profile.maxArea * 90 / 100));
				profile.growthBursts = std::min(4, profile.growthBursts + 1);
			}
			else if (sizeVarianceRoll <= 16 && !isTerminalPocket)
			{
				profile.maxW = std::max(2, profile.maxW - 2);
				profile.maxH = std::max(2, profile.maxH - 2);
				profile.maxArea = std::max(4, profile.maxArea * 64 / 100);
				profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 62 / 100)));
				profile.growthBursts = std::max(1, profile.growthBursts - 1);
				profile.bias = 2;
			}
			else if (sizeVarianceRoll <= 36 && !seed.onMainPath && !seed.isHub && !seed.isArena && !deepBranch)
			{
				profile.maxW = std::max(2, profile.maxW - 1);
				profile.maxH = std::max(2, profile.maxH - 1);
				profile.maxArea = std::max(4, profile.maxArea * 76 / 100);
				profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 72 / 100)));
				profile.growthBursts = std::max(1, profile.growthBursts - 1);
				profile.bias = 2;
			}
		}

		if (profile.sizeClass == 2)
		{
			const int classBonusW = seed.isHub ? 2 : (seed.onMainPath ? 1 : 0);
			const int classBonusH = seed.isHub ? 2 : (seed.onMainPath ? 1 : 0);
			profile.maxW = std::min(cap, profile.maxW + 1 + classBonusW);
			profile.maxH = std::min(cap, profile.maxH + 1 + classBonusH);
			profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxArea * 170 / 100));
			profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, profile.maxArea * 92 / 100));
			profile.growthBursts = std::min(4, profile.growthBursts + 1);
			if (profile.maxW >= profile.maxH + 3)
				profile.bias = 0;
			else if (profile.maxH >= profile.maxW + 3)
				profile.bias = 1;
		}
		else if (profile.sizeClass == 0)
		{
			profile.maxW = std::max(2, profile.maxW - 2);
			profile.maxH = std::max(2, profile.maxH - 2);
			profile.maxArea = std::max(4, profile.maxArea * 56 / 100);
			profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 54 / 100)));
			profile.growthBursts = std::max(1, profile.growthBursts - 1);
			profile.bias = 2;
		}
		else
		{
			// Standard room keeps the middle ground, small random drift.
			if (RNG() % 2 == 0)
			{
				profile.maxW = std::max(2, profile.maxW + (RNG() % 2) - 1);
				profile.maxH = std::max(2, profile.maxH + (RNG() % 2));
			}
		}

		auto ApplyMapRoomMode = [](GrowthProfile&, bool, const ProcGenCell&) {
			// TODO: implement archetype-specific room mode adjustments
		};
		ApplyMapRoomMode(profile, isTerminalPocket, seed);

		profile.maxW = clamp(profile.maxW, 2, cap);
		profile.maxH = clamp(profile.maxH, 2, cap);
		profile.maxArea = clamp(profile.maxArea, 4, profile.maxW * profile.maxH);
		profile.targetArea = clamp(profile.targetArea, 3, profile.maxArea);
		if (seed.branchDepth >= 2)
			profile.targetArea = std::min(profile.targetArea, std::max(4, profile.maxArea - 6));
		if (isTerminalPocket)
		{
			profile.maxArea = std::max(4, profile.maxArea * 60 / 100);
			profile.targetArea = std::min(profile.targetArea, std::max(4, profile.maxArea * 70 / 100));
		}
		if (isCrossJunction)
		{
			profile.targetArea = std::max(profile.targetArea, std::max(4, (profile.maxArea * 3) / 4));
		}

		if (profile.sizeClass == 2)
		{
			if (roomSilhouetteClass == 0)
			{
				profile.maxW = std::min(cap, std::max(profile.maxW, std::max(4, cap / 2)));
				profile.maxH = std::max(3, std::min(profile.maxH, std::max(3, cap / 3)));
				profile.bias = 0;
			}
			else if (roomSilhouetteClass == 1)
			{
				profile.maxH = std::min(cap, std::max(profile.maxH, std::max(4, cap / 2)));
				profile.maxW = std::max(3, std::min(profile.maxW, std::max(3, cap / 3)));
				profile.bias = 1;
			}
			else
			{
				profile.maxW = std::max(4, profile.maxW - 1);
				profile.maxH = std::max(4, profile.maxH - 1);
				profile.bias = 2;
			}
			profile.maxArea = std::max(4, std::min(profile.maxArea, profile.maxW * profile.maxH));
			profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 90 / 100)));
		}
		else if (profile.sizeClass == 0)
		{
			if (roomSilhouetteClass == 0)
			{
				profile.maxW = std::max(2, std::min(profile.maxW, std::max(2, cap / 2)));
				profile.maxH = std::max(2, std::min(profile.maxH, 4));
				profile.bias = 0;
			}
			else if (roomSilhouetteClass == 1)
			{
				profile.maxH = std::max(2, std::min(profile.maxH, std::max(2, cap / 2)));
				profile.maxW = std::max(2, std::min(profile.maxW, 4));
				profile.bias = 1;
			}
			else
			{
				profile.maxW = std::max(2, std::min(profile.maxW, std::max(2, cap / 4)));
				profile.maxH = std::max(2, std::min(profile.maxH, std::max(2, cap / 4)));
				profile.bias = 2;
			}
			profile.maxArea = std::max(4, std::min(profile.maxArea, profile.maxW * profile.maxH));
			profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 58 / 100)));
		}
		else
		{
			if (roomSilhouetteClass == 0 && profile.maxW < cap)
				profile.maxW = std::min(cap, profile.maxW + 1);
			else if (roomSilhouetteClass == 1 && profile.maxH < cap)
				profile.maxH = std::min(cap, profile.maxH + 1);
			else if (roomSilhouetteClass == 2)
				profile.bias = 2;
		}

			if (progressionTier <= 2 && !isStart && !seed.onMainPath)
			{
				profile.maxArea = std::max(4, profile.maxArea - (profile.maxArea / 3));
				profile.targetArea = std::max(profile.targetArea, std::max(4, (profile.maxArea * 2) / 3));
			}
			else if (progressionTier >= 5 && seed.onMainPath)
			{
				profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(4, profile.maxArea + (profile.maxArea / 2)));
				profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, (profile.maxArea * 3) / 4));
				profile.growthBursts = std::min(4, profile.growthBursts + 1);
			}
		else if (seed.pathRank >= 3 && seed.onMainPath)
		{
			profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(4, profile.maxArea + (profile.maxArea / 3)));
			profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, std::max(4, (profile.maxArea * 2) / 3)));
			profile.growthBursts = std::min(4, profile.growthBursts + 1);
		}
		if (!seed.onMainPath && progressionTier <= 2 && seed.branchDepth <= 1)
		{
			profile.maxArea = std::max(4, profile.maxArea - (profile.maxArea / 4));
			profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, (profile.maxArea * 85) / 100)));
			profile.maxW = std::max(2, profile.maxW - 1);
			profile.maxH = std::max(2, profile.maxH - 1);
		}

		return profile;
	};

		TArray<GrowthProfile> growthProfiles;
		growthProfiles.Resize(Rooms.Size());
		for (unsigned int i = 0; i < growthProfiles.Size(); i++)
			growthProfiles[i].maxW = 0;

		auto FindRoomSeed = [&](const RoomInfo& room) -> const ProcGenCell*
		{
			for (int sj = room.minJ; sj <= room.maxJ; sj++)
			{
				for (int si = room.minI; si <= room.maxI; si++)
				{
					const ProcGenCell& candidate = Grid[sj][si];
					if (candidate.roomId == room.id)
					{
						return &candidate;
					}
				}
			}
			return nullptr;
		};

		auto ScoreRoomForLandmarkPromotion = [&](const ProcGenCell& seed) -> int
		{
			int openH = (seed.conn[DIR_W] ? 1 : 0) + (seed.conn[DIR_E] ? 1 : 0);
			int openV = (seed.conn[DIR_N] ? 1 : 0) + (seed.conn[DIR_S] ? 1 : 0);
			int openTotal = openH + openV;
			int score = 0;
			if (seed.onMainPath) score += 16;
			if (seed.isHub || seed.isArena) score += 10;
			if (seed.isLocked || seed.hasKey || seed.hasBoss || seed.hasExit || seed.hasPlayerStart) score += 9;
			score += clamp(seed.pathRank >= 0 ? seed.pathRank : 0, 0, 10) * 4;
			score += seed.branchDepth * 3;
			score += openTotal * 2;
			return score;
		};

		auto ScoreRoomForPocketPromotion = [&](const ProcGenCell& seed) -> int
		{
			int openH = (seed.conn[DIR_W] ? 1 : 0) + (seed.conn[DIR_E] ? 1 : 0);
			int openV = (seed.conn[DIR_N] ? 1 : 0) + (seed.conn[DIR_S] ? 1 : 0);
			int openTotal = openH + openV;
			int score = 0;
			if (!seed.onMainPath) score += 12;
			if (seed.branchDepth > 1) score += 8;
			if (seed.conn[DIR_N] + seed.conn[DIR_S] + seed.conn[DIR_W] + seed.conn[DIR_E] <= 2) score += 8;
			if (openTotal <= 1) score += 10;
			if (seed.neighborCount <= 1) score += 4;
			if (seed.onMainPath) score = std::max(0, score - 20);
			return score;
		};

		auto ForceRoomSizeClass = [&](GrowthProfile& profile, const ProcGenCell& seed, int sizeClass) -> void
		{
			profile.sizeClass = clamp(sizeClass, 0, 2);
			if (profile.sizeClass == 2)
			{
				int floorW = std::max(4, std::min(maxRoomSize, mapRoomQuotient));
				int floorH = std::max(4, std::min(maxRoomSize, std::max(4, (mapRoomQuotient * 3) / 5)));
				profile.maxW = std::max(profile.maxW, floorW);
				profile.maxH = std::max(profile.maxH, floorH);
				profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxW * profile.maxH * 90 / 100));
				profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, std::max(4, profile.maxArea * 88 / 100)));
				profile.growthBursts = std::min(4, profile.growthBursts + 1);
				profile.bias = (seed.onMainPath ? (RNG() % 2) : 2); // keep asymmetric options for landmark anchors
			}
			else if (profile.sizeClass == 0)
			{
				int capPocket = std::max(2, std::min(5, std::max(2, mapRoomQuotient / 3)));
				profile.maxW = std::max(2, std::min(profile.maxW, capPocket));
				profile.maxH = std::max(2, std::min(profile.maxH, capPocket));
				profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(4, profile.maxArea * 66 / 100));
				profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 58 / 100)));
				profile.growthBursts = std::max(1, profile.growthBursts - 1);
				profile.bias = 2;
			}
		};

		auto MergeRoomSizeClass = [&](int currentClass, int currentCells, int incomingClass, int incomingCells) -> int
		{
			currentClass = clamp(currentClass, 0, 2);
			incomingClass = clamp(incomingClass, 0, 2);
			if (incomingClass <= currentClass) return currentClass;
			int normalizedCurrentCells = std::max(1, currentCells);
			if (incomingCells >= (normalizedCurrentCells * 2))
				return incomingClass;
			if (incomingCells >= normalizedCurrentCells)
				return clamp(currentClass + 1, 0, 2);
			return currentClass;
		};

		TArray<int> roomSeedIds;
		for (int rid = 0; rid < (int)Rooms.Size(); rid++)
		{
			if (Rooms[rid].id < 0) continue;
			const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
			if (roomSeed != nullptr)
			{
				growthProfiles[rid] = PickGrowthProfile(*roomSeed);
				roomSeedIds.Push(rid);
			}
		}

		int currentLandmarks = 0;
		int currentPockets = 0;
		for (int rid = 0; rid < (int)roomSeedIds.Size(); rid++)
		{
			const GrowthProfile& profile = growthProfiles[roomSeedIds[rid]];
			if (profile.sizeClass == 2) currentLandmarks++;
			else if (profile.sizeClass == 0) currentPockets++;
		}

		for (int missing = mapLandmarkQuota - currentLandmarks; missing > 0; missing--)
		{
			int bestRid = -1;
			int bestScore = -1;
			for (int k = 0; k < (int)roomSeedIds.Size(); k++)
			{
				int rid = roomSeedIds[k];
				if (growthProfiles[rid].sizeClass == 2) continue;
				const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
				if (roomSeed == nullptr) continue;
				int score = ScoreRoomForLandmarkPromotion(*roomSeed);
				if (score > bestScore)
				{
					bestScore = score;
					bestRid = rid;
				}
			}
			if (bestRid < 0) break;
			const ProcGenCell* roomSeed = FindRoomSeed(Rooms[bestRid]);
			if (roomSeed != nullptr)
			{
				ForceRoomSizeClass(growthProfiles[bestRid], *roomSeed, 2);
				currentLandmarks++;
			}
		}

		for (int missing = mapPocketQuota - currentPockets; missing > 0; missing--)
		{
			int bestRid = -1;
			int bestScore = -1;
			for (int k = 0; k < (int)roomSeedIds.Size(); k++)
			{
				int rid = roomSeedIds[k];
				if (growthProfiles[rid].sizeClass != 1) continue;
				const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
				if (roomSeed == nullptr) continue;
				int score = ScoreRoomForPocketPromotion(*roomSeed);
				if (score > bestScore)
				{
					bestScore = score;
					bestRid = rid;
				}
			}
			if (bestRid < 0) break;
			const ProcGenCell* roomSeed = FindRoomSeed(Rooms[bestRid]);
			if (roomSeed != nullptr)
			{
				ForceRoomSizeClass(growthProfiles[bestRid], *roomSeed, 0);
				currentPockets++;
			}
		}

		auto IsFeatureSeed = [&](const ProcGenCell& seed) -> bool
		{
			return seed.hasPlayerStart || seed.hasExit || seed.hasBoss || seed.hasKey;
		};

		auto ScoreRoomForLandmarkContrast = [&](const ProcGenCell& seed) -> int
		{
			int score = 0;
			const int openH = (seed.conn[DIR_W] ? 1 : 0) + (seed.conn[DIR_E] ? 1 : 0);
			const int openV = (seed.conn[DIR_N] ? 1 : 0) + (seed.conn[DIR_S] ? 1 : 0);
			const int openTotal = openH + openV;
			score += seed.onMainPath ? 20 : 0;
			score += (seed.isHub || seed.isArena) ? 18 : 0;
			score += (seed.isLocked ? 12 : 0);
			score += seed.branchDepth * 4;
			score += seed.pathRank >= 0 ? clamp(seed.pathRank * 2, 0, 20) : 0;
			score += openTotal * 3;
			if (!seed.onMainPath && seed.pathRank >= 1) score += 8;
			return score;
		};

		auto ScoreRoomForPocketContrast = [&](const ProcGenCell& seed) -> int
		{
			int score = 0;
			const int openH = (seed.conn[DIR_W] ? 1 : 0) + (seed.conn[DIR_E] ? 1 : 0);
			const int openV = (seed.conn[DIR_N] ? 1 : 0) + (seed.conn[DIR_S] ? 1 : 0);
			const int openTotal = openH + openV;
			if (!seed.onMainPath)
			{
				score += 20;
				score += (seed.branchDepth == 0) ? 12 : 0;
				score += (seed.branchDepth > 1) ? 8 : 0;
				score += 10 - (std::max(1, openTotal) * 2);
			}
			score += (seed.neighborCount <= 1 ? 10 : 0);
			score += (seed.connectionCount <= 1 ? 10 : 0);
			if (seed.onMainPath) score -= 12;
			return clamp(score, 0, 200);
		};

		auto EnforceContrastRoomSize = [&](GrowthProfile& profile, const ProcGenCell& seed, int targetClass, bool extreme) -> void
		{
			ForceRoomSizeClass(profile, seed, targetClass);
			if (extreme)
			{
				if (targetClass == 2)
				{
					profile.maxW = std::min(maxRoomSize, profile.maxW + 2);
					profile.maxH = std::min(maxRoomSize, profile.maxH + 2);
					profile.maxArea = std::min(profile.maxW * profile.maxH,
						std::max(profile.maxArea, profile.maxW * profile.maxH * 96 / 100));
					profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, profile.maxArea * 95 / 100));
					profile.growthBursts = std::min(4, profile.growthBursts + 2);
					profile.bias = (seed.isHub || seed.isArena || seed.onMainPath) ? ((RNG() % 2) == 0 ? 0 : 1) : 2;
				}
				else if (targetClass == 0)
				{
					profile.maxW = std::max(2, profile.maxW - 1);
					profile.maxH = std::max(2, profile.maxH - 1);
					profile.maxArea = std::max(4, profile.maxArea * 48 / 100);
					profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 46 / 100)));
					profile.growthBursts = std::max(1, profile.growthBursts - 1);
					profile.bias = 2;
				}
			}
		};

		auto ScoreRoomForWideSilhouette = [&](const ProcGenCell& seed) -> int
		{
			int score = 0;
			const int openH = (seed.conn[DIR_W] ? 1 : 0) + (seed.conn[DIR_E] ? 1 : 0);
			const int openV = (seed.conn[DIR_N] ? 1 : 0) + (seed.conn[DIR_S] ? 1 : 0);
			score += openH * 4;
			score += (openV == 0 || openH >= 2) ? 8 : 0;
			score += seed.onMainPath ? 6 : 0;
			score += seed.branchDepth <= 1 ? 3 : 0;
			score += seed.neighborCount <= 1 ? 4 : 0;
			return clamp(score, 0, 200);
		};

		auto ScoreRoomForTallSilhouette = [&](const ProcGenCell& seed) -> int
		{
			int score = 0;
			const int openH = (seed.conn[DIR_W] ? 1 : 0) + (seed.conn[DIR_E] ? 1 : 0);
			const int openV = (seed.conn[DIR_N] ? 1 : 0) + (seed.conn[DIR_S] ? 1 : 0);
			score += openV * 4;
			score += (openH == 0 || openV >= 2) ? 8 : 0;
			score += seed.branchDepth > 1 ? 6 : 0;
			score += seed.neighborCount <= 2 ? 4 : 0;
			return clamp(score, 0, 200);
		};

		auto ScoreRoomForCompactSilhouette = [&](const ProcGenCell& seed) -> int
		{
			int score = 0;
			const int openTotal = (seed.conn[DIR_N] ? 1 : 0) + (seed.conn[DIR_S] ? 1 : 0) +
				(seed.conn[DIR_W] ? 1 : 0) + (seed.conn[DIR_E] ? 1 : 0);
			score += (seed.connectionCount <= 2 ? 12 : 0);
			score += (!seed.onMainPath ? 10 : 4);
			score += (seed.branchDepth >= 2 ? 6 : 0);
			score += (openTotal <= 2 ? 8 : 0);
			score += (seed.conn[DIR_N] == 0 && seed.conn[DIR_S] == 0 ? 4 : 0);
			return clamp(score, 0, 250);
		};

		auto EnforceRoomSilhouette = [&](GrowthProfile& profile, const ProcGenCell& seed, int silhouetteClass, bool extreme) -> void
		{
			(void)seed;
			if (silhouetteClass == 0)
			{
				profile.bias = 0;
				profile.shapeHint = 0;
				profile.maxW = std::min(maxRoomSize, profile.maxW + (extreme ? 3 : 2));
				profile.maxH = std::max(2, profile.maxH - (extreme ? 2 : 1));
				profile.maxArea = std::max(4, std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxArea * (extreme ? 86 : 78) / 100)));
				profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 85 / 100)));
				profile.growthBursts = std::max(1, profile.growthBursts - 1);
			}
			else if (silhouetteClass == 1)
			{
				profile.bias = 1;
				profile.shapeHint = 1;
				profile.maxH = std::min(maxRoomSize, profile.maxH + (extreme ? 3 : 2));
				profile.maxW = std::max(2, profile.maxW - (extreme ? 2 : 1));
				profile.maxArea = std::max(4, std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxArea * (extreme ? 86 : 78) / 100)));
				profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 85 / 100)));
				profile.growthBursts = std::max(1, profile.growthBursts - 1);
			}
			else
			{
				profile.bias = 2;
				profile.shapeHint = 2;
				profile.maxW = std::max(2, std::min(profile.maxW, std::max(2, mapRoomQuotient / 2 + (extreme ? 1 : 0))));
				profile.maxH = std::max(2, std::min(profile.maxH, std::max(2, mapRoomQuotient / 2 + (extreme ? 1 : 0))));
				profile.maxArea = std::max(4, std::min(profile.maxW * profile.maxH, std::max(3, profile.maxArea * (extreme ? 66 : 72) / 100)));
				profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * (extreme ? 62 : 68) / 100)));
				profile.growthBursts = std::max(1, profile.growthBursts - 1);
			}
		};

		const int contrastLargeTarget = std::max(1, std::min(3, (int)roomSeedIds.Size() / 5 + (roomRouteContrast == 2 ? 1 : 0)));
		const int contrastSmallTarget = std::max(2, std::min(4, std::max(1, (int)roomSeedIds.Size() / 4 - 1)));
				for (int missing = contrastLargeTarget; missing > 0; missing--)
				{
					int bestRid = -1;
					int bestScore = -1;
					for (int k = 0; k < (int)roomSeedIds.Size(); k++)
					{
						const int rid = roomSeedIds[k];
						const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
						if (roomSeed == nullptr) continue;
						if (growthProfiles[rid].sizeClass == 2) continue;
						const int score = ScoreRoomForLandmarkContrast(*roomSeed);
						if (score > bestScore)
						{
							bestScore = score;
							bestRid = rid;
						}
			}
			if (bestRid < 0) break;
			const ProcGenCell* roomSeed = FindRoomSeed(Rooms[bestRid]);
			if (roomSeed != nullptr)
			{
				EnforceContrastRoomSize(growthProfiles[bestRid], *roomSeed, 2, true);
					}
				}

				auto EstimateRoomScale = [&](int rid) -> int
				{
					if (growthProfiles[rid].maxArea > 0) return growthProfiles[rid].maxArea;
					return std::max(4, growthProfiles[rid].maxW * growthProfiles[rid].maxH);
				};

				const int guaranteedLandmarks = 1 + (roomRouteContrast == 2 ? 1 : 0);
				int assignedLarge = 0;
				for (int i = 0; i < guaranteedLandmarks && roomSeedIds.Size() > 0; i++)
				{
					int bestRid = -1;
					int bestScore = -1;
					int bestPenalty = 0;
					for (int k = 0; k < (int)roomSeedIds.Size(); k++)
					{
						const int rid = roomSeedIds[k];
						const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
						if (roomSeed == nullptr) continue;
						if (growthProfiles[rid].sizeClass == 2) continue;
						int penalty = (roomSeed->onMainPath ? -2 : 0) +
							(roomSeed->hasPlayerStart || roomSeed->hasExit || roomSeed->hasBoss || roomSeed->hasKey ? -1 : 0);
						const int score = EstimateRoomScale(rid) - (penalty * 20);
						if (bestRid < 0 || score > bestScore || (score == bestScore && penalty < bestPenalty))
						{
							bestScore = score;
							bestPenalty = penalty;
							bestRid = rid;
						}
					}
					if (bestRid < 0) break;
					const ProcGenCell* roomSeed = FindRoomSeed(Rooms[bestRid]);
					if (roomSeed != nullptr)
					{
						EnforceContrastRoomSize(growthProfiles[bestRid], *roomSeed, 2, true);
						growthProfiles[bestRid].sizeClass = 2;
						assignedLarge++;
					}
				}

				const int guaranteedPockets = std::max(2, std::min(4, std::max(1, (int)roomSeedIds.Size() / 5)));
				int assignedSmall = 0;
				TArray<int> silhouetteLocked;
				silhouetteLocked.Resize(Rooms.Size());
				for (unsigned int i = 0; i < silhouetteLocked.Size(); i++)
					silhouetteLocked[i] = 0;
				for (int i = 0; i < guaranteedPockets && roomSeedIds.Size() > 0; i++)
				{
					int bestRid = -1;
					int bestScore = -1;
					for (int k = 0; k < (int)roomSeedIds.Size(); k++)
					{
						const int rid = roomSeedIds[k];
						const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
						if (roomSeed == nullptr) continue;
						if (rid >= 0 && rid < (int)silhouetteLocked.Size() && silhouetteLocked[rid]) continue;
						if (growthProfiles[rid].sizeClass != 1) continue;
						if (roomSeed->onMainPath) continue;
						int score = EstimateRoomScale(rid);
						const int routePenalty = (roomSeed->hasPlayerStart || roomSeed->hasExit || roomSeed->hasBoss || roomSeed->hasKey ? 30 : 0) +
							(roomSeed->branchDepth >= 2 ? 8 : 0) +
							(roomSeed->connectionCount <= 1 ? 8 : 0);
						score -= routePenalty;
						if (bestRid < 0 || score < bestScore)
						{
							bestScore = score;
							bestRid = rid;
						}
					}
					if (bestRid < 0) break;
					const ProcGenCell* roomSeed = FindRoomSeed(Rooms[bestRid]);
					if (roomSeed != nullptr)
					{
						EnforceContrastRoomSize(growthProfiles[bestRid], *roomSeed, 0, true);
						growthProfiles[bestRid].sizeClass = 0;
						if (bestRid >= 0 && bestRid < (int)silhouetteLocked.Size())
							silhouetteLocked[bestRid] = 1;
						assignedSmall++;
					}
				}

				const int targetWideSilhouette = std::max(1, std::min(4, (int)roomSeedIds.Size() / 5 + (roomShapeMode == 2 ? 1 : 0)));
				const int targetTallSilhouette = std::max(1, std::min(4, (int)roomSeedIds.Size() / 5 + (roomShapeMode == 2 ? 1 : 0)));
		const int targetCompactSilhouette = std::max(1, std::min(4, std::max(1, (int)roomSeedIds.Size() / 4 - 1)));
				int placedWide = 0;
				int placedTall = 0;
				int placedCompact = 0;

				for (int i = 0; i < targetWideSilhouette && roomSeedIds.Size() > 0; i++)
				{
					int bestRid = -1;
					int bestScore = -1;
					for (int k = 0; k < (int)roomSeedIds.Size(); k++)
					{
						const int rid = roomSeedIds[k];
						const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
						if (roomSeed == nullptr) continue;
						const bool avoidFeature = roomSeed->isHub || roomSeed->isArena || roomSeed->isLocked || roomSeed->hasPlayerStart;
						if (rid >= 0 && rid < (int)silhouetteLocked.Size() && silhouetteLocked[rid]) continue;
						if (avoidFeature && roomSeedIds.Size() > 8) continue;
						const int score = ScoreRoomForWideSilhouette(*roomSeed);
						if (score > bestScore)
						{
							bestScore = score;
							bestRid = rid;
						}
					}
					if (bestRid < 0) break;
					const ProcGenCell* roomSeed = FindRoomSeed(Rooms[bestRid]);
					if (roomSeed != nullptr)
					{
						EnforceRoomSilhouette(growthProfiles[bestRid], *roomSeed, 0, placedWide < 1);
						growthProfiles[bestRid].bias = 0;
						if (bestRid >= 0 && bestRid < (int)silhouetteLocked.Size())
							silhouetteLocked[bestRid] = 1;
						placedWide++;
					}
				}

				for (int i = 0; i < targetTallSilhouette && roomSeedIds.Size() > 0; i++)
				{
					int bestRid = -1;
					int bestScore = -1;
					for (int k = 0; k < (int)roomSeedIds.Size(); k++)
					{
						const int rid = roomSeedIds[k];
						const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
						if (roomSeed == nullptr) continue;
						if (roomSeed->onMainPath && roomSeed->branchDepth == 0 && roomSeed->pathRank <= 1) continue;
						if (rid >= 0 && rid < (int)silhouetteLocked.Size() && silhouetteLocked[rid]) continue;
						const int score = ScoreRoomForTallSilhouette(*roomSeed);
						if (score > bestScore)
						{
							bestScore = score;
							bestRid = rid;
						}
					}
					if (bestRid < 0) break;
					const ProcGenCell* roomSeed = FindRoomSeed(Rooms[bestRid]);
					if (roomSeed != nullptr)
					{
						EnforceRoomSilhouette(growthProfiles[bestRid], *roomSeed, 1, placedTall < 1);
						growthProfiles[bestRid].bias = 1;
						if (bestRid >= 0 && bestRid < (int)silhouetteLocked.Size())
							silhouetteLocked[bestRid] = 1;
						placedTall++;
					}
				}

				for (int i = 0; i < targetCompactSilhouette && roomSeedIds.Size() > 0; i++)
				{
					int bestRid = -1;
					int bestScore = -1;
					for (int k = 0; k < (int)roomSeedIds.Size(); k++)
					{
						const int rid = roomSeedIds[k];
						const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
						if (roomSeed == nullptr) continue;
						if (growthProfiles[rid].sizeClass == 2) continue;
						if (rid >= 0 && rid < (int)silhouetteLocked.Size() && silhouetteLocked[rid]) continue;
						const int score = ScoreRoomForCompactSilhouette(*roomSeed);
						if (score > bestScore)
						{
							bestScore = score;
							bestRid = rid;
						}
					}
					if (bestRid < 0) break;
					const ProcGenCell* roomSeed = FindRoomSeed(Rooms[bestRid]);
					if (roomSeed != nullptr)
					{
						EnforceRoomSilhouette(growthProfiles[bestRid], *roomSeed, 2, placedCompact < 1);
						growthProfiles[bestRid].bias = 2;
						if (bestRid >= 0 && bestRid < (int)silhouetteLocked.Size())
							silhouetteLocked[bestRid] = 1;
						placedCompact++;
					}
				}

				for (int missing = contrastSmallTarget; missing > 0; missing--)
				{
					int bestRid = -1;
					int bestScore = -1;
					for (int k = 0; k < (int)roomSeedIds.Size(); k++)
			{
				const int rid = roomSeedIds[k];
				const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
				if (roomSeed == nullptr) continue;
				if (growthProfiles[rid].sizeClass != 1) continue;
				if (roomSeed->onMainPath) continue;
				const int score = ScoreRoomForPocketContrast(*roomSeed);
				if (score > bestScore)
				{
					bestScore = score;
					bestRid = rid;
				}
			}
			if (bestRid < 0) break;
			const ProcGenCell* roomSeed = FindRoomSeed(Rooms[bestRid]);
			if (roomSeed != nullptr)
			{
				EnforceContrastRoomSize(growthProfiles[bestRid], *roomSeed, 0, true);
					}
				}

		// Final global variance pass: guarantee explicit extremes so maps always include
		// both tiny side pockets and oversized anchors rather than repeating the same scale.
		auto EstimateProfileArea = [&](int rid) -> int
		{
			if (growthProfiles[rid].maxArea > 0)
				return growthProfiles[rid].maxArea;
			return std::max(4, growthProfiles[rid].maxW * growthProfiles[rid].maxH);
		};

		TArray<int> varianceLocked;
		varianceLocked.Resize(Rooms.Size());
		for (int i = 0; i < (int)varianceLocked.Size(); i++)
			varianceLocked[i] = 0;

		auto PickByAreaExtremity = [&](bool wantSmall, bool allowSpecial) -> int
		{
			int bestRid = -1;
			int bestAreaScore = wantSmall ? 0x7fffffff : -1;
			for (int idx = 0; idx < (int)roomSeedIds.Size(); idx++)
			{
				const int rid = roomSeedIds[idx];
				if (rid < 0 || rid >= (int)Rooms.Size()) continue;
				if (rid < (int)varianceLocked.Size() && varianceLocked[rid]) continue;
				const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
				if (roomSeed == nullptr) continue;
				if (!allowSpecial && IsFeatureSeed(*roomSeed)) continue;
				if (!allowSpecial && roomSeed->isHub) continue;
				if (!allowSpecial && roomSeed->isArena) continue;
				const int areaScore = EstimateProfileArea(rid);
				if (wantSmall)
				{
					if (areaScore < bestAreaScore)
					{
						bestAreaScore = areaScore;
						bestRid = rid;
					}
				}
				else
				{
					const int score = roomSeed->onMainPath ? areaScore + 3 : areaScore;
					if (score > bestAreaScore)
					{
						bestAreaScore = score;
						bestRid = rid;
					}
				}
			}
			return bestRid;
		};

		const int extremeTinyRooms = std::max(1, std::min(3, (int)roomSeedIds.Size() / 6 + 1));
		for (int i = 0; i < extremeTinyRooms; i++)
		{
			const int tinyRid = PickByAreaExtremity(true, false);
			if (tinyRid < 0) break;
			const ProcGenCell* tinySeed = FindRoomSeed(Rooms[tinyRid]);
			if (tinySeed != nullptr)
			{
				EnforceContrastRoomSize(growthProfiles[tinyRid], *tinySeed, 0, false);
				growthProfiles[tinyRid].sizeClass = 0;
				varianceLocked[tinyRid] = 1;
			}
		}

		const int extremeLargeRooms = std::max(1, std::min(3, (int)roomSeedIds.Size() / 6 + 1));
		for (int i = 0; i < extremeLargeRooms; i++)
		{
			const int largeRid = PickByAreaExtremity(false, true);
			if (largeRid < 0) break;
			const ProcGenCell* largeSeed = FindRoomSeed(Rooms[largeRid]);
			if (largeSeed != nullptr)
			{
				EnforceContrastRoomSize(growthProfiles[largeRid], *largeSeed, 2, true);
				growthProfiles[largeRid].sizeClass = 2;
				varianceLocked[largeRid] = 1;
			}
		}

		// Final silhouette diversity guardrail so we don't lose all directionality after earlier passes.
		int explicitWide = 0;
		int explicitTall = 0;
		int explicitCompact = 0;
		for (int k = 0; k < (int)roomSeedIds.Size(); k++)
		{
			const int rid = roomSeedIds[k];
			if (rid < 0 || rid >= (int)Rooms.Size()) continue;
			switch (growthProfiles[rid].shapeHint)
			{
			case 0: explicitWide++; break;
			case 1: explicitTall++; break;
			case 2: explicitCompact++; break;
			default: break;
			}
		}

		auto PickRoomForSilhouette = [&](int silhouetteClass) -> int
		{
			int bestRid = -1;
			int bestScore = -1;
			for (int k = 0; k < (int)roomSeedIds.Size(); k++)
			{
				const int rid = roomSeedIds[k];
				if (rid < 0 || rid >= (int)Rooms.Size()) continue;
				if (rid < (int)varianceLocked.Size() && varianceLocked[rid]) continue;
				const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
				if (roomSeed == nullptr) continue;
				if (roomSeed->hasPlayerStart || roomSeed->hasBoss || roomSeed->hasExit || roomSeed->hasKey || roomSeed->isHub || roomSeed->isArena) continue;

				int score = 0;
				if (silhouetteClass == 0)
					score = ScoreRoomForWideSilhouette(*roomSeed);
				else if (silhouetteClass == 1)
					score = ScoreRoomForTallSilhouette(*roomSeed);
				else
					score = ScoreRoomForCompactSilhouette(*roomSeed);

				if (score > bestScore)
				{
					bestScore = score;
					bestRid = rid;
				}
			}
			return bestRid;
		};

		if (roomSeedIds.Size() >= 5 && (explicitWide == 0 || explicitTall == 0 || explicitCompact == 0))
		{
			auto ApplySilhouetteFallback = [&](int missingClass) -> void
			{
				const int bestRid = PickRoomForSilhouette(missingClass);
				if (bestRid < 0) return;
				const ProcGenCell* bestSeed = FindRoomSeed(Rooms[bestRid]);
				if (bestSeed == nullptr) return;
				EnforceRoomSilhouette(growthProfiles[bestRid], *bestSeed, missingClass, false);
				varianceLocked[bestRid] = 1;
			};

			if (explicitWide == 0) ApplySilhouetteFallback(0);
			if (explicitTall == 0) ApplySilhouetteFallback(1);
			if (explicitCompact == 0) ApplySilhouetteFallback(2);
		}

		auto EffectiveShapeClass = [&](int rid) -> int
		{
			const int hint = growthProfiles[rid].shapeHint;
			if (hint >= 0 && hint <= 2) return hint;
			return clamp(growthProfiles[rid].bias, 0, 2);
		};

		int visibleWide = 0, visibleTall = 0, visibleCompact = 0;
		for (int k = 0; k < (int)roomSeedIds.Size(); k++)
		{
			const int rid = roomSeedIds[k];
			switch (EffectiveShapeClass(rid))
			{
			case 0: visibleWide++; break;
			case 1: visibleTall++; break;
			default: visibleCompact++; break;
			}
		}

		// Ensure the final room mix does not collapse back into two shapes or one dominant trend.
		if (roomSeedIds.Size() >= 8)
		{
			const int targetMinPresence = (int)roomSeedIds.Size() >= 12 ? 2 : 1;
			auto PickRebalanceRoom = [&](int fromClass, int toClass) -> int
			{
				int bestRid = -1;
				int bestScore = -1;
				for (int k = 0; k < (int)roomSeedIds.Size(); k++)
				{
					const int rid = roomSeedIds[k];
					const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
					if (roomSeed == nullptr) continue;
					if (roomSeed->hasPlayerStart || roomSeed->hasBoss || roomSeed->hasExit || roomSeed->hasKey || roomSeed->isHub || roomSeed->isArena) continue;
					if (rid < (int)varianceLocked.Size() && varianceLocked[rid]) continue;
					if (EffectiveShapeClass(rid) != fromClass) continue;

					int score = 0;
					if (toClass == 0) score = ScoreRoomForWideSilhouette(*roomSeed);
					else if (toClass == 1) score = ScoreRoomForTallSilhouette(*roomSeed);
					else score = ScoreRoomForCompactSilhouette(*roomSeed);
					if (roomSeed->onMainPath) score -= 2;

					if (score > bestScore)
					{
						bestScore = score;
						bestRid = rid;
					}
				}
				return bestRid;
			};

			for (int targetClass = 0; targetClass < 3; targetClass++)
			{
				int targetCount = (targetClass == 0) ? visibleWide : (targetClass == 1) ? visibleTall : visibleCompact;
				if (targetCount >= targetMinPresence) continue;
				const int missing = targetMinPresence - targetCount;
				for (int i = 0; i < missing; i++)
				{
					int fromClass = 0;
					int availableDominant = visibleWide;
					if (visibleTall > availableDominant) { availableDominant = visibleTall; fromClass = 1; }
					if (visibleCompact > availableDominant) { availableDominant = visibleCompact; fromClass = 2; }
					if (availableDominant <= targetMinPresence) break;
					const int donorRid = PickRebalanceRoom(fromClass, targetClass);
					if (donorRid < 0) break;
					const ProcGenCell* donorSeed = FindRoomSeed(Rooms[donorRid]);
					if (donorSeed == nullptr) break;
					EnforceRoomSilhouette(growthProfiles[donorRid], *donorSeed, targetClass, false);
					varianceLocked[donorRid] = 1;
					if (fromClass == 0) visibleWide--;
					else if (fromClass == 1) visibleTall--;
					else visibleCompact--;
					if (targetClass == 0) visibleWide++;
					else if (targetClass == 1) visibleTall++;
					else visibleCompact++;
				}
			}
		}

		// Explicit size-band pass to force obvious scale contrast even when prior passes collapse the room mix.
		TArray<int> areaOrder;
		areaOrder.Resize(roomSeedIds.Size());
		for (int i = 0; i < (int)areaOrder.Size(); i++)
			areaOrder[i] = roomSeedIds[i];
		for (int i = 0; i < (int)areaOrder.Size(); i++)
		{
			int bestIdx = i;
			int bestArea = EstimateProfileArea(areaOrder[i]);
			for (int j = i + 1; j < (int)areaOrder.Size(); j++)
			{
				const int area = EstimateProfileArea(areaOrder[j]);
				if (area < bestArea)
				{
					bestArea = area;
					bestIdx = j;
				}
			}
			if (bestIdx != i)
			{
				const int tmp = areaOrder[i];
				areaOrder[i] = areaOrder[bestIdx];
				areaOrder[bestIdx] = tmp;
			}
		}

		auto ApplySizeBand = [&](GrowthProfile& profile, const ProcGenCell& seed, int band) -> void
		{
			ForceRoomSizeClass(profile, seed, band);
			profile.sizeBandLock = band;
			if (band == 2) // macro room band
			{
				const int macroScaleBonus = roomScaleProfile >= 3 ? 2 : (roomScaleProfile >= 1 ? 1 : 0);
				const int macroMinW = std::max(4, std::min(maxRoomSize, mapRoomQuotient + 1 + (seed.onMainPath ? 2 : 0) + macroScaleBonus));
				const int macroMinH = std::max(4, std::min(maxRoomSize, std::max(4, (mapRoomQuotient * 4) / 5 + (seed.onMainPath ? 1 : 0) + (macroScaleBonus / 2))));
				profile.maxW = std::max(profile.maxW, macroMinW);
				profile.maxH = std::max(profile.maxH, macroMinH);
				profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxArea * 205 / 100));
				profile.targetArea = std::min(profile.maxArea, std::max(profile.targetArea, profile.maxArea * 98 / 100));
				profile.growthBursts = std::min(4, profile.growthBursts + 2);
				profile.bias = (profile.bias == 2) ? ((seed.onMainPath ? 0 : 1)) : profile.bias;
			}
			else if (band == 0) // micro room band
			{
				const int microCap = std::max(2, std::min(3, std::max(2, mapRoomQuotient / 3)));
				profile.maxW = std::max(2, std::min(profile.maxW, microCap));
				profile.maxH = std::max(2, std::min(profile.maxH, microCap));
				profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(4, profile.maxArea * 45 / 100));
				profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 50 / 100)));
				profile.growthBursts = std::max(1, profile.growthBursts - 1);
				profile.bias = 2;
			}
			else
			{
				profile.maxArea = std::min(profile.maxW * profile.maxH, std::max(profile.maxArea, profile.maxArea * 92 / 100));
				profile.targetArea = std::max(3, std::min(profile.targetArea, std::max(4, profile.maxArea * 80 / 100)));
			}
			profile.maxArea = std::max(4, std::min(profile.maxArea, profile.maxW * profile.maxH));
			profile.targetArea = clamp(profile.targetArea, 3, profile.maxArea);
		};

		TArray<int> scaleLocked;
		scaleLocked.Resize(Rooms.Size());
		for (int i = 0; i < (int)scaleLocked.Size(); i++)
			scaleLocked[i] = 0;

		const int microNeed = clamp(std::max(1, (int)roomSeedIds.Size() / 4 + 1 + (roomDensityProfile == 0 ? 1 : 0) - (roomScaleProfile >= 2 ? 1 : 0) + (roomSizeVariance >= 3 ? 1 : 0)), 1, 5);
		const int macroNeed = clamp(std::max(1, (int)roomSeedIds.Size() / 5 + 1 + (roomScaleProfile >= 2 ? 1 : 0) + (roomRouteContrast == 2 ? 1 : 0) + (roomSizeVariance >= 4 ? 1 : 0) - (roomDensityProfile == 0 ? 1 : 0)), 1, 5);

		for (int i = 0; i < microNeed; i++)
		{
			int bestRid = -1;
			int bestScore = -1;
			for (int idx = 0; idx < (int)areaOrder.Size(); idx++)
			{
				const int rid = areaOrder[idx];
				if (rid < 0 || rid >= (int)scaleLocked.Size() || scaleLocked[rid]) continue;
				const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
				if (roomSeed == nullptr) continue;
				if (roomSeed->onMainPath && roomSeed->pathRank < 2 && roomSeedIds.Size() > 4) continue;
				if (roomSeed->hasPlayerStart || roomSeed->hasBoss || roomSeed->hasExit || roomSeed->hasKey || roomSeed->isArena || roomSeed->isHub)
					continue;

				const int score = ScoreRoomForPocketContrast(*roomSeed);
				if (bestRid < 0 || score > bestScore)
				{
					bestRid = rid;
					bestScore = score;
				}
			}
			if (bestRid < 0)
			{
				for (int idx = 0; idx < (int)areaOrder.Size(); idx++)
				{
					const int rid = areaOrder[idx];
					if (rid < 0 || rid >= (int)scaleLocked.Size() || scaleLocked[rid]) continue;
					const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
					if (roomSeed == nullptr) continue;
					const int score = ScoreRoomForPocketContrast(*roomSeed);
					if (bestRid < 0 || score > bestScore)
					{
						bestRid = rid;
						bestScore = score;
					}
				}
			}
			if (bestRid < 0) break;
			const ProcGenCell* bestSeed = FindRoomSeed(Rooms[bestRid]);
			if (bestSeed != nullptr)
			{
				ApplySizeBand(growthProfiles[bestRid], *bestSeed, 0);
				scaleLocked[bestRid] = 1;
			}
		}

		for (int i = 0; i < macroNeed; i++)
		{
			int bestRid = -1;
			int bestScore = -1;
			for (int idx = (int)areaOrder.Size() - 1; idx >= 0; idx--)
			{
				const int rid = areaOrder[idx];
				if (rid < 0 || rid >= (int)scaleLocked.Size() || scaleLocked[rid]) continue;
				const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
				if (roomSeed == nullptr) continue;
				if (roomSeed->hasPlayerStart && roomSeedIds.Size() > 3) continue;

				int score = ScoreRoomForLandmarkContrast(*roomSeed);
				if (roomSeed->onMainPath) score += 18;
				if (roomSeed->isHub || roomSeed->isArena) score += 12;
				if (roomSeed->pathRank >= 0 && roomSeed->pathRank < 2) score -= 8;
				if (roomSeed->pathRank >= 6) score += 10;

				if (bestRid < 0 || score > bestScore)
				{
					bestRid = rid;
					bestScore = score;
				}
			}
			if (bestRid < 0)
			{
				for (int idx = (int)areaOrder.Size() - 1; idx >= 0; idx--)
				{
					const int rid = areaOrder[idx];
					if (rid < 0 || rid >= (int)scaleLocked.Size() || scaleLocked[rid]) continue;
					const ProcGenCell* roomSeed = FindRoomSeed(Rooms[rid]);
					if (roomSeed == nullptr) continue;
					const int score = ScoreRoomForLandmarkContrast(*roomSeed);
					if (bestRid < 0 || score > bestScore)
					{
						bestRid = rid;
						bestScore = score;
					}
				}
			}
			if (bestRid < 0) break;
			const ProcGenCell* bestSeed = FindRoomSeed(Rooms[bestRid]);
			if (bestSeed != nullptr)
			{
				ApplySizeBand(growthProfiles[bestRid], *bestSeed, 2);
				scaleLocked[bestRid] = 1;
			}
		}

		auto CellsCompatible = [&](const ProcGenCell& a, const ProcGenCell& b) -> bool
		{
		// Never merge special-content cells with anything else.
		if (a.hasPlayerStart || b.hasPlayerStart) return false;
		if (a.hasExit || b.hasExit) return false;
		if (a.hasBoss || b.hasBoss) return false;
		if (a.hasKey || b.hasKey)
		{
			// Key cells can only merge with other key cells of the same type.
			return a.hasKey && b.hasKey && a.keyType == b.keyType;
		}
		// Never merge locked with unlocked, or different lock types.
		if (a.isLocked != b.isLocked) return false;
		if (a.isLocked && b.isLocked && a.lockType != b.lockType) return false;
		// Hub/arena status must match if either cell has it.
		if (a.isHub != b.isHub && (a.isHub || b.isHub)) return false;
		if (a.isArena != b.isArena && (a.isArena || b.isArena)) return false;
		// Main-path and off-path cells should not mix.
		if (a.onMainPath != b.onMainPath) return false;
		return true;
	};

	auto ApplyMapRoomMode = [&](GrowthProfile& p, bool isTerminal, const ProcGenCell& seed) -> void
	{
		const int routeBand = (seed.pathRank >= 0) ? clamp(seed.pathRank, 0, 9) : 0;
		const int archetypeRoll = (roomArchetype + routeBand + (seed.onMainPath ? 1 : 0) + seed.branchDepth * 2) % 6;
		const bool isProminent = seed.onMainPath || seed.isHub || seed.isArena || seed.isLocked ||
			seed.hasKey || seed.hasExit || seed.hasBoss;
		const int routeRoleClass = clamp((seed.onMainPath ? 5 : 2) + (seed.isHub || seed.isArena ? 2 : 0) +
		                         (seed.isLocked ? 1 : 0) + seed.branchDepth - (isTerminal ? 1 : 0), 0, 9);
		const bool mapDensitySprawl = (roomDensityProfile == 4);
		const bool mapDensityTight = (roomDensityProfile == 0);
		const int openH = (seed.conn[DIR_W] ? 1 : 0) + (seed.conn[DIR_E] ? 1 : 0);
		const int openV = (seed.conn[DIR_N] ? 1 : 0) + (seed.conn[DIR_S] ? 1 : 0);
		const int openTotal = openH + openV;
		const bool isDeepBranch = !seed.onMainPath && seed.branchDepth >= 2;
		const bool isMainlinePulse = seed.onMainPath && (seed.pathRank >= 0 && seed.pathRank <= 2);
		const bool isLateMainline = seed.onMainPath && seed.pathRank >= 4;

		if (p.sizeClass == 2)
		{
			p.maxW = std::min(maxRoomSize, p.maxW + 2);
			p.maxH = std::min(maxRoomSize, p.maxH + 1);
			p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 155 / 100));
			p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 90 / 100));
			p.growthBursts = std::min(4, p.growthBursts + 1);
		}
		else if (p.sizeClass == 0)
		{
			p.maxW = std::max(2, p.maxW - 1);
			p.maxH = std::max(2, p.maxH - 1);
			p.maxArea = std::max(4, p.maxArea * 74 / 100);
			p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 66 / 100)));
			p.growthBursts = std::max(1, p.growthBursts - 1);
			p.bias = 2;
		}

		if (roomShapeMode == 0)
		{
			p.maxW = std::min(maxRoomSize, p.maxW + 1);
			p.maxH = std::min(maxRoomSize, p.maxH + 1);
			p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 130 / 100));
			p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 85 / 100));
			p.growthBursts = std::min(4, p.growthBursts + 1);
		}
		else if (roomShapeMode == 1)
		{
			p.maxArea = std::max(4, p.maxArea * 75 / 100);
			p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 78 / 100)));
			p.maxW = std::max(2, p.maxW - (RNG() % 2));
			p.maxH = std::max(2, p.maxH - (RNG() % 2));
			p.growthBursts = std::max(1, p.growthBursts - 1);
			if (p.bias == 0 || isTerminal)
				p.bias = 2;
		}
		else if (roomShapeMode == 2)
		{
			if (RNG() % 2 == 0)
			{
				p.maxW = std::min(maxRoomSize, p.maxW + 2);
				p.maxH = std::max(2, p.maxH - 1);
				p.bias = 0;
			}
			else
			{
				p.maxH = std::min(maxRoomSize, p.maxH + 2);
				p.maxW = std::max(2, p.maxW - 1);
				p.bias = 1;
			}
			p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 120 / 100));
			p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 82 / 100));
			p.growthBursts = std::min(4, p.growthBursts + 1);
		}
		else
		{
			if ((RNG() % 2) == 0)
				p.maxArea = std::max(4, p.maxArea * 88 / 100);
			else
				p.maxArea = std::min(p.maxW * p.maxH, p.maxArea + (RNG() % 5));

			if ((RNG() % 2) == 0)
				p.maxW = std::max(2, p.maxW - 1);
			else if (p.maxW < maxRoomSize)
				p.maxW = std::min(maxRoomSize, p.maxW + 1);
			if ((RNG() % 2) == 0)
				p.maxH = std::max(2, p.maxH - 1);
			else if (p.maxH < maxRoomSize)
				p.maxH = std::min(maxRoomSize, p.maxH + 1);

			p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 75 / 100)));
		}

		// Apply route-aware archetypes for stronger room variety:
		// 0=landmark-heavy cathedral, 1=tiny maze pockets, 2=long ribbon rooms,
		// 3=balanced contrast, 4=parity extremes, 5=wild landmark pockets.
		if (archetypeRoll == 0)
		{
			if (isProminent)
			{
				p.maxW = std::min(maxRoomSize, p.maxW + 2);
				p.maxH = std::min(maxRoomSize, p.maxH + 2);
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 180 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 100 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 2);
				p.bias = 2;
			}
			else
			{
				p.maxArea = std::max(4, p.maxArea * 75 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 78 / 100)));
				p.maxW = std::max(2, p.maxW - 1);
				p.maxH = std::max(2, p.maxH - 1);
				p.growthBursts = std::max(1, p.growthBursts - 1);
			}
		}
			else if (archetypeRoll == 1) // Dense micro pockets.
			{
				p.maxW = std::max(2, p.maxW - 2);
				p.maxH = std::max(2, p.maxH - 2);
				p.maxArea = std::max(3, p.maxArea * 52 / 100);
			p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 70 / 100)));
			p.growthBursts = std::max(1, p.growthBursts - 1);
			p.bias = 2;
		}
			else if (archetypeRoll == 2) // Ribbon-dominant pockets.
			{
				if (openTotal <= 1 || (openH > 0 && openV == 0))
				{
					p.maxW = std::min(maxRoomSize, p.maxW + 1);
				p.maxH = std::max(2, p.maxH - 1);
				p.bias = 0;
			}
			else
			{
				p.maxH = std::min(maxRoomSize, p.maxH + 1);
				p.maxW = std::max(2, p.maxW - 1);
				p.bias = 1;
			}
			p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 130 / 100));
			p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 82 / 100));
			p.growthBursts = std::min(4, p.growthBursts + 1);
		}
		else if (archetypeRoll == 3) // Contrast-by-route.
		{
			if (routeBand % 2 == 0)
			{
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 150 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 92 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 1);
			}
			else
			{
				p.maxArea = std::max(4, p.maxArea * 65 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 75 / 100)));
				p.maxW = std::max(2, p.maxW - 1);
				p.maxH = std::max(2, p.maxH - 1);
				p.growthBursts = std::max(1, p.growthBursts - 1);
			}
		}
		else if (archetypeRoll == 4) // Wild parity extremes tied to route progression.
		{
			if (seed.onMainPath && (routeBand >= 4 || isProminent))
			{
				p.maxW = std::min(maxRoomSize, p.maxW + 3);
				p.maxH = std::min(maxRoomSize, p.maxH + 2);
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 185 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 100 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 2);
			}
			else
			{
				p.maxArea = std::max(4, p.maxArea * 68 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 76 / 100)));
				p.maxW = std::max(2, p.maxW - 1);
				p.maxH = std::max(2, p.maxH - 1);
				p.growthBursts = std::max(1, p.growthBursts - 1);
			}
			}
			else // archetypeRoll == 5
			{
				if (isProminent && (seed.branchDepth == 0 || seed.branchDepth >= 2))
				{
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 165 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 96 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 2);
				p.maxW = std::min(maxRoomSize, p.maxW + 2);
				p.maxH = std::min(maxRoomSize, p.maxH + 1);
			}
				else
				{
					p.maxArea = std::max(4, p.maxArea * 74 / 100);
					p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 80 / 100)));
					p.maxW = std::max(2, p.maxW - 1);
					p.maxH = std::max(2, p.maxH - 1);
				}
			}

			// Route-biome shaping: early tight hubs, late compression/expansion swings, and clear branch signature.
			if (isMainlinePulse && !seed.isArena && !seed.isHub)
			{
				p.maxW = std::max(2, p.maxW - 1);
				p.maxH = std::max(2, p.maxH - 1);
				p.maxArea = std::max(4, p.maxArea * 90 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 75 / 100)));
				p.growthBursts = std::max(1, p.growthBursts - 1);
			}
			else if (isLateMainline)
			{
				p.maxW = std::min(maxRoomSize, p.maxW + 1);
				p.maxH = std::min(maxRoomSize, p.maxH + 1);
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 130 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 88 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 1);
			}
			if (isDeepBranch && !isProminent)
			{
				p.maxW = std::max(2, p.maxW - 1);
				p.maxH = std::max(2, p.maxH - 1);
				p.maxArea = std::max(4, p.maxArea * 70 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 72 / 100)));
				p.growthBursts = std::max(1, p.growthBursts - 1);
			}

			if (roomSizeFamily == 0) // Cathedral: big landmarks on the route, compact dead ends.
			{
				if (seed.onMainPath && seed.pathRank >= 3)
				{
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 150 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 85 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 1);
			}
			if (!seed.onMainPath && (seed.branchDepth >= 1 || p.maxArea > 12))
			{
				p.maxArea = std::max(4, p.maxArea * 70 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 80 / 100)));
			}
		}
		else if (roomSizeFamily == 1) // Tight maze: lots of smaller varied pockets.
		{
			p.maxArea = std::max(4, p.maxArea * 70 / 100);
			p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 72 / 100)));
			p.maxW = std::max(2, p.maxW - 1);
			p.maxH = std::max(2, p.maxH - 1);
			p.growthBursts = std::max(1, p.growthBursts - 1);
			p.bias = 2;
		}
		else if (roomSizeFamily == 2) // Ribbon: elongated travel with occasional long pockets.
		{
			if (seed.onMainPath && (seed.pathRank & 1))
			{
				p.maxW = std::min(maxRoomSize, p.maxW + 1);
				p.maxH = std::max(2, p.maxH - 1);
				p.bias = 0;
			}
			else if (!seed.onMainPath && seed.branchDepth >= 1)
			{
				p.maxW = std::max(2, p.maxW - 1);
				p.maxH = std::max(2, p.maxH - 1);
				p.maxArea = std::max(4, p.maxArea * 88 / 100);
			}
			else
			{
				p.maxH = std::min(maxRoomSize, p.maxH + 1);
				p.bias = 1;
			}
			p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 80 / 100)));
		}
		else if (roomSizeFamily == 3) // Contrast: one-punch big rooms balanced by tight pockets.
		{
			const bool isProminent = seed.onMainPath || seed.isHub || seed.isArena;
			if (isProminent)
			{
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 135 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 88 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 1);
			}
			else if (seed.pathRank >= 0 && seed.pathRank <= 2)
			{
				p.maxArea = std::max(4, p.maxArea * 75 / 100);
			}
		}
		else if (roomSizeFamily == 4) // Mixed: preserve spread and let map scale drive extremes.
		{
			const bool isProminent = seed.onMainPath || seed.isHub || seed.isArena;
			if (isProminent)
			{
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 135 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 90 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 1);
			}
			else if (!seed.onMainPath && p.maxArea > 8 && !seed.isLocked)
			{
				if ((seed.pathRank & 1) == 0)
				{
					p.maxArea = std::max(4, p.maxArea * 72 / 100);
					p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 70 / 100)));
					p.maxW = std::max(2, p.maxW - 1);
					p.maxH = std::max(2, p.maxH - 1);
					p.growthBursts = std::max(1, p.growthBursts - 1);
					p.bias = 2;
				}
				else
				{
					p.maxW = std::min(maxRoomSize, p.maxW + 1);
					p.maxH = std::min(maxRoomSize, p.maxH + 1);
					p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 140 / 100));
					p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 88 / 100));
				}
			}
		}

		if (roomScaleProfile == 0) // Compact map: mostly tighter rooms.
		{
			p.maxW = std::max(2, p.maxW - 1);
			p.maxH = std::max(2, p.maxH - 1);
			p.maxArea = std::max(4, p.maxArea * 75 / 100);
			p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 78 / 100)));
			p.growthBursts = std::max(1, p.growthBursts - 1);
		}
		else if (roomScaleProfile == 2) // Expansive map: boost mainline and landmark rooms.
		{
			if (seed.onMainPath || seed.isHub || seed.isArena || seed.pathRank <= 2 || p.maxArea > 10)
			{
				p.maxW = std::min(maxRoomSize, p.maxW + 2);
				p.maxH = std::min(maxRoomSize, p.maxH + 2);
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 150 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 95 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 1);
			}
			else
			{
				p.maxArea = std::max(4, p.maxArea * 88 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 82 / 100)));
			}
		}
		else if (roomScaleProfile == 3) // Alternating scale by route position.
		{
			if ((seed.pathRank & 1) != 0)
			{
				p.maxW = std::max(2, p.maxW - 1);
				p.maxH = std::max(2, p.maxH - 1);
				p.maxArea = std::max(4, p.maxArea * 78 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 80 / 100)));
				p.growthBursts = std::max(1, p.growthBursts - 1);
			}
			else
				{
					p.maxW = std::min(maxRoomSize, p.maxW + 2);
					p.maxH = std::min(maxRoomSize, p.maxH + 1);
					p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 140 / 100));
					p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 88 / 100));
					p.growthBursts = std::min(4, p.growthBursts + 1);
				}
			}
			else if (roomScaleProfile == 4) // Dramatic map scale: big anchors with tight pockets.
			{
				if (seed.pathRank <= 1 || seed.isHub || seed.isArena)
				{
					p.maxW = std::min(maxRoomSize, p.maxW + 2);
					p.maxH = std::min(maxRoomSize, p.maxH + 2);
					p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 170 / 100));
					p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 100 / 100));
					p.growthBursts = std::min(4, p.growthBursts + 2);
				}
				else
				{
					p.maxArea = std::max(4, p.maxArea * 70 / 100);
					p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 76 / 100)));
					p.growthBursts = std::max(1, p.growthBursts - 1);
				}
			}

		const int contrastBand = (seed.pathRank < 0) ? 0 : clamp(seed.pathRank / 3, 0, 4);
		if (roomRouteContrast == 1) // Staircase progression: compact start pockets, broad anchors late.
		{
			if (seed.onMainPath)
			{
				if (seed.isHub || seed.isArena || contrastBand >= 2 || seed.pathRank <= 1)
				{
					p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 155 / 100));
					p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 92 / 100));
					p.growthBursts = std::min(4, p.growthBursts + 1);
				}
				else
				{
					p.maxArea = std::max(4, p.maxArea * 90 / 100);
				}
			}
			else if (seed.branchDepth >= 2)
			{
				p.maxArea = std::max(4, p.maxArea * 65 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 72 / 100)));
				p.maxW = std::max(2, p.maxW - 1);
				p.maxH = std::max(2, p.maxH - 1);
			}
		}
		else if (roomRouteContrast == 2) // Landmark emphasis around hubs/arenas, deep pockets shrink.
		{
			if (seed.onMainPath || seed.isHub || seed.isArena || seed.hasKey || seed.hasPlayerStart || seed.hasExit)
			{
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 160 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 95 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 1);
			}
			else
			{
				p.maxArea = std::max(4, p.maxArea * 78 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 75 / 100)));
			}
		}
		else if (roomRouteContrast == 3) // Alternating corridor-pocket contrast by rank parity.
		{
			if (seed.onMainPath && (seed.pathRank & 1) == 0)
			{
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 145 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 90 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 1);
			}
			else if (!seed.onMainPath && (seed.pathRank & 1) == 1)
			{
				p.maxArea = std::max(4, p.maxArea * 75 / 100);
				p.maxW = std::max(2, p.maxW - 1);
				p.maxH = std::max(2, p.maxH - 1);
				p.growthBursts = std::max(1, p.growthBursts - 1);
			}
		}

		// Route-role classes make size bands explicit.
		if (routeRoleClass >= 7 && !isTerminal)
		{
			p.maxW = std::min(maxRoomSize, p.maxW + 2);
			p.maxH = std::min(maxRoomSize, p.maxH + 2);
			p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 150 / 100));
			p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 95 / 100));
			p.growthBursts = std::min(4, p.growthBursts + 1);
		}
		else if (routeRoleClass <= 2 && !seed.onMainPath && seed.branchDepth >= 2)
		{
			p.maxArea = std::max(4, p.maxArea * 62 / 100);
			p.maxW = std::max(2, p.maxW - 1);
			p.maxH = std::max(2, p.maxH - 1);
			p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 68 / 100)));
			p.growthBursts = std::max(1, p.growthBursts - 1);
		}

		// Map-wide density profile: sprawl tends to create contrast between landmarks and tight pockets,
		// while compact mode favors stronger branch compression.
		if (mapDensitySprawl)
		{
			if (seed.onMainPath || seed.isHub || seed.isArena || seed.isLocked || seed.pathRank >= 4)
			{
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 135 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 92 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 1);
			}
			else if (!seed.onMainPath && seed.branchDepth >= 1)
			{
				p.maxArea = std::max(4, p.maxArea * 76 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 75 / 100)));
			}
		}
		else if (mapDensityTight && !seed.onMainPath && !seed.isHub && !seed.isArena)
		{
			p.maxArea = std::max(4, p.maxArea * 72 / 100);
			p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 66 / 100)));
			p.maxW = std::max(2, p.maxW - 1);
			p.maxH = std::max(2, p.maxH - 1);
		}

		if (!seed.onMainPath && seed.branchDepth == 0 && p.maxArea > 8)
		{
			p.targetArea = std::min(p.targetArea, std::max(4, p.maxArea * 85 / 100));
		}

		// Small pockets should occasionally become almost linear transition rooms for route rhythm.
		if (!seed.onMainPath && seed.branchDepth >= 1 && openTotal <= 2 && (RNG() % 4) == 0)
		{
			p.maxW = std::max(2, p.maxW - 1);
			p.maxH = std::max(2, p.maxH - 1);
			p.maxArea = std::max(4, p.maxArea * 82 / 100);
			p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 70 / 100)));
		}

		// Topology-aware silhouette shaping to prevent repetitive room geometry.
		const bool isCorridorNS = seed.conn[DIR_N] && seed.conn[DIR_S] && !seed.conn[DIR_W] && !seed.conn[DIR_E];
		const bool isCorridorEW = seed.conn[DIR_W] && seed.conn[DIR_E] && !seed.conn[DIR_N] && !seed.conn[DIR_S];
		const bool isLJunction = (openTotal == 2) && !(isCorridorNS || isCorridorEW);
		const int silhouetteRoll = (routeBand * 11 + seed.branchDepth * 17 + (seed.onMainPath ? 19 : 0) + (openTotal * 29)) % 100;

		if (isCorridorNS)
		{
			p.bias = 1;
			p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 120 / 100));
			p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 80 / 100));
			if (isDeepBranch && !isProminent)
			{
				p.maxH = std::min(maxRoomSize, p.maxH + 2);
				p.maxW = std::max(2, p.maxW - 2);
				p.maxArea = std::max(4, p.maxArea * 78 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 70 / 100)));
				p.growthBursts = std::max(1, p.growthBursts - 1);
			}
			else if (isLateMainline || seed.onMainPath)
			{
				p.maxH = std::min(maxRoomSize, p.maxH + 1);
				p.maxW = std::max(2, p.maxW - 1);
			}
			else
			{
				p.maxH = std::min(maxRoomSize, p.maxH + 1);
				p.maxW = std::max(2, p.maxW - 1);
			}
		}
		else if (isCorridorEW)
		{
			p.bias = 0;
			p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 120 / 100));
			p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 80 / 100));
			if (isDeepBranch && !isProminent)
			{
				p.maxW = std::min(maxRoomSize, p.maxW + 2);
				p.maxH = std::max(2, p.maxH - 2);
				p.maxArea = std::max(4, p.maxArea * 78 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 70 / 100)));
				p.growthBursts = std::max(1, p.growthBursts - 1);
			}
			else if (isLateMainline || seed.onMainPath)
			{
				p.maxW = std::min(maxRoomSize, p.maxW + 1);
				p.maxH = std::max(2, p.maxH - 1);
			}
			else
			{
				p.maxW = std::min(maxRoomSize, p.maxW + 1);
				p.maxH = std::max(2, p.maxH - 1);
			}
		}
		else if (isLJunction)
		{
			p.bias = 2;
			if (seed.isHub || seed.isArena || seed.onMainPath)
			{
				p.maxW = std::min(maxRoomSize, p.maxW + 1);
				p.maxH = std::min(maxRoomSize, p.maxH + 1);
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 140 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 90 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 1);
			}
			else
			{
				if (silhouetteRoll < 35)
				{
					p.maxArea = std::max(4, p.maxArea * 82 / 100);
					p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 68 / 100)));
					p.maxW = std::max(2, p.maxW - 1);
					p.maxH = std::max(2, p.maxH - 1);
				}
				else
				{
					p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 112 / 100));
					p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 84 / 100));
				}
			}
		}
		else if (silhouetteRoll >= 86)
		{
			// Rare outlier room shape to create dramatic scale contrast across the map.
			p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 165 / 100));
			p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 100 / 100));
			p.maxW = std::min(maxRoomSize, p.maxW + 2);
			p.maxH = std::min(maxRoomSize, p.maxH + 2);
			p.growthBursts = std::min(4, p.growthBursts + 1);
		}
		else if (silhouetteRoll <= 10)
		{
			// Rare compact accent pocket for abrupt local chokepoints.
			p.maxArea = std::max(4, p.maxArea * 64 / 100);
			p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 66 / 100)));
			p.maxW = std::max(2, p.maxW - 2);
			p.maxH = std::max(2, p.maxH - 1);
			p.growthBursts = std::max(1, p.growthBursts - 1);
		}

		// Strong route pulse shaping: alternate oversized and compact pockets as the path progresses.
		const int pathPulse = ((seed.pathRank >= 0 ? seed.pathRank : 0) * 17 + seed.branchDepth * 13 +
			(seed.onMainPath ? 31 : 17) + (seed.isHub || seed.isArena ? 41 : 0) + isProminent * 9) % 100;
		if (seed.onMainPath)
		{
			if (pathPulse >= 78)
			{
				p.maxW = std::min(maxRoomSize, p.maxW + 2);
				p.maxH = std::min(maxRoomSize, p.maxH + 2);
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 150 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 96 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 2);
			}
			else if (pathPulse <= 14)
			{
				p.maxArea = std::max(4, p.maxArea * 82 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 76 / 100)));
				p.maxW = std::max(2, p.maxW - 1);
				p.maxH = std::max(2, p.maxH - 1);
				p.growthBursts = std::max(1, p.growthBursts - 1);
			}
		}
		else if (!seed.onMainPath && seed.branchDepth >= 2)
		{
			if (pathPulse >= 86)
			{
				// Rare occasional roomy off-path cave where the branch does open up.
				p.maxW = std::min(maxRoomSize, p.maxW + 1);
				p.maxH = std::min(maxRoomSize, p.maxH + 1);
				p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxArea * 128 / 100));
				p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 82 / 100));
				p.growthBursts = std::min(4, p.growthBursts + 1);
			}
			else if (pathPulse <= 22)
			{
				p.maxArea = std::max(4, p.maxArea * 60 / 100);
				p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 65 / 100)));
				p.maxW = std::max(2, p.maxW - 1);
				p.maxH = std::max(2, p.maxH - 1);
				p.growthBursts = std::max(1, p.growthBursts - 1);
			}
		}

		if (p.sizeClass == 2)
		{
			const int minMainW = std::max(3, p.maxW - 2);
			const int minMainH = std::max(3, p.maxH - 2);
			p.maxW = std::max(3, std::min(maxRoomSize, std::max(p.maxW, minMainW)));
			p.maxH = std::max(3, std::min(maxRoomSize, std::max(p.maxH, minMainH)));
			const int mapLandmarkFloorW = std::max(4, std::min(maxRoomSize, mapRoomQuotient));
			const int mapLandmarkFloorH = std::max(4, std::min(maxRoomSize, std::max(4, (mapRoomQuotient * 3) / 5)));
			p.maxW = std::max(p.maxW, mapLandmarkFloorW);
			p.maxH = std::max(p.maxH, mapLandmarkFloorH);
			p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxW * p.maxH * 85 / 100));
			p.targetArea = std::min(p.maxArea, std::max(p.targetArea, std::max(4, (p.maxArea * 88) / 100)));
			p.growthBursts = std::min(4, p.growthBursts + 1);
			p.bias = (p.bias == 2) ? p.bias : ((RNG() % 2) == 0 ? 0 : 1);
		}
		else if (p.sizeClass == 0)
		{
			const int mapPocketCeil = std::max(2, std::min(5, std::max(2, mapRoomQuotient / 3)));
			p.maxW = std::max(2, std::min(p.maxW, mapPocketCeil));
			p.maxH = std::max(2, std::min(p.maxH, mapPocketCeil));
			p.maxArea = std::min(p.maxArea, p.maxW * p.maxH);
			p.maxArea = std::min(std::max(4, p.maxArea - (RNG() % 2)), p.maxW * p.maxH);
			p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 64 / 100)));
			p.growthBursts = std::max(1, p.growthBursts - 1);
			p.bias = 2;
		}

		// Reinforce explicit silhouette intent that was selected earlier so layout diversity is preserved.
		if (p.shapeHint == 0) // Wide emphasis.
		{
			p.bias = 0;
			p.maxW = std::min(maxRoomSize, std::max(p.maxW, p.maxH + 1));
			p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxW * p.maxH * 84 / 100));
			p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 88 / 100));
		}
		else if (p.shapeHint == 1) // Tall emphasis.
		{
			p.bias = 1;
			p.maxH = std::min(maxRoomSize, std::max(p.maxH, p.maxW + 1));
			p.maxArea = std::min(p.maxW * p.maxH, std::max(p.maxArea, p.maxW * p.maxH * 84 / 100));
			p.targetArea = std::min(p.maxArea, std::max(p.targetArea, p.maxArea * 88 / 100));
		}
		else if (p.shapeHint == 2) // Compact emphasis.
		{
			p.bias = 2;
			const int compactCap = std::max(2, std::min(7, std::max(2, mapRoomQuotient / 2 + 1)));
			p.maxW = std::max(2, std::min(p.maxW, compactCap));
			p.maxH = std::max(2, std::min(p.maxH, compactCap));
			p.maxArea = std::min(p.maxArea, p.maxW * p.maxH);
			p.targetArea = std::max(3, std::min(p.targetArea, std::max(4, p.maxArea * 72 / 100)));
			p.growthBursts = std::max(1, p.growthBursts - 1);
		}

		p.maxArea = clamp(p.maxArea, 4, p.maxW * p.maxH);
		p.targetArea = clamp(p.targetArea, 3, p.maxArea);
		};

	// Process expansion in rounds so rooms grow together more evenly.
	for (int round = 0; round < 32; round++)
	{
		bool anyExpanded = false;

		TArray<int> roomOrder;
		for (int i = 0; i < (int)Rooms.Size(); i++)
			if (Rooms[i].id >= 0)
				roomOrder.Push(i);

		if (roomOrder.Size() == 0) break;

		for (int i = (int)roomOrder.Size() - 1; i > 0; i--)
		{
			int j = RNG() % (i + 1);
			auto tmp = roomOrder[i];
			roomOrder[i] = roomOrder[j];
			roomOrder[j] = tmp;
		}

		for (int ri = 0; ri < (int)roomOrder.Size(); ri++)
		{
			int rid = roomOrder[ri];
			RoomInfo& room = Rooms[rid];
			if (room.id < 0) continue;

			const ProcGenCell* roomSeed = nullptr;
			for (int sj = room.minJ; sj <= room.maxJ && roomSeed == nullptr; sj++)
			{
				for (int si = room.minI; si <= room.maxI; si++)
				{
					if (Grid[sj][si].roomId == rid)
					{
						roomSeed = &Grid[sj][si];
						break;
					}
				}
			}
			if (roomSeed == nullptr) continue;

			if (growthProfiles[rid].maxW <= 0)
			{
				growthProfiles[rid] = PickGrowthProfile(*roomSeed);
			}

			bool expandedThisRound = false;
			int localGrowthBursts = std::max(1, growthProfiles[rid].growthBursts);
			int localMaxW = growthProfiles[rid].maxW;
			int localMaxH = growthProfiles[rid].maxH;
			int localMaxArea = growthProfiles[rid].maxArea;
			int localTargetArea = growthProfiles[rid].targetArea;
			int localBias = growthProfiles[rid].bias;
			const int localShapeHint = growthProfiles[rid].shapeHint;
			const int localSizeClass = growthProfiles[rid].sizeClass;
			const int localSizeBandLock = growthProfiles[rid].sizeBandLock;
			int localMinNeighbors = 0;
			double localTunnelPressure = 0.0;
			int localPathRank = 0;
			bool localBranch = !roomSeed->onMainPath && roomSeed->branchDepth >= 1;
			const int roomConnSum = roomSeed->conn[DIR_N] + roomSeed->conn[DIR_S] + roomSeed->conn[DIR_W] + roomSeed->conn[DIR_E];
			if (roomConnSum <= 1 || roomSeed->neighborCount <= 1)
			{
				localGrowthBursts = std::max(1, localGrowthBursts - 1);
				localTargetArea = std::max(3, localTargetArea - 2);
			}
			if (roomSeed->onMainPath && roomSeed->pathRank >= 5)
			{
				localMaxW = std::min(maxRoomSize, localMaxW + 1);
				localMaxH = std::min(maxRoomSize, localMaxH + 1);
				localTargetArea = clamp(localTargetArea + 2, 3, localMaxArea);
				localGrowthBursts = std::min(4, localGrowthBursts + 1);
			}
			if (roomSeed->isHub || roomSeed->isArena)
			{
				localMinNeighbors = 2;
				localTunnelPressure = 0.6;
				localBias = 2;
			}
			if (localBranch)
			{
				localMinNeighbors = std::max(localMinNeighbors, 1);
				localTunnelPressure += 0.4;
			}
			if (localShapeHint == 0)
				localBias = 0;
			else if (localShapeHint == 1)
				localBias = 1;
			else if (localShapeHint == 2)
				localBias = 2;
			if (roomSeed->pathRank >= 0)
			{
				localPathRank = std::min(8, roomSeed->pathRank);
			}
			if (growthProfiles[rid].maxArea >= (growthProfiles[rid].maxW * growthProfiles[rid].maxH * 85 / 100))
			{
				localGrowthBursts = std::min(4, localGrowthBursts + 1);
				localTargetArea = std::min(localMaxArea, localTargetArea + std::max(2, localMaxArea / 12));
			}
			if (localBias == 2)
			{
				localTunnelPressure = std::max(0.0, localTunnelPressure - 0.15);
			}
			else
			{
				localTunnelPressure = std::min(1.0, localTunnelPressure + 0.1);
			}
			if ((growthProfiles[rid].maxW > growthProfiles[rid].maxH * 2) ||
				(growthProfiles[rid].maxH > growthProfiles[rid].maxW * 2))
			{
				localTunnelPressure = std::min(1.0, localTunnelPressure + 0.15);
			}
			if (localPathRank >= 7)
			{
				localGrowthBursts = std::min(4, localGrowthBursts + 1);
			}
			if (localSizeBandLock == 0)
				{
					localGrowthBursts = std::max(1, localGrowthBursts - 1);
					const int lockPocketCap = std::max(2, std::min(3, std::max(2, mapRoomQuotient / 3)));
					localMaxW = std::max(2, std::min(localMaxW - 1, lockPocketCap));
					localMaxH = std::max(2, std::min(localMaxH - 1, lockPocketCap));
					localMaxArea = std::max(4, localMaxArea * 45 / 100);
					localMaxArea = std::min(localMaxArea, localMaxW * localMaxH);
					localTargetArea = std::max(3, std::min(localTargetArea, std::max(4, localMaxArea * 54 / 100)));
					localTunnelPressure = std::max(0.0, localTunnelPressure - 0.2);
				}
				else if (localSizeBandLock == 2)
				{
					localGrowthBursts = std::min(4, localGrowthBursts + 1);
					const int macroMinW = std::max(4, std::min(maxRoomSize, mapRoomQuotient + 2 + (localGrowthBursts > 2 ? 1 : 0) + (localPathRank > 4 ? 1 : 0)));
					const int macroMinH = std::max(4, std::min(maxRoomSize, std::max(4, (mapRoomQuotient * 4) / 5 + 2)));
					localMaxW = std::max(localMaxW, macroMinW);
					localMaxH = std::max(localMaxH, macroMinH);
					localMaxArea = std::min(localMaxW * localMaxH, std::max(localMaxArea, localMaxArea * 190 / 100));
					localTargetArea = std::min(localMaxArea, std::max(localTargetArea, localMaxArea * 92 / 100));
					localTunnelPressure = std::min(1.0, localTunnelPressure + 0.12);
				}
				else if (localSizeClass == 0)
				{
					localGrowthBursts = std::max(1, localGrowthBursts - 1);
					const int localPocketCap = std::max(2, std::min(5, std::max(2, mapRoomQuotient / 2)));
					localMaxW = std::max(2, std::min(localMaxW - 1, localPocketCap));
					localMaxH = std::max(2, std::min(localMaxH - 1, localPocketCap));
					localMaxArea = std::max(4, localMaxArea * 62 / 100);
					localMaxArea = std::min(localMaxArea, localMaxW * localMaxH);
					localTargetArea = std::max(3, std::min(localTargetArea, std::max(4, localMaxArea * 68 / 100)));
					localTunnelPressure = std::max(0.0, localTunnelPressure - 0.2);
				}
			else if (localSizeClass == 2)
			{
				localGrowthBursts = std::min(4, localGrowthBursts + 1);
				const int localLandmarkMinW = std::max(3, std::min(maxRoomSize, std::max(3, mapRoomQuotient - 1)));
				const int localLandmarkMinH = std::max(3, std::min(maxRoomSize, std::max(3, (mapRoomQuotient * 3) / 4)));
				localMaxW = std::max(localMaxW + 1, localLandmarkMinW);
				localMaxH = std::max(localMaxH + 1, localLandmarkMinH);
				localMaxW = std::min(maxRoomSize, localMaxW);
				localMaxH = std::min(maxRoomSize, localMaxH);
				localMaxArea = std::min(localMaxW * localMaxH, std::max(localMaxArea, localMaxArea * 125 / 100));
				localTargetArea = std::min(localMaxArea, std::max(localTargetArea, localMaxArea * 80 / 100));
				localTunnelPressure = std::min(1.0, localTunnelPressure + 0.1);
			}

			for (int burst = 0; burst < localGrowthBursts; burst++)
			{
				int rw = room.maxI - room.minI + 1;
				int rh = room.maxJ - room.minJ + 1;
				if (rw >= localMaxW && rh >= localMaxH) break;
				if (room.cellCount >= localTargetArea && (RNG() % 100) < 80) break;
				if (burst > 0 && (RNG() % 100) < 40) break;

				int dirOrder[4] = { DIR_N, DIR_S, DIR_W, DIR_E };
				for (int a = 3; a > 0; a--)
				{
					int b = RNG() % (a + 1);
					int tmp = dirOrder[a];
					dirOrder[a] = dirOrder[b];
					dirOrder[b] = tmp;
				}

				bool expanded = false;
				for (int di = 0; di < 4 && !expanded; di++)
				{
					int dir = dirOrder[di];
					int stripMinI = room.minI;
					int stripMaxI = room.maxI;
					int stripMinJ = room.minJ;
					int stripMaxJ = room.maxJ;
					if (dir == DIR_E) stripMinI = stripMaxI = room.maxI + 1;
					else if (dir == DIR_W) stripMinI = stripMaxI = room.minI - 1;
					else if (dir == DIR_S) stripMinJ = stripMaxJ = room.maxJ + 1;
					else if (dir == DIR_N) stripMinJ = stripMaxJ = room.minJ - 1;

					if (stripMinI < 0 || stripMaxI >= W || stripMinJ < 0 || stripMaxJ >= H)
						continue;
					if ((dir == DIR_E || dir == DIR_W) && rw >= localMaxW)
						continue;
					if ((dir == DIR_N || dir == DIR_S) && rh >= localMaxH)
						continue;
					if (localMinNeighbors > 0)
					{
						int neighborCountDir = 0;
					if (dir == DIR_E)
					{
						for (int cj = stripMinJ; cj <= stripMaxJ; cj++)
							neighborCountDir += Grid[cj][stripMinI - 1].neighborCount;
						int edgeLen = stripMaxJ - stripMinJ + 1;
						if (neighborCountDir >= localMinNeighbors * std::max(1, edgeLen))
							continue;
					}
					else if (dir == DIR_W)
					{
						for (int cj = stripMinJ; cj <= stripMaxJ; cj++)
							neighborCountDir += Grid[cj][stripMaxI + 1].neighborCount;
						int edgeLen = stripMaxJ - stripMinJ + 1;
						if (neighborCountDir >= localMinNeighbors * std::max(1, edgeLen))
							continue;
					}
					else if (dir == DIR_S)
					{
						for (int ci = stripMinI; ci <= stripMaxI; ci++)
							neighborCountDir += Grid[stripMinJ - 1][ci].neighborCount;
						int edgeLen = stripMaxI - stripMinI + 1;
						if (neighborCountDir >= localMinNeighbors * std::max(1, edgeLen))
							continue;
					}
					else if (dir == DIR_N)
					{
						for (int ci = stripMinI; ci <= stripMaxI; ci++)
							neighborCountDir += Grid[stripMaxJ + 1][ci].neighborCount;
						int edgeLen = stripMaxI - stripMinI + 1;
						if (neighborCountDir >= localMinNeighbors * std::max(1, edgeLen))
							continue;
					}
					}

					if (localBias == 0 && (dir == DIR_N || dir == DIR_S) && rw < localMaxW - 1)
					{
						if (RNG() % 10 > 4) continue;
					}
					else if (localBias == 1 && (dir == DIR_E || dir == DIR_W) && rh < localMaxH - 1)
					{
						if (RNG() % 10 > 4) continue;
					}

					int unionMinI = room.minI;
					int unionMaxI = room.maxI;
					int unionMinJ = room.minJ;
					int unionMaxJ = room.maxJ;
					int mergedCellCount = room.cellCount;
					bool canExpand = true;
					TArray<int> mergedIds;

					for (int cj = stripMinJ; cj <= stripMaxJ && canExpand; cj++)
					{
						for (int ci = stripMinI; ci <= stripMaxI && canExpand; ci++)
						{
							if (!Grid[cj][ci].present)
							{
								canExpand = false;
								break;
							}

							int ai = ci;
							int aj = cj;
							int bi = ci;
							int bj = cj;
							if (dir == DIR_E) ai = ci - 1;
							else if (dir == DIR_W) ai = ci + 1;
							else if (dir == DIR_S) aj = cj - 1;
							else if (dir == DIR_N) aj = cj + 1;

							if (!CellsCompatible(Grid[aj][ai], Grid[bj][bi]))
							{
								canExpand = false;
								break;
							}

							if (dir == DIR_E && (!Grid[aj][ai].conn[DIR_E] || !Grid[bj][bi].conn[DIR_W]))
							{
								canExpand = false;
								break;
							}
							if (dir == DIR_W && (!Grid[aj][ai].conn[DIR_W] || !Grid[bj][bi].conn[DIR_E]))
							{
								canExpand = false;
								break;
							}
							if (dir == DIR_S && (!Grid[aj][ai].conn[DIR_S] || !Grid[bj][bi].conn[DIR_N]))
							{
								canExpand = false;
								break;
							}
							if (dir == DIR_N && (!Grid[aj][ai].conn[DIR_N] || !Grid[bj][bi].conn[DIR_S]))
							{
								canExpand = false;
								break;
							}

								int oldId = Grid[cj][ci].roomId;
								if (oldId >= 0 && oldId != rid)
								{
									const int oldBandLock = growthProfiles[oldId].sizeBandLock;
									const int oldClass = growthProfiles[oldId].sizeClass;
									if ((localSizeBandLock >= 0 && oldBandLock >= 0 && localSizeBandLock != oldBandLock) ||
										(localSizeBandLock >= 0 && oldBandLock < 0 &&
											((localSizeBandLock == 0 && oldClass != 0) ||
											 (localSizeBandLock == 2 && oldClass != 2))) ||
										(localSizeBandLock < 0 && oldBandLock >= 0 && (oldClass == 0 || oldClass == 2)))
									{
										canExpand = false;
										break;
									}

								bool alreadyMerged = false;
								for (unsigned int mi = 0; mi < mergedIds.Size(); mi++)
								{
									if (mergedIds[mi] == oldId)
									{
										alreadyMerged = true;
										break;
									}
								}
								if (!alreadyMerged)
								{
									RoomInfo& other = Rooms[oldId];
									unionMinI = std::min(unionMinI, other.minI);
									unionMaxI = std::max(unionMaxI, other.maxI);
									unionMinJ = std::min(unionMinJ, other.minJ);
									unionMaxJ = std::max(unionMaxJ, other.maxJ);
									mergedCellCount += other.cellCount;
									mergedIds.Push(oldId);
								}
							}
						}
					}

					if (!canExpand)
						continue;

					int newW = unionMaxI - unionMinI + 1;
					int newH = unionMaxJ - unionMinJ + 1;
					int localSoftArea = std::min(localMaxArea, localTargetArea + 4 + (RNG() % 5));
					bool elongatedDir = (localBias == 0 && (dir == DIR_E || dir == DIR_W)) || (localBias == 1 && (dir == DIR_N || dir == DIR_S));
					if (localTunnelPressure > 0.7 && !elongatedDir && RNG() % 10 < 5)
						continue;

					if (newW > localMaxW || newH > localMaxH || mergedCellCount > localMaxArea)
						continue;
					if (mergedCellCount >= localSoftArea && (RNG() % 100) < 40)
						continue;

					int mergedCells = std::max(1, room.cellCount);
					for (unsigned int mi = 0; mi < mergedIds.Size(); mi++)
						{
							int oldId = mergedIds[mi];
							const int incomingCells = std::max(1, Rooms[oldId].cellCount);
							if (growthProfiles[oldId].maxW <= 0)
							{
								const ProcGenCell* mergedSeed = nullptr;
								for (int oj = Rooms[oldId].minJ; oj <= Rooms[oldId].maxJ && mergedSeed == nullptr; oj++)
								{
								for (int oi = Rooms[oldId].minI; oi <= Rooms[oldId].maxI; oi++)
								{
									if (Grid[oj][oi].roomId == oldId)
									{
										mergedSeed = &Grid[oj][oi];
										break;
									}
								}
							}
								if (mergedSeed != nullptr)
									growthProfiles[oldId] = PickGrowthProfile(*mergedSeed);
								else
									growthProfiles[oldId] = growthProfiles[rid];
							}

						growthProfiles[rid].maxW = std::max(growthProfiles[rid].maxW, growthProfiles[oldId].maxW);
						growthProfiles[rid].maxH = std::max(growthProfiles[rid].maxH, growthProfiles[oldId].maxH);
							growthProfiles[rid].maxArea = std::max(growthProfiles[rid].maxArea, growthProfiles[oldId].maxArea + 2);
							growthProfiles[rid].targetArea = std::max(growthProfiles[rid].targetArea, std::max(4, growthProfiles[oldId].targetArea));
							growthProfiles[rid].growthBursts = std::max(growthProfiles[rid].growthBursts, growthProfiles[oldId].growthBursts);
							growthProfiles[rid].sizeClass = MergeRoomSizeClass(
								growthProfiles[rid].sizeClass,
								mergedCells,
								growthProfiles[oldId].sizeClass,
								incomingCells);
							if (growthProfiles[rid].sizeBandLock < 0 && growthProfiles[oldId].sizeBandLock >= 0)
								growthProfiles[rid].sizeBandLock = growthProfiles[oldId].sizeBandLock;
							mergedCells += incomingCells;
							RoomInfo& other = Rooms[oldId];
							for (int oj = other.minJ; oj <= other.maxJ; oj++)
							{
								for (int oi = other.minI; oi <= other.maxI; oi++)
							{
								if (Grid[oj][oi].roomId == oldId)
									Grid[oj][oi].roomId = rid;
							}
						}
						Rooms[oldId].id = -1;
					}

					for (int cj = stripMinJ; cj <= stripMaxJ; cj++)
						for (int ci = stripMinI; ci <= stripMaxI; ci++)
							Grid[cj][ci].roomId = rid;

					room.minI = unionMinI;
					room.maxI = unionMaxI;
					room.minJ = unionMinJ;
					room.maxJ = unionMaxJ;
					room.cellCount = mergedCellCount;
					expanded = true;
					expandedThisRound = true;
					anyExpanded = true;
					if (RNG() % 100 < 35 && mergedCellCount > localTargetArea)
						break;
				}
			}
			if (!expandedThisRound && growthProfiles[rid].targetArea > 5 && (RNG() % 100) < 25)
			{
				growthProfiles[rid].targetArea = std::max(4, growthProfiles[rid].targetArea - 2);
			}
		}

		if (!anyExpanded) break;
	}
}

// ---------------------------------------------------------------------------
// Coherence Passes
// ---------------------------------------------------------------------------

void FProceduralMapGenerator::ApplyCoherence(int W, int H)
{
	// Compute BFS distances per-room from start
	DistMap.Clear();
	DistMap.Resize(H);
	for (int j = 0; j < H; j++)
		DistMap[j].Resize(W);
	for (int j = 0; j < H; j++)
		for (int i = 0; i < W; i++)
			DistMap[j][i] = -1;

	TArray<int> roomQueue;
	TArray<bool> roomVisited;
	roomVisited.Resize(Rooms.Size());
	for (unsigned int i = 0; i < roomVisited.Size(); i++)
		roomVisited[i] = false;

	int startRoom = -1;
	for (int j = 0; j < H; j++)
		for (int i = 0; i < W; i++)
			if (Grid[j][i].present && Grid[j][i].hasPlayerStart)
				startRoom = Grid[j][i].roomId;

	if (startRoom >= 0)
	{
		roomQueue.Push(startRoom);
		roomVisited[startRoom] = true;
		Rooms[startRoom].distFromStart = 0;
	}

	for (unsigned int qi = 0; qi < roomQueue.Size(); qi++)
	{
		int rid = roomQueue[qi];
		RoomInfo& room = Rooms[rid];

		for (int j = room.minJ; j <= room.maxJ; j++)
		{
			for (int i = room.minI; i <= room.maxI; i++)
			{
				if (Grid[j][i].roomId != rid) continue;
				for (int d = 0; d < 4; d++)
				{
					if (!Grid[j][i].conn[d]) continue;
					int ni = i + DX[d];
					int nj = j + DY[d];
					if (ni < 0 || ni >= W || nj < 0 || nj >= H) continue;
					if (!Grid[nj][ni].present) continue;
					int nrid = Grid[nj][ni].roomId;
					if (nrid >= 0 && !roomVisited[nrid])
					{
						roomVisited[nrid] = true;
						Rooms[nrid].distFromStart = room.distFromStart + 1;
						roomQueue.Push(nrid);
					}
				}
			}
		}
	}

	// Copy room flags from cells to rooms + compute dead-end status
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.id < 0) continue;
		room.isDeadEnd = true;
		room.hasDoor = false;
		room.hasPlayerStart = false;
		room.hasExit = false;
		room.hasBoss = false;
		room.hasKey = false;
		room.keyType = 0;
		room.isLocked = false;
		room.lockType = 0;
		room.hasWeapon = false;
		room.weaponType = 0;
		room.hasAmmo = false;
		room.ammoType = 0;
		room.hasHealth = false;
		room.healthType = 0;
		room.hasArmor = false;
		room.armorType = 0;
		room.cellCount = 0;
		room.progressionRank = 9999;
		room.branchDepth = 0;
		room.onMainPath = false;
		room.isArena = false;
		room.isHub = false;
	}

	for (int j = 0; j < H; j++)
	{
		for (int i = 0; i < W; i++)
		{
			if (!Grid[j][i].present) continue;
			int rid = Grid[j][i].roomId;
			if (rid < 0) continue;
			Rooms[rid].cellCount++;
			if (Grid[j][i].hasPlayerStart) Rooms[rid].hasPlayerStart = true;
			if (Grid[j][i].hasExit) Rooms[rid].hasExit = true;
			if (Grid[j][i].hasBoss) Rooms[rid].hasBoss = true;
			if (Grid[j][i].hasKey) { Rooms[rid].hasKey = true; Rooms[rid].keyType = Grid[j][i].keyType; }
			if (Grid[j][i].isLocked) { Rooms[rid].isLocked = true; Rooms[rid].lockType = Grid[j][i].lockType; }
			if (Grid[j][i].pathRank >= 0 && Grid[j][i].pathRank < Rooms[rid].progressionRank)
				Rooms[rid].progressionRank = Grid[j][i].pathRank;
			if (Grid[j][i].branchDepth > Rooms[rid].branchDepth)
				Rooms[rid].branchDepth = Grid[j][i].branchDepth;
			if (Grid[j][i].onMainPath) Rooms[rid].onMainPath = true;
			if (Grid[j][i].isArena) Rooms[rid].isArena = true;
			if (Grid[j][i].isHub) Rooms[rid].isHub = true;
		}
	}

	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.id < 0) continue;
		int roomConns = 0;
		for (int j = room.minJ; j <= room.maxJ; j++)
		{
			for (int i = room.minI; i <= room.maxI; i++)
			{
				if (Grid[j][i].roomId != (int)ri) continue;
				for (int d = 0; d < 4; d++)
				{
					if (!Grid[j][i].conn[d]) continue;
					int ni = i + DX[d];
					int nj = j + DY[d];
					if (ni < 0 || ni >= W || nj < 0 || nj >= H) continue;
					if (!Grid[nj][ni].present) continue;
					if (Grid[nj][ni].roomId != (int)ri) roomConns++;
				}
			}
		}
		room.isDeadEnd = (roomConns <= 1);
		if (!room.isHub)
			room.isHub = room.onMainPath && roomConns >= 3;
	}

	// Assign textures, heights, lighting, and enemies per-room
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.id < 0) continue;

		int dist = room.distFromStart;

		// Lighting: brighter starts and key moments, moodier side branches.
		room.light = 192 - dist * 5;
		if (room.onMainPath) room.light += 8;
		if (room.isArena) room.light -= 8;
		if (room.isHub) room.light += 8;
		if (room.isDeadEnd && !room.hasKey) room.light -= 16;
		if (room.hasPlayerStart) room.light = 224;
		if (room.hasKey) room.light = 216;
		if (room.isLocked) room.light = 184;
		if (room.hasExit) room.light = 208;
		if (room.light < 96) room.light = 96;
		if (room.light > 224) room.light = 224;

		if (Theme.Compare("techbase") == 0)
		{
			room.floorTex = TechFloors[RNG() % countof(TechFloors)];
			room.ceilTex  = TechCeils[RNG() % countof(TechCeils)];
			room.wallTex  = TechWalls[RNG() % countof(TechWalls)];
		}
		else if (Theme.Compare("hell") == 0)
		{
			room.floorTex = HellFloors[RNG() % countof(HellFloors)];
			room.ceilTex  = HellCeils[RNG() % countof(HellCeils)];
			room.wallTex  = HellWalls[RNG() % countof(HellWalls)];
		}
		else
		{
			room.floorTex = GenFloors[RNG() % countof(GenFloors)];
			room.ceilTex  = GenCeils[RNG() % countof(GenCeils)];
			room.wallTex  = GenWalls[RNG() % countof(GenWalls)];
		}

		if (room.hasPlayerStart)
		{
			room.floorZ = 0.0;
			room.ceilZ = 160.0;
		}
		else if (room.hasExit || room.hasBoss)
		{
			room.floorZ = -24.0;
			room.ceilZ = 224.0;
		}
		else if (room.isHub)
		{
			room.floorZ = 0.0;
			room.ceilZ = 192.0;
		}
		else if (room.isArena)
		{
			room.floorZ = -8.0 * (dist % 3);
			room.ceilZ = 192.0 + 16.0 * (Difficulty >= 3);
		}
		else if (room.hasKey)
		{
			room.floorZ = 8.0;
			room.ceilZ = 160.0;
		}
		else if (room.isLocked)
		{
			room.floorZ = -16.0;
			room.ceilZ = 176.0;
		}
		else
		{
			room.floorZ = -8.0 * (dist % 3);
			room.ceilZ = 128.0 + 16.0 * (room.onMainPath ? 1 : 0);
		}

		if (room.onMainPath && !room.isHub && !room.isArena && !room.hasKey && !room.isLocked &&
			!room.hasBoss && !room.hasExit && room.cellCount >= 2)
		{
			room.ceilZ += 24.0;
		}

		if (room.hasKey && room.cellCount >= 3)
		{
			room.floorZ -= 8.0;
			room.ceilZ += 32.0;
		}
		if (room.isLocked && room.cellCount >= 3)
		{
			room.ceilZ += 16.0;
		}
		if (room.hasWeapon && !room.hasPlayerStart && !room.hasExit)
		{
			room.floorZ -= 8.0;
			room.ceilZ += 32.0;
		}
		if (room.hasBoss || room.hasExit)
		{
			room.floorZ -= 8.0;
			room.ceilZ += 32.0;
		}
		if (room.isHub && room.progressionRank >= 2 && !room.hasKey && !room.hasBoss)
		{
			room.ceilZ += 24.0;
		}
		if (!room.onMainPath && room.branchDepth == 1 && !room.hasExit && !room.hasBoss)
		{
			room.floorZ -= 8.0;
			room.ceilZ += 16.0;
		}
		if (room.branchDepth >= 2 && !room.hasExit && !room.hasBoss)
		{
			room.floorZ -= 8.0;
			room.ceilZ += 16.0;
		}
		if (room.hasWeapon && !room.onMainPath && room.cellCount >= 3)
		{
			room.floorZ -= 8.0;
			room.ceilZ += 24.0;
		}

		if (!room.hasPlayerStart && !room.hasExit && !room.hasKey && room.isArena && (RNG() % 4) == 0)
		{
			room.ceilTex = "F_SKY1";
			room.ceilZ += 96.0;
		}
		else if (room.isHub && !room.isLocked && !room.hasBoss && (RNG() % 3) != 0)
		{
			room.ceilTex = "F_SKY1";
			room.ceilZ += 64.0;
		}

		if (!room.hasPlayerStart && !room.hasExit && !room.hasKey && !room.isLocked &&
			!room.onMainPath && room.cellCount <= 2 && (RNG() % 6) == 0)
		{
			room.floorZ = -24.0;
			if (Theme.Compare("hell") == 0)
				room.floorTex = "LAVA1";
			else
				room.floorTex = "NUKAGE1";
		}

		// Add small height variations so corridor openings are visible between adjacent rooms
		room.floorZ += (RNG() % 3) * 4.0;
		room.ceilZ += (RNG() % 3) * 4.0;

		if (room.hasKey)
			room.wallTex = (Theme.Compare("hell") == 0) ? "SP_HOT1" : "COMPUTE1";
		else if (room.isLocked)
			room.wallTex = (Theme.Compare("hell") == 0) ? "MARBLE1" : "TEKWALL4";
		else if (room.isHub)
			room.wallTex = (Theme.Compare("hell") == 0) ? "WOODMET1" : "STARTAN3";
		else if (room.isArena)
			room.wallTex = (Theme.Compare("hell") == 0) ? "GSTVINE1" : "STARG2";

		if (room.hasBoss)
		{
			room.ceilTex = "F_SKY1";
			room.light = 224;
			room.wallTex = (Theme.Compare("hell") == 0) ? "SKINMET1" : "TEKWALL4";
		}
		else if (room.hasExit)
		{
			room.light = 216;
		}
		else if (room.hasWeapon && !room.hasPlayerStart)
		{
			room.light = std::min(room.light + 12, 224);
			if (!room.isLocked && !room.hasKey)
			{
				room.ceilTex = "F_SKY1";
				room.ceilZ += 48.0;
			}
			room.wallTex = (Theme.Compare("hell") == 0) ? "SP_HOT1" : "COMPUTE1";
		}
		else if (room.onMainPath && !room.isHub && !room.isArena && !room.hasExit && !room.hasBoss)
		{
			room.light = std::min(room.light + 6, 216);
		}

		if (room.onMainPath && room.progressionRank >= 2 && !room.isHub && !room.hasKey &&
			!room.isLocked && !room.hasExit && !room.hasBoss)
		{
			room.wallTex = (Theme.Compare("hell") == 0) ? "MARBLE1" : "STARTAN3";
			room.light = std::min(room.light + 6, 220);
			if (Theme.Compare("hell") == 0)
				room.floorTex = "FLOOR7_2";
			else
				room.floorTex = "FLOOR4_8";
			if (room.progressionRank >= 4 && room.cellCount >= 3)
				room.ceilTex = "F_SKY1";
		}
		else if (room.branchDepth >= 2 && !room.hasExit && !room.hasBoss)
		{
			room.wallTex = (Theme.Compare("hell") == 0) ? "GSTVINE1" : "STARG2";
			room.light = std::min(room.light + 4, 212);
			if (Theme.Compare("hell") == 0)
				room.floorTex = "FLAT5_1";
			else
				room.floorTex = "FLOOR0_3";
			if (room.cellCount >= 3 && Theme.Compare("hell") == 0)
				room.ceilTex = "F_SKY1";
		}
		else if (!room.onMainPath && room.branchDepth == 1 && !room.hasExit && !room.hasBoss)
		{
			room.wallTex = (Theme.Compare("hell") == 0) ? "MARBLE1" : "STARG2";
			room.light = std::min(room.light + 2, 208);
			if (Theme.Compare("hell") == 0)
				room.floorTex = "FLOOR6_1";
			else
				room.floorTex = "FLOOR0_1";
		}
		if (room.hasWeapon && !room.onMainPath && !room.hasExit)
		{
			room.wallTex = (Theme.Compare("hell") == 0) ? "SP_HOT1" : "COMPUTE1";
			room.light = std::min(room.light + 8, 220);
			if (Theme.Compare("hell") == 0)
				room.floorTex = "FLOOR6_2";
			else
				room.floorTex = "FLOOR1_1";
		}

		int roomArea = room.cellCount;
		int maxEnemies = clamp(roomArea * ((room.isArena || room.isHub) ? 3 : 2), 1, 14 + Difficulty * 2);
		int enemyPressure = Difficulty - 1 + dist / 3;
		if (room.onMainPath) enemyPressure += 1;
		if (room.isHub) enemyPressure += 2;
		if (room.isArena) enemyPressure += 2;
		if (room.hasKey) enemyPressure += 2;
		if (room.isLocked) enemyPressure += 2;
		if (room.hasBoss) enemyPressure += 3;
		if (room.isDeadEnd && !room.hasKey) enemyPressure += 1;
		room.monsterTier = clamp(1 + Difficulty / 3 + dist / 3 + (room.isHub ? 1 : 0) + (room.isArena ? 1 : 0) + (room.isLocked ? 1 : 0), 1, 5);
		room.hasHealth = false;
		room.healthType = 0;
		bool earlyPhase = room.progressionRank >= 0 && room.progressionRank <= 1;
		bool midPhase = room.progressionRank >= 2 && room.progressionRank <= 4;
		bool latePhase = room.progressionRank >= 5;

		if (room.hasPlayerStart)
		{
			room.enemyCount = 0;
			room.monsterTier = 0;
		}
		else if (room.progressionRank <= 1 && !room.hasKey && !room.isLocked)
		{
			room.enemyCount = clamp(room.enemyCount, 0, 2 + Difficulty / 2);
			room.monsterTier = 1;
		}
		else if (room.hasBoss)
		{
			room.enemyCount = clamp(2 + Difficulty + roomArea, 3, maxEnemies);
		}
		else if (room.hasExit)
		{
			room.enemyCount = clamp(1 + Difficulty + roomArea / 2, 2, maxEnemies);
		}
		else if (room.hasKey)
		{
			room.enemyCount = clamp(1 + Difficulty + roomArea / 2, 2, maxEnemies);
		}
		else if (room.isLocked)
		{
			room.enemyCount = clamp(2 + Difficulty + roomArea / 2, 2, maxEnemies);
			room.hasDoor = true;
		}
		else if (room.isArena)
		{
			room.enemyCount = clamp(1 + enemyPressure + roomArea / 2, 2, maxEnemies);
		}
		else if (room.isHub)
		{
			room.enemyCount = clamp(2 + enemyPressure + roomArea / 3, 3, maxEnemies);
		}
		else if (room.onMainPath)
		{
			room.enemyCount = clamp(enemyPressure, 1, maxEnemies);
		}
		else if (room.isDeadEnd)
		{
			room.enemyCount = clamp(enemyPressure / 2, 1, maxEnemies);
			room.hasDoor = true;
		}
		else
		{
			room.enemyCount = clamp(1 + enemyPressure / 2, 1, maxEnemies);
		}

		// Prevent lethal density spikes in tiny branch pockets.
		if (!room.hasPlayerStart && room.cellCount <= 8 && room.enemyCount > (2 + Difficulty / 3))
		{
			room.enemyCount = std::max(1, 1 + Difficulty / 3);
			room.monsterTier = std::min(room.monsterTier, 2 + Difficulty / 4);
		}

		if (earlyPhase && room.onMainPath && !room.hasKey && !room.isLocked && !room.hasExit)
		{
			room.enemyCount = std::min(room.enemyCount, 2 + Difficulty / 2);
			room.monsterTier = std::min(room.monsterTier, 2);
		}

		// Use doors and chokes as progression language so the widened graph
		// still reads as chamber-to-chamber Doom movement instead of a smeared floorplan.
		if (!room.hasPlayerStart && !room.hasExit && !room.hasBoss)
		{
			if (room.isLocked || room.hasKey)
			{
				room.hasDoor = true;
			}
		else if (room.onMainPath && !room.isHub && !room.isArena && room.progressionRank >= 3)
		{
			room.hasDoor = true;
		}
			else if (room.branchDepth >= 2)
			{
				room.hasDoor = true;
			}
			else if (room.hasWeapon && !room.onMainPath)
			{
				room.hasDoor = true;
			}
			else if (!room.onMainPath && room.branchDepth >= 1)
			{
				room.hasDoor = true;
			}
			if (latePhase && room.onMainPath && !room.isHub)
				room.hasDoor = true;
			if (room.isHub && room.progressionRank >= 2)
				room.hasDoor = true;
			if (room.hasWeapon && room.onMainPath)
				room.hasDoor = true;
		}

		if (room.hasWeapon && !room.hasPlayerStart && !room.hasExit)
		{
			room.enemyCount = std::max(room.enemyCount, 1 + Difficulty / 2 + (room.cellCount >= 3));
			room.monsterTier = std::min(5, std::max(room.monsterTier, 1 + Difficulty / 2));
		}
		if (room.hasKey)
		{
			room.enemyCount = std::max(room.enemyCount, 1 + Difficulty / 2 + (room.cellCount >= 3));
			room.monsterTier = std::min(5, std::max(room.monsterTier, 1 + Difficulty / 2));
		}
		if (room.isLocked)
		{
			room.enemyCount = std::max(room.enemyCount, 3 + Difficulty / 2);
			room.monsterTier = std::min(5, std::max(room.monsterTier, 2 + Difficulty / 2));
		}
		if (room.onMainPath && room.progressionRank >= 2 && !room.isHub && !room.hasKey &&
			!room.isLocked && !room.hasExit && !room.hasBoss)
		{
			room.enemyCount = std::max(room.enemyCount, 2 + room.progressionRank / 2);
			room.monsterTier = std::min(5, std::max(room.monsterTier, 2 + room.progressionRank / 3));
		}
		if (midPhase && room.onMainPath && !room.isHub && !room.hasExit && !room.hasBoss)
		{
			room.enemyCount = std::max(room.enemyCount, 2 + Difficulty / 2);
			room.monsterTier = std::min(5, std::max(room.monsterTier, 2));
		}
		if (latePhase && room.onMainPath && !room.hasExit && !room.hasBoss)
		{
			room.enemyCount = std::max(room.enemyCount, 3 + Difficulty / 2 + room.cellCount / 2);
			room.monsterTier = std::min(5, std::max(room.monsterTier, 3));
		}
		if (room.branchDepth >= 2 && !room.hasExit && !room.hasBoss)
		{
			room.enemyCount = std::max(room.enemyCount, 2 + room.branchDepth + Difficulty / 3);
			room.monsterTier = std::min(5, std::max(room.monsterTier, 2 + room.branchDepth / 2));
		}
		if (!room.onMainPath && room.branchDepth == 1 && !room.hasExit && !room.hasBoss)
		{
			room.enemyCount = std::max(room.enemyCount, 1 + Difficulty / 2 + room.cellCount / 2);
			room.monsterTier = std::min(5, std::max(room.monsterTier, 2));
		}
		if (room.hasWeapon && !room.onMainPath && !room.hasExit)
		{
			room.enemyCount = std::max(room.enemyCount, 3 + Difficulty / 2);
			room.monsterTier = std::min(5, std::max(room.monsterTier, 3));
		}

		// Tiny rooms should stay survivable.
		if (!room.hasPlayerStart && room.cellCount <= 2 && !room.hasBoss && !room.hasExit)
		{
			room.enemyCount = std::min(room.enemyCount, 1 + (Difficulty >= 3 ? 1 : 0));
			room.monsterTier = std::min(room.monsterTier, 2);
		}
		if (!room.hasPlayerStart && room.progressionRank <= 2 && room.cellCount <= 3 && !room.hasBoss && !room.hasExit)
		{
			room.enemyCount = std::min(room.enemyCount, 1);
			room.monsterTier = 1;
		}
		if (!room.hasPlayerStart && room.progressionRank <= 2 && room.cellCount <= 4 && !room.hasExit && !room.hasBoss)
		{
			room.hasHealth = true;
			room.healthType = 2012;
		}

		if (!room.hasPlayerStart && !room.hasExit &&
			(room.hasKey || room.isLocked || room.isArena || room.enemyCount >= 3 + Difficulty / 2))
		{
			room.hasHealth = true;
			room.healthType = HealthDrops[RNG() % countof(HealthDrops)];
		}

		for (int j = room.minJ; j <= room.maxJ; j++)
		{
			for (int i = room.minI; i <= room.maxI; i++)
			{
				if (Grid[j][i].roomId == (int)ri)
				{
					Grid[j][i].floorZ = room.floorZ;
					Grid[j][i].ceilZ = room.ceilZ;
					Grid[j][i].floorTex = room.floorTex;
					Grid[j][i].ceilTex = room.ceilTex;
					Grid[j][i].wallTex = room.wallTex;
					Grid[j][i].light = room.light;
					Grid[j][i].enemyCount = room.enemyCount;
					Grid[j][i].monsterTier = room.monsterTier;
					Grid[j][i].hasHealth = room.hasHealth;
					Grid[j][i].healthType = room.healthType;
				}
			}
		}
	}

	// Height smoothing pass
	bool changed = true;
	for (int pass = 0; pass < 5 && changed; pass++)
	{
		changed = false;
		for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		{
			RoomInfo& room = Rooms[ri];
			if (room.id < 0) continue;

			for (int j = room.minJ; j <= room.maxJ; j++)
			{
				for (int i = room.minI; i <= room.maxI; i++)
				{
					if (Grid[j][i].roomId != (int)ri) continue;
					for (int d = 0; d < 4; d++)
					{
						if (!Grid[j][i].conn[d]) continue;
						int ni = i + DX[d];
						int nj = j + DY[d];
						if (ni < 0 || ni >= W || nj < 0 || nj >= H) continue;
						if (!Grid[nj][ni].present) continue;
						int nrid = Grid[nj][ni].roomId;
						if (nrid == (int)ri) continue;
						if (nrid < 0) continue;
						RoomInfo& other = Rooms[nrid];
						if (other.id < 0) continue;
						double diff = room.floorZ - other.floorZ;
						if (fabs(diff) > 16.0)
						{
							double adjust = (diff > 0) ? 8.0 : -8.0;
							room.floorZ -= adjust;
							other.floorZ += adjust;
							changed = true;
						}
					}
				}
			}
		}
	}

	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.id < 0) continue;
		for (int j = room.minJ; j <= room.maxJ; j++)
			for (int i = room.minI; i <= room.maxI; i++)
				if (Grid[j][i].roomId == (int)ri)
					Grid[j][i].floorZ = room.floorZ;
	}
}

// ---------------------------------------------------------------------------
// Weapon Placement
// ---------------------------------------------------------------------------

void FProceduralMapGenerator::PlaceWeapons(int W, int H)
{
	static const int EarlyWeapons[] = { 2001, 2002 }; // shotgun, chaingun
	static const int MidWeapons[] = { 82, 2003 };     // SSG, rocket launcher
	static const int LateWeapons[] = { 2004, 2006 };  // plasma rifle, BFG
	static const int AmmoTypes[] = { 2007, 2008, 2010, 2047 };

	(void)W;
	(void)H;

	TArray<int> mainRooms, hubRooms, arenaRooms, keyRooms, lockedRooms, sideRooms, progressionRooms;
	int startRoomIdx = -1;
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.id < 0) continue;
		progressionRooms.Push(ri);
		if (room.hasPlayerStart)
		{
			startRoomIdx = (int)ri;
			continue;
		}

		if (room.onMainPath && !room.hasExit)
			mainRooms.Push(ri);
		if (room.isHub && !room.hasExit)
			hubRooms.Push(ri);
		if (room.isArena && !room.hasExit)
			arenaRooms.Push(ri);
		if (room.hasKey)
			keyRooms.Push(ri);
		if (room.isLocked && !room.hasExit)
			lockedRooms.Push(ri);
		if (!room.onMainPath && !room.hasExit)
			sideRooms.Push(ri);
	}

	auto SortByProgression = [&](TArray<int>& arr)
	{
		for (int i = 0; i < (int)arr.Size(); i++)
		{
			int best = i;
			for (int j = i + 1; j < (int)arr.Size(); j++)
			{
				if (Rooms[arr[j]].progressionRank < Rooms[arr[best]].progressionRank)
					best = j;
			}
			if (best != i)
			{
				int tmp = arr[i];
				arr[i] = arr[best];
				arr[best] = tmp;
			}
		}
	};
	SortByProgression(mainRooms);
	SortByProgression(hubRooms);
	SortByProgression(arenaRooms);
	SortByProgression(keyRooms);
	SortByProgression(lockedRooms);
	SortByProgression(sideRooms);
	SortByProgression(progressionRooms);

	auto GiveWeapon = [&](int roomIndex, int weaponType)
	{
		if (roomIndex < 0) return;
		RoomInfo& room = Rooms[roomIndex];
		if (room.hasWeapon || room.hasExit) return;
		room.hasWeapon = true;
		room.weaponType = weaponType;
	};

	auto AmmoForWeapon = [&](int weaponType) -> int
	{
		if (weaponType == 2001 || weaponType == 82) return 2008;
		if (weaponType == 2002) return 2007;
		if (weaponType == 2003) return 2010;
		if (weaponType == 2004 || weaponType == 2006) return 2047;
		return AmmoTypes[RNG() % countof(AmmoTypes)];
	};

	int rewardBias = Size + Difficulty;
	if (startRoomIdx >= 0)
		GiveWeapon(startRoomIdx, 2001);
	if (mainRooms.Size() > 0)
		GiveWeapon(mainRooms[0], 2001);
	if (hubRooms.Size() > 0)
		GiveWeapon(hubRooms[0], 2002);
	else if (mainRooms.Size() > 1)
		GiveWeapon(mainRooms[1], 2002);
	if (sideRooms.Size() > 0 && rewardBias >= 4)
		GiveWeapon(sideRooms[0], (Rooms[sideRooms[0]].progressionRank <= 2) ? 2001 : 82);
	if (mainRooms.Size() > 2 && rewardBias >= 5)
		GiveWeapon(mainRooms[mainRooms.Size() / 2], 82);
	if (keyRooms.Size() > 0)
		GiveWeapon(keyRooms[0], (Rooms[keyRooms[0]].progressionRank <= 2) ? 2002 : 82);
	if (keyRooms.Size() > 1 && rewardBias >= 6)
		GiveWeapon(keyRooms[keyRooms.Size() - 1], 2003);
	if (lockedRooms.Size() > 0)
		GiveWeapon(lockedRooms[0], (rewardBias >= 5) ? 2003 : 82);
	if (lockedRooms.Size() > 1 && rewardBias >= 6)
		GiveWeapon(lockedRooms[1], 2004);
	else if (arenaRooms.Size() > 1 && rewardBias >= 6)
		GiveWeapon(arenaRooms[arenaRooms.Size() - 1], 2003);
	if (mainRooms.Size() > 2 && rewardBias >= 7)
		GiveWeapon(mainRooms[mainRooms.Size() - 1], 2004);
	if (mainRooms.Size() > 4 && rewardBias >= 8)
		GiveWeapon(mainRooms[mainRooms.Size() - 2], 2003);
	if (sideRooms.Size() > 0 && rewardBias >= 9)
		GiveWeapon(sideRooms[sideRooms.Size() - 1], (Difficulty >= 5 && Size >= 5) ? 2006 : 2004);
	if (lockedRooms.Size() > 0 && rewardBias >= 9)
		GiveWeapon(lockedRooms[lockedRooms.Size() - 1], (Difficulty >= 4) ? 2006 : 2004);
	auto WeaponStage = [&](int weaponType) -> int
	{
		if (weaponType == 2001 || weaponType == 2002) return 2;
		if (weaponType == 82 || weaponType == 2003) return 3;
		if (weaponType == 2004 || weaponType == 2006) return 4;
		return 1;
	};

	int arsenalStage = 1;
	for (unsigned int pi = 0; pi < progressionRooms.Size(); pi++)
	{
		RoomInfo& room = Rooms[progressionRooms[pi]];
		if (room.id < 0) continue;
		if (room.hasPlayerStart)
		{
			arsenalStage = 1;
			continue;
		}

		int effectiveStage = arsenalStage;
		if (room.hasWeapon)
			effectiveStage = std::max(effectiveStage, WeaponStage(room.weaponType));

		int maxTier = clamp(1 + effectiveStage + (room.hasBoss ? 1 : 0), 1, 5);
		room.monsterTier = std::min(room.monsterTier, maxTier);

		int minEnemies = 0;
		if (room.hasBoss) minEnemies = 3 + Difficulty;
		else if (room.hasExit) minEnemies = 2 + Difficulty / 2;
		else if (room.hasKey || room.isLocked || room.isArena || room.isHub) minEnemies = 1 + room.cellCount / 2;
		else if (room.onMainPath) minEnemies = 1 + room.progressionRank / 3;

		int maxAllowed = room.enemyCount;
		if (effectiveStage <= 1)
			maxAllowed = std::min(maxAllowed, room.isArena ? 3 : 2);
		else if (effectiveStage == 2)
			maxAllowed = std::min(maxAllowed, room.isArena ? 5 : 4);
		else if (effectiveStage == 3)
			maxAllowed = std::min(maxAllowed, room.isArena ? 7 : 5);
		else
			maxAllowed = std::min(maxAllowed, room.isArena ? 9 + Difficulty / 2 : 6 + Difficulty / 2);

		if (maxAllowed < minEnemies) maxAllowed = minEnemies;

		room.enemyCount = clamp(room.enemyCount, minEnemies, maxAllowed);

		if (room.hasWeapon)
			arsenalStage = std::max(arsenalStage, WeaponStage(room.weaponType));
	}

	// Place ammo and health according to the encounter pressure in the room.
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.id < 0) continue;

		if (room.hasPlayerStart)
		{
			room.hasArmor = true;
			room.armorType = 2018;
			room.hasHealth = true;
			room.healthType = 2011;
			room.hasAmmo = true;
			room.ammoType = 2008;
			continue;
		}
		if (room.hasBoss || (room.hasExit && Difficulty >= 4))
		{
			room.hasArmor = true;
			room.armorType = 2019;
		}
		else if (room.hasKey || room.isLocked || room.isArena || room.isHub)
		{
			room.hasArmor = true;
			room.armorType = ArmorDrops[(room.progressionRank + Difficulty) % countof(ArmorDrops)];
		}

		if (room.hasExit)
		{
			room.hasAmmo = true;
			room.ammoType = AmmoTypes[(Difficulty + room.progressionRank) % countof(AmmoTypes)];
			room.hasHealth = true;
			room.healthType = 2012;
			if (!room.hasWeapon && rewardBias >= 6)
			{
				room.hasWeapon = true;
				room.weaponType = (Difficulty >= 4) ? 2004 : 2003;
			}
			continue;
		}

		bool heavyFight = room.enemyCount >= (3 + Difficulty / 2) || room.isArena || room.isHub || room.hasBoss;
		bool rewardRoom = room.hasKey || room.isLocked || room.hasWeapon;

		if (room.hasWeapon)
		{
			room.hasAmmo = true;
			room.ammoType = AmmoForWeapon(room.weaponType);
			if (room.hasKey || room.isLocked || heavyFight)
			{
				room.hasHealth = true;
				room.healthType = HealthDrops[RNG() % countof(HealthDrops)];
			}
			if (room.onMainPath && room.progressionRank >= 4)
			{
				room.hasArmor = true;
				room.armorType = ArmorDrops[(room.progressionRank + Difficulty) % countof(ArmorDrops)];
			}
			continue;
		}

		if (rewardRoom || heavyFight || room.branchDepth >= 2)
		{
			room.hasAmmo = true;
			room.ammoType = AmmoTypes[(room.monsterTier + Difficulty + room.progressionRank) % countof(AmmoTypes)];
		}
		else if (room.onMainPath && room.progressionRank >= 4)
		{
			room.hasAmmo = true;
			room.ammoType = AmmoTypes[(room.progressionRank + Difficulty) % countof(AmmoTypes)];
		}

		if (rewardRoom || heavyFight || room.hasDoor)
		{
			room.hasHealth = true;
			room.healthType = HealthDrops[RNG() % countof(HealthDrops)];
		}
		else if (room.onMainPath && room.progressionRank >= 4)
		{
			room.hasHealth = true;
			room.healthType = HealthDrops[(room.progressionRank + Difficulty) % countof(HealthDrops)];
		}
		else if (!room.hasAmmo && !room.hasHealth && !room.onMainPath && (RNG() % 3) == 0)
		{
			room.hasAmmo = true;
			room.ammoType = AmmoTypes[RNG() % countof(AmmoTypes)];
		}

		if (!room.hasArmor && room.hasWeapon && (!room.onMainPath || room.isLocked || room.hasKey))
		{
			room.hasArmor = true;
			room.armorType = ArmorDrops[(room.progressionRank + room.monsterTier) % countof(ArmorDrops)];
		}
		if (!room.hasArmor && room.onMainPath && room.progressionRank >= 5 &&
			(room.hasDoor || heavyFight || room.isArena))
		{
			room.hasArmor = true;
			room.armorType = ArmorDrops[(room.progressionRank + Difficulty + 1) % countof(ArmorDrops)];
		}
	}

	// Propagate weapon/ammo flags to cells
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.id < 0) continue;
		for (int j = room.minJ; j <= room.maxJ; j++)
		{
			for (int i = room.minI; i <= room.maxI; i++)
			{
				if (Grid[j][i].roomId == (int)ri)
				{
					if (room.hasWeapon)
					{
						Grid[j][i].hasWeapon = true;
						Grid[j][i].weaponType = room.weaponType;
					}
					if (room.hasAmmo)
					{
						Grid[j][i].hasAmmo = true;
						Grid[j][i].ammoType = room.ammoType;
					}
					if (room.hasHealth)
					{
						Grid[j][i].hasHealth = true;
						Grid[j][i].healthType = room.healthType;
					}
					if (room.hasArmor)
					{
						Grid[j][i].hasArmor = true;
						Grid[j][i].armorType = room.armorType;
					}
				}
			}
		}
	}
}
