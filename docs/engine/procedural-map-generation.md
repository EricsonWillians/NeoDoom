# Procedural Map Generation

> **Living Document** — This page is updated whenever the procedural generator is modified. Last updated: 2026-07-13.

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

### From the main menu

Choose **Procedural Game** from Doom's main menu. The setup screen contains every generation control:

- **Seed** — an exact signed integer. Reusing the same seed and settings rebuilds the same map.
- **Randomize Seed** — chooses and displays a new positive seed without starting immediately.
- **Theme** — Techbase or Hell.
- **Generation Difficulty** — five encounter-pressure bands from Light Resistance to Nightmare.
- **Map Size** — an integer slider from 1 (compact) through 20 (colossal). The largest values intentionally trade generation/load time for very long routes and broad footprints.
- **Generate & Play** — starts `PROCMAP` with the displayed settings.
- **New Random Map** — chooses a new seed and starts it in one action.
- **Restore Defaults** — returns to seed `0`, Techbase, Classic Doom difficulty, and Standard size.

All four settings are archived, so the setup survives a restart. The entry is restored after mod MENUDEF processing, remains present in classic and localized text-only layouts, and oversized replacement main menus scroll with the wheel, arrows, Page Up/Down, Home, and End.

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
| `size` | Map scale and progression depth | `1`–`20` |

---

## Console Commands

### `procmap [seed|random]`

Generates a procedural map using the current CVars and loads it immediately.

- If `seed` is provided, it overrides `procgen_seed` for this invocation. `random` chooses a new positive seed first.
- The actual generation happens inside `P_OpenProceduralMapData()` when the engine loads `PROCMAP`, ensuring a single deterministic generation per map load.

### Menu helper commands

- `procmap_randomize_seed` updates the archived seed without launching a map.
- `procmap_restore_defaults` restores every procedural CVar to its menu default.
- Startup `+procmap` invocations enter the engine's normal autostart path; live menu/console invocations defer a new single-player game on the next tick.

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
| `procgen_size` | `int` | `3` | Map size (1–20). Controls route length, canvas dimensions, keys, branches, landmarks, and encounter budget. Values above 5 are intentionally huge. |

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

Generation is **mission-graph first**. The grid is an embedding surface, not a mandate to fill the map with square rooms. This keeps progression provable while allowing room scale and silhouette to vary.

The design targets were measured from representative maps in `doom.wad` and `doom2.wad`: directional footprints, 10–35% diagonal linedefs, restrained doors, distinct light/height zones, optional dead-end rewards, and encounter counts that grow with the map rather than with every decorative sector.

### 1. Route Embedding

- Canvas dimensions are `W = 8 + 2 × size`, `H = 7 + size`. Difficulty changes landmark cell budgets rather than route length; the finale claims its larger footprint before secondary arenas consume nearby empty cells.
- A randomized DFS spanning tree is created privately as an embedding scaffold.
- The chosen critical path starts near the west edge, favors a distant eastern exit, and targets `9 + 4 × size` cells.
- Only the selected path, planned branches, and landmark footprints become map geometry. The old dense grid carpet is not emitted.

### 2. Mission Graph and Progression

- Sizes 1–2 plan one key, sizes 3–4 plan two, and size 5+ plans three when route length permits.
- Each key occupies a dedicated side branch before its corresponding gate.
- Gates own exactly one directed boundary. A locked room no longer turns every one of its edges into duplicate locked doors.
- Optional branches are distributed along the critical path and avoid touching it away from their anchor.
- Extra loops are added only within the same lock stage, so circulation cannot bypass key progression.

Key order is blue, red, then yellow (`type` 5, 13, and 6). Locked portals use the normal `Door_Raise` action (special 12) plus the appropriate UDMF `locknumber`; this follows the same manual-door path as stock ZDoom maps.

### 3. Landmark and Room Composition

The start, hubs, arenas, key shrines, and exit are expanded into multi-cell landmarks. Arena, shrine, and finale footprints also grow with generation difficulty, providing more lateral movement as projectile pressure and monster mass increase. A bounded room compositor then merges compatible cells according to their role:

- landmarks become broad chambers;
- ordinary main-route cells alternate between short connectors and halls;
- deep branches remain visually denser but retain at least 192-unit chambers and 104-unit open portals;
- locked gate cells remain isolated so their owning boundary cannot disappear inside a merged room.

