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
		int special = 0;
		int id = 0;
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
		double halfWidth = 48.0;
		bool door = false;
		bool secret = false;
		int lockType = 0;
	};

	struct CellConnections
	{
		ConnectionRef refs[4];
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

	TArray<BuildVertex> vertices;
	TArray<BuildSector> sectors;
	TArray<BuildSide> sides;
	TArray<BuildLine> lines;
	TArray<BuildThing> things;

	auto AddVertex = [&](double x, double y) -> int
	{
		for (unsigned int i = 0; i < vertices.Size(); i++)
		{
			if (fabs(vertices[i].x - x) < 0.001 && fabs(vertices[i].y - y) < 0.001)
				return i;
		}
		BuildVertex vertex;
		vertex.x = x;
		vertex.y = y;
		vertices.Push(vertex);
		return vertices.Size() - 1;
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
		int lineIndex = AddLine(x1, y1, x2, y2, sector, -1,
			nullptr, texture, nullptr, nullptr, nullptr, nullptr,
			true, 0, 0, 0, 0, 0, 0, 0, false, false, false, false, true);
		if (lineIndex >= 0)
		{
			int sideIndex = lines[lineIndex].sideFront;
			sides[sideIndex].offsetY = -(int)lround(sectors[sector].floorZ);
		}
		return lineIndex;
	};

	auto AddSwitchWall = [&](double x1, double y1, double x2, double y2,
		int sector, int targetTag) -> int
	{
		const char* texture = Theme.Compare("hell") == 0 ? "SW1GARG" : "SW1COMP";
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

	auto CellCenterX = [&](int x) -> double
	{
		return ((x + 0.5) - W / 2.0) * CELL_SIZE;
	};
	auto CellCenterY = [&](int y) -> double
	{
		return ((y + 0.5) - H / 2.0) * CELL_SIZE;
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

	// Expose several landmarks to the sky. Every map receives both an outdoor
	// finale and at least one additional open combat space; colossal maps can
	// alternate indoor routes with a much broader courtyard cadence.
	TArray<bool> outdoorRooms;
	outdoorRooms.Resize(Rooms.Size());
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++) outdoorRooms[ri] = false;
	int outdoorBudget = 2 + Size / 2;
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
	TArray<int> keyTriggerTags;
	TArray<int> perchTags;
	TArray<int> perchCellX;
	TArray<int> perchCellY;
	TArray<int> perchApproachSides;
	TArray<int> liftTags;
	TArray<int> liftCellX;
	TArray<int> liftCellY;
	revealKinds.Resize(Rooms.Size());
	revealTags.Resize(Rooms.Size());
	revealBorderTypes.Resize(Rooms.Size());
	revealCellX.Resize(Rooms.Size());
	revealCellY.Resize(Rooms.Size());
	revealDoorSides.Resize(Rooms.Size());
	revealProfileAdjustX.Resize(Rooms.Size());
	revealProfileAdjustY.Resize(Rooms.Size());
	keyTriggerTags.Resize(Rooms.Size());
	perchTags.Resize(Rooms.Size());
	perchCellX.Resize(Rooms.Size());
	perchCellY.Resize(Rooms.Size());
	perchApproachSides.Resize(Rooms.Size());
	liftTags.Resize(Rooms.Size());
	liftCellX.Resize(Rooms.Size());
	liftCellY.Resize(Rooms.Size());
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		revealKinds[ri] = RevealNone;
		revealTags[ri] = 0;
		revealBorderTypes[ri] = 0;
		revealCellX[ri] = revealCellY[ri] = -1;
		revealDoorSides[ri] = -1;
		revealProfileAdjustX[ri] = revealProfileAdjustY[ri] = 0.0;
		keyTriggerTags[ri] = 0;
		perchTags[ri] = 0;
		perchCellX[ri] = perchCellY[ri] = -1;
		perchApproachSides[ri] = -1;
		liftTags[ri] = 0;
		liftCellX[ri] = liftCellY[ri] = -1;
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
		TArray<std::pair<int, int>> candidates;
		for (int y = 0; y < H; y++)
		{
			for (int x = 0; x < W; x++)
			{
				const ProcGenCell& cell = Grid[y][x];
				if (!cell.present || cell.roomId != roomId) continue;
				if (cell.hasPlayerStart || cell.hasKey || cell.hasExit || cell.hasBoss || cell.isLocked)
					continue;
				if (IsLandmarkAnchorCell(roomId, x, y)) continue;
				candidates.Push(std::make_pair(x, y));
			}
		}
		if (candidates.Size() == 0) return false;
		const auto& selected = candidates[RNG() % candidates.Size()];
		featureX = selected.first;
		featureY = selected.second;
		return true;
	};
	auto PickPerchCell = [&](int roomId, int& featureX, int& featureY) -> bool
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
				if (x == revealCellX[roomId] && y == revealCellY[roomId]) continue;
				if (IsLandmarkAnchorCell(roomId, x, y)) continue;
				candidates.Push(std::make_pair(x, y));
			}
		}
		if (candidates.Size() == 0) return false;
		const auto& selected = candidates[RNG() % candidates.Size()];
		featureX = selected.first;
		featureY = selected.second;
		return true;
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

	// At least one key shrine becomes a deterministic-random ambush. Additional
	// keys have an independent chance to reveal their own monster closet.
	TArray<int> keyRooms;
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		if (Rooms[ri].hasKey) keyRooms.Push(ri);
	ShuffleRooms(keyRooms);
	int nextKeyTrapTag = 1000;
	bool assignedKeyTrap = false;
	for (unsigned int index = 0; index < keyRooms.Size(); index++)
	{
		const int keyRoomId = keyRooms[index];
		if (assignedKeyTrap && (RNG() % 100) >= 60) continue;
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
	for (int pass = 0; pass < 2; pass++)
	{
		for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		{
			const RoomInfo& room = Rooms[ri];
			if (revealKinds[ri] != RevealNone || !CanHostReveal(ri, RevealSwitchCache) ||
				room.hasPlayerStart ||
				room.hasKey || room.isLocked || room.isSecret)
				continue;
			if ((pass == 0 && room.isArena) || (pass == 1 && !room.isArena)) continue;
			if (room.isArena || room.isHub || room.isDeadEnd || room.onMainPath)
				switchRooms.Push(ri);
		}
		if (switchRooms.Size() >= (unsigned int)(1 + Size / 6)) break;
	}
	ShuffleRooms(switchRooms);
	int nextSwitchTag = 1500;
	int switchBudget = 1 + Size / 6;
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
		revealDoorSides[roomId] = ChooseRevealDoorSide(roomId,
			featureX, featureY, RevealSwitchCache);
		switchBudget--;
	}
	if (nextSwitchTag == 1500)
	{
		LastError = "Could not place a switch-operated reveal chamber";
		return false;
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

	// Three or more interactive pavilions should not read as copies placed on
	// the same axis. Prefer rotating one toward open composed-room space; the
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
			if (revealKinds[ri] == RevealNone) continue;
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
	const int perchBudgetTarget = 1 + Size / 4;
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
				room.hasKey || room.isLocked ||
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
	auto IsLiftCellCandidate = [&](int roomId, int x, int y) -> bool
	{
		const ProcGenCell& cell = Grid[y][x];
		if (!cell.present || cell.roomId != roomId || cell.hasPlayerStart ||
			cell.hasKey || cell.hasExit || cell.hasBoss || cell.isLocked)
			return false;
		return !((x == revealCellX[roomId] && y == revealCellY[roomId]) ||
			(x == perchCellX[roomId] && y == perchCellY[roomId]) ||
			IsLandmarkAnchorCell(roomId, x, y));
	};
	auto HasLiftCell = [&](int roomId) -> bool
	{
		for (int y = 0; y < H; y++)
		{
			for (int x = 0; x < W; x++)
				if (IsLiftCellCandidate(roomId, x, y)) return true;
		}
		return false;
	};
	auto PickLiftCell = [&](int roomId, int& featureX, int& featureY) -> bool
	{
		TArray<std::pair<int, int>> candidates;
		for (int y = 0; y < H; y++)
		{
			for (int x = 0; x < W; x++)
			{
				if (!IsLiftCellCandidate(roomId, x, y)) continue;
				candidates.Push(std::make_pair(x, y));
			}
		}
		if (candidates.Size() == 0) return false;
		const auto& selected = candidates[RNG() % candidates.Size()];
		featureX = selected.first;
		featureY = selected.second;
		return true;
	};
	TArray<int> liftRooms;
	auto HasLiftRoom = [&](int roomId) -> bool
	{
		for (unsigned int index = 0; index < liftRooms.Size(); index++)
			if (liftRooms[index] == roomId) return true;
		return false;
	};
	const int liftBudgetTarget = 1 + Size / 8;
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
		if (room.isSecret) sectors[room.sectorIdx].special = 9;
	}

	TArray<TArray<CellConnections>> connectionGrid;
	connectionGrid.Resize(H);
	for (int y = 0; y < H; y++) connectionGrid[y].Resize(W);

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

				bool secretDoor = roomA != roomB &&
					(Rooms[roomA].isSecret || Rooms[roomB].isSecret);
				bool door = lockType > 0 || secretDoor;
				// The start landmark is a guaranteed safe staging area. Its own
				// encounter budget is zero, and closed unlocked doors prevent
				// monsters in the first combat room from immediately flooding it.
				if (!door && roomA != roomB &&
					(Rooms[roomA].hasPlayerStart || Rooms[roomB].hasPlayerStart))
				{
					door = true;
					if (normalDoorBudget > 0) normalDoorBudget--;
				}
				if (!door && roomA != roomB && normalDoorBudget > 0 && !PairHasDoor(roomA, roomB))
				{
					bool requested = Rooms[roomA].hasDoor || Rooms[roomB].hasDoor ||
						Rooms[roomA].hasKey || Rooms[roomB].hasKey;
					if (requested || ((Rooms[roomA].isArena || Rooms[roomB].isArena) && (RNG() % 100) < 18))
					{
						door = true;
						normalDoorBudget--;
					}
				}
				if (door && roomA != roomB) RecordDoorPair(roomA, roomB);

				double halfWidth = door ? 64.0 : 72.0;
				if (!door && (Rooms[roomA].isArena || Rooms[roomB].isArena)) halfWidth = 96.0;
				else if (!door && (Rooms[roomA].isHub || Rooms[roomB].isHub)) halfWidth = 88.0;
				else if (!door && (Rooms[roomA].branchDepth >= 2 || Rooms[roomB].branchDepth >= 2)) halfWidth = 64.0;
				double apertureHalf = direction == DIR_E ?
					std::min(roomHalfY[roomA], roomHalfY[roomB]) :
					std::min(roomHalfX[roomA], roomHalfX[roomB]);
				if (roomA == roomB && !door) halfWidth = apertureHalf;
				else
				{
					double connectionCut = std::max(Rooms[roomA].cornerCut, Rooms[roomB].cornerCut);
					halfWidth = std::min(halfWidth, apertureHalf - connectionCut);
				}

				int connectionSector = -1;
				int doorSector = -1;
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
						(Theme.Compare("hell") == 0 ? "FLAT5_1" : "CEIL3_5");
					int light = clamp((Rooms[roomA].light + Rooms[roomB].light) / 2, 160, 208);
					if (sky) light = std::max(light, 192);
					if (door)
					{
						doorSector = AddSector(floorZ, floorZ,
							SafeTexture(Rooms[roomA].floorTex, "FLOOR4_8"), ceiling, light);
					}
					else
					{
						connectionSector = AddSector(floorZ, openCeil,
							SafeTexture(Rooms[roomA].floorTex, "FLOOR4_8"), ceiling, light);
					}
				}

				ConnectionRef refA;
				refA.sector = door ? Rooms[roomA].sectorIdx : connectionSector;
				refA.doorSector = doorSector;
				refA.halfWidth = halfWidth;
				refA.door = door;
				refA.secret = secretDoor;
				refA.lockType = lockType;
				ConnectionRef refB = refA;
				refB.sector = door ? Rooms[roomB].sectorIdx : connectionSector;
				connectionGrid[y][x].refs[direction] = refA;
				connectionGrid[ny][nx].refs[OPP[direction]] = refB;
			}
		}
	}

	auto DoorTexture = [&](int lockType) -> const char*
	{
		if (Theme.Compare("hell") == 0)
			return lockType > 0 ? "MARBFAC3" : "MARBFAC2";
		if (lockType == 1) return "BIGDOOR2";
		if (lockType == 2) return "BIGDOOR3";
		if (lockType == 3) return "BIGDOOR4";
		return "BIGDOOR1";
	};

	auto DoorTrackTexture = [&](int lockType) -> const char*
	{
		if (lockType == 1) return "DOORRED";
		if (lockType == 2) return "DOORBLU";
		if (lockType == 3) return "DOORYEL";
		return "DOORTRAK";
	};

	auto AddDoorFace = [&](double x1, double y1, double x2, double y2,
		int roomSector, int doorSector, int lockType, bool secretDoor, const char* roomWall)
	{
		const char* doorTexture = secretDoor ? roomWall : DoorTexture(lockType);
		// The upper texture is deliberately pegged to the moving door ceiling;
		// unlike the tracks, the door face must rise with the sector.
		int lineIndex = AddLine(x1, y1, x2, y2, roomSector, doorSector,
			doorTexture, nullptr, roomWall,
			doorTexture, nullptr, roomWall,
			false, 12, lockType, 0, 16, 150, 0, 0,
			true, false, true, false, false);
		if (lineIndex >= 0)
		{
			lines[lineIndex].secret = secretDoor;
			const double faceWidth = hypot(x2 - x1, y2 - y1);
			const double faceHeight = std::max(1.0,
				sectors[roomSector].ceilZ - sectors[doorSector].floorZ);
			const double fittedYScale = std::min(1.0, 128.0 / faceHeight);
			sides[lines[lineIndex].sideFront].scaleYTop = fittedYScale;
			sides[lines[lineIndex].sideBack].scaleYTop = fittedYScale;
			// Center stock 128-wide motifs even when a narrow room profile trims
			// the doorway aperture.
			if (!secretDoor)
			{
				int crop = (int)lround(std::max(0.0, (128.0 - faceWidth) * 0.5));
				sides[lines[lineIndex].sideFront].offsetX = crop;
				sides[lines[lineIndex].sideBack].offsetX = crop;
			}
		}
	};

	auto AddPortal = [&](double x1, double y1, double x2, double y2,
		int roomSector, const ConnectionRef& ref, const char* roomWall)
	{
		if (ref.sector < 0 || ref.sector == roomSector) return;
		if (!ref.door)
		{
			AddLine(x1, y1, x2, y2, roomSector, ref.sector,
				roomWall, nullptr, roomWall,
				roomWall, nullptr, roomWall,
				false, 0, 0, 0, 0, 0, 0, 0,
				false, false, false, true, true);
		}
	};

	TArray<bool> switchWallEmitted;
	switchWallEmitted.Resize(Rooms.Size());
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++) switchWallEmitted[ri] = false;
	auto AddChamberWall = [&](int roomId, double x1, double y1, double x2, double y2,
		int sector, const char* texture, bool switchEligible)
	{
		const double length = hypot(x2 - x1, y2 - y1);
		if (switchEligible && revealKinds[roomId] == RevealSwitchCache &&
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
				sector, revealTags[roomId]) >= 0)
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
			const char* cornerWall = SafeTexture(room.accentTex, wall);
			double cx = CellCenterX(x);
			double cy = CellCenterY(y);
			double halfX = HalfXForCell(x, y);
			double halfY = HalfYForCell(x, y);
			double left = cx - halfX;
			double right = cx + halfX;
			double bottom = cy - halfY;
			double top = cy + halfY;

			const ConnectionRef& topRef = connectionGrid[y][x].refs[DIR_S];
			const ConnectionRef& rightRef = connectionGrid[y][x].refs[DIR_E];
			const ConnectionRef& bottomRef = connectionGrid[y][x].refs[DIR_N];
			const ConnectionRef& leftRef = connectionGrid[y][x].refs[DIR_W];
			bool topFull = topRef.sector == roomSector;
			bool rightFull = rightRef.sector == roomSector;
			bool bottomFull = bottomRef.sector == roomSector;
			bool leftFull = leftRef.sector == roomSector;

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
				AddChamberWall(cell.roomId, topLeft, top, cx - topRef.halfWidth, top,
					roomSector, wall, true);
				AddPortal(cx - topRef.halfWidth, top, cx + topRef.halfWidth, top,
					roomSector, topRef, wall);
				AddChamberWall(cell.roomId, cx + topRef.halfWidth, top, topRight, top,
					roomSector, wall, true);
			}
			else AddChamberWall(cell.roomId, topLeft, top, topRight, top, roomSector, wall, true);
			AddWall(topRight, top, right, rightTop, roomSector, cornerWall);

			// East edge: top -> bottom.
			if (rightRef.sector >= 0)
			{
				AddChamberWall(cell.roomId, right, rightTop, right, cy + rightRef.halfWidth,
					roomSector, wall, true);
				AddPortal(right, cy + rightRef.halfWidth, right, cy - rightRef.halfWidth,
					roomSector, rightRef, wall);
				AddChamberWall(cell.roomId, right, cy - rightRef.halfWidth, right, rightBottom,
					roomSector, wall, true);
			}
			else AddChamberWall(cell.roomId, right, rightTop, right, rightBottom,
				roomSector, wall, true);
			AddWall(right, rightBottom, bottomRight, bottom, roomSector, cornerWall);

			// South/world-bottom edge: right -> left (grid DIR_N).
			if (bottomRef.sector >= 0)
			{
				AddChamberWall(cell.roomId, bottomRight, bottom, cx + bottomRef.halfWidth,
					bottom, roomSector, wall, true);
				AddPortal(cx + bottomRef.halfWidth, bottom, cx - bottomRef.halfWidth, bottom,
					roomSector, bottomRef, wall);
				AddChamberWall(cell.roomId, cx - bottomRef.halfWidth, bottom, bottomLeft,
					bottom, roomSector, wall, true);
			}
			else AddChamberWall(cell.roomId, bottomRight, bottom, bottomLeft, bottom,
				roomSector, wall, true);
			AddWall(bottomLeft, bottom, left, leftBottom, roomSector, cornerWall);

			// West edge: bottom -> top.
			if (leftRef.sector >= 0)
			{
				AddChamberWall(cell.roomId, left, leftBottom, left, cy - leftRef.halfWidth,
					roomSector, wall, true);
				AddPortal(left, cy - leftRef.halfWidth, left, cy + leftRef.halfWidth,
					roomSector, leftRef, wall);
				AddChamberWall(cell.roomId, left, cy + leftRef.halfWidth, left, leftTop,
					roomSector, wall, true);
			}
			else AddChamberWall(cell.roomId, left, leftBottom, left, leftTop,
				roomSector, wall, true);
			AddWall(left, leftTop, topLeft, top, roomSector, cornerWall);
		}
	}
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		if (revealKinds[ri] == RevealSwitchCache && !switchWallEmitted[ri])
		{
			LastError = "Could not place a procedural switch panel";
			return false;
		}
	}

	// Corridor side walls complete the union between chamber openings. End
	// portals were emitted above and share deduplicated vertices with these.
	const char* corridorWall = Theme.Compare("hell") == 0 ? "GSTVINE1" : "SUPPORT2";
	for (int y = 0; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			if (!Grid[y][x].present) continue;

			const ConnectionRef& east = connectionGrid[y][x].refs[DIR_E];
			if (east.sector >= 0 && x + 1 < W && Grid[y][x + 1].present)
			{
				double x1 = CellCenterX(x) + HalfXForCell(x, y);
				double x2 = CellCenterX(x + 1) - HalfXForCell(x + 1, y);
				double cy = CellCenterY(y);
				if (east.door && east.doorSector >= 0)
				{
					int roomA = Grid[y][x].roomId;
					int roomB = Grid[y][x + 1].roomId;
					double mid = (x1 + x2) * 0.5;
					double doorLeft = mid - 8.0;
					double doorRight = mid + 8.0;
					double top = cy + east.halfWidth;
					double bottom = cy - east.halfWidth;
					const char* track = east.secret ? corridorWall : DoorTrackTexture(east.lockType);
					const char* jamb = east.lockType > 0 ? track : corridorWall;

					// Static recessed jambs flank a classic 16-unit moving door. Keyed
					// portals extend their color along all four approach borders.
					AddWall(x1, top, doorLeft, top, Rooms[roomA].sectorIdx, jamb);
					AddWall(doorLeft, top, doorRight, top, east.doorSector, track);
					AddWall(doorRight, top, x2, top, Rooms[roomB].sectorIdx, jamb);
					AddWall(doorLeft, bottom, x1, bottom, Rooms[roomA].sectorIdx, jamb);
					AddWall(doorRight, bottom, doorLeft, bottom, east.doorSector, track);
					AddWall(x2, bottom, doorRight, bottom, Rooms[roomB].sectorIdx, jamb);

					AddDoorFace(doorLeft, top, doorLeft, bottom,
						Rooms[roomA].sectorIdx, east.doorSector, east.lockType, east.secret,
						SafeTexture(Rooms[roomA].wallTex, "STARTAN3"));
					AddDoorFace(doorRight, bottom, doorRight, top,
						Rooms[roomB].sectorIdx, east.doorSector, east.lockType, east.secret,
						SafeTexture(Rooms[roomB].wallTex, "STARTAN3"));
				}
				else
				{
					AddWall(x1, cy + east.halfWidth, x2, cy + east.halfWidth,
						east.sector, corridorWall);
					AddWall(x2, cy - east.halfWidth, x1, cy - east.halfWidth,
						east.sector, corridorWall);
				}
			}

			const ConnectionRef& north = connectionGrid[y][x].refs[DIR_S];
			if (north.sector >= 0 && y + 1 < H && Grid[y + 1][x].present)
			{
				double y1 = CellCenterY(y) + HalfYForCell(x, y);
				double y2 = CellCenterY(y + 1) - HalfYForCell(x, y + 1);
				double cx = CellCenterX(x);
				if (north.door && north.doorSector >= 0)
				{
					int roomA = Grid[y][x].roomId;
					int roomB = Grid[y + 1][x].roomId;
					double mid = (y1 + y2) * 0.5;
					double doorBottom = mid - 8.0;
					double doorTop = mid + 8.0;
					double left = cx - north.halfWidth;
					double right = cx + north.halfWidth;
					const char* track = north.secret ? corridorWall : DoorTrackTexture(north.lockType);
					const char* jamb = north.lockType > 0 ? track : corridorWall;

					AddWall(right, doorBottom, right, y1, Rooms[roomA].sectorIdx, jamb);
					AddWall(right, doorTop, right, doorBottom, north.doorSector, track);
					AddWall(right, y2, right, doorTop, Rooms[roomB].sectorIdx, jamb);
					AddWall(left, y1, left, doorBottom, Rooms[roomA].sectorIdx, jamb);
					AddWall(left, doorBottom, left, doorTop, north.doorSector, track);
					AddWall(left, doorTop, left, y2, Rooms[roomB].sectorIdx, jamb);

					AddDoorFace(left, doorBottom, right, doorBottom,
						Rooms[roomA].sectorIdx, north.doorSector, north.lockType, north.secret,
						SafeTexture(Rooms[roomA].wallTex, "STARTAN3"));
					AddDoorFace(right, doorTop, left, doorTop,
						Rooms[roomB].sectorIdx, north.doorSector, north.lockType, north.secret,
						SafeTexture(Rooms[roomB].wallTex, "STARTAN3"));
				}
				else
				{
					AddWall(cx + north.halfWidth, y2, cx + north.halfWidth, y1,
						north.sector, corridorWall);
					AddWall(cx - north.halfWidth, y1, cx - north.halfWidth, y2,
						north.sector, corridorWall);
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
		int roomSector, int doorSector, const char* doorTexture, const char* roomWall)
	{
		int lineIndex = AddLine(x1, y1, x2, y2, roomSector, doorSector,
			doorTexture, nullptr, roomWall,
			doorTexture, nullptr, roomWall,
			false, 0, 0, 0, 0, 0, 0, 0,
			false, false, false, false, false);
		if (lineIndex < 0) return;
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
		int doorSide) -> int
	{
		const double doorHalf = 40.0;
		const char* roomWall = SafeTexture(room.wallTex, "STARTAN3");
		const char* closetWall = SafeTexture(room.accentTex, roomWall);
		const char* doorTexture = DoorTexture(borderType);
		const char* track = DoorTrackTexture(borderType);
		int closetSector = AddSector(room.floorZ, room.ceilZ,
			SafeTexture(room.floorTex, "FLOOR4_8"), SafeTexture(room.ceilTex, "CEIL3_5"),
			std::min(room.light + 8, 216));
		int doorSector = AddSector(room.floorZ, room.floorZ,
			SafeTexture(room.floorTex, "FLOOR4_8"), SafeTexture(room.ceilTex, "CEIL3_5"),
			std::min(room.light + 8, 216), targetTag);

		auto MakeChamferedLoop = [&](double halfX, double halfY,
			double chamfer) -> TArray<std::pair<double, double>>
		{
			TArray<std::pair<double, double>> points;
			points.Push(std::make_pair(cx - halfX + chamfer, cy - halfY));
			points.Push(std::make_pair(cx + halfX - chamfer, cy - halfY));
			points.Push(std::make_pair(cx + halfX, cy - halfY + chamfer));
			points.Push(std::make_pair(cx + halfX, cy + halfY - chamfer));
			points.Push(std::make_pair(cx + halfX - chamfer, cy + halfY));
			points.Push(std::make_pair(cx - halfX + chamfer, cy + halfY));
			points.Push(std::make_pair(cx - halfX, cy + halfY - chamfer));
			points.Push(std::make_pair(cx - halfX, cy - halfY + chamfer));
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
		DoorEndpoints(inner, innerAX, innerAY, innerBX, innerBY);

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
			room.sectorIdx, doorSector, doorTexture, roomWall);
		AddRemoteDoorFace(innerBX, innerBY, innerAX, innerAY,
			closetSector, doorSector, doorTexture, closetWall);
		AddWall(outerAX, outerAY, innerAX, innerAY, doorSector, track);
		AddWall(innerBX, innerBY, outerBX, outerBY, doorSector, track);
		return closetSector;
	};

	auto AddSniperPerch = [&](const RoomInfo& room, double cx, double cy,
		int perchTag, bool sky, int approachSide) -> int
	{
		const double half = 56.0;
		const double stairHalf = 40.0;
		const double stepDepth = 24.0;
		const double requestedRise = Difficulty >= 4 ? 64.0 : 48.0;
		const double raisedFloor = std::min(room.floorZ + requestedRise,
			room.ceilZ - 80.0);
		const int riseSteps = clamp((int)lround((raisedFloor - room.floorZ) / 16.0), 3, 4);
		const int stairCount = riseSteps - 1;
		const char* floor = Theme.Compare("hell") == 0 ? "FLAT5_2" : "FLAT20";
		const char* wall = SafeTexture(room.accentTex, "STEP1");
		const char* ceiling = sky ? "F_SKY1" : SafeTexture(room.ceilTex, "CEIL3_5");
		int perchLight = std::min(room.light + 16, 224);
		if (sky) perchLight = std::max(perchLight, 192);
		int perchSector = AddSector(raisedFloor, room.ceilZ, floor,
			ceiling, perchLight, perchTag);

		TArray<int> stairSectors;
		stairSectors.Resize(stairCount);
		for (int level = 0; level < stairCount; level++)
		{
			int stairLight = std::min(room.light + 4 * (level + 1), perchLight);
			if (sky) stairLight = std::max(stairLight, 192);
			stairSectors[level] = AddSector(room.floorZ + (level + 1) * 16.0,
				room.ceilZ, floor, ceiling, stairLight);
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
		auto Point = [&](double outward, double tangent,
			double& x, double& y)
		{
			x = cx + outwardX * outward + tangentX * tangent;
			y = cy + outwardY * outward + tangentY * tangent;
		};
		auto AddPerchEdge = [&](double x1, double y1, double x2, double y2,
			int frontSector, int backSector, bool retainMonster)
		{
			AddLine(x1, y1, x2, y2, frontSector, backSector,
				wall, nullptr, wall, wall, nullptr, wall,
				false, 0, 0, 0, 0, 0, 0, 0,
				false, false, false, true, true, retainMonster);
		};

		// The platform perimeter is clockwise. Split the approach edge around a
		// 80-unit opening; other edges are monster-retaining but remain transparent
		// to player movement, hitscan, and projectiles.
		for (int side = 0; side < 4; side++)
		{
			double x1, y1, x2, y2;
			if (side == approachSide)
			{
				Point(half, half, x1, y1);
				Point(half, stairHalf, x2, y2);
				AddPerchEdge(x1, y1, x2, y2,
					perchSector, room.sectorIdx, true);
				Point(half, -stairHalf, x1, y1);
				Point(half, -half, x2, y2);
				AddPerchEdge(x1, y1, x2, y2,
					perchSector, room.sectorIdx, true);
			}
			else
			{
				const double sideOutwardX = OutwardX[side];
				const double sideOutwardY = OutwardY[side];
				const double sideTangentX = TangentX[side];
				const double sideTangentY = TangentY[side];
				x1 = cx + sideOutwardX * half + sideTangentX * half;
				y1 = cy + sideOutwardY * half + sideTangentY * half;
				x2 = cx + sideOutwardX * half - sideTangentX * half;
				y2 = cy + sideOutwardY * half - sideTangentY * half;
				AddPerchEdge(x1, y1, x2, y2,
					perchSector, room.sectorIdx, true);
			}
		}

		// Lowest-to-highest 16-unit tiers. Only the long retaining sides block
		// monsters; the outer entry, every riser, and the platform connection are
		// an explicit traversable route in the serialized topology.
		for (int level = 0; level < stairCount; level++)
		{
			const double near = half + (stairCount - 1 - level) * stepDepth;
			const double far = near + stepDepth;
			double ax, ay, bx, by, cx2, cy2, dx, dy;
			Point(near, stairHalf, ax, ay);
			Point(far, stairHalf, bx, by);
			Point(far, -stairHalf, cx2, cy2);
			Point(near, -stairHalf, dx, dy);
			AddPerchEdge(ax, ay, bx, by,
				stairSectors[level], room.sectorIdx, true);
			AddPerchEdge(cx2, cy2, dx, dy,
				stairSectors[level], room.sectorIdx, true);
			if (level == 0)
				AddPerchEdge(bx, by, cx2, cy2,
					stairSectors[level], room.sectorIdx, false);
			const int higherSector = level + 1 < stairCount ?
				stairSectors[level + 1] : perchSector;
			AddPerchEdge(dx, dy, ax, ay,
				stairSectors[level], higherSector, false);
		}
		return perchSector;
	};

	auto AddLiftPlatform = [&](const RoomInfo& room, double cx, double cy,
		int liftTag, bool sky) -> int
	{
		const double half = 40.0;
		const double raisedFloor = room.floorZ + 32.0;
		const bool hell = Theme.Compare("hell") == 0;
		const char* floor = hell ? "FLAT5_1" : "FLAT20";
		const char* ceiling = sky ? "F_SKY1" : SafeTexture(room.ceilTex, "CEIL3_5");
		const int light = sky ? std::max(room.light + 16, 192) : room.light + 16;
		int liftSector = AddSector(raisedFloor, room.ceilZ, floor, ceiling,
			std::min(light, 224), liftTag);
		auto AddLiftEdge = [&](double x1, double y1, double x2, double y2)
		{
			AddLine(x1, y1, x2, y2, liftSector, room.sectorIdx,
				"PLAT1", nullptr, "PLAT1", "PLAT1", nullptr, "PLAT1",
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

	auto AddLandmarkPlatform = [&](const RoomInfo& room, double cx, double cy,
		bool sky, int crossOpenTag) -> int
	{
		double half = room.isArena ? 80.0 : 64.0;
		double raise = room.isArena ? 16.0 : 8.0;
		if (room.hasKey) { half = 64.0; raise = 16.0; }
		if (room.hasPlayerStart) { half = 64.0; raise = 8.0; }
		if (room.hasExit) { half = 96.0; raise = 16.0; }
		const bool hell = Theme.Compare("hell") == 0;
		const char* floor = hell ? "FLOOR7_2" : "FLAT20";
		if (room.hasKey) floor = hell ? "FLAT5_1" : "FLOOR0_1";
		else if (room.hasPlayerStart) floor = hell ? "FLOOR6_1" : "FLOOR5_1";
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
		int revealCell = -1;
		int perchCell = -1;
		int liftCell = -1;
		TArray<int> placementCells;
		for (unsigned int index = 0; index < roomCells.Size(); index++)
		{
			const int x = roomCells[index].first;
			const int y = roomCells[index].second;
			if (x == revealCellX[ri] && y == revealCellY[ri]) revealCell = index;
			if (x == perchCellX[ri] && y == perchCellY[ri]) perchCell = index;
			if (x == liftCellX[ri] && y == liftCellY[ri]) liftCell = index;
		}
		for (unsigned int index = 0; index < roomCells.Size(); index++)
			if ((int)index != revealCell && (int)index != perchCell && (int)index != liftCell)
				placementCells.Push(index);
		if (placementCells.Size() == 0) placementCells.Push(0);

		auto CellPosition = [&](int index, double& px, double& py)
		{
			index = clamp(index, 0, (int)roomCells.Size() - 1);
			px = CellCenterX(roomCells[index].first);
			py = CellCenterY(roomCells[index].second);
		};

		static const double slotX[] = { 0, 80, -80, 0, 0, 80, -80, 80, -80 };
		static const double slotY[] = { 0, 0, 0, 80, -80, 80, 80, -80, -80 };
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
		int landmarkCell = 0;
		if (room.hasExit && exitCell >= 0) landmarkCell = exitCell;
		else if (room.hasKey && keyCell >= 0) landmarkCell = keyCell;
		else if (room.hasPlayerStart && playerCell >= 0) landmarkCell = playerCell;

		if (revealCell >= 0 && revealKinds[ri] != RevealNone)
		{
			double revealX, revealY;
			CellPosition(revealCell, revealX, revealY);
			const RevealProfile profile = BuildRevealProfile(ri, revealKinds[ri]);
			revealX += profile.offsetX;
			revealY += profile.offsetY;
			const int doorSide = clamp(revealDoorSides[ri], 0, 3);
			AddRevealCloset(room, revealX, revealY, revealTags[ri],
				revealBorderTypes[ri], profile, doorSide);
			static const double TangentX[] = { 1.0, 0.0, -1.0, 0.0 };
			static const double TangentY[] = { 0.0, 1.0, 0.0, -1.0 };
			static const double InwardX[] = { 0.0, -1.0, 0.0, 1.0 };
			static const double InwardY[] = { 1.0, 0.0, -1.0, 0.0 };
			const double tangentHalf = (doorSide & 1) ? profile.innerY : profile.innerX;
			const double actorSpread = std::min(32.0, tangentHalf - 22.0);
			auto RevealPosition = [&](double tangent, double inward,
				double& x, double& y)
			{
				x = revealX + TangentX[doorSide] * tangent +
					InwardX[doorSide] * inward;
				y = revealY + TangentY[doorSide] * tangent +
					InwardY[doorSide] * inward;
			};
			if (revealKinds[ri] == RevealKeyTrap)
			{
				double firstX, firstY, secondX, secondY, rewardX, rewardY;
				RevealPosition(-actorSpread, 8.0, firstX, firstY);
				RevealPosition(actorSpread, 8.0, secondX, secondY);
				RevealPosition(0.0, -24.0, rewardX, rewardY);
				const int outwardAngle = doorSide == 0 ? 270 :
					(doorSide == 1 ? 0 : (doorSide == 2 ? 90 : 180));
				AddThing(firstX, firstY, ChooseRangedMonster(room, 1),
					outwardAngle, true);
				AddThing(secondX, secondY, ChooseRangedMonster(room, 2),
					outwardAngle, true);
				AddThing(rewardX, rewardY, 2011);
			}
			else
			{
				double firstX, firstY, secondX, secondY;
				RevealPosition(-actorSpread, 8.0, firstX, firstY);
				RevealPosition(actorSpread, 8.0, secondX, secondY);
				AddThing(firstX, firstY, 2008);
				AddThing(secondX, secondY, 2012);
			}
		}

		if (perchCell >= 0 && perchTags[ri] > 0)
		{
			double perchX, perchY;
			CellPosition(perchCell, perchX, perchY);
			const int approachSide = clamp(perchApproachSides[ri], 0, 3);
			AddSniperPerch(room, perchX, perchY, perchTags[ri],
				outdoorRooms[ri], approachSide);
			AddThing(perchX, perchY, ChooseRangedMonster(room, 3),
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

		if (room.cellCount >= 2 && (room.isArena || room.isHub || room.hasPlayerStart) &&
			!room.isLocked)
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
			int packs = 1 + (room.enemyCount >= 4 ? 1 : 0);
			for (int pack = 0; pack < packs; pack++)
			{
				double x, y;
				SlotPosition(rewardSlot++, x, y);
				AddThing(x, y, room.ammoType);
			}
		}
		if (room.hasHealth)
		{
			int packs = 1 + (room.enemyCount >= 4 && Difficulty >= 3 ? 1 : 0);
			for (int pack = 0; pack < packs; pack++)
			{
				double x, y;
				SlotPosition(rewardSlot++, x, y);
				AddThing(x, y, room.healthType);
			}
		}
		if (room.hasArmor)
		{
			double x, y;
			SlotPosition(rewardSlot++, x, y);
			AddThing(x, y, room.armorType);
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

		// Sparse role-aware decoration. Solid props are checked against every
		// gameplay thing already placed and live in chamber corners, never in a
		// portal center or landmark pad. Corpse props are non-solid but still
		// keep a respectful distance from pickups and actors.
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
		auto PlaceDecoration = [&](int type, bool solid, int salt) -> bool
		{
			static const double decorX[] = { -64.0, 64.0, 64.0, -64.0 };
			static const double decorY[] = { 64.0, 64.0, -64.0, -64.0 };
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
				if (!DecorationSpotClear(x, y, solid ? 44.0 : 28.0)) continue;
				AddThing(x, y, type, (corner * 90 + 45) % 360);
				return true;
			}
			return false;
		};

		const bool hellTheme = Theme.Compare("hell") == 0;
		const bool doom2Roster = (gameinfo.flags & GI_MAPxx) != 0;
		bool majorLandmark = room.hasPlayerStart || room.hasKey || room.hasExit ||
			room.isHub || room.isArena || room.isSecret;
		int decorationCount = majorLandmark ? 2 :
			(((room.id * 5 + room.progressionRank + room.branchDepth) % 5) == 0 ? 1 : 0);
		if (decorationCount > 0)
		{
			if (!hellTheme)
			{
				int primary = doom2Roster ? (room.isArena ? 86 : 85) :
					(room.isArena ? 2028 : 48);
				if (outdoorRooms[ri]) primary = doom2Roster ? 85 : 48;
				for (int decor = 0; decor < decorationCount; decor++)
					PlaceDecoration(primary, true, room.id + decor * 2);
			}
			else
			{
				int primary;
				if (room.hasKey && room.keyType == 2) primary = 44; // blue
				else if (room.hasKey && room.keyType == 1) primary = 46; // red
				else if (room.hasKey && room.keyType == 3) primary = 35; // yellow/gold
				else if (room.hasExit) primary = 41; // evil eye finale marker
				else if (room.isSecret) primary = 35; // candelabra reward cue
				else if (outdoorRooms[ri]) primary = 43; // torch tree
				else if (room.monsterTier <= 2) primary = 55; // short blue torch
				else if (room.monsterTier <= 4) primary = 56; // short green torch
				else primary = 57; // short red torch
				for (int decor = 0; decor < decorationCount; decor++)
				{
					int type = outdoorRooms[ri] && decor > 0 ? 43 : primary;
					PlaceDecoration(type, true, room.id + decor * 2);
				}
			}
		}

		if (!room.hasPlayerStart && (room.isArena || room.isSecret) && room.enemyCount >= 3)
		{
			int corpse = hellTheme ? 20 : 15; // dead imp / dead marine
			PlaceDecoration(corpse, false, room.id + 3);
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
