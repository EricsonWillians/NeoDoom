/*
** procgen_udmf.cpp
**
** UDMF TEXTMAP output builder: vertices, sectors, pillars, doors,
** sidedefs, linedefs, and thing placement emission.
**
**---------------------------------------------------------------------------
*/

#include "procgen_internal.h"

using namespace ProcGen;

// ---------------------------------------------------------------------------
// UDMF Builder Helpers
// ---------------------------------------------------------------------------

static void AppendSector(FString& out, double floorz, double ceilz,
	const char* floortex, const char* ceiltex, int light)
{
	out.AppendFormat(
		"sector\n"
		"{\n"
		"\theightfloor = %.0f;\n"
		"\theightceiling = %.0f;\n"
		"\ttexturefloor = \"%s\";\n"
		"\ttextureceiling = \"%s\";\n"
		"\tlightlevel = %d;\n"
		"}\n\n",
		floorz, ceilz, floortex, ceiltex, light);
}

static void AppendVertex(FString& out, double x, double y)
{
	out.AppendFormat(
		"vertex\n"
		"{\n"
		"\tx = %.2f;\n"
		"\ty = %.2f;\n"
		"}\n\n",
		x, y);
}

static void AppendLinedef(FString& out, int v1, int v2, int sidefront, int sideback,
	bool twosided, int special = 0, int locknumber = 0,
	bool dontpegtop = false, bool dontpegbottom = false, bool forceBlocking = false,
	int arg0 = 0, int arg1 = 0, int arg2 = 0, int arg3 = 0, int arg4 = 0)
{
	out.AppendFormat(
		"linedef\n"
		"{\n"
		"\tv1 = %d;\n"
		"\tv2 = %d;\n"
		"\tsidefront = %d;\n",
		v1, v2, sidefront);

	if (sideback >= 0)
	{
		out.AppendFormat("\tsideback = %d;\n", sideback);
		out += "\ttwosided = true;\n";
	}

	if (forceBlocking || sideback < 0)
	{
		out += "\tblocking = true;\n";
	}

	if (special > 0)
	{
		out.AppendFormat("\tspecial = %d;\n", special);
	}
	if (locknumber > 0)
	{
		out.AppendFormat("\tlocknumber = %d;\n", locknumber);
	}
	if (arg0 > 0)
	{
		out.AppendFormat("\targ0 = %d;\n", arg0);
	}
	if (arg1 > 0)
	{
		out.AppendFormat("\targ1 = %d;\n", arg1);
	}
	if (arg2 > 0)
	{
		out.AppendFormat("\targ2 = %d;\n", arg2);
	}
	if (arg3 > 0)
	{
		out.AppendFormat("\targ3 = %d;\n", arg3);
	}
	if (arg4 > 0)
	{
		out.AppendFormat("\targ4 = %d;\n", arg4);
	}

	if (dontpegtop)
		out += "\tdontpegtop = true;\n";
	if (dontpegbottom)
		out += "\tdontpegbottom = true;\n";

	out += "}\n\n";
}

static void AppendSidedef(FString& out, int sector,
	const char* top, const char* mid, const char* bot,
	int offsetx = 0, int offsety = 0)
{
	const char* safeTop = (top && top[0] != '\0') ? top : "-";
	const char* safeMid = (mid && mid[0] != '\0') ? mid : "-";
	const char* safeBot = (bot && bot[0] != '\0') ? bot : "-";

	out.AppendFormat(
		"sidedef\n"
		"{\n"
		"\tsector = %d;\n",
		sector);

	if (strcmp(safeTop, "-") != 0)
		out.AppendFormat("\ttexturetop = \"%s\";\n", safeTop);
	if (strcmp(safeMid, "-") != 0)
		out.AppendFormat("\ttexturemiddle = \"%s\";\n", safeMid);
	if (strcmp(safeBot, "-") != 0)
		out.AppendFormat("\ttexturebottom = \"%s\";\n", safeBot);

	out.AppendFormat(
		"\toffsetx = %d;\n"
		"\toffsety = %d;\n"
		"}\n\n",
		offsetx, offsety);
}

static void AppendThing(FString& out, double x, double y, int ednum,
	int angle = 0)
{
	out.AppendFormat(
		"thing\n"
		"{\n"
		"\tx = %.2f;\n"
		"\ty = %.2f;\n"
		"\tangle = %d;\n"
		"\ttype = %d;\n"
		"\tskill1 = true;\n"
		"\tskill2 = true;\n"
		"\tskill3 = true;\n"
		"\tskill4 = true;\n"
		"\tskill5 = true;\n"
		"\tsingle = true;\n"
		"\tcoop = true;\n"
		"\tdm = true;\n"
		"}\n\n",
		x, y, angle, ednum);
}

// ---------------------------------------------------------------------------
// BuildUDMF
// ---------------------------------------------------------------------------

