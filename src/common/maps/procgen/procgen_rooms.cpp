/*
** procgen_rooms.cpp
**
** Coarse-cell room composition, visual coherence, encounter pacing, and
** resource progression for procedural maps.
**
**---------------------------------------------------------------------------
*/

#include "procgen_internal.h"
#include "gamedata/gi.h"

using namespace ProcGen;

static bool ContainsRoom(const TArray<int>& rooms, int room)
{
	for (unsigned int i = 0; i < rooms.Size(); i++)
		if (rooms[i] == room) return true;
	return false;
}

void FProceduralMapGenerator::MergeRooms(int W, int H)
{
	Rooms.Clear();
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			Grid[y][x].roomId = -1;

	auto IsSpecial = [&](const ProcGenCell& cell) -> bool
	{
		return cell.hasPlayerStart || cell.hasExit || cell.hasBoss || cell.hasKey ||
			cell.isLocked || cell.reservedSecret;
	};

	auto Compatible = [&](const ProcGenCell& seed, const ProcGenCell& candidate) -> bool
	{
		if (candidate.lockStage != seed.lockStage) return false;
		if (candidate.reservedSecret != seed.reservedSecret) return false;
		if (candidate.isLocked || seed.isLocked)
			return candidate.isLocked && seed.isLocked && candidate.lockType == seed.lockType;

		if (IsSpecial(candidate) && !IsSpecial(seed)) return false;
		if (candidate.hasPlayerStart != seed.hasPlayerStart && candidate.hasPlayerStart) return false;
		if (candidate.hasExit != seed.hasExit && candidate.hasExit) return false;
		if (candidate.hasBoss != seed.hasBoss && candidate.hasBoss) return false;
		if (candidate.hasKey && (!seed.hasKey || candidate.keyType != seed.keyType)) return false;

		if (seed.hasKey)
			return candidate.pathRank == seed.pathRank && candidate.isArena;
		if (seed.hasExit || seed.hasBoss)
			return candidate.pathRank == seed.pathRank && candidate.isArena;
		if (seed.hasPlayerStart)
			return candidate.pathRank == seed.pathRank;

		if (abs(candidate.pathRank - seed.pathRank) > 1) return false;
		if (candidate.onMainPath != seed.onMainPath)
		{
			if (!(seed.isHub || seed.isArena) || candidate.pathRank != seed.pathRank)
				return false;
		}
		if (candidate.branchDepth > 0 && seed.branchDepth > 0 &&
			abs(candidate.branchDepth - seed.branchDepth) > 1)
			return false;
		if ((candidate.isArena != seed.isArena || candidate.isHub != seed.isHub) &&
			(candidate.isArena || seed.isArena || candidate.isHub || seed.isHub))
			return candidate.pathRank == seed.pathRank;
		return true;
	};

	auto TargetRoomSize = [&](const ProcGenCell& seed) -> int
	{
		const int combatGrowth = Difficulty - 1;
		if (seed.isLocked) return 1;
		const int landmarkGrowth = 2 + Size / 4;
		if (seed.hasExit || seed.hasBoss)
			return 5 + landmarkGrowth + combatGrowth * 2 + (RNG() % (3 + Size / 8));
		if (seed.hasKey)
			return 3 + landmarkGrowth + combatGrowth + (RNG() % (2 + Size / 10));
		if (seed.hasPlayerStart) return 2 + landmarkGrowth + (RNG() % 3);
		if (seed.isArena)
			return 4 + landmarkGrowth + combatGrowth * 2 + (RNG() % (3 + Size / 8));
		if (seed.isHub) return 3 + landmarkGrowth + combatGrowth / 2 + (RNG() % 4);

		// Maintain three strong authored scales plus occasional dominant rooms.
		// The proportions mirror the broad rhythm measured in the Doom II IWAD:
		// connectors and intimate rooms, ordinary combat rooms, then landmarks
		// several times the median footprint.
		const int roll = RNG() % 100;
		if (seed.onMainPath)
		{
			if (roll < 20) return 1;
			if (roll < 45) return 2;
			if (roll < 78) return 3 + (RNG() % (3 + Size / 12));
			return 7 + (RNG() % (4 + Size / 10));
		}
		if (seed.branchDepth >= 2)
		{
			if (roll < 38) return 1;
			if (roll < 72) return 2;
			if (roll < 95) return 3 + (RNG() % (3 + Size / 16));
			return 6 + (RNG() % (3 + Size / 16));
		}
		if (roll < 30) return 1;
		if (roll < 60) return 2;
		if (roll < 90) return 3 + (RNG() % (3 + Size / 14));
		return 7 + (RNG() % (3 + Size / 12));
	};

	// Process important cells first so their surrounding landmark footprint is
	// claimed before ordinary route cells consume it.
	for (int priority = 0; priority < 2; priority++)
	{
		for (int y = 0; y < H; y++)
		{
			for (int x = 0; x < W; x++)
			{
				ProcGenCell& seed = Grid[y][x];
				if (!seed.present || seed.roomId >= 0) continue;
				if ((priority == 0) != IsSpecial(seed)) continue;

				const int target = TargetRoomSize(seed);
				RoomInfo room;
				room.id = Rooms.Size();
				room.minI = room.maxI = x;
				room.minJ = room.maxJ = y;
				room.cellCount = 0;
				room.spatialClass = target <= 1 ? 0 :
					(target <= 2 ? 1 : (target <= 6 ? 2 : 3));
				if (target <= 1) room.shapeFamily = 0;
				else if (seed.isArena || seed.isHub || seed.hasExit || seed.hasBoss)
					room.shapeFamily = 3;
				else
				{
					const int familyRoll = RNG() % 100;
					room.shapeFamily = familyRoll < 24 ? 0 :
						(familyRoll < 49 ? 1 : (familyRoll < 74 ? 2 : 3));
				}
				Rooms.Push(room);
				const int roomId = Rooms.Size() - 1;

				TArray<std::pair<int, int>> cells;
				cells.Push(std::make_pair(x, y));
				seed.roomId = roomId;
				while ((int)cells.Size() < target)
				{
					int bestX = -1;
					int bestY = -1;
					int bestScore = -100000;
					for (unsigned int ci = 0; ci < cells.Size(); ci++)
					{
						const int cx = cells[ci].first;
						const int cy = cells[ci].second;
						for (int d = 0; d < 4; d++)
						{
							const int nx = cx + DX[d];
							const int ny = cy + DY[d];
							if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
							ProcGenCell& candidate = Grid[ny][nx];
							if (!candidate.present || candidate.roomId >= 0 || !Compatible(seed, candidate)) continue;

							int minX = std::min(Rooms[roomId].minI, nx);
							int maxX = std::max(Rooms[roomId].maxI, nx);
							int minY = std::min(Rooms[roomId].minJ, ny);
							int maxY = std::max(Rooms[roomId].maxJ, ny);
							int width = maxX - minX + 1;
							int height = maxY - minY + 1;
							if (width > height * 4 || height > width * 4) continue;

							int sameNeighbors = 0;
							int linkedNeighbors = 0;
							for (int od = 0; od < 4; od++)
							{
								const int ox = nx + DX[od];
								const int oy = ny + DY[od];
								if (ox < 0 || ox >= W || oy < 0 || oy >= H) continue;
								if (Grid[oy][ox].roomId == roomId)
								{
									sameNeighbors++;
									if (candidate.conn[od]) linkedNeighbors++;
								}
							}

							int score = linkedNeighbors * 18;
							const int family = Rooms[roomId].shapeFamily;
							if (family == 0)
							{
								score += sameNeighbors * 26;
								score -= abs(width - height) * 4;
							}
							else if (family == 1)
							{
								// Long bays and galleries remain thin instead of filling every
								// neighboring cell into another rounded rectangle.
								score += (width - height) * 15;
								score += sameNeighbors == 1 ? 20 : -sameNeighbors * 8;
							}
							else if (family == 2)
							{
								score += (height - width) * 15;
								score += sameNeighbors == 1 ? 20 : -sameNeighbors * 8;
							}
							else
							{
								// Compound rooms seek a turn and then branch, producing L, T,
								// cross, and stepped footprints rather than solid cell blocks.
								score += sameNeighbors == 1 ? 24 : (sameNeighbors == 2 ? 4 : -20);
								if (width > 1 && height > 1) score += 22;
								score += abs(width - height) * 2;
							}
							if (candidate.pathRank == seed.pathRank) score += 18;
							if (candidate.onMainPath == seed.onMainPath) score += 8;
							score += RNG() % 11;
							if (score > bestScore)
							{
								bestScore = score;
								bestX = nx;
								bestY = ny;
							}
						}
					}

					if (bestX < 0) break;
					Grid[bestY][bestX].roomId = roomId;
					cells.Push(std::make_pair(bestX, bestY));
					Rooms[roomId].minI = std::min(Rooms[roomId].minI, bestX);
					Rooms[roomId].maxI = std::max(Rooms[roomId].maxI, bestX);
					Rooms[roomId].minJ = std::min(Rooms[roomId].minJ, bestY);
					Rooms[roomId].maxJ = std::max(Rooms[roomId].maxJ, bestY);
				}

				Rooms[roomId].cellCount = cells.Size();
				const int realizedClass = cells.Size() <= 1 ? 0 :
					(cells.Size() <= 2 ? 1 : (cells.Size() <= 6 ? 2 : 3));
				Rooms[roomId].spatialClass = IsSpecial(seed) &&
					(seed.isArena || seed.isHub || seed.hasExit || seed.hasBoss || seed.hasKey) ?
					std::max(2, realizedClass) : realizedClass;
			}
		}
	}

	// Cells belonging to one room form continuous floor space even when their
	// original mission-graph connection was only implicit landmark expansion.
	for (int y = 0; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			if (!Grid[y][x].present) continue;
			for (int d = 0; d < 4; d++)
			{
				const int nx = x + DX[d];
				const int ny = y + DY[d];
				if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
				if (Grid[ny][nx].present && Grid[ny][nx].roomId == Grid[y][x].roomId)
				{
					Grid[y][x].conn[d] = true;
					Grid[ny][nx].conn[OPP[d]] = true;
				}
			}
		}
	}
}

