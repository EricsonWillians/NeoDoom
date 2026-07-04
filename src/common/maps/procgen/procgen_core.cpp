/*
** procgen_core.cpp
**
** Core procedural map generation: grid dungeon DFS, loop injection,
** key-door progression, and initial per-cell theming.
**
**---------------------------------------------------------------------------
*/

#include "procgen_internal.h"
#include "printf.h"

using namespace ProcGen;

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Grid dungeon generator
// ---------------------------------------------------------------------------

bool FProceduralMapGenerator::Generate()
{
	LastError = "";
	UDMFBuffer = "";
	Grid.Clear();

	int W = 6 + Size * 2;   // 8 .. 16
	int H = 5 + Size * 2;   // 7 .. 15

	Grid.Resize(H);
	for (int j = 0; j < H; j++)
	{
		Grid[j].Resize(W);
		for (int i = 0; i < W; i++)
			Grid[j][i] = ProcGenCell();
	}

	auto InBounds = [&](int x, int y) -> bool
	{
		return x >= 0 && x < W && y >= 0 && y < H;
	};

	auto ConnectCells = [&](int ax, int ay, int bx, int by)
	{
		for (int d = 0; d < 4; d++)
		{
			if (ax + DX[d] == bx && ay + DY[d] == by)
			{
				Grid[ay][ax].conn[d] = true;
				Grid[by][bx].conn[OPP[d]] = true;
				break;
			}
		}
	};

	int sx = 1 + (RNG() % std::max(1, W / 3));
	int sy = H - 2 - (RNG() % std::max(1, H / 3));

	TArray<TArray<bool>> visited;
	visited.Resize(H);
	for (int j = 0; j < H; j++)
	{
		visited[j].Resize(W);
		for (int i = 0; i < W; i++)
			visited[j][i] = false;
	}

	TArray<TArray<std::pair<int, int>>> parent;
	parent.Resize(H);
	for (int j = 0; j < H; j++)
	{
		parent[j].Resize(W);
		for (int i = 0; i < W; i++)
			parent[j][i] = std::make_pair(-1, -1);
	}

	TArray<std::pair<int, int>> stack;
	stack.Push(std::make_pair(sx, sy));
	visited[sy][sx] = true;
	Grid[sy][sx].present = true;
	int presentCount = 1;
	int targetCells = (W * H * (56 + Size * 4)) / 100;
	const int densityBias = (RNG() % 9) - 4; // -4..4
	targetCells += densityBias * (W * H) / 30;
	int minimumCells = 20 + Size * 8;
	if (targetCells < minimumCells) targetCells = minimumCells;
	int maximumCells = W * H - (W + H) / 8;
	if (targetCells > maximumCells) targetCells = maximumCells;

	while (stack.Size() > 0 && presentCount < targetCells)
	{
		int cx = stack.Last().first;
		int cy = stack.Last().second;

		TArray<int> dirs;
		int bestScore = -100000;
		for (int d = 0; d < 4; d++)
		{
			int nx = cx + DX[d];
			int ny = cy + DY[d];
			if (!InBounds(nx, ny) || visited[ny][nx]) continue;

			int score = nx * 5 + (H - 1 - ny) * 6;
			if (d == DIR_N) score += 4;
			if (d == DIR_E) score += 2;
			score += (int)(RNG() % 5) - 2;

			if (score > bestScore)
			{
				bestScore = score;
				dirs.Clear();
				dirs.Push(d);
			}
			else if (score == bestScore)
			{
				dirs.Push(d);
			}
		}

		if (dirs.Size() == 0)
		{
			stack.Pop();
			continue;
		}

		int dir = dirs[RNG() % dirs.Size()];
		int nx = cx + DX[dir];
		int ny = cy + DY[dir];
		visited[ny][nx] = true;
		Grid[ny][nx].present = true;
		presentCount++;
		parent[ny][nx] = std::make_pair(cx, cy);
		ConnectCells(cx, cy, nx, ny);
		stack.Push(std::make_pair(nx, ny));
	}

	// If the first biased walk finished too early, seed additional growth from
	// existing frontier cells instead of filling the entire grid.
	while (presentCount < targetCells)
	{
		TArray<std::pair<int, int>> restartCells;
		for (int j = 0; j < H; j++)
		{
			for (int i = 0; i < W; i++)
			{
				if (!Grid[j][i].present) continue;
				for (int d = 0; d < 4; d++)
				{
					int nx = i + DX[d];
					int ny = j + DY[d];
					if (InBounds(nx, ny) && !visited[ny][nx])
					{
						restartCells.Push(std::make_pair(i, j));
						break;
					}
				}
			}
		}

		if (restartCells.Size() == 0)
			break;

		auto restart = restartCells[RNG() % restartCells.Size()];
		stack.Push(restart);

		while (stack.Size() > 0 && presentCount < targetCells)
		{
			int cx = stack.Last().first;
			int cy = stack.Last().second;

			TArray<int> dirs;
			int bestScore = -100000;
			for (int d = 0; d < 4; d++)
			{
				int nx = cx + DX[d];
				int ny = cy + DY[d];
				if (!InBounds(nx, ny) || visited[ny][nx]) continue;

				int score = 0;
				score += (d == DIR_N) ? 3 : 0;
				score += (d == DIR_E) ? 2 : 0;
				score += (Grid[cy][cx].onMainPath ? 2 : 0);
				score += (int)(RNG() % 7) - 3;

				if (score > bestScore)
				{
					bestScore = score;
					dirs.Clear();
					dirs.Push(d);
				}
				else if (score == bestScore)
				{
					dirs.Push(d);
				}
			}

			if (dirs.Size() == 0)
			{
				stack.Pop();
				continue;
			}

			int dir = dirs[RNG() % dirs.Size()];
			int nx = cx + DX[dir];
			int ny = cy + DY[dir];
			visited[ny][nx] = true;
			Grid[ny][nx].present = true;
			presentCount++;
			parent[ny][nx] = std::make_pair(cx, cy);
			ConnectCells(cx, cy, nx, ny);
			stack.Push(std::make_pair(nx, ny));
		}
	}

	TArray<TArray<int>> dist;
	dist.Resize(H);
	for (int j = 0; j < H; j++)
	{
		dist[j].Resize(W);
		for (int i = 0; i < W; i++)
			dist[j][i] = -1;
	}

	TArray<std::pair<int, int>> bfsQueue;
	dist[sy][sx] = 0;
	bfsQueue.Push(std::make_pair(sx, sy));
	for (unsigned int qi = 0; qi < bfsQueue.Size(); qi++)
	{
		int cx = bfsQueue[qi].first;
		int cy = bfsQueue[qi].second;
		for (int d = 0; d < 4; d++)
		{
			if (!Grid[cy][cx].conn[d]) continue;
			int nx = cx + DX[d];
			int ny = cy + DY[d];
			if (!InBounds(nx, ny) || dist[ny][nx] != -1) continue;
			dist[ny][nx] = dist[cy][cx] + 1;
			parent[ny][nx] = std::make_pair(cx, cy);
			bfsQueue.Push(std::make_pair(nx, ny));
		}
	}

	int ex = sx;
	int ey = sy;
	int bestExitScore = -100000;
	int minExitDist = std::max(6, W / 3 + H / 4 + Size);
	for (int j = 0; j < H; j++)
	{
		for (int i = 0; i < W; i++)
		{
			if (dist[j][i] < 0 || (i == sx && j == sy)) continue;
			int connCount = 0;
			for (int d = 0; d < 4; d++)
				if (Grid[j][i].conn[d]) connCount++;
			int score = dist[j][i] * 28 + i * 5 + (H - 1 - j) * 7;
			if (connCount == 1) score += 20;
			else if (connCount == 2) score += 6;
			else score -= 10;
			if (dist[j][i] < minExitDist)
				score -= (minExitDist - dist[j][i]) * 32;
			if (score > bestExitScore)
			{
				bestExitScore = score;
				ex = i;
				ey = j;
			}
		}
	}

	if (ex == sx && ey == sy)
	{
		int fallbackDist = -1;
		for (int j = 0; j < H; j++)
		{
			for (int i = 0; i < W; i++)
			{
				if ((i == sx && j == sy) || dist[j][i] < 0) continue;
				if (dist[j][i] > fallbackDist)
				{
					fallbackDist = dist[j][i];
					ex = i;
					ey = j;
				}
			}
		}
	}

	TArray<std::pair<int, int>> mainPath;
	int px = ex;
	int py = ey;
	mainPath.Push(std::make_pair(px, py));
	int guard = W * H + 4;
	while (!(px == sx && py == sy) && guard-- > 0)
	{
		auto p = parent[py][px];
		if (p.first < 0 || p.second < 0)
			break;
		px = p.first;
		py = p.second;
		mainPath.Push(std::make_pair(px, py));
	}
	for (int i = 0, j = (int)mainPath.Size() - 1; i < j; i++, j--)
	{
		auto tmp = mainPath[i];
		mainPath[i] = mainPath[j];
		mainPath[j] = tmp;
	}

	TArray<TArray<bool>> keep;
	TArray<TArray<bool>> usedBranch;
	TArray<TArray<int>> mainRank;
	keep.Resize(H);
	usedBranch.Resize(H);
	mainRank.Resize(H);
	for (int j = 0; j < H; j++)
	{
		keep[j].Resize(W);
		usedBranch[j].Resize(W);
		mainRank[j].Resize(W);
		for (int i = 0; i < W; i++)
		{
			keep[j][i] = false;
			usedBranch[j][i] = false;
			mainRank[j][i] = -1;
		}
	}

	for (int rank = 0; rank < (int)mainPath.Size(); rank++)
	{
		int x = mainPath[rank].first;
		int y = mainPath[rank].second;
		keep[y][x] = true;
		mainRank[y][x] = rank;
		Grid[y][x].pathRank = rank;
		Grid[y][x].onMainPath = true;
		Grid[y][x].branchDepth = 0;
	}

	auto MarkMainSpan = [&](int centerRank, int radius, bool hub, bool arena)
	{
		if (mainPath.Size() == 0) return;
		int startRank = clamp(centerRank - radius, 0, (int)mainPath.Size() - 1);
		int endRank = clamp(centerRank + radius, 0, (int)mainPath.Size() - 1);
		for (int rank = startRank; rank <= endRank; rank++)
		{
			int x = mainPath[rank].first;
			int y = mainPath[rank].second;
			Grid[y][x].isHub = Grid[y][x].isHub || hub;
			Grid[y][x].isArena = Grid[y][x].isArena || arena;
		}
	};

	auto JitterRank = [&](int baseRank, int spread, int lowerBound, int upperBound) -> int
	{
		int roll = RNG() % (spread * 2 + 1);
		return clamp(baseRank + roll - spread, lowerBound, upperBound);
	};

	const int routeStyle = RNG() % 5;
	int corridorBias = 0;
	bool compactLoops = false;
	int sideBranchBoost = 0;
	int shoulderStep = 2;
	int spinePocketStep = 3;
	int spinePassStep = 1;
	int routeLoopBias = 0;
	int braidBonus = 1;
	int lateAnchorSpread = 5;
	int branchAnchorCount = 4;
	int branchAnchorStride = 2;
	const int branchAnchorJitter = (RNG() % 4);

	switch (routeStyle)
	{
	case 0: // Expansive cathedral route with broad side growth.
		corridorBias = 1;
		sideBranchBoost = 1;
		shoulderStep = 1;
		spinePocketStep = 3;
		spinePassStep = 1;
		routeLoopBias = 2;
		braidBonus = 3;
		lateAnchorSpread = 5;
		branchAnchorCount = 5;
		branchAnchorStride = 2;
		break;
	case 1: // Dense branch-limb generation.
		corridorBias = 0;
		compactLoops = true;
		sideBranchBoost = 2;
		shoulderStep = 2;
		spinePocketStep = 2;
		spinePassStep = 2;
		routeLoopBias = -1;
		braidBonus = 0;
		lateAnchorSpread = 4;
		branchAnchorCount = 6;
		branchAnchorStride = 1;
		break;
	case 2: // Compact/controlled route with moderate branching.
		corridorBias = -1;
		sideBranchBoost = 0;
		shoulderStep = 2;
		spinePocketStep = 3;
		spinePassStep = 1;
		routeLoopBias = 1;
		braidBonus = 1;
		lateAnchorSpread = 4;
		branchAnchorCount = 4;
		branchAnchorStride = 2;
		break;
	case 3: // Linear progression, fewer late surprises.
		corridorBias = -1;
		compactLoops = true;
		sideBranchBoost = -1;
		shoulderStep = 2;
		spinePocketStep = 3;
		spinePassStep = 3;
		routeLoopBias = -2;
		braidBonus = -1;
		lateAnchorSpread = 2;
		branchAnchorCount = 3;
		branchAnchorStride = 3;
		break;
	default: // 4: Braided labyrinth with lots of alternate lanes and loops.
		corridorBias = 0;
		sideBranchBoost = 2;
		shoulderStep = 1;
		spinePocketStep = 1;
		spinePassStep = 1;
		routeLoopBias = 4;
		braidBonus = 5;
		lateAnchorSpread = 5;
		branchAnchorCount = 7;
		branchAnchorStride = 1;
		break;
	}

	if (mainPath.Size() >= 2)
		MarkMainSpan(1, 1, true, false);
	if (mainPath.Size() >= 5)
		MarkMainSpan((int)mainPath.Size() - 3, 1, false, true);
	if (mainPath.Size() >= 7)
		MarkMainSpan((int)mainPath.Size() - 5, 1, true, false);

	struct BranchCandidate
	{
		TArray<std::pair<int, int>> chain;
		int anchorRank = -1;
		int score = 0;
	};

	TArray<BranchCandidate> branchCandidates;
	for (int j = 0; j < H; j++)
	{
		for (int i = 0; i < W; i++)
		{
			if (mainRank[j][i] >= 0) continue;

			int connCount = 0;
			for (int d = 0; d < 4; d++)
				if (Grid[j][i].conn[d]) connCount++;
			if (connCount != 1) continue;

			BranchCandidate candidate;
			int cx = i;
			int cy = j;
			int traceGuard = W * H + 4;
			while (traceGuard-- > 0 && mainRank[cy][cx] < 0)
			{
				candidate.chain.Push(std::make_pair(cx, cy));
				auto p = parent[cy][cx];
				if (p.first < 0 || p.second < 0)
				{
					candidate.chain.Clear();
					break;
				}
				cx = p.first;
				cy = p.second;
			}
			if (candidate.chain.Size() == 0) continue;
			if (mainRank[cy][cx] < 0) continue;

				candidate.anchorRank = mainRank[cy][cx];
				if (candidate.anchorRank <= 0 || candidate.anchorRank >= (int)mainPath.Size() - 1)
					continue;

				candidate.score = candidate.anchorRank * 4 + dist[j][i] * 6 + (int)candidate.chain.Size() * 14;
				if (routeStyle == 4 && candidate.chain.Size() >= 4)
					candidate.score -= 10;
				else if (routeStyle == 1 && candidate.chain.Size() <= 2)
					candidate.score -= 12;
				branchCandidates.Push(candidate);
			}
	}

	for (int i = 0; i < (int)branchCandidates.Size(); i++)
	{
		int best = i;
		for (int j = i + 1; j < (int)branchCandidates.Size(); j++)
		{
			if (branchCandidates[j].score > branchCandidates[best].score)
				best = j;
		}
		if (best != i)
		{
			auto tmp = branchCandidates[i];
			branchCandidates[i] = branchCandidates[best];
			branchCandidates[best] = tmp;
		}
	}

	auto KeepBranchChain = [&](const TArray<std::pair<int, int>>& chain, int anchorRank, bool arenaTail)
	{
		for (int idx = (int)chain.Size() - 1; idx >= 0; idx--)
		{
			int x = chain[idx].first;
			int y = chain[idx].second;
			int depth = (int)chain.Size() - idx;
			keep[y][x] = true;
			usedBranch[y][x] = true;
			if (Grid[y][x].pathRank < 0 || anchorRank < Grid[y][x].pathRank)
				Grid[y][x].pathRank = anchorRank;
			if (depth > Grid[y][x].branchDepth)
				Grid[y][x].branchDepth = depth;
			if (arenaTail && idx == 0)
				Grid[y][x].isArena = true;
		}
	};

	int targetKeys = 1;
	if (Size >= 3 && mainPath.Size() >= 8)
		targetKeys = 2;
	if (Size >= 5 && Difficulty >= 4 && mainPath.Size() >= 12)
		targetKeys = 3;

	TArray<int> desiredGates;
	if (targetKeys == 1)
	{
		desiredGates.Push(clamp((int)mainPath.Size() * 2 / 3, 2, (int)mainPath.Size() - 2));
	}
	else if (targetKeys == 2)
	{
		int gate1 = clamp((int)mainPath.Size() / 3, 2, (int)mainPath.Size() - 4);
		int gate2 = clamp((int)mainPath.Size() * 2 / 3, gate1 + 2, (int)mainPath.Size() - 2);
		desiredGates.Push(gate1);
		desiredGates.Push(gate2);
	}
	else
	{
		int gate1 = clamp((int)mainPath.Size() / 4, 2, (int)mainPath.Size() - 6);
		int gate2 = clamp((int)mainPath.Size() / 2, gate1 + 2, (int)mainPath.Size() - 4);
		int gate3 = clamp((int)mainPath.Size() * 3 / 4, gate2 + 2, (int)mainPath.Size() - 2);
		desiredGates.Push(gate1);
		desiredGates.Push(gate2);
		desiredGates.Push(gate3);
	}

	struct KeyPlacement
	{
		int x = -1;
		int y = -1;
		int anchorRank = -1;
		int keyType = 0;
	};

	TArray<KeyPlacement> placedKeys;
	const int keyOrder[3] = { 2, 1, 3 };
	for (int k = 0; k < targetKeys; k++)
	{
		int gateRank = desiredGates[k];
		int windowMin = (k == 0) ? 1 : desiredGates[k - 1] + 1;
		int windowMax = gateRank - 1;
		int picked = -1;

		for (int i = 0; i < (int)branchCandidates.Size(); i++)
		{
			const BranchCandidate& candidate = branchCandidates[i];
			if (candidate.anchorRank < windowMin || candidate.anchorRank > windowMax)
				continue;

			bool overlaps = false;
			for (unsigned int ci = 0; ci < candidate.chain.Size(); ci++)
			{
				int x = candidate.chain[ci].first;
				int y = candidate.chain[ci].second;
				if (usedBranch[y][x])
				{
					overlaps = true;
					break;
				}
			}
			if (!overlaps)
			{
				picked = i;
				break;
			}
		}

		if (picked < 0)
		{
			for (int i = 0; i < (int)branchCandidates.Size(); i++)
			{
				const BranchCandidate& candidate = branchCandidates[i];
				if (candidate.anchorRank <= 0 || candidate.anchorRank >= gateRank)
					continue;

				bool overlaps = false;
				for (unsigned int ci = 0; ci < candidate.chain.Size(); ci++)
				{
					int x = candidate.chain[ci].first;
					int y = candidate.chain[ci].second;
					if (usedBranch[y][x])
					{
						overlaps = true;
						break;
					}
				}
				if (!overlaps)
				{
					picked = i;
					break;
				}
			}
		}

		if (picked >= 0)
		{
			const BranchCandidate& candidate = branchCandidates[picked];
			KeepBranchChain(candidate.chain, candidate.anchorRank, true);

			int keyX = candidate.chain[0].first;
			int keyY = candidate.chain[0].second;
			Grid[keyY][keyX].hasKey = true;
			Grid[keyY][keyX].keyType = keyOrder[k % countof(keyOrder)];
			Grid[keyY][keyX].isArena = true;

			KeyPlacement placement;
			placement.x = keyX;
			placement.y = keyY;
			placement.anchorRank = candidate.anchorRank;
			placement.keyType = Grid[keyY][keyX].keyType;
			placedKeys.Push(placement);
		}
	}

	if (placedKeys.Size() == 0)
	{
		for (int rank = 1; rank < (int)mainPath.Size() - 1 && placedKeys.Size() == 0; rank++)
		{
			int ax = mainPath[rank].first;
			int ay = mainPath[rank].second;
			for (int d = 0; d < 4; d++)
			{
				int nx = ax + DX[d];
				int ny = ay + DY[d];
				if (!InBounds(nx, ny) || keep[ny][nx]) continue;
				keep[ny][nx] = true;
				Grid[ny][nx].pathRank = rank;
				Grid[ny][nx].branchDepth = 1;
				Grid[ny][nx].hasKey = true;
				Grid[ny][nx].keyType = keyOrder[0];
				ConnectCells(ax, ay, nx, ny);

				KeyPlacement placement;
				placement.x = nx;
				placement.y = ny;
				placement.anchorRank = rank;
				placement.keyType = keyOrder[0];
				placedKeys.Push(placement);
				break;
			}
		}
	}

	int targetSideBranches = clamp(4 + Size / 2 + Difficulty / 3 + corridorBias + sideBranchBoost + (branchAnchorCount - 4), 3, 14);
	auto TryKeepBranch = [&](const BranchCandidate& candidate) -> bool
	{
		if (candidate.anchorRank <= 0 || candidate.anchorRank >= (int)mainPath.Size() - 1)
			return false;

		bool overlaps = false;
		for (unsigned int ci = 0; ci < candidate.chain.Size(); ci++)
		{
			int x = candidate.chain[ci].first;
			int y = candidate.chain[ci].second;
			if (usedBranch[y][x])
			{
				overlaps = true;
				break;
			}
		}
		if (overlaps) return false;

		KeepBranchChain(candidate.chain, candidate.anchorRank, candidate.chain.Size() >= 2);
		targetSideBranches--;
		return true;
	};

	TArray<int> desiredBranchAnchors;
	if (mainPath.Size() >= 6)
	{
		const int maxRank = (int)mainPath.Size() - 2;
		const int sampleCount = clamp(branchAnchorCount + branchAnchorJitter - 1, 3, 8);
		const int spread = std::max(1, lateAnchorSpread - 1);
		for (int s = 1; s <= sampleCount; s++)
		{
			int rank = JitterRank((int)mainPath.Size() * s / (sampleCount + 1), spread, 1, maxRank);
			if ((s & 1) == 0 && routeStyle == 3)
				rank = clamp(rank + branchAnchorStride, 1, maxRank);
			if (routeStyle == 1 && (s & 1))
				rank = std::max(1, rank - branchAnchorStride);
			desiredBranchAnchors.Push(rank);
		}
	}

	for (unsigned int ai = 0; ai < desiredBranchAnchors.Size() && targetSideBranches > 0; ai++)
	{
		int desired = desiredBranchAnchors[ai];
		int bestIndex = -1;
		int bestScore = 1000000;
		for (int i = 0; i < (int)branchCandidates.Size(); i++)
		{
			const BranchCandidate& candidate = branchCandidates[i];
			if (candidate.anchorRank <= 0 || candidate.anchorRank >= (int)mainPath.Size() - 1)
				continue;

			int score = abs(candidate.anchorRank - desired) * 24;
			score -= (int)candidate.chain.Size() * 12;
			score -= candidate.score / 2;
			if (candidate.chain.Size() >= 2) score -= 16;
			if (candidate.anchorRank >= (int)mainPath.Size() * 2 / 3) score -= 8;
			if (candidate.anchorRank <= (int)mainPath.Size() / 4) score -= 4;
			if (routeStyle == 0 && (candidate.anchorRank & 1) == 0) score -= 4;
			if (routeStyle == 1 && (candidate.chain.Size() < 3) && (candidate.anchorRank <= (int)mainPath.Size() / 2)) score += 8;
			if (routeStyle == 3 && candidate.anchorRank >= (int)mainPath.Size() * 3 / 4) score += 10;
			if (bestIndex < 0 || score < bestScore)
			{
				bestIndex = i;
				bestScore = score;
			}
		}

		if (bestIndex >= 0)
			TryKeepBranch(branchCandidates[bestIndex]);
	}

	for (int i = 0; i < (int)branchCandidates.Size() && targetSideBranches > 0; i++)
	{
		const BranchCandidate& candidate = branchCandidates[i];
		TryKeepBranch(candidate);
	}

	auto ExpandArena = [&](int ax, int ay, int extra, bool inheritMain)
	{
		if (!InBounds(ax, ay) || !keep[ay][ax]) return;

		Grid[ay][ax].isArena = true;
		TArray<std::pair<int, int>> frontier;
		frontier.Push(std::make_pair(ax, ay));

		for (int placed = 0; placed < extra; placed++)
		{
			int bestX = -1;
			int bestY = -1;
			int bestScore = -100000;

			for (unsigned int fi = 0; fi < frontier.Size(); fi++)
			{
				int fx = frontier[fi].first;
				int fy = frontier[fi].second;
				for (int d = 0; d < 4; d++)
				{
					int nx = fx + DX[d];
					int ny = fy + DY[d];
					if (!InBounds(nx, ny) || keep[ny][nx]) continue;

					int score = 24 - (abs(nx - ax) + abs(ny - ay)) * 6;
					if (inheritMain) score += 4;
					if (Grid[fy][fx].isHub) score += 4;
					if (nx == 0 || ny == 0 || nx == W - 1 || ny == H - 1) score -= 3;
					int openness = 0;
					for (int od = 0; od < 4; od++)
					{
						int ox = nx + DX[od];
						int oy = ny + DY[od];
						if (InBounds(ox, oy) && !keep[oy][ox]) openness++;
					}
					score += openness * 3;
					if (score > bestScore)
					{
						bestScore = score;
						bestX = nx;
						bestY = ny;
					}
				}
			}

			if (bestX < 0 || bestY < 0)
				break;

			keep[bestY][bestX] = true;
			Grid[bestY][bestX].pathRank = Grid[ay][ax].pathRank;
			Grid[bestY][bestX].branchDepth = inheritMain ? std::max(1, Grid[ay][ax].branchDepth) : Grid[ay][ax].branchDepth;
			Grid[bestY][bestX].onMainPath = false;
			Grid[bestY][bestX].isArena = true;

			for (int d = 0; d < 4; d++)
			{
				int nx = bestX + DX[d];
				int ny = bestY + DY[d];
				if (!InBounds(nx, ny) || !keep[ny][nx]) continue;
				if (abs(Grid[ny][nx].pathRank - Grid[bestY][bestX].pathRank) > 1 && !Grid[ny][nx].isArena)
					continue;
				ConnectCells(bestX, bestY, nx, ny);
			}

			frontier.Push(std::make_pair(bestX, bestY));
		}
	};

	auto ExpandShoulders = [&](int ax, int ay, int extra, bool inheritMain)
	{
		if (!InBounds(ax, ay) || !keep[ay][ax]) return;

		for (int placed = 0; placed < extra; placed++)
		{
			int bestX = -1;
			int bestY = -1;
			int bestScore = -100000;

			for (int fy = 0; fy < H; fy++)
			{
				for (int fx = 0; fx < W; fx++)
				{
					if (!keep[fy][fx]) continue;
					if (abs(fx - ax) + abs(fy - ay) > 2) continue;

					for (int d = 0; d < 4; d++)
					{
						int nx = fx + DX[d];
						int ny = fy + DY[d];
						if (!InBounds(nx, ny) || keep[ny][nx]) continue;

						int nearAnchor = abs(nx - ax) + abs(ny - ay);
						if (nearAnchor > 2) continue;

						int score = 40 - nearAnchor * 10;
						int adjacency = 0;
						int openness = 0;
						for (int od = 0; od < 4; od++)
						{
							int ox = nx + DX[od];
							int oy = ny + DY[od];
							if (!InBounds(ox, oy)) continue;
							if (keep[oy][ox]) adjacency++;
							else openness++;
						}
						score += adjacency * 10;
						score += openness * 2;
						if (inheritMain) score += 4;
						if (Grid[fy][fx].isHub || Grid[fy][fx].isArena) score += 6;
						if (Grid[fy][fx].branchDepth >= 2) score += 3;
						if (score > bestScore)
						{
							bestScore = score;
							bestX = nx;
							bestY = ny;
						}
					}
				}
			}

			if (bestX < 0 || bestY < 0)
				break;

			keep[bestY][bestX] = true;
			Grid[bestY][bestX].pathRank = Grid[ay][ax].pathRank;
			Grid[bestY][bestX].branchDepth = inheritMain ? std::max(1, Grid[ay][ax].branchDepth) : Grid[ay][ax].branchDepth;
			Grid[bestY][bestX].onMainPath = false;
			Grid[bestY][bestX].isHub = false;

			for (int d = 0; d < 4; d++)
			{
				int nx = bestX + DX[d];
				int ny = bestY + DY[d];
				if (!InBounds(nx, ny) || !keep[ny][nx]) continue;
				if (abs(Grid[ny][nx].pathRank - Grid[bestY][bestX].pathRank) > 1 &&
					!Grid[ny][nx].isArena && !Grid[bestY][bestX].isArena)
					continue;
				ConnectCells(bestX, bestY, nx, ny);
			}
		}
	};

	auto ExpandSpinePocket = [&](int rank, int extra)
	{
		if (rank <= 0 || rank >= (int)mainPath.Size() - 1) return;
		if (extra <= 0) return;

		int cx = mainPath[rank].first;
		int cy = mainPath[rank].second;
		int px = mainPath[rank - 1].first;
		int py = mainPath[rank - 1].second;
		int nx = mainPath[rank + 1].first;
		int ny = mainPath[rank + 1].second;

		int dirs[2];
		if (abs(nx - px) >= abs(ny - py))
		{
			dirs[0] = DIR_N;
			dirs[1] = DIR_S;
		}
		else
		{
			dirs[0] = DIR_W;
			dirs[1] = DIR_E;
		}

		for (int pass = 0; pass < extra; pass++)
		{
			for (int side = 0; side < 2; side++)
			{
				int tx = cx + DX[dirs[side]] * (1 + pass);
				int ty = cy + DY[dirs[side]] * (1 + pass);
				if (!InBounds(tx, ty) || keep[ty][tx]) continue;

				keep[ty][tx] = true;
				Grid[ty][tx].pathRank = Grid[cy][cx].pathRank;
				Grid[ty][tx].branchDepth = std::max(1, Grid[cy][cx].branchDepth);
				Grid[ty][tx].onMainPath = false;
				Grid[ty][tx].isHub = false;
				Grid[ty][tx].isArena = false;

				for (int d = 0; d < 4; d++)
				{
					int ox = tx + DX[d];
					int oy = ty + DY[d];
					if (!InBounds(ox, oy) || !keep[oy][ox]) continue;
					if (abs(Grid[oy][ox].pathRank - Grid[ty][tx].pathRank) > 1 &&
						!Grid[oy][ox].isArena && !Grid[ty][tx].isArena)
						continue;
					ConnectCells(tx, ty, ox, oy);
				}
			}
		}
	};

	auto ExpandWing = [&](int rank, int length, bool inheritMain)
	{
		if (rank <= 0 || rank >= (int)mainPath.Size() - 1) return;
		if (length <= 0) return;

		int cx = mainPath[rank].first;
		int cy = mainPath[rank].second;
		int px = mainPath[rank - 1].first;
		int py = mainPath[rank - 1].second;
		int nx = mainPath[rank + 1].first;
		int ny = mainPath[rank + 1].second;

		int sideDirs[2];
		if (abs(nx - px) >= abs(ny - py))
		{
			sideDirs[0] = DIR_N;
			sideDirs[1] = DIR_S;
		}
		else
		{
			sideDirs[0] = DIR_W;
			sideDirs[1] = DIR_E;
		}

		int bestDir = -1;
		int bestScore = -100000;
		for (int s = 0; s < 2; s++)
		{
			int score = 0;
			int tx = cx;
			int ty = cy;
			bool blocked = false;
			for (int step = 1; step <= length; step++)
			{
				tx += DX[sideDirs[s]];
				ty += DY[sideDirs[s]];
				if (!InBounds(tx, ty) || keep[ty][tx])
				{
					blocked = true;
					break;
				}
				score += 20;
				for (int d = 0; d < 4; d++)
				{
					int ox = tx + DX[d];
					int oy = ty + DY[d];
					if (InBounds(ox, oy) && !keep[oy][ox]) score += 2;
				}
			}
			if (!blocked && score > bestScore)
			{
				bestScore = score;
				bestDir = sideDirs[s];
			}
		}

		if (bestDir < 0) return;

		int tx = cx;
		int ty = cy;
		for (int step = 1; step <= length; step++)
		{
			tx += DX[bestDir];
			ty += DY[bestDir];
			if (!InBounds(tx, ty) || keep[ty][tx]) break;

			keep[ty][tx] = true;
			Grid[ty][tx].pathRank = Grid[cy][cx].pathRank;
			Grid[ty][tx].branchDepth = inheritMain ? 1 : Grid[cy][cx].branchDepth + 1;
			Grid[ty][tx].onMainPath = false;
			Grid[ty][tx].isHub = false;
			Grid[ty][tx].isArena = (step == length) || Grid[cy][cx].isArena;

			for (int d = 0; d < 4; d++)
			{
				int ox = tx + DX[d];
				int oy = ty + DY[d];
				if (!InBounds(ox, oy) || !keep[oy][ox]) continue;
				if (abs(Grid[oy][ox].pathRank - Grid[ty][tx].pathRank) > 1 &&
					!Grid[oy][ox].isArena && !Grid[ty][tx].isArena)
					continue;
				ConnectCells(tx, ty, ox, oy);
			}
		}

		ExpandShoulders(tx, ty, 1 + length / 2, false);
	};

	if (mainPath.Size() >= 4)
	{
		int anchorA = clamp((int)mainPath.Size() / 3, 1, (int)mainPath.Size() - 2);
		int anchorB = clamp((int)mainPath.Size() * 2 / 3, anchorA + 1, (int)mainPath.Size() - 2);
		int anchorC = clamp((int)mainPath.Size() - 2, 1, (int)mainPath.Size() - 2);
		MarkMainSpan(anchorA, 1, true, false);
		MarkMainSpan(anchorB, 1, true, false);
		MarkMainSpan(anchorC, 1, false, true);
		ExpandArena(mainPath[anchorA].first, mainPath[anchorA].second, 2 + Size / 2, true);
		ExpandArena(mainPath[anchorB].first, mainPath[anchorB].second, 2 + Difficulty / 2 + Size / 3, true);
		if (anchorC != anchorB)
			ExpandArena(mainPath[anchorC].first, mainPath[anchorC].second, 1 + Size / 2 + Difficulty / 3, true);
	}

	ExpandShoulders(sx, sy, 4 + Size / 2, true);
	if (mainPath.Size() > 1)
		ExpandShoulders(mainPath[1].first, mainPath[1].second, 2 + Size / 3, true);

	for (int rank = 1; rank < (int)mainPath.Size() - 1; rank += shoulderStep)
	{
		int rx = mainPath[rank].first;
		int ry = mainPath[rank].second;
		int extra = 1;
		if (Grid[ry][rx].isHub) extra++;
		if (rank >= (int)mainPath.Size() / 2) extra++;
		if (rank >= (int)mainPath.Size() * 2 / 3) extra++;
		if (routeStyle == 0 || routeStyle == 4) extra += RNG() % 2;
		if (compactLoops && (RNG() % 3) == 0) extra = std::max(1, extra - 1);
		ExpandShoulders(rx, ry, extra, true);
	}

	for (int rank = 2; rank < (int)mainPath.Size() - 2; rank += spinePocketStep)
	{
		int rx = mainPath[rank].first;
		int ry = mainPath[rank].second;
		if (!Grid[ry][rx].isHub && !Grid[ry][rx].isArena)
			ExpandShoulders(rx, ry, 1 + (rank >= (int)mainPath.Size() / 2), true);
	}

	for (int rank = 1; rank < (int)mainPath.Size() - 1; rank += spinePassStep)
	{
		int extra = 0;
		if (Grid[mainPath[rank].second][mainPath[rank].first].isHub)
			extra = 2 + (rank >= (int)mainPath.Size() / 2);
		else if (rank >= (int)mainPath.Size() * 2 / 3)
			extra = 1 + (Difficulty >= 4);
		else if (rank >= (int)mainPath.Size() / 3 && (rank % 2) == 0)
			extra = 1;
		if ((routeStyle == 0 || routeStyle == 4) && (RNG() % 2) == 0)
			extra += 1;
		if (compactLoops && extra > 0 && (RNG() % 3) == 0)
			extra = std::max(0, extra - 1);

		ExpandSpinePocket(rank, extra);
	}

	for (int rank = 2; rank < (int)mainPath.Size() - 2; rank++)
	{
		bool makeWing = false;
		int length = 0;
		if (Grid[mainPath[rank].second][mainPath[rank].first].isHub)
		{
			makeWing = true;
			length = 1 + Size / 3;
		}
		else if (rank >= (int)mainPath.Size() / 2 && (rank % 3) == 0)
		{
			makeWing = true;
			length = 1 + (Difficulty >= 4);
		}

		if (makeWing)
			ExpandWing(rank, length, true);
	}

	for (int rank = 0; rank < (int)mainPath.Size(); rank++)
	{
		int rx = mainPath[rank].first;
		int ry = mainPath[rank].second;
		if (Grid[ry][rx].isHub)
		{
			int extra = 2 + (rank >= (int)mainPath.Size() / 2) + (Difficulty >= 4);
		if (routeStyle == 0 || routeStyle == 4) extra += RNG() % 2;
		ExpandShoulders(rx, ry, extra, true);
	}
		else if (Grid[ry][rx].isArena && rank >= (int)mainPath.Size() / 2)
		{
			ExpandShoulders(rx, ry, 1 + Size / 3, true);
		}
	}

	for (unsigned int k = 0; k < placedKeys.Size(); k++)
	{
		MarkMainSpan(placedKeys[k].anchorRank, 1, true, false);
		if (placedKeys[k].anchorRank > 1)
			MarkMainSpan(placedKeys[k].anchorRank - 1, 0, false, true);
		if (placedKeys[k].anchorRank + 1 < (int)mainPath.Size() - 1)
			MarkMainSpan(placedKeys[k].anchorRank + 1, 0, true, false);
		ExpandArena(placedKeys[k].x, placedKeys[k].y, 3 + Size / 2 + (Difficulty >= 3), false);
		ExpandShoulders(placedKeys[k].x, placedKeys[k].y, 4 + Size / 2 + (Difficulty >= 4), false);
		ExpandWing(placedKeys[k].anchorRank, 1 + Size / 3, true);
		if (placedKeys[k].anchorRank > 0)
		{
			int ax = mainPath[placedKeys[k].anchorRank].first;
			int ay = mainPath[placedKeys[k].anchorRank].second;
			ExpandShoulders(ax, ay, 2 + Size / 3, true);
			ExpandSpinePocket(placedKeys[k].anchorRank, 1 + (Difficulty >= 4));
		}
		if (placedKeys[k].anchorRank + 1 < (int)mainPath.Size())
		{
			int bx = mainPath[placedKeys[k].anchorRank + 1].first;
			int by = mainPath[placedKeys[k].anchorRank + 1].second;
			ExpandShoulders(bx, by, 1 + Size / 3, true);
		}
	}

	for (int j = 0; j < H; j++)
	{
		for (int i = 0; i < W; i++)
		{
			if (!keep[j][i]) continue;
			if (Grid[j][i].branchDepth >= 2)
			{
				ExpandShoulders(i, j, 1 + (Difficulty >= 3), false);
			}
			else if (!Grid[j][i].onMainPath && Grid[j][i].branchDepth == 1 &&
				(Grid[j][i].isArena || Grid[j][i].hasKey))
			{
				ExpandShoulders(i, j, 1, false);
			}
			else if (!Grid[j][i].onMainPath && Grid[j][i].hasWeapon)
			{
				ExpandShoulders(i, j, 1 + (Difficulty >= 3), false);
			}
		}
	}

	ExpandArena(ex, ey, 5 + Size / 2 + Difficulty / 2, true);
	ExpandShoulders(ex, ey, 4 + Size / 2, true);
	if (mainPath.Size() > 1)
	{
		int preExitRank = (int)mainPath.Size() - 2;
		ExpandShoulders(mainPath[preExitRank].first, mainPath[preExitRank].second, 2 + Size / 3, true);
		ExpandSpinePocket(preExitRank, 1 + Size / 3);
	}

	const int loopBias = routeLoopBias;
	int targetLoops = clamp(5 + Size / 2 + Difficulty / 2 + loopBias, 3, 13);
	for (int j = 0; j < H && targetLoops > 0; j++)
	{
		for (int i = 0; i < W && targetLoops > 0; i++)
		{
			if (!keep[j][i]) continue;

			for (int d = 0; d < 2 && targetLoops > 0; d++)
			{
				int nd = (d == 0) ? DIR_N : DIR_W;
				int ni = i + DX[nd];
				int nj = j + DY[nd];
				if (!InBounds(ni, nj) || !keep[nj][ni]) continue;
				if (Grid[j][i].conn[nd]) continue;

				bool mainLoop = Grid[j][i].onMainPath && Grid[nj][ni].onMainPath &&
					abs(Grid[j][i].pathRank - Grid[nj][ni].pathRank) >= 2;
				bool branchLoop = (Grid[j][i].branchDepth > 0 && Grid[nj][ni].onMainPath) ||
					(Grid[nj][ni].branchDepth > 0 && Grid[j][i].onMainPath);
				if (!mainLoop && !branchLoop) continue;
				if ((RNG() % 100) >= 60) continue;

				ConnectCells(i, j, ni, nj);
				targetLoops--;
			}
		}
	}

	// Braid widened route pockets together so the map develops alternate
	// lanes and lateral circulation instead of remaining a single-file spine.
	int braidBudget = clamp(5 + Size + Difficulty + braidBonus, 2, 18);
	for (int pass = 0; pass < 2 && braidBudget > 0; pass++)
	{
		for (int j = 0; j < H && braidBudget > 0; j++)
		{
			for (int i = 0; i < W && braidBudget > 0; i++)
			{
				if (!keep[j][i]) continue;
				if (!Grid[j][i].onMainPath && Grid[j][i].branchDepth == 0) continue;

				for (int d = 0; d < 2 && braidBudget > 0; d++)
				{
					int nd = (d == 0) ? DIR_N : DIR_W;
					int ni = i + DX[nd];
					int nj = j + DY[nd];
					if (!InBounds(ni, nj) || !keep[nj][ni]) continue;
					if (Grid[j][i].conn[nd]) continue;

					int rankA = Grid[j][i].pathRank;
					int rankB = Grid[nj][ni].pathRank;
					int branchA = Grid[j][i].branchDepth;
					int branchB = Grid[nj][ni].branchDepth;

					bool sameCluster = abs(rankA - rankB) <= 1;
					bool latePocket = (rankA >= (int)mainPath.Size() / 2 || rankB >= (int)mainPath.Size() / 2) &&
						(branchA == 0 || branchB == 0);
					bool branchPocket = branchA >= 1 && branchB >= 1 && abs(rankA - rankB) <= 2;
					bool hubPocket = Grid[j][i].isHub || Grid[nj][ni].isHub;
					bool arenaPocket = Grid[j][i].isArena || Grid[nj][ni].isArena;

					if (!sameCluster && !latePocket && !branchPocket && !hubPocket && !arenaPocket)
						continue;

					int chance = 45;
					if (hubPocket) chance += 25;
					if (arenaPocket) chance += 15;
					if (latePocket) chance += 10;
					if (branchPocket) chance += 10;
					if ((RNG() % 100) >= chance) continue;

					ConnectCells(i, j, ni, nj);
					braidBudget--;
				}
			}
		}
	}

	for (int j = 0; j < H; j++)
	{
		for (int i = 0; i < W; i++)
		{
			if (!keep[j][i])
			{
				Grid[j][i] = ProcGenCell();
				continue;
			}

			Grid[j][i].present = true;
			Grid[j][i].sectorIdx = -1;
			Grid[j][i].roomId = -1;
			for (int d = 0; d < 4; d++)
			{
				int ni = i + DX[d];
				int nj = j + DY[d];
				if (!InBounds(ni, nj) || !keep[nj][ni])
					Grid[j][i].conn[d] = false;
			}
		}
	}

	for (int j = 0; j < H; j++)
	{
		for (int i = 0; i < W; i++)
		{
			if (!Grid[j][i].present) continue;

			for (int d = 0; d < 4; d++)
			{
				int ni = i + DX[d];
				int nj = j + DY[d];
				if (!InBounds(ni, nj) || !Grid[nj][ni].present)
				{
					Grid[j][i].conn[d] = false;
					continue;
				}
				bool open = Grid[j][i].conn[d] && Grid[nj][ni].conn[OPP[d]];
				Grid[j][i].conn[d] = open;
			}
		}
	}

	Grid[sy][sx].hasPlayerStart = true;
	Grid[ey][ex].hasExit = true;
	Grid[ey][ex].hasBoss = true;

	auto ChooseGateRank = [&](int desired, int minRank, int maxRank) -> int
	{
		int clampedMin = clamp(minRank, 1, (int)mainPath.Size() - 1);
		int clampedMax = clamp(maxRank, clampedMin, (int)mainPath.Size() - 1);
		int bestRank = -1;
		int bestDelta = 100000;
		for (int rank = clampedMin; rank <= clampedMax; rank++)
		{
			if (mainPath[rank].first != mainPath[rank - 1].first)
				continue;
			int delta = abs(rank - desired);
			if (delta < bestDelta)
			{
				bestDelta = delta;
				bestRank = rank;
			}
		}
		return (bestRank >= 0) ? bestRank : clamp(desired, clampedMin, clampedMax);
	};

	TArray<int> gateRanks;
	for (unsigned int k = 0; k < placedKeys.Size(); k++)
	{
		int desired = desiredGates[std::min((int)k, (int)desiredGates.Size() - 1)];
		int minRank = placedKeys[k].anchorRank + 1;
		if (gateRanks.Size() > 0 && minRank <= gateRanks.Last())
			minRank = gateRanks.Last() + 1;
		int maxRank = (k + 1 < placedKeys.Size()) ?
			std::max(minRank, desiredGates[std::min((int)k + 1, (int)desiredGates.Size() - 1)] - 1) :
			(int)mainPath.Size() - 1;
		gateRanks.Push(ChooseGateRank(desired, minRank, maxRank));
	}

	for (unsigned int k = 0; k < gateRanks.Size(); k++)
	{
		int gx = mainPath[gateRanks[k]].first;
		int gy = mainPath[gateRanks[k]].second;
		MarkMainSpan(gateRanks[k], 1, true, true);
		if (gateRanks[k] > 1)
			MarkMainSpan(gateRanks[k] - 1, 0, true, false);
		if (gateRanks[k] + 1 < (int)mainPath.Size() - 1)
			MarkMainSpan(gateRanks[k] + 1, 0, false, true);
		Grid[gy][gx].isArena = true;
		ExpandArena(gx, gy, 1 + Size / 3 + (Difficulty >= 4), true);
		ExpandShoulders(gx, gy, 3 + Size / 2 + (Difficulty >= 3), true);
		ExpandWing(gateRanks[k], 1 + Size / 3, true);
	}

	for (int j = 0; j < H; j++)
	{
		for (int i = 0; i < W; i++)
		{
			if (!Grid[j][i].present) continue;

			Grid[j][i].neighborCount = 0;
			Grid[j][i].connectionCount = 0;
			for (int d = 0; d < 4; d++)
			{
				int ni = i + DX[d];
				int nj = j + DY[d];
				if (InBounds(ni, nj) && Grid[nj][ni].present)
					Grid[j][i].neighborCount++;
				if (Grid[j][i].conn[d])
					Grid[j][i].connectionCount++;
			}

			Grid[j][i].isHub = Grid[j][i].onMainPath && Grid[j][i].connectionCount >= 3;

			for (unsigned int k = 0; k < gateRanks.Size(); k++)
			{
				int regionStart = gateRanks[k];
				bool gateCell = Grid[j][i].pathRank == regionStart &&
					(Grid[j][i].onMainPath || Grid[j][i].isArena);
				bool postGatePocket = (Difficulty >= 4 || Size >= 4) &&
					Grid[j][i].pathRank == regionStart + 1 &&
					Grid[j][i].branchDepth == 0 &&
					Grid[j][i].isArena;
				if (!Grid[j][i].hasPlayerStart && !Grid[j][i].hasExit && !Grid[j][i].hasKey &&
					(gateCell || postGatePocket))
				{
					Grid[j][i].isLocked = true;
					Grid[j][i].lockType = placedKeys[k].keyType;
				}
			}
		}
	}

	MergeRooms(W, H);

	ApplyCoherence(W, H);
	PlaceWeapons(W, H);

	return BuildUDMF(W, H);
}