Room growth prefers compact silhouettes, preserves progression rank, and converts all same-room adjacency to continuous floor space.

Each composed room also receives a deterministic proportion profile. Connectors, longitudinal halls, hubs, locked vestibules, shrines, and arenas vary between 192 and 240 map units per coarse cell while preserving exact same-room joins. Starts use a 224-unit staging footprint, and even compact locks retain 192 units. This breaks up repeated octagonal modules without sacrificing closed topology or forcing combat through narrow modules.

### 4. Visual Coherence

- Four progression zones select stable wall/floor/ceiling families for `techbase` and `hell`.
- Side branches receive related accent palettes instead of unrelated random textures.
- Floor cadence changes in 8–24 unit steps; connected floors are smoothed to Doom's traversable step limit.
- Ceiling height follows room role: compact connectors start at 112 units, general halls at 120, with progressively taller hubs, arenas, and exit chambers.
- Lighting darkens through progression and on deep branches, while starts, keys, hubs, and exits receive readable highlights. Emission clamps every playable sector to at least 160 to prevent accidental black rooms.
- Every coarse chamber has bounded 45-degree corner cuts. This produces a substantial diagonal vocabulary without allowing perimeter shaping to cross into the void or disturb a portal.
- Wall textures center the stock 128-unit motif on every architectural segment and keep a floor-derived vertical offset. Opposite walls, equal doorway shoulders, and all four chamfers therefore use the same phase, while raised floors do not drag surrounding wall rows out of alignment.
- Large landmarks use support-textured corner cuts, role-specific floor pads, ceiling coffers, and small light accents rather than applying detail uniformly to every room.
- Every map exposes the finale and at least one additional combat landmark to `F_SKY1`; the outdoor budget is `2 + size / 2`, so long maps alternate enclosed routes with multiple bright courtyards instead of reserving open air for the exit alone.
- Techbase landmarks use lamps in Doom II and shared tech pillars/columns in Ultimate Doom. Hell landmarks use progression-colored torches, key-colored shrine markers, candelabras for secrets, evil eyes for finales, and torch trees outdoors.
- Solid decorations are corner-biased and rejected when they would overlap an actor or pickup. Sparse corpses can reinforce prior combat without affecting collision.

### 5. Doors and Architectural Detail

- Locked doors are emitted only on their planned gate edge.
- Normal doors have a global budget and at most one door per room pair.
- Reward rooms and deep branches may request doors; random doors are intentionally rare.
- Doors are recessed 16-unit slabs centered inside static jambs. Their 96-unit faces crop stock 128-unit door textures symmetrically. Keyed doors extend `DOORRED`, `DOORBLU`, or `DOORYEL` from the two moving tracks across all four recessed approach borders, making the required key readable from either side.
- Door faces remain pegged to the moving ceiling, while one-sided track walls use `dontpegbottom` and a world-aligned row offset. The slab moves; its tracks never do.
- Multi-cell starts and hubs can receive a centered 8-unit landmark platform. Arenas, key shrines, and exits use two concentric 8-unit tiers, producing a readable 16-unit stair dais instead of an abrupt curb.
- Deep optional branches can terminate in wall-aligned secret doors and real sector-special 9 secret rooms with health, armor, and ammunition rewards.
- Detail stays fully inside one known chamber, preventing feature sectors from leaking into the void around concave rooms.

### 6. Interactive Spaces, Traps, and Height

- Broad rooms can contain inset supply pavilions whose closed tagged door is opened permanently by a real `SW1COMP` or `SW1GARG` wall switch using `Door_Open`. Each stock 64×128 switch is fitted to one centered 64-unit panel, surrounded by ordinary wall shoulders, and scaled vertically to appear exactly once.
- At least one key is surrounded by a once-only walk trigger. Entering its raised shrine pad—or a safe trigger ring when the shrine composed to one cell—opens a nearby colored reveal chamber containing two deaf ranged monsters. Additional key traps are selected randomly from the seed.
- Reveal chambers are real 120–160-unit sectors surrounded by an 18–22-unit void moat and joined through a tagged closed slab; they are not overlapping decorative geometry or invisible blocking walls. Four clipped corners on both loops replace the repeated center box. Width, depth, chamfer, subtle cell offset, and entrance side vary deterministically with room profile and reveal role; the entrance prefers an adjacent cell in the composed room. Host selection still proves at least 40 units of circulation around every straight and diagonal boundary, reserves the feature cell from other authored geometry, and uses a 64-unit door aperture.
- Selected arenas and broad halls contain 48–64-unit raised ranged platforms. A 64-unit-wide directional stair uses 16-unit risers—two intermediate tiers for a 48-unit platform and three for 64—so players and monsters can always reach the high ground. Only the exposed retaining sides use `blockmonsters`; the entry, every riser, and the platform connection remain open.
- Every map contains at least one optional 64-unit lift raised 32 units above its room. All four faces run repeatable `Plat_DownWaitUpStay`, the center carries a visible reward, monsters cannot jam the platform, and a validated 40-unit bypass keeps the main route usable in either lift state.
- The exit uses a bright level-224 `GATE1` pad with four `EXITDOOR` borders inside its open finale courtyard, making the walkover destination visually distinct from ordinary landmark platforms.