bool FProceduralMapGenerator::BuildUDMF(int W, int H)
{
	FString& s = UDMFBuffer;
	s = "namespace = \"zdoom\";\n\n";

	int vertCols = W + 1;
	int vertRows = H + 1;
	static const int DOOR_HALF = 16;

	// --- Vertices (grid) ---
	for (int j = 0; j < vertRows; j++)
	{
		for (int i = 0; i < vertCols; i++)
		{
			double vx = (i - W / 2) * CELL_SIZE;
			double vy = (j - H / 2) * CELL_SIZE;
			AppendVertex(s, vx, vy);
		}
	}

	auto VIndex = [vertCols](int i, int j) -> int
	{
		return j * vertCols + i;
	};

	auto IsValidRoomIndex = [&](int roomIdx) -> bool
	{
		return roomIdx >= 0 && roomIdx < (int)Rooms.Size();
	};

	// --- Sectors (rooms) ---
	int sectorCount = 0;
	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.id < 0) continue;
		room.sectorIdx = sectorCount++;
		AppendSector(s, room.floorZ, room.ceilZ,
			room.floorTex.GetChars(),
			room.ceilTex.GetChars(),
			room.light);
	}
	// Assign cell sector indices from their room
	for (int j = 0; j < H; j++)
		for (int i = 0; i < W; i++)
			if (Grid[j][i].present && IsValidRoomIndex(Grid[j][i].roomId) &&
				Rooms[Grid[j][i].roomId].id >= 0 && Rooms[Grid[j][i].roomId].sectorIdx >= 0)
			{
				Grid[j][i].sectorIdx = Rooms[Grid[j][i].roomId].sectorIdx;
			}
			else
			{
				Grid[j][i].present = false;
				Grid[j][i].roomId = -1;
				Grid[j][i].sectorIdx = -1;
			}

	int nextExtraVert = vertCols * vertRows;

	auto EmitVert = [&](double x, double y) -> int
	{
		AppendVertex(s, x, y);
		return nextExtraVert++;
	};

	auto OpeningHalfWidth = [&](const RoomInfo& a, const RoomInfo& b) -> double
	{
		if ((a.hasExit || a.hasBoss) || (b.hasExit || b.hasBoss)) return 104.0;
		if ((a.isHub && b.onMainPath) || (b.isHub && a.onMainPath)) return 92.0;
		if (a.isArena || b.isArena) return 88.0;
		if (a.isHub || b.isHub) return 84.0;
		if (a.onMainPath != b.onMainPath) return 40.0;
		if (a.hasDoor || b.hasDoor) return 44.0;
		if (a.isLocked || b.isLocked) return 48.0;
		if (a.hasKey || b.hasKey) return 60.0;
		if (a.onMainPath && b.onMainPath) return 84.0;
		if (a.isDeadEnd || b.isDeadEnd || a.branchDepth >= 2 || b.branchDepth >= 2) return 32.0;
		return 64.0;
	};

	// --- Pillars (interior cover for large rooms) ---
	struct PillarInfoExt
	{
		int sectorIdx;
		int roomIdx;
		int vbl, vbr, vtr, vtl;
	};
	TArray<PillarInfoExt> pillarExts;

	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.id < 0) continue;
		if (room.hasKey) continue;

		int roomW = room.maxI - room.minI + 1;
		int roomH = room.maxJ - room.minJ + 1;
		int area = room.cellCount;
		if (area < 4) continue;
		if (!room.isArena && !room.isHub && !room.hasBoss) continue;

		int numPillars = 1;
		if (area >= 6) numPillars = 1 + (RNG() % 2);
		if (area >= 8) numPillars = 2 + (RNG() % 2);

		double rX0 = (room.minI - W / 2.0) * CELL_SIZE;
		double rX1 = (room.maxI + 1 - W / 2.0) * CELL_SIZE;
		double rY0 = (room.minJ - H / 2.0) * CELL_SIZE;
		double rY1 = (room.maxJ + 1 - H / 2.0) * CELL_SIZE;

		for (int p = 0; p < numPillars; p++)
		{
			double margin = 80.0;
			double px0 = rX0 + margin;
			double px1 = rX1 - margin;
			double py0 = rY0 + margin;
			double py1 = rY1 - margin;
			if (px1 <= px0 || py1 <= py0) continue;

			double cx = px0 + (px1 - px0) * 0.5;
			double cy = py0 + (py1 - py0) * 0.5;
			if (numPillars > 1)
			{
				if (p == 0) { cx = px0 + (px1 - px0) * 0.35; cy = py0 + (py1 - py0) * 0.35; }
				else if (p == 1) { cx = px0 + (px1 - px0) * 0.65; cy = py0 + (py1 - py0) * 0.65; }
				else { cx = px0 + (px1 - px0) * 0.5; cy = py0 + (py1 - py0) * 0.5; }
			}

			double half = 32.0;
			int vbl = EmitVert(cx - half, cy - half);
			int vbr = EmitVert(cx + half, cy - half);
			int vtr = EmitVert(cx + half, cy + half);
			int vtl = EmitVert(cx - half, cy + half);

			int pSector = sectorCount++;
			double pFloor = room.floorZ + 56.0;
			if (pFloor + 8.0 >= room.ceilZ) pFloor = room.ceilZ - 8.0;
			AppendSector(s, pFloor, room.ceilZ,
				room.floorTex.GetChars(), room.ceilTex.GetChars(), room.light);

			PillarInfoExt pi;
			pi.sectorIdx = pSector;
			pi.roomIdx = (int)ri;
			pi.vbl = vbl; pi.vbr = vbr; pi.vtr = vtr; pi.vtl = vtl;
			pillarExts.Push(pi);
		}
	}

	struct InsetInfoExt
	{
		int sectorIdx;
		int roomIdx;
		int vbl, vbr, vtr, vtl;
	};
	TArray<InsetInfoExt> insetExts;

	auto AddInsetFeature = [&](int roomIndex, double cx, double cy, double halfX, double halfY,
		double floorOffset, const char* floorTex, int lightDelta)
	{
		RoomInfo& room = Rooms[roomIndex];
		int vbl = EmitVert(cx - halfX, cy - halfY);
		int vbr = EmitVert(cx + halfX, cy - halfY);
		int vtr = EmitVert(cx + halfX, cy + halfY);
		int vtl = EmitVert(cx - halfX, cy + halfY);

		int featureSector = sectorCount++;
		double featureFloor = room.floorZ + floorOffset;
		if (featureFloor + 16.0 >= room.ceilZ) featureFloor = room.ceilZ - 16.0;
		AppendSector(s, featureFloor, room.ceilZ,
			floorTex ? floorTex : room.floorTex.GetChars(),
			room.ceilTex.GetChars(),
			room.light + lightDelta);

		InsetInfoExt inset;
		inset.sectorIdx = featureSector;
		inset.roomIdx = roomIndex;
		inset.vbl = vbl; inset.vbr = vbr; inset.vtr = vtr; inset.vtl = vtl;
		insetExts.Push(inset);
	};

	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.id < 0) continue;
		if (room.cellCount < 2) continue;

		double rX0 = (room.minI - W / 2.0) * CELL_SIZE;
		double rX1 = (room.maxI + 1 - W / 2.0) * CELL_SIZE;
		double rY0 = (room.minJ - H / 2.0) * CELL_SIZE;
		double rY1 = (room.maxJ + 1 - H / 2.0) * CELL_SIZE;
		double cx = (rX0 + rX1) * 0.5;
		double cy = (rY0 + rY1) * 0.5;
		double roomW = rX1 - rX0;
		double roomH = rY1 - rY0;

		if (room.hasPlayerStart)
		{
			AddInsetFeature((int)ri, cx, cy, 48.0, 48.0, 16.0, room.floorTex.GetChars(), 16);
			if (roomW >= 320.0)
				AddInsetFeature((int)ri, cx, cy + 80.0, 72.0, 24.0, -8.0, room.floorTex.GetChars(), -4);
		}
		else if (room.hasKey)
		{
			AddInsetFeature((int)ri, cx, cy, 40.0, 40.0, 24.0, room.floorTex.GetChars(), 16);
			if (roomW >= 320.0)
			{
				AddInsetFeature((int)ri, cx - roomW * 0.25, cy, 28.0, 52.0, -16.0, room.floorTex.GetChars(), -8);
				AddInsetFeature((int)ri, cx + roomW * 0.25, cy, 28.0, 52.0, -16.0, room.floorTex.GetChars(), -8);
			}
		}
		else if (room.hasExit || room.hasBoss)
		{
			AddInsetFeature((int)ri, cx, cy, 64.0, 64.0, 24.0, room.floorTex.GetChars(), 24);
			if (roomW >= 384.0 && roomH >= 384.0)
			{
				AddInsetFeature((int)ri, cx - 96.0, cy, 32.0, 80.0, -20.0, room.floorTex.GetChars(), -12);
				AddInsetFeature((int)ri, cx + 96.0, cy, 32.0, 80.0, -20.0, room.floorTex.GetChars(), -12);
			}
			if (roomH >= 384.0)
			{
				AddInsetFeature((int)ri, cx, cy - roomH * 0.18, 72.0, 20.0, 16.0, room.floorTex.GetChars(), 8);
				AddInsetFeature((int)ri, cx, cy + roomH * 0.18, 72.0, 20.0, 16.0, room.floorTex.GetChars(), 8);
			}
		}
		else if (room.isHub && room.cellCount >= 3 && (RNG() % 2) == 0)
		{
			AddInsetFeature((int)ri, cx, cy, 52.0, 52.0, -16.0, room.floorTex.GetChars(), -8);
			if (roomW >= 384.0)
			{
				AddInsetFeature((int)ri, cx - roomW * 0.22, cy, 24.0, 64.0, 12.0, room.floorTex.GetChars(), 8);
				AddInsetFeature((int)ri, cx + roomW * 0.22, cy, 24.0, 64.0, 12.0, room.floorTex.GetChars(), 8);
			}
			if (roomH >= 384.0)
			{
				AddInsetFeature((int)ri, cx, cy - roomH * 0.22, 64.0, 24.0, 12.0, room.floorTex.GetChars(), 8);
				AddInsetFeature((int)ri, cx, cy + roomH * 0.22, 64.0, 24.0, 12.0, room.floorTex.GetChars(), 8);
			}
		}
		else if (room.hasWeapon && !room.hasPlayerStart && !room.hasExit)
		{
			AddInsetFeature((int)ri, cx, cy, 42.0, 42.0, 18.0, room.floorTex.GetChars(), 20);
			if (roomW >= 320.0)
			{
				AddInsetFeature((int)ri, cx - roomW * 0.20, cy, 22.0, 44.0, -10.0, room.floorTex.GetChars(), -4);
				AddInsetFeature((int)ri, cx + roomW * 0.20, cy, 22.0, 44.0, -10.0, room.floorTex.GetChars(), -4);
			}
		}
		else if (room.onMainPath && !room.isHub && !room.isArena && !room.hasKey && !room.isLocked &&
			!room.hasExit && !room.hasBoss && room.cellCount >= 2)
		{
			AddInsetFeature((int)ri, cx, cy, 28.0, 72.0, -12.0, room.floorTex.GetChars(), -6);
			if (roomW >= 320.0)
			{
				AddInsetFeature((int)ri, cx - roomW * 0.18, cy, 18.0, 40.0, 12.0, room.floorTex.GetChars(), 8);
				AddInsetFeature((int)ri, cx + roomW * 0.18, cy, 18.0, 40.0, 12.0, room.floorTex.GetChars(), 8);
			}
			if (room.progressionRank >= 2 && roomH >= 320.0)
			{
				AddInsetFeature((int)ri, cx, cy - roomH * 0.18, 56.0, 18.0, 14.0, room.floorTex.GetChars(), 8);
				AddInsetFeature((int)ri, cx, cy + roomH * 0.18, 56.0, 18.0, 14.0, room.floorTex.GetChars(), 8);
			}
		}
		else if ((room.hasExit || room.hasBoss) && room.cellCount >= 3)
		{
			AddInsetFeature((int)ri, cx, cy - roomH * 0.18, 72.0, 20.0, 16.0, room.floorTex.GetChars(), 8);
			AddInsetFeature((int)ri, cx, cy + roomH * 0.18, 72.0, 20.0, 16.0, room.floorTex.GetChars(), 8);
		}
		else if (room.isLocked && room.cellCount >= 2)
		{
			AddInsetFeature((int)ri, cx, cy, 40.0, 40.0, -12.0, room.floorTex.GetChars(), -8);
			if (roomW >= 320.0)
				AddInsetFeature((int)ri, cx, cy, 84.0, 20.0, 16.0, room.floorTex.GetChars(), 12);
		}
		else if (room.isDeadEnd && !room.hasWeapon && !room.hasAmmo && room.cellCount >= 2)
		{
			AddInsetFeature((int)ri, cx, cy, 36.0, 36.0, 12.0, room.floorTex.GetChars(), 12);
		}
		else if (!room.hasPlayerStart && !room.hasExit && !room.hasBoss &&
			room.isArena && room.cellCount >= 4 && (RNG() % 3) != 0)
		{
			const char* featureTex = room.floorTex.GetChars();
			if (Theme.Compare("hell") == 0)
				featureTex = "LAVA1";
			else if (Theme.Compare("techbase") == 0)
				featureTex = "NUKAGE1";
			AddInsetFeature((int)ri, cx, cy, 56.0, 56.0, -24.0, featureTex, -16);
			if (roomW >= 384.0)
			{
				AddInsetFeature((int)ri, cx - roomW * 0.24, cy, 24.0, 48.0, 20.0, room.floorTex.GetChars(), 8);
				AddInsetFeature((int)ri, cx + roomW * 0.24, cy, 24.0, 48.0, 20.0, room.floorTex.GetChars(), 8);
			}
		}

		// Secondary layered composition pass so important rooms feel like
		// designed spaces rather than one center platform.
		if (room.isHub && room.progressionRank >= 2 && room.cellCount >= 3)
		{
			AddInsetFeature((int)ri, cx, cy, 96.0, 16.0, 14.0, room.floorTex.GetChars(), 10);
			if (roomH >= 384.0)
			{
				AddInsetFeature((int)ri, cx, cy - roomH * 0.28, 56.0, 18.0, -10.0, room.floorTex.GetChars(), -6);
				AddInsetFeature((int)ri, cx, cy + roomH * 0.28, 56.0, 18.0, -10.0, room.floorTex.GetChars(), -6);
			}
		}

		if (room.hasKey && roomH >= 320.0)
		{
			AddInsetFeature((int)ri, cx, cy - roomH * 0.22, 64.0, 18.0, 14.0, room.floorTex.GetChars(), 10);
			AddInsetFeature((int)ri, cx, cy + roomH * 0.22, 64.0, 18.0, -10.0, room.floorTex.GetChars(), -6);
		}

		if (room.isLocked && room.cellCount >= 3)
		{
			AddInsetFeature((int)ri, cx, cy - 72.0, 56.0, 16.0, 18.0, room.floorTex.GetChars(), 12);
			if (roomW >= 320.0)
			{
				AddInsetFeature((int)ri, cx - roomW * 0.20, cy + 56.0, 20.0, 36.0, -10.0, room.floorTex.GetChars(), -6);
				AddInsetFeature((int)ri, cx + roomW * 0.20, cy + 56.0, 20.0, 36.0, -10.0, room.floorTex.GetChars(), -6);
			}
		}

		if ((room.hasExit || room.hasBoss) && room.cellCount >= 3)
		{
			AddInsetFeature((int)ri, cx, cy + 104.0, 96.0, 18.0, 18.0, room.floorTex.GetChars(), 12);
			if (roomW >= 448.0)
			{
				AddInsetFeature((int)ri, cx - roomW * 0.24, cy, 22.0, 72.0, -12.0, room.floorTex.GetChars(), -8);
				AddInsetFeature((int)ri, cx + roomW * 0.24, cy, 22.0, 72.0, -12.0, room.floorTex.GetChars(), -8);
			}
		}

		if (room.hasWeapon && !room.onMainPath && room.cellCount >= 2)
		{
			AddInsetFeature((int)ri, cx, cy - 64.0, 52.0, 14.0, 16.0, room.floorTex.GetChars(), 10);
		}

		// Irregular silhouette accents for larger spaces so the map stops
		// reading as purely rectangular even when the underlying room bounds are.
		if (room.cellCount >= 4 && roomW >= 384.0 && roomH >= 384.0)
		{
			if ((room.progressionRank + room.branchDepth + (int)ri) % 2 == 0)
			{
				AddInsetFeature((int)ri, cx - roomW * 0.26, cy - roomH * 0.22, 20.0, 44.0, -14.0, room.floorTex.GetChars(), -8);
				AddInsetFeature((int)ri, cx + roomW * 0.22, cy + roomH * 0.26, 44.0, 20.0, 12.0, room.floorTex.GetChars(), 8);
			}
			else
			{
				AddInsetFeature((int)ri, cx + roomW * 0.26, cy - roomH * 0.22, 20.0, 44.0, -14.0, room.floorTex.GetChars(), -8);
				AddInsetFeature((int)ri, cx - roomW * 0.22, cy + roomH * 0.26, 44.0, 20.0, 12.0, room.floorTex.GetChars(), 8);
			}
		}

		if (room.onMainPath && !room.hasKey && !room.isLocked && !room.hasExit && !room.hasBoss &&
			room.cellCount >= 3 && roomH >= 384.0)
		{
			AddInsetFeature((int)ri, cx, cy - roomH * 0.30, 42.0, 16.0, 16.0, room.floorTex.GetChars(), 10);
		}

		if (room.isArena && room.cellCount >= 4)
		{
			AddInsetFeature((int)ri, cx - 88.0, cy - 88.0, 20.0, 20.0, 20.0, room.floorTex.GetChars(), 8);
			AddInsetFeature((int)ri, cx + 88.0, cy + 88.0, 20.0, 20.0, 20.0, room.floorTex.GetChars(), 8);
		}

		// Processional asymmetry pass: give important rooms a sense of
		// approach, reveal, and off-axis composition.
		if (room.onMainPath && room.progressionRank >= 2 && room.cellCount >= 3)
		{
			double side = ((room.progressionRank + room.branchDepth + (int)ri) % 2 == 0) ? -1.0 : 1.0;
			if (!room.hasExit && !room.hasBoss && !room.hasKey && !room.isLocked)
			{
				AddInsetFeature((int)ri, cx + side * roomW * 0.22, cy - roomH * 0.12, 22.0, 56.0, 18.0, room.floorTex.GetChars(), 10);
				AddInsetFeature((int)ri, cx - side * roomW * 0.18, cy + roomH * 0.18, 40.0, 18.0, -10.0, room.floorTex.GetChars(), -6);
			}
		}

		if (room.hasKey && room.cellCount >= 3)
		{
			double side = ((room.keyType + (int)ri) % 2 == 0) ? -1.0 : 1.0;
			AddInsetFeature((int)ri, cx + side * roomW * 0.18, cy - roomH * 0.14, 20.0, 48.0, 16.0, room.floorTex.GetChars(), 10);
			AddInsetFeature((int)ri, cx - side * roomW * 0.14, cy + roomH * 0.20, 48.0, 18.0, -12.0, room.floorTex.GetChars(), -8);
		}

		if (room.isLocked && room.cellCount >= 3)
		{
			double side = ((room.lockType + room.progressionRank) % 2 == 0) ? -1.0 : 1.0;
			AddInsetFeature((int)ri, cx + side * roomW * 0.18, cy - 64.0, 18.0, 52.0, 20.0, room.floorTex.GetChars(), 12);
			AddInsetFeature((int)ri, cx - side * roomW * 0.20, cy + 84.0, 28.0, 20.0, -14.0, room.floorTex.GetChars(), -8);
		}

		if ((room.hasExit || room.hasBoss) && room.cellCount >= 3)
		{
			double side = ((room.progressionRank + (int)ri) % 2 == 0) ? -1.0 : 1.0;
			AddInsetFeature((int)ri, cx + side * roomW * 0.18, cy - roomH * 0.20, 18.0, 64.0, 18.0, room.floorTex.GetChars(), 10);
			AddInsetFeature((int)ri, cx - side * roomW * 0.22, cy + roomH * 0.22, 52.0, 18.0, -14.0, room.floorTex.GetChars(), -10);
		}

		if (room.hasWeapon && !room.onMainPath && room.cellCount >= 2)
		{
			double side = ((room.progressionRank + room.branchDepth + (int)ri) % 2 == 0) ? -1.0 : 1.0;
			AddInsetFeature((int)ri, cx + side * roomW * 0.16, cy + 56.0, 18.0, 44.0, 14.0, room.floorTex.GetChars(), 8);
		}

		if (room.branchDepth >= 2 && !room.hasExit && !room.hasBoss && room.cellCount >= 2)
		{
			double side = ((room.branchDepth + room.progressionRank + (int)ri) % 2 == 0) ? -1.0 : 1.0;
			AddInsetFeature((int)ri, cx + side * roomW * 0.20, cy - roomH * 0.16, 18.0, 40.0, 16.0, room.floorTex.GetChars(), 8);
			AddInsetFeature((int)ri, cx - side * roomW * 0.12, cy + roomH * 0.18, 36.0, 16.0, -12.0, room.floorTex.GetChars(), -8);
			if (roomW >= 320.0)
			{
				AddInsetFeature((int)ri, cx, cy + roomH * 0.28, 52.0, 14.0, 18.0, room.floorTex.GetChars(), 10);
			}
		}

		if (!room.onMainPath && room.branchDepth == 1 && !room.hasWeapon && !room.hasKey &&
			!room.isLocked && !room.hasExit && !room.hasBoss && room.cellCount >= 2)
		{
			double side = ((room.progressionRank + (int)ri) % 2 == 0) ? -1.0 : 1.0;
			AddInsetFeature((int)ri, cx + side * roomW * 0.18, cy, 18.0, 52.0, 14.0, room.floorTex.GetChars(), 8);
			AddInsetFeature((int)ri, cx - side * roomW * 0.14, cy + roomH * 0.18, 44.0, 16.0, -10.0, room.floorTex.GetChars(), -6);
		}

		if (room.isHub && room.cellCount >= 4 && roomW >= 384.0)
		{
			AddInsetFeature((int)ri, cx - roomW * 0.30, cy, 16.0, 52.0, 18.0, room.floorTex.GetChars(), 8);
			AddInsetFeature((int)ri, cx + roomW * 0.30, cy, 16.0, 52.0, 18.0, room.floorTex.GetChars(), 8);
		}

		if (room.hasWeapon && !room.onMainPath && room.cellCount >= 3)
		{
			double side = ((room.progressionRank + (int)ri) % 2 == 0) ? -1.0 : 1.0;
			AddInsetFeature((int)ri, cx + side * roomW * 0.22, cy - roomH * 0.18, 20.0, 52.0, 18.0, room.floorTex.GetChars(), 10);
			AddInsetFeature((int)ri, cx - side * roomW * 0.18, cy + roomH * 0.24, 52.0, 16.0, -12.0, room.floorTex.GetChars(), -8);
		}

		if (room.onMainPath && room.progressionRank >= 3 && room.cellCount >= 3 &&
			!room.hasKey && !room.isLocked && !room.hasExit && !room.hasBoss)
		{
			AddInsetFeature((int)ri, cx - roomW * 0.24, cy, 16.0, 56.0, 18.0, room.floorTex.GetChars(), 10);
			AddInsetFeature((int)ri, cx + roomW * 0.20, cy + roomH * 0.18, 44.0, 16.0, -12.0, room.floorTex.GetChars(), -8);
		}

		if (room.cellCount >= 4 && roomW >= 384.0)
		{
			if (room.isHub)
			{
				AddInsetFeature((int)ri, cx, cy, 18.0, 104.0, -18.0, room.floorTex.GetChars(), -8);
			}
			else if (room.hasWeapon && !room.onMainPath)
			{
				AddInsetFeature((int)ri, cx, cy + roomH * 0.10, 96.0, 14.0, 18.0, room.floorTex.GetChars(), 10);
			}
			else if (room.branchDepth >= 2)
			{
				AddInsetFeature((int)ri, cx + roomW * 0.10, cy - roomH * 0.10, 14.0, 88.0, -16.0, room.floorTex.GetChars(), -10);
			}
			else if (room.onMainPath && room.progressionRank >= 3 && !room.hasExit && !room.hasBoss)
			{
				AddInsetFeature((int)ri, cx, cy - roomH * 0.10, 104.0, 14.0, 18.0, room.floorTex.GetChars(), 10);
			}
		}

		// Stronger layered floor language for larger late/progression spaces.
		if (room.cellCount >= 4 && roomW >= 384.0 && roomH >= 384.0)
		{
			if (room.onMainPath && room.progressionRank >= 4 && !room.hasExit && !room.hasBoss)
			{
				AddInsetFeature((int)ri, cx - roomW * 0.16, cy, 14.0, 112.0, -18.0, room.floorTex.GetChars(), -8);
				AddInsetFeature((int)ri, cx + roomW * 0.14, cy - roomH * 0.14, 64.0, 14.0, 18.0, room.floorTex.GetChars(), 10);
			}
			else if (room.branchDepth >= 2)
			{
				AddInsetFeature((int)ri, cx, cy, 14.0, 96.0, -18.0, room.floorTex.GetChars(), -10);
				AddInsetFeature((int)ri, cx - roomW * 0.14, cy + roomH * 0.12, 72.0, 14.0, 16.0, room.floorTex.GetChars(), 8);
			}
			else if (room.hasWeapon && !room.onMainPath)
			{
				AddInsetFeature((int)ri, cx, cy - roomH * 0.16, 72.0, 14.0, 20.0, room.floorTex.GetChars(), 10);
				AddInsetFeature((int)ri, cx + roomW * 0.16, cy + roomH * 0.10, 14.0, 72.0, -16.0, room.floorTex.GetChars(), -8);
			}
			else if (room.hasExit || room.hasBoss)
			{
				AddInsetFeature((int)ri, cx, cy + roomH * 0.26, 112.0, 14.0, 20.0, room.floorTex.GetChars(), 10);
				AddInsetFeature((int)ri, cx - roomW * 0.18, cy - roomH * 0.18, 14.0, 80.0, -18.0, room.floorTex.GetChars(), -10);
			}
		}

		// Archetype motifs: larger spaces get a stronger primary composition
		// so they read like authored Doom beats rather than decorated boxes.
		if (roomW >= 448.0 && roomH >= 448.0)
		{
			if (room.isHub)
			{
				// Ringwalk-ish hub: central depression plus four raised stations.
				AddInsetFeature((int)ri, cx, cy, 88.0, 88.0, -22.0, room.floorTex.GetChars(), -10);
				AddInsetFeature((int)ri, cx - 120.0, cy, 20.0, 52.0, 18.0, room.floorTex.GetChars(), 10);
				AddInsetFeature((int)ri, cx + 120.0, cy, 20.0, 52.0, 18.0, room.floorTex.GetChars(), 10);
				AddInsetFeature((int)ri, cx, cy - 120.0, 52.0, 20.0, 18.0, room.floorTex.GetChars(), 10);
				AddInsetFeature((int)ri, cx, cy + 120.0, 52.0, 20.0, 18.0, room.floorTex.GetChars(), 10);
			}
			else if (room.hasKey)
			{
				// Shrine/chapel: altar plus flanking aisles.
				AddInsetFeature((int)ri, cx, cy - 112.0, 84.0, 18.0, 20.0, room.floorTex.GetChars(), 10);
				AddInsetFeature((int)ri, cx - 116.0, cy + 24.0, 18.0, 84.0, -16.0, room.floorTex.GetChars(), -8);
				AddInsetFeature((int)ri, cx + 116.0, cy + 24.0, 18.0, 84.0, -16.0, room.floorTex.GetChars(), -8);
			}
			else if (room.hasExit || room.hasBoss)
			{
				// Final chamber: cross-axial arena with a long approach and side trenches.
				AddInsetFeature((int)ri, cx, cy + 132.0, 116.0, 16.0, 18.0, room.floorTex.GetChars(), 10);
				AddInsetFeature((int)ri, cx - 132.0, cy - 20.0, 16.0, 96.0, -18.0, room.floorTex.GetChars(), -10);
				AddInsetFeature((int)ri, cx + 132.0, cy - 20.0, 16.0, 96.0, -18.0, room.floorTex.GetChars(), -10);
				AddInsetFeature((int)ri, cx, cy - 124.0, 72.0, 16.0, 18.0, room.floorTex.GetChars(), 8);
			}
			else if (room.hasWeapon && !room.onMainPath)
			{
				// Reward shrine: long approach bar with asymmetric side pocket.
				double side = ((room.progressionRank + (int)ri) % 2 == 0) ? -1.0 : 1.0;
				AddInsetFeature((int)ri, cx, cy - 120.0, 92.0, 16.0, 18.0, room.floorTex.GetChars(), 10);
				AddInsetFeature((int)ri, cx + side * 120.0, cy + 28.0, 18.0, 88.0, -16.0, room.floorTex.GetChars(), -8);
			}
			else if (room.onMainPath && room.progressionRank >= 3 && !room.isLocked)
			{
				// Processional hall: center trench with staggered side plinths.
				AddInsetFeature((int)ri, cx, cy, 18.0, 124.0, -18.0, room.floorTex.GetChars(), -10);
				AddInsetFeature((int)ri, cx - 112.0, cy - 64.0, 18.0, 44.0, 18.0, room.floorTex.GetChars(), 8);
				AddInsetFeature((int)ri, cx + 112.0, cy + 64.0, 18.0, 44.0, 18.0, room.floorTex.GetChars(), 8);
			}
		}
	}

	int sidedefCount = 0;
	for (const auto& p : pillarExts)
	{
		const char* wtex = Rooms[p.roomIdx].wallTex.GetChars();
		int roomSector = Rooms[p.roomIdx].sectorIdx;
		// Pillar boundaries: two-sided, front=room, back=pillar
		int sf = sidedefCount++;
		AppendSidedef(s, roomSector, wtex, nullptr, wtex);
		int sb = sidedefCount++;
		AppendSidedef(s, p.sectorIdx, wtex, nullptr, wtex);
		AppendLinedef(s, p.vbl, p.vbr, sf, sb, true);

		sf = sidedefCount++;
		AppendSidedef(s, roomSector, wtex, nullptr, wtex);
		sb = sidedefCount++;
		AppendSidedef(s, p.sectorIdx, wtex, nullptr, wtex);
		AppendLinedef(s, p.vbr, p.vtr, sf, sb, true);

		sf = sidedefCount++;
		AppendSidedef(s, roomSector, wtex, nullptr, wtex);
		sb = sidedefCount++;
		AppendSidedef(s, p.sectorIdx, wtex, nullptr, wtex);
		AppendLinedef(s, p.vtr, p.vtl, sf, sb, true);

		sf = sidedefCount++;
		AppendSidedef(s, roomSector, wtex, nullptr, wtex);
		sb = sidedefCount++;
		AppendSidedef(s, p.sectorIdx, wtex, nullptr, wtex);
		AppendLinedef(s, p.vtl, p.vbl, sf, sb, true);
	}

	for (const auto& inset : insetExts)
	{
		const char* wtex = Rooms[inset.roomIdx].wallTex.GetChars();
		int roomSector = Rooms[inset.roomIdx].sectorIdx;

		int sf = sidedefCount++;
		AppendSidedef(s, roomSector, wtex, nullptr, wtex);
		int sb = sidedefCount++;
		AppendSidedef(s, inset.sectorIdx, wtex, nullptr, wtex);
		AppendLinedef(s, inset.vbl, inset.vbr, sf, sb, true);

		sf = sidedefCount++;
		AppendSidedef(s, roomSector, wtex, nullptr, wtex);
		sb = sidedefCount++;
		AppendSidedef(s, inset.sectorIdx, wtex, nullptr, wtex);
		AppendLinedef(s, inset.vbr, inset.vtr, sf, sb, true);

		sf = sidedefCount++;
		AppendSidedef(s, roomSector, wtex, nullptr, wtex);
		sb = sidedefCount++;
		AppendSidedef(s, inset.sectorIdx, wtex, nullptr, wtex);
		AppendLinedef(s, inset.vtr, inset.vtl, sf, sb, true);

		sf = sidedefCount++;
		AppendSidedef(s, roomSector, wtex, nullptr, wtex);
		sb = sidedefCount++;
		AppendSidedef(s, inset.sectorIdx, wtex, nullptr, wtex);
		AppendLinedef(s, inset.vtl, inset.vbl, sf, sb, true);
	}

	// --- Door tracking (locked + unlocked) ---
	struct DoorInfo
	{
		int sectorIdx;
		int vbl, vbr, vtr, vtl;
		int lockType;
		bool horizontal;
		int i, j; // primary cell
	};
	TArray<DoorInfo> doors;

	// Helper to get door texture based on lock and theme
	auto GetDoorTex = [&](int lock) -> const char*
	{
		if (lock == 1) return "DOORRED";
		if (lock == 2) return "DOORBLU";
		if (lock == 3) return "DOORYEL";
		if (Theme.Compare("hell") == 0) return "BIGDOOR1";
		return "DOOR1";
	};

	// First pass: locked connections
	for (int j = 0; j < H; j++)
	{
		for (int i = 0; i < W; i++)
		{
			if (!Grid[j][i].present) continue;

			for (int d : {DIR_N, DIR_W})
			{
				if (!Grid[j][i].conn[d]) continue;
				int ni = i + DX[d];
				int nj = j + DY[d];
				if (ni < 0 || ni >= W || nj < 0 || nj >= H) continue;
				if (!Grid[nj][ni].present) continue;

				int lock = 0;
				if (Grid[j][i].isLocked && !Grid[nj][ni].isLocked)
					lock = Grid[j][i].lockType;
				else if (Grid[nj][ni].isLocked && !Grid[j][i].isLocked)
					lock = Grid[nj][ni].lockType;
				else if (Grid[j][i].isLocked && Grid[nj][ni].isLocked)
					lock = std::max(Grid[j][i].lockType, Grid[nj][ni].lockType);

				if (lock > 0)
				{
					if (Grid[j][i].roomId >= 0 && Grid[j][i].roomId == Grid[nj][ni].roomId)
						continue; // skip locked doors inside the same room
					if (!IsValidRoomIndex(Grid[j][i].roomId) || !IsValidRoomIndex(Grid[nj][ni].roomId))
						continue;
					if (Grid[j][i].sectorIdx < 0 || Grid[nj][ni].sectorIdx < 0)
						continue;
					double doorFloorZ = std::max(Grid[j][i].floorZ, Grid[nj][ni].floorZ);
					double doorCeilZ = doorFloorZ + 64.0;
					if (Grid[j][i].ceilZ > doorCeilZ) doorCeilZ = Grid[j][i].ceilZ;
					if (Grid[nj][ni].ceilZ > doorCeilZ) doorCeilZ = Grid[nj][ni].ceilZ;
					int doorLight = (Grid[j][i].light + Grid[nj][ni].light) / 2;

					double x1, x2, y1, y2;
					bool horizontalDoor = (d == DIR_N);
					if (horizontalDoor)
					{
						x1 = (i - W / 2) * CELL_SIZE;
						x2 = (i + 1 - W / 2) * CELL_SIZE;
						double y = (j - H / 2.0) * CELL_SIZE;
						y1 = y - DOOR_HALF;
						y2 = y + DOOR_HALF;
					}
					else
					{
						double x = (i - W / 2) * CELL_SIZE;
						x1 = x - DOOR_HALF;
						x2 = x + DOOR_HALF;
						y1 = (j - H / 2) * CELL_SIZE;
						y2 = (j + 1 - H / 2) * CELL_SIZE;
					}

					int vbl = EmitVert(x1, y1);
					int vbr = EmitVert(x2, y1);
					int vtr = EmitVert(x2, y2);
					int vtl = EmitVert(x1, y2);

					int doorSector = sectorCount++;
					AppendSector(s, doorFloorZ, doorCeilZ,
						Grid[j][i].floorTex.GetChars(),
						Grid[j][i].ceilTex.GetChars(), doorLight);

					DoorInfo di;
					di.sectorIdx = doorSector;
					di.vbl = vbl; di.vbr = vbr; di.vtr = vtr; di.vtl = vtl;
					di.lockType = lock;
					di.horizontal = horizontalDoor;
					di.i = i; di.j = j;
					doors.Push(di);
				}
			}
		}
	}

	// Second pass: unlocked doors between different rooms
	for (int j = 0; j < H; j++)
	{
		for (int i = 0; i < W; i++)
		{
			if (!Grid[j][i].present) continue;

				for (int d : {DIR_N, DIR_W})
				{
					if (!Grid[j][i].conn[d]) continue;
					int ni = i + DX[d];
					int nj = j + DY[d];
					if (ni < 0 || ni >= W || nj < 0 || nj >= H) continue;
					if (!Grid[nj][ni].present) continue;
					if (Grid[j][i].roomId == Grid[nj][ni].roomId) continue; // same room
					if (!IsValidRoomIndex(Grid[j][i].roomId) || !IsValidRoomIndex(Grid[nj][ni].roomId))
						continue;
					if (Grid[j][i].sectorIdx < 0 || Grid[nj][ni].sectorIdx < 0)
						continue;

				// Skip if already a locked door on this edge
				bool alreadyDoor = false;
				for (const auto& dd : doors)
				{
					if (dd.i == i && dd.j == j && dd.horizontal == (d == DIR_N))
						{ alreadyDoor = true; break; }
				}
				if (alreadyDoor) continue;

				int roomAIndex = Grid[j][i].roomId;
				int roomBIndex = Grid[nj][ni].roomId;
				if (!IsValidRoomIndex(roomAIndex) || !IsValidRoomIndex(roomBIndex))
					continue;

				RoomInfo& roomA = Rooms[roomAIndex];
				RoomInfo& roomB = Rooms[roomBIndex];
				bool shouldDoor = false;
				if (roomA.hasDoor || roomB.hasDoor || roomA.isDeadEnd || roomB.isDeadEnd)
					shouldDoor = true;
				else if (roomA.hasKey || roomB.hasKey || roomA.isLocked || roomB.isLocked)
					shouldDoor = true;
				else if ((roomA.isHub || roomB.isHub) && (roomA.onMainPath != roomB.onMainPath))
					shouldDoor = true;
					else if (roomA.isArena || roomB.isArena)
						shouldDoor = ((RNG() % 10) < 6);
					else if ((RNG() % 10) < 3)
						shouldDoor = true;

					if (!shouldDoor) continue;

					double doorFloorZ = std::max(Grid[j][i].floorZ, Grid[nj][ni].floorZ);
					double doorCeilZ = doorFloorZ + 64.0;
					if (Grid[j][i].ceilZ > doorCeilZ) doorCeilZ = Grid[j][i].ceilZ;
					if (Grid[nj][ni].ceilZ > doorCeilZ) doorCeilZ = Grid[nj][ni].ceilZ;
					int doorLight = (Grid[j][i].light + Grid[nj][ni].light) / 2;

				double x1, x2, y1, y2;
				bool horizontalDoor = (d == DIR_N);
				if (horizontalDoor)
				{
					x1 = (i - W / 2) * CELL_SIZE;
					x2 = (i + 1 - W / 2) * CELL_SIZE;
					double y = (j - H / 2.0) * CELL_SIZE;
					y1 = y - DOOR_HALF;
					y2 = y + DOOR_HALF;
				}
				else
				{
					double x = (i - W / 2) * CELL_SIZE;
					x1 = x - DOOR_HALF;
					x2 = x + DOOR_HALF;
					y1 = (j - H / 2) * CELL_SIZE;
					y2 = (j + 1 - H / 2) * CELL_SIZE;
				}

				int vbl = EmitVert(x1, y1);
				int vbr = EmitVert(x2, y1);
				int vtr = EmitVert(x2, y2);
				int vtl = EmitVert(x1, y2);

				int doorSector = sectorCount++;
				AppendSector(s, doorFloorZ, doorCeilZ,
					Grid[j][i].floorTex.GetChars(),
					Grid[j][i].ceilTex.GetChars(), doorLight);

				DoorInfo di;
				di.sectorIdx = doorSector;
				di.vbl = vbl; di.vbr = vbr; di.vtr = vtr; di.vtl = vtl;
				di.lockType = 0; // unlocked
				di.horizontal = horizontalDoor;
				di.i = i; di.j = j;
				doors.Push(di);
			}
		}
	}

	// --- Sidedefs & Linedefs ---

	// Horizontal edges (between row j-1 and row j)
	for (int j = 0; j <= H; j++)
	{
		for (int i = 0; i < W; i++)
		{
			bool below = (j > 0) && Grid[j - 1][i].present;
			bool above = (j < H) && Grid[j][i].present;
			if (!below && !above) continue;

			if (below && above && Grid[j - 1][i].roomId == Grid[j][i].roomId)
				continue;

			const DoorInfo* door = nullptr;
			for (const auto& d : doors)
			{
				if (d.horizontal && d.i == i && d.j == j)
				{
					door = &d;
					break;
				}
			}

			if (door)
			{
				const int doorSpecial = (door->lockType > 0) ? 13 : 1;
				const bool lockedDoor = door->lockType > 0;

				const char* wtexBelow = Grid[j - 1][i].wallTex.GetChars();
				const char* wtexAbove = Grid[j][i].wallTex.GetChars();
				const char* doorTex = GetDoorTex(door->lockType);
				const int doorArg = lockedDoor ? door->lockType : 0;

				// Door bottom: vbl -> vbr, front = below cell, back = door sector
				int sf = sidedefCount++;
				AppendSidedef(s, Grid[j - 1][i].sectorIdx, wtexBelow, doorTex, wtexBelow);
				int sb = sidedefCount++;
				AppendSidedef(s, door->sectorIdx, wtexBelow, doorTex, wtexBelow);
				AppendLinedef(s, door->vbl, door->vbr, sf, sb, true, doorSpecial, doorArg, false, false, lockedDoor);

				// Door top: vtr -> vtl, front = above cell, back = door sector
				sf = sidedefCount++;
				AppendSidedef(s, Grid[j][i].sectorIdx, wtexAbove, doorTex, wtexAbove);
				sb = sidedefCount++;
				AppendSidedef(s, door->sectorIdx, wtexAbove, doorTex, wtexAbove);
				AppendLinedef(s, door->vtr, door->vtl, sf, sb, true, doorSpecial, doorArg, false, false, lockedDoor);
			}
			else if (below && above)
			{
				bool connected = Grid[j - 1][i].conn[DIR_S] || Grid[j][i].conn[DIR_N];
				const char* wtexBelow = Grid[j - 1][i].wallTex.GetChars();
				const char* wtexAbove = Grid[j][i].wallTex.GetChars();
				bool diffFloor = (Grid[j - 1][i].floorZ != Grid[j][i].floorZ);
				bool diffCeil = (Grid[j - 1][i].ceilZ != Grid[j][i].ceilZ);

				int v1 = VIndex(i, j);
				int v2 = VIndex(i + 1, j);

				if (connected)
				{
					int roomBelowIdx = Grid[j - 1][i].roomId;
					int roomAboveIdx = Grid[j][i].roomId;
					if (!IsValidRoomIndex(roomBelowIdx) || !IsValidRoomIndex(roomAboveIdx))
					{
						continue;
					}

					RoomInfo& roomBelow = Rooms[roomBelowIdx];
					RoomInfo& roomAbove = Rooms[roomAboveIdx];
					double centerX = ((i + 0.5) - W / 2.0) * CELL_SIZE;
					double y = (j - H / 2.0) * CELL_SIZE;
					double halfWidth = OpeningHalfWidth(roomBelow, roomAbove);
					double openLeft = centerX - halfWidth;
					double openRight = centerX + halfWidth;
					int vLeft = EmitVert(openLeft, y);
					int vRight = EmitVert(openRight, y);

					int sf = sidedefCount++;
					AppendSidedef(s, Grid[j - 1][i].sectorIdx, wtexBelow, wtexBelow, wtexBelow);
					int sb = sidedefCount++;
					AppendSidedef(s, Grid[j][i].sectorIdx, wtexAbove, wtexAbove, wtexAbove);
					AppendLinedef(s, vLeft, v1, sf, sb, true, 0, 0, diffCeil, diffFloor, true);

					sf = sidedefCount++;
					AppendSidedef(s, Grid[j - 1][i].sectorIdx, wtexBelow, nullptr, wtexBelow);
					sb = sidedefCount++;
					AppendSidedef(s, Grid[j][i].sectorIdx, wtexAbove, nullptr, wtexAbove);
					AppendLinedef(s, vRight, vLeft, sf, sb, true, 0, 0, diffCeil, diffFloor);

					sf = sidedefCount++;
					AppendSidedef(s, Grid[j - 1][i].sectorIdx, wtexBelow, wtexBelow, wtexBelow);
					sb = sidedefCount++;
					AppendSidedef(s, Grid[j][i].sectorIdx, wtexAbove, wtexAbove, wtexAbove);
					AppendLinedef(s, v2, vRight, sf, sb, true, 0, 0, diffCeil, diffFloor, true);
				}
				else
				{
					int sf = sidedefCount++;
					AppendSidedef(s, Grid[j - 1][i].sectorIdx, wtexBelow, wtexBelow, wtexBelow);
					int sb = sidedefCount++;
					AppendSidedef(s, Grid[j][i].sectorIdx, wtexAbove, wtexAbove, wtexAbove);
					AppendLinedef(s, v2, v1, sf, sb, true, 0, 0, diffCeil, diffFloor, true);
				}
			}
			else if (below)
			{
				int v1 = VIndex(i, j);
				int v2 = VIndex(i + 1, j);
				int sf = sidedefCount++;
				const char* wtex = Grid[j - 1][i].wallTex.GetChars();
				AppendSidedef(s, Grid[j - 1][i].sectorIdx, nullptr, wtex, nullptr);
				AppendLinedef(s, v2, v1, sf, -1, false);
			}
			else
			{
				int v1 = VIndex(i, j);
				int v2 = VIndex(i + 1, j);
				int sf = sidedefCount++;
				const char* wtex = Grid[j][i].wallTex.GetChars();
				AppendSidedef(s, Grid[j][i].sectorIdx, nullptr, wtex, nullptr);
				AppendLinedef(s, v1, v2, sf, -1, false);
			}
		}
	}

	// Vertical edges (between col i-1 and col i)
	for (int i = 0; i <= W; i++)
	{
		for (int j = 0; j < H; j++)
		{
			const DoorInfo* verticalDoor = nullptr;
			for (const auto& d : doors)
			{
				if (!d.horizontal && d.i == i && d.j == j)
				{
					verticalDoor = &d;
					break;
				}
			}

			if (verticalDoor)
			{
				if (i <= 0 || i >= W)
				{
					continue;
				}
				const int doorSpecial = (verticalDoor->lockType > 0) ? 13 : 1;
				const bool lockedDoor = verticalDoor->lockType > 0;
				const int doorArg = lockedDoor ? verticalDoor->lockType : 0;

				const char* wtexLeft = Grid[j][i - 1].wallTex.GetChars();
				const char* wtexRight = Grid[j][i].wallTex.GetChars();
				const char* doorTex = GetDoorTex(verticalDoor->lockType);

				int sf = sidedefCount++;
				AppendSidedef(s, Grid[j][i - 1].sectorIdx, wtexLeft, doorTex, wtexLeft);
				int sb = sidedefCount++;
				AppendSidedef(s, verticalDoor->sectorIdx, wtexLeft, doorTex, wtexLeft);
				AppendLinedef(s, verticalDoor->vtl, verticalDoor->vbl, sf, sb, true, doorSpecial, doorArg, false, false, lockedDoor);

				sf = sidedefCount++;
				AppendSidedef(s, Grid[j][i].sectorIdx, wtexRight, doorTex, wtexRight);
				sb = sidedefCount++;
				AppendSidedef(s, verticalDoor->sectorIdx, wtexRight, doorTex, wtexRight);
				AppendLinedef(s, verticalDoor->vbr, verticalDoor->vtr, sf, sb, true, doorSpecial, doorArg, false, false, lockedDoor);
				continue;
			}

			double y0 = (j - H / 2) * CELL_SIZE;
			double y1 = (j + 1 - H / 2) * CELL_SIZE;

			const DoorInfo* doorBottom = nullptr;
			const DoorInfo* doorTop = nullptr;
			bool bottomIsLeftFrame = false;
			bool topIsLeftFrame = false;

			for (const auto& d : doors)
			{
				if (!d.horizontal) continue;
				if (d.i == i)
				{
					if (d.j == j + 1) { doorTop = &d; topIsLeftFrame = true; }
					if (d.j == j) { doorBottom = &d; bottomIsLeftFrame = true; }
				}
				if (d.i + 1 == i)
				{
					if (d.j == j + 1) { doorTop = &d; topIsLeftFrame = false; }
					if (d.j == j) { doorBottom = &d; bottomIsLeftFrame = false; }
				}
			}

			TArray<std::pair<double, double>> roomSegments;
			TArray<std::pair<double, double>> doorFrameSegments;
			TArray<const DoorInfo*> doorFrameDoors;
			TArray<bool> doorFrameIsLeft;

			if (doorBottom && doorTop)
			{
				doorFrameSegments.Push(std::make_pair(y0, y0 + DOOR_HALF));
				doorFrameDoors.Push(doorBottom);
				doorFrameIsLeft.Push(bottomIsLeftFrame);
				roomSegments.Push(std::make_pair(y0 + DOOR_HALF, y1 - DOOR_HALF));
				doorFrameSegments.Push(std::make_pair(y1 - DOOR_HALF, y1));
				doorFrameDoors.Push(doorTop);
				doorFrameIsLeft.Push(topIsLeftFrame);
			}
			else if (doorBottom)
			{
				doorFrameSegments.Push(std::make_pair(y0, y0 + DOOR_HALF));
				doorFrameDoors.Push(doorBottom);
				doorFrameIsLeft.Push(bottomIsLeftFrame);
				roomSegments.Push(std::make_pair(y0 + DOOR_HALF, y1));
			}
			else if (doorTop)
			{
				roomSegments.Push(std::make_pair(y0, y1 - DOOR_HALF));
				doorFrameSegments.Push(std::make_pair(y1 - DOOR_HALF, y1));
				doorFrameDoors.Push(doorTop);
				doorFrameIsLeft.Push(topIsLeftFrame);
			}
			else
			{
				roomSegments.Push(std::make_pair(y0, y1));
			}

			// Emit room segments
			for (const auto& seg : roomSegments)
			{
				bool left = (i > 0) && Grid[j][i - 1].present;
				bool right = (i < W) && Grid[j][i].present;
				if (!left && !right) continue;

				if (left && right && Grid[j][i - 1].roomId == Grid[j][i].roomId)
					continue;

				int vs, ve;
				if (seg.first == y0 && seg.second == y1)
				{
					vs = VIndex(i, j);
					ve = VIndex(i, j + 1);
				}
				else if (seg.first == y0)
				{
					vs = VIndex(i, j);
					ve = EmitVert((i - W / 2) * CELL_SIZE, seg.second);
				}
				else if (seg.second == y1)
				{
					vs = EmitVert((i - W / 2) * CELL_SIZE, seg.first);
					ve = VIndex(i, j + 1);
				}
				else
				{
					vs = EmitVert((i - W / 2) * CELL_SIZE, seg.first);
					ve = EmitVert((i - W / 2) * CELL_SIZE, seg.second);
				}

				if (left && right)
				{
					bool connected = Grid[j][i - 1].conn[DIR_E] || Grid[j][i].conn[DIR_W];
					const char* wtexLeft = Grid[j][i - 1].wallTex.GetChars();
					const char* wtexRight = Grid[j][i].wallTex.GetChars();
					bool diffFloor = (Grid[j][i - 1].floorZ != Grid[j][i].floorZ);
					bool diffCeil = (Grid[j][i - 1].ceilZ != Grid[j][i].ceilZ);

					if (connected)
					{
						int roomLeftIdx = Grid[j][i - 1].roomId;
						int roomRightIdx = Grid[j][i].roomId;
						if (!IsValidRoomIndex(roomLeftIdx) || !IsValidRoomIndex(roomRightIdx))
						{
							continue;
						}

						RoomInfo& roomLeft = Rooms[roomLeftIdx];
						RoomInfo& roomRight = Rooms[roomRightIdx];
						double x = (i - W / 2.0) * CELL_SIZE;
						double centerY = ((j + 0.5) - H / 2.0) * CELL_SIZE;
						double halfWidth = OpeningHalfWidth(roomLeft, roomRight);
						double openBottom = centerY - halfWidth;
						double openTop = centerY + halfWidth;
						int vBottom = EmitVert(x, openBottom);
						int vTop = EmitVert(x, openTop);

						int sf = sidedefCount++;
						AppendSidedef(s, Grid[j][i - 1].sectorIdx, wtexLeft, wtexLeft, wtexLeft);
						int sb = sidedefCount++;
						AppendSidedef(s, Grid[j][i].sectorIdx, wtexRight, wtexRight, wtexRight);
						AppendLinedef(s, vs, vBottom, sf, sb, true, 0, 0, diffCeil, diffFloor, true);

						sf = sidedefCount++;
						AppendSidedef(s, Grid[j][i - 1].sectorIdx, wtexLeft, nullptr, wtexLeft);
						sb = sidedefCount++;
						AppendSidedef(s, Grid[j][i].sectorIdx, wtexRight, nullptr, wtexRight);
						AppendLinedef(s, vBottom, vTop, sf, sb, true, 0, 0, diffCeil, diffFloor);

						sf = sidedefCount++;
						AppendSidedef(s, Grid[j][i - 1].sectorIdx, wtexLeft, wtexLeft, wtexLeft);
						sb = sidedefCount++;
						AppendSidedef(s, Grid[j][i].sectorIdx, wtexRight, wtexRight, wtexRight);
						AppendLinedef(s, vTop, ve, sf, sb, true, 0, 0, diffCeil, diffFloor, true);
					}
					else
					{
						int sf = sidedefCount++;
						AppendSidedef(s, Grid[j][i - 1].sectorIdx, wtexLeft, wtexLeft, wtexLeft);
						int sb = sidedefCount++;
						AppendSidedef(s, Grid[j][i].sectorIdx, wtexRight, wtexRight, wtexRight);
						AppendLinedef(s, vs, ve, sf, sb, true, 0, 0, diffCeil, diffFloor, true);
					}
				}
				else if (left)
				{
					int sf = sidedefCount++;
					const char* wtex = Grid[j][i - 1].wallTex.GetChars();
					AppendSidedef(s, Grid[j][i - 1].sectorIdx, nullptr, wtex, nullptr);
					AppendLinedef(s, vs, ve, sf, -1, false);
				}
				else
				{
					int sf = sidedefCount++;
					const char* wtex = Grid[j][i].wallTex.GetChars();
					AppendSidedef(s, Grid[j][i].sectorIdx, nullptr, wtex, nullptr);
					AppendLinedef(s, ve, vs, sf, -1, false);
				}
			}

			// Emit door frame segments
			for (unsigned int di = 0; di < doorFrameSegments.Size(); di++)
			{
				const auto& seg = doorFrameSegments[di];
				const DoorInfo* d = doorFrameDoors[di];
				bool isLeft = doorFrameIsLeft[di];

				int vs = EmitVert((i - W / 2) * CELL_SIZE, seg.first);
				int ve = EmitVert((i - W / 2) * CELL_SIZE, seg.second);

				int sectorFront, sectorBack;
				const char* wtexFront;
				const char* wtexBack;

				if (isLeft)
				{
					sectorFront = d->sectorIdx;
					wtexFront = Grid[d->j][d->i].wallTex.GetChars();
					if (i > 0 && Grid[j][i - 1].present)
					{
						sectorBack = Grid[j][i - 1].sectorIdx;
						wtexBack = Grid[j][i - 1].wallTex.GetChars();
					}
					else
					{
						sectorBack = -1;
						wtexBack = wtexFront;
					}
				}
				else
				{
					sectorFront = d->sectorIdx;
					wtexFront = Grid[d->j][d->i].wallTex.GetChars();
					if (i < W && Grid[j][i].present)
					{
						sectorBack = Grid[j][i].sectorIdx;
						wtexBack = Grid[j][i].wallTex.GetChars();
					}
					else
					{
						sectorBack = -1;
						wtexBack = wtexFront;
					}
				}

					if (isLeft)
					{
						int sf = sidedefCount++;
						AppendSidedef(s, sectorFront, wtexFront, "DOORTRAK", wtexFront);
						if (sectorBack >= 0)
						{
							int sb = sidedefCount++;
							AppendSidedef(s, sectorBack, wtexBack, "DOORTRAK", wtexBack);
							AppendLinedef(s, vs, ve, sf, sb, true, 0, 0, false, false, true);
						}
						else
						{
							AppendLinedef(s, vs, ve, sf, -1, false, 0, 0, false, false, true);
						}
					}
					else
					{
						int sf = sidedefCount++;
						AppendSidedef(s, sectorFront, wtexFront, "DOORTRAK", wtexFront);
						if (sectorBack >= 0)
						{
							int sb = sidedefCount++;
							AppendSidedef(s, sectorBack, wtexBack, "DOORTRAK", wtexBack);
							AppendLinedef(s, ve, vs, sf, sb, true, 0, 0, false, false, true);
						}
						else
						{
							AppendLinedef(s, ve, vs, sf, -1, false, 0, 0, false, false, true);
						}
					}
			}
		}
	}

	// Vertical door top/bottom tracks.
	for (const auto& d : doors)
	{
		if (d.horizontal) continue;

		const char* wtex = Grid[d.j][d.i].wallTex.GetChars();
		int topBackSector = -1;
		const char* topBackTex = wtex;
		if (d.j > 0)
		{
			if (Grid[d.j - 1][d.i].present)
			{
				topBackSector = Grid[d.j - 1][d.i].sectorIdx;
				topBackTex = Grid[d.j - 1][d.i].wallTex.GetChars();
			}
			else if (d.i + 1 < W && Grid[d.j - 1][d.i + 1].present)
			{
				topBackSector = Grid[d.j - 1][d.i + 1].sectorIdx;
				topBackTex = Grid[d.j - 1][d.i + 1].wallTex.GetChars();
			}
		}

		int sf = sidedefCount++;
		AppendSidedef(s, d.sectorIdx, wtex, "DOORTRAK", wtex);
		if (topBackSector >= 0)
		{
			int sb = sidedefCount++;
			AppendSidedef(s, topBackSector, topBackTex, "DOORTRAK", topBackTex);
			AppendLinedef(s, d.vbr, d.vbl, sf, sb, true, 0, 0, false, false, true);
		}
		else
		{
			AppendLinedef(s, d.vbr, d.vbl, sf, -1, false, 0, 0, false, false, true);
		}

		int bottomBackSector = -1;
		const char* bottomBackTex = wtex;
		if (d.j + 1 < H)
		{
			if (Grid[d.j + 1][d.i].present)
			{
				bottomBackSector = Grid[d.j + 1][d.i].sectorIdx;
				bottomBackTex = Grid[d.j + 1][d.i].wallTex.GetChars();
			}
			else if (d.i + 1 < W && Grid[d.j + 1][d.i + 1].present)
			{
				bottomBackSector = Grid[d.j + 1][d.i + 1].sectorIdx;
				bottomBackTex = Grid[d.j + 1][d.i + 1].wallTex.GetChars();
			}
		}

		sf = sidedefCount++;
		AppendSidedef(s, d.sectorIdx, wtex, "DOORTRAK", wtex);
		if (bottomBackSector >= 0)
		{
			int sb = sidedefCount++;
			AppendSidedef(s, bottomBackSector, bottomBackTex, "DOORTRAK", bottomBackTex);
			AppendLinedef(s, d.vtl, d.vtr, sf, sb, true, 0, 0, false, false, true);
		}
		else
		{
			AppendLinedef(s, d.vtl, d.vtr, sf, -1, false, 0, 0, false, false, true);
		}
	}

	// --- Things ---
	int exitSector = -1;
	double exitX = 0, exitY = 0;

	auto ChooseMonsterForRoom = [&](const RoomInfo& room) -> int
	{
		if (room.monsterTier <= 2)
			return EnemiesEasy[RNG() % countof(EnemiesEasy)];
		if (room.monsterTier <= 4)
			return EnemiesMed[RNG() % countof(EnemiesMed)];
		return EnemiesHard[RNG() % countof(EnemiesHard)];
	};

	for (unsigned int ri = 0; ri < Rooms.Size(); ri++)
	{
		RoomInfo& room = Rooms[ri];
		if (room.id < 0) continue;

		TArray<std::pair<int, int>> roomCells;
		double cx = 0.0;
		double cy = 0.0;
		for (int j = room.minJ; j <= room.maxJ; j++)
		{
			for (int i = room.minI; i <= room.maxI; i++)
			{
				if (Grid[j][i].roomId != (int)ri) continue;
				roomCells.Push(std::make_pair(i, j));
				cx += ((i + 0.5) - W / 2.0) * CELL_SIZE;
				cy += ((j + 0.5) - H / 2.0) * CELL_SIZE;
			}
		}
		if (roomCells.Size() == 0) continue;

		cx /= roomCells.Size();
		cy /= roomCells.Size();

		auto SlotXY = [&](int slot, double& outX, double& outY)
		{
			unsigned int idx = (unsigned int)((slot * 5 + room.monsterTier + room.branchDepth + room.progressionRank) % (int)roomCells.Size());
			int ci = roomCells[idx].first;
			int cj = roomCells[idx].second;
			static const double jitX[] = { 0.0, -28.0, 28.0, -36.0, 36.0, 0.0 };
			static const double jitY[] = { 0.0, -20.0, 20.0, 30.0, -30.0, 42.0 };
			outX = ((ci + 0.5) - W / 2.0) * CELL_SIZE + jitX[slot % countof(jitX)];
			outY = ((cj + 0.5) - H / 2.0) * CELL_SIZE + jitY[slot % countof(jitY)];
		};

		if (room.hasPlayerStart)
		{
			AppendThing(s, cx, cy, 1, 90);
		}
		if (room.hasExit)
		{
			exitSector = room.sectorIdx;
			exitX = cx;
			exitY = cy;
		}
		if (room.hasKey && room.keyType >= 1 && room.keyType <= 3)
		{
			double tx = cx;
			double ty = cy;
			int keyEdNum = 13;
			switch (room.keyType)
			{
			case 1: keyEdNum = 13; break;
			case 2: keyEdNum = 5; break;
			case 3: keyEdNum = 6; break;
			default: keyEdNum = 13; break;
			}
			AppendThing(s, tx, ty, keyEdNum);
		}
		if (room.hasWeapon)
		{
			double wx, wy, ax, ay;
			if (room.hasExit || room.hasBoss || room.hasKey || room.isLocked || !room.onMainPath)
			{
				wx = cx - 56.0;
				wy = cy;
				ax = cx + 56.0;
				ay = cy;
			}
			else
			{
				SlotXY(1, wx, wy);
				SlotXY(2, ax, ay);
			}
			AppendThing(s, wx, wy, room.weaponType);
			int ammoType = 2007;
			if (room.weaponType == 2001) ammoType = 2008;
			else if (room.weaponType == 2002) ammoType = 2007;
			else if (room.weaponType == 2003) ammoType = 2010;
			else if (room.weaponType == 82) ammoType = 2008;
			else if (room.weaponType == 2004) ammoType = 2047;
			else if (room.weaponType == 2006) ammoType = 2047;
			AppendThing(s, ax, ay, ammoType);
		}
		if (room.hasAmmo)
		{
			double tx, ty;
			if (room.hasWeapon || room.hasKey || room.hasExit || room.hasBoss)
			{
				tx = cx;
				ty = cy + 72.0;
			}
			else
			{
				SlotXY(3, tx, ty);
			}
			AppendThing(s, tx, ty, room.ammoType);
		}
		if (room.hasHealth)
		{
			double tx, ty;
			if (room.hasWeapon || room.hasKey || room.isLocked)
			{
				tx = cx - 72.0;
				ty = cy + 40.0;
			}
			else
			{
				SlotXY(4, tx, ty);
			}
			AppendThing(s, tx, ty, room.healthType);
		}
		if (room.hasArmor)
		{
			double tx, ty;
			if (room.hasWeapon || room.hasKey || room.isLocked)
			{
				tx = cx + 72.0;
				ty = cy + 40.0;
			}
			else
			{
				SlotXY(5, tx, ty);
			}
			AppendThing(s, tx, ty, room.armorType);
		}

		if (!room.hasPlayerStart && !room.hasExit && !room.hasKey && !room.hasWeapon && !room.hasAmmo && (RNG() % 4) == 0)
		{
			static const int items[] = { 2007, 2008, 2011, 2012, 2014, 2015, 2018 };
			int item = items[RNG() % countof(items)];
			double tx, ty;
			SlotXY(6, tx, ty);
			AppendThing(s, tx, ty, item);
		}

		if (room.hasBoss && !room.hasPlayerStart)
		{
			int bossEdnum;
			if (Difficulty <= 2)
				bossEdnum = BossesEasy[RNG() % countof(BossesEasy)];
			else if (Difficulty <= 4)
				bossEdnum = BossesMed[RNG() % countof(BossesMed)];
			else
				bossEdnum = BossesHard[RNG() % countof(BossesHard)];
			AppendThing(s, cx, cy, bossEdnum);
		}

		// Enemies: place 1 near center as a guard, rest spread randomly
		for (int e = 0; e < room.enemyCount; e++)
		{
			int ednum = ChooseMonsterForRoom(room);
			double ex, ey;
			if (room.hasBoss)
			{
				static const double ringX[] = { -112.0, 112.0, 0.0, 0.0, -144.0, 144.0 };
				static const double ringY[] = { 0.0, 0.0, -112.0, 112.0, 96.0, -96.0 };
				ex = cx + ringX[e % countof(ringX)] + ((RNG() % 33) - 16);
				ey = cy + ringY[e % countof(ringY)] + ((RNG() % 33) - 16);
			}
			else if (room.hasKey || room.isHub)
			{
				static const double flankX[] = { -96.0, 96.0, -144.0, 144.0, 0.0, 0.0 };
				static const double flankY[] = { 0.0, 0.0, 64.0, -64.0, -112.0, 112.0 };
				ex = cx + flankX[e % countof(flankX)] + ((RNG() % 41) - 20);
				ey = cy + flankY[e % countof(flankY)] + ((RNG() % 41) - 20);
			}
			else if (room.hasWeapon && !room.onMainPath)
			{
				static const double rewardX[] = { -104.0, 104.0, 0.0, -64.0, 64.0, 0.0 };
				static const double rewardY[] = { -56.0, -56.0, 112.0, 72.0, 72.0, -128.0 };
				ex = cx + rewardX[e % countof(rewardX)] + ((RNG() % 37) - 18);
				ey = cy + rewardY[e % countof(rewardY)] + ((RNG() % 37) - 18);
			}
			else if (room.branchDepth >= 2)
			{
				static const double shrineX[] = { -88.0, 88.0, -32.0, 32.0, 0.0, 0.0 };
				static const double shrineY[] = { -64.0, -64.0, 56.0, 56.0, 120.0, -132.0 };
				ex = cx + shrineX[e % countof(shrineX)] + ((RNG() % 35) - 17);
				ey = cy + shrineY[e % countof(shrineY)] + ((RNG() % 35) - 17);
			}
			else if (room.isLocked)
			{
				static const double lockX[] = { -80.0, 80.0, -32.0, 32.0, 0.0, 0.0 };
				static const double lockY[] = { -96.0, -96.0, 48.0, 48.0, 112.0, -144.0 };
				ex = cx + lockX[e % countof(lockX)] + ((RNG() % 37) - 18);
				ey = cy + lockY[e % countof(lockY)] + ((RNG() % 37) - 18);
			}
			else if (room.onMainPath)
			{
				static const double marchX[] = { -72.0, 72.0, -24.0, 24.0, 0.0, 0.0, -112.0, 112.0 };
				static const double marchY[] = { -96.0, -96.0, 16.0, 16.0, 104.0, -144.0, 72.0, 72.0 };
				ex = cx + marchX[e % countof(marchX)] + ((RNG() % 33) - 16);
				ey = cy + marchY[e % countof(marchY)] + ((RNG() % 33) - 16);
			}
			else if (room.isArena)
			{
				static const double arenaX[] = { -120.0, 120.0, -120.0, 120.0, 0.0, 0.0 };
				static const double arenaY[] = { -120.0, -120.0, 120.0, 120.0, -160.0, 160.0 };
				ex = cx + arenaX[e % countof(arenaX)] + ((RNG() % 49) - 24);
				ey = cy + arenaY[e % countof(arenaY)] + ((RNG() % 49) - 24);
			}
			else
			{
				unsigned int idx = (unsigned int)((e * 7 + room.monsterTier + room.progressionRank) % (int)roomCells.Size());
				int ei = roomCells[idx].first;
				int ej = roomCells[idx].second;
				ex = ((ei + 0.5) - W / 2.0) * CELL_SIZE - 36.0 + (RNG() % 72);
				ey = ((ej + 0.5) - H / 2.0) * CELL_SIZE - 36.0 + (RNG() % 72);
			}
			AppendThing(s, ex, ey, ednum);
		}
	}

	// --- Exit trigger ---
	if (exitSector >= 0)
	{
		int vExit1 = nextExtraVert;
		int vExit2 = nextExtraVert + 1;
		nextExtraVert += 2;
		AppendVertex(s, exitX - 8, exitY);
		AppendVertex(s, exitX + 8, exitY);

		int sf = sidedefCount++;
		AppendSidedef(s, exitSector, nullptr, nullptr, nullptr);
		int sb = sidedefCount++;
		AppendSidedef(s, exitSector, nullptr, nullptr, nullptr);

		s.AppendFormat(
			"linedef\n"
			"{\n"
			"\tv1 = %d;\n"
			"\tv2 = %d;\n"
			"\tsidefront = %d;\n"
			"\tsideback = %d;\n"
			"\ttwosided = true;\n"
			"\tspecial = 243;\n"
			"}\n\n",
			vExit1, vExit2, sf, sb);
	}

	return true;
}
