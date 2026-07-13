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
		const char* ceilTex, int light) -> int
	{
		BuildSector sector;
		sector.floorZ = floorZ;
		sector.ceilZ = ceilZ;
		sector.floorTex = floorTex;
		sector.ceilTex = ceilTex;
		sector.light = clamp(light, 160, 224);
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
		bool dontPegTop = false, bool dontPegBottom = false) -> int
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

	auto AddThing = [&](double x, double y, int type, int angle = 0)
	{
		BuildThing thing;
		thing.x = x;
		thing.y = y;
		thing.type = type;
		thing.angle = angle;
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

	// Pick a small number of clear outdoor landmarks. Every map receives a sky
	// at the exit, and larger maps expose one additional arena or hub.
	TArray<bool> outdoorRooms;
	outdoorRooms.Resize(Rooms.Size());
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++) outdoorRooms[ri] = false;
	int outdoorBudget = 1 + Size / 3;
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		if (Rooms[ri].hasExit)
		{
			outdoorRooms[ri] = true;
			outdoorBudget--;
		}
	}
	for (int pass = 0; pass < 2 && outdoorBudget > 0; pass++)
	{
		for (unsigned int ri = 0; ri < Rooms.Size() && outdoorBudget > 0; ri++)
		{
			const RoomInfo& room = Rooms[ri];
			if (outdoorRooms[ri] || room.hasPlayerStart || room.hasKey || room.isLocked) continue;
			bool candidate = (pass == 0) ? room.isArena : room.isHub;
			if (!candidate || room.cellCount < 2) continue;
			outdoorRooms[ri] = true;
			outdoorBudget--;
		}
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

				double halfWidth = door ? 48.0 : 56.0;
				if (!door && (Rooms[roomA].isArena || Rooms[roomB].isArena)) halfWidth = 72.0;
				else if (!door && (Rooms[roomA].isHub || Rooms[roomB].isHub)) halfWidth = 64.0;
				else if (!door && (Rooms[roomA].branchDepth >= 2 || Rooms[roomB].branchDepth >= 2)) halfWidth = 44.0;
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
				AddWall(topLeft, top, cx - topRef.halfWidth, top, roomSector, wall);
				AddPortal(cx - topRef.halfWidth, top, cx + topRef.halfWidth, top,
					roomSector, topRef, wall);
				AddWall(cx + topRef.halfWidth, top, topRight, top, roomSector, wall);
			}
			else AddWall(topLeft, top, topRight, top, roomSector, wall);
			AddWall(topRight, top, right, rightTop, roomSector, cornerWall);

			// East edge: top -> bottom.
			if (rightRef.sector >= 0)
			{
				AddWall(right, rightTop, right, cy + rightRef.halfWidth, roomSector, wall);
				AddPortal(right, cy + rightRef.halfWidth, right, cy - rightRef.halfWidth,
					roomSector, rightRef, wall);
				AddWall(right, cy - rightRef.halfWidth, right, rightBottom, roomSector, wall);
			}
			else AddWall(right, rightTop, right, rightBottom, roomSector, wall);
			AddWall(right, rightBottom, bottomRight, bottom, roomSector, cornerWall);

			// South/world-bottom edge: right -> left (grid DIR_N).
			if (bottomRef.sector >= 0)
			{
				AddWall(bottomRight, bottom, cx + bottomRef.halfWidth, bottom, roomSector, wall);
				AddPortal(cx + bottomRef.halfWidth, bottom, cx - bottomRef.halfWidth, bottom,
					roomSector, bottomRef, wall);
				AddWall(cx - bottomRef.halfWidth, bottom, bottomLeft, bottom, roomSector, wall);
			}
			else AddWall(bottomRight, bottom, bottomLeft, bottom, roomSector, wall);
			AddWall(bottomLeft, bottom, left, leftBottom, roomSector, cornerWall);

			// West edge: bottom -> top.
			if (leftRef.sector >= 0)
			{
				AddWall(left, leftBottom, left, cy - leftRef.halfWidth, roomSector, wall);
				AddPortal(left, cy - leftRef.halfWidth, left, cy + leftRef.halfWidth,
					roomSector, leftRef, wall);
				AddWall(left, cy + leftRef.halfWidth, left, leftTop, roomSector, wall);
			}
			else AddWall(left, leftBottom, left, leftTop, roomSector, wall);
			AddWall(left, leftTop, topLeft, top, roomSector, cornerWall);
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

					// Static recessed jambs flank a classic 16-unit moving door.
					AddWall(x1, top, doorLeft, top, Rooms[roomA].sectorIdx, corridorWall);
					AddWall(doorLeft, top, doorRight, top, east.doorSector, track);
					AddWall(doorRight, top, x2, top, Rooms[roomB].sectorIdx, corridorWall);
					AddWall(doorLeft, bottom, x1, bottom, Rooms[roomA].sectorIdx, corridorWall);
					AddWall(doorRight, bottom, doorLeft, bottom, east.doorSector, track);
					AddWall(x2, bottom, doorRight, bottom, Rooms[roomB].sectorIdx, corridorWall);

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

					AddWall(right, doorBottom, right, y1, Rooms[roomA].sectorIdx, corridorWall);
					AddWall(right, doorTop, right, doorBottom, north.doorSector, track);
					AddWall(right, y2, right, doorTop, Rooms[roomB].sectorIdx, corridorWall);
					AddWall(left, y1, left, doorBottom, Rooms[roomA].sectorIdx, corridorWall);
					AddWall(left, doorBottom, left, doorTop, north.doorSector, track);
					AddWall(left, doorTop, left, y2, Rooms[roomB].sectorIdx, corridorWall);

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

	auto AddLandmarkPlatform = [&](const RoomInfo& room, double cx, double cy, bool sky) -> int
	{
		double half = room.isArena ? 64.0 : 48.0;
		double raise = room.isArena ? 16.0 : 8.0;
		if (room.hasKey) { half = 48.0; raise = 16.0; }
		if (room.hasPlayerStart) { half = 48.0; raise = 8.0; }
		const bool hell = Theme.Compare("hell") == 0;
		const char* floor = hell ? "FLOOR7_2" : "FLAT20";
		if (room.hasKey) floor = hell ? "FLAT5_1" : "FLOOR0_1";
		else if (room.hasPlayerStart) floor = hell ? "FLOOR6_1" : "FLOOR5_1";
		const char* ceiling = sky ? "F_SKY1" : SafeTexture(room.ceilTex, "CEIL3_5");
		const char* step = "STEP1";
		double featureCeil = room.ceilZ;
		if (!sky && room.isHub && !room.hasPlayerStart)
			featureCeil = std::max(room.floorZ + raise + 80.0, room.ceilZ - 16.0);
		int platformLight = sky ? std::max(room.light + 8, 192) : room.light + 8;
		int platformSector = AddSector(room.floorZ + raise, featureCeil,
			floor, ceiling, platformLight);

		auto AddStep = [&](double x1, double y1, double x2, double y2)
		{
			AddLine(x1, y1, x2, y2, platformSector, room.sectorIdx,
				step, nullptr, step, step, nullptr, step,
				false, 0, 0, 0, 0, 0, 0, 0,
				false, false, false, true, true);
		};

		// Clockwise: the raised sector is always on the front/right side.
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

		auto CellPosition = [&](int index, double& px, double& py)
		{
			index = clamp(index, 0, (int)roomCells.Size() - 1);
			px = CellCenterX(roomCells[index].first);
			py = CellCenterY(roomCells[index].second);
		};

		static const double slotX[] = { 0, 48, -48, 0, 0, 48, -48, 48, -48 };
		static const double slotY[] = { 0, 0, 0, 48, -48, 48, 48, -48, -48 };
		auto SlotPosition = [&](int slot, double& px, double& py)
		{
			int cellIndex = (slot / countof(slotX)) % (int)roomCells.Size();
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
		if (room.cellCount >= 2 && (room.isArena || room.isHub || room.hasPlayerStart) &&
			!room.isLocked)
		{
			CellPosition(landmarkCell, anchorX, anchorY);
			landmarkSector = AddLandmarkPlatform(room, anchorX, anchorY, outdoorRooms[ri]);
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

		static const double enemyX[] = { -64, 64, -64, 64, 0, 0, -40, 40, -72, 72, -24, 24 };
		static const double enemyY[] = { -56, -56, 56, 56, -72, 72, -24, 24, 0, 0, 64, -64 };
		for (int enemy = 0; enemy < room.enemyCount; enemy++)
		{
			int cellIndex = (enemy + room.progressionRank) % (int)roomCells.Size();
			if (room.hasBoss && roomCells.Size() > 1 && cellIndex == exitCell)
				cellIndex = (cellIndex + 1) % (int)roomCells.Size();
			double x, y;
			CellPosition(cellIndex, x, y);
			double targetX = x;
			double targetY = y;
			int pattern = (enemy / std::max(1, (int)roomCells.Size()) + enemy) % countof(enemyX);
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
			int attempts = (int)roomCells.Size() * countof(decorX);
			for (int attempt = 0; attempt < attempts; attempt++)
			{
				int cellIndex = (landmarkCell + 1 + attempt / countof(decorX)) %
					(int)roomCells.Size();
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
			"thing\n{\n\tx = %.2f;\n\ty = %.2f;\n\tangle = %d;\n\ttype = %d;\n"
			"\tskill1 = true;\n\tskill2 = true;\n\tskill3 = true;\n\tskill4 = true;\n\tskill5 = true;\n"
			"\tsingle = true;\n\tcoop = true;\n\tdm = true;\n}\n\n",
			thing.x, thing.y, thing.angle, thing.type);
	}

	return vertices.Size() > 0 && sectors.Size() > 0 && lines.Size() > 0;
}
