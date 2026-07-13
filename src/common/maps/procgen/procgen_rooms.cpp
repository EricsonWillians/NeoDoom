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
		return cell.hasPlayerStart || cell.hasExit || cell.hasBoss || cell.hasKey || cell.isLocked;
	};

	auto Compatible = [&](const ProcGenCell& seed, const ProcGenCell& candidate) -> bool
	{
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
		if (seed.hasExit || seed.hasBoss) return 4 + Size / 2 + combatGrowth * 2;
		if (seed.hasKey) return 2 + Size / 2 + combatGrowth;
		if (seed.hasPlayerStart) return 2 + Size / 2;
		if (seed.isArena) return 3 + Size / 2 + combatGrowth * 2 + (RNG() % 2);
		if (seed.isHub) return 3 + Size / 2 + combatGrowth / 2;
		if (seed.onMainPath)
			return 1 + (RNG() % (3 + Size / 2)); // closets through broad multi-cell halls
		if (seed.branchDepth >= 2)
			return 1 + (RNG() % 3);
		return 1 + (RNG() % (3 + (Size >= 3 ? 1 : 0)));
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

				RoomInfo room;
				room.id = Rooms.Size();
				room.minI = room.maxI = x;
				room.minJ = room.maxJ = y;
				room.cellCount = 0;
				Rooms.Push(room);
				const int roomId = Rooms.Size() - 1;

				TArray<std::pair<int, int>> cells;
				cells.Push(std::make_pair(x, y));
				seed.roomId = roomId;
				const int target = TargetRoomSize(seed);

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

							int score = sameNeighbors * 24 + linkedNeighbors * 18;
							if (candidate.pathRank == seed.pathRank) score += 18;
							if (candidate.onMainPath == seed.onMainPath) score += 8;
							score -= abs(width - height) * 3;
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
		room.enemyCount = 0;
		room.monsterTier = 1;
		room.isSecret = false;
		room.hasDoor = false;
		room.hasWeapon = false;
		room.hasAmmo = false;
		room.hasHealth = false;
		room.hasArmor = false;
	}

	int startRoom = -1;
	for (int y = 0; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			ProcGenCell& cell = Grid[y][x];
			if (!cell.present || cell.roomId < 0 || cell.roomId >= (int)Rooms.Size()) continue;
			RoomInfo& room = Rooms[cell.roomId];
			room.hasPlayerStart = room.hasPlayerStart || cell.hasPlayerStart;
			room.hasExit = room.hasExit || cell.hasExit;
			room.hasBoss = room.hasBoss || cell.hasBoss;
			room.onMainPath = room.onMainPath || cell.onMainPath;
			room.isArena = room.isArena || cell.isArena;
			room.isHub = room.isHub || cell.isHub;
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

	static const char* TechWallZones[4][4] = {
		{ "STARTAN3", "STARTAN2", "BROWN96", "BROWNGRN" },
		{ "BROWN1", "BROWN96", "BROWNGRN", "STARTAN2" },
		{ "STONE2", "STONE3", "METAL1", "COMPSPAN" },
		{ "TEKWALL1", "TEKWALL4", "COMPSPAN", "METAL1" }
	};
	static const char* TechFloorZones[4][4] = {
		{ "FLOOR4_8", "FLOOR4_1", "FLOOR4_6", "FLOOR5_1" },
		{ "FLOOR5_1", "FLOOR5_2", "FLAT1", "FLOOR4_6" },
		{ "FLOOR0_1", "FLAT14", "FLOOR4_1", "FLOOR5_2" },
		{ "FLAT20", "FLOOR4_8", "FLAT14", "FLOOR0_1" }
	};
	static const char* TechCeilZones[4][4] = {
		{ "CEIL3_5", "CEIL3_6", "CEIL5_1", "FLAT20" },
		{ "FLAT20", "CEIL5_2", "CEIL3_5", "FLOOR0_1" },
		{ "CEIL5_1", "CEIL5_2", "FLAT14", "CEIL3_6" },
		{ "FLOOR7_2", "FLAT20", "CEIL5_1", "FLAT10" }
	};
	static const char* HellWallZones[4][4] = {
		{ "STONE2", "STONE3", "GSTONE1", "GSTONE2" },
		{ "MARBLE1", "MARBLE2", "MARBLE3", "STONE3" },
		{ "GSTVINE1", "GSTVINE2", "GSTONE1", "WOOD1" },
		{ "SP_HOT1", "GSTONE2", "MARBLE3", "WOOD1" }
	};
	static const char* HellFloorZones[4][4] = {
		{ "FLOOR6_1", "FLOOR6_2", "FLAT5_1", "FLAT5_2" },
		{ "FLAT5_1", "FLAT5_2", "FLOOR7_1", "FLOOR6_1" },
		{ "FLOOR7_2", "FLOOR7_1", "FLAT8", "FLAT10" },
		{ "FLOOR6_2", "FLAT8", "FLAT10", "FLOOR7_2" }
	};
	static const char* HellCeilZones[4][4] = {
		{ "FLAT5_1", "FLAT5_2", "FLOOR6_1", "CEIL5_1" },
		{ "FLOOR7_2", "FLAT10", "FLAT5_2", "CEIL5_2" },
		{ "FLAT10", "FLAT8", "FLOOR7_1", "CEIL5_1" },
		{ "CEIL5_1", "FLAT10", "FLAT8", "FLOOR6_2" }
	};
	static const char* TechAccents[] = { "SUPPORT2", "SUPPORT3", "METAL1", "COMPSPAN" };
	static const char* HellAccents[] = { "GSTVINE2", "GSTONE2", "MARBLE2", "WOOD1" };
	static const double HalfProfiles[8][2] = {
		{ 80.0, 88.0 }, { 88.0, 112.0 }, { 112.0, 88.0 }, { 96.0, 120.0 },
		{ 120.0, 96.0 }, { 104.0, 104.0 }, { 120.0, 120.0 }, { 88.0, 104.0 }
	};
	static const double CornerProfiles[] = { 12.0, 16.0, 20.0, 28.0, 36.0 };
	static const int FloorCadence[] = { 0, 8, -8, 16 };
	const bool hell = Theme.Compare("hell") == 0;

	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		const int phase = clamp(room.distFromStart * 4 / (maxDistance + 1), 0, 3);
		int palette = phase;
		if (!room.onMainPath && !room.hasKey && !room.hasExit)
			palette = clamp(phase + (room.branchDepth >= 2 ? 1 : 0), 0, 3);

		int styleHash = abs(room.id * 37 + room.minI * 17 + room.maxJ * 29 +
			room.cellCount * 13 + room.progressionRank * 7 + room.branchDepth * 19);
		room.visualVariant = styleHash % 8;
		int textureVariant = (styleHash / 3 + room.cellCount + room.branchDepth) % 4;
		room.wallTex = hell ? HellWallZones[palette][textureVariant] : TechWallZones[palette][textureVariant];
		room.floorTex = hell ? HellFloorZones[palette][(textureVariant + room.cellCount) % 4] :
			TechFloorZones[palette][(textureVariant + room.cellCount) % 4];
		room.ceilTex = hell ? HellCeilZones[palette][(textureVariant + 2) % 4] :
			TechCeilZones[palette][(textureVariant + 2) % 4];
		room.accentTex = hell ? HellAccents[(textureVariant + palette) % countof(HellAccents)] :
			TechAccents[(textureVariant + palette) % countof(TechAccents)];

		room.halfWidth = HalfProfiles[room.visualVariant][0];
		room.halfHeight = HalfProfiles[room.visualVariant][1];
		int spanX = room.maxI - room.minI;
		int spanY = room.maxJ - room.minJ;
		if (spanX > spanY)
		{
			room.halfWidth = std::max(room.halfWidth, 112.0);
			room.halfHeight = std::min(room.halfHeight, 104.0);
		}
		else if (spanY > spanX)
		{
			room.halfWidth = std::min(room.halfWidth, 104.0);
			room.halfHeight = std::max(room.halfHeight, 112.0);
		}
		if (room.isArena || room.hasExit)
			room.halfWidth = room.halfHeight = 120.0;
		else if (room.isHub || room.hasKey)
		{
			room.halfWidth = std::max(room.halfWidth, 112.0);
			room.halfHeight = std::max(room.halfHeight, 104.0);
		}
		if (room.hasPlayerStart)
			room.halfWidth = room.halfHeight = 104.0;
		if (room.isLocked)
			room.halfWidth = room.halfHeight = 88.0;
		room.cornerCut = CornerProfiles[(styleHash / 5) % countof(CornerProfiles)];
		if (room.isArena || room.hasBoss)
			room.cornerCut = std::min(room.cornerCut, 16.0);
		room.cornerCut = std::min(room.cornerCut,
			std::max(0.0, std::min(room.halfWidth, room.halfHeight) - 56.0));

		room.floorZ = FloorCadence[phase] + ((room.visualVariant % 3) - 1) * 4;
		if (!room.onMainPath && room.branchDepth >= 2) room.floorZ -= 8;

		int clearHeight = 104 + (room.visualVariant % 4) * 16;
		if (room.cellCount == 1 && !room.hasKey && !room.hasExit)
			clearHeight = 96 + (room.visualVariant % 3) * 16;
		if (room.isHub) clearHeight = 144 + (room.visualVariant % 3) * 16;
		if (room.isArena) clearHeight = 176 + (room.visualVariant % 3) * 16;
		if (room.hasExit || room.hasBoss) clearHeight = 208 + (room.visualVariant % 3) * 16;
		room.ceilZ = room.floorZ + clearHeight;

		room.light = 192 - phase * 8;
		if (!room.onMainPath) room.light -= 8;
		if (room.branchDepth >= 2) room.light -= 8;
		if (room.isArena || room.isHub) room.light += 8;
		if (room.hasPlayerStart || room.hasKey || room.hasExit) room.light += 8;
		room.light = clamp((room.light / 8) * 8, 160, 208);

		if (room.hasPlayerStart)
		{
			room.wallTex = hell ? "STONE2" : "STARTAN3";
			room.floorTex = hell ? "FLOOR6_1" : "FLOOR4_8";
			room.accentTex = hell ? "GSTONE2" : "SUPPORT2";
			room.floorZ = 0;
			room.ceilZ = 128;
			room.enemyCount = 0;
			room.monsterTier = 1;
		}
		else
		{
			// Progression raises pressure in broad steps. Classic difficulty no
			// longer adds a blanket monster to every main-route room, while the
			// explicit arena budgets below still scale predictably.
			int pressure = (Difficulty - 1) / 2;
			if (Difficulty == 2 && ((room.id + phase) % 4) == 0) pressure++;
			if (phase >= 2) pressure++;
			if (Difficulty >= 4 && room.onMainPath && phase > 0) pressure++;
			if (room.branchDepth >= 2) pressure--;
			room.enemyCount = clamp(pressure + (int)(RNG() % 2), 1, 3);
			if (room.isDeadEnd && !room.hasKey) room.enemyCount = std::min(room.enemyCount, 1 + Difficulty / 2);
			if (room.isHub) room.enemyCount = clamp(1 + Difficulty / 2 + phase / 2, 2, 4);
			if (room.isArena) room.enemyCount = clamp(1 + Difficulty / 2 + phase / 2 + room.cellCount / 6, 2, 5);
			if (room.hasKey) room.enemyCount = clamp(1 + Difficulty / 2 + phase / 2 + room.cellCount / 6, 2, 5);
			if (room.isLocked) room.enemyCount = clamp(1 + Difficulty / 3 + phase / 2, 1, 4);
			if (room.hasExit) room.enemyCount = clamp(1 + Difficulty / 2 + Size / 6 + room.cellCount / 8, 2, 5);
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

	// Keep every traversable boundary within Doom's 24-unit step limit while
	// retaining a visible progression cadence.
	for (int pass = 0; pass < 4; pass++)
	{
		for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
		{
			for (unsigned int ai = 0; ai < adjacency[ri].Size(); ai++)
			{
				RoomInfo& a = Rooms[ri];
				RoomInfo& b = Rooms[adjacency[ri][ai]];
				if (a.floorZ - b.floorZ > 24) a.floorZ = b.floorZ + 24;
				if (b.floorZ - a.floorZ > 24) b.floorZ = a.floorZ + 24;
			}
		}
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
			room.hasHealth = true;
			room.healthType = 2011;
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
		}
		if (majorFight || reward || (sustainedFight && room.onMainPath) ||
			(room.onMainPath && (RNG() % 100) < 50))
		{
			room.hasHealth = true;
			room.healthType = majorFight ? 2012 : 2011;
		}
		if (room.hasKey || room.hasBoss || (room.isDeadEnd && room.branchDepth >= 2 && (RNG() % 100) < 40))
		{
			room.hasArmor = true;
			room.armorType = room.hasBoss ? 2019 : 2015;
		}
	}

	// Turn a few optional dead ends into real Doom-style secrets. Selection is
	// deterministic and favors the deepest side limbs; every secret receives a
	// useful recovery bundle even when weapon progression chose another room.
	int secretBudget = 1 + Size / 3;
	for (int pass = 0; pass < 3 && secretBudget > 0; pass++)
	{
		for (int index = (int)sideRooms.Size() - 1; index >= 0 && secretBudget > 0; index--)
		{
			RoomInfo& room = Rooms[sideRooms[index]];
			if (room.isSecret || room.hasKey || room.hasExit || room.isLocked) continue;
			if (pass < 2 && !room.isDeadEnd) continue;
			if (pass == 0 && room.branchDepth < 2) continue;
			if (pass == 2 && room.branchDepth < 1) continue;
			room.isSecret = true;
			room.hasDoor = true;
			room.hasAmmo = true;
			room.ammoType = AmmoForStage(room);
			room.hasHealth = true;
			room.healthType = 2012;
			if (!room.hasArmor)
			{
				room.hasArmor = true;
				room.armorType = 2015;
			}
			secretBudget--;
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