### 7. Encounters and Resources

Enemy pressure is calculated once per room from difficulty, progression phase, room role, branch depth, and usable cell count. Starts are safe, ordinary rooms stay bounded, small rooms cap monster tiers, and arenas/key/exit rooms receive explicit encounter budgets. Each room selects a coherent infantry, demon, flying, bruiser, or heavy roster instead of independently mixing every tier; Arch-Viles are excluded from random placement. Ultimate Doom IWADs automatically filter out Doom II-only monsters, while Doom II maps may use the expanded roster. Heavy finale bosses require at least eight merged arena cells; otherwise the finale safely falls back to a smaller boss. The Spider Mastermind is excluded because its 128-unit radius cannot safely occupy the center of the current 256-unit cell geometry.

Weapon progression is guaranteed: the shotgun is placed 32 units directly ahead of the player start, Doom II schedules its exclusive super shotgun before the early chaingun, the rocket launcher appears in the middle on size 2+, the plasma rifle late on size 4+, and an optional BFG branch reward on the largest high-difficulty maps. Ultimate Doom omits the unsupported super shotgun cleanly. Ammunition follows mission phase and guaranteed weapon availability instead of monster tier; difficulty 4–5 fights with at least three enemies always receive ammunition, and four-plus-enemy encounters receive additional ammo and recovery packs.

Critical things snap to the nearest real cell center, so starts, keys, and exits cannot land in the void of a concave room.

---

## Generated Map Structure

### UDMF Output

The generator emits a complete UDMF TEXTMAP with the following sections:

1. **`namespace = "zdoom"`**
2. **Vertices** — Deduplicated chamber, chamfer, corridor, doorway, trigger, and bounded-detail vertices.
3. **Sectors** — One per composed room, plus explicit corridor, closed door, stair-tier, lift, reveal-chamber, raised-perch, and optional secret sectors. Remote doors, perches, and lifts carry unique UDMF IDs.
4. **Sidedefs** — Generated per linedef (front + optional back).
5. **Linedefs** — The emitted forms include:
   - **1-sided boundary walls**: always `blocking = true` with a real `texturemiddle`; each chamber and corridor is a closed polygon.
   - **2-sided open portals**: connect room and corridor sectors, with pegging set for height transitions.
   - **2-sided door portals**: paired faces around a 16-unit slab using `Door_Raise` (12), tag 0, speed 16, delay 150, `playeruse`, and `repeatspecial`; locked variants add `locknumber`.
   - **2-sided stair/platform edges**: coherent 8-unit transitions and light accents inside selected landmarks.
   - **2-sided lift edges**: four usable, repeatable `Plat_DownWaitUpStay` faces around an optional reward platform.
   - **remote activation lines**: one-sided usable switches or two-sided key-pad crossings use `Door_Open` (11) against a tagged reveal slab.
   - **raised-platform edges**: two-sided retaining lines around a split stair opening, plus two or three 16-unit stair tiers whose entire access route is player- and monster-open.