void FProceduralMapGenerator::ApplyCoherence(int W, int H)
{
	TArray<TArray<int>> adjacency;
	adjacency.Resize(Rooms.Size());

	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		room.hasPlayerStart = false;
		room.hasExit = false;
		room.hasBoss = false;
		room.hasKey = false;
		room.keyType = 0;
		room.isLocked = false;
		room.lockType = 0;
		room.onMainPath = false;
		room.isArena = false;
		room.isHub = false;
		room.branchDepth = 0;
		room.distFromStart = -1;
		room.progressionRank = 9999;
		room.lockStage = -1;
		room.enemyCount = 0;
		room.monsterTier = 1;
		room.isSecret = false;
		room.reservedSecret = false;
		room.hasDoor = false;
		room.hasWeapon = false;
		room.hasAmmo = false;
		room.ammoCount = 0;
		room.hasHealth = false;
		room.healthCount = 0;
		room.healthBonusCount = 0;
		room.hasArmor = false;
		room.powerups.Clear();
	}

	int startRoom = -1;
	for (int y = 0; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			ProcGenCell& cell = Grid[y][x];
			if (!cell.present || cell.roomId < 0 || cell.roomId >= (int)Rooms.Size()) continue;
			RoomInfo& room = Rooms[cell.roomId];
			if (room.lockStage < 0) room.lockStage = cell.lockStage;
			room.hasPlayerStart = room.hasPlayerStart || cell.hasPlayerStart;
			room.hasExit = room.hasExit || cell.hasExit;
			room.hasBoss = room.hasBoss || cell.hasBoss;
			room.onMainPath = room.onMainPath || cell.onMainPath;
			room.isArena = room.isArena || cell.isArena;
			room.isHub = room.isHub || cell.isHub;
			room.reservedSecret = room.reservedSecret || cell.reservedSecret;
			room.branchDepth = std::max(room.branchDepth, cell.branchDepth);
			if (cell.pathRank >= 0) room.progressionRank = std::min(room.progressionRank, cell.pathRank);
			if (cell.hasKey)
			{
				room.hasKey = true;
				room.keyType = cell.keyType;
			}
			if (cell.isLocked)
			{
				room.isLocked = true;
				room.lockType = cell.lockType;
			}
			if (cell.hasPlayerStart) startRoom = cell.roomId;

			for (int d = 0; d < 4; d++)
			{
				if (!cell.conn[d]) continue;
				const int nx = x + DX[d];
				const int ny = y + DY[d];
				if (nx < 0 || nx >= W || ny < 0 || ny >= H || !Grid[ny][nx].present) continue;
				const int other = Grid[ny][nx].roomId;
				if (other != cell.roomId && other >= 0 && !ContainsRoom(adjacency[cell.roomId], other))
					adjacency[cell.roomId].Push(other);
			}
		}
	}

	if (startRoom < 0 && Rooms.Size() > 0) startRoom = 0;
	TArray<int> queue;
	if (startRoom >= 0)
	{
		Rooms[startRoom].distFromStart = 0;
		queue.Push(startRoom);
	}
	for (unsigned int qi = 0; qi < queue.Size(); qi++)
	{
		const int roomId = queue[qi];
		for (unsigned int ai = 0; ai < adjacency[roomId].Size(); ai++)
		{
			const int other = adjacency[roomId][ai];
			if (Rooms[other].distFromStart >= 0) continue;
			Rooms[other].distFromStart = Rooms[roomId].distFromStart + 1;
			queue.Push(other);
		}
	}

	int maxDistance = 1;
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.progressionRank == 9999) room.progressionRank = 0;
		if (room.distFromStart < 0) room.distFromStart = room.progressionRank;
		maxDistance = std::max(maxDistance, room.distFromStart);
		room.isDeadEnd = adjacency[ri].Size() <= 1 && !room.hasPlayerStart && !room.hasExit;
		if (adjacency[ri].Size() >= 3 && room.onMainPath) room.isHub = true;
	}

	static const char* TechWallZones[4][6] = {
		{ "STARTAN3", "STARTAN2", "BROWN96", "BROWNGRN", "BROWN1", "STONE2" },
		{ "BROWN1", "BROWN96", "BROWNGRN", "STARTAN2", "STONE2", "STARTAN3" },
		{ "STONE2", "STONE3", "METAL1", "COMPSPAN", "BROWN96", "TEKWALL1" },
		{ "TEKWALL1", "TEKWALL4", "COMPSPAN", "METAL1", "STONE3", "STARTAN2" }
	};
	static const char* TechFloorZones[4][6] = {
		{ "FLOOR4_8", "FLOOR4_1", "FLOOR4_6", "FLOOR5_1", "FLOOR5_2", "FLAT1" },
		{ "FLOOR5_1", "FLOOR5_2", "FLAT1", "FLOOR4_6", "FLOOR0_1", "FLOOR4_8" },
		{ "FLOOR0_1", "FLAT14", "FLOOR4_1", "FLOOR5_2", "FLAT20", "FLOOR4_6" },
		{ "FLAT20", "FLOOR4_8", "FLAT14", "FLOOR0_1", "FLAT10", "FLOOR7_2" }
	};
	static const char* TechCeilZones[4][6] = {
		{ "CEIL3_5", "CEIL3_6", "CEIL5_1", "FLAT20", "CEIL5_2", "FLOOR0_1" },
		{ "FLAT20", "CEIL5_2", "CEIL3_5", "FLOOR0_1", "CEIL3_6", "FLAT14" },
		{ "CEIL5_1", "CEIL5_2", "FLAT14", "CEIL3_6", "FLAT20", "FLAT10" },
		{ "FLOOR7_2", "FLAT20", "CEIL5_1", "FLAT10", "CEIL5_2", "FLAT14" }
	};
	static const char* HellWallZones[4][6] = {
		{ "STONE2", "STONE3", "GSTONE1", "GSTONE2", "MARBLE1", "GSTVINE1" },
		{ "MARBLE1", "MARBLE2", "MARBLE3", "STONE3", "GSTONE2", "WOOD1" },
		{ "GSTVINE1", "GSTVINE2", "GSTONE1", "WOOD1", "MARBLE2", "STONE2" },
		{ "SP_HOT1", "GSTONE2", "MARBLE3", "WOOD1", "GSTVINE2", "MARBLE1" }
	};
	static const char* HellFloorZones[4][6] = {
		{ "FLOOR6_1", "FLOOR6_2", "FLAT5_1", "FLAT5_2", "FLOOR7_1", "FLAT8" },
		{ "FLAT5_1", "FLAT5_2", "FLOOR7_1", "FLOOR6_1", "FLOOR6_2", "FLAT10" },
		{ "FLOOR7_2", "FLOOR7_1", "FLAT8", "FLAT10", "FLAT5_2", "FLOOR6_2" },
		{ "FLOOR6_2", "FLAT8", "FLAT10", "FLOOR7_2", "FLAT5_1", "FLOOR7_1" }
	};
	static const char* HellCeilZones[4][6] = {
		{ "FLAT5_1", "FLAT5_2", "FLOOR6_1", "CEIL5_1", "FLAT8", "CEIL5_2" },
		{ "FLOOR7_2", "FLAT10", "FLAT5_2", "CEIL5_2", "FLAT8", "FLOOR6_2" },
		{ "FLAT10", "FLAT8", "FLOOR7_1", "CEIL5_1", "FLAT5_2", "FLOOR6_1" },
		{ "CEIL5_1", "FLAT10", "FLAT8", "FLOOR6_2", "FLOOR7_1", "FLAT5_1" }
	};
	static const char* IndustrialWallZones[4][6] = {
		{ "BROWN96", "BROWN1", "STARTAN2", "SUPPORT3", "METAL1", "BROWNGRN" },
		{ "METAL1", "BROWN96", "COMPSPAN", "SUPPORT3", "STONE2", "TEKWALL1" },
		{ "METAL1", "COMPSPAN", "TEKWALL1", "TEKWALL4", "STONE3", "BROWN1" },
		{ "TEKWALL4", "COMPSPAN", "METAL1", "STONE3", "TEKWALL1", "BROWN96" }
	};
	static const char* IndustrialFloorZones[4][6] = {
		{ "FLOOR5_1", "FLOOR5_2", "FLAT1", "FLOOR4_6", "FLOOR0_1", "FLOOR4_8" },
		{ "FLOOR0_1", "FLAT14", "FLOOR5_2", "FLOOR4_1", "FLAT20", "FLOOR4_6" },
		{ "FLAT20", "FLOOR0_1", "FLAT14", "FLOOR5_2", "FLAT10", "FLOOR7_2" },
		{ "FLAT20", "FLAT14", "FLOOR0_1", "FLOOR7_2", "FLAT10", "FLOOR4_8" }
	};
	static const char* IndustrialCeilZones[4][6] = {
		{ "CEIL5_1", "CEIL3_5", "CEIL5_2", "FLAT20", "CEIL3_6", "FLOOR0_1" },
		{ "FLAT20", "CEIL5_2", "CEIL3_5", "FLOOR0_1", "FLAT14", "CEIL3_6" },
		{ "CEIL5_2", "FLAT14", "FLAT20", "CEIL5_1", "FLOOR0_1", "FLAT10" },
		{ "FLAT20", "FLAT10", "CEIL5_1", "FLAT14", "CEIL5_2", "FLOOR7_2" }
	};
	static const char* GothicWallZones[4][6] = {
		{ "STONE2", "STONE3", "GSTONE1", "MARBLE1", "GSTONE2", "WOOD1" },
		{ "MARBLE1", "MARBLE2", "MARBLE3", "STONE3", "WOOD1", "GSTONE2" },
		{ "WOOD1", "GSTVINE1", "MARBLE2", "GSTONE1", "GSTVINE2", "MARBLE3" },
		{ "MARBLE3", "WOOD1", "SP_HOT1", "GSTVINE2", "GSTONE2", "MARBLE1" }
	};
	static const char* GothicFloorZones[4][6] = {
		{ "FLAT5_1", "FLOOR6_1", "FLAT5_2", "FLOOR6_2", "FLOOR7_1", "FLAT8" },
		{ "FLOOR7_2", "FLAT5_1", "FLAT8", "FLOOR6_1", "FLAT10", "FLAT5_2" },
		{ "FLOOR7_2", "FLAT8", "FLAT10", "FLOOR7_1", "FLAT5_2", "FLOOR6_2" },
		{ "FLAT10", "FLAT8", "FLOOR7_2", "FLOOR6_2", "FLAT5_1", "FLOOR7_1" }
	};
	static const char* GothicCeilZones[4][6] = {
		{ "FLAT5_1", "FLOOR6_1", "CEIL5_1", "FLAT5_2", "FLAT8", "CEIL5_2" },
		{ "FLOOR7_2", "FLAT10", "CEIL5_2", "FLAT8", "FLOOR6_2", "FLAT5_2" },
		{ "FLAT10", "FLAT8", "FLOOR7_1", "CEIL5_1", "FLOOR6_1", "FLAT5_2" },
		{ "CEIL5_1", "FLAT10", "FLAT8", "FLOOR6_2", "FLOOR7_1", "FLAT5_1" }
	};
	static const char* CorruptedWallZones[4][6] = {
		{ "STARTAN3", "COMPSPAN", "BROWN96", "STARTAN2", "TEKWALL1", "STONE2" },
		{ "COMPSPAN", "STONE3", "BROWNGRN", "GSTVINE1", "METAL1", "MARBLE1" },
		{ "GSTVINE1", "GSTONE1", "COMPSPAN", "SP_HOT1", "TEKWALL4", "MARBLE3" },
		{ "SP_HOT1", "GSTVINE2", "MARBLE3", "TEKWALL4", "GSTONE2", "WOOD1" }
	};
	static const char* CorruptedFloorZones[4][6] = {
		{ "FLOOR4_8", "FLOOR5_1", "FLAT1", "FLOOR4_6", "FLOOR0_1", "FLAT20" },
		{ "FLOOR0_1", "FLAT14", "FLOOR5_2", "FLAT5_1", "FLOOR7_1", "FLOOR4_1" },
		{ "FLAT5_1", "FLOOR7_2", "FLAT10", "FLOOR0_1", "FLAT8", "FLOOR6_2" },
		{ "FLAT8", "FLAT10", "FLOOR7_2", "FLAT5_2", "FLOOR6_2", "FLAT20" }
	};
	static const char* CorruptedCeilZones[4][6] = {
		{ "CEIL3_5", "FLAT20", "CEIL5_1", "CEIL3_6", "FLOOR0_1", "FLAT14" },
		{ "FLAT20", "CEIL5_2", "FLAT14", "FLAT5_1", "CEIL3_5", "FLOOR7_2" },
		{ "FLAT5_2", "FLAT10", "FLAT8", "CEIL5_1", "FLOOR7_1", "FLAT20" },
		{ "FLAT10", "FLAT8", "FLOOR6_2", "CEIL5_1", "FLOOR7_1", "FLAT5_1" }
	};
	static const char* TechAccents[] = {
		"SUPPORT2", "SUPPORT3", "METAL1", "COMPSPAN", "BROWN96", "TEKWALL4"
	};
	static const char* TechDetails[] = {
		"COMPSPAN", "TEKWALL1", "SUPPORT3", "STONE3", "BROWNGRN", "METAL1"
	};
	static const char* HellAccents[] = {
		"GSTVINE2", "GSTONE2", "MARBLE2", "WOOD1", "STONE3", "SP_HOT1"
	};
	static const char* HellDetails[] = {
		"MARBLE3", "GSTVINE1", "WOOD1", "GSTONE1", "STONE2", "GSTVINE2"
	};
	static const char* IndustrialAccents[] = {
		"METAL1", "SUPPORT3", "COMPSPAN", "TEKWALL4", "BROWN96", "SUPPORT2"
	};
	static const char* IndustrialDetails[] = {
		"COMPSPAN", "SUPPORT3", "TEKWALL1", "METAL1", "BROWNGRN", "STONE3"
	};
	static const char* GothicAccents[] = {
		"WOOD1", "MARBLE2", "GSTONE2", "MARBLE3", "GSTVINE2", "STONE3"
	};
	static const char* GothicDetails[] = {
		"MARBLE3", "WOOD1", "GSTVINE1", "GSTONE1", "MARBLE2", "GSTVINE2"
	};
	static const char* CorruptedAccents[4][6] = {
		{ "SUPPORT2", "COMPSPAN", "METAL1", "TEKWALL4", "BROWN96", "SUPPORT3" },
		{ "COMPSPAN", "GSTVINE1", "METAL1", "MARBLE2", "SUPPORT3", "STONE3" },
		{ "GSTVINE2", "COMPSPAN", "MARBLE2", "TEKWALL4", "GSTONE2", "METAL1" },
		{ "GSTVINE2", "MARBLE2", "SP_HOT1", "WOOD1", "TEKWALL4", "GSTONE2" }
	};
	static const char* CorruptedDetails[4][6] = {
		{ "COMPSPAN", "TEKWALL1", "SUPPORT3", "BROWNGRN", "METAL1", "STONE3" },
		{ "TEKWALL1", "GSTVINE1", "COMPSPAN", "STONE3", "MARBLE3", "BROWNGRN" },
		{ "GSTVINE1", "MARBLE3", "COMPSPAN", "GSTONE1", "TEKWALL4", "WOOD1" },
		{ "MARBLE3", "GSTVINE1", "WOOD1", "TEKWALL4", "GSTONE1", "GSTVINE2" }
	};
	static const double HalfProfiles[12][2] = {
		{ 88.0, 160.0 }, { 160.0, 88.0 }, { 104.0, 136.0 }, { 136.0, 104.0 },
		{ 120.0, 176.0 }, { 176.0, 120.0 }, { 136.0, 152.0 }, { 152.0, 136.0 },
		{ 144.0, 168.0 }, { 168.0, 144.0 }, { 160.0, 160.0 }, { 176.0, 176.0 }
	};
	static const double CornerProfiles[] = { 20.0, 28.0, 36.0, 44.0, 52.0 };
	// Broad terraces give the route a readable vertical silhouette. Every value
	// is a multiple of 16 so BuildUDMF can bridge neighboring rooms with exact
	// 8-unit stair sectors instead of relying on Doom's implicit step limit.
	static const int GentleFloorCadence[] = { 0, 16, 32, 48, 64, 48, 32, 16 };
	static const int VariedFloorCadence[] = { 0, 32, 64, 96, 64, 32, 0, -32 };
	static const int DramaticFloorCadence[] = { 0, 48, 96, 144, 96, 48, 0, -48 };
	const ThemeStyle themeStyle = GetThemeStyle(Theme);
	TArray<int> roomClearHeights;
	roomClearHeights.Resize(Rooms.Size());

	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		const int phase = clamp(room.distFromStart * 4 / (maxDistance + 1), 0, 3);
		int palette = phase;
		if (!room.onMainPath && !room.hasKey && !room.hasExit)
			palette = clamp(phase + (room.branchDepth >= 2 ? 1 : 0), 0, 3);

		int styleHash = abs(room.id * 37 + room.minI * 17 + room.maxJ * 29 +
			room.cellCount * 13 + room.progressionRank * 7 + room.branchDepth * 19);
		room.visualVariant = styleHash % countof(HalfProfiles);
		// Keep neighboring rooms in broad material clusters. Wall changes occur at
		// lock stages, progression zones, or strong room roles instead of on every
		// random edge; floor and ceiling variation can remain more frequent because
		// their boundary is an explicit portal threshold.
		bool infernalSurfaces = themeStyle == ThemeHell || themeStyle == ThemeGothic ||
			(themeStyle == ThemeCorrupted && phase >= 2);
		int surfacePalette = palette;
		int familyShift = 0;
		if (themeStyle == ThemeIndustrial)
		{
			familyShift = 1;
		}
		else if (themeStyle == ThemeGothic)
		{
			familyShift = 2;
		}
		else if (themeStyle == ThemeCorrupted)
		{
			familyShift = infernalSurfaces ? 1 : 3;
		}
		int textureVariant = (surfacePalette * 2 + room.lockStage + familyShift) % 6;
		if (room.branchDepth >= 2) textureVariant = (textureVariant + 1) % 6;
		if (room.isArena || room.isHub || room.hasKey || room.hasExit)
			textureVariant = (textureVariant + 2) % 6;
		const int floorVariant = (textureVariant + room.cellCount + styleHash / 11) % 6;
		const int ceilingVariant = (textureVariant + 2 + styleHash / 17) % 6;
		const char* (*wallZones)[6] = infernalSurfaces ? HellWallZones : TechWallZones;
		const char* (*floorZones)[6] = infernalSurfaces ? HellFloorZones : TechFloorZones;
		const char* (*ceilZones)[6] = infernalSurfaces ? HellCeilZones : TechCeilZones;
		if (themeStyle == ThemeIndustrial)
		{
			wallZones = IndustrialWallZones;
			floorZones = IndustrialFloorZones;
			ceilZones = IndustrialCeilZones;
		}
		else if (themeStyle == ThemeGothic)
		{
			wallZones = GothicWallZones;
			floorZones = GothicFloorZones;
			ceilZones = GothicCeilZones;
		}
		else if (themeStyle == ThemeCorrupted)
		{
			wallZones = CorruptedWallZones;
			floorZones = CorruptedFloorZones;
			ceilZones = CorruptedCeilZones;
		}
		room.wallTex = wallZones[surfacePalette][textureVariant];
		room.floorTex = floorZones[surfacePalette][floorVariant];
		room.ceilTex = ceilZones[surfacePalette][ceilingVariant];
		if (themeStyle == ThemeIndustrial)
		{
			room.accentTex = IndustrialAccents[(textureVariant + surfacePalette) % countof(IndustrialAccents)];
			room.detailTex = IndustrialDetails[(textureVariant + room.visualVariant) % countof(IndustrialDetails)];
		}
		else if (themeStyle == ThemeGothic)
		{
			room.accentTex = GothicAccents[(textureVariant + surfacePalette) % countof(GothicAccents)];
			room.detailTex = GothicDetails[(textureVariant + room.visualVariant) % countof(GothicDetails)];
		}
		else if (themeStyle == ThemeCorrupted)
		{
			room.accentTex = CorruptedAccents[surfacePalette]
				[(textureVariant + surfacePalette) % 6];
			room.detailTex = CorruptedDetails[surfacePalette]
				[(textureVariant + room.visualVariant) % 6];
		}
		else
		{
			room.accentTex = infernalSurfaces ?
				HellAccents[(textureVariant + surfacePalette) % countof(HellAccents)] :
				TechAccents[(textureVariant + surfacePalette) % countof(TechAccents)];
			room.detailTex = infernalSurfaces ?
				HellDetails[(textureVariant + room.visualVariant) % countof(HellDetails)] :
				TechDetails[(textureVariant + room.visualVariant) % countof(TechDetails)];
		}

		room.halfWidth = HalfProfiles[room.visualVariant][0];
		room.halfHeight = HalfProfiles[room.visualVariant][1];
		if (room.spatialClass == 0)
		{
			// One-cell connectors are deliberately intimate and directional.
			if ((room.shapeFamily + room.visualVariant) & 1)
				room.halfWidth = std::min(room.halfWidth, 104.0);
			else
				room.halfHeight = std::min(room.halfHeight, 104.0);
		}
		else if (room.spatialClass == 1)
		{
			room.halfWidth = std::min(room.halfWidth, 152.0);
			room.halfHeight = std::min(room.halfHeight, 152.0);
		}
		else if (room.spatialClass == 3)
		{
			room.halfWidth = std::max(room.halfWidth, 160.0);
			room.halfHeight = std::max(room.halfHeight, 160.0);
		}
		int spanX = room.maxI - room.minI;
		int spanY = room.maxJ - room.minJ;
		if (spanX > spanY)
		{
			room.halfWidth = std::max(room.halfWidth, 168.0);
			room.halfHeight = std::min(room.halfHeight, 144.0);
		}
		else if (spanY > spanX)
		{
			room.halfWidth = std::min(room.halfWidth, 144.0);
			room.halfHeight = std::max(room.halfHeight, 168.0);
		}
		// Themes have architectural silhouettes, not just different wallpaper.
		// Industrial cells form long machine bays, Gothic cells read as broad
		// cathedral modules, Hell is irregular, and Corrupted Tech becomes more
		// distorted as its progression phase advances.
		if (themeStyle == ThemeIndustrial)
		{
			if (room.visualVariant & 1)
			{
				room.halfHeight = std::max(168.0, room.halfHeight);
				room.halfWidth = std::min(136.0, room.halfWidth);
			}
			else
			{
				room.halfWidth = std::max(168.0, room.halfWidth);
				room.halfHeight = std::min(136.0, room.halfHeight);
			}
		}
		else if (themeStyle == ThemeGothic)
		{
			// Cathedral modules alternate narrow aisles and broad transepts instead
			// of normalizing every room to the same square footprint.
			room.halfWidth = std::min(176.0, room.halfWidth + 8.0);
			room.halfHeight = std::min(176.0, room.halfHeight + 8.0);
		}
		else if (themeStyle == ThemeHell)
		{
			if (room.visualVariant & 1) room.halfWidth = std::min(184.0, room.halfWidth + 8.0);
			else room.halfHeight = std::min(184.0, room.halfHeight + 8.0);
		}
		else if (themeStyle == ThemeCorrupted && phase >= 2)
		{
			if (room.visualVariant & 1) room.halfWidth = std::max(104.0, room.halfWidth - 16.0);
			else room.halfHeight = std::max(104.0, room.halfHeight - 16.0);
		}
		if (room.isArena || room.hasExit)
			room.halfWidth = room.halfHeight = 184.0;
		else if (room.isHub || room.hasKey)
		{
			room.halfWidth = std::max(room.halfWidth, 176.0);
			room.halfHeight = std::max(room.halfHeight, 168.0);
		}
		if (room.hasPlayerStart)
			room.halfWidth = room.halfHeight = 176.0;
		if (room.isLocked)
			room.halfWidth = room.halfHeight = 160.0;
		// Leave a physical connector bay between adjacent coarse cells. Two
		// 176-unit chambers leave 32 units between their faces: enough for the
		// 16-unit moving-door slab and an eight-unit recessed approach/lintel on
		// each side. Reserving this before room features are sized keeps doors
		// from stealing space later from stairs, reveals, or landmark clearances.
		// The serialized module cadence includes occasional 368-unit gaps. A
		// 168-unit chamber contract on both sides preserves a 32-unit connector
		// bay: an eight-unit approach, 16-unit door slab, and second approach.
		room.halfWidth = std::min(room.halfWidth, 168.0);
		room.halfHeight = std::min(room.halfHeight, 168.0);
		room.cornerCut = CornerProfiles[(styleHash / 5) % countof(CornerProfiles)];
		if (Detail == 0) room.cornerCut = std::min(room.cornerCut, 28.0);
		else if (Detail == 2) room.cornerCut += 8.0;
		if (themeStyle == ThemeTechbase) room.cornerCut = std::min(room.cornerCut, 36.0);
		else if (themeStyle == ThemeIndustrial) room.cornerCut = std::min(room.cornerCut, 28.0);
		else if (themeStyle == ThemeGothic) room.cornerCut = std::min(room.cornerCut, 24.0);
		else if (themeStyle == ThemeHell) room.cornerCut = std::max(room.cornerCut, 36.0);
		else if (themeStyle == ThemeCorrupted)
			room.cornerCut = phase >= 2 ? std::max(room.cornerCut, 36.0) :
				std::min(room.cornerCut, 28.0);
		if (room.isArena || room.isHub || room.hasKey || room.hasBoss)
			room.cornerCut = std::min(room.cornerCut, 16.0);
		room.cornerCut = std::min(room.cornerCut,
			std::max(0.0, std::min(room.halfWidth, room.halfHeight) - 56.0));

		const int* floorCadence = Verticality == 0 ? GentleFloorCadence :
			(Verticality == 2 ? DramaticFloorCadence : VariedFloorCadence);
		const int elevationPhase = room.distFromStart % countof(VariedFloorCadence);
		room.floorZ = floorCadence[elevationPhase];
		if (!room.onMainPath)
		{
			const int baseBranchRise = Verticality == 0 ? 16 : (Verticality == 2 ? 48 : 32);
			const int branchRise = room.branchDepth >= 2 ? baseBranchRise : 16;
			room.floorZ += ((styleHash / 23) & 1) ? branchRise : -branchRise;
		}
		if (themeStyle == ThemeHell) room.floorZ += phase >= 2 ? 16 : 0;
		else if (themeStyle == ThemeGothic && phase >= 1) room.floorZ += 16;
		else if (themeStyle == ThemeIndustrial && !room.onMainPath)
			room.floorZ += (styleHash & 1) ? 16 : -16;
		else if (themeStyle == ThemeCorrupted && phase >= 2)
			room.floorZ += (phase - 1) * 16;
		const double minFloor = Verticality == 0 ? -32.0 : (Verticality == 2 ? -96.0 : -64.0);
		const double maxFloor = Verticality == 0 ? 96.0 : (Verticality == 2 ? 176.0 : 128.0);
		room.floorZ = clamp(room.floorZ, minFloor, maxFloor);

		int clearHeight = 160 + (room.visualVariant % 4) * 16;
		if (room.spatialClass == 0) clearHeight = 128 + (room.visualVariant % 3) * 16;
		else if (room.spatialClass == 3) clearHeight += 32;
		if (room.cellCount == 1 && !room.hasKey && !room.hasExit)
			clearHeight = 144 + (room.visualVariant % 3) * 16;
		if (room.isHub) clearHeight = 192 + (room.visualVariant % 3) * 16;
		if (room.isArena) clearHeight = 240 + (room.visualVariant % 3) * 16;
		if (room.hasExit || room.hasBoss) clearHeight = 288 + (room.visualVariant % 3) * 16;
		if (themeStyle == ThemeTechbase && !room.isArena && !room.isHub)
			clearHeight = std::max(144, clearHeight - 16);
		else if (themeStyle == ThemeHell)
			clearHeight += room.isArena || room.hasExit ? 32 : 16;
		else if (themeStyle == ThemeIndustrial)
			clearHeight += (room.visualVariant & 1) ? 32 : 0;
		else if (themeStyle == ThemeGothic)
			clearHeight += room.isArena || room.isHub || room.hasExit ? 64 : 48;
		else if (themeStyle == ThemeCorrupted)
			clearHeight += phase * 16;
		roomClearHeights[ri] = clearHeight;

		room.light = 192 - phase * 8;
		if (!room.onMainPath) room.light -= 8;
		if (room.branchDepth >= 2) room.light -= 8;
		if (room.isArena || room.isHub) room.light += 8;
		if (room.hasPlayerStart || room.hasKey || room.hasExit) room.light += 8;
		if (themeStyle == ThemeTechbase) room.light += 8;
		else if (themeStyle == ThemeHell) room.light -= (styleHash & 1) ? 8 : 0;
		else if (themeStyle == ThemeIndustrial) room.light -= 8;
		else if (themeStyle == ThemeGothic)
			room.light += (room.isArena || room.hasKey || room.hasExit) ? 8 : -16;
		else if (themeStyle == ThemeCorrupted) room.light -= phase * 8;
		room.light = clamp((room.light / 8) * 8, 160, 208);
		static const int TechLightColors[] = { 0xe8f2ff, 0xdcecff, 0xd4e6ff, 0xc8dcf4 };
		static const int HellLightColors[] = { 0xffddc8, 0xffc4a8, 0xffa080, 0xff8068 };
		static const int IndustrialLightColors[] = { 0xf0ead8, 0xe6dcc4, 0xdcd0b4, 0xd4c6a8 };
		static const int GothicLightColors[] = { 0xe4e0ff, 0xd8d0f4, 0xc8c0e8, 0xb8acd8 };
		static const int CorruptedLightColors[] = { 0xe4f0ff, 0xd0d8e8, 0xe8b098, 0xff8068 };
		const int* lightColors = TechLightColors;
		if (themeStyle == ThemeHell) lightColors = HellLightColors;
		else if (themeStyle == ThemeIndustrial) lightColors = IndustrialLightColors;
		else if (themeStyle == ThemeGothic) lightColors = GothicLightColors;
		else if (themeStyle == ThemeCorrupted) lightColors = CorruptedLightColors;
		room.lightColor = lightColors[phase];
		room.fadeColor = themeStyle == ThemeHell ? 0x100000 :
			(themeStyle == ThemeGothic ? 0x080810 :
			(themeStyle == ThemeIndustrial ? 0x080704 :
			(themeStyle == ThemeCorrupted && phase >= 2 ? 0x100000 : 0x04080c)));

		if (room.hasPlayerStart)
		{
			if (themeStyle == ThemeHell)
			{
				room.wallTex = "STONE2"; room.floorTex = "FLOOR6_1";
				room.accentTex = "GSTONE2"; room.detailTex = "GSTVINE1";
			}
			else if (themeStyle == ThemeGothic)
			{
				room.wallTex = "MARBLE1"; room.floorTex = "FLAT5_1";
				room.accentTex = "WOOD1"; room.detailTex = "MARBLE3";
			}
			else if (themeStyle == ThemeIndustrial)
			{
				room.wallTex = "BROWN96"; room.floorTex = "FLOOR5_1";
				room.accentTex = "METAL1"; room.detailTex = "COMPSPAN";
			}
			else
			{
				room.wallTex = "STARTAN3"; room.floorTex = "FLOOR4_8";
				room.accentTex = "SUPPORT2"; room.detailTex = "COMPSPAN";
			}
			room.floorZ = 0;
			roomClearHeights[ri] = 192;
			room.enemyCount = 0;
			room.monsterTier = 1;
		}
		else
		{
			// Progression raises pressure in broad steps. Classic difficulty no
			// longer adds a blanket monster to every main-route room, while the
			// explicit arena budgets below still scale predictably.
			int pressure = (Difficulty - 1) / 2 + (Difficulty >= 4 ? 1 : 0);
			const int landmarkPressure = Difficulty / 2 + (Difficulty >= 5 ? 1 : 0);
			if (Difficulty == 2 && ((room.id + phase) % 4) == 0) pressure++;
			if (phase >= 2) pressure++;
			if (Difficulty >= 5 && room.onMainPath && phase > 0) pressure++;
			if (room.branchDepth >= 2) pressure--;
			room.enemyCount = clamp(pressure + (int)(RNG() % 2), 1, 3);
			if (room.isDeadEnd && !room.hasKey) room.enemyCount = std::min(room.enemyCount, 1 + Difficulty / 2);
			if (room.isHub) room.enemyCount = clamp(1 + landmarkPressure + phase / 2, 2, 4);
			if (room.isArena) room.enemyCount = clamp(1 + landmarkPressure + phase / 2 + room.cellCount / 6, 2, 5);
			if (room.hasKey) room.enemyCount = clamp(1 + landmarkPressure + phase / 2 + room.cellCount / 6, 2, 5);
			if (room.isLocked) room.enemyCount = clamp(1 + Difficulty / 3 +
				(Difficulty >= 5 ? 1 : 0) + phase / 2, 1, 4);
			if (room.hasExit) room.enemyCount = clamp(1 + landmarkPressure + Size / 6 + room.cellCount / 8, 2, 5);
			if (room.hasBoss) room.enemyCount = std::min(room.enemyCount, std::max(1, Difficulty - 2));
			if (room.distFromStart == 1 && !room.isArena && !room.hasKey && !room.isLocked)
				room.enemyCount = std::min(room.enemyCount, 2);
			room.monsterTier = clamp(1 + phase + (Difficulty >= 4 ? 1 : 0) +
				(room.hasBoss && Difficulty >= 5 ? 1 : 0), 1, 5);
			if (room.cellCount <= 1) room.monsterTier = std::min(room.monsterTier, 2);
			else if (room.cellCount <= 2) room.monsterTier = std::min(room.monsterTier, 3);
		}

		// Doors punctuate a route. Locks are handled per-edge by BuildUDMF; only
		// selected rewards, deep branches, and transitions request normal doors.
		room.hasDoor = false;
		if (room.hasKey) room.hasDoor = true;
		else if (room.isDeadEnd && room.branchDepth >= 2) room.hasDoor = (RNG() % 100) < 55;
		else if (!room.onMainPath && room.branchDepth >= 2) room.hasDoor = (RNG() % 100) < 25;
		else if (room.onMainPath && phase >= 2 && !room.isHub && !room.isArena)
			room.hasDoor = (RNG() % 100) < 18;
	}

	// A real door cannot also serve as a staircase. Collapse only the components
	// that require a door (start staging, keyed boundaries, and key shrines),
	// leaving ordinary route edges free to become visible height transitions.
	TArray<int> floorParent;
	floorParent.Resize(Rooms.Size());
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++) floorParent[ri] = ri;
	auto FindFloorRoot = [&](int roomId) -> int
	{
		int root = roomId;
		while (floorParent[root] != root) root = floorParent[root];
		while (floorParent[roomId] != roomId)
		{
			const int next = floorParent[roomId];
			floorParent[roomId] = root;
			roomId = next;
		}
		return root;
	};
	auto JoinFloorComponents = [&](int first, int second)
	{
		const int firstRoot = FindFloorRoot(first);
		const int secondRoot = FindFloorRoot(second);
		if (firstRoot != secondRoot) floorParent[secondRoot] = firstRoot;
	};
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		for (unsigned int ai = 0; ai < adjacency[ri].Size(); ai++)
		{
			const int other = adjacency[ri][ai];
			const RoomInfo& first = Rooms[ri];
			const RoomInfo& second = Rooms[other];
			const bool mandatoryDoor = first.lockStage != second.lockStage ||
				first.hasPlayerStart || second.hasPlayerStart ||
				first.hasKey || second.hasKey;
			if (mandatoryDoor) JoinFloorComponents(ri, other);
		}
	}

	TArray<int> componentFloor;
	TArray<int> componentMembers;
	TArray<int> componentDistance;
	componentFloor.Resize(Rooms.Size());
	componentMembers.Resize(Rooms.Size());
	componentDistance.Resize(Rooms.Size());
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		componentFloor[ri] = 0;
		componentMembers[ri] = 0;
		componentDistance[ri] = 0x3fffffff;
	}
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		const int root = FindFloorRoot(ri);
		componentFloor[root] += (int)lround(Rooms[ri].floorZ);
		componentMembers[root]++;
		componentDistance[root] = std::min(componentDistance[root], Rooms[ri].distFromStart);
	}
	int startRoot = startRoom >= 0 ? FindFloorRoot(startRoom) : -1;
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		if (componentMembers[ri] == 0) continue;
		componentFloor[ri] = (int)lround(
			componentFloor[ri] / (double)componentMembers[ri] / 16.0) * 16;
	}
	if (startRoot >= 0) componentFloor[startRoot] = 0;

	// Limit one inter-room staircase to 64 units (eight short risers). Iterate
	// over graph cycles while keeping the player-start terrace anchored at zero.
	// Dramatic verticality on an Exploratory size-80 graph can have hundreds of
	// composed-room components. Use a graph-sized convergence bound instead of a
	// small constant so the 64-unit stair constraint propagates all the way from
	// the anchored start terrace through the longest optional limb.
	const int floorRelaxationPasses = std::max(24, (int)Rooms.Size() + 4);
	for (int pass = 0; pass < floorRelaxationPasses; pass++)
	{
		bool changed = false;
		for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		{
			for (unsigned int ai = 0; ai < adjacency[ri].Size(); ai++)
			{
				const int firstRoot = FindFloorRoot(ri);
				const int secondRoot = FindFloorRoot(adjacency[ri][ai]);
				if (firstRoot == secondRoot) continue;
				const int difference = componentFloor[firstRoot] - componentFloor[secondRoot];
				if (abs(difference) <= 64) continue;
				int movingRoot;
				int fixedRoot;
				if (firstRoot == startRoot)
				{
					movingRoot = secondRoot;
					fixedRoot = firstRoot;
				}
				else if (secondRoot == startRoot)
				{
					movingRoot = firstRoot;
					fixedRoot = secondRoot;
				}
				else if (componentDistance[firstRoot] > componentDistance[secondRoot] ||
					(componentDistance[firstRoot] == componentDistance[secondRoot] &&
						firstRoot > secondRoot))
				{
					movingRoot = firstRoot;
					fixedRoot = secondRoot;
				}
				else
				{
					movingRoot = secondRoot;
					fixedRoot = firstRoot;
				}
				componentFloor[movingRoot] = componentFloor[fixedRoot] +
					(componentFloor[movingRoot] > componentFloor[fixedRoot] ? 64 : -64);
				changed = true;
			}
		}
		if (!changed) break;
	}
	bool terraceConstraintViolated = false;
	for (unsigned int ri = 0; ri < Rooms.Size() && !terraceConstraintViolated; ri++)
	{
		for (unsigned int ai = 0; ai < adjacency[ri].Size(); ai++)
		{
			const int firstRoot = FindFloorRoot(ri);
			const int secondRoot = FindFloorRoot(adjacency[ri][ai]);
			if (firstRoot != secondRoot &&
				abs(componentFloor[firstRoot] - componentFloor[secondRoot]) > 64)
			{
				terraceConstraintViolated = true;
				break;
			}
		}
	}
	if (terraceConstraintViolated)
	{
		// Cyclic graphs can make local target-preserving projections oscillate.
		// Fall back to a component-graph BFS cadence: endpoints of every edge then
		// differ in graph distance by at most one, and every cadence step is at most
		// 48 units. Phase transitions may add 16, retaining the hard 64-unit bound.
		TArray<TArray<int>> componentAdjacency;
		componentAdjacency.Resize(Rooms.Size());
		for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		{
			const int firstRoot = FindFloorRoot(ri);
			for (unsigned int ai = 0; ai < adjacency[ri].Size(); ai++)
			{
				const int secondRoot = FindFloorRoot(adjacency[ri][ai]);
				if (firstRoot == secondRoot) continue;
				if (!ContainsRoom(componentAdjacency[firstRoot], secondRoot))
					componentAdjacency[firstRoot].Push(secondRoot);
				if (!ContainsRoom(componentAdjacency[secondRoot], firstRoot))
					componentAdjacency[secondRoot].Push(firstRoot);
			}
		}
		TArray<int> graphDistance;
		graphDistance.Resize(Rooms.Size());
		for (unsigned int ri = 0; ri < graphDistance.Size(); ri++) graphDistance[ri] = -1;
		TArray<int> componentQueue;
		if (startRoot >= 0)
		{
			graphDistance[startRoot] = 0;
			componentQueue.Push(startRoot);
		}
		for (unsigned int qi = 0; qi < componentQueue.Size(); qi++)
		{
			const int root = componentQueue[qi];
			for (unsigned int ai = 0; ai < componentAdjacency[root].Size(); ai++)
			{
				const int other = componentAdjacency[root][ai];
				if (graphDistance[other] >= 0) continue;
				graphDistance[other] = graphDistance[root] + 1;
				componentQueue.Push(other);
			}
		}
		int maximumGraphDistance = 1;
		for (unsigned int ri = 0; ri < graphDistance.Size(); ri++)
			maximumGraphDistance = std::max(maximumGraphDistance, graphDistance[ri]);
		const int* fallbackCadence = Verticality == 0 ? GentleFloorCadence :
			(Verticality == 2 ? DramaticFloorCadence : VariedFloorCadence);
		for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		{
			if (FindFloorRoot(ri) != (int)ri) continue;
			const int distance = std::max(0, graphDistance[ri]);
			int floor = fallbackCadence[distance % countof(VariedFloorCadence)];
			const int graphPhase = clamp(distance * 4 / (maximumGraphDistance + 1), 0, 3);
			if (themeStyle == ThemeHell && graphPhase >= 2) floor += 16;
			else if (themeStyle == ThemeGothic && graphPhase >= 1) floor += 16;
			else if (themeStyle == ThemeCorrupted && graphPhase >= 2)
				floor += (graphPhase - 1) * 16;
			componentFloor[ri] = floor;
		}
	}
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		room.floorZ = componentFloor[FindFloorRoot(ri)];
		room.ceilZ = room.floorZ + roomClearHeights[ri];
	}

	for (int y = 0; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			ProcGenCell& cell = Grid[y][x];
			if (!cell.present || cell.roomId < 0) continue;
			RoomInfo& room = Rooms[cell.roomId];
			cell.floorZ = room.floorZ;
			cell.ceilZ = room.ceilZ;
			cell.floorTex = room.floorTex;
			cell.ceilTex = room.ceilTex;
			cell.wallTex = room.wallTex;
			cell.light = room.light;
			cell.enemyCount = room.enemyCount;
			cell.monsterTier = room.monsterTier;
		}
	}
}

