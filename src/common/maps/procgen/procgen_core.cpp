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
	: Difficulty(3), Size(3)
{
	Theme = "techbase";
}

void FProceduralMapGenerator::SetSeed(int seed)
{
	RNG.Init((uint32_t)seed);
}

void FProceduralMapGenerator::SetTheme(const char* theme)
{
	Theme = theme;
	Theme.ToLower();
}

void FProceduralMapGenerator::SetDifficulty(int difficulty)
{
	Difficulty = clamp(difficulty, 1, 5);
}

void FProceduralMapGenerator::SetSize(int size)
{
	Size = clamp(size, 1, 5);
}

bool FProceduralMapGenerator::Generate()
{
	LastError = "";
	UDMFBuffer = "";
	Grid.Clear();
	Rooms.Clear();

	// A rectangular canvas better matches the broad, directional footprints of
	// classic Doom maps than the old nearly-square, high-density cell carpet.
	const int W = 8 + Size * 2; // 10 .. 18 (2560 .. 4608 map units)
	const int H = 7 + Size;     // 8  .. 12 (2048 .. 3072 map units)

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
			if (d == DIR_E) score += 18;
			if (d == DIR_W) score -= 8;
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

	const int desiredRoute = 9 + Size * 4;
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
		int anchor = std::max(1, gateRank - std::max(2, (int)mainPath.Size() / (targetKeys + 3)));
		TArray<std::pair<int, int>> limb;
		bool placed = false;
		for (int attempt = 0; attempt < 8 && !placed; attempt++)
		{
			int tryRank = clamp(anchor + ((attempt + 1) / 2) * ((attempt & 1) ? 1 : -1), 1, gateRank - 1);
			placed = GrowBranch(tryRank, 2 + Size / 2 + (RNG() % 2), limb);
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
	const int sideBranchTarget = 2 + Size;
	for (int b = 0; b < sideBranchTarget; b++)
	{
		int rank = clamp((int)mainPath.Size() * (b + 1) / (sideBranchTarget + 1) +
			(int)(RNG() % 3) - 1, 1, (int)mainPath.Size() - 2);
		bool nearKeyBranch = false;
		for (unsigned int k = 0; k < keys.Size(); k++)
			if (abs(keys[k].anchorRank - rank) <= 1) nearKeyBranch = true;
		if (nearKeyBranch) rank = clamp(rank + 2, 1, (int)mainPath.Size() - 2);

		TArray<std::pair<int, int>> limb;
		GrowBranch(rank, 1 + (RNG() % (2 + Size / 2)), limb);
		if (limb.Size() >= 2 && (b & 1))
			Grid[limb.Last().second][limb.Last().first].isArena = true;
	}

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
					Grid[ny][nx].pathRank == Grid[bestY][bestX].pathRank)
					ConnectCells(bestX, bestY, nx, ny);
			}
			cluster.Push(std::make_pair(bestX, bestY));
		}
	};

	Grid[sy][sx].hasPlayerStart = true;
	ExpandLandmark(sx, sy, 1 + Size / 2, true, false);

	const int firstHubRank = clamp((int)mainPath.Size() / 3, 2, (int)mainPath.Size() - 3);
	ExpandLandmark(mainPath[firstHubRank].first, mainPath[firstHubRank].second, 2 + Size / 2, true, false);
	const int arenaRank = clamp((int)mainPath.Size() * 2 / 3, firstHubRank + 1, (int)mainPath.Size() - 2);
	ExpandLandmark(mainPath[arenaRank].first, mainPath[arenaRank].second, 2 + Size / 2, false, true);
	for (unsigned int k = 0; k < keys.Size(); k++)
		ExpandLandmark(keys[k].x, keys[k].y, 1 + Size / 2, false, true);

	Grid[ey][ex].hasExit = true;
	Grid[ey][ex].hasBoss = (Difficulty >= 5 || (Difficulty >= 4 && Size >= 4));
	ExpandLandmark(ex, ey, 3 + Size / 2, false, true);

	// Add local circulation only inside the same lock stage. These loops create
	// classic Doom re-use and cross-views without bypassing key progression.
	auto StageForRank = [&](int rank) -> int
	{
		int stage = 0;
		for (unsigned int k = 0; k < gateRanks.Size(); k++)
			if (rank >= gateRanks[k]) stage++;
		return stage;
	};

	int loopBudget = 1 + Size;
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
					if (StageForRank(Grid[y][x].pathRank) != StageForRank(Grid[ny][nx].pathRank)) continue;
					if (abs(Grid[y][x].pathRank - Grid[ny][nx].pathRank) > 5) continue;
					if ((RNG() % 100) >= 38) continue;
					ConnectCells(x, y, nx, ny);
					loopBudget--;
					if (loopBudget <= 0) break;
				}
			}
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