6. **Things** — Player start, staged keys, paced enemies, deaf key-closet ambushers, elevated ranged enemies, weapons/resources, optional boss, and collision-checked theme/role decorations.

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
| `src/common/maps/procgen.cpp` | CVars, console commands, and `P_OpenProceduralMapData()` |
| `src/common/maps/procgen/procgen_core.cpp` | Route embedding, mission graph, key gates, branches, loops, and landmarks |
| `src/common/maps/procgen/procgen_rooms.cpp` | Room composition, visual zones, encounter pacing, and weapon/resource progression |
| `src/common/maps/procgen/procgen_udmf.cpp` | Closed chamber/corridor architecture, functional doors, shaped perimeter, UDMF geometry, and thing emission |
| `src/common/maps/procgen/procgen_internal.h` | Shared grid directions plus enemy and item tables |
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
    void SetSize(int size);              // 1-20
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
- **Closed geometry**: every chamber and connection sector owns a complete clockwise boundary. Adjacent but unconnected chambers retain separate textured one-sided walls with a void gap; the generator never uses a blocking two-sided line as a fake solid wall.
- **Manual doors**: `arg0 = 0` makes `Door_Raise` operate on the linedef's back sector. Portal winding therefore places the room on the front and the initially closed door sector on the back.
- **Remote doors**: switch and key triggers use `Door_Open` with a nonzero sector ID. Their reveal slabs start closed, while the shaped inset chamber beyond remains a valid connected sector containing rewards or deaf ambushers. Key traps use a more intimate footprint; switch caches receive additional floor area.
- **Raised-platform edges**: `blockmonsters` is intentionally limited to exposed retaining sides and never appears on the stair route. It prevents sideways AI drops without intercepting player movement, hitscan, or projectile fire, while the 16-unit tiers make the high area reachable from the room floor.
- **Lift edges**: special 62 targets a unique 3000–3999 sector ID from every face. Lifts are never the only route through a room and retain a full bypass while raised.
- **Door pegging**: stock Doom door tracks are one-sided middle textures with flags `blocking + dontpegbottom` (17). Generated tracks reproduce that contract; door faces deliberately omit `dontpegtop` so they rise with the ceiling.
- **Texture alignment**: every ordinary one-sided wall centers a 128-unit horizontal phase from its segment length and derives `offsety` from its sector floor. Switches use a separate exact-fit contract: one 64-unit panel, zero origin offsets, horizontal scale 1, and `scaley_mid = 128 / wallHeight`.

---

## Testing

### `test_procgen.sh`