void FProceduralMapGenerator::PlaceWeapons(int W, int H)
{
	(void)W;
	(void)H;
	TArray<int> mainRooms;
	TArray<int> sideRooms;
	int startRoom = -1;
	int maxProgressionRank = 1;
	const ThemeStyle themeStyle = GetThemeStyle(Theme);

	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		maxProgressionRank = std::max(maxProgressionRank, room.progressionRank);
		if (room.hasPlayerStart) startRoom = ri;
		else if (room.onMainPath && !room.hasExit) mainRooms.Push(ri);
		else if (!room.hasExit) sideRooms.Push(ri);
	}

	auto SortByDistance = [&](TArray<int>& list)
	{
		for (int i = 0; i < (int)list.Size(); i++)
		{
			int best = i;
			for (int j = i + 1; j < (int)list.Size(); j++)
				if (Rooms[list[j]].distFromStart < Rooms[list[best]].distFromStart) best = j;
			if (best != i)
			{
				int tmp = list[i];
				list[i] = list[best];
				list[best] = tmp;
			}
		}
	};
	SortByDistance(mainRooms);
	SortByDistance(sideRooms);

	auto GiveWeapon = [&](int roomId, int type)
	{
		if (roomId < 0 || roomId >= (int)Rooms.Size() || Rooms[roomId].hasExit) return;
		Rooms[roomId].hasWeapon = true;
		Rooms[roomId].weaponType = type;
	};
	auto GiveProgressionWeapon = [&](int preferredIndex, int type)
	{
		if (mainRooms.Size() == 0) return;
		preferredIndex = clamp(preferredIndex, 0, (int)mainRooms.Size() - 1);
		for (int distance = 0; distance < (int)mainRooms.Size(); distance++)
		{
			int candidates[] = { preferredIndex + distance, preferredIndex - distance };
			for (int candidate : candidates)
			{
				if (candidate < 0 || candidate >= (int)mainRooms.Size()) continue;
				int roomId = mainRooms[candidate];
				if (Rooms[roomId].hasWeapon) continue;
				GiveWeapon(roomId, type);
				return;
			}
		}
	};

	if (startRoom >= 0) GiveWeapon(startRoom, 2001); // shotgun: immediate agency
	const bool doom2Roster = (gameinfo.flags & GI_MAPxx) != 0;
	if (doom2Roster && mainRooms.Size() > 1)
		GiveProgressionWeapon(mainRooms.Size() / 4, 82); // Doom II super shotgun
	if (mainRooms.Size() > 2)
		GiveProgressionWeapon(mainRooms.Size() / 3, 2002); // chaingun
	if (Size >= 2 && mainRooms.Size() > 3)
		GiveProgressionWeapon(mainRooms.Size() / 2, 2003); // rocket launcher
	if (Size >= 4 && mainRooms.Size() > 4)
		GiveProgressionWeapon(mainRooms.Size() * 3 / 4, 2004); // plasma
	if (Size >= 5 && Difficulty >= 5 && sideRooms.Size() > 0)
		GiveWeapon(sideRooms.Last(), 2006); // optional BFG reward

	auto AmmoForStage = [&](const RoomInfo& room) -> int
	{
		if (room.hasWeapon)
		{
			if (room.weaponType == 2001 || room.weaponType == 82) return 2008;
			if (room.weaponType == 2002) return 2007;
			if (room.weaponType == 2003) return 2010;
			if (room.weaponType == 2004 || room.weaponType == 2006) return 2047;
		}
		int phase = clamp(room.progressionRank * 4 / (maxProgressionRank + 1), 0, 3);
		if (phase >= 3 && Size >= 4) return 2047;
		if (phase >= 2 && Size >= 2) return 2010;
		return (RNG() & 1) ? 2008 : 2007;
	};
	auto LargeAmmoForStage = [&](const RoomInfo& room) -> int
	{
		int small = AmmoForStage(room);
		if (small == 2008) return 2049; // shell box
		if (small == 2007) return 2048; // bullet box
		if (small == 2010) return 2046; // rocket box
		if (small == 2047) return 17;   // cell pack
		return small;
	};

	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.hasPlayerStart)
		{
			room.hasAmmo = true;
			room.ammoType = 2008;
			room.ammoCount = 2;
			room.hasHealth = true;
			room.healthType = 2011;
			room.healthCount = 2;
			room.healthBonusCount = 4;
			continue;
		}

		const bool majorFight = room.enemyCount >= 5 || room.isArena || room.hasKey || room.hasExit;
		const bool sustainedFight = Difficulty >= 4 && room.enemyCount >= 3;
		const bool reward = room.hasWeapon || room.hasKey || room.isDeadEnd;
		if (room.hasWeapon || majorFight || sustainedFight ||
			(room.onMainPath && (RNG() % 100) < 60))
		{
			room.hasAmmo = true;
			room.ammoType = majorFight ? LargeAmmoForStage(room) : AmmoForStage(room);
			room.ammoCount = majorFight ? 2 : 1;
		}
		if (majorFight || reward || (sustainedFight && room.onMainPath) ||
			(room.onMainPath && (RNG() % 100) < 75))
		{
			room.hasHealth = true;
			room.healthType = majorFight ? 2012 : 2011;
			room.healthCount = majorFight ? 2 : 1;
		}
		if ((!room.hasHealth && room.onMainPath && (RNG() % 100) < 55) ||
			(!room.onMainPath && room.branchDepth >= 2))
			room.healthBonusCount = 2 + (room.branchDepth >= 2 ? 2 : 0);
		if (room.hasKey || room.hasBoss || (room.isDeadEnd && room.branchDepth >= 2 && (RNG() % 100) < 40))
		{
			room.hasArmor = true;
			room.armorType = room.hasBoss ? 2019 : 2015;
		}
	}

	// Never leave a long run of the critical path without recovery. Two rooms
	// may be dry for pacing, but the third always offers at least stimpacks plus
	// a few health bonuses. This remains independent of random item rolls.
	int dryMainRooms = 0;
	for (unsigned int index = 0; index < mainRooms.Size(); index++)
	{
		RoomInfo& room = Rooms[mainRooms[index]];
		if (room.hasHealth || room.healthBonusCount > 0)
		{
			dryMainRooms = 0;
			continue;
		}
		dryMainRooms++;
		if (dryMainRooms >= 3)
		{
			room.hasHealth = true;
			room.healthType = 2011;
			room.healthCount = 2;
			room.healthBonusCount = 2;
			dryMainRooms = 0;
		}
	}

	// Deep optional rooms are explicit survival opportunities, not decorative
	// dead ends. Seeded selection favors the far ends of side limbs and grants a
	// recovery bundle substantial enough to justify exploration.
	int survivalCacheBudget = std::max(2, 1 + Size / 3);
	for (int pass = 0; pass < 3 && survivalCacheBudget > 0; pass++)
	{
		for (int index = (int)sideRooms.Size() - 1; index >= 0 && survivalCacheBudget > 0; index--)
		{
			RoomInfo& room = Rooms[sideRooms[index]];
			if (room.hasKey || room.hasExit || room.isLocked) continue;
			if (pass == 0 && (!room.isDeadEnd || room.branchDepth < 2)) continue;
			if (pass == 1 && room.branchDepth < 2) continue;
			if (pass == 2 && room.branchDepth < 1) continue;
			if (room.healthCount >= 2 && room.ammoCount >= 2) continue;
			room.hasHealth = true;
			room.healthType = 2012;
			room.healthCount = std::max(room.healthCount, 2);
			room.healthBonusCount = std::max(room.healthBonusCount, 4);
			room.hasAmmo = true;
			room.ammoType = LargeAmmoForStage(room);
			room.ammoCount = std::max(room.ammoCount, 2);
			room.hasDoor = room.hasDoor || room.isDeadEnd;
			if ((survivalCacheBudget & 1) == 0 && !room.hasArmor)
			{
				room.hasArmor = true;
				room.armorType = 2015;
			}
			survivalCacheBudget--;
		}
	}

	// Turn a few optional dead ends into real Doom-style secrets. Selection is
	// deterministic and favors the deepest side limbs; every secret receives a
	// useful recovery bundle even when weapon progression chose another room.
	int secretBudget = 3 + Size / 3 + (Detail >= 1 ? 1 : 0) + (Detail == 2 ? 1 : 0);
	auto MakeSecret = [&](RoomInfo& room)
	{
		room.isSecret = true;
		room.hasDoor = true;
		room.hasAmmo = true;
		room.ammoType = AmmoForStage(room);
		room.ammoCount = std::max(room.ammoCount, 2);
		room.hasHealth = true;
		room.healthType = 2012;
		room.healthCount = std::max(room.healthCount, 2);
		room.healthBonusCount = std::max(room.healthBonusCount, 4);
		if (!room.hasArmor)
		{
			room.hasArmor = true;
			room.armorType = 2015;
		}
	};
	for (int index = (int)sideRooms.Size() - 1; index >= 0 && secretBudget > 0; index--)
	{
		RoomInfo& room = Rooms[sideRooms[index]];
		if (!room.reservedSecret || !room.isDeadEnd || room.hasKey ||
			room.hasExit || room.isLocked)
			continue;
		MakeSecret(room);
		secretBudget--;
	}
	for (int pass = 0; pass < 3 && secretBudget > 0; pass++)
	{
		for (int index = (int)sideRooms.Size() - 1; index >= 0 && secretBudget > 0; index--)
		{
			RoomInfo& room = Rooms[sideRooms[index]];
			if (room.isSecret || !room.isDeadEnd || room.hasKey || room.hasExit || room.isLocked) continue;
			if (pass == 0 && room.branchDepth < 2) continue;
			if (pass == 2 && room.branchDepth < 1) continue;
			MakeSecret(room);
			secretBudget--;
		}
	}

	// Secrets are part of the item-progression contract, not merely rooms with
	// extra medikits. Stock Doom powerups arrive in a deliberate order: basic
	// carrying/combat utility first, exploration and defensive rewards deeper in
	// the map, and encounter-skipping artifacts only on large, hard layouts.
	TArray<int> secretRooms;
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		if (Rooms[ri].isSecret) secretRooms.Push(ri);
	SortByDistance(secretRooms);
	auto AddSecretPowerup = [&](int roomIndex, int type)
	{
		if (secretRooms.Size() == 0) return;
		roomIndex = clamp(roomIndex, 0, (int)secretRooms.Size() - 1);
		RoomInfo& room = Rooms[secretRooms[roomIndex]];
		for (unsigned int item = 0; item < room.powerups.Size(); item++)
			if (room.powerups[item] == type) return;
		room.powerups.Push(type);
	};
	if (secretRooms.Size() > 0)
	{
		AddSecretPowerup(0, 8); // backpack: early optional carrying capacity
		AddSecretPowerup(secretRooms.Size() - 1, 2024); // partial invisibility
		if (Size >= 4)
			AddSecretPowerup(secretRooms.Size() / 2, 2023); // berserk
		if (Size >= 5)
			AddSecretPowerup(secretRooms.Size() - 1, 2013); // soul sphere
		if (Size >= 8)
			AddSecretPowerup(std::max(0, (int)secretRooms.Size() - 2), 2026); // map
		if (Size >= 12 && (themeStyle == ThemeHell || themeStyle == ThemeGothic ||
			themeStyle == ThemeCorrupted))
			AddSecretPowerup(secretRooms.Size() / 2, 2045); // light amplification
		if (Size >= 12 && Difficulty >= 4)
			AddSecretPowerup(secretRooms.Size() - 1, 2022); // invulnerability
		if (doom2Roster && Size >= 20 && Difficulty >= 4)
			AddSecretPowerup(secretRooms.Size() - 1, 83); // Doom II megasphere
	}

	// Large, high-difficulty layouts contain enough individually modest fights
	// that per-room random recovery rolls can under-supply the campaign in
	// aggregate. Guarantee at least one substantial pickup per four authored
	// monsters, first filling combat rooms that received none and only then
	// adding a second pickup to existing caches. This is deterministic and
	// scales with actual encounter pressure rather than the canvas dimensions.
	int totalEnemies = 0;
	int directRecovery = 0;
	for (const RoomInfo& room : Rooms)
	{
		if (room.id < 0) continue;
		totalEnemies += room.enemyCount;
		directRecovery += room.hasHealth ? room.healthCount : 0;
	}
	const int minimumDirectRecovery = (totalEnemies + 3) / 4;
	for (int pass = 0; pass < 3 && directRecovery < minimumDirectRecovery; pass++)
	{
		for (RoomInfo& room : Rooms)
		{
			if (room.id < 0 || room.hasPlayerStart || room.enemyCount <= 0)
				continue;
			if (pass == 0 && (room.hasHealth || (!room.onMainPath && room.enemyCount < 3)))
				continue;
			if (pass == 1 && room.hasHealth)
				continue;
			if (pass == 2 && (!room.hasHealth || room.healthCount >= 3))
				continue;

			if (!room.hasHealth)
			{
				room.hasHealth = true;
				room.healthType = (room.enemyCount >= 4 || room.isArena ||
					room.hasKey || room.hasExit) ? 2012 : 2011;
				room.healthCount = 1;
			}
			else room.healthCount++;
			directRecovery++;
			if (directRecovery >= minimumDirectRecovery) break;
		}
	}

	for (int y = 0; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			ProcGenCell& cell = Grid[y][x];
			if (!cell.present || cell.roomId < 0) continue;
			RoomInfo& room = Rooms[cell.roomId];
			cell.hasWeapon = room.hasWeapon;
			cell.weaponType = room.weaponType;
			cell.hasAmmo = room.hasAmmo;
			cell.ammoType = room.ammoType;
			cell.hasHealth = room.hasHealth;
			cell.healthType = room.healthType;
			cell.hasArmor = room.hasArmor;
			cell.armorType = room.armorType;
			cell.enemyCount = room.enemyCount;
			cell.monsterTier = room.monsterTier;
		}
	}
}
