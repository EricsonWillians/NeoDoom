/*
** procgen_core.cpp
**
** Mission-graph-first procedural map generation. The coarse grid is only an
** embedding surface: progression, optional branches, locks, loops, and
** landmarks are planned before rooms and UDMF geometry are built.
**
**---------------------------------------------------------------------------
*/

#include "procgen_internal.h"

using namespace ProcGen;

FProceduralMapGenerator FProceduralMapGenerator::Instance;

FProceduralMapGenerator& FProceduralMapGenerator::GetInstance()
{
	return Instance;
}

FProceduralMapGenerator::FProceduralMapGenerator()
	: Seed(0), Difficulty(3), Size(DefaultMapSize),
	  Layout(DefaultStyleSetting), Verticality(DefaultStyleSetting),
	  Detail(DefaultStyleSetting), Outdoors(DefaultStyleSetting)
{
	Theme = "techbase";
}

void FProceduralMapGenerator::SetSeed(int seed)
{
	Seed = seed;
	RNG.Init((uint32_t)seed);
}

void FProceduralMapGenerator::SetTheme(const char* theme)
{
	Theme = theme;
	Theme.ToLower();
	if (Theme.Compare("techbase") != 0 && Theme.Compare("hell") != 0 &&
		Theme.Compare("industrial") != 0 && Theme.Compare("gothic") != 0 &&
		Theme.Compare("corrupted") != 0)
		Theme = "techbase";
}

void FProceduralMapGenerator::SetDifficulty(int difficulty)
{
	Difficulty = clamp(difficulty, 1, 5);
}

void FProceduralMapGenerator::SetSize(int size)
{
	Size = clamp(size, MinMapSize, MaxMapSize);
}

void FProceduralMapGenerator::SetLayout(int layout)
{
	Layout = clamp(layout, 0, 2);
}

void FProceduralMapGenerator::SetVerticality(int verticality)
{
	Verticality = clamp(verticality, 0, 2);
}

void FProceduralMapGenerator::SetDetail(int detail)
{
	Detail = clamp(detail, 0, 2);
}

void FProceduralMapGenerator::SetOutdoors(int outdoors)
{
	Outdoors = clamp(outdoors, 0, 2);
}