```bash
# Run the representative structural validation matrix (default)
./test_procgen.sh validate

# Confirm identical inputs are byte-identical and a new seed differs
./test_procgen.sh determinism

# Verify the packed main-menu entry, every setup control, persistence, and launch action
./test_procgen.sh menu

# Verify monotonic difficulty pressure, strictly growing finale area, and resources
./test_procgen.sh balance

# Verify Ultimate Doom actor/texture compatibility and perform a real runtime load
./test_procgen.sh doom1

# Enter PROCMAP through the runtime map loader and node builder
./test_procgen.sh load

# Stress 11 seeds while rotating themes, sizes, difficulty bands, and a compact allocation edge
./test_procgen.sh seeds

# Inspect a specific seed (shows lock/key/exit lines)
./test_procgen.sh inspect 42

# Sample compact through colossal size settings
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

- Exactly one player start and exit trigger.
- One to three keys (`type = 5`, `13`, or `6`, depending on size).
- Exactly two lock linedefs per key (the two faces of one planned gate boundary).
- Sector and thing counts remain within size-scaled budgets.
- Exit trigger present (`special = 243`).
- No `texturemiddle = "-"` on 1-sided walls.
- Every 1-sided line is blocking; no 2-sided line masquerades as a solid wall.
- Every door sector starts closed and every door face uses `Door_Raise` with use/repeat activation and valid arguments.
- Every door has exactly two centered faces separated by 16 units, vertically fitted non-repeating face textures, and two bottom-pegged track walls; keyed track colors must match the lock.
- Every present key color appears on at least six door-border segments, not only on the two narrow moving tracks.
- At least one usable `Door_Open` switch targets a real closed sector ID, and at least one key-pad crossing targets a separate ambush door containing two deaf ranged monsters. Each switch appears once on an exact 64-unit panel with no horizontal or vertical repetition.
- Every reveal door is 64 units wide and its inset footprint retains at least 40 units of circulation clearance on all four sides and chamfered corners.
- Every reveal consists of two bounded clipped-corner loops with exactly four diagonal edges each, a valid 120–160-unit footprint, and an 18–22-unit moat. Maps with multiple reveals must emit multiple footprint sizes; maps with three or more must vary the entrance axis.
- Every key pavilion contains exactly two wall-clear ambushers, and every switch pavilion retains its ammunition/health cache after shaping.
- At least one ranged platform stands 48 or 64 units above its surrounding room, contains a ranged enemy, and descends to the room through an exact sequence of 16-unit tiers with no monster-blocked access edge.
- At least one 32-unit lift has four valid use/repeat faces, 64 units of raised-state headroom, a reward, and at least 40 units of bypass clearance.
- Ordinary two-sided traversal—including every raised-platform access route—retains at least 56 units of headroom and no floor discontinuity above 24 units; intentional retaining sides, closed doors, and operable lifts are checked separately.
- The exit trigger belongs to a `GATE1` sector with four `EXITDOOR` borders and two complete 8-unit stair tiers, and every map contains at least two open-sky sectors.
- The start shotgun is within 40 units and in front of the player, random Arch-Viles are forbidden, and monster/ammo/health/weapon budgets remain within size-scaled bounds.
- Every map contains role-aware decorative things; solid props remain clear of gameplay actors and pickups, and Ultimate Doom never receives Doom II-only lamp sprites.
- Sky landmarks must span at least 400 units on one axis at outdoor light levels; Hell additionally proves its evil-eye finale, torch-tree courtyard, and key-color shrine markers, while techbases prove their IWAD-safe lamp or pillar vocabulary.
- At least one real secret reward sector and wall-aligned secret door are generated.
- At least one `F_SKY1` sector exists, no sector is darker than 160, and the emitted sector graph is connected.
- Diagonal linedefs remain substantial enough to keep the chamber silhouette from regressing to a pure square grid.
- Standard and larger maps must retain broad wall/floor/ceiling texture diversity across independently sized and shaped rooms.
- Repeating the same seed/theme/difficulty/size produces byte-identical UDMF.
- A fixed-size difficulty sweep must increase the emitted finale-room floor area at every step; the Nightmare reference arena must exceed 500,000 map-unit².
- Timed headless `+map PROCMAP` runs for small, medium, and large maps in both themes reach `PROCMAP - Unnamed` and report no map or node-builder errors (`./test_procgen.sh load`).

---

## Changelog

### 2026-07-13 — Reachable High Ground and Wider Rooms

- Raised the minimum compact chamber footprint from 160×176 to 192×192 units, widened starts to 224×224 and deep-branch portals from 88 to 104 units, and increased ordinary vertical clearance to at least 112 units.
- Replaced unreachable 48–64-unit ranged ledges with directional 64-unit stair approaches using exact 16-unit risers.
- Kept only exposed retaining sides monster-blocking; the entry, risers, and platform connection now remain open to both player and monster traversal.
- Extended serialized validation to reconstruct each platform route and prove floor-level reachability, tier cadence, headroom, and an unblocked monster path.

### 2026-07-13 — Room-Scaled Reveal Pavilions

- Replaced the repeated centered rectangular reveal box with clipped-corner outer and inner loops.
- Added deterministic width, depth, chamfer, moat, and bounded off-center variation across 120–160-unit footprints.
- Oriented each entrance toward open composed-room space where possible and rotated actors/rewards with the chosen approach.
- Gave switch caches a larger spatial role than key ambush pavilions while retaining 64-unit doors and 40-unit exterior clearance.
- Extended serialized tests to reconstruct each feature loop, prove four diagonals per boundary, require size/orientation diversity, and validate shaped-interior actor clearance and rewards.

### 2026-07-13 — Traversal-Safe Reveals, Fitted Switches, Stairs, and Lifts

- Restricted reveal chambers to broad host cells with a serialized 40-unit circulation ring and widened their doors from 48 to 64 units.
- Rebuilt switches as centered 64×128 single-copy panels using `SW1COMP`/`SW1GARG` with explicit middle-texture scaling and ordinary wall shoulders.
- Replaced major 16-unit landmark curbs with two coherent 8-unit stair tiers.
- Added optional four-sided 32-unit reward lifts with repeatable use activation, monster-jam protection, and a permanent bypass route.
- Added structural checks for ordinary traversal headroom/step height, reveal clearance, exact switch fitting, stair completeness, and lift geometry/action semantics.

### 2026-07-13 — Interactive Reveals, Vertical Combat, and Exit Language

- Added tagged switch-operated supply chambers with theme-correct stock switch panels.
- Added deterministic-random key ambushes whose shrine crossings reveal two deaf ranged monsters in a nearby real closet sector.
- Added raised, monster-blocking sniper perches to open arenas and broad halls.
- Increased the open-area cadence, rebuilt the exit as a bright `GATE1`/`EXITDOOR` landmark, and extended key colors across recessed door borders.
- Added serialized-topology checks for remote targets, switch and key activations, ambushers, perch height/occupancy, open-sky count, exit materials, and keyed border coverage.

### 2026-07-13 — Symmetric Surfaces, Arena Safety, and Colossal Maps

- Replaced world-position wall phases with segment-centered phases, keeping opposite walls, doorway shoulders, and chamfer accents visually symmetric.
- Expanded hubs and combat landmarks with difficulty, reduced ordinary-room pressure, capped heavy tiers in small rooms, and kept boss support encounters bounded.
- Required at least eight merged cells before selecting a Cyberdemon and removed the physically incompatible Spider Mastermind from generated finales.
- Replaced the five-value map-size menu with a 1–20 slider and extended generation to colossal routes and canvases.
- Added regression checks for symmetric wall offsets, heavyweight boss clearance, and representative sizes through 20.

### 2026-07-10 — Scrollable Mod Menus and Room-Variation Pass

- Added viewport scrolling, mouse-wheel navigation, Page Up/Down, and Home/End support to list menus so expanded mod main menus keep every action reachable.
- Added eight per-room dimension profiles, five chamfer profiles, expanded multi-cell room targets, varied clear heights, and semantic accent materials.
- Expanded both themes from one surface per progression phase to four wall, floor, and ceiling alternatives per phase using Doom/Ultimate Doom-safe assets.
- Fitted tall door faces vertically with UDMF `scaley_top`, preventing stock 128-unit door art from tiling up high openings.
- Reduced ordinary, arena, key, locked, exit, and boss support encounters; delayed heavy monster tiers and increased large-ammo and recovery support around major fights.
- Added regression checks for scroll support, surface diversity, centered variable-width doors, and non-repeating tall-door scaling.

### 2026-07-10 — Main-Menu Integration

- Added a dedicated Procedural Game entry to both Doom main-menu layouts.
- Added persistent seed editing/randomization, theme, generation difficulty, and map-size controls.
- Added deterministic replay, one-action random generation, and defaults restoration actions.
- Added a direct single-player launch path that clears the menu stack and defers `PROCMAP` as a new game.
- Added packed-MENUDEF, persistence, randomization, and real map-entry regression coverage.

### 2026-07-10 — Architecture, Alignment, Door, and Balance Pass

- Rebuilt doors as recessed 16-unit slabs with static jambs, centered full-size faces, keyed track strips, and immobile bottom-pegged tracks.
- Added world-aligned wall offsets, floor-aligned row offsets, role-aware room proportions, landmark pads/coffers, and support-framed chamfers.
- Added coherent per-room encounter families, real finale bosses, immediate start weapon agency, phase-aware ammunition, and encounter-scaled recovery packs.
- Added a real Doom II super-shotgun stage plus Ultimate Doom-safe monster, finale-boss, weapon, and prop fallbacks.
- Added deterministic secret reward rooms and wall-aligned secret doors on optional branches.
- Added IWAD-aware tech props plus Hell torch/key/finale/outdoor decoration semiotics with collision-safe placement.
- Expanded validation to prove door depth/face/track semantics, keyed track textures, texture-coordinate alignment, immediate shotgun placement, secret presence, and resource budgets.

### 2026-07-10 — Closed-Geometry and Doom-Language Rewrite

- Replaced dense grid filling with a directional critical path and explicit optional branches.
- Added staged blue/red/yellow key progression with non-bypassable, single-edge gates.
- Added lock-stage-aware loops, landmark expansion, and a bounded role-aware room compositor.
- Added coherent four-zone texture, light, floor, and ceiling progression for tech and hell themes.
- Rebalanced encounter/resource scaling and guaranteed useful weapon progression.
- Replaced ambiguous shared-grid walls with inset, closed chamber polygons and explicit closed corridor/door sectors.
- Replaced incorrect polyobject/door specials with working `Door_Raise` portals, explicit use/repeat activation, closed starting sectors, and UDMF locks.
- Added guaranteed sky landmarks, a readable 160 light floor, bounded 45-degree chamfers, restrained landmark platforms, and safe critical-thing placement.
- Expanded `test_procgen.sh` with topology, wall solidity, door semantics, sky/light, diagonal-shaping, determinism, and real runtime-load checks.

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
