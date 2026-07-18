/*
** procgen_udmf.cpp
**
** Robust UDMF emitter for procedural maps. Chambers are inset from the coarse
** grid and joined by explicit corridor sectors. This guarantees that every
** exposed wall is a closed, one-sided boundary and avoids ambiguous two-sided
** "solid" walls between unrelated sectors.
**
**---------------------------------------------------------------------------
*/

#include "procgen_internal.h"
#include "gamedata/gi.h"

using namespace ProcGen;

namespace
{
	struct BuildVertex
	{
		double x = 0.0;
		double y = 0.0;
	};

	struct BuildSector
	{
		double floorZ = 0.0;
		double ceilZ = 128.0;
		FString floorTex;
		FString ceilTex;
		int light = 160;
		int lightColor = 0xffffff;
		int fadeColor = 0;
		int special = 0;
		int id = 0;
		int damageAmount = 0;
		int damageInterval = 32;
		int leakiness = 0;
		FString damageType;
		bool damageTerrainEffect = false;
	};

	struct BuildSide
	{
		int sector = -1;
		FString top;
		FString middle;
		FString bottom;
		int offsetX = 0;
		int offsetY = 0;
		double scaleYTop = 1.0;
		double scaleXMid = 1.0;
		double scaleYMid = 1.0;
	};

	struct BuildLine
	{
		int v1 = -1;
		int v2 = -1;
		int sideFront = -1;
		int sideBack = -1;
		bool blocking = false;
		bool dontPegTop = false;
		bool dontPegBottom = false;
		bool playerUse = false;
		bool playerCross = false;
		bool repeatSpecial = false;
		bool blockMonsters = false;
		bool secret = false;
		int special = 0;
		int lockNumber = 0;
		int args[5] = { 0, 0, 0, 0, 0 };
	};

	struct BuildThing
	{
		double x = 0.0;
		double y = 0.0;
		int type = 0;
		int angle = 0;
		bool ambush = false;
	};

	struct ConnectionRef
	{
		int sector = -1;
		int doorSector = -1;
		int stairIndex = -1;
		double halfWidth = 48.0;
		bool door = false;
		bool secret = false;
		int lockType = 0;
		double doorHeight = 128.0;
		FString doorTexture;
		int doorTextureWidth = 128;
		int doorTextureHeight = 128;
	};

	struct DoorProfile
	{
		const char* texture = "BIGDOOR1";
		int width = 128;
		int height = 96;
	};

	struct CellConnections
	{
		ConnectionRef refs[4];
	};

	struct StairConnection
	{
		// Ordered from the DIR_E / DIR_S source cell toward its neighbor.
		TArray<int> sectors;
	};

	struct CellEdgeExtents
	{
		double edge[4] = { 0.0, 0.0, 0.0, 0.0 };
	};

	struct RevealProfile
	{
		double outerX = 0.0;
		double outerY = 0.0;
		double innerX = 0.0;
		double innerY = 0.0;
		double outerChamfer = 0.0;
		double innerChamfer = 0.0;
		double offsetX = 0.0;
		double offsetY = 0.0;
		double floorDelta = 0.0;
		double ceilingDrop = 0.0;
		int variant = 0;
	};

	enum RevealArchitecture
	{
		RevealPavilion,
		RevealWallAlcove,
		RevealFalseWall,
	};

	enum RevealCue
	{
		RevealHidden,
		RevealSubtle,
		RevealProminent,
	};

	enum FluidKind
	{
		FluidWater,
		FluidBlood,
		FluidNukage,
		FluidLava,
	};

	static const char* SafeTexture(const FString& texture, const char* fallback)
	{
		return texture.IsEmpty() ? fallback : texture.GetChars();
	}
}

