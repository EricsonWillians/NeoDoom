# Procedural Map Generation

> **Living Document** — This page is updated whenever the procedural generator is modified. Last updated: 2025-06-05.

BiasedDoom includes a runtime procedural dungeon generator that synthesizes complete UDMF maps in memory. Maps are generated on demand when the engine loads the special map name `PROCMAP` (or any name starting with `PROC`). No WAD/PK3 editing is required.

## Table of Contents

- [Quick Start](#quick-start)
- [Console Commands](#console-commands)
- [CVars](#cvars)
- [ZScript API](#zscript-api)
- [Algorithm Overview](#algorithm-overview)
- [Generated Map Structure](#generated-map-structure)
- [Architecture & Source Files](#architecture--source-files)
- [Testing](#testing)
- [Changelog](#changelog)

---

## Quick Start

### From the console (in-game)

Open the console (default key is `` ` ``) and type:

```
procmap          // generate and load using current CVars
map PROCMAP      // load PROCMAP; CVars control generation
```

### From the Linux terminal

All examples assume your binary is at `./build/biaseddoom` and IWAD is at `~/.config/biaseddoom/doom2.wad`. Adjust paths as needed.

```bash
# --- Basic: load with default CVars ---
./build/biaseddoom -iwad ~/.config/biaseddoom/doom2.wad +procmap

# --- Set CVars on the command line, then load ---
./build/biaseddoom -iwad ~/.config/biaseddoom/doom2.wad \
    +procgen_seed 42 \
    +procgen_theme hell \
    +procgen_difficulty 5 \
    +procgen_size 4 \
    +procmap

# --- Same thing, shorter (procmap accepts seed override) ---
./build/biaseddoom -iwad ~/.config/biaseddoom/doom2.wad \
    +procgen_theme hell \
    +procgen_difficulty 5 \
    +procgen_size 4 \
    +procmap 42

# --- Use map command directly (CVars must be set first) ---
./build/biaseddoom -iwad ~/.config/biaseddoom/doom2.wad \
    +procgen_seed 12345 \
    +procgen_theme techbase \
    +procgen_difficulty 3 \
    +procgen_size 3 \
    +map PROCMAP

# --- Dump UDMF to disk for inspection (no GUI needed) ---
./build/biaseddoom -iwad ~/.config/biaseddoom/doom2.wad \
    +dumpprocudmf 42 techbase 3 4 \
    +quit

# --- Full autonomous headless run (no sound, no GUI) ---
./build/biaseddoom -nosound -nomusic -nogui \
    -iwad ~/.config/biaseddoom/doom2.wad \
    +dumpprocudmf 99 hell 5 5 \
    +quit

# --- Batch: generate 10 maps with different seeds ---
for seed in {1..10}; do
    ./build/biaseddoom -nosound -nomusic -nogui \
        -iwad ~/.config/biaseddoom/doom2.wad \
        +dumpprocudmf "$seed" techbase 3 3 \
        +quit >/dev/null 2>&1
    cp /tmp/procmap_test.udmf "/tmp/procmap_seed_${seed}.udmf"
    echo "Generated /tmp/procmap_seed_${seed}.udmf"
done
```

### Parameters

| Parameter | Meaning | Range |
|-----------|---------|-------|
| `seed` | RNG seed for deterministic generation | any `int` |
| `theme` | Visual theme | `techbase`, `hell`, or default |
| `difficulty` | Enemy/item density | `1`–`5` |
| `size` | Grid dimensions | `1`–`5` |

---

## Console Commands

### `procmap [seed]`

Generates a procedural map using the current CVars and loads it immediately.

- If `seed` is provided, it overrides `procgen_seed` for this invocation.
- The actual generation happens inside `P_OpenProceduralMapData()` when the engine loads `PROCMAP`, ensuring a single deterministic generation per map load.

### `dumpprocudmf <seed> [theme] [difficulty] [size]`

Generates a map and writes the raw UDMF TEXTMAP to `/tmp/procmap_test.udmf`. Useful for debugging and inspection.

Example:
```
dumpprocudmf 42 hell 5 5
```

---

## CVars

All CVars are archived (`CVAR_ARCHIVE`), so they persist across sessions.

| CVar | Type | Default | Description |
|------|------|---------|-------------|
| `procgen_seed` | `int` | `0` | RNG seed. Same seed + same parameters = identical map. |
| `procgen_theme` | `string` | `"techbase"` | Visual theme. `"techbase"` or `"hell"`. |
| `procgen_difficulty` | `int` | `3` | Difficulty level (1–5). Affects enemy count, enemy tiers, and boss selection. |
| `procgen_size` | `int` | `3` | Map size (1–5). Grid dimensions are `3 + size` in both axes. |

### Setting CVars

In the console:

```
procgen_seed 42
procgen_theme hell
procgen_difficulty 5
procgen_size 4
```

From the Linux terminal (prepend `+` to each CVar):

```bash
./build/biaseddoom -iwad ~/.config/biaseddoom/doom2.wad \
    +procgen_seed 42 \
    +procgen_theme hell \
    +procgen_difficulty 5 \
    +procgen_size 4
```

**Important:** CVars are archived (`CVAR_ARCHIVE`), so once you set them they persist across game restarts. To reset to defaults:

```
reset procgen_seed
reset procgen_theme
reset procgen_difficulty
reset procgen_size
```

---

## ZScript API

Mods can trigger procedural generation from ZScript via the `ProceduralMapGenerator` class.

```zscript
class MyEventHandler : EventHandler
{
    override void WorldLoaded(WorldEvent e)
    {
        // Configure and generate
        ProceduralMapGenerator.SetSeed(12345);
        ProceduralMapGenerator.SetTheme("techbase");
        ProceduralMapGenerator.SetDifficulty(3);
        ProceduralMapGenerator.SetSize(4);

        int ok = ProceduralMapGenerator.Generate();
        if (!ok)
        {
            console.printf("Generation failed: %s", ProceduralMapGenerator.GetLastError());
        }
    }
}
```

### `GenerateAndLoad` (convenience)

```zscript
int ok = ProceduralMapGenerator.GenerateAndLoad(
    42,      // seed
    "hell",  // theme
    5,       // difficulty
    4        // size
);
```

Returns `1` on success, `0` on failure.

---

## Algorithm Overview

The generator builds a grid-based dungeon using a **randomized DFS spanning tree** with controlled loop injection.

### 1. Grid Initialization

- Grid size: `W × H` where `W = H = 3 + size` (range 4–8).
- Each cell can be present/absent and has 4 directional connection flags (N, S, W, E).

### 2. Spanning Tree (Randomized DFS)

Starting from the center cell, the algorithm performs a depth-first walk, carving passages to unvisited neighbors. This guarantees:
- Every present cell is reachable from the start.
- No cycles (yet).
- Natural dead ends for key placement.

### 3. Loop Injection (~12.5%)

After the spanning tree is complete, the algorithm computes BFS distances from the start. Then it considers potential extra connections between cells that:
- Are already present (adjacent in the grid).
- Are **not** already connected.
- Have BFS distances that differ by **≥ 2** (ensuring loops connect distant branches, not adjacent rooms).

Each qualifying candidate has a `1/8` chance of being opened.

### 4. Theming & Height Variance

- **Themes**: `techbase`, `hell`, or generic fallback. Each theme has its own floor, ceiling, and wall texture pools.
- **Height variance**: ~17% of rooms get altered floor/ceiling heights. Linedefs spanning height differences receive `dontpegtop` / `dontpegbottom` flags so textures align correctly.
- **Liquid pits**: ~10% chance for a non-start/non-exit/non-key room to become a hazard pit (`NUKAGE1` or `LAVA1`).

### 5. Key-Door Progression

1. **Dead-end detection**: All cells with exactly 1 connection (excluding start/exit) are collected.
2. **Shuffle**: Dead ends are shuffled deterministically via the RNG.
3. **Key placement**: The first dead end receives a key (currently always red, ednum `7`).
4. **Exit lock**: The exit room is locked with `locknumber = 1` and linedef `special = 11` (door locked by red key).

This creates a simple but reliable single-key hunt: explore, find the key in a dead end, return to unlock the exit.

### 6. Entity Placement

| Entity | Placement Rule |
|--------|---------------|
| **Player start** | Center cell (deterministic). |
| **Exit trigger** | Walkover linedef (`special = 243`) in the exit room. |
| **Boss** | Exit room, selected by difficulty tier. |
| **Key** | First shuffled dead-end cell. |
| **Enemies** | Per-room count based on difficulty + RNG jitter. |
| **Items** | ~20% chance per non-special room. |

---

## Generated Map Structure

### UDMF Output

The generator emits a complete UDMF TEXTMAP with the following sections:

1. **`namespace = "zdoom"`**
2. **Vertices** — `(W+1) × (H+1)` grid points.
3. **Sectors** — One per present cell, with floor/ceiling Z, textures, and light.
4. **Sidedefs** — Generated per linedef (front + optional back).
5. **Linedefs** — Three kinds:
   - **1-sided border walls**: `blocking = true`, `texturemiddle` = wall texture, `texturetop`/`texturebottom` = `"-"`.
   - **2-sided doorways**: `twosided = true`, optional `locknumber`, peg flags when heights differ.
   - **2-sided solid internal walls**: `twosided = true`, `blocking = true` (for adjacent present cells with no connection).
6. **Things** — Player start, enemies, items, keys, boss.

### Winding Order

Front sidedefs always face **into** the sector they bound:

| Edge Type | Vector | Front Faces |
|-----------|--------|-------------|
| Horizontal, below sector | `v2 → v1` (west) | North |
| Horizontal, above sector | `v1 → v2` (east) | South |
| Vertical, left sector | `v1 → v2` (north) | West |
| Vertical, right sector | `v2 → v1` (south) | East |

This ensures the Doom renderer never sees reversed or void-facing walls.

---

## Architecture & Source Files

| File | Purpose |
|------|---------|
| `src/common/maps/procgen.h` | `FProceduralMapGenerator` class declaration, `ProcGenCell` struct |
| `src/common/maps/procgen.cpp` | Core generator: grid algorithm, UDMF builder, CVars, console commands, `P_OpenProceduralMapData()` |
| `src/playsim/procgen_zscript.cpp` | Native ZScript function bindings (`DEFINE_ACTION_FUNCTION_NATIVE`) |
| `wadsrc/static/zscript/procgen/procgen.zs` | ZScript API class declaration |
| `src/p_openmap.cpp` | Map loading hook: `P_OpenMapData()` calls `P_OpenProceduralMapData()` for `PROCMAP` |
| `test_procgen.sh` | Automated validation script |

### Key Classes & Functions

```cpp
// Singleton generator
class FProceduralMapGenerator {
    void SetSeed(int seed);
    void SetTheme(const char* theme);
    void SetDifficulty(int difficulty);  // 1-5
    void SetSize(int size);              // 1-5
    bool Generate();                     // builds grid + UDMF
    const FString& GetUDMFText() const;
    const char* GetLastError() const;
    static FProceduralMapGenerator& GetInstance();
};

// Map loading hooks
MapData* P_OpenProceduralMapData(const char* mapname);
bool P_IsProceduralMapName(const char* mapname);
```

### Important Implementation Notes

- **Double-generation bug fix**: `P_OpenProceduralMapData()` re-seeds from CVars *before* calling `Generate()`, ensuring deterministic output even if a previous `Generate()` call advanced the RNG.
- **1-sided walls**: `texturemiddle` must be a real wall texture. Setting it to `"-"` creates invisible but solid walls (HOM).
- **Internal solid walls**: Adjacent present cells without a connection become 2-sided `blocking` linedefs so both sectors have valid geometry.

---

## Testing

### `test_procgen.sh`

```bash
# Test 10 different seeds
./test_procgen.sh seeds

# Inspect a specific seed (shows lock/key/exit lines)
./test_procgen.sh inspect 42

# Test all size settings
./test_procgen.sh size

# Show first 100 lines of last UDMF
./test_procgen.sh udmf
```

### Manual verification

```bash
# Build
./supreme-build.sh

# Dump and inspect
./build/biaseddoom -iwad doom2.wad +dumpprocudmf 42 techbase 3 4 +quit
head -50 /tmp/procmap_test.udmf
```

### What to verify

- Sectors count ≈ `W × H` (not all cells are present, but most are).
- Exactly 1 key thing (`type = 7` for red).
- At least 1 lock (`locknumber = 1`).
- Exit trigger present (`special = 243`).
- Player start present (`type = 1`).
- No `texturemiddle = "-"` on 1-sided walls.

---

## Changelog

### 2025-06-05 — Complete UDMF Rewrite

- **Fixed critical rendering bugs**:
  - 1-sided walls now use real `texturemiddle` (was `"-"`, causing HOM).
  - Correct winding order for all perimeter and internal walls.
  - Internal solid walls between unconnected adjacent cells now emit 2-sided `blocking` linedefs.
- **Implemented advanced features**:
  - Height variance with `dontpegtop` / `dontpegbottom`.
  - Liquid pit rooms (`NUKAGE1` / `LAVA1`).
  - Loop injection preferentially between distant DFS branches (BFS distance ≥ 2).
  - Texture offsets (`offsetx` / `offsety`) on sidedefs.
- **Key-card progression**: Single-key hunt (red key in dead end, locks exit room).
- **Double-generation bug fix**: `P_OpenProceduralMapData()` re-seeds from CVars before generation.

### 2025-06-04 — Initial Implementation

- Randomized DFS grid generation.
- Spanning tree + loop injection.
- Basic UDMF output with sectors, linedefs, sidedefs, things.
- Console commands: `procmap`, `dumpprocudmf`.
- CVars: `procgen_seed`, `procgen_theme`, `procgen_difficulty`, `procgen_size`.
- ZScript API: `ProceduralMapGenerator` class.
- Map loading hook: `PROCMAP` intercepted in `P_OpenMapData()`.
