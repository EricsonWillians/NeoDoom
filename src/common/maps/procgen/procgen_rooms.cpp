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

	// Initialize each present cell as its own room
	for (int j = 0; j < H; j++)
	{
		for (int i = 0; i < W; i++)
		{
			if (!Grid[j][i].present) continue;
			RoomInfo room;
			room.id = (int)Rooms.Size();
			room.minI = room.maxI = i;
			room.minJ = room.maxJ = j;
			room.cellCount = 1;
			Grid[j][i].roomId = room.id;
			Rooms.Push(room);
		}
	}

	// Cap room size based on grid dimensions
	int smallerAxis = W < H ? W : H;
	int maxRoomSize = 2 + smallerAxis / 4;
	if (maxRoomSize > 2) maxRoomSize = 2;
	if (maxRoomSize < 2) maxRoomSize = 2;

	auto CellsCompatible = [&](const ProcGenCell& a, const ProcGenCell& b) -> bool
	{
		if (a.hasPlayerStart != b.hasPlayerStart) return false;
		if (a.hasExit != b.hasExit) return false;
		if (a.hasKey != b.hasKey) return false;
		if (a.hasKey && a.keyType != b.keyType) return false;
		if (a.isLocked != b.isLocked) return false;
		if (a.isLocked && a.lockType != b.lockType) return false;
		if (a.hasBoss != b.hasBoss) return false;
		if (a.onMainPath != b.onMainPath) return false;
		if (a.onMainPath && b.onMainPath && a.pathRank != b.pathRank) return false;
		if ((a.hasKey || b.hasKey || a.isLocked || b.isLocked || a.hasBoss || b.hasBoss) &&
			a.pathRank != b.pathRank) return false;
		if (a.branchDepth > 0 && b.branchDepth > 0 && abs(a.branchDepth - b.branchDepth) > 1) return false;
		if (a.isHub != b.isHub) return false;
		if (a.isArena != b.isArena && (a.isArena || b.isArena)) return false;
		if (a.connectionCount >= 3 || b.connectionCount >= 3)
		{
			if (a.pathRank != b.pathRank) return false;
		}
		if ((a.branchDepth >= 2 || b.branchDepth >= 2) && a.branchDepth != b.branchDepth) return false;
		if ((a.hasWeapon || b.hasWeapon) && (a.branchDepth > 0 || b.branchDepth > 0) &&
			a.branchDepth != b.branchDepth) return false;
		if (a.isHub != b.isHub && (a.onMainPath || b.onMainPath)) return false;
		if (a.onMainPath != b.onMainPath && (a.isArena || b.isArena)) return false;
		if (a.branchDepth == 0 && b.branchDepth > 1 && a.onMainPath) return false;
		if (b.branchDepth == 0 && a.branchDepth > 1 && b.onMainPath) return false;
		return true;
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

			int localMaxRoomSize = maxRoomSize;
			if (roomSeed->hasPlayerStart || roomSeed->hasExit || roomSeed->hasBoss ||
				roomSeed->hasKey || roomSeed->isLocked || roomSeed->isHub || roomSeed->isArena ||
				roomSeed->hasWeapon)
			{
				localMaxRoomSize = 1;
			}
			else if (roomSeed->onMainPath)
			{
				localMaxRoomSize = 1;
			}
			else if (roomSeed->branchDepth >= 2)
			{
				localMaxRoomSize = 1;
			}
			else if (roomSeed->branchDepth >= 1)
			{
				localMaxRoomSize = 1;
			}

			int rw = room.maxI - room.minI + 1;
			int rh = room.maxJ - room.minJ + 1;
			if (rw >= localMaxRoomSize && rh >= localMaxRoomSize) continue;

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
				if (newW > localMaxRoomSize || newH > localMaxRoomSize)
					continue;

				for (unsigned int mi = 0; mi < mergedIds.Size(); mi++)
				{
					int oldId = mergedIds[mi];
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
				anyExpanded = true;
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
			room.monsterTier = std::min(room.monsterTier, 2);
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
			else if (room.branchDepth >= 2)
			{
				room.hasDoor = true;
			}
			else if (room.hasWeapon && !room.onMainPath)
			{
				room.hasDoor = true;
			}
			else if (room.onMainPath && !room.isHub && !room.isArena && room.progressionRank >= 1)
			{
				room.hasDoor = (room.progressionRank % 2) == 1;
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
			room.enemyCount = std::max(room.enemyCount, 2 + Difficulty / 2 + (room.cellCount >= 3));
			room.monsterTier = std::min(5, std::max(room.monsterTier, 2 + Difficulty / 2));
		}
		if (room.hasKey)
		{
			room.enemyCount = std::max(room.enemyCount, 2 + Difficulty / 2 + (room.cellCount >= 3));
			room.monsterTier = std::min(5, std::max(room.monsterTier, 2 + Difficulty / 2));
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
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.id < 0) continue;
		progressionRooms.Push(ri);
		if (room.hasPlayerStart) continue;

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
		else if (room.hasKey || room.isLocked || room.isArena || room.isHub) minEnemies = 2 + room.cellCount / 2;
		else if (room.onMainPath) minEnemies = 1 + room.progressionRank / 3;

		int maxAllowed = room.enemyCount;
		if (effectiveStage <= 1)
			maxAllowed = std::min(maxAllowed, room.isArena ? 4 : 3);
		else if (effectiveStage == 2)
			maxAllowed = std::min(maxAllowed, room.isArena ? 6 : 5);
		else if (effectiveStage == 3)
			maxAllowed = std::min(maxAllowed, room.isArena ? 8 : 6);
		else
			maxAllowed = std::min(maxAllowed, room.isArena ? 10 + Difficulty / 2 : 8 + Difficulty / 2);

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