bool FProceduralMapGenerator::BuildUDMF(int W, int H)
{
	static const double CELL_HALF = CELL_SIZE * 0.5;
	static const double WALL_INSET = 24.0;
	static const double ROOM_HALF = CELL_HALF - WALL_INSET;
	const ThemeStyle themeStyle = GetThemeStyle(Theme);
	// Keep descriptor selection stable for signed seeds, including INT_MIN,
	// without invoking the undefined overflow of abs(INT_MIN).
	const uint32_t variantSeed = Seed < 0 ? 0u - (uint32_t)Seed : (uint32_t)Seed;
	const int variantSeedMod3 = (int)(variantSeed % 3u);
	const int variantSeedThirdMod3 = (int)((variantSeed / 3u) % 3u);
	const bool infernalArchitecture = UsesInfernalArchitecture(themeStyle);

	TArray<BuildVertex> vertices;
	TArray<BuildSector> sectors;
	TArray<BuildSide> sides;
	TArray<BuildLine> lines;
	TArray<BuildThing> things;
	TMap<uint64_t, int> vertexLookup;
	TMap<uint64_t, int> solidWallLookup;

	auto AddVertex = [&](double x, double y) -> int
	{
		const int32_t quantizedX = (int32_t)llround(x * 1000.0);
		const int32_t quantizedY = (int32_t)llround(y * 1000.0);
		const uint64_t key = (uint64_t)(uint32_t)quantizedX << 32 |
			(uint64_t)(uint32_t)quantizedY;
		if (int* existing = vertexLookup.CheckKey(key)) return *existing;
		BuildVertex vertex;
		vertex.x = x;
		vertex.y = y;
		vertices.Push(vertex);
		const int index = vertices.Size() - 1;
		vertexLookup.Insert(key, index);
		return index;
	};

	auto AddSector = [&](double floorZ, double ceilZ, const char* floorTex,
		const char* ceilTex, int light, int id = 0) -> int
	{
		BuildSector sector;
		sector.floorZ = floorZ;
		sector.ceilZ = ceilZ;
		sector.floorTex = floorTex;
		sector.ceilTex = ceilTex;
		sector.light = clamp(light, 160, 224);
		sector.id = id;
		sectors.Push(sector);
		return sectors.Size() - 1;
	};

	auto AddSide = [&](int sector, const char* top, const char* middle,
		const char* bottom) -> int
	{
		BuildSide side;
		side.sector = sector;
		side.top = (top && top[0]) ? top : "-";
		side.middle = (middle && middle[0]) ? middle : "-";
		side.bottom = (bottom && bottom[0]) ? bottom : "-";
		sides.Push(side);
		return sides.Size() - 1;
	};

	auto CenteredTextureOffset = [&](double x1, double y1, double x2, double y2) -> int
	{
		// Stock wall motifs are predominantly 128 units wide. Centering that phase
		// on every architectural segment makes opposite walls and all four
		// chamfers agree regardless of world position or linedef direction.
		const double length = hypot(x2 - x1, y2 - y1);
		return (int)lround((128.0 - length) * 0.5);
	};

	auto AddLine = [&](double x1, double y1, double x2, double y2,
		int frontSector, int backSector,
		const char* frontTop, const char* frontMiddle, const char* frontBottom,
		const char* backTop, const char* backMiddle, const char* backBottom,
		bool blocking, int special, int lockNumber,
		int arg0, int arg1, int arg2, int arg3, int arg4,
		bool playerUse, bool playerCross, bool repeatSpecial,
		bool dontPegTop = false, bool dontPegBottom = false,
		bool blockMonsters = false) -> int
	{
		if (fabs(x1 - x2) < 0.001 && fabs(y1 - y2) < 0.001) return -1;

		BuildLine line;
		line.v1 = AddVertex(x1, y1);
		line.v2 = AddVertex(x2, y2);
		line.sideFront = AddSide(frontSector, frontTop, frontMiddle, frontBottom);
		sides[line.sideFront].offsetX = CenteredTextureOffset(x1, y1, x2, y2);
		if (backSector >= 0)
		{
			line.sideBack = AddSide(backSector, backTop, backMiddle, backBottom);
			sides[line.sideBack].offsetX = CenteredTextureOffset(x2, y2, x1, y1);
		}
		line.blocking = blocking || backSector < 0;
		line.special = special;
		line.lockNumber = lockNumber;
		line.args[0] = arg0;
		line.args[1] = arg1;
		line.args[2] = arg2;
		line.args[3] = arg3;
		line.args[4] = arg4;
		line.playerUse = playerUse;
		line.playerCross = playerCross;
		line.repeatSpecial = repeatSpecial;
		line.blockMonsters = blockMonsters;
		line.dontPegTop = dontPegTop;
		line.dontPegBottom = dontPegBottom;
		lines.Push(line);
		return lines.Size() - 1;
	};

	auto AddWall = [&](double x1, double y1, double x2, double y2,
		int sector, const char* texture) -> int
	{
		const int firstVertex = AddVertex(x1, y1);
		const int secondVertex = AddVertex(x2, y2);
		const uint32_t lowVertex = (uint32_t)std::min(firstVertex, secondVertex);
		const uint32_t highVertex = (uint32_t)std::max(firstVertex, secondVertex);
		const uint64_t wallKey = (uint64_t)lowVertex << 32 | highVertex;
		if (int* existingIndex = solidWallLookup.CheckKey(wallKey))
		{
			const int matchedLine = *existingIndex;
			BuildLine& existing = lines[matchedLine];
			const int existingSector = sides[existing.sideFront].sector;
			if (existing.sideBack < 0 && existingSector == sector)
			{
				if (existing.v1 == secondVertex && existing.v2 == firstVertex)
				{
					// Two opposite solid faces from the same sector describe an
					// internal chamber/corridor seam, not a wall. Serialize one open
					// two-sided line so the BSP receives an unambiguous partition.
					existing.sideBack = AddSide(sector, nullptr, nullptr, nullptr);
					sides[existing.sideFront].top = "-";
					sides[existing.sideFront].middle = "-";
					sides[existing.sideFront].bottom = "-";
					existing.blocking = false;
					existing.dontPegBottom = false;
					solidWallLookup.Remove(wallKey);
				}
				return matchedLine;
			}
		}
		int lineIndex = AddLine(x1, y1, x2, y2, sector, -1,
			nullptr, texture, nullptr, nullptr, nullptr, nullptr,
			true, 0, 0, 0, 0, 0, 0, 0, false, false, false, false, true);
		if (lineIndex >= 0)
		{
			solidWallLookup.Insert(wallKey, lineIndex);
			int sideIndex = lines[lineIndex].sideFront;
			sides[sideIndex].offsetY = -(int)lround(sectors[sector].floorZ);
		}
		return lineIndex;
	};

	auto AddSwitchWall = [&](double x1, double y1, double x2, double y2,
		int sector, int targetTag) -> int
	{
		const char* texture = infernalArchitecture ? "SW1GARG" : "SW1COMP";
		int lineIndex = AddLine(x1, y1, x2, y2, sector, -1,
			nullptr, texture, nullptr, nullptr, nullptr, nullptr,
			true, 11, 0, targetTag, 16, 0, 0, 0,
			true, false, false, false, true);
		if (lineIndex >= 0)
		{
			BuildSide& side = sides[lines[lineIndex].sideFront];
			const double wallHeight = std::max(1.0,
				sectors[sector].ceilZ - sectors[sector].floorZ);
			// Both switch textures are 64x128 in Ultimate Doom and Doom II. The
			// panel line is exactly 64 units wide; fitting its vertical scale to the
			// wall height guarantees one motif in each axis with no tiling.
			side.offsetX = 0;
			side.offsetY = 0;
			side.scaleXMid = 1.0;
			side.scaleYMid = 128.0 / wallHeight;
		}
		return lineIndex;
	};

	auto AddThing = [&](double x, double y, int type, int angle = 0, bool ambush = false)
	{
		BuildThing thing;
		thing.x = x;
		thing.y = y;
		thing.type = type;
		thing.angle = angle;
		thing.ambush = ambush;
		things.Push(thing);
	};

	auto IsValidRoom = [&](int roomId) -> bool
	{
		return roomId >= 0 && roomId < (int)Rooms.Size() && Rooms[roomId].id >= 0;
	};
	int layoutMinX = W;
	int layoutMaxX = -1;
	int layoutMinY = H;
	int layoutMaxY = -1;
	for (int y = 0; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			if (!Grid[y][x].present) continue;
			layoutMinX = std::min(layoutMinX, x);
			layoutMaxX = std::max(layoutMaxX, x);
			layoutMinY = std::min(layoutMinY, y);
			layoutMaxY = std::max(layoutMaxY, y);
		}
	}
	const double layoutCenterX = layoutMaxX >= layoutMinX ?
		(layoutMinX + layoutMaxX + 1) * 0.5 : W * 0.5;
	const double layoutCenterY = layoutMaxY >= layoutMinY ?
		(layoutMinY + layoutMaxY + 1) * 0.5 : H * 0.5;

	auto CellCenterX = [&](int x) -> double
	{
		return ((x + 0.5) - layoutCenterX) * CELL_SIZE;
	};
	auto CellCenterY = [&](int y) -> double
	{
		return ((y + 0.5) - layoutCenterY) * CELL_SIZE;
	};

	// Room profiles are assigned during the coherence pass. Cells in the same
	// room share a profile so their joins remain exact, while separate rooms can
	// vary substantially in width, depth, corner treatment, and vertical scale.
	TArray<double> roomHalfX;
	TArray<double> roomHalfY;
	roomHalfX.Resize(Rooms.Size());
	roomHalfY.Resize(Rooms.Size());
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		const RoomInfo& room = Rooms[ri];
		roomHalfX[ri] = clamp(room.halfWidth, 72.0, CELL_HALF - 8.0);
		roomHalfY[ri] = clamp(room.halfHeight, 72.0, CELL_HALF - 8.0);
	}

	auto HalfXForCell = [&](int x, int y) -> double
	{
		int room = Grid[y][x].roomId;
		return IsValidRoom(room) ? roomHalfX[room] : ROOM_HALF;
	};
	auto HalfYForCell = [&](int x, int y) -> double
	{
		int room = Grid[y][x].roomId;
		return IsValidRoom(room) ? roomHalfY[room] : ROOM_HALF;
	};

	// Secret rewards are selected after the general elevation pass. They use a
	// conventional hidden door and are deliberately dead ends, so inherit the
	// sole neighboring terrace rather than placing an impassable step under the
	// moving slab.
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& secret = Rooms[ri];
		if (!secret.isSecret || !secret.isDeadEnd) continue;
		bool aligned = false;
		for (int y = 0; y < H && !aligned; y++)
		{
			for (int x = 0; x < W && !aligned; x++)
			{
				if (!Grid[y][x].present || Grid[y][x].roomId != (int)ri) continue;
				for (int direction = 0; direction < 4; direction++)
				{
					if (!Grid[y][x].conn[direction]) continue;
					const int nx = x + DX[direction];
					const int ny = y + DY[direction];
					if (nx < 0 || nx >= W || ny < 0 || ny >= H ||
						!Grid[ny][nx].present) continue;
					const int neighborId = Grid[ny][nx].roomId;
					if (!IsValidRoom(neighborId) || neighborId == (int)ri) continue;
					const double clearHeight = secret.ceilZ - secret.floorZ;
					secret.floorZ = Rooms[neighborId].floorZ;
					secret.ceilZ = secret.floorZ + clearHeight;
					aligned = true;
					break;
				}
			}
		}
	}

	// Directional extents shorten only the two chamber faces involved in a
	// vertical transition. That creates a 112+ unit connector for distinct stair
	// treads without shrinking unrelated sides of the same chamber.
	TArray<TArray<CellEdgeExtents>> cellEdges;
	cellEdges.Resize(H);
	for (int y = 0; y < H; y++)
	{
		cellEdges[y].Resize(W);
		for (int x = 0; x < W; x++)
		{
			cellEdges[y][x].edge[DIR_N] = HalfYForCell(x, y);
			cellEdges[y][x].edge[DIR_S] = HalfYForCell(x, y);
			cellEdges[y][x].edge[DIR_W] = HalfXForCell(x, y);
			cellEdges[y][x].edge[DIR_E] = HalfXForCell(x, y);
		}
	}
	static const double STAIR_ROOM_EDGE = 136.0;
	for (int y = 0; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			if (!Grid[y][x].present) continue;
			for (int direction : { DIR_E, DIR_S })
			{
				if (!Grid[y][x].conn[direction]) continue;
				const int nx = x + DX[direction];
				const int ny = y + DY[direction];
				if (nx < 0 || nx >= W || ny < 0 || ny >= H ||
					!Grid[ny][nx].present) continue;
				const int roomA = Grid[y][x].roomId;
				const int roomB = Grid[ny][nx].roomId;
				if (!IsValidRoom(roomA) || !IsValidRoom(roomB) || roomA == roomB ||
					fabs(Rooms[roomA].floorZ - Rooms[roomB].floorZ) < 0.001)
					continue;
				cellEdges[y][x].edge[direction] = std::min(
					cellEdges[y][x].edge[direction], STAIR_ROOM_EDGE);
				cellEdges[ny][nx].edge[OPP[direction]] = std::min(
					cellEdges[ny][nx].edge[OPP[direction]], STAIR_ROOM_EDGE);
			}
		}
	}
	auto EdgeForCell = [&](int x, int y, int direction) -> double
	{
		return cellEdges[y][x].edge[direction];
	};
	auto CellHasHeightTransition = [&](int x, int y) -> bool
	{
		if (!Grid[y][x].present || !IsValidRoom(Grid[y][x].roomId)) return false;
		const int roomId = Grid[y][x].roomId;
		for (int direction = 0; direction < 4; direction++)
		{
			if (!Grid[y][x].conn[direction]) continue;
			const int nx = x + DX[direction];
			const int ny = y + DY[direction];
			if (nx < 0 || nx >= W || ny < 0 || ny >= H ||
				!Grid[ny][nx].present || !IsValidRoom(Grid[ny][nx].roomId)) continue;
			const int neighborId = Grid[ny][nx].roomId;
			if (neighborId != roomId &&
				fabs(Rooms[neighborId].floorZ - Rooms[roomId].floorZ) >= 0.001)
				return true;
		}
		return false;
	};

	// Expose several landmarks to the sky. Every map receives both an outdoor
	// finale and at least one additional open combat space; colossal maps can
	// alternate indoor routes with a much broader courtyard cadence.
	TArray<bool> outdoorRooms;
	outdoorRooms.Resize(Rooms.Size());
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++) outdoorRooms[ri] = false;
	int outdoorBudget = Outdoors == 0 ? 1 + Size / 12 :
		(Outdoors == 2 ? 3 + Size : 2 + Size / 2);
	if (themeStyle == ThemeHell) outdoorBudget += 1 + Size / 5;
	else if (themeStyle == ThemeGothic) outdoorBudget += 1 + Size / 8;
	else if (themeStyle == ThemeIndustrial)
		outdoorBudget = std::max(1, outdoorBudget - std::max(1, Size / 5));
	else if (themeStyle == ThemeCorrupted) outdoorBudget += Size / 8;
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		if (Rooms[ri].hasExit)
		{
			outdoorRooms[ri] = true;
			outdoorBudget--;
		}
	}
	for (int pass = 0; pass < 3 && outdoorBudget > 0; pass++)
	{
		for (unsigned int ri = 0; ri < Rooms.Size() && outdoorBudget > 0; ri++)
		{
			const RoomInfo& room = Rooms[ri];
			if (outdoorRooms[ri] || room.hasPlayerStart || room.hasKey || room.isLocked) continue;
			bool candidate = pass == 0 ? room.isArena :
				(pass == 1 ? room.isHub : (room.onMainPath && room.cellCount >= 3));
			if (!candidate || room.cellCount < 2) continue;
			outdoorRooms[ri] = true;
			outdoorBudget--;
		}
	}

	enum RevealKind
	{
		RevealNone,
		RevealKeyTrap,
		RevealSwitchCache,
	};
	TArray<int> revealKinds;
	TArray<int> revealTags;
	TArray<int> revealBorderTypes;
	TArray<int> revealCellX;
	TArray<int> revealCellY;
	TArray<int> revealDoorSides;
	TArray<double> revealProfileAdjustX;
	TArray<double> revealProfileAdjustY;
	TArray<int> revealVariants;
	TArray<int> revealArchitectures;
	TArray<int> revealCues;
	TArray<int> revealWallLineIndices;
	TArray<int> keyTriggerTags;
	TArray<int> switchTargetTags;
	TArray<int> perchTags;
	TArray<int> perchCellX;
	TArray<int> perchCellY;
	TArray<int> perchApproachSides;
	TArray<int> perchVariants;
	TArray<int> liftTags;
	TArray<int> liftCellX;
	TArray<int> liftCellY;
	TArray<int> fluidKinds;
	TArray<int> fluidVariants;
	TArray<int> fluidCellX;
	TArray<int> fluidCellY;
	TArray<bool> falseWallNeighborReserved;
	revealKinds.Resize(Rooms.Size());
	revealTags.Resize(Rooms.Size());
	revealBorderTypes.Resize(Rooms.Size());
	revealCellX.Resize(Rooms.Size());
	revealCellY.Resize(Rooms.Size());
	revealDoorSides.Resize(Rooms.Size());
	revealProfileAdjustX.Resize(Rooms.Size());
	revealProfileAdjustY.Resize(Rooms.Size());
	revealVariants.Resize(Rooms.Size());
	revealArchitectures.Resize(Rooms.Size());
	revealCues.Resize(Rooms.Size());
	revealWallLineIndices.Resize(Rooms.Size());
	keyTriggerTags.Resize(Rooms.Size());
	switchTargetTags.Resize(Rooms.Size());
	perchTags.Resize(Rooms.Size());
	perchCellX.Resize(Rooms.Size());
	perchCellY.Resize(Rooms.Size());
	perchApproachSides.Resize(Rooms.Size());
	perchVariants.Resize(Rooms.Size());
	liftTags.Resize(Rooms.Size());
	liftCellX.Resize(Rooms.Size());
	liftCellY.Resize(Rooms.Size());
	fluidKinds.Resize(Rooms.Size());
	fluidVariants.Resize(Rooms.Size());
	fluidCellX.Resize(Rooms.Size());
	fluidCellY.Resize(Rooms.Size());
	falseWallNeighborReserved.Resize(W * H);
	for (int index = 0; index < W * H; index++) falseWallNeighborReserved[index] = false;
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		revealKinds[ri] = RevealNone;
		revealTags[ri] = 0;
		revealBorderTypes[ri] = 0;
		revealCellX[ri] = revealCellY[ri] = -1;
		revealDoorSides[ri] = -1;
		revealProfileAdjustX[ri] = revealProfileAdjustY[ri] = 0.0;
		revealVariants[ri] = -1;
		revealArchitectures[ri] = RevealPavilion;
		revealCues[ri] = RevealProminent;
		revealWallLineIndices[ri] = -1;
		keyTriggerTags[ri] = 0;
		switchTargetTags[ri] = 0;
		perchTags[ri] = 0;
		perchCellX[ri] = perchCellY[ri] = -1;
		perchApproachSides[ri] = -1;
		perchVariants[ri] = 0;
		liftTags[ri] = 0;
		liftCellX[ri] = liftCellY[ri] = -1;
		fluidKinds[ri] = FluidWater;
		fluidVariants[ri] = -1;
		fluidCellX[ri] = fluidCellY[ri] = -1;
	}
	constexpr double RevealClearance = 64.0;
	auto BuildRevealProfile = [&](int roomId, int revealKind) -> RevealProfile
	{
		static const double DesiredX[] = { 84.0, 92.0, 88.0, 96.0 };
		static const double DesiredY[] = { 88.0, 84.0, 96.0, 92.0 };
		static const double MoatWidth[] = { 22.0, 24.0, 26.0, 24.0 };
		static const double Chamfer[] = { 16.0, 20.0, 24.0, 28.0 };
		const RoomInfo& room = Rooms[roomId];
		const int style = abs(room.id * 17 + room.visualVariant * 11 +
			room.progressionRank * 5 + revealKind * 7) % countof(DesiredX);
		const double roleGrowth = revealKind == RevealSwitchCache ? 6.0 : -2.0;
		const double maxOuterX = roomHalfX[roomId] - RevealClearance;
		const double maxOuterY = roomHalfY[roomId] - RevealClearance;

		RevealProfile profile;
		profile.outerX = std::min(DesiredX[style] + roleGrowth, maxOuterX) +
			revealProfileAdjustX[roomId];
		profile.outerY = std::min(DesiredY[style] + roleGrowth, maxOuterY) +
			revealProfileAdjustY[roomId];
		const double moat = MoatWidth[style];
		profile.innerX = profile.outerX - moat;
		profile.innerY = profile.outerY - moat;
		profile.outerChamfer = std::min(Chamfer[style],
			std::min(profile.outerX, profile.outerY) - 32.0);
		profile.innerChamfer = std::min(std::max(8.0, profile.outerChamfer - 6.0),
			std::min(profile.innerX, profile.innerY) - 32.0);
		if (revealArchitectures[roomId] == RevealWallAlcove)
		{
			// A wall bank reads as a deliberately constructed rectangular annex,
			// rather than another copy of the clipped freestanding pavilion.
			profile.outerChamfer = 6.0;
			profile.innerChamfer = 6.0;
		}

		// Use only the clearance beyond the 64-unit traversal contract for a
		// subtle off-center placement. The diagonal budget prevents that offset
		// from squeezing the room's clipped corners.
		double offsetX = std::min(24.0,
			floor(std::max(0.0, maxOuterX - profile.outerX) / 4.0) * 4.0);
		double offsetY = std::min(24.0,
			floor(std::max(0.0, maxOuterY - profile.outerY) / 4.0) * 4.0);
		const double diagonalBudget = floor(std::max(0.0,
			roomHalfX[roomId] + roomHalfY[roomId] - room.cornerCut -
			profile.outerX - profile.outerY - RevealClearance * sqrt(2.0)) / 4.0) * 4.0;
		if (offsetX + offsetY > diagonalBudget)
		{
			offsetY = std::max(0.0, diagonalBudget - offsetX);
			if (offsetX + offsetY > diagonalBudget)
				offsetX = std::max(0.0, diagonalBudget - offsetY);
		}
		const int offsetHash = abs(room.id * 29 + room.visualVariant * 13 + revealKind * 3);
		profile.offsetX = (offsetHash & 1) ? offsetX : -offsetX;
		profile.offsetY = (offsetHash & 2) ? offsetY : -offsetY;
		profile.variant = revealVariants[roomId] >= 0 ? revealVariants[roomId] : style;
		static const double KeyFloorDelta[] = { 0.0, -8.0, 8.0, 0.0 };
		static const double CacheFloorDelta[] = { 8.0, 0.0, 16.0, -8.0 };
		static const double CeilingDrop[] = { 0.0, 16.0, 24.0, 8.0 };
		profile.floorDelta = revealKind == RevealKeyTrap ?
			KeyFloorDelta[profile.variant % countof(KeyFloorDelta)] :
			CacheFloorDelta[profile.variant % countof(CacheFloorDelta)];
		profile.ceilingDrop = CeilingDrop[profile.variant % countof(CeilingDrop)];
		return profile;
	};
	auto CanHostReveal = [&](int roomId, int revealKind) -> bool
	{
		if (!IsValidRoom(roomId) || Rooms[roomId].cellCount < 2) return false;
		const RoomInfo& room = Rooms[roomId];
		const RevealProfile profile = BuildRevealProfile(roomId, revealKind);
		if (profile.innerX < 56.0 || profile.innerY < 56.0 ||
			profile.outerChamfer < 4.0 || profile.innerChamfer < 4.0)
			return false;
		const double sideClearanceX = roomHalfX[roomId] - profile.outerX - fabs(profile.offsetX);
		const double sideClearanceY = roomHalfY[roomId] - profile.outerY - fabs(profile.offsetY);
		const double chamferClearance =
			(roomHalfX[roomId] + roomHalfY[roomId] - room.cornerCut -
				profile.outerX - profile.outerY - fabs(profile.offsetX) -
				fabs(profile.offsetY)) / sqrt(2.0);
		return sideClearanceX >= RevealClearance &&
			sideClearanceY >= RevealClearance &&
			chamferClearance >= RevealClearance;
	};
	auto ChooseRevealDoorSide = [&](int roomId, int featureX, int featureY,
		int revealKind) -> int
	{
		// Side order is south, east, north, west. Prefer an edge that faces into
		// another cell of the composed room, producing an open approach instead of
		// pointing the door at the nearest perimeter wall.
		static const int DoorSideForGridDirection[4] = { 0, 2, 3, 1 };
		TArray<int> preferred;
		for (int direction = 0; direction < 4; direction++)
		{
			const int nx = featureX + DX[direction];
			const int ny = featureY + DY[direction];
			if (nx >= 0 && nx < W && ny >= 0 && ny < H &&
				Grid[ny][nx].present && Grid[ny][nx].roomId == roomId)
				preferred.Push(DoorSideForGridDirection[direction]);
		}
		if (preferred.Size() == 0)
		{
			for (int direction = 0; direction < 4; direction++)
				if (Grid[featureY][featureX].conn[direction])
					preferred.Push(DoorSideForGridDirection[direction]);
		}
		const int style = abs(Rooms[roomId].id * 19 + Rooms[roomId].visualVariant * 7 +
			revealKind * 5);
		return preferred.Size() > 0 ? preferred[style % preferred.Size()] : style % 4;
	};
	auto IsLandmarkAnchorCell = [&](int roomId, int x, int y) -> bool
	{
		const RoomInfo& room = Rooms[roomId];
		if (Grid[y][x].hasPlayerStart || Grid[y][x].hasKey || Grid[y][x].hasExit)
			return true;
		if (!room.isArena && !room.isHub) return false;
		for (int candidateY = room.minJ; candidateY <= room.maxJ; candidateY++)
		{
			for (int candidateX = room.minI; candidateX <= room.maxI; candidateX++)
			{
				if (candidateX >= 0 && candidateX < W && candidateY >= 0 && candidateY < H &&
					Grid[candidateY][candidateX].present &&
					Grid[candidateY][candidateX].roomId == roomId)
					return candidateX == x && candidateY == y;
			}
		}
		return false;
	};

	auto PickFeatureCell = [&](int roomId, int revealKind,
		int& featureX, int& featureY) -> bool
	{
		if (!CanHostReveal(roomId, revealKind)) return false;
		for (int pass = 0; pass < 2; pass++)
		{
			TArray<std::pair<int, int>> candidates;
			for (int y = 0; y < H; y++)
			{
				for (int x = 0; x < W; x++)
				{
					const ProcGenCell& cell = Grid[y][x];
					if (!cell.present || cell.roomId != roomId) continue;
					if (cell.hasPlayerStart || cell.hasKey || cell.hasExit || cell.hasBoss || cell.isLocked)
						continue;
					if (pass == 0 && CellHasHeightTransition(x, y)) continue;
					if (IsLandmarkAnchorCell(roomId, x, y)) continue;
					candidates.Push(std::make_pair(x, y));
				}
			}
			if (candidates.Size() == 0) continue;
			const auto& selected = candidates[RNG() % candidates.Size()];
			featureX = selected.first;
			featureY = selected.second;
			return true;
		}
		return false;
	};
	static const int WallSideForGridDirection[4] = { 0, 2, 3, 1 };
	static const int OppositeGridDirection[4] = { DIR_S, DIR_N, DIR_E, DIR_W };
	auto PickWallAlcoveCell = [&](int roomId, int& featureX, int& featureY,
		int& doorSide) -> bool
	{
		struct WallAlcoveCandidate
		{
			int x;
			int y;
			int side;
		};
		for (int pass = 0; pass < 2; pass++)
		{
			TArray<WallAlcoveCandidate> candidates;
			for (int y = 0; y < H; y++)
			{
				for (int x = 0; x < W; x++)
				{
					const ProcGenCell& cell = Grid[y][x];
					if (!cell.present || cell.roomId != roomId || cell.hasPlayerStart ||
						cell.hasKey || cell.hasExit || cell.hasBoss || cell.isLocked ||
						CellHasHeightTransition(x, y) || IsLandmarkAnchorCell(roomId, x, y))
						continue;
					for (int backDirection = 0; backDirection < 4; backDirection++)
					{
						if (cell.conn[backDirection]) continue;
						const int bx = x + DX[backDirection];
						const int by = y + DY[backDirection];
						if (bx >= 0 && bx < W && by >= 0 && by < H && Grid[by][bx].present)
							continue;
						const int frontDirection = OppositeGridDirection[backDirection];
						const int nx = x + DX[frontDirection];
						const int ny = y + DY[frontDirection];
						const bool facesRoom = nx >= 0 && nx < W && ny >= 0 && ny < H &&
							Grid[ny][nx].present && Grid[ny][nx].roomId == roomId;
						if (pass == 0 && !facesRoom) continue;
						const double span = backDirection == DIR_N || backDirection == DIR_S ?
							roomHalfX[roomId] * 2.0 : roomHalfY[roomId] * 2.0;
						if (span - Rooms[roomId].cornerCut * 2.0 < 208.0) continue;
						const int backSide = WallSideForGridDirection[backDirection];
						candidates.Push({ x, y, (backSide + 2) % 4 });
					}
				}
			}
			if (candidates.Size() == 0) continue;
			const WallAlcoveCandidate& selected = candidates[RNG() % candidates.Size()];
			featureX = selected.x;
			featureY = selected.y;
			doorSide = selected.side;
			return true;
		}
		return false;
	};
	auto PickFalseWallCell = [&](int roomId, int& featureX, int& featureY,
		int& doorSide, int& neighborX, int& neighborY) -> bool
	{
		struct FalseWallCandidate
		{
			int x;
			int y;
			int side;
			int neighborX;
			int neighborY;
		};
		for (int pass = 0; pass < 2; pass++)
		{
			TArray<FalseWallCandidate> candidates;
			for (int y = 0; y < H; y++)
			{
				for (int x = 0; x < W; x++)
				{
					const ProcGenCell& cell = Grid[y][x];
					if (!cell.present || cell.roomId != roomId || cell.hasPlayerStart ||
						cell.hasKey || cell.hasExit || cell.hasBoss || cell.isLocked)
						continue;
					if (pass == 0 && CellHasHeightTransition(x, y)) continue;
					if (IsLandmarkAnchorCell(roomId, x, y)) continue;
					for (int direction = 0; direction < 4; direction++)
					{
						if (cell.conn[direction]) continue;
						const int nx = x + DX[direction];
						const int ny = y + DY[direction];
						// The closet grows into a reserved, in-bounds coarse-grid neighbor.
						// This prevents two independently selected false walls from sharing
						// the same void cell or extending beyond the verified layout.
						if (nx < 0 || nx >= W || ny < 0 || ny >= H || Grid[ny][nx].present ||
							falseWallNeighborReserved[ny * W + nx])
							continue;
						const double span = direction == DIR_N || direction == DIR_S ?
							roomHalfX[roomId] * 2.0 : roomHalfY[roomId] * 2.0;
						if (span - Rooms[roomId].cornerCut * 2.0 < 144.0) continue;
						candidates.Push({ x, y, WallSideForGridDirection[direction], nx, ny });
					}
				}
			}
			if (candidates.Size() == 0) continue;
			const FalseWallCandidate& selected = candidates[RNG() % candidates.Size()];
			featureX = selected.x;
			featureY = selected.y;
			doorSide = selected.side;
			neighborX = selected.neighborX;
			neighborY = selected.neighborY;
			return true;
		}
		return false;
	};
	int revealArchitectureOrdinal = variantSeedMod3;
	auto AssignRevealArchitecture = [&](int roomId, int& featureX,
		int& featureY, int revealKind)
	{
		const int ordinal = revealArchitectureOrdinal++;
		int architecture = ordinal % 3;
		int doorSide = -1;
		int neighborX = -1;
		int neighborY = -1;
		if (architecture == RevealFalseWall)
		{
			if (!PickFalseWallCell(roomId, featureX, featureY, doorSide,
				neighborX, neighborY))
			{
				architecture = RevealWallAlcove;
				if (!PickWallAlcoveCell(roomId, featureX, featureY, doorSide))
					architecture = RevealPavilion;
			}
		}
		else if (architecture == RevealWallAlcove &&
			!PickWallAlcoveCell(roomId, featureX, featureY, doorSide))
		{
			architecture = RevealFalseWall;
			if (!PickFalseWallCell(roomId, featureX, featureY, doorSide,
				neighborX, neighborY))
				architecture = RevealPavilion;
		}
		revealArchitectures[roomId] = architecture;
		revealCues[roomId] = (variantSeedThirdMod3 + ordinal + revealKind) % 3;
		if (architecture == RevealWallAlcove || architecture == RevealFalseWall)
		{
			revealDoorSides[roomId] = doorSide;
			if (architecture == RevealFalseWall)
				falseWallNeighborReserved[neighborY * W + neighborX] = true;
		}
	};
	auto PickPerchCell = [&](int roomId, int& featureX, int& featureY) -> bool
	{
		// Prefer an untouched, level cell. Compact maps may legitimately use every
		// such cell for a terrace connector, so a second pass accepts a transition
		// cell: the perch compositor owns that cell and emits its own complete stair
		// sequence, preserving the same traversability contract.
		for (int pass = 0; pass < 2; pass++)
		{
			TArray<std::pair<int, int>> candidates;
			for (int y = 0; y < H; y++)
			{
				for (int x = 0; x < W; x++)
				{
					const ProcGenCell& cell = Grid[y][x];
					if (!cell.present || cell.roomId != roomId) continue;
					if (cell.hasPlayerStart || cell.hasKey || cell.hasExit || cell.hasBoss || cell.isLocked)
						continue;
					if (pass == 0 && CellHasHeightTransition(x, y)) continue;
					if (x == revealCellX[roomId] && y == revealCellY[roomId]) continue;
					if (IsLandmarkAnchorCell(roomId, x, y)) continue;
					candidates.Push(std::make_pair(x, y));
				}
			}
			if (candidates.Size() == 0) continue;
			const auto& selected = candidates[RNG() % candidates.Size()];
			featureX = selected.first;
			featureY = selected.second;
			return true;
		}
		return false;
	};
	auto ChoosePerchApproachSide = [&](int roomId, int featureX, int featureY) -> int
	{
		// Side order is south, east, north, west. A perch is only assigned to a
		// composed room, so point its staircase into another cell of that room and
		// keep the full run away from the local perimeter.
		static const int SideForGridDirection[4] = { 0, 2, 3, 1 };
		TArray<int> preferred;
		for (int direction = 0; direction < 4; direction++)
		{
			const int nx = featureX + DX[direction];
			const int ny = featureY + DY[direction];
			if (nx >= 0 && nx < W && ny >= 0 && ny < H &&
				Grid[ny][nx].present && Grid[ny][nx].roomId == roomId)
				preferred.Push(SideForGridDirection[direction]);
		}
		const int style = abs(Rooms[roomId].id * 23 +
			Rooms[roomId].visualVariant * 11 + featureX * 5 + featureY * 7);
		return preferred.Size() > 0 ? preferred[style % preferred.Size()] : style % 4;
	};

	auto ShuffleRooms = [&](TArray<int>& roomIds)
	{
		for (int i = (int)roomIds.Size() - 1; i > 0; i--)
		{
			const int other = RNG() % (i + 1);
			const int saved = roomIds[i];
			roomIds[i] = roomIds[other];
			roomIds[other] = saved;
		}
	};
	auto RoomsConnected = [&](int firstRoom, int secondRoom) -> bool
	{
		for (int y = 0; y < H; y++)
		{
			for (int x = 0; x < W; x++)
			{
				if (!Grid[y][x].present || Grid[y][x].roomId != firstRoom) continue;
				for (int direction = 0; direction < 4; direction++)
				{
					if (!Grid[y][x].conn[direction]) continue;
					const int nx = x + DX[direction];
					const int ny = y + DY[direction];
					if (nx >= 0 && nx < W && ny >= 0 && ny < H &&
						Grid[ny][nx].roomId == secondRoom)
						return true;
				}
			}
		}
		return false;
	};
	auto CanHostSwitchPanel = [&](int roomId) -> bool
	{
		if (!IsValidRoom(roomId)) return false;
		const RoomInfo& room = Rooms[roomId];
		for (int y = room.minJ; y <= room.maxJ; y++)
		{
			for (int x = room.minI; x <= room.maxI; x++)
			{
				if (x < 0 || x >= W || y < 0 || y >= H ||
					!Grid[y][x].present || Grid[y][x].roomId != roomId)
					continue;
				for (int direction = 0; direction < 4; direction++)
				{
					if (Grid[y][x].conn[direction]) continue;
					const double halfSpan = direction == DIR_N || direction == DIR_S ?
						roomHalfX[roomId] : roomHalfY[roomId];
					if (halfSpan * 2.0 - room.cornerCut * 2.0 >= 96.0)
						return true;
				}
			}
		}
		return false;
	};

	// At least one key shrine becomes a deterministic-random ambush. Additional
	// keys have an independent chance to reveal their own monster closet.
	TArray<int> keyRooms;
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		if (Rooms[ri].hasKey) keyRooms.Push(ri);
	ShuffleRooms(keyRooms);
	int nextKeyTrapTag = 1000;
	bool assignedKeyTrap = false;
	int additionalKeyTrapChance = Detail == 0 ? 0 : (Detail == 2 ? 90 : 60);
	if (themeStyle == ThemeHell || themeStyle == ThemeCorrupted)
		additionalKeyTrapChance = std::min(100, additionalKeyTrapChance + 10);
	for (unsigned int index = 0; index < keyRooms.Size(); index++)
	{
		const int keyRoomId = keyRooms[index];
		if (assignedKeyTrap && (RNG() % 100) >= additionalKeyTrapChance) continue;
		int hostRoomId = keyRoomId;
		int featureX, featureY;
		if (!PickFeatureCell(hostRoomId, RevealKeyTrap, featureX, featureY))
		{
			int bestScore = 1000000;
			hostRoomId = -1;
			for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
			{
				const RoomInfo& room = Rooms[ri];
				if ((int)ri == keyRoomId || revealKinds[ri] != RevealNone ||
					!CanHostReveal(ri, RevealKeyTrap) ||
					room.hasPlayerStart || room.hasKey || room.hasExit || room.hasBoss ||
					room.isLocked || room.isSecret)
					continue;
				int score = abs(room.progressionRank - Rooms[keyRoomId].progressionRank) * 12 +
					abs(room.distFromStart - Rooms[keyRoomId].distFromStart) * 4 + (RNG() % 5);
				if (RoomsConnected(keyRoomId, ri)) score -= 1000;
				if (score < bestScore)
				{
					bestScore = score;
					hostRoomId = ri;
				}
			}
			if (hostRoomId >= 0 &&
				!PickFeatureCell(hostRoomId, RevealKeyTrap, featureX, featureY))
				hostRoomId = -1;
		}
		if (hostRoomId < 0) continue;
		const int targetTag = nextKeyTrapTag++;
		revealKinds[hostRoomId] = RevealKeyTrap;
		revealTags[hostRoomId] = targetTag;
		revealBorderTypes[hostRoomId] = Rooms[keyRoomId].keyType;
		revealCellX[hostRoomId] = featureX;
		revealCellY[hostRoomId] = featureY;
		static const int KeyRevealVariants[] = { 0, 2, 3 };
		revealVariants[hostRoomId] =
			KeyRevealVariants[(targetTag - 1000) % countof(KeyRevealVariants)];
		AssignRevealArchitecture(hostRoomId, featureX, featureY, RevealKeyTrap);
		revealCellX[hostRoomId] = featureX;
		revealCellY[hostRoomId] = featureY;
		if (revealArchitectures[hostRoomId] == RevealPavilion)
			revealDoorSides[hostRoomId] = ChooseRevealDoorSide(hostRoomId,
				featureX, featureY, RevealKeyTrap);
		keyTriggerTags[keyRoomId] = targetTag;
		assignedKeyTrap = true;
	}
	if (!assignedKeyTrap)
	{
		LastError = "Could not place a key-triggered ambush chamber";
		return false;
	}

	// Broad non-critical rooms can contain a remote supply cache. A switch is
	// authored on a real perimeter wall and opens the tagged closet permanently.
	TArray<int> switchRooms;
	int desiredSwitchRooms = Detail == 0 ? 1 + Size / 12 :
		(Detail == 2 ? 2 + Size / 4 : 1 + Size / 6);
	if (themeStyle == ThemeIndustrial) desiredSwitchRooms += 1 + Size / 12;
	else if (themeStyle == ThemeTechbase && Detail == 2) desiredSwitchRooms++;
	for (int pass = 0; pass < 2; pass++)
	{
		for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		{
			const RoomInfo& room = Rooms[ri];
			if (revealKinds[ri] != RevealNone || !CanHostReveal(ri, RevealSwitchCache) ||
				!CanHostSwitchPanel(ri) ||
				room.hasPlayerStart ||
				room.hasKey || room.isLocked || room.isSecret)
				continue;
			if ((pass == 0 && room.isArena) || (pass == 1 && !room.isArena)) continue;
			if (room.isArena || room.isHub || room.isDeadEnd || room.onMainPath)
				switchRooms.Push(ri);
		}
		if (switchRooms.Size() >= (unsigned int)desiredSwitchRooms) break;
	}
	ShuffleRooms(switchRooms);
	int nextSwitchTag = 1500;
	int switchBudget = desiredSwitchRooms;
	for (unsigned int index = 0; index < switchRooms.Size() && switchBudget > 0; index++)
	{
		const int roomId = switchRooms[index];
		int featureX, featureY;
		if (!PickFeatureCell(roomId, RevealSwitchCache, featureX, featureY)) continue;
		revealKinds[roomId] = RevealSwitchCache;
		revealTags[roomId] = nextSwitchTag++;
		revealBorderTypes[roomId] = 0;
		revealCellX[roomId] = featureX;
		revealCellY[roomId] = featureY;
		static const int SwitchRevealVariants[] = { 1, 3, 2, 0 };
		revealVariants[roomId] =
			SwitchRevealVariants[(revealTags[roomId] - 1500) % countof(SwitchRevealVariants)];
		AssignRevealArchitecture(roomId, featureX, featureY, RevealSwitchCache);
		revealCellX[roomId] = featureX;
		revealCellY[roomId] = featureY;
		if (revealArchitectures[roomId] == RevealPavilion)
			revealDoorSides[roomId] = ChooseRevealDoorSide(roomId,
				featureX, featureY, RevealSwitchCache);

		// Some opportunity switches live across the room boundary from their
		// cache. Keep the source in the same lock stage so the remote action adds
		// discovery and reuse without operating through an unavailable key gate.
		int switchRoomId = roomId;
		if ((revealVariants[roomId] & 1) != 0)
		{
			int bestScore = 1000000;
			for (unsigned int source = 0; source < Rooms.Size(); source++)
			{
				const RoomInfo& sourceRoom = Rooms[source];
				if ((int)source == roomId || switchTargetTags[source] > 0 ||
					revealKinds[source] != RevealNone || sourceRoom.isLocked ||
					sourceRoom.isSecret || sourceRoom.hasKey || sourceRoom.hasExit ||
					sourceRoom.hasPlayerStart || sourceRoom.lockStage != Rooms[roomId].lockStage ||
					!CanHostSwitchPanel(source))
					continue;
				bool reservedCacheHost = false;
				for (unsigned int candidate = 0; candidate < switchRooms.Size(); candidate++)
				{
					if (switchRooms[candidate] == (int)source)
					{
						reservedCacheHost = true;
						break;
					}
				}
				if (reservedCacheHost) continue;
				int score = abs(sourceRoom.progressionRank - Rooms[roomId].progressionRank) * 12 +
					abs(sourceRoom.distFromStart - Rooms[roomId].distFromStart) * 4 +
					(RNG() % 5);
				if (RoomsConnected(roomId, source)) score -= 1000;
				if (score < bestScore)
				{
					bestScore = score;
					switchRoomId = source;
				}
			}
		}
		switchTargetTags[switchRoomId] = revealTags[roomId];
		switchBudget--;
	}
	if (nextSwitchTag == 1500)
	{
		LastError = "Could not place a switch-operated reveal chamber";
		return false;
	}

	// A reveal pavilion needs its full circulation ring. If the only valid host
	// cell also owns a staircase, restore that chamber face; the opposite face
	// remains inset and still provides a useful connector run.
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		if (revealKinds[ri] == RevealNone) continue;
		const int x = revealCellX[ri];
		const int y = revealCellY[ri];
		if (x < 0 || x >= W || y < 0 || y >= H) continue;
		cellEdges[y][x].edge[DIR_N] = roomHalfY[ri];
		cellEdges[y][x].edge[DIR_S] = roomHalfY[ri];
		cellEdges[y][x].edge[DIR_W] = roomHalfX[ri];
		cellEdges[y][x].edge[DIR_E] = roomHalfX[ri];
	}

	// Clamping against different host rooms can occasionally collapse every
	// profile to the same bounding box. If that happens, safely narrow one axis
	// by four units while retaining the 160-unit footprint and 56-unit interior
	// half-size contracts.
	int firstFootprintX = -1;
	int firstFootprintY = -1;
	int revealProfileCount = 0;
	bool variedFootprints = false;
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		if (revealKinds[ri] == RevealNone) continue;
		const RevealProfile profile = BuildRevealProfile(ri, revealKinds[ri]);
		const int footprintX = (int)lround(profile.outerX * 2.0);
		const int footprintY = (int)lround(profile.outerY * 2.0);
		if (revealProfileCount == 0)
		{
			firstFootprintX = footprintX;
			firstFootprintY = footprintY;
		}
		else if (footprintX != firstFootprintX || footprintY != firstFootprintY)
			variedFootprints = true;
		revealProfileCount++;
	}
	if (revealProfileCount >= 2 && !variedFootprints)
	{
		for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		{
			if (revealKinds[ri] == RevealNone) continue;
			const RevealProfile profile = BuildRevealProfile(ri, revealKinds[ri]);
			if (profile.outerX >= 84.0 && profile.innerX >= 60.0)
			{
				revealProfileAdjustX[ri] = -4.0;
				break;
			}
			if (profile.outerY >= 84.0 && profile.innerY >= 60.0)
			{
				revealProfileAdjustY[ri] = -4.0;
				break;
			}
		}
	}

	// Three or more interactive structures should not all face the same axis.
	// Prefer rotating one toward open composed-room space; the
	// guaranteed circulation ring remains a safe fallback on linear layouts.
	int revealCount = 0;
	int revealAxisMask = 0;
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		if (revealKinds[ri] == RevealNone) continue;
		revealCount++;
		revealAxisMask |= 1 << (revealDoorSides[ri] & 1);
	}
	if (revealCount >= 3 && revealAxisMask != 3)
	{
		const int desiredAxis = (revealAxisMask & 1) ? 1 : 0;
		static const int SideForGridDirection[4] = { 0, 2, 3, 1 };
		for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		{
			if (revealKinds[ri] == RevealNone ||
				revealArchitectures[ri] != RevealPavilion) continue;
			const int featureX = revealCellX[ri];
			const int featureY = revealCellY[ri];
			TArray<int> alternatives;
			for (int pass = 0; pass < 3 && alternatives.Size() == 0; pass++)
			{
				for (int direction = 0; direction < 4; direction++)
				{
					const int side = SideForGridDirection[direction];
					if ((side & 1) != desiredAxis) continue;
					const int nx = featureX + DX[direction];
					const int ny = featureY + DY[direction];
					const bool sameRoom = nx >= 0 && nx < W && ny >= 0 && ny < H &&
						Grid[ny][nx].present && Grid[ny][nx].roomId == (int)ri;
					const bool connected = Grid[featureY][featureX].conn[direction];
					if ((pass == 0 && sameRoom) || (pass == 1 && connected) || pass == 2)
						alternatives.Push(side);
				}
			}
			if (alternatives.Size() > 0)
			{
				const int style = abs(Rooms[ri].id * 31 + Rooms[ri].visualVariant * 7);
				revealDoorSides[ri] = alternatives[style % alternatives.Size()];
				break;
			}
		}
	}

	// A separate sample of open arenas receives a raised ranged platform;
	// sufficiently tall hubs and broad route rooms provide deterministic
	// fallbacks. Every platform points a staircase into the composed room so
	// elevation changes create combat choices without unreachable space.
	TArray<int> perchRooms;
	int perchBudgetTarget = Detail == 0 ? 1 + Size / 10 :
		(Detail == 2 ? 2 + Size / 3 : 1 + Size / 4);
	if (themeStyle == ThemeHell || themeStyle == ThemeGothic)
		perchBudgetTarget += 1 + Size / 12;
	auto HasPerchCandidate = [&](int roomId) -> bool
	{
		for (unsigned int index = 0; index < perchRooms.Size(); index++)
			if (perchRooms[index] == roomId) return true;
		return false;
	};
	for (int pass = 0; pass < 3; pass++)
	{
		for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		{
			const RoomInfo& room = Rooms[ri];
			if (HasPerchCandidate(ri) || room.cellCount < 2 || room.hasPlayerStart ||
				room.hasKey || room.isLocked || room.isSecret ||
				room.ceilZ - room.floorZ < 128.0)
				continue;
			const bool preferred = room.isArena || (outdoorRooms[ri] && room.isHub);
			const bool finale = room.hasExit || room.hasBoss;
			if (pass == 0 && (!preferred || finale || revealKinds[ri] != RevealNone)) continue;
			if (pass == 1 && (finale ||
				(preferred && revealKinds[ri] == RevealNone))) continue;
			if (pass == 2 && !finale) continue;
			if (preferred || room.isHub || room.onMainPath || room.isDeadEnd || finale)
				perchRooms.Push(ri);
		}
		if (perchRooms.Size() >= (unsigned int)perchBudgetTarget) break;
	}
	ShuffleRooms(perchRooms);
	int nextPerchTag = 2000;
	int perchBudget = perchBudgetTarget;
	for (unsigned int index = 0; index < perchRooms.Size() && perchBudget > 0; index++)
	{
		const int roomId = perchRooms[index];
		int featureX, featureY;
		if (!PickPerchCell(roomId, featureX, featureY)) continue;
		perchTags[roomId] = nextPerchTag++;
		perchCellX[roomId] = featureX;
		perchCellY[roomId] = featureY;
		perchApproachSides[roomId] = ChoosePerchApproachSide(roomId,
			featureX, featureY);
		perchVariants[roomId] = (variantSeedMod3 + nextPerchTag - 2001) % 3;
		perchBudget--;
	}
	if (nextPerchTag == 2000)
	{
		LastError = "Could not place an elevated ranged-monster perch";
		return false;
	}

	// Operable lifts add meaningful height variation without placing a mandatory
	// route behind moving geometry. They live in spare cells with a full walkable
	// ring, so a raised or occupied platform can always be bypassed.
	auto IsLiftCellCandidate = [&](int roomId, int x, int y,
		bool allowTransition) -> bool
	{
		const ProcGenCell& cell = Grid[y][x];
		if (!cell.present || cell.roomId != roomId || cell.hasPlayerStart ||
			cell.hasKey || cell.hasExit || cell.hasBoss || cell.isLocked)
			return false;
		if (!allowTransition && CellHasHeightTransition(x, y)) return false;
		return !((x == revealCellX[roomId] && y == revealCellY[roomId]) ||
			(x == perchCellX[roomId] && y == perchCellY[roomId]) ||
			IsLandmarkAnchorCell(roomId, x, y));
	};
	auto HasLiftCell = [&](int roomId) -> bool
	{
		for (int y = 0; y < H; y++)
		{
			for (int x = 0; x < W; x++)
				if (IsLiftCellCandidate(roomId, x, y, true)) return true;
		}
		return false;
	};
	auto PickLiftCell = [&](int roomId, int& featureX, int& featureY) -> bool
	{
		for (int pass = 0; pass < 2; pass++)
		{
			TArray<std::pair<int, int>> candidates;
			for (int y = 0; y < H; y++)
			{
				for (int x = 0; x < W; x++)
				{
					if (!IsLiftCellCandidate(roomId, x, y, pass != 0)) continue;
					candidates.Push(std::make_pair(x, y));
				}
			}
			if (candidates.Size() == 0) continue;
			const auto& selected = candidates[RNG() % candidates.Size()];
			featureX = selected.first;
			featureY = selected.second;
			return true;
		}
		return false;
	};
	TArray<int> liftRooms;
	auto HasLiftRoom = [&](int roomId) -> bool
	{
		for (unsigned int index = 0; index < liftRooms.Size(); index++)
			if (liftRooms[index] == roomId) return true;
		return false;
	};
	int liftBudgetTarget = Detail == 0 ? 1 :
		(Detail == 2 ? 2 + Size / 5 : 1 + Size / 8);
	if (themeStyle == ThemeIndustrial) liftBudgetTarget += 1 + Size / 8;
	else if (themeStyle == ThemeTechbase && Detail == 2) liftBudgetTarget++;
	for (int pass = 0; pass < 3; pass++)
	{
		for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		{
			const RoomInfo& room = Rooms[ri];
			if (HasLiftRoom(ri) || room.hasPlayerStart || room.hasKey ||
				room.hasExit || room.hasBoss || room.isLocked || room.isSecret ||
				room.ceilZ - room.floorZ < 96.0 || !HasLiftCell(ri))
				continue;
			const bool ordinaryRoute = room.onMainPath && !room.isArena && !room.isHub;
			const bool landmark = room.isArena || room.isHub;
			if ((pass == 0 && !ordinaryRoute) || (pass == 1 && !landmark) ||
				(pass == 2 && (ordinaryRoute || landmark)))
				continue;
			liftRooms.Push(ri);
		}
		if (liftRooms.Size() >= (unsigned int)liftBudgetTarget) break;
	}
	ShuffleRooms(liftRooms);
	int nextLiftTag = 3000;
	int liftBudget = liftBudgetTarget;
	for (unsigned int index = 0; index < liftRooms.Size() && liftBudget > 0; index++)
	{
		const int roomId = liftRooms[index];
		int featureX, featureY;
		if (!PickLiftCell(roomId, featureX, featureY)) continue;
		liftTags[roomId] = nextLiftTag++;
		liftCellX[roomId] = featureX;
		liftCellY[roomId] = featureY;
		liftBudget--;
	}
	if (nextLiftTag == 3000)
	{
		LastError = "Could not place a safely bypassable lift";
		return false;
	}

	// Animated liquid basins occupy spare feature cells, never landmark anchors
	// or mandatory connectors. Keeping at least one ordinary placement cell in
	// the host room prevents encounter and reward fallbacks from entering a pool.
	auto PickFluidCell = [&](int roomId, int& featureX, int& featureY) -> bool
	{
		const RoomInfo& room = Rooms[roomId];
		int reservedCells = revealKinds[roomId] != RevealNone ? 1 : 0;
		reservedCells += perchTags[roomId] > 0 ? 1 : 0;
		reservedCells += liftTags[roomId] > 0 ? 1 : 0;
		reservedCells += (room.isArena || room.isHub) ? 1 : 0;
		if (room.cellCount < reservedCells + 2) return false;
		TArray<std::pair<int, int>> candidates;
		for (int y = 0; y < H; y++)
		{
			for (int x = 0; x < W; x++)
			{
				const ProcGenCell& cell = Grid[y][x];
				if (!cell.present || cell.roomId != roomId || cell.hasPlayerStart ||
					cell.hasKey || cell.hasExit || cell.hasBoss || cell.isLocked ||
					CellHasHeightTransition(x, y) || IsLandmarkAnchorCell(roomId, x, y))
					continue;
				if ((x == revealCellX[roomId] && y == revealCellY[roomId]) ||
					(x == perchCellX[roomId] && y == perchCellY[roomId]) ||
					(x == liftCellX[roomId] && y == liftCellY[roomId]))
					continue;
				candidates.Push(std::make_pair(x, y));
			}
		}
		if (candidates.Size() == 0) return false;
		const auto& selected = candidates[RNG() % candidates.Size()];
		featureX = selected.first;
		featureY = selected.second;
		return true;
	};
	auto ChooseFluidKind = [&](const RoomInfo& room, int ordinal) -> int
	{
		const bool hazardous = (ordinal & 1) != 0;
		if (themeStyle == ThemeHell)
			return hazardous ? FluidLava : FluidBlood;
		if (themeStyle == ThemeGothic)
		{
			if (hazardous) return FluidLava;
			return (ordinal % 3) == 0 ? FluidBlood : FluidWater;
		}
		if (themeStyle == ThemeCorrupted)
		{
			const bool infernalPhase = room.lockStage >= 2;
			return hazardous ? (infernalPhase ? FluidLava : FluidNukage) :
				(infernalPhase ? FluidBlood : FluidWater);
		}
		return hazardous ? FluidNukage : FluidWater;
	};
	TArray<int> fluidRooms;
	int fluidBudgetTarget = Detail == 0 ? 1 + Size / 12 :
		(Detail == 2 ? 2 + Size / 3 : 1 + Size / 5);
	if (Outdoors == 2) fluidBudgetTarget += 1 + Size / 10;
	if (themeStyle == ThemeHell || themeStyle == ThemeIndustrial)
		fluidBudgetTarget += Size / 10;
	for (int pass = 0; pass < 3; pass++)
	{
		for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		{
			const RoomInfo& room = Rooms[ri];
			if (room.hasPlayerStart || room.hasKey || room.hasExit || room.hasBoss ||
				room.isLocked || room.isSecret || room.cellCount < 2)
				continue;
			bool alreadySelected = false;
			for (unsigned int selected = 0; selected < fluidRooms.Size(); selected++)
				if (fluidRooms[selected] == (int)ri) alreadySelected = true;
			if (alreadySelected) continue;
			const bool landmark = room.isArena || room.isHub || outdoorRooms[ri];
			const bool optional = room.isDeadEnd || !room.onMainPath;
			if ((pass == 0 && !landmark) || (pass == 1 && !optional) ||
				(pass == 2 && (landmark || optional)))
				continue;
			int featureX, featureY;
			if (!PickFluidCell(ri, featureX, featureY)) continue;
			fluidCellX[ri] = featureX;
			fluidCellY[ri] = featureY;
			fluidRooms.Push(ri);
		}
		if (fluidRooms.Size() >= (unsigned int)fluidBudgetTarget) break;
	}
	ShuffleRooms(fluidRooms);
	for (unsigned int index = 0;
		index < fluidRooms.Size() && index < (unsigned int)fluidBudgetTarget; index++)
	{
		const int roomId = fluidRooms[index];
		fluidKinds[roomId] = ChooseFluidKind(Rooms[roomId], index);
		fluidVariants[roomId] = (variantSeedMod3 + index) % 3;
	}
	// Discard candidates beyond the requested budget, because their cells were
	// tentatively reserved while the preferred-room passes were assembled.
	for (unsigned int index = fluidBudgetTarget; index < fluidRooms.Size(); index++)
	{
		const int roomId = fluidRooms[index];
		fluidCellX[roomId] = fluidCellY[roomId] = -1;
	}
	if (fluidRooms.Size() == 0)
	{
		LastError = "Could not place a safely bypassable fluid basin";
		return false;
	}

	// Room sectors are shared by all chamber cells belonging to the composed
	// room. Physical separation is still explicit in the chamber boundaries.
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.id < 0) continue;
		const char* ceiling = outdoorRooms[ri] ? "F_SKY1" : SafeTexture(room.ceilTex, "CEIL3_5");
		int light = outdoorRooms[ri] ? std::max(room.light, 192) : std::max(room.light, 160);
		room.sectorIdx = AddSector(room.floorZ, room.ceilZ,
			SafeTexture(room.floorTex, "FLOOR4_8"), ceiling, light);
		sectors[room.sectorIdx].lightColor = room.lightColor;
		sectors[room.sectorIdx].fadeColor = outdoorRooms[ri] ? 0 : room.fadeColor;
		// ZDoom-namespace UDMF sector specials are already translated. Doom's raw
		// special 9 would therefore remain an ordinary non-secret effect; the
		// engine's canonical SECRET_MASK is the real automap/statistics flag.
		if (room.isSecret) sectors[room.sectorIdx].special = 0x0400;
	}
	auto ApplyRoomLighting = [&](int sectorIndex, const RoomInfo& room, bool sky)
	{
		if (sectorIndex < 0 || sectorIndex >= (int)sectors.Size()) return;
		sectors[sectorIndex].lightColor = room.lightColor;
		sectors[sectorIndex].fadeColor = sky ? 0 : room.fadeColor;
	};

	TArray<TArray<CellConnections>> connectionGrid;
	connectionGrid.Resize(H);
	for (int y = 0; y < H; y++) connectionGrid[y].Resize(W);
	TArray<StairConnection> stairConnections;

	TArray<std::pair<int, int>> doorPairs;
	auto PairHasDoor = [&](int roomA, int roomB) -> bool
	{
		int low = std::min(roomA, roomB);
		int high = std::max(roomA, roomB);
		for (const auto& pair : doorPairs)
			if (pair.first == low && pair.second == high) return true;
		return false;
	};
	auto RecordDoorPair = [&](int roomA, int roomB)
	{
		doorPairs.Push(std::make_pair(std::min(roomA, roomB), std::max(roomA, roomB)));
	};
	auto LockedDoorTexture = [&](int lockType) -> const char*
	{
		if (lockType == 1) return "BIGDOOR2";
		if (lockType == 2) return "BIGDOOR3";
		if (lockType == 3) return "BIGDOOR4";
		return "BIGDOOR1";
	};
	auto ChooseDoorProfile = [&](int roomA, int roomB, int lockType,
		bool secretDoor) -> DoorProfile
	{
		if (lockType > 0)
			return { LockedDoorTexture(lockType), 128, 128 };
		if (secretDoor)
		{
			// The face itself is replaced with the owning room's wall texture. A
			// compact stock-width opening keeps the hidden door indistinguishable
			// from an ordinary wall panel until used.
			const int height = ((Rooms[roomA].visualVariant + Rooms[roomB].visualVariant) & 1) ?
				96 : 128;
			return { nullptr, 64, height };
		}

		static const DoorProfile TechDoors[] = {
			{ "DOOR1", 64, 72 }, { "DOOR3", 64, 72 },
			{ "BIGDOOR1", 128, 96 }, { "BIGDOOR5", 128, 128 },
			{ "BIGDOOR7", 128, 128 }
		};
		static const DoorProfile IndustrialDoors[] = {
			{ "DOOR3", 64, 72 }, { "BIGDOOR1", 128, 96 },
			{ "BIGDOOR5", 128, 128 }, { "BIGDOOR7", 128, 128 }
		};
		static const DoorProfile HellDoors[] = {
			{ "BIGDOOR1", 128, 96 }, { "BIGDOOR6", 128, 112 },
			{ "BIGDOOR7", 128, 128 }, { "MARBFAC2", 128, 128 }
		};
		static const DoorProfile GothicDoors[] = {
			{ "BIGDOOR1", 128, 96 }, { "BIGDOOR6", 128, 112 },
			{ "BIGDOOR7", 128, 128 }, { "MARBFAC3", 128, 128 }
		};
		static const DoorProfile CorruptedDoors[] = {
			{ "DOOR1", 64, 72 }, { "BIGDOOR1", 128, 96 },
			{ "BIGDOOR6", 128, 112 }, { "BIGDOOR7", 128, 128 },
			{ "MARBFAC2", 128, 128 }
		};
		static const DoorProfile Doom2SpecialDoors[] = {
			{ "SPCDOOR1", 64, 128 }, { "SPCDOOR2", 64, 128 },
			{ "SPCDOOR3", 64, 128 }, { "SPCDOOR4", 64, 128 }
		};

		const int style = abs(roomA * 43 + roomB * 71 +
			Rooms[roomA].visualVariant * 11 + Rooms[roomB].visualVariant * 17 +
			Rooms[roomA].lockStage * 5);
		// Doom II computer/special doors appear primarily in Techbase and
		// Industrial maps, as they do in the stock campaign. They remain excluded
		// from Ultimate Doom, whose IWAD does not define the SPCDOOR family.
		if ((gameinfo.flags & GI_MAPxx) &&
			(themeStyle == ThemeTechbase || themeStyle == ThemeIndustrial) &&
			(style % 5) == 0)
			return Doom2SpecialDoors[(style / 5) % countof(Doom2SpecialDoors)];

		const DoorProfile* profiles = TechDoors;
		int profileCount = countof(TechDoors);
		if (themeStyle == ThemeIndustrial)
		{
			profiles = IndustrialDoors;
			profileCount = countof(IndustrialDoors);
		}
		else if (themeStyle == ThemeHell)
		{
			profiles = HellDoors;
			profileCount = countof(HellDoors);
		}
		else if (themeStyle == ThemeGothic)
		{
			profiles = GothicDoors;
			profileCount = countof(GothicDoors);
		}
		else if (themeStyle == ThemeCorrupted)
		{
			profiles = CorruptedDoors;
			profileCount = countof(CorruptedDoors);
		}
		return profiles[style % profileCount];
	};

	int normalDoorBudget = 2 + Size;
	for (int y = 0; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			if (!Grid[y][x].present) continue;
			for (int direction : { DIR_E, DIR_S })
			{
				if (!Grid[y][x].conn[direction]) continue;
				int nx = x + DX[direction];
				int ny = y + DY[direction];
				if (nx < 0 || nx >= W || ny < 0 || ny >= H || !Grid[ny][nx].present) continue;

				int roomA = Grid[y][x].roomId;
				int roomB = Grid[ny][nx].roomId;
				if (!IsValidRoom(roomA) || !IsValidRoom(roomB)) continue;

				bool lockHere = Grid[y][x].isLocked &&
					(Grid[y][x].lockDir < 0 || Grid[y][x].lockDir == direction);
				bool lockThere = Grid[ny][nx].isLocked &&
					(Grid[ny][nx].lockDir < 0 || Grid[ny][nx].lockDir == OPP[direction]);
				int lockType = lockHere ? Grid[y][x].lockType : (lockThere ? Grid[ny][nx].lockType : 0);
				const bool crossesStage = Grid[y][x].lockStage != Grid[ny][nx].lockStage;
				if ((crossesStage && lockType <= 0) || (!crossesStage && lockType > 0))
				{
					LastError = crossesStage ?
						"A serialized opening would bypass a key stage" :
						"A keyed door was assigned inside one progression stage";
					return false;
				}

				bool secretDoor = roomA != roomB &&
					(Rooms[roomA].isSecret || Rooms[roomB].isSecret);
				const bool protectedFeatureEndpoint =
					Rooms[roomA].hasPlayerStart || Rooms[roomA].hasKey || Rooms[roomA].hasExit ||
					Rooms[roomB].hasPlayerStart || Rooms[roomB].hasKey || Rooms[roomB].hasExit ||
					revealKinds[roomA] != RevealNone || revealKinds[roomB] != RevealNone ||
					perchTags[roomA] > 0 || perchTags[roomB] > 0 ||
					liftTags[roomA] > 0 || liftTags[roomB] > 0;
				bool door = lockType > 0 || secretDoor;
				const bool levelThreshold =
					fabs(Rooms[roomA].floorZ - Rooms[roomB].floorZ) < 0.001;
				// The start landmark is a guaranteed safe staging area. Its own
				// encounter budget is zero, and closed unlocked doors prevent
				// monsters in the first combat room from immediately flooding it.
				if (!door && !protectedFeatureEndpoint && levelThreshold && roomA != roomB &&
					(Rooms[roomA].hasPlayerStart || Rooms[roomB].hasPlayerStart))
				{
					door = true;
					if (normalDoorBudget > 0) normalDoorBudget--;
				}
				if (!door && !protectedFeatureEndpoint && levelThreshold && roomA != roomB &&
					normalDoorBudget > 0 && !PairHasDoor(roomA, roomB))
				{
					bool requested = Rooms[roomA].hasDoor || Rooms[roomB].hasDoor ||
						Rooms[roomA].hasKey || Rooms[roomB].hasKey;
					if (requested || ((Rooms[roomA].isArena || Rooms[roomB].isArena) && (RNG() % 100) < 18))
					{
						door = true;
						normalDoorBudget--;
					}
				}
				if (door && !levelThreshold)
				{
					// Locked, secret, and start-room doors are normalized to a
					// common terrace before this pass. Failing here is safer than
					// emitting a moving door with an impassable ledge under it.
					LastError = "A mandatory procedural door spans unequal floor heights";
					return false;
				}
				if (door && roomA != roomB)
				{
					RecordDoorPair(roomA, roomB);
				}

				DoorProfile doorProfile;
				if (door) doorProfile = ChooseDoorProfile(roomA, roomB, lockType, secretDoor);
				double halfWidth = door ? doorProfile.width * 0.5 : 72.0;
				if (!door && (Rooms[roomA].isArena || Rooms[roomB].isArena)) halfWidth = 96.0;
				else if (!door && (Rooms[roomA].isHub || Rooms[roomB].isHub)) halfWidth = 88.0;
				else if (!door && (Rooms[roomA].branchDepth >= 2 || Rooms[roomB].branchDepth >= 2)) halfWidth = 64.0;
				// A cell can be inset on one unrelated face to make room for a
				// staircase. Clamp this portal against the actual directional
				// extents on both cells, otherwise a wide opening can overrun a
				// chamfer and leave a four-unit BSP sliver at the corner.
				double apertureA = direction == DIR_E ?
					std::min(EdgeForCell(x, y, DIR_N), EdgeForCell(x, y, DIR_S)) :
					std::min(EdgeForCell(x, y, DIR_W), EdgeForCell(x, y, DIR_E));
				double apertureB = direction == DIR_E ?
					std::min(EdgeForCell(nx, ny, DIR_N), EdgeForCell(nx, ny, DIR_S)) :
					std::min(EdgeForCell(nx, ny, DIR_W), EdgeForCell(nx, ny, DIR_E));
				double apertureHalf = std::min(apertureA, apertureB);
				if (roomA == roomB && !door)
				{
					// Keep composed rooms visually open without consuming the entire
					// coarse-cell edge. Full-edge joins from four adjacent cells meet
					// at a single grid vertex and form zero-area pinwheel loops, which
					// the GL node builder cannot triangulate reliably on huge maps.
					// A 224-256 unit portal still reads as a broad hall while leaving
					// an explicit, non-intersecting wall boundary at every junction.
					const double desiredHalf = (Rooms[roomA].isArena || Rooms[roomA].isHub) ?
						128.0 : 112.0;
					halfWidth = std::min(desiredHalf,
						apertureHalf - Rooms[roomA].cornerCut);
				}
				else
				{
					double connectionCut = std::max(Rooms[roomA].cornerCut, Rooms[roomB].cornerCut);
					const double availableHalf = apertureHalf - connectionCut;
					if (door && halfWidth > availableHalf + 0.001)
					{
						if (lockType > 0 || availableHalf < 32.0)
						{
							LastError = "A mandatory procedural door does not fit its jamb aperture";
							return false;
						}
						// Preserve the door instead of cropping its art into the shoulders.
						// The 64x72 stock doors are the authentic compact fallback.
						doorProfile = { ((roomA + roomB) & 1) ? "DOOR1" : "DOOR3", 64, 72 };
						halfWidth = 32.0;
					}
					else halfWidth = std::min(halfWidth, availableHalf);
				}

				int connectionSector = -1;
				int doorSector = -1;
				int approachSectorA = -1;
				int approachSectorB = -1;
				int stairIndex = -1;
				if (roomA == roomB && !door)
				{
					connectionSector = Rooms[roomA].sectorIdx;
				}
				else
				{
					double floorZ = std::max(Rooms[roomA].floorZ, Rooms[roomB].floorZ);
					double openCeil = std::min(Rooms[roomA].ceilZ, Rooms[roomB].ceilZ);
					if (openCeil < floorZ + 72.0) openCeil = floorZ + 72.0;
					bool sky = !door && outdoorRooms[roomA] && outdoorRooms[roomB];
					const char* ceiling = sky ? "F_SKY1" :
						(infernalArchitecture ? "FLAT5_1" : "CEIL3_5");
					int light = clamp((Rooms[roomA].light + Rooms[roomB].light) / 2, 160, 208);
					if (sky) light = std::max(light, 192);
					if (door)
					{
						const double availableClearance = std::min(
							Rooms[roomA].ceilZ - floorZ, Rooms[roomB].ceilZ - floorZ);
						const double doorHeight = std::min<double>(doorProfile.height, availableClearance);
						if (doorHeight < 64.0)
						{
							LastError = "A procedural door has insufficient lintel clearance";
							return false;
						}
						const double doorCeil = floorZ + doorHeight;
						approachSectorA = AddSector(floorZ, doorCeil,
							SafeTexture(Rooms[roomA].floorTex, "FLOOR4_8"),
							SafeTexture(Rooms[roomA].ceilTex, "CEIL3_5"), Rooms[roomA].light);
						approachSectorB = AddSector(floorZ, doorCeil,
							SafeTexture(Rooms[roomB].floorTex, "FLOOR4_8"),
							SafeTexture(Rooms[roomB].ceilTex, "CEIL3_5"), Rooms[roomB].light);
						doorSector = AddSector(floorZ, floorZ,
							SafeTexture(Rooms[roomA].floorTex, "FLOOR4_8"), ceiling, light);
						sectors[approachSectorA].lightColor = Rooms[roomA].lightColor;
						sectors[approachSectorA].fadeColor = Rooms[roomA].fadeColor;
						sectors[approachSectorB].lightColor = Rooms[roomB].lightColor;
						sectors[approachSectorB].fadeColor = Rooms[roomB].fadeColor;
						sectors[doorSector].lightColor = Rooms[roomA].lightColor;
						sectors[doorSector].fadeColor = Rooms[roomA].fadeColor;
					}
					else if (fabs(Rooms[roomA].floorZ - Rooms[roomB].floorZ) < 0.001)
					{
						connectionSector = AddSector(floorZ, openCeil,
							SafeTexture(Rooms[roomA].floorTex, "FLOOR4_8"), ceiling, light);
					}
					else
					{
						const int floorDifference = (int)lround(
							Rooms[roomB].floorZ - Rooms[roomA].floorZ);
						if ((floorDifference % 8) != 0 || abs(floorDifference) > 64)
						{
							LastError.Format("A procedural terrace between rooms %d and %d has an invalid %d-unit rise",
								roomA, roomB, floorDifference);
							return false;
						}
						StairConnection staircase;
						const int stepCount = abs(floorDifference) / 8;
						const int stepDirection = floorDifference > 0 ? 1 : -1;
						for (int step = 0; step < stepCount; step++)
						{
							const double stepFloor = Rooms[roomA].floorZ +
								(step + 1) * 8.0 * stepDirection;
							const char* stepFlat = step * 2 < stepCount ?
								SafeTexture(Rooms[roomA].floorTex, "FLOOR4_8") :
								SafeTexture(Rooms[roomB].floorTex, "FLOOR4_8");
							staircase.sectors.Push(AddSector(stepFloor, openCeil,
								stepFlat, ceiling, light));
						}
						stairConnections.Push(std::move(staircase));
						stairIndex = stairConnections.Size() - 1;
						connectionSector = stairConnections[stairIndex].sectors[0];
					}
				}

				ConnectionRef refA;
				refA.sector = door ? approachSectorA : connectionSector;
				refA.doorSector = doorSector;
				refA.stairIndex = stairIndex;
				refA.halfWidth = halfWidth;
				refA.door = door;
				refA.secret = secretDoor;
				refA.lockType = lockType;
				if (door)
				{
					refA.doorHeight = sectors[approachSectorA].ceilZ - sectors[approachSectorA].floorZ;
					refA.doorTexture = doorProfile.texture ? doorProfile.texture : "";
					refA.doorTextureWidth = doorProfile.width;
					refA.doorTextureHeight = doorProfile.height;
				}
				ConnectionRef refB = refA;
				refB.sector = door ? approachSectorB :
					(stairIndex >= 0 ? stairConnections[stairIndex].sectors.Last() : connectionSector);
				connectionGrid[y][x].refs[direction] = refA;
				connectionGrid[ny][nx].refs[OPP[direction]] = refB;
			}
		}
	}

	auto DoorTrackTexture = [&](int lockType) -> const char*
	{
		if (lockType == 1) return "DOORRED";
		if (lockType == 2) return "DOORBLU";
		if (lockType == 3) return "DOORYEL";
		return "DOORTRAK";
	};

	auto AddDoorFace = [&](double x1, double y1, double x2, double y2,
		int approachSector, int doorSector, const ConnectionRef& ref,
		const char* roomWall)
	{
		const char* doorTexture = ref.secret ? roomWall :
			SafeTexture(ref.doorTexture, "BIGDOOR1");
		// The upper texture is deliberately pegged to the moving door ceiling;
		// unlike the tracks, the door face must rise with the sector.
		int lineIndex = AddLine(x1, y1, x2, y2, approachSector, doorSector,
			doorTexture, nullptr, roomWall,
			doorTexture, nullptr, roomWall,
			false, 12, ref.lockType, 0, 16, 150, 0, 0,
			true, false, true, false, false);
		if (lineIndex >= 0)
		{
			lines[lineIndex].secret = ref.secret;
			const double faceWidth = hypot(x2 - x1, y2 - y1);
			const double faceHeight = std::max(1.0,
				sectors[approachSector].ceilZ - sectors[doorSector].floorZ);
			const int textureWidth = ref.secret ? 128 : ref.doorTextureWidth;
			const int textureHeight = ref.secret ? 128 : ref.doorTextureHeight;
			const double fittedYScale = std::min(1.0, textureHeight / faceHeight);
			sides[lines[lineIndex].sideFront].scaleYTop = fittedYScale;
			sides[lines[lineIndex].sideBack].scaleYTop = fittedYScale;
			// Match the face to its stock texture's real width. This prevents a
			// 64-unit DOOR/SPCDOOR motif from being phased like a 128-unit BIGDOOR
			// and visually spilling into the adjacent jambs.
			int crop = (int)lround(std::max(0.0, (textureWidth - faceWidth) * 0.5));
			sides[lines[lineIndex].sideFront].offsetX = crop;
			sides[lines[lineIndex].sideBack].offsetX = crop;
		}
	};

	auto AddPortal = [&](double x1, double y1, double x2, double y2,
		int roomSector, const ConnectionRef& ref, const char* roomWall)
	{
		if (ref.sector < 0 || ref.sector == roomSector) return;
		// Door connections use a separate lowered approach sector. This portal
		// authors a real lintel between the tall room and the stock-height jamb,
		// containing the moving face instead of letting its texture share the
		// neighboring room wall's vertical span.
		AddLine(x1, y1, x2, y2, roomSector, ref.sector,
			roomWall, nullptr, roomWall,
			roomWall, nullptr, roomWall,
			false, 0, 0, 0, 0, 0, 0, 0,
			false, false, false, true, true);
	};

	TArray<bool> switchWallEmitted;
	switchWallEmitted.Resize(Rooms.Size());
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++) switchWallEmitted[ri] = false;
	auto AddChamberWall = [&](int roomId, int cellX, int cellY, int direction,
		double x1, double y1, double x2, double y2, int sector,
		const char* texture, bool switchEligible)
	{
		const double length = hypot(x2 - x1, y2 - y1);
		if (revealArchitectures[roomId] == RevealFalseWall &&
			revealCellX[roomId] == cellX && revealCellY[roomId] == cellY &&
			revealDoorSides[roomId] == WallSideForGridDirection[direction] &&
			revealWallLineIndices[roomId] < 0 && length >= 144.0)
		{
			const double unitX = (x2 - x1) / length;
			const double unitY = (y2 - y1) / length;
			const double centerX = (x1 + x2) * 0.5;
			const double centerY = (y1 + y2) * 0.5;
			const double doorHalf = 48.0;
			const double doorX1 = centerX - unitX * doorHalf;
			const double doorY1 = centerY - unitY * doorHalf;
			const double doorX2 = centerX + unitX * doorHalf;
			const double doorY2 = centerY + unitY * doorHalf;
			AddWall(x1, y1, doorX1, doorY1, sector, texture);
			revealWallLineIndices[roomId] = AddWall(doorX1, doorY1,
				doorX2, doorY2, sector, texture);
			AddWall(doorX2, doorY2, x2, y2, sector, texture);
			return;
		}
		if (switchEligible && switchTargetTags[roomId] > 0 &&
			!switchWallEmitted[roomId] && length >= 96.0)
		{
			const double unitX = (x2 - x1) / length;
			const double unitY = (y2 - y1) / length;
			const double centerX = (x1 + x2) * 0.5;
			const double centerY = (y1 + y2) * 0.5;
			const double panelX1 = centerX - unitX * 32.0;
			const double panelY1 = centerY - unitY * 32.0;
			const double panelX2 = centerX + unitX * 32.0;
			const double panelY2 = centerY + unitY * 32.0;
			AddWall(x1, y1, panelX1, panelY1, sector, texture);
			if (AddSwitchWall(panelX1, panelY1, panelX2, panelY2,
				sector, switchTargetTags[roomId]) >= 0)
				switchWallEmitted[roomId] = true;
			AddWall(panelX2, panelY2, x2, y2, sector, texture);
			return;
		}
		AddWall(x1, y1, x2, y2, sector, texture);
	};

	// Emit one closed, chamfered chamber polygon per present coarse cell. The
	// 45-degree corners break up the coarse grid silhouette while every segment
	// remains part of a simple, clockwise boundary whose front side faces in.
	for (int y = 0; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			ProcGenCell& cell = Grid[y][x];
			if (!cell.present || !IsValidRoom(cell.roomId)) continue;
			int roomSector = Rooms[cell.roomId].sectorIdx;
			const char* wall = SafeTexture(Rooms[cell.roomId].wallTex, "STARTAN3");
			const RoomInfo& room = Rooms[cell.roomId];
			// Flat runs remain coherent, while their real chamfer seams can carry a
			// theme-specific support or damaged-detail material. Detail density controls
			// how often these architectural accents appear without creating fake splits.
			const int trimHash = abs(x * 17 + y * 29 + room.visualVariant * 7 + room.id * 3);
			const bool useTrim = Detail == 2 || (Detail == 1 && (trimHash % 3) != 0);
			const char* cornerWall = useTrim ?
				SafeTexture((Detail == 2 && (trimHash & 1)) ? room.detailTex : room.accentTex, wall) : wall;
			double cx = CellCenterX(x);
			double cy = CellCenterY(y);
			double leftHalf = EdgeForCell(x, y, DIR_W);
			double rightHalf = EdgeForCell(x, y, DIR_E);
			double bottomHalf = EdgeForCell(x, y, DIR_N);
			double topHalf = EdgeForCell(x, y, DIR_S);
			double left = cx - leftHalf;
			double right = cx + rightHalf;
			double bottom = cy - bottomHalf;
			double top = cy + topHalf;

			const ConnectionRef& topRef = connectionGrid[y][x].refs[DIR_S];
			const ConnectionRef& rightRef = connectionGrid[y][x].refs[DIR_E];
			const ConnectionRef& bottomRef = connectionGrid[y][x].refs[DIR_N];
			const ConnectionRef& leftRef = connectionGrid[y][x].refs[DIR_W];
			bool topFull = topRef.sector == roomSector &&
				topRef.halfWidth >= std::min(leftHalf, rightHalf) - 0.001;
			bool rightFull = rightRef.sector == roomSector &&
				rightRef.halfWidth >= std::min(bottomHalf, topHalf) - 0.001;
			bool bottomFull = bottomRef.sector == roomSector &&
				bottomRef.halfWidth >= std::min(leftHalf, rightHalf) - 0.001;
			bool leftFull = leftRef.sector == roomSector &&
				leftRef.halfWidth >= std::min(bottomHalf, topHalf) - 0.001;

			// A same-room connection consumes the whole coarse edge. Corners are
			// chamfered only where both adjacent edges belong to the true room
			// perimeter, so composed rooms read as one hall instead of pods joined
			// by repeated narrow waists.
			double cutTR = (topFull || rightFull) ? 0.0 : room.cornerCut;
			double cutBR = (rightFull || bottomFull) ? 0.0 : room.cornerCut;
			double cutBL = (bottomFull || leftFull) ? 0.0 : room.cornerCut;
			double cutTL = (leftFull || topFull) ? 0.0 : room.cornerCut;
			double topLeft = left + cutTL;
			double topRight = right - cutTR;
			double rightTop = top - cutTR;
			double rightBottom = bottom + cutBR;
			double bottomRight = right - cutBR;
			double bottomLeft = left + cutBL;
			double leftBottom = bottom + cutBL;
			double leftTop = top - cutTL;

			// North/world-top edge: left -> right (grid DIR_S).
			if (topRef.sector >= 0)
			{
				AddChamberWall(cell.roomId, x, y, DIR_S,
					topLeft, top, cx - topRef.halfWidth, top,
					roomSector, wall, true);
				AddPortal(cx - topRef.halfWidth, top, cx + topRef.halfWidth, top,
					roomSector, topRef, wall);
				AddChamberWall(cell.roomId, x, y, DIR_S,
					cx + topRef.halfWidth, top, topRight, top,
					roomSector, wall, true);
			}
			else AddChamberWall(cell.roomId, x, y, DIR_S,
				topLeft, top, topRight, top, roomSector, wall, true);
			AddWall(topRight, top, right, rightTop, roomSector, cornerWall);

			// East edge: top -> bottom.
			if (rightRef.sector >= 0)
			{
				AddChamberWall(cell.roomId, x, y, DIR_E,
					right, rightTop, right, cy + rightRef.halfWidth,
					roomSector, wall, true);
				AddPortal(right, cy + rightRef.halfWidth, right, cy - rightRef.halfWidth,
					roomSector, rightRef, wall);
				AddChamberWall(cell.roomId, x, y, DIR_E,
					right, cy - rightRef.halfWidth, right, rightBottom,
					roomSector, wall, true);
			}
			else AddChamberWall(cell.roomId, x, y, DIR_E,
				right, rightTop, right, rightBottom,
				roomSector, wall, true);
			AddWall(right, rightBottom, bottomRight, bottom, roomSector, cornerWall);

			// South/world-bottom edge: right -> left (grid DIR_N).
			if (bottomRef.sector >= 0)
			{
				AddChamberWall(cell.roomId, x, y, DIR_N,
					bottomRight, bottom, cx + bottomRef.halfWidth,
					bottom, roomSector, wall, true);
				AddPortal(cx + bottomRef.halfWidth, bottom, cx - bottomRef.halfWidth, bottom,
					roomSector, bottomRef, wall);
				AddChamberWall(cell.roomId, x, y, DIR_N,
					cx - bottomRef.halfWidth, bottom, bottomLeft,
					bottom, roomSector, wall, true);
			}
			else AddChamberWall(cell.roomId, x, y, DIR_N,
				bottomRight, bottom, bottomLeft, bottom,
				roomSector, wall, true);
			AddWall(bottomLeft, bottom, left, leftBottom, roomSector, cornerWall);

			// West edge: bottom -> top.
			if (leftRef.sector >= 0)
			{
				AddChamberWall(cell.roomId, x, y, DIR_W,
					left, leftBottom, left, cy - leftRef.halfWidth,
					roomSector, wall, true);
				AddPortal(left, cy - leftRef.halfWidth, left, cy + leftRef.halfWidth,
					roomSector, leftRef, wall);
				AddChamberWall(cell.roomId, x, y, DIR_W,
					left, cy + leftRef.halfWidth, left, leftTop,
					roomSector, wall, true);
			}
			else AddChamberWall(cell.roomId, x, y, DIR_W,
				left, leftBottom, left, leftTop,
				roomSector, wall, true);
			AddWall(left, leftTop, topLeft, top, roomSector, cornerWall);
		}
	}
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		if (revealArchitectures[ri] == RevealFalseWall &&
			revealKinds[ri] != RevealNone && revealWallLineIndices[ri] < 0)
		{
			LastError.Format("Could not reserve false-wall reveal face for room %u", ri);
			return false;
		}
		if (switchTargetTags[ri] > 0 && !switchWallEmitted[ri])
		{
			LastError.Format("Could not place procedural switch panel for room %u (tag %d, cells %d)",
				ri, switchTargetTags[ri], Rooms[ri].cellCount);
			return false;
		}
	}

	// Corridor side walls complete the union between chamber openings. End
	// portals were emitted above and share deduplicated vertices with these.
	const char* corridorWall = themeStyle == ThemeIndustrial ? "SUPPORT3" :
		(themeStyle == ThemeGothic ? "WOOD1" :
			(infernalArchitecture ? "GSTVINE1" : "SUPPORT2"));
	for (int y = 0; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			if (!Grid[y][x].present) continue;

			const ConnectionRef& east = connectionGrid[y][x].refs[DIR_E];
			if (east.sector >= 0 && x + 1 < W && Grid[y][x + 1].present)
			{
				double x1 = CellCenterX(x) + EdgeForCell(x, y, DIR_E);
				double x2 = CellCenterX(x + 1) - EdgeForCell(x + 1, y, DIR_W);
				double cy = CellCenterY(y);
				// Keep connector returns inside their owning coarse-cell half. A
				// maximum-width opening already reaches within eight units of the
				// cell midpoint; extending its reveal all the way to that midpoint
				// makes perpendicular connectors emit coincident solid lines at
				// four-way junctions. Those overlaps produce malformed GL-node holes
				// on large maps even though the UDMF itself still parses successfully.
				const double revealDepth = clamp(CELL_HALF - 8.0 - east.halfWidth,
					0.0, 8.0);
				if (east.door && east.doorSector >= 0)
				{
					int roomA = Grid[y][x].roomId;
					int roomB = Grid[y][x + 1].roomId;
					const ConnectionRef& west = connectionGrid[y][x + 1].refs[DIR_W];
					const int approachA = east.sector;
					const int approachB = west.sector;
					double mid = (x1 + x2) * 0.5;
					double doorLeft = mid - 8.0;
					double doorRight = mid + 8.0;
					double portalTop = cy + east.halfWidth;
					double portalBottom = cy - east.halfWidth;
					// Door art owns exactly the selected stock width. Ordinary corridor
					// reveals may flare by eight units, but doing that at the slab makes a
					// 64-wide DOOR texture bleed across 80 units of jamb geometry.
					double top = portalTop;
					double bottom = portalBottom;
					const char* track = east.secret ? corridorWall : DoorTrackTexture(east.lockType);
					const char* jamb = east.lockType > 0 ? track : corridorWall;
					const char* roomWallA = SafeTexture(Rooms[roomA].wallTex, "STARTAN3");
					const char* roomWallB = SafeTexture(Rooms[roomB].wallTex, "STARTAN3");

					// Step the connector walls outward behind 8-unit returns. The depth
					// break gives support/jamb materials a physical seam instead of
					// changing texture midway through a continuous chamber wall.
					AddWall(x1, portalTop, x1, top, approachA, roomWallA);
					AddWall(x1, top, doorLeft, top, approachA, jamb);
					AddWall(doorLeft, top, doorRight, top, east.doorSector, track);
					AddWall(doorRight, top, x2, top, approachB, jamb);
					AddWall(x2, top, x2, portalTop, approachB, roomWallB);
					AddWall(x2, portalBottom, x2, bottom, approachB, roomWallB);
					AddWall(doorLeft, bottom, x1, bottom, approachA, jamb);
					AddWall(doorRight, bottom, doorLeft, bottom, east.doorSector, track);
					AddWall(x2, bottom, doorRight, bottom, approachB, jamb);
					AddWall(x1, bottom, x1, portalBottom, approachA, roomWallA);

					AddDoorFace(doorLeft, top, doorLeft, bottom,
						approachA, east.doorSector, east,
						SafeTexture(Rooms[roomA].wallTex, "STARTAN3"));
					AddDoorFace(doorRight, bottom, doorRight, top,
						approachB, east.doorSector, west,
						SafeTexture(Rooms[roomB].wallTex, "STARTAN3"));
				}
				else
				{
					const double portalTop = cy + east.halfWidth;
					const double portalBottom = cy - east.halfWidth;
					const double top = portalTop + revealDepth;
					const double bottom = portalBottom - revealDepth;
					if (east.stairIndex >= 0)
					{
						const StairConnection& staircase = stairConnections[east.stairIndex];
						const int stepCount = staircase.sectors.Size();
						const int firstSector = staircase.sectors[0];
						const int lastSector = staircase.sectors.Last();
						const char* stepWall = SafeTexture(
							Rooms[Grid[y][x].roomId].accentTex, "STEP1");
						AddWall(x1, portalTop, x1, top, firstSector, corridorWall);
						AddWall(x2, top, x2, portalTop, lastSector, corridorWall);
						AddWall(x2, portalBottom, x2, bottom, lastSector, corridorWall);
						AddWall(x1, bottom, x1, portalBottom, firstSector, corridorWall);
						for (int step = 0; step < stepCount; step++)
						{
							const double stepX1 = x1 + (x2 - x1) * step / stepCount;
							const double stepX2 = x1 + (x2 - x1) * (step + 1) / stepCount;
							const int stepSector = staircase.sectors[step];
							AddWall(stepX1, top, stepX2, top, stepSector, corridorWall);
							AddWall(stepX2, bottom, stepX1, bottom, stepSector, corridorWall);
							if (step + 1 < stepCount)
							{
								AddLine(stepX2, top, stepX2, bottom,
									stepSector, staircase.sectors[step + 1],
									stepWall, nullptr, stepWall,
									stepWall, nullptr, stepWall,
									false, 0, 0, 0, 0, 0, 0, 0,
									false, false, false, true, true);
							}
						}
					}
					else
					{
						const int roomA = Grid[y][x].roomId;
						const int roomB = Grid[y][x + 1].roomId;
						const char* endWallA = east.sector == Rooms[roomA].sectorIdx ?
							SafeTexture(Rooms[roomA].wallTex, "STARTAN3") : corridorWall;
						const char* endWallB = east.sector == Rooms[roomB].sectorIdx ?
							SafeTexture(Rooms[roomB].wallTex, "STARTAN3") : corridorWall;
						AddWall(x1, portalTop, x1, top, east.sector, endWallA);
						AddWall(x1, top, x2, top, east.sector, corridorWall);
						AddWall(x2, top, x2, portalTop, east.sector, endWallB);
						AddWall(x2, portalBottom, x2, bottom, east.sector, endWallB);
						AddWall(x2, bottom, x1, bottom, east.sector, corridorWall);
						AddWall(x1, bottom, x1, portalBottom, east.sector, endWallA);
					}
				}
			}

			const ConnectionRef& north = connectionGrid[y][x].refs[DIR_S];
			if (north.sector >= 0 && y + 1 < H && Grid[y + 1][x].present)
			{
				double y1 = CellCenterY(y) + EdgeForCell(x, y, DIR_S);
				double y2 = CellCenterY(y + 1) - EdgeForCell(x, y + 1, DIR_N);
				double cx = CellCenterX(x);
				const double revealDepth = clamp(CELL_HALF - 8.0 - north.halfWidth,
					0.0, 8.0);
				if (north.door && north.doorSector >= 0)
				{
					int roomA = Grid[y][x].roomId;
					int roomB = Grid[y + 1][x].roomId;
					const ConnectionRef& south = connectionGrid[y + 1][x].refs[DIR_N];
					const int approachA = north.sector;
					const int approachB = south.sector;
					double mid = (y1 + y2) * 0.5;
					double doorBottom = mid - 8.0;
					double doorTop = mid + 8.0;
					double portalLeft = cx - north.halfWidth;
					double portalRight = cx + north.halfWidth;
					double left = portalLeft;
					double right = portalRight;
					const char* track = north.secret ? corridorWall : DoorTrackTexture(north.lockType);
					const char* jamb = north.lockType > 0 ? track : corridorWall;
					const char* roomWallA = SafeTexture(Rooms[roomA].wallTex, "STARTAN3");
					const char* roomWallB = SafeTexture(Rooms[roomB].wallTex, "STARTAN3");

					AddWall(portalRight, y2, right, y2, approachB, roomWallB);
					AddWall(right, doorBottom, right, y1, approachA, jamb);
					AddWall(right, doorTop, right, doorBottom, north.doorSector, track);
					AddWall(right, y2, right, doorTop, approachB, jamb);
					AddWall(right, y1, portalRight, y1, approachA, roomWallA);
					AddWall(portalLeft, y1, left, y1, approachA, roomWallA);
					AddWall(left, y1, left, doorBottom, approachA, jamb);
					AddWall(left, doorBottom, left, doorTop, north.doorSector, track);
					AddWall(left, doorTop, left, y2, approachB, jamb);
					AddWall(left, y2, portalLeft, y2, approachB, roomWallB);

					AddDoorFace(left, doorBottom, right, doorBottom,
						approachA, north.doorSector, north,
						SafeTexture(Rooms[roomA].wallTex, "STARTAN3"));
					AddDoorFace(right, doorTop, left, doorTop,
						approachB, north.doorSector, south,
						SafeTexture(Rooms[roomB].wallTex, "STARTAN3"));
				}
				else
				{
					const double portalLeft = cx - north.halfWidth;
					const double portalRight = cx + north.halfWidth;
					const double left = portalLeft - revealDepth;
					const double right = portalRight + revealDepth;
					if (north.stairIndex >= 0)
					{
						const StairConnection& staircase = stairConnections[north.stairIndex];
						const int stepCount = staircase.sectors.Size();
						const int firstSector = staircase.sectors[0];
						const int lastSector = staircase.sectors.Last();
						const char* stepWall = SafeTexture(
							Rooms[Grid[y][x].roomId].accentTex, "STEP1");
						AddWall(portalRight, y2, right, y2, lastSector, corridorWall);
						AddWall(right, y1, portalRight, y1, firstSector, corridorWall);
						AddWall(portalLeft, y1, left, y1, firstSector, corridorWall);
						AddWall(left, y2, portalLeft, y2, lastSector, corridorWall);
						for (int step = 0; step < stepCount; step++)
						{
							const double stepY1 = y1 + (y2 - y1) * step / stepCount;
							const double stepY2 = y1 + (y2 - y1) * (step + 1) / stepCount;
							const int stepSector = staircase.sectors[step];
							AddWall(right, stepY2, right, stepY1, stepSector, corridorWall);
							AddWall(left, stepY1, left, stepY2, stepSector, corridorWall);
							if (step + 1 < stepCount)
							{
								AddLine(left, stepY2, right, stepY2,
									stepSector, staircase.sectors[step + 1],
									stepWall, nullptr, stepWall,
									stepWall, nullptr, stepWall,
									false, 0, 0, 0, 0, 0, 0, 0,
									false, false, false, true, true);
							}
						}
					}
					else
					{
						const int roomA = Grid[y][x].roomId;
						const int roomB = Grid[y + 1][x].roomId;
						const char* endWallA = north.sector == Rooms[roomA].sectorIdx ?
							SafeTexture(Rooms[roomA].wallTex, "STARTAN3") : corridorWall;
						const char* endWallB = north.sector == Rooms[roomB].sectorIdx ?
							SafeTexture(Rooms[roomB].wallTex, "STARTAN3") : corridorWall;
						AddWall(portalRight, y2, right, y2, north.sector, endWallB);
						AddWall(right, y2, right, y1, north.sector, corridorWall);
						AddWall(right, y1, portalRight, y1, north.sector, endWallA);
						AddWall(portalLeft, y1, left, y1, north.sector, endWallA);
						AddWall(left, y1, left, y2, north.sector, corridorWall);
						AddWall(left, y2, portalLeft, y2, north.sector, endWallB);
					}
				}
			}
		}
	}

	auto ChooseMonster = [&](const RoomInfo& room, int enemyIndex) -> int
	{
		static const int DoomEarly[] = { 3004, 3004, 9, 3001, 3002 };
		static const int DoomMid[] = { 9, 3001, 3002, 3005, 3006 };
		static const int DoomLate[] = { 3001, 3002, 3003, 3005, 3006 };
		static const int EarlyInfantry[] = { 3004, 3004, 9, 3001 };
		static const int EarlyDemons[] = { 3001, 3001, 3002 };
		static const int MidInfantry[] = { 9, 3001, 65, 66 };
		static const int MidDemons[] = { 3002, 3002, 3001, 69 };
		static const int MidFlyers[] = { 3005, 3005, 3006, 3001 };
		static const int LateBruisers[] = { 69, 3002, 66, 3003 };
		static const int LateHeavy[] = { 66, 69, 67, 3003 };
		static const int LateAir[] = { 3005, 71, 66, 69 };
		int family = (room.id + room.progressionRank + room.branchDepth) % 3;
		int jitter = enemyIndex + (int)(RNG() % 3);
		if (!(gameinfo.flags & GI_MAPxx))
		{
			if (room.monsterTier <= 2) return DoomEarly[jitter % countof(DoomEarly)];
			if (room.monsterTier <= 4) return DoomMid[jitter % countof(DoomMid)];
			return DoomLate[jitter % countof(DoomLate)];
		}

		if (room.monsterTier <= 2)
		{
			if ((family & 1) == 0) return EarlyInfantry[jitter % countof(EarlyInfantry)];
			return EarlyDemons[jitter % countof(EarlyDemons)];
		}
		if (room.monsterTier <= 4)
		{
			if (family == 0) return MidInfantry[jitter % countof(MidInfantry)];
			if (family == 1) return MidDemons[jitter % countof(MidDemons)];
			return MidFlyers[jitter % countof(MidFlyers)];
		}
		if (family == 0) return LateBruisers[jitter % countof(LateBruisers)];
		if (family == 1) return LateHeavy[jitter % countof(LateHeavy)];
		return LateAir[jitter % countof(LateAir)];
	};

	auto ChooseRangedMonster = [&](const RoomInfo& room, int salt) -> int
	{
		static const int DoomRanged[] = { 3004, 9, 3001, 3001 };
		static const int Doom2Ranged[] = { 3004, 9, 3001, 65, 66 };
		if (!(gameinfo.flags & GI_MAPxx))
			return DoomRanged[(room.monsterTier + salt + RNG()) % countof(DoomRanged)];
		int count = room.monsterTier >= 4 ? countof(Doom2Ranged) : 3;
		return Doom2Ranged[(room.monsterTier + salt + RNG()) % count];
	};

	auto AddRemoteDoorFace = [&](double x1, double y1, double x2, double y2,
		int roomSector, int doorSector, const char* doorTexture, const char* roomWall,
		bool secretFace = false)
	{
		int lineIndex = AddLine(x1, y1, x2, y2, roomSector, doorSector,
			doorTexture, nullptr, roomWall,
			doorTexture, nullptr, roomWall,
			false, 0, 0, 0, 0, 0, 0, 0,
			false, false, false, false, false);
		if (lineIndex < 0) return;
		lines[lineIndex].secret = secretFace;
		const double faceWidth = hypot(x2 - x1, y2 - y1);
		const double faceHeight = std::max(1.0,
			sectors[roomSector].ceilZ - sectors[doorSector].floorZ);
		const double fittedYScale = std::min(1.0, 128.0 / faceHeight);
		const int crop = (int)lround(std::max(0.0, (128.0 - faceWidth) * 0.5));
		sides[lines[lineIndex].sideFront].scaleYTop = fittedYScale;
		sides[lines[lineIndex].sideBack].scaleYTop = fittedYScale;
		sides[lines[lineIndex].sideFront].offsetX = crop;
		sides[lines[lineIndex].sideBack].offsetX = crop;
	};

	auto AddRevealCloset = [&](const RoomInfo& room, double cx, double cy,
		int targetTag, int borderType, const RevealProfile& profile,
		int doorSide, int architecture, int cue) -> int
	{
		const double doorHalf = architecture == RevealWallAlcove ? 32.0 : 40.0;
		const char* roomWall = SafeTexture(room.wallTex, "STARTAN3");
		const bool hiddenDoor = cue == RevealHidden;
		const bool subtleDoor = cue == RevealSubtle;
		const char* closetWall = (profile.variant & 2) != 0 ?
			SafeTexture(room.detailTex, roomWall) : SafeTexture(room.accentTex, roomWall);
		const char* prominentTexture = borderType > 0 ? LockedDoorTexture(borderType) :
			(themeStyle == ThemeIndustrial ? "BIGDOOR5" :
				(themeStyle == ThemeGothic || themeStyle == ThemeHell ? "BIGDOOR6" :
					(themeStyle == ThemeCorrupted ? "BIGDOOR7" : "BIGDOOR1")));
		const char* doorTexture = hiddenDoor ? roomWall :
			(subtleDoor ? SafeTexture(room.detailTex, roomWall) : prominentTexture);
		const char* track = borderType > 0 ? DoorTrackTexture(borderType) :
			(hiddenDoor ? roomWall :
				(subtleDoor ? SafeTexture(room.accentTex, roomWall) : DoorTrackTexture(0)));
		static const char* TechRevealFloors[] = {
			"FLOOR0_1", "FLAT20", "FLOOR5_2", "FLAT14"
		};
		static const char* HellRevealFloors[] = {
			"FLAT5_1", "FLOOR7_2", "FLAT8", "FLAT5_2"
		};
		static const char* IndustrialRevealFloors[] = {
			"FLOOR0_1", "FLAT20", "FLOOR5_2", "FLAT14"
		};
		static const char* GothicRevealFloors[] = {
			"FLAT10", "FLOOR7_2", "FLAT5_1", "FLAT8"
		};
		const char** revealFloors = TechRevealFloors;
		if (themeStyle == ThemeIndustrial) revealFloors = IndustrialRevealFloors;
		else if (themeStyle == ThemeGothic) revealFloors = GothicRevealFloors;
		else if (themeStyle == ThemeHell ||
			(themeStyle == ThemeCorrupted && room.lockStage >= 2))
			revealFloors = HellRevealFloors;
		const char* revealFloor = revealFloors[profile.variant % 4];
		const double closetFloor = room.floorZ + profile.floorDelta;
		const double closetCeil = std::max(closetFloor + 96.0,
			room.ceilZ - profile.ceilingDrop);
		const int closetLight = room.light +
			(profile.variant == 0 ? 16 : (profile.variant == 1 ? -8 : 8));
		int closetSector = AddSector(closetFloor, closetCeil,
			revealFloor, SafeTexture(room.ceilTex, "CEIL3_5"), closetLight);
		int doorSector = AddSector(room.floorZ, room.floorZ,
			revealFloor, SafeTexture(room.ceilTex, "CEIL3_5"),
			closetLight, targetTag);
		ApplyRoomLighting(closetSector, room, false);
		ApplyRoomLighting(doorSector, room, false);

		auto MakeChamferedLoop = [&](double halfX, double halfY,
			double chamfer) -> TArray<std::pair<double, double>>
		{
			const double delta = profile.variant == 0 ? 0.0 :
				(profile.variant == 1 ? 6.0 : (profile.variant == 2 ? 10.0 : 8.0));
			double cuts[4] = { chamfer, chamfer, chamfer, chamfer };
			if (profile.variant == 1)
			{
				cuts[0] += delta;
				cuts[1] -= delta;
				cuts[2] += delta;
				cuts[3] -= delta;
			}
			else if (profile.variant == 2)
			{
				cuts[0] -= delta;
				cuts[1] -= delta;
				cuts[2] += delta;
				cuts[3] += delta;
			}
			else if (profile.variant == 3)
			{
				cuts[0] += delta;
				cuts[2] -= delta;
			}
			// Any cardinal edge can own the 80-unit door. Preserve shoulders on
			// every edge even when the asymmetric profile enlarges a corner cut.
			const double maxCut = std::max(6.0,
				std::min(halfX, halfY) - doorHalf - 2.0);
			for (double& cut : cuts) cut = clamp(cut, 6.0, maxCut);
			TArray<std::pair<double, double>> points;
			points.Push(std::make_pair(cx - halfX + cuts[0], cy - halfY));
			points.Push(std::make_pair(cx + halfX - cuts[1], cy - halfY));
			points.Push(std::make_pair(cx + halfX, cy - halfY + cuts[1]));
			points.Push(std::make_pair(cx + halfX, cy + halfY - cuts[2]));
			points.Push(std::make_pair(cx + halfX - cuts[2], cy + halfY));
			points.Push(std::make_pair(cx - halfX + cuts[3], cy + halfY));
			points.Push(std::make_pair(cx - halfX, cy + halfY - cuts[3]));
			points.Push(std::make_pair(cx - halfX, cy - halfY + cuts[0]));
			return points;
		};
		const TArray<std::pair<double, double>> outer = MakeChamferedLoop(
			profile.outerX, profile.outerY, profile.outerChamfer);
		const TArray<std::pair<double, double>> inner = MakeChamferedLoop(
			profile.innerX, profile.innerY, profile.innerChamfer);
		const int doorEdge = clamp(doorSide, 0, 3) * 2;

		auto DoorEndpoints = [&](const TArray<std::pair<double, double>>& points,
			double& ax, double& ay, double& bx, double& by)
		{
			const auto& first = points[doorEdge];
			const auto& second = points[(doorEdge + 1) % points.Size()];
			const double length = hypot(second.first - first.first,
				second.second - first.second);
			const double ux = (second.first - first.first) / length;
			const double uy = (second.second - first.second) / length;
			const double mx = (first.first + second.first) * 0.5;
			const double my = (first.second + second.second) * 0.5;
			ax = mx - ux * doorHalf;
			ay = my - uy * doorHalf;
			bx = mx + ux * doorHalf;
			by = my + uy * doorHalf;
		};
		double outerAX, outerAY, outerBX, outerBY;
		double innerAX, innerAY, innerBX, innerBY;
		DoorEndpoints(outer, outerAX, outerAY, outerBX, outerBY);
		// Project the outer doorway center onto the inner edge. Asymmetric
		// chamfers may shorten different corners, but the moving slab must remain
		// normal to both loops instead of acquiring a subtle diagonal skew.
		const auto& innerFirst = inner[doorEdge];
		const auto& innerSecond = inner[(doorEdge + 1) % inner.Size()];
		const double innerLength = hypot(innerSecond.first - innerFirst.first,
			innerSecond.second - innerFirst.second);
		const double innerUnitX = (innerSecond.first - innerFirst.first) / innerLength;
		const double innerUnitY = (innerSecond.second - innerFirst.second) / innerLength;
		const double outerCenterX = (outerAX + outerBX) * 0.5;
		const double outerCenterY = (outerAY + outerBY) * 0.5;
		double innerCenterDistance =
			(outerCenterX - innerFirst.first) * innerUnitX +
			(outerCenterY - innerFirst.second) * innerUnitY;
		innerCenterDistance = clamp(innerCenterDistance, doorHalf,
			innerLength - doorHalf);
		const double innerCenterX = innerFirst.first + innerUnitX * innerCenterDistance;
		const double innerCenterY = innerFirst.second + innerUnitY * innerCenterDistance;
		innerAX = innerCenterX - innerUnitX * doorHalf;
		innerAY = innerCenterY - innerUnitY * doorHalf;
		innerBX = innerCenterX + innerUnitX * doorHalf;
		innerBY = innerCenterY + innerUnitY * doorHalf;

		// The outer loop is counter-clockwise so its front/right side faces the
		// surrounding room. Four clipped corners replace the former rectangular
		// island, and only the selected cardinal edge contains a door gap.
		for (unsigned int index = 0; index < outer.Size(); index++)
		{
			const auto& first = outer[index];
			const auto& second = outer[(index + 1) % outer.Size()];
			if ((int)index == doorEdge)
			{
				AddWall(first.first, first.second, outerAX, outerAY,
					room.sectorIdx, roomWall);
				AddWall(outerBX, outerBY, second.first, second.second,
					room.sectorIdx, roomWall);
			}
			else AddWall(first.first, first.second, second.first, second.second,
				room.sectorIdx, roomWall);
		}

		// Reverse every inner edge so the front/right side faces the closet's
		// playable interior. Its chamfer and moat depth vary with the room profile.
		for (unsigned int index = 0; index < inner.Size(); index++)
		{
			const auto& first = inner[index];
			const auto& second = inner[(index + 1) % inner.Size()];
			if ((int)index == doorEdge)
			{
				AddWall(second.first, second.second, innerBX, innerBY,
					closetSector, closetWall);
				AddWall(innerAX, innerAY, first.first, first.second,
					closetSector, closetWall);
			}
			else AddWall(second.first, second.second, first.first, first.second,
				closetSector, closetWall);
		}

		AddRemoteDoorFace(outerAX, outerAY, outerBX, outerBY,
			room.sectorIdx, doorSector, doorTexture, roomWall, hiddenDoor);
		AddRemoteDoorFace(innerBX, innerBY, innerAX, innerAY,
			closetSector, doorSector, doorTexture, closetWall);
		AddWall(outerAX, outerAY, innerAX, innerAY, doorSector, track);
		AddWall(innerBX, innerBY, outerBX, outerBY, doorSector, track);
		return closetSector;
	};

	auto AddFalseWallCloset = [&](const RoomInfo& room, int wallLineIndex,
		int targetTag, int borderType, const RevealProfile& profile,
		int doorSide, int cue, double& actorCenterX, double& actorCenterY) -> int
	{
		if (wallLineIndex < 0 || wallLineIndex >= (int)lines.Size()) return -1;
		BuildLine& doorLine = lines[wallLineIndex];
		if (doorLine.sideBack >= 0) return -1;
		const double outerAX = vertices[doorLine.v1].x;
		const double outerAY = vertices[doorLine.v1].y;
		const double outerBX = vertices[doorLine.v2].x;
		const double outerBY = vertices[doorLine.v2].y;
		const double faceWidth = hypot(outerBX - outerAX, outerBY - outerAY);
		if (faceWidth < 95.9) return -1;

		const char* roomWall = SafeTexture(room.wallTex, "STARTAN3");
		const char* closetWall = (profile.variant & 2) != 0 ?
			SafeTexture(room.detailTex, roomWall) : SafeTexture(room.accentTex, roomWall);
		const char* prominentTexture = borderType > 0 ? LockedDoorTexture(borderType) :
			(themeStyle == ThemeIndustrial ? "BIGDOOR5" :
				(themeStyle == ThemeGothic || themeStyle == ThemeHell ? "BIGDOOR6" :
					(themeStyle == ThemeCorrupted ? "BIGDOOR7" : "BIGDOOR1")));
		const bool hiddenDoor = cue == RevealHidden;
		const bool subtleDoor = cue == RevealSubtle;
		const char* doorTexture = hiddenDoor ? roomWall :
			(subtleDoor ? SafeTexture(room.detailTex, roomWall) : prominentTexture);
		const char* track = borderType > 0 && !hiddenDoor ? DoorTrackTexture(borderType) :
			(hiddenDoor ? roomWall : SafeTexture(room.accentTex, DoorTrackTexture(0)));
		const char* revealFloor = themeStyle == ThemeHell ? "FLAT5_2" :
			(themeStyle == ThemeGothic ? "FLAT10" :
				(themeStyle == ThemeIndustrial ? "FLOOR0_1" :
					SafeTexture(room.floorTex, "FLAT20")));
		const double closetFloor = room.floorZ + profile.floorDelta;
		const double closetCeil = std::max(closetFloor + 96.0,
			room.ceilZ - profile.ceilingDrop);
		const int closetLight = room.light +
			(profile.variant == 0 ? 16 : (profile.variant == 1 ? -8 : 8));
		const int closetSector = AddSector(closetFloor, closetCeil,
			revealFloor, SafeTexture(room.ceilTex, "CEIL3_5"), closetLight);
		const int doorSector = AddSector(room.floorZ, room.floorZ,
			revealFloor, SafeTexture(room.ceilTex, "CEIL3_5"), closetLight, targetTag);
		ApplyRoomLighting(closetSector, room, false);
		ApplyRoomLighting(doorSector, room, false);

		// Turn the reserved ordinary wall segment into one side of a remotely
		// operated door. The room-matching cue remains a genuine false wall and is
		// hidden on the automap; framed/prominent variants expose progressively
		// stronger architectural hints without changing activation behavior.
		BuildSide& roomSide = sides[doorLine.sideFront];
		roomSide.top = doorTexture;
		roomSide.middle = "-";
		roomSide.bottom = roomWall;
		roomSide.offsetX = (int)lround(std::max(0.0, (128.0 - faceWidth) * 0.5));
		roomSide.offsetY = 0;
		roomSide.scaleYTop = std::min(1.0, 128.0 /
			std::max(1.0, sectors[room.sectorIdx].ceilZ - room.floorZ));
		doorLine.sideBack = AddSide(doorSector, doorTexture, nullptr, roomWall);
		sides[doorLine.sideBack].offsetX = roomSide.offsetX;
		sides[doorLine.sideBack].scaleYTop = roomSide.scaleYTop;
		doorLine.blocking = false;
		doorLine.dontPegBottom = false;
		doorLine.secret = hiddenDoor;
		const uint32_t lowVertex = (uint32_t)std::min(doorLine.v1, doorLine.v2);
		const uint32_t highVertex = (uint32_t)std::max(doorLine.v1, doorLine.v2);
		solidWallLookup.Remove((uint64_t)lowVertex << 32 | highVertex);

		static const double OutwardX[] = { 0.0, 1.0, 0.0, -1.0 };
		static const double OutwardY[] = { -1.0, 0.0, 1.0, 0.0 };
		doorSide = clamp(doorSide, 0, 3);
		const double outwardX = OutwardX[doorSide];
		const double outwardY = OutwardY[doorSide];
		const double tangentX = (outerBX - outerAX) / faceWidth;
		const double tangentY = (outerBY - outerAY) / faceWidth;
		const double slabDepth = 16.0;
		const double chamberDepth = 112.0 + (profile.variant % 3) * 16.0;
		const double chamberHalf = 64.0 + (profile.variant & 1) * 8.0;
		const double innerAX = outerAX + outwardX * slabDepth;
		const double innerAY = outerAY + outwardY * slabDepth;
		const double innerBX = outerBX + outwardX * slabDepth;
		const double innerBY = outerBY + outwardY * slabDepth;
		const double centerX = (outerAX + outerBX) * 0.5;
		const double centerY = (outerAY + outerBY) * 0.5;
		auto Point = [&](double tangent, double outward, double& x, double& y)
		{
			x = centerX + tangentX * tangent + outwardX * outward;
			y = centerY + tangentY * tangent + outwardY * outward;
		};
		double frontLeftX, frontLeftY, backLeftX, backLeftY;
		double backRightX, backRightY, frontRightX, frontRightY;
		Point(-chamberHalf, slabDepth, frontLeftX, frontLeftY);
		Point(-chamberHalf, chamberDepth, backLeftX, backLeftY);
		Point(chamberHalf, chamberDepth, backRightX, backRightY);
		Point(chamberHalf, slabDepth, frontRightX, frontRightY);
		AddWall(innerAX, innerAY, frontLeftX, frontLeftY, closetSector, closetWall);
		AddWall(frontLeftX, frontLeftY, backLeftX, backLeftY, closetSector, closetWall);
		AddWall(backLeftX, backLeftY, backRightX, backRightY, closetSector, closetWall);
		AddWall(backRightX, backRightY, frontRightX, frontRightY, closetSector, closetWall);
		AddWall(frontRightX, frontRightY, innerBX, innerBY, closetSector, closetWall);
		AddRemoteDoorFace(innerBX, innerBY, innerAX, innerAY,
			closetSector, doorSector, doorTexture, closetWall);
		AddWall(outerAX, outerAY, innerAX, innerAY, doorSector, track);
		AddWall(innerBX, innerBY, outerBX, outerBY, doorSector, track);
		actorCenterX = centerX + outwardX * ((slabDepth + chamberDepth) * 0.5);
		actorCenterY = centerY + outwardY * ((slabDepth + chamberDepth) * 0.5);
		return closetSector;
	};

	auto AddSniperPerch = [&](const RoomInfo& room, double cx, double cy,
		int perchTag, bool sky, int approachSide, int variant,
		double& platformX, double& platformY) -> int
	{
		variant = clamp(variant, 0, 2);
		const double halfOutward = variant == 2 ? 48.0 : (variant == 1 ? 60.0 : 56.0);
		const double halfTangent = variant == 2 ? 72.0 : (variant == 1 ? 60.0 : 56.0);
		const double stairHalf = variant == 0 ? 40.0 : 32.0;
		const double stairOffset = variant == 1 ? 8.0 : 0.0;
		const double stepDepth = variant == 2 ? 48.0 : 24.0;
		const double doglegSign = ((room.id + room.visualVariant) & 1) != 0 ? 1.0 : -1.0;
		const double requestedRise = Difficulty >= 4 || variant == 1 ? 64.0 : 48.0;
		const double raisedFloor = std::min(room.floorZ + requestedRise,
			room.ceilZ - 80.0);
		const int riseSteps = clamp((int)lround((raisedFloor - room.floorZ) / 16.0), 3, 4);
		const int stairCount = riseSteps - 1;
		const char* floor = themeStyle == ThemeIndustrial ? "FLOOR0_1" :
			(themeStyle == ThemeGothic ? "FLOOR7_2" :
			(themeStyle == ThemeHell ? "FLAT5_2" :
			(themeStyle == ThemeCorrupted ? SafeTexture(room.floorTex, "FLAT5_2") : "FLAT20")));
		const char* wall = SafeTexture(room.accentTex, "STEP1");
		const char* ceiling = sky ? "F_SKY1" : SafeTexture(room.ceilTex, "CEIL3_5");
		int perchLight = std::min(room.light + 16, 224);
		if (sky) perchLight = std::max(perchLight, 192);
		int perchSector = AddSector(raisedFloor, room.ceilZ, floor,
			ceiling, perchLight, perchTag);
		ApplyRoomLighting(perchSector, room, sky);

		TArray<int> stairSectors;
		stairSectors.Resize(stairCount);
		for (int level = 0; level < stairCount; level++)
		{
			int stairLight = std::min(room.light + 4 * (level + 1), perchLight);
			if (sky) stairLight = std::max(stairLight, 192);
			stairSectors[level] = AddSector(room.floorZ + (level + 1) * 16.0,
				room.ceilZ, floor, ceiling, stairLight);
			ApplyRoomLighting(stairSectors[level], room, sky);
		}

		approachSide = clamp(approachSide, 0, 3);
		static const double OutwardX[] = { 0.0, 1.0, 0.0, -1.0 };
		static const double OutwardY[] = { -1.0, 0.0, 1.0, 0.0 };
		static const double TangentX[] = { 1.0, 0.0, -1.0, 0.0 };
		static const double TangentY[] = { 0.0, 1.0, 0.0, -1.0 };
		const double outwardX = OutwardX[approachSide];
		const double outwardY = OutwardY[approachSide];
		const double tangentX = TangentX[approachSide];
		const double tangentY = TangentY[approachSide];
		// The broad balcony backs toward the cell perimeter and returns its stair
		// along one side. The other profiles remain centered fighting platforms.
		platformX = cx - outwardX * (variant == 2 ? 48.0 : 0.0);
		platformY = cy - outwardY * (variant == 2 ? 48.0 : 0.0);
		auto Point = [&](double outward, double tangent,
			double& x, double& y)
		{
			x = platformX + outwardX * outward + tangentX * tangent;
			y = platformY + outwardY * outward + tangentY * tangent;
		};
		auto AddPerchEdge = [&](double x1, double y1, double x2, double y2,
			int frontSector, int backSector, bool retainMonster)
		{
			AddLine(x1, y1, x2, y2, frontSector, backSector,
				wall, nullptr, wall, wall, nullptr, wall,
				false, 0, 0, 0, 0, 0, 0, 0,
				false, false, false, true, true, retainMonster);
		};

		// Emit a centered square, an eight-sided turret, or a wide wall-backed
		// balcony. Every perimeter is clockwise and leaves exactly one stair mouth;
		// retaining edges constrain the initial ranged actor without blocking the
		// player's movement, shots, or deliberate drop-offs.
		double approachTopX, approachTopY, openingTopX, openingTopY;
		double openingBottomX, openingBottomY, approachBottomX, approachBottomY;
		const double approachExtent = variant == 1 ? halfTangent - 16.0 : halfTangent;
		Point(halfOutward, approachExtent, approachTopX, approachTopY);
		Point(halfOutward, stairOffset + stairHalf, openingTopX, openingTopY);
		Point(halfOutward, stairOffset - stairHalf, openingBottomX, openingBottomY);
		Point(halfOutward, -approachExtent, approachBottomX, approachBottomY);
		AddPerchEdge(approachTopX, approachTopY, openingTopX, openingTopY,
			perchSector, room.sectorIdx, true);
		AddPerchEdge(openingBottomX, openingBottomY,
			approachBottomX, approachBottomY, perchSector, room.sectorIdx, true);
		if (variant == 1)
		{
			const double chamfer = 16.0;
			const double localPoints[7][2] = {
				{ halfOutward - chamfer, -halfTangent },
				{ -halfOutward + chamfer, -halfTangent },
				{ -halfOutward, -halfTangent + chamfer },
				{ -halfOutward, halfTangent - chamfer },
				{ -halfOutward + chamfer, halfTangent },
				{ halfOutward - chamfer, halfTangent },
				{ halfOutward, halfTangent - chamfer },
			};
			double previousX, previousY;
			Point(halfOutward, -halfTangent + chamfer, previousX, previousY);
			for (const auto& local : localPoints)
			{
				double nextX, nextY;
				Point(local[0], local[1], nextX, nextY);
				AddPerchEdge(previousX, previousY, nextX, nextY,
					perchSector, room.sectorIdx, true);
				previousX = nextX;
				previousY = nextY;
			}
		}
		else
		{
			double firstX, firstY, secondX, secondY;
			Point(halfOutward, -halfTangent, firstX, firstY);
			Point(-halfOutward, -halfTangent, secondX, secondY);
			AddPerchEdge(firstX, firstY, secondX, secondY,
				perchSector, room.sectorIdx, true);
			Point(-halfOutward, halfTangent, firstX, firstY);
			AddPerchEdge(secondX, secondY, firstX, firstY,
				perchSector, room.sectorIdx, true);
			Point(halfOutward, halfTangent, secondX, secondY);
			AddPerchEdge(firstX, firstY, secondX, secondY,
				perchSector, room.sectorIdx, true);
		}

		// Lowest-to-highest 16-unit tiers. The balcony turns its lowest flight by
		// 90 degrees around the next landing; square and turret variants use straight
		// and offset flights. Only retaining sides block monsters, while every entry,
		// riser, landing, and platform connection remains traversable.
		const bool dogleg = variant == 2;
		for (int level = dogleg ? 1 : 0; level < stairCount; level++)
		{
			const double near = halfOutward + (stairCount - 1 - level) * stepDepth;
			const double far = near + stepDepth;
			double ax, ay, bx, by, cx2, cy2, dx, dy;
			Point(near, stairOffset + stairHalf, ax, ay);
			Point(far, stairOffset + stairHalf, bx, by);
			Point(far, stairOffset - stairHalf, cx2, cy2);
			Point(near, stairOffset - stairHalf, dx, dy);
			const bool turnLanding = dogleg && level == 1;
			AddPerchEdge(ax, ay, bx, by, stairSectors[level],
				turnLanding && doglegSign > 0.0 ? stairSectors[0] : room.sectorIdx,
				!(turnLanding && doglegSign > 0.0));
			AddPerchEdge(cx2, cy2, dx, dy, stairSectors[level],
				turnLanding && doglegSign < 0.0 ? stairSectors[0] : room.sectorIdx,
				!(turnLanding && doglegSign < 0.0));
			if (!dogleg && level == 0)
				AddPerchEdge(bx, by, cx2, cy2,
					stairSectors[level], room.sectorIdx, false);
			else if (turnLanding)
				AddPerchEdge(bx, by, cx2, cy2,
					stairSectors[level], room.sectorIdx, true);
			const int higherSector = level + 1 < stairCount ?
				stairSectors[level + 1] : perchSector;
			AddPerchEdge(dx, dy, ax, ay,
				stairSectors[level], higherSector, false);
		}
		if (dogleg)
		{
			// The lowest tread shares one long edge with the +32 landing and opens
			// to the room at its perpendicular end, forming a real L-shaped route.
			const double sharedNear = halfOutward + (stairCount - 2) * stepDepth;
			const double sharedFar = sharedNear + stepDepth;
			const double sharedTangent = doglegSign * stairHalf;
			const double outerTangent = sharedTangent + doglegSign * 64.0;
			double ax, ay, bx, by, cx2, cy2, dx, dy;
			if (doglegSign > 0.0)
			{
				Point(sharedNear, outerTangent, ax, ay);
				Point(sharedFar, outerTangent, bx, by);
				Point(sharedFar, sharedTangent, cx2, cy2);
				Point(sharedNear, sharedTangent, dx, dy);
			}
			else
			{
				Point(sharedFar, outerTangent, ax, ay);
				Point(sharedNear, outerTangent, bx, by);
				Point(sharedNear, sharedTangent, cx2, cy2);
				Point(sharedFar, sharedTangent, dx, dy);
			}
			AddPerchEdge(ax, ay, bx, by,
				stairSectors[0], room.sectorIdx, false);
			AddPerchEdge(bx, by, cx2, cy2,
				stairSectors[0], room.sectorIdx, true);
			AddPerchEdge(dx, dy, ax, ay,
				stairSectors[0], room.sectorIdx, true);
		}
		return perchSector;
	};

	auto AddLiftPlatform = [&](const RoomInfo& room, double cx, double cy,
		int liftTag, bool sky) -> int
	{
		const double half = 40.0;
		const double raisedFloor = room.floorZ + 32.0;
		const char* floor = themeStyle == ThemeIndustrial ? "FLOOR0_1" :
			(themeStyle == ThemeGothic ? "FLAT10" :
			(themeStyle == ThemeHell ? "FLAT5_1" :
			(themeStyle == ThemeCorrupted ? SafeTexture(room.floorTex, "FLAT5_1") : "FLAT20")));
		const char* liftWall = themeStyle == ThemeIndustrial ? "PLAT1" :
			(themeStyle == ThemeGothic ? "WOOD1" :
			(themeStyle == ThemeHell ? "MARBLE2" :
			(themeStyle == ThemeCorrupted ? SafeTexture(room.accentTex, "PLAT1") : "TEKWALL1")));
		const char* ceiling = sky ? "F_SKY1" : SafeTexture(room.ceilTex, "CEIL3_5");
		const int light = sky ? std::max(room.light + 16, 192) : room.light + 16;
		int liftSector = AddSector(raisedFloor, room.ceilZ, floor, ceiling,
			std::min(light, 224), liftTag);
		ApplyRoomLighting(liftSector, room, sky);
		auto AddLiftEdge = [&](double x1, double y1, double x2, double y2)
		{
			AddLine(x1, y1, x2, y2, liftSector, room.sectorIdx,
				liftWall, nullptr, liftWall, liftWall, nullptr, liftWall,
				false, 62, 0, liftTag, 16, 105, 0, 0,
				true, false, true, true, true, true);
		};
		// Clockwise, with the moving platform on the front/right side. Each face
		// can lower it, wait, and return; monster blocking keeps the mechanism from
		// being jammed while the surrounding 40+ unit route stays open.
		AddLiftEdge(cx - half, cy + half, cx + half, cy + half);
		AddLiftEdge(cx + half, cy + half, cx + half, cy - half);
		AddLiftEdge(cx + half, cy - half, cx - half, cy - half);
		AddLiftEdge(cx - half, cy - half, cx - half, cy + half);
		return liftSector;
	};

	auto AddFluidBasin = [&](const RoomInfo& room, double cx, double cy,
		int fluidKind, int variant, bool sky)
	{
		const bool hazardous = fluidKind == FluidNukage || fluidKind == FluidLava;
		const char* flat = fluidKind == FluidWater ? "FWATER1" :
			(fluidKind == FluidBlood ? "BLOOD1" :
				(fluidKind == FluidNukage ? "NUKAGE1" : "LAVA1"));
		const char* basinWall = SafeTexture(room.accentTex, "STEP1");
		const char* ceiling = sky ? "F_SKY1" : SafeTexture(room.ceilTex, "CEIL3_5");
		const double depth = hazardous ? 16.0 : 8.0;
		auto MakeLoop = [&](double centerX, double centerY, double halfX,
			double halfY, double chamfer) -> TArray<std::pair<double, double>>
		{
			TArray<std::pair<double, double>> points;
			points.Push(std::make_pair(centerX - halfX + chamfer, centerY + halfY));
			points.Push(std::make_pair(centerX + halfX - chamfer, centerY + halfY));
			points.Push(std::make_pair(centerX + halfX, centerY + halfY - chamfer));
			points.Push(std::make_pair(centerX + halfX, centerY - halfY + chamfer));
			points.Push(std::make_pair(centerX + halfX - chamfer, centerY - halfY));
			points.Push(std::make_pair(centerX - halfX + chamfer, centerY - halfY));
			points.Push(std::make_pair(centerX - halfX, centerY - halfY + chamfer));
			points.Push(std::make_pair(centerX - halfX, centerY + halfY - chamfer));
			return points;
		};
		auto AddFluidLoop = [&](const TArray<std::pair<double, double>>& points)
		{
			const int light = sky ? std::max(192, std::min(224,
				room.light + (hazardous ? 8 : 0))) :
				std::min(224, room.light + (hazardous ? 8 : 0));
			const int sectorIndex = AddSector(room.floorZ - depth, room.ceilZ,
				flat, ceiling, light);
			ApplyRoomLighting(sectorIndex, room, sky);
			BuildSector& sector = sectors[sectorIndex];
			if (fluidKind == FluidNukage)
			{
				sector.damageAmount = 5;
				sector.damageInterval = 32;
				sector.damageType = "Slime";
			}
			else if (fluidKind == FluidLava)
			{
				sector.damageAmount = 5;
				sector.damageInterval = 16;
				sector.leakiness = 256;
				sector.damageType = "Fire";
				sector.damageTerrainEffect = true;
			}
			for (unsigned int point = 0; point < points.Size(); point++)
			{
				const auto& first = points[point];
				const auto& second = points[(point + 1) % points.Size()];
				AddLine(first.first, first.second, second.first, second.second,
					sectorIndex, room.sectorIdx,
					basinWall, nullptr, basinWall,
					basinWall, nullptr, basinWall,
					false, 0, 0, 0, 0, 0, 0, 0,
					false, false, false, true, true);
			}
		};

		variant %= 3;
		if (variant == 0)
		{
			AddFluidLoop(MakeLoop(cx, cy, 80.0, 72.0, 16.0));
		}
		else if (variant == 1)
		{
			const bool horizontal = ((room.visualVariant + room.id) & 1) == 0;
			AddFluidLoop(MakeLoop(cx, cy, horizontal ? 92.0 : 32.0,
				horizontal ? 32.0 : 92.0, 8.0));
		}
		else
		{
			const bool horizontal = ((room.visualVariant + room.id) & 1) == 0;
			for (int side : { -1, 1 })
			{
				const double poolX = cx + (horizontal ? side * 52.0 : 0.0);
				const double poolY = cy + (horizontal ? 0.0 : side * 52.0);
				AddFluidLoop(MakeLoop(poolX, poolY,
					horizontal ? 36.0 : 52.0, horizontal ? 52.0 : 36.0, 8.0));
			}
		}
	};

	auto AddLandmarkPlatform = [&](const RoomInfo& room, double cx, double cy,
		bool sky, int crossOpenTag) -> int
	{
		double half = room.isArena ? 80.0 : 64.0;
		double raise = room.isArena ? 16.0 : 8.0;
		if (room.hasKey) { half = 64.0; raise = 16.0; }
		if (room.hasPlayerStart) { half = 64.0; raise = 8.0; }
		if (room.hasExit) { half = 96.0; raise = 16.0; }
		const bool hell = themeStyle == ThemeHell;
		const bool gothic = themeStyle == ThemeGothic;
		const bool industrial = themeStyle == ThemeIndustrial;
		const bool corrupted = themeStyle == ThemeCorrupted;
		const char* floor = industrial ? "FLOOR0_1" :
			(gothic ? "FLAT10" : (hell ? "FLOOR7_2" :
			(corrupted ? SafeTexture(room.floorTex, "FLOOR7_2") : "FLAT20")));
		if (room.hasKey) floor = (hell || gothic) ? "FLAT5_1" :
			(industrial ? "FLAT20" : "FLOOR0_1");
		else if (room.hasPlayerStart) floor = (hell || gothic) ? "FLOOR6_1" :
			(industrial ? "FLOOR0_1" : "FLOOR5_1");
		else if (room.hasExit) floor = "GATE1";
		const char* ceiling = sky ? "F_SKY1" : SafeTexture(room.ceilTex, "CEIL3_5");
		const char* step = room.hasExit ? "EXITDOOR" : "STEP1";
		double featureCeil = room.ceilZ;
		if (!sky && room.isHub && !room.hasPlayerStart)
			featureCeil = std::max(room.floorZ + raise + 80.0, room.ceilZ - 16.0);
		int platformLight = room.hasExit ? 224 :
			(sky ? std::max(room.light + 8, 192) : room.light + 8);
		const bool tiered = raise >= 16.0;
		const double outerHalf = half + 24.0;
		int outerSector = room.sectorIdx;
		if (tiered)
		{
			const int outerLight = sky ? std::max(platformLight - 8, 192) :
				std::max(room.light, platformLight - 8);
			outerSector = AddSector(room.floorZ + 8.0, featureCeil,
				floor, ceiling, outerLight);
			ApplyRoomLighting(outerSector, room, sky);
			auto AddOuterStep = [&](double x1, double y1, double x2, double y2)
			{
				AddLine(x1, y1, x2, y2, outerSector, room.sectorIdx,
					"STEP1", nullptr, "STEP1", "STEP1", nullptr, "STEP1",
					false, 0, 0, 0, 0, 0, 0, 0,
					false, false, false, true, true);
			};
			AddOuterStep(cx - outerHalf, cy + outerHalf, cx + outerHalf, cy + outerHalf);
			AddOuterStep(cx + outerHalf, cy + outerHalf, cx + outerHalf, cy - outerHalf);
			AddOuterStep(cx + outerHalf, cy - outerHalf, cx - outerHalf, cy - outerHalf);
			AddOuterStep(cx - outerHalf, cy - outerHalf, cx - outerHalf, cy + outerHalf);
		}
		int platformSector = AddSector(room.floorZ + raise, featureCeil,
			floor, ceiling, platformLight);
		ApplyRoomLighting(platformSector, room, sky);

		auto AddStep = [&](double x1, double y1, double x2, double y2)
		{
			AddLine(x1, y1, x2, y2, platformSector, outerSector,
				step, nullptr, step, step, nullptr, step,
				false, crossOpenTag > 0 ? 11 : 0, 0,
				crossOpenTag, 16, 0, 0, 0,
				false, crossOpenTag > 0, false, true, true);
		};

		// Clockwise: the raised sector is always on the front/right side. Major
		// landmarks use two 8-unit tiers, providing readable Doom-scale stairs
		// instead of a single abrupt 16-unit curb.
		AddStep(cx - half, cy + half, cx + half, cy + half);
		AddStep(cx + half, cy + half, cx + half, cy - half);
		AddStep(cx + half, cy - half, cx - half, cy - half);
		AddStep(cx - half, cy - half, cx - half, cy + half);
		return platformSector;
	};

	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.id < 0 || room.sectorIdx < 0) continue;

		TArray<std::pair<int, int>> roomCells;
		int playerCell = -1;
		int keyCell = -1;
		int exitCell = -1;
		for (int y = room.minJ; y <= room.maxJ; y++)
		{
			for (int x = room.minI; x <= room.maxI; x++)
			{
				if (x < 0 || x >= W || y < 0 || y >= H || Grid[y][x].roomId != (int)ri) continue;
				roomCells.Push(std::make_pair(x, y));
				int index = roomCells.Size() - 1;
				if (Grid[y][x].hasPlayerStart) playerCell = index;
				if (Grid[y][x].hasKey) keyCell = index;
				if (Grid[y][x].hasExit) exitCell = index;
			}
		}
		if (roomCells.Size() == 0) continue;
		int landmarkCell = 0;
		if (room.hasExit && exitCell >= 0) landmarkCell = exitCell;
		else if (room.hasKey && keyCell >= 0) landmarkCell = keyCell;
		else if (room.hasPlayerStart && playerCell >= 0) landmarkCell = playerCell;
		const bool landmarkRole = room.isArena || room.isHub || room.hasPlayerStart ||
			room.hasKey || room.hasExit;
		const bool landmarkHasSpace = room.cellCount >= 2 || room.hasKey || room.hasExit;
		const bool willHaveLandmark = landmarkRole && landmarkHasSpace && !room.isLocked;
		int revealCell = -1;
		int perchCell = -1;
		int liftCell = -1;
		int fluidCell = -1;
		TArray<int> placementCells;
		for (unsigned int index = 0; index < roomCells.Size(); index++)
		{
			const int x = roomCells[index].first;
			const int y = roomCells[index].second;
			if (x == revealCellX[ri] && y == revealCellY[ri]) revealCell = index;
			if (x == perchCellX[ri] && y == perchCellY[ri]) perchCell = index;
			if (x == liftCellX[ri] && y == liftCellY[ri]) liftCell = index;
			if (x == fluidCellX[ri] && y == fluidCellY[ri]) fluidCell = index;
		}
		for (unsigned int index = 0; index < roomCells.Size(); index++)
			if ((int)index != revealCell && (int)index != perchCell &&
				(int)index != liftCell && (int)index != fluidCell &&
				(!willHaveLandmark || roomCells.Size() == 1 || (int)index != landmarkCell))
				placementCells.Push(index);
		if (placementCells.Size() == 0) placementCells.Push(0);

		auto CellPosition = [&](int index, double& px, double& py)
		{
			index = clamp(index, 0, (int)roomCells.Size() - 1);
			px = CellCenterX(roomCells[index].first);
			py = CellCenterY(roomCells[index].second);
		};

		static const double slotX[] = {
			0, 80, -80, 0, 0, 80, -80, 80, -80,
			40, -40, 0, 0, 40, -40, 40, -40
		};
		static const double slotY[] = {
			0, 0, 0, 80, -80, 80, 80, -80, -80,
			0, 0, 40, -40, 40, 40, -40, -40
		};
		auto SlotPosition = [&](int slot, double& px, double& py)
		{
			int placementIndex = (slot / countof(slotX)) % (int)placementCells.Size();
			int cellIndex = placementCells[placementIndex];
			CellPosition(cellIndex, px, py);
			px += slotX[slot % countof(slotX)];
			py += slotY[slot % countof(slotY)];
		};

		double anchorX, anchorY;
		CellPosition(0, anchorX, anchorY);
		int startFacingAngle = 0;
		int landmarkSector = -1;

		if (revealCell >= 0 && revealKinds[ri] != RevealNone)
		{
			double revealX, revealY;
			CellPosition(revealCell, revealX, revealY);
			const RevealProfile profile = BuildRevealProfile(ri, revealKinds[ri]);
			const int doorSide = clamp(revealDoorSides[ri], 0, 3);
			const bool falseWall = revealArchitectures[ri] == RevealFalseWall;
			if (falseWall)
			{
				if (AddFalseWallCloset(room, revealWallLineIndices[ri], revealTags[ri],
					revealBorderTypes[ri], profile, doorSide, revealCues[ri],
					revealX, revealY) < 0)
				{
					LastError.Format("Could not emit false-wall reveal for room %u", ri);
					return false;
				}
			}
			else
			{
				if (revealArchitectures[ri] == RevealWallAlcove)
				{
					// Back the rectangular bank against a real exposed wall while
					// retaining a narrow rendering seam. Its door faces into the room,
					// unlike the circulation-on-all-sides pavilion.
					static const int BackDirectionForDoorSide[4] = {
						DIR_S, DIR_W, DIR_N, DIR_E
					};
					const int backDirection = BackDirectionForDoorSide[doorSide];
					const double backExtent = EdgeForCell(
						revealCellX[ri], revealCellY[ri], backDirection);
					const double profileDepth = (doorSide & 1) != 0 ?
						profile.outerX : profile.outerY;
					const double wallX = CellCenterX(revealCellX[ri]) +
						DX[backDirection] * backExtent;
					const double wallY = CellCenterY(revealCellY[ri]) +
						DY[backDirection] * backExtent;
					revealX = wallX - DX[backDirection] * (profileDepth + 12.0);
					revealY = wallY - DY[backDirection] * (profileDepth + 12.0);
				}
				else
				{
					revealX += profile.offsetX;
					revealY += profile.offsetY;
				}
				AddRevealCloset(room, revealX, revealY, revealTags[ri],
					revealBorderTypes[ri], profile, doorSide,
					revealArchitectures[ri], revealCues[ri]);
			}
			static const double TangentX[] = { 1.0, 0.0, -1.0, 0.0 };
			static const double TangentY[] = { 0.0, 1.0, 0.0, -1.0 };
			static const double InwardX[] = { 0.0, -1.0, 0.0, 1.0 };
			static const double InwardY[] = { 1.0, 0.0, -1.0, 0.0 };
			static const double OutwardX[] = { 0.0, 1.0, 0.0, -1.0 };
			static const double OutwardY[] = { -1.0, 0.0, 1.0, 0.0 };
			const double tangentHalf = (doorSide & 1) ? profile.innerY : profile.innerX;
			const double actorSpread = std::min(30.0, tangentHalf - 24.0);
			auto RevealPosition = [&](double tangent, double inward,
				double& x, double& y)
			{
				const double depthX = falseWall ? OutwardX[doorSide] : InwardX[doorSide];
				const double depthY = falseWall ? OutwardY[doorSide] : InwardY[doorSide];
				x = revealX + TangentX[doorSide] * tangent +
					depthX * inward;
				y = revealY + TangentY[doorSide] * tangent +
					depthY * inward;
			};
			if (revealKinds[ri] == RevealKeyTrap)
			{
				double firstX, firstY, secondX, secondY, rewardX, rewardY;
				double firstDepth = 8.0;
				double secondDepth = 8.0;
				double firstTangent = -actorSpread;
				double secondTangent = actorSpread;
				if (profile.variant == 1)
				{
					firstDepth = 20.0;
					secondDepth = -8.0;
					firstTangent *= 0.75;
					secondTangent *= 0.75;
				}
				else if (profile.variant == 2)
				{
					firstDepth = -8.0;
					secondDepth = 16.0;
				}
				else if (profile.variant == 3)
				{
					firstDepth = 24.0;
					secondTangent *= 0.5;
				}
				RevealPosition(firstTangent, firstDepth, firstX, firstY);
				RevealPosition(secondTangent, secondDepth, secondX, secondY);
				RevealPosition(0.0, profile.variant == 2 ? 24.0 : -24.0,
					rewardX, rewardY);
				int outwardAngle = doorSide == 0 ? 270 :
					(doorSide == 1 ? 0 : (doorSide == 2 ? 90 : 180));
				if (falseWall) outwardAngle = (outwardAngle + 180) % 360;
				AddThing(firstX, firstY, ChooseRangedMonster(room, 1),
					outwardAngle, true);
				AddThing(secondX, secondY, ChooseRangedMonster(room, 2),
					outwardAngle, true);
				AddThing(rewardX, rewardY, 2011);
			}
			else
			{
				double firstX, firstY, secondX, secondY;
				const double firstDepth = (profile.variant & 2) != 0 ? -20.0 : 12.0;
				const double secondDepth = (profile.variant & 1) != 0 ? 20.0 : -12.0;
				RevealPosition(-actorSpread, firstDepth, firstX, firstY);
				RevealPosition(actorSpread, secondDepth, secondX, secondY);
				AddThing(firstX, firstY, 2008);
				AddThing(secondX, secondY, 2012);
				int cachePowerup;
				if (room.lockStage <= 0)
					cachePowerup = (profile.variant & 1) ? 2023 : 8; // berserk/backpack
				else if (room.lockStage == 1)
					cachePowerup = (profile.variant & 1) ? 2026 : 2024; // map/invisibility
				else
					cachePowerup = (profile.variant & 1) ? 2013 : 2024; // soul sphere/invisibility
				double powerupX, powerupY;
				RevealPosition(0.0, profile.variant == 2 ? -24.0 : 24.0,
					powerupX, powerupY);
				AddThing(powerupX, powerupY, cachePowerup);
				if (profile.variant == 3)
				{
					double bonusX, bonusY;
					RevealPosition(0.0, 0.0, bonusX, bonusY);
					AddThing(bonusX, bonusY, 2015);
				}
			}
		}

		if (perchCell >= 0 && perchTags[ri] > 0)
		{
			double perchX, perchY;
			CellPosition(perchCell, perchX, perchY);
			const int approachSide = clamp(perchApproachSides[ri], 0, 3);
			double platformX, platformY;
			AddSniperPerch(room, perchX, perchY, perchTags[ri],
				outdoorRooms[ri], approachSide, perchVariants[ri],
				platformX, platformY);
			AddThing(platformX, platformY, ChooseRangedMonster(room, 3),
				(room.progressionRank * 90) % 360);
			static const double InwardX[] = { 0.0, -1.0, 0.0, 1.0 };
			static const double InwardY[] = { 1.0, 0.0, -1.0, 0.0 };
			AddThing(perchX + InwardX[approachSide] * 80.0,
				perchY + InwardY[approachSide] * 80.0,
				(RNG() & 1) ? 2007 : 2008);
		}

		if (liftCell >= 0 && liftTags[ri] > 0)
		{
			double liftX, liftY;
			CellPosition(liftCell, liftX, liftY);
			AddLiftPlatform(room, liftX, liftY, liftTags[ri], outdoorRooms[ri]);
			AddThing(liftX, liftY, (RNG() & 1) ? 2012 : 2008);
		}

		if (fluidCell >= 0 && fluidVariants[ri] >= 0)
		{
			double fluidX, fluidY;
			CellPosition(fluidCell, fluidX, fluidY);
			AddFluidBasin(room, fluidX, fluidY, fluidKinds[ri],
				fluidVariants[ri], outdoorRooms[ri]);
		}

		if (willHaveLandmark)
		{
			CellPosition(landmarkCell, anchorX, anchorY);
			int crossOpenTag = keyTriggerTags[ri];
			landmarkSector = AddLandmarkPlatform(room, anchorX, anchorY,
				outdoorRooms[ri], crossOpenTag);
		}
		if (room.hasPlayerStart)
		{
			CellPosition(playerCell >= 0 ? playerCell : 0, anchorX, anchorY);
			int startIndex = playerCell >= 0 ? playerCell : 0;
			int startX = roomCells[startIndex].first;
			int startY = roomCells[startIndex].second;
			for (int direction = 0; direction < 4; direction++)
			{
				if (!Grid[startY][startX].conn[direction]) continue;
				startFacingAngle = direction == DIR_E ? 0 : (direction == DIR_S ? 90 :
					(direction == DIR_W ? 180 : 270));
				break;
			}
			AddThing(anchorX, anchorY, 1, startFacingAngle);
		}
		if (room.hasKey && room.keyType >= 1 && room.keyType <= 3)
		{
			CellPosition(keyCell >= 0 ? keyCell : 0, anchorX, anchorY);
			int keyType = room.keyType == 1 ? 13 : (room.keyType == 2 ? 5 : 6);
			AddThing(anchorX, anchorY, keyType);
			if (keyTriggerTags[ri] > 0 && landmarkSector < 0)
			{
				auto AddKeyTrigger = [&](double x1, double y1, double x2, double y2)
				{
					AddLine(x1, y1, x2, y2, room.sectorIdx, room.sectorIdx,
						nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
						false, 11, 0, keyTriggerTags[ri], 16, 0, 0, 0,
						false, true, false);
				};
				AddKeyTrigger(anchorX - 48.0, anchorY + 48.0,
					anchorX + 48.0, anchorY + 48.0);
				AddKeyTrigger(anchorX + 48.0, anchorY + 48.0,
					anchorX + 48.0, anchorY - 48.0);
				AddKeyTrigger(anchorX + 48.0, anchorY - 48.0,
					anchorX - 48.0, anchorY - 48.0);
				AddKeyTrigger(anchorX - 48.0, anchorY - 48.0,
					anchorX - 48.0, anchorY + 48.0);
			}
		}

		int rewardSlot = 1;
		if (room.hasWeapon)
		{
			double x = anchorX;
			double y = anchorY;
			if (!room.hasPlayerStart) SlotPosition(rewardSlot, x, y);
			else
			{
				double radians = startFacingAngle * (3.14159265358979323846 / 180.0);
				x += cos(radians) * 32.0;
				y += sin(radians) * 32.0;
			}
			rewardSlot++;
			AddThing(x, y, room.weaponType);
		}
		if (room.hasAmmo)
		{
			int packs = std::max(1, room.ammoCount);
			for (int pack = 0; pack < packs; pack++)
			{
				double x, y;
				SlotPosition(rewardSlot++, x, y);
				AddThing(x, y, room.ammoType);
			}
		}
		if (room.hasHealth)
		{
			int packs = std::max(1, room.healthCount);
			for (int pack = 0; pack < packs; pack++)
			{
				double x, y;
				SlotPosition(rewardSlot++, x, y);
				AddThing(x, y, room.healthType);
			}
		}
		for (int bonus = 0; bonus < room.healthBonusCount; bonus++)
		{
			double x, y;
			SlotPosition(rewardSlot++, x, y);
			AddThing(x, y, 2014);
		}
		if (room.hasArmor)
		{
			double x, y;
			SlotPosition(rewardSlot++, x, y);
			AddThing(x, y, room.armorType);
		}
		for (unsigned int powerup = 0; powerup < room.powerups.Size(); powerup++)
		{
			double x, y;
			SlotPosition(rewardSlot++, x, y);
			AddThing(x, y, room.powerups[powerup]);
		}

		if (room.hasBoss && !room.hasPlayerStart)
		{
			CellPosition(exitCell >= 0 ? exitCell : 0, anchorX, anchorY);
			int bossType;
			const int MinimumHeavyBossCells = 8;
			const bool hasHeavyArena = room.cellCount >= MinimumHeavyBossCells;
			if (!(gameinfo.flags & GI_MAPxx))
			{
				// Ultimate Doom does not have Doom II's visually similar Hell Knight.
				bossType = Difficulty >= 5 && hasHeavyArena ?
					BossesHard[RNG() % countof(BossesHard)] : 3003;
			}
			else
			{
				bossType = Difficulty <= 3 ? BossesEasy[RNG() % countof(BossesEasy)] :
					(Difficulty <= 4 || !hasHeavyArena ? BossesMed[RNG() % countof(BossesMed)] :
						BossesHard[RNG() % countof(BossesHard)]);
			}
			AddThing(anchorX, anchorY, bossType);
		}

		static const double enemyX[] = { -96, 96, -96, 96, 0, 0, -64, 64, -112, 112, -40, 40 };
		static const double enemyY[] = { -88, -88, 88, 88, -112, 112, -48, 48, 0, 0, 96, -96 };
		for (int enemy = 0; enemy < room.enemyCount; enemy++)
		{
			int placementIndex = (enemy + room.progressionRank) % (int)placementCells.Size();
			int cellIndex = placementCells[placementIndex];
			if (room.hasBoss && placementCells.Size() > 1 && cellIndex == exitCell)
				cellIndex = placementCells[(placementIndex + 1) % placementCells.Size()];
			double x, y;
			CellPosition(cellIndex, x, y);
			double targetX = x;
			double targetY = y;
			int pattern = (enemy / std::max(1, (int)placementCells.Size()) + enemy) % countof(enemyX);
			double safeX = std::max(32.0, room.halfWidth - room.cornerCut - 20.0);
			double safeY = std::max(32.0, room.halfHeight - room.cornerCut - 20.0);
			x += clamp(enemyX[pattern], -safeX, safeX);
			y += clamp(enemyY[pattern], -safeY, safeY);
			int angle = (int)lround(atan2(targetY - y, targetX - x) *
				(180.0 / 3.14159265358979323846));
			if (angle < 0) angle += 360;
			AddThing(x, y, ChooseMonster(room, enemy), angle);
		}

		// Dense role-aware decoration. Solid props are checked against every
		// gameplay thing already placed and live along chamber corners and wall
		// bays, never in a portal center or landmark pad. Corpse props are
		// non-solid but still keep a respectful distance from pickups and actors.
		auto DecorationSpotClear = [&](double x, double y, double clearance) -> bool
		{
			for (const auto& thing : things)
			{
				double dx = thing.x - x;
				double dy = thing.y - y;
				if (dx * dx + dy * dy < clearance * clearance) return false;
			}
			return true;
		};
		auto DecorationBlocksPassage = [&](double x, double y) -> bool
		{
			for (const auto& line : lines)
			{
				if (line.sideBack < 0 || line.blocking) continue;
				const int frontSector = sides[line.sideFront].sector;
				const int backSector = sides[line.sideBack].sector;
				if (frontSector == backSector) continue;
				const BuildSector& front = sectors[frontSector];
				const BuildSector& back = sectors[backSector];
				const bool operableDoor = line.special == 12;
				const bool operableLift = line.special == 62;
				auto UsesLandmarkTrim = [](const BuildSide& side) -> bool
				{
					return side.top.Compare("STEP1") == 0 || side.bottom.Compare("STEP1") == 0 ||
						side.top.Compare("EXITDOOR") == 0 || side.bottom.Compare("EXITDOOR") == 0;
				};
				const bool landmarkStep = line.special == 0 &&
					(UsesLandmarkTrim(sides[line.sideFront]) ||
						UsesLandmarkTrim(sides[line.sideBack]));
				const double approachDepth = landmarkStep ? 40.0 : 112.0;
				const double apertureMargin = landmarkStep ? 12.0 : 28.0;
				const double opening = std::min(front.ceilZ, back.ceilZ) -
					std::max(front.floorZ, back.floorZ);
				if (!operableDoor && !operableLift &&
					(opening < 56.0 || fabs(front.floorZ - back.floorZ) > 24.0))
					continue;

				const BuildVertex& first = vertices[line.v1];
				const BuildVertex& second = vertices[line.v2];
				const double dx = second.x - first.x;
				const double dy = second.y - first.y;
				const double length = hypot(dx, dy);
				if (length < 0.001) continue;
				const double unitX = dx / length;
				const double unitY = dy / length;
				const double relativeX = x - first.x;
				const double relativeY = y - first.y;
				const double along = relativeX * unitX + relativeY * unitY;
				const double normal = fabs(relativeX * unitY - relativeY * unitX);
				if (along >= -apertureMargin && along <= length + apertureMargin &&
					normal <= approachDepth)
					return true;
			}
			return false;
		};
		auto PlaceDecoration = [&](int type, bool solid, int salt) -> bool
		{
			static const double decorX[] = {
				-112.0, 112.0, 112.0, -112.0,
				-112.0, 0.0, 112.0, 0.0,
				-80.0, 80.0, 80.0, -80.0
			};
			static const double decorY[] = {
				112.0, 112.0, -112.0, -112.0,
				0.0, 112.0, 0.0, -112.0,
				80.0, 80.0, -80.0, -80.0
			};
			int attempts = (int)placementCells.Size() * countof(decorX);
			for (int attempt = 0; attempt < attempts; attempt++)
			{
				int placementIndex = (attempt / countof(decorX)) % (int)placementCells.Size();
				int cellIndex = placementCells[placementIndex];
				int corner = (attempt + salt) % countof(decorX);
				double x, y;
				CellPosition(cellIndex, x, y);
				double safeX = std::max(32.0, room.halfWidth - room.cornerCut - 18.0);
				double safeY = std::max(32.0, room.halfHeight - room.cornerCut - 18.0);
				x += clamp(decorX[corner], -safeX, safeX);
				y += clamp(decorY[corner], -safeY, safeY);
				if (!DecorationSpotClear(x, y, solid ? 40.0 : 26.0)) continue;
				if (solid && DecorationBlocksPassage(x, y)) continue;
				AddThing(x, y, type, (corner * 90 + 45) % 360);
				return true;
			}
			// Dense finales can occupy every traditional Doom corner slot. Search a
			// finer deterministic wall-bay lattice before dropping a mandatory theme
			// landmark; the same actor and portal-clearance checks still apply.
			for (unsigned int placementIndex = 0; placementIndex < placementCells.Size(); placementIndex++)
			{
				double centerX, centerY;
				CellPosition(placementCells[placementIndex], centerX, centerY);
				double safeX = std::max(32.0, room.halfWidth - room.cornerCut - 18.0);
				double safeY = std::max(32.0, room.halfHeight - room.cornerCut - 18.0);
				for (int row = -3; row <= 3; row++)
				{
					for (int column = -3; column <= 3; column++)
					{
						if (abs(row) < 2 && abs(column) < 2) continue;
						double x = centerX + safeX * column / 3.0;
						double y = centerY + safeY * row / 3.0;
						if (!DecorationSpotClear(x, y, solid ? 40.0 : 26.0)) continue;
						if (solid && DecorationBlocksPassage(x, y)) continue;
						AddThing(x, y, type, ((column - row + 8) * 45) % 360);
						return true;
					}
				}
			}
			return false;
		};

		const ThemeStyle themeStyle = GetThemeStyle(Theme);
		const bool corruptedInfernal = themeStyle == ThemeCorrupted &&
			(room.lockStage >= 2 || room.monsterTier >= 4);
		const bool infernalDecor = themeStyle == ThemeHell || themeStyle == ThemeGothic ||
			corruptedInfernal;
		const bool doom2Roster = (gameinfo.flags & GI_MAPxx) != 0;
		bool majorLandmark = room.hasPlayerStart || room.hasKey || room.hasExit ||
			room.isHub || room.isArena || room.isSecret;
		int decorationCount = majorLandmark ? std::min(8, 4 + room.cellCount / 2) :
			(1 + abs(room.id * 5 + room.progressionRank + room.branchDepth) % 3);
		if (Detail == 0) decorationCount = std::max(1, (decorationCount + 1) / 2);
		else if (Detail == 2) decorationCount = std::min(12,
			decorationCount + 2 + room.cellCount / 3);
		if (themeStyle == ThemeGothic && majorLandmark) decorationCount = std::min(12, decorationCount + 2);
		else if (themeStyle == ThemeIndustrial && room.isHub) decorationCount = std::min(12, decorationCount + 2);
		if (decorationCount > 0)
		{
			if (!infernalDecor)
			{
				int primary = doom2Roster ? (room.isArena ? 86 : 85) :
					(room.isArena ? 2028 : 48);
				if (themeStyle == ThemeIndustrial)
					primary = doom2Roster ? 86 : 48;
				if (outdoorRooms[ri]) primary = doom2Roster ? 85 : 48;
				for (int decor = 0; decor < decorationCount; decor++)
				{
					int type = primary;
					if (themeStyle == ThemeIndustrial && decor > 0)
					{
						const int machineryPart = decor % 4;
						if (machineryPart == 1) type = 48; // tall tech column
						else if (machineryPart == 2) type = 2035; // machinery barrel
						else if (machineryPart == 3) type = doom2Roster ? 85 : 2028;
					}
					else if (themeStyle == ThemeCorrupted && decor > 0 && (decor & 1))
						type = room.monsterTier >= 3 ? 56 : 55;
					else if (themeStyle == ThemeTechbase && decor > 0 && decor % 3 == 2)
						type = room.isArena ? 48 : (doom2Roster ? 86 : 2028);
					PlaceDecoration(type, true, room.id + decor * 2);
				}
			}
			else
			{
				int primary;
				if (room.hasKey && room.keyType == 2) primary = 44; // blue
				else if (room.hasKey && room.keyType == 1) primary = 46; // red
				else if (room.hasKey && room.keyType == 3) primary = 35; // yellow/gold
				else if (room.hasExit) primary = 41; // evil eye finale marker
				else if (room.isSecret || themeStyle == ThemeGothic) primary = 35; // candelabra
				else if (outdoorRooms[ri]) primary = 43; // torch tree
				else if (room.monsterTier <= 2) primary = 55; // short blue torch
				else if (room.monsterTier <= 4) primary = 56; // short green torch
				else primary = 57; // short red torch
				for (int decor = 0; decor < decorationCount; decor++)
				{
					int type = outdoorRooms[ri] && decor > 0 ? 43 : primary;
					if (themeStyle == ThemeGothic && !outdoorRooms[ri] && decor > 0)
						type = (decor & 1) ? 45 : 35;
					else if (themeStyle == ThemeHell && !outdoorRooms[ri] && decor > 0 &&
						decor % 3 == 0)
						type = 35;
					PlaceDecoration(type, true, room.id + decor * 2);
				}
			}
		}

		if (!room.hasPlayerStart && room.enemyCount >= 2 &&
			(room.isArena || room.isSecret || ((room.id + room.branchDepth) % 3) == 0))
		{
			int corpse = infernalDecor ? 20 : 15; // dead imp / dead marine
			PlaceDecoration(corpse, false, room.id + 3);
			if (majorLandmark && room.enemyCount >= 4)
				PlaceDecoration(corpse, false, room.id + 9);
		}

		if (room.hasExit)
		{
			CellPosition(exitCell >= 0 ? exitCell : 0, anchorX, anchorY);
			// Exit_Normal with explicit walk activation. Both sides reference the
			// same sector, as is standard for an interior walkover trigger.
			int triggerSector = landmarkSector >= 0 && landmarkCell == (exitCell >= 0 ? exitCell : 0) ?
				landmarkSector : room.sectorIdx;
			AddLine(anchorX - 48.0, anchorY, anchorX + 48.0, anchorY,
				triggerSector, triggerSector,
				nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
				false, 243, 0, 0, 0, 0, 0, 0,
				false, true, false);
		}
	}

	FString& output = UDMFBuffer;
	output = "namespace = \"zdoom\";\n\n";

	for (const auto& vertex : vertices)
	{
		output.AppendFormat(
			"vertex\n{\n\tx = %.2f;\n\ty = %.2f;\n}\n\n",
			vertex.x, vertex.y);
	}

	for (const auto& sector : sectors)
	{
		output.AppendFormat(
			"sector\n{\n\theightfloor = %.0f;\n\theightceiling = %.0f;\n"
			"\ttexturefloor = \"%s\";\n\ttextureceiling = \"%s\";\n\tlightlevel = %d;\n",
			sector.floorZ, sector.ceilZ, sector.floorTex.GetChars(),
			sector.ceilTex.GetChars(), sector.light);
		if (sector.id > 0) output.AppendFormat("\tid = %d;\n", sector.id);
		if (sector.special > 0) output.AppendFormat("\tspecial = %d;\n", sector.special);
		if (sector.lightColor != 0xffffff)
			output.AppendFormat("\tlightcolor = %d;\n", sector.lightColor);
		if (sector.fadeColor != 0)
			output.AppendFormat("\tfadecolor = %d;\n", sector.fadeColor);
		if (sector.damageAmount > 0)
		{
			output.AppendFormat("\tdamageamount = %d;\n", sector.damageAmount);
			output.AppendFormat("\tdamageinterval = %d;\n", sector.damageInterval);
			if (!sector.damageType.IsEmpty())
				output.AppendFormat("\tdamagetype = \"%s\";\n", sector.damageType.GetChars());
			if (sector.leakiness > 0)
				output.AppendFormat("\tleakiness = %d;\n", sector.leakiness);
			if (sector.damageTerrainEffect)
				output += "\tdamageterraineffect = true;\n";
		}
		output += "}\n\n";
	}

	for (const auto& side : sides)
	{
		output.AppendFormat("sidedef\n{\n\tsector = %d;\n", side.sector);
		if (side.top.Compare("-") != 0) output.AppendFormat("\ttexturetop = \"%s\";\n", side.top.GetChars());
		if (side.middle.Compare("-") != 0) output.AppendFormat("\ttexturemiddle = \"%s\";\n", side.middle.GetChars());
		if (side.bottom.Compare("-") != 0) output.AppendFormat("\ttexturebottom = \"%s\";\n", side.bottom.GetChars());
		if (fabs(side.scaleYTop - 1.0) > 0.000001)
			output.AppendFormat("\tscaley_top = %.6f;\n", side.scaleYTop);
		if (fabs(side.scaleXMid - 1.0) > 0.000001)
			output.AppendFormat("\tscalex_mid = %.6f;\n", side.scaleXMid);
		if (fabs(side.scaleYMid - 1.0) > 0.000001)
			output.AppendFormat("\tscaley_mid = %.6f;\n", side.scaleYMid);
		output.AppendFormat("\toffsetx = %d;\n\toffsety = %d;\n}\n\n", side.offsetX, side.offsetY);
	}

	for (const auto& line : lines)
	{
		output.AppendFormat(
			"linedef\n{\n\tv1 = %d;\n\tv2 = %d;\n\tsidefront = %d;\n",
			line.v1, line.v2, line.sideFront);
		if (line.sideBack >= 0)
		{
			output.AppendFormat("\tsideback = %d;\n\ttwosided = true;\n", line.sideBack);
		}
		if (line.blocking) output += "\tblocking = true;\n";
		if (line.dontPegTop) output += "\tdontpegtop = true;\n";
		if (line.dontPegBottom) output += "\tdontpegbottom = true;\n";
		if (line.playerUse) output += "\tplayeruse = true;\n";
		if (line.playerCross) output += "\tplayercross = true;\n";
		if (line.repeatSpecial) output += "\trepeatspecial = true;\n";
		if (line.blockMonsters) output += "\tblockmonsters = true;\n";
		if (line.secret) output += "\tsecret = true;\n";
		if (line.special > 0)
		{
			output.AppendFormat("\tspecial = %d;\n", line.special);
			for (int arg = 0; arg < 5; arg++)
				output.AppendFormat("\targ%d = %d;\n", arg, line.args[arg]);
		}
		if (line.lockNumber > 0) output.AppendFormat("\tlocknumber = %d;\n", line.lockNumber);
		output += "}\n\n";
	}

	for (const auto& thing : things)
	{
		output.AppendFormat(
			"thing\n{\n\tx = %.2f;\n\ty = %.2f;\n\tangle = %d;\n\ttype = %d;\n",
			thing.x, thing.y, thing.angle, thing.type);
		if (thing.ambush) output += "\tambush = true;\n";
		output += "\tskill1 = true;\n\tskill2 = true;\n\tskill3 = true;\n"
			"\tskill4 = true;\n\tskill5 = true;\n\tsingle = true;\n"
			"\tcoop = true;\n\tdm = true;\n}\n\n";
	}

	return vertices.Size() > 0 && sectors.Size() > 0 && lines.Size() > 0;
}