bool FProceduralMapGenerator::Generate()
{
	LastError = "";
	UDMFBuffer = "";
	Grid.Clear();
	Rooms.Clear();
	const ThemeStyle themeStyle = GetThemeStyle(Theme);
	auto ScaleSetting = [](int value, int setting, int lowPercent, int highPercent) -> int
	{
		const int percent = setting <= 0 ? lowPercent : (setting >= 2 ? highPercent : 100);
		return std::max(1, (value * percent + 50) / 100);
	};

	// A rectangular canvas better matches the broad, directional footprints of
	// classic Doom maps. Above size 40, transfer each additional column of width
	// growth into height. This keeps the absurd settings at least as capacious
	// without forcing their start and exit against UDMF's horizontal limit.
	const int extremeReflow = std::max(0, Size - 40);
	const int W = 8 + Size * 2 - extremeReflow;
	const int H = 7 + Size + extremeReflow;
	// The engine accepts coordinates through +/-262144 (MAX_MAP_COORD in
	// doomdef.h), but extreme maps should not make starts, blockmaps, or node
	// partitions live against that boundary. This band retains a generous
	// safety margin of a full coordinate decade while allowing maps with a
	// footprint more than five times wider than the old +/-24500 band.
	const double MaxCoordinate = 131072.0;
	const double extentX = (W * 0.5 - 1.5) * CELL_SIZE + 192.0;
	const double extentY = (H * 0.5 - 1.5) * CELL_SIZE + 192.0;
	if (extentX > MaxCoordinate || extentY > MaxCoordinate)
	{
		LastError = "Requested map size exceeds the procedural coordinate safety range";
		return false;
	}

	Grid.Resize(H);
	for (int y = 0; y < H; y++)
	{
		Grid[y].Resize(W);
		for (int x = 0; x < W; x++)
			Grid[y][x] = ProcGenCell();
	}

	auto InBounds = [&](int x, int y) -> bool
	{
		return x >= 1 && x < W - 1 && y >= 1 && y < H - 1;
	};

	auto ConnectCells = [&](int ax, int ay, int bx, int by)
	{
		for (int d = 0; d < 4; d++)
		{
			if (ax + DX[d] == bx && ay + DY[d] == by)
			{
				Grid[ay][ax].conn[d] = true;
				Grid[by][bx].conn[OPP[d]] = true;
				return;
			}
		}
	};

	auto DirectionBetween = [&](int ax, int ay, int bx, int by) -> int
	{
		for (int d = 0; d < 4; d++)
			if (ax + DX[d] == bx && ay + DY[d] == by)
				return d;
		return -1;
	};

	// Build a private randomized spanning tree. It supplies natural bends and
	// detours, but only the selected route and branches become map geometry.
	TArray<TArray<bool>> visited;
	TArray<TArray<int>> depth;
	TArray<TArray<std::pair<int, int>>> parent;
	visited.Resize(H);
	depth.Resize(H);
	parent.Resize(H);
	for (int y = 0; y < H; y++)
	{
		visited[y].Resize(W);
		depth[y].Resize(W);
		parent[y].Resize(W);
		for (int x = 0; x < W; x++)
		{
			visited[y][x] = false;
			depth[y][x] = -1;
			parent[y][x] = std::make_pair(-1, -1);
		}
	}

	const int sx = 1;
	const int sy = 1 + (RNG() % (H - 2));
	TArray<std::pair<int, int>> stack;
	stack.Push(std::make_pair(sx, sy));
	visited[sy][sx] = true;
	depth[sy][sx] = 0;

	while (stack.Size() > 0)
	{
		const int cx = stack.Last().first;
		const int cy = stack.Last().second;
		int bestDir = -1;
		int bestScore = -100000;

		for (int d = 0; d < 4; d++)
		{
			const int nx = cx + DX[d];
			const int ny = cy + DY[d];
			if (!InBounds(nx, ny) || visited[ny][nx]) continue;

			int score = (int)(RNG() % 100);
			const int eastBias = Layout == 0 ? 34 : (Layout == 2 ? 8 : 18);
			if (d == DIR_E) score += eastBias;
			if (d == DIR_W) score -= Layout == 0 ? 15 : (Layout == 2 ? 3 : 8);
			if (Layout == 2 && (d == DIR_N || d == DIR_S)) score += 7;
			if (themeStyle == ThemeIndustrial && d == DIR_E) score += 5;
			if ((themeStyle == ThemeHell || themeStyle == ThemeGothic) &&
				(d == DIR_N || d == DIR_S)) score += 4;
			if (ny == 1 || ny == H - 2) score -= 5;
			if (score > bestScore)
			{
				bestScore = score;
				bestDir = d;
			}
		}

		if (bestDir < 0)
		{
			stack.Pop();
			continue;
		}

		const int nx = cx + DX[bestDir];
		const int ny = cy + DY[bestDir];
		visited[ny][nx] = true;
		depth[ny][nx] = depth[cy][cx] + 1;
		parent[ny][nx] = std::make_pair(cx, cy);
		stack.Push(std::make_pair(nx, ny));
	}

	const int desiredRoute = ScaleSetting(9 + Size * 4, Layout, 78, 122);
	int ex = sx;
	int ey = sy;
	int bestExitScore = -1000000;
	for (int y = 1; y < H - 1; y++)
	{
		for (int x = 1; x < W - 1; x++)
		{
			if (!visited[y][x] || depth[y][x] < 7) continue;
			int score = x * 28 - abs(depth[y][x] - desiredRoute) * 8;
			if (x >= W - 3) score += 100;
			if (abs(y - sy) >= H / 3) score += 18;
			if (score > bestExitScore)
			{
				bestExitScore = score;
				ex = x;
				ey = y;
			}
		}
	}

	if (ex == sx && ey == sy)
	{
		LastError = "Could not embed a sufficiently long main route";
		return false;
	}

	TArray<std::pair<int, int>> mainPath;
	for (int x = ex, y = ey, guard = W * H + 4; guard-- > 0;)
	{
		mainPath.Push(std::make_pair(x, y));
		if (x == sx && y == sy) break;
		auto p = parent[y][x];
		if (p.first < 0)
		{
			LastError = "Main route parent chain is incomplete";
			return false;
		}
		x = p.first;
		y = p.second;
	}
	for (int a = 0, b = (int)mainPath.Size() - 1; a < b; a++, b--)
	{
		auto tmp = mainPath[a];
		mainPath[a] = mainPath[b];
		mainPath[b] = tmp;
	}

	if (mainPath.Size() < 8)
	{
		LastError = "Generated main route is too short";
		return false;
	}

	TArray<TArray<bool>> keep;
	keep.Resize(H);
	for (int y = 0; y < H; y++)
	{
		keep[y].Resize(W);
		for (int x = 0; x < W; x++) keep[y][x] = false;
	}

	for (int rank = 0; rank < (int)mainPath.Size(); rank++)
	{
		const int x = mainPath[rank].first;
		const int y = mainPath[rank].second;
		keep[y][x] = true;
		Grid[y][x].present = true;
		Grid[y][x].pathRank = rank;
		Grid[y][x].onMainPath = true;
		if (rank > 0)
			ConnectCells(mainPath[rank - 1].first, mainPath[rank - 1].second, x, y);
	}

	// Grow a deliberate optional limb. Branches avoid touching the main path
	// away from their anchor, preventing accidental shortcuts around locks.
	auto GrowBranch = [&](int anchorRank, int wanted, TArray<std::pair<int, int>>& result) -> bool
	{
		result.Clear();
		if (anchorRank <= 0 || anchorRank >= (int)mainPath.Size() - 1) return false;

		int cx = mainPath[anchorRank].first;
		int cy = mainPath[anchorRank].second;
		for (int step = 1; step <= wanted; step++)
		{
			int bestX = -1;
			int bestY = -1;
			int bestScore = -100000;
			for (int d = 0; d < 4; d++)
			{
				const int nx = cx + DX[d];
				const int ny = cy + DY[d];
				if (!InBounds(nx, ny) || keep[ny][nx]) continue;

				int touchesMain = 0;
				int touchesKept = 0;
				int openness = 0;
				for (int od = 0; od < 4; od++)
				{
					const int ox = nx + DX[od];
					const int oy = ny + DY[od];
					if (!InBounds(ox, oy)) continue;
					if (keep[oy][ox])
					{
						touchesKept++;
						if (Grid[oy][ox].onMainPath && !(ox == cx && oy == cy)) touchesMain++;
					}
					else openness++;
				}

				int score = (int)(RNG() % 31) + openness * 7 - touchesKept * 6 - touchesMain * 80;
				if (nx == 1 || nx == W - 2 || ny == 1 || ny == H - 2) score += 5;
				if (score > bestScore)
				{
					bestScore = score;
					bestX = nx;
					bestY = ny;
				}
			}

			if (bestX < 0) break;
			keep[bestY][bestX] = true;
			Grid[bestY][bestX].present = true;
			Grid[bestY][bestX].pathRank = anchorRank;
			Grid[bestY][bestX].branchDepth = step;
			ConnectCells(cx, cy, bestX, bestY);
			result.Push(std::make_pair(bestX, bestY));
			cx = bestX;
			cy = bestY;
		}
		return result.Size() > 0;
	};

	struct KeyPlan
	{
		int x = -1;
		int y = -1;
		int anchorRank = -1;
		int gateRank = -1;
		int type = 0;
	};
	TArray<KeyPlan> keys;
	TArray<int> gateRanks;
	const int targetKeys = (Size >= 5 && mainPath.Size() >= 18) ? 3 :
		((Size >= 3 && mainPath.Size() >= 13) ? 2 : 1);
	const int keyOrder[] = { 2, 1, 3 }; // blue, red, yellow

	for (int k = 0; k < targetKeys; k++)
	{
		const int gateRank = clamp((int)mainPath.Size() * (k + 1) / (targetKeys + 1), 3,
			(int)mainPath.Size() - 2);
		const int stageStartRank = gateRanks.Size() > 0 ? gateRanks.Last() : 0;
		int anchor = std::max(stageStartRank + 1,
			gateRank - std::max(2, (int)mainPath.Size() / (targetKeys + 3)));
		TArray<std::pair<int, int>> limb;
		bool placed = false;
		for (int attempt = 0; attempt < 8 && !placed; attempt++)
		{
			int tryRank = clamp(anchor + ((attempt + 1) / 2) * ((attempt & 1) ? 1 : -1),
				stageStartRank + 1, gateRank - 1);
			const int keyBranchLength = ScaleSetting(2 + Size / 2 + (RNG() % 2),
				Layout, 78, 122);
			placed = GrowBranch(tryRank, keyBranchLength, limb);
			if (placed) anchor = tryRank;
		}
		if (!placed) continue;

		auto tip = limb.Last();
		Grid[tip.second][tip.first].hasKey = true;
		Grid[tip.second][tip.first].keyType = keyOrder[k];
		Grid[tip.second][tip.first].isArena = true;

		KeyPlan plan;
		plan.x = tip.first;
		plan.y = tip.second;
		plan.anchorRank = anchor;
		plan.gateRank = gateRank;
		plan.type = keyOrder[k];
		keys.Push(plan);
		gateRanks.Push(gateRank);
	}

	if (keys.Size() == 0)
	{
		LastError = "Could not place a key branch";
		return false;
	}

	// A lock belongs to one directed boundary, not to an entire room. This
	// prevents the former two-to-eight locked doors around a single gate cell.
	for (unsigned int k = 0; k < keys.Size(); k++)
	{
		const int rank = keys[k].gateRank;
		const int gx = mainPath[rank].first;
		const int gy = mainPath[rank].second;
		const int px = mainPath[rank - 1].first;
		const int py = mainPath[rank - 1].second;
		Grid[gy][gx].isLocked = true;
		Grid[gy][gx].lockType = keys[k].type;
		Grid[gy][gx].lockDir = DirectionBetween(gx, gy, px, py);
	}

	// Optional branches are spaced across the critical path, giving the player
	// reasons to explore without turning the map into a uniform maze.
	int sideBranchTarget = ScaleSetting(4 + Size + Size / 4, Layout, 52, 155);
	if (Size >= 5 && themeStyle == ThemeHell) sideBranchTarget += std::max(1, Size / 6);
	else if (Size >= 5 && themeStyle == ThemeGothic) sideBranchTarget += std::max(1, Size / 8);
	else if (Size >= 5 && themeStyle == ThemeIndustrial) sideBranchTarget += std::max(1, Size / 10);
	int reservedSecretX = -1;
	int reservedSecretY = -1;
	for (int b = 0; b < sideBranchTarget; b++)
	{
		int rank = clamp((int)mainPath.Size() * (b + 1) / (sideBranchTarget + 1) +
			(int)(RNG() % 3) - 1, 1, (int)mainPath.Size() - 2);
		bool nearKeyBranch = false;
		for (unsigned int k = 0; k < keys.Size(); k++)
			if (abs(keys[k].anchorRank - rank) <= 1) nearKeyBranch = true;
		if (nearKeyBranch) rank = clamp(rank + 2, 1, (int)mainPath.Size() - 2);

		TArray<std::pair<int, int>> limb;
		const int baseLength = 1 + (RNG() % (2 + Size / 2));
		GrowBranch(rank, ScaleSetting(baseLength, Layout, 65, 145), limb);
		if (limb.Size() > 0 && reservedSecretX < 0)
		{
			reservedSecretX = limb.Last().first;
			reservedSecretY = limb.Last().second;
		}
		if (limb.Size() >= 2 && (b & 1))
			Grid[limb.Last().second][limb.Last().first].isArena = true;
	}

	// Keep one optional leaf structurally reserved for a conventional hidden
	// reward. Compact layouts can spend their first few free cells on key limbs,
	// so retry every route anchor before landmark growth if the ordinary branch
	// budget found no room. The reserved tip is isolated from later loops and
	// room merging; this makes the secret a true one-door leaf rather than a
	// cosmetic flag on a through route.
	if (reservedSecretX < 0)
	{
		for (int rank = 1; rank < (int)mainPath.Size() - 1 && reservedSecretX < 0; rank++)
		{
			TArray<std::pair<int, int>> limb;
			if (!GrowBranch(rank, 2, limb) || limb.Size() == 0) continue;
			reservedSecretX = limb.Last().first;
			reservedSecretY = limb.Last().second;
		}
	}
	if (reservedSecretX < 0)
	{
		LastError = "Could not reserve an optional secret branch";
		return false;
	}
	Grid[reservedSecretY][reservedSecretX].reservedSecret = true;

	// Expand selected beats into recognisable chambers. Added cells inherit one
	// progression rank, so the room pass can merge them into a single landmark.
	auto ExpandLandmark = [&](int cx, int cy, int budget, bool hub, bool arena)
	{
		if (!InBounds(cx, cy) || !keep[cy][cx]) return;
		Grid[cy][cx].isHub = Grid[cy][cx].isHub || hub;
		Grid[cy][cx].isArena = Grid[cy][cx].isArena || arena;
		TArray<std::pair<int, int>> cluster;
		cluster.Push(std::make_pair(cx, cy));

		for (int n = 0; n < budget; n++)
		{
			int bestX = -1;
			int bestY = -1;
			int bestScore = -100000;
			for (unsigned int ci = 0; ci < cluster.Size(); ci++)
			{
				const int ax = cluster[ci].first;
				const int ay = cluster[ci].second;
				for (int d = 0; d < 4; d++)
				{
					const int nx = ax + DX[d];
					const int ny = ay + DY[d];
					if (!InBounds(nx, ny) || keep[ny][nx]) continue;
					int adjacentCluster = 0;
					for (int od = 0; od < 4; od++)
					{
						const int ox = nx + DX[od];
						const int oy = ny + DY[od];
						if (InBounds(ox, oy) && keep[oy][ox] &&
							Grid[oy][ox].pathRank == Grid[cy][cx].pathRank)
							adjacentCluster++;
					}
					int score = adjacentCluster * 18 - (abs(nx - cx) + abs(ny - cy)) * 5 + (RNG() % 9);
					if (score > bestScore)
					{
						bestScore = score;
						bestX = nx;
						bestY = ny;
					}
				}
			}

			if (bestX < 0) break;
			keep[bestY][bestX] = true;
			Grid[bestY][bestX].present = true;
			Grid[bestY][bestX].pathRank = Grid[cy][cx].pathRank;
			Grid[bestY][bestX].branchDepth = Grid[cy][cx].branchDepth;
			Grid[bestY][bestX].onMainPath = Grid[cy][cx].onMainPath;
			Grid[bestY][bestX].isHub = hub;
			Grid[bestY][bestX].isArena = arena;
			for (int d = 0; d < 4; d++)
			{
				const int nx = bestX + DX[d];
				const int ny = bestY + DY[d];
				if (InBounds(nx, ny) && keep[ny][nx] &&
						!Grid[ny][nx].reservedSecret &&
						Grid[ny][nx].pathRank == Grid[bestY][bestX].pathRank)
					ConnectCells(bestX, bestY, nx, ny);
			}
			cluster.Push(std::make_pair(bestX, bestY));
		}
	};

	const int combatGrowth = Difficulty - 1;
	auto LandmarkBudget = [&](int budget) -> int
	{
		int result = ScaleSetting(budget, Detail, 68, 140);
		if (Size >= 5 && themeStyle == ThemeGothic && budget >= 2) result++;
		if (Size >= 5 && themeStyle == ThemeHell && budget >= 3) result++;
		return result;
	};
	Grid[ey][ex].hasExit = true;
	Grid[ey][ex].hasBoss = (Difficulty >= 5 || (Difficulty >= 4 && Size >= 4));

	Grid[sy][sx].hasPlayerStart = true;
	ExpandLandmark(sx, sy, LandmarkBudget(1 + Size / 2), true, false);
	// Reserve the finale before secondary landmarks consume nearby empty cells.
	// This keeps the heavyweight-boss capacity check meaningful on compact maps.
	ExpandLandmark(ex, ey, LandmarkBudget(3 + Size / 2 + combatGrowth * 2), false, true);

	const int firstHubRank = clamp((int)mainPath.Size() / 3, 2, (int)mainPath.Size() - 3);
	ExpandLandmark(mainPath[firstHubRank].first, mainPath[firstHubRank].second,
		LandmarkBudget(2 + Size / 2 + combatGrowth / 2), true, false);
	const int arenaRank = clamp((int)mainPath.Size() * 2 / 3, firstHubRank + 1, (int)mainPath.Size() - 2);
	ExpandLandmark(mainPath[arenaRank].first, mainPath[arenaRank].second,
		LandmarkBudget(2 + Size / 2 + combatGrowth * 2), false, true);
	for (unsigned int k = 0; k < keys.Size(); k++)
		ExpandLandmark(keys[k].x, keys[k].y,
			LandmarkBudget(1 + Size / 2 + combatGrowth), false, true);

	// Add local circulation only inside the same lock stage. These loops create
	// classic Doom re-use and cross-views without bypassing key progression.
	auto StageForRank = [&](int rank) -> int
	{
		int stage = 0;
		for (unsigned int k = 0; k < gateRanks.Size(); k++)
			if (rank >= gateRanks[k]) stage++;
		return stage;
	};

	int loopBudget = ScaleSetting(2 + Size + Size / 2, Layout, 35, 180);
	if (themeStyle == ThemeTechbase || themeStyle == ThemeIndustrial)
		loopBudget += std::max(1, Size / 8);
	const int loopChance = Layout == 0 ? 20 : (Layout == 2 ? 58 : 38);
	for (int pass = 0; pass < 3 && loopBudget > 0; pass++)
	{
		for (int y = 1; y < H - 1 && loopBudget > 0; y++)
		{
			for (int x = 1; x < W - 1 && loopBudget > 0; x++)
			{
				if (!keep[y][x]) continue;
				for (int d : { DIR_E, DIR_S })
				{
					const int nx = x + DX[d];
					const int ny = y + DY[d];
					if (!InBounds(nx, ny) || !keep[ny][nx] || Grid[y][x].conn[d]) continue;
					if (Grid[y][x].reservedSecret || Grid[ny][nx].reservedSecret) continue;
					if (StageForRank(Grid[y][x].pathRank) != StageForRank(Grid[ny][nx].pathRank)) continue;
					const int rankGap = abs(Grid[y][x].pathRank - Grid[ny][nx].pathRank);
					const int maximumFoldback = 7 + Size / 5;
					if (rankGap > maximumFoldback) continue;
					// Spend the first pass on reconnections that fold a route back by
					// several beats. Later passes may add local circulation if the
					// layout cannot physically accommodate enough long loops.
					if (pass == 0 && rankGap < 3) continue;
					if ((RNG() % 100) >= loopChance) continue;
					ConnectCells(x, y, nx, ny);
					loopBudget--;
					if (loopBudget <= 0) break;
				}
			}
		}
	}

	// Materialize the progression stage on every kept cell, then audit every
	// connection before room composition can hide its coarse-grid origin. A
	// stage boundary may have exactly one crossing: the directed keyed edge that
	// enters its gate rank. Ordinary portals and unlocked doors are never valid
	// substitutes on that cut.
	for (int y = 1; y < H - 1; y++)
		for (int x = 1; x < W - 1; x++)
			if (keep[y][x])
				Grid[y][x].lockStage = StageForRank(Grid[y][x].pathRank);

	TArray<int> gateCrossings;
	gateCrossings.Resize(keys.Size());
	for (unsigned int k = 0; k < gateCrossings.Size(); k++)
	{
		gateCrossings[k] = 0;
		if (StageForRank(keys[k].anchorRank) + 1 != StageForRank(keys[k].gateRank))
		{
			LastError = "A key was placed outside the stage immediately before its gate";
			return false;
		}
	}

	for (int y = 1; y < H - 1; y++)
	{
		for (int x = 1; x < W - 1; x++)
		{
			if (!keep[y][x]) continue;
			for (int direction : { DIR_E, DIR_S })
			{
				if (!Grid[y][x].conn[direction]) continue;
				const int nx = x + DX[direction];
				const int ny = y + DY[direction];
				if (!InBounds(nx, ny) || !keep[ny][nx]) continue;
				const ProcGenCell& first = Grid[y][x];
				const ProcGenCell& second = Grid[ny][nx];
				if (first.lockStage == second.lockStage)
				{
					const bool ownsLock = (first.isLocked && first.lockDir == direction) ||
						(second.isLocked && second.lockDir == OPP[direction]);
					if (ownsLock)
					{
						LastError = "A keyed edge does not separate two progression stages";
						return false;
					}
					continue;
				}

				if (abs(first.lockStage - second.lockStage) != 1)
				{
					LastError = "A connection skips one or more key progression stages";
					return false;
				}

				const ProcGenCell& later = first.lockStage > second.lockStage ? first : second;
				const int directionToEarlier = first.lockStage > second.lockStage ?
					direction : OPP[direction];
				if (!later.isLocked || later.lockDir != directionToEarlier || later.lockType <= 0)
				{
					LastError = "An unlocked opening bypasses a key progression boundary";
					return false;
				}

				int gateIndex = -1;
				for (unsigned int k = 0; k < keys.Size(); k++)
				{
					if (later.pathRank == keys[k].gateRank && later.lockType == keys[k].type)
					{
						gateIndex = k;
						break;
					}
				}
				if (gateIndex < 0)
				{
					LastError = "A progression boundary is not owned by its planned key gate";
					return false;
				}
				gateCrossings[gateIndex]++;
			}
		}
	}
	for (unsigned int k = 0; k < gateCrossings.Size(); k++)
	{
		if (gateCrossings[k] != 1)
		{
			LastError = "A key gate does not own exactly one progression crossing";
			return false;
		}
	}

	for (int y = 0; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			if (!keep[y][x])
			{
				Grid[y][x] = ProcGenCell();
				continue;
			}

			Grid[y][x].present = true;
			Grid[y][x].neighborCount = 0;
			Grid[y][x].connectionCount = 0;
			for (int d = 0; d < 4; d++)
			{
				const int nx = x + DX[d];
				const int ny = y + DY[d];
				if (!InBounds(nx, ny) || !keep[ny][nx])
					Grid[y][x].conn[d] = false;
				else
				{
					Grid[y][x].neighborCount++;
					if (Grid[y][x].conn[d]) Grid[y][x].connectionCount++;
				}
			}
			if (Grid[y][x].connectionCount >= 3 && Grid[y][x].onMainPath)
				Grid[y][x].isHub = true;
		}
	}

	MergeRooms(W, H);
	ApplyCoherence(W, H);
	PlaceWeapons(W, H);
	return BuildUDMF(W, H);
}
