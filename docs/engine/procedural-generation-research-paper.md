# Mission-Graph-First Procedural Level Synthesis for Doom

## A deterministic, progression-safe, runtime UDMF generator in BiasedDoom 4.15.3

**BiasedDoom contributors**

**Implementation paper — July 2026**

## Abstract

BiasedDoom generates complete, playable Doom levels at runtime without selecting
from prefabricated maps. The generator accepts a seed, a visual theme, a
difficulty level, and a size level; constructs a directed critical path with
optional branches, staged keys, locks, loops, hubs, arenas, secrets, and an
exit; embeds that mission graph on a bounded grid; composes adjacent cells into
rooms; assigns geometry, materials, lighting, encounters, weapons, and recovery
resources; and serializes the result as an in-memory UDMF `TEXTMAP`. The normal
engine map loader and node builder then consume that text exactly as they would
consume a map stored in a WAD or PK3.

The central design choice is to separate *progression topology* from *physical
geometry*. A randomized spanning tree is used only as an embedding substrate.
The emitted map is a deliberately selected subset whose critical path is known
before doors or rooms exist. Keys are placed on optional limbs before their
corresponding gate ranks, and circulation loops may connect only rooms in the
same lock stage. This makes progression safety a construction property rather
than a post-generation repair problem.

The current implementation also treats Doom geometry as a set of explicit
topological contracts. Every exposed wall is a blocking, one-sided linedef with
a real middle texture; traversable joins use explicit two-sided portals;
functional doors are closed 16-unit sectors with two `Door_Raise` faces and
static tracks; and sector, sidedef, and linedef references are validated after
serialization. Room-level visual coherence is produced by progression-aware
four-zone palettes, eight dimension profiles, five corner profiles, semantic
landmarks, varied vertical clearances, and role-aware decoration. Encounter
pressure remains bounded per room, while guaranteed weapon milestones and
resource budgets preserve player agency. Tagged reveal sectors add usable wall
switches and key-triggered ambushes, while raised monster-blocking perches add
vertical ranged pressure without obstructing player fire. Reveal hosts prove a
40-unit circulation ring, stock switch art is fitted exactly once, major
landmarks use 8-unit stair tiers, and optional four-sided lifts add operable
vertical motion without becoming mandatory route gates.

The representative validation matrix spans both themes, all difficulty bands,
Ultimate Doom and Doom II actor vocabularies, and compact through colossal map
sizes. It verifies exactly two locked faces per key, bounded encounter/resource
budgets, heavyweight boss clearance, connected geometry, and real runtime map
loading. Repeated generation of the same input produces byte-identical UDMF.

## 1. Problem statement

A Doom level generator must satisfy several concerns that are easy to conflate:

1. **Progression:** the exit must be reachable, every required key must be
   obtainable before its lock, and optional loops must not bypass gates.
2. **Embedding:** logical rooms and branches must occupy non-overlapping space
   within a practical map footprint.
3. **Geometry:** sector boundaries must have correct winding, valid references,
   closed perimeters, and renderer-safe textures.
4. **Playability:** height transitions, door behavior, actor clearance, and
   starts/exits must obey engine rules.
5. **Pacing:** encounter strength, weapon availability, ammunition, health, and
   armor must evolve coherently across the map.
6. **Authorship:** rooms need recognizable roles and visual variety rather than
   looking like a uniformly decorated maze.
7. **Reproducibility:** a seed must reproduce the same map for debugging,
   sharing, and regression testing.
8. **Compatibility:** output must use the correct actor vocabulary for both
   Ultimate Doom and Doom II and remain usable with normal mod loading.

A generator that begins by filling a grid and only later attempts to infer a
mission structure makes these properties mutually fragile. A newly inserted
door can make the exit impossible; a random loop can make a key irrelevant; a
merged sector can erase a wall boundary; and an encounter randomizer can place
heavy monsters before an appropriate weapon. BiasedDoom instead establishes a
sequence of representations in which each pass owns a smaller, explicit set of
decisions.

## 2. Contributions

The implementation makes the following concrete contributions:

- A mission-graph-first pipeline whose key/lock ordering is guaranteed by
  construction and whose loops are constrained by lock stage.
- A bounded grid embedding that produces broad, directional maps but retains
  deterministic branch and landmark placement.
- A semantic room compositor that merges cells according to role,
  progression, branch depth, and connectivity rather than proximity alone.
- A four-phase visual grammar with independent theme palettes, deterministic
  per-room variation, variable room profiles, readable light bounds, outdoor
  landmarks, and semantic props.
- Explicit, closed UDMF geometry with deduplicated vertices, correct sidedef
  winding, functional recessed doors, fitted door art, aligned wall textures,
  and collision-aware thing placement.
- Interactive tagged geometry comprising usable switch caches, key-platform
  ambush reveals, projectile-transparent ranged perches, strongly keyed door
  borders, fitted single-copy switch panels, traversal-safe inset clearances,
  tiered stair landmarks, bypassable reward lifts, and an unmistakable exit pad.
- A bounded encounter/economy model with guaranteed weapon milestones,
  phase-aware ammunition, major-fight recovery, IWAD-aware actor tables, and
  difficulty monotonicity tests.
- An in-memory map-loading bridge and native ZScript interface that require no
  temporary WAD and no separate executable.
- A validation suite that examines the serialized artifact and real runtime
  loading rather than relying only on internal generator assertions.

## 3. System interface and generation contract

The generator is a process-wide `FProceduralMapGenerator` singleton. Its public
configuration is intentionally small:

| Input | Domain | Meaning |
|---|---:|---|
| `seed` | signed integer, consumed as 32 bits | deterministic random stream |
| `theme` | `techbase` or `hell` | material and decoration vocabulary |
| `difficulty` | 1–5 | encounter count, monster tier, boss policy |
| `size` | 1–20 | canvas size, route target, branches, keys, landmarks, weapon milestones |

`SetDifficulty` and `SetSize` clamp out-of-range values. The archived CVars
`procgen_seed`, `procgen_theme`, `procgen_difficulty`, and `procgen_size` expose
the contract to the console and menu. `procmap` starts a single-player game on
the virtual map name `PROCMAP`; `dumpprocudmf` serializes the same result to
`/tmp/procmap_test.udmf` for inspection.

The map-loading boundary is important. `P_OpenMapData` asks
`P_OpenProceduralMapData` to handle names equal to `PROCMAP` or beginning with
`PROC`. Immediately before generation, the factory re-applies all four CVars
and re-seeds the generator. The resulting string is installed as the
`ML_TEXTMAP` lump of a newly allocated textual `MapData`. The rest of the
engine—including parsing, node construction, sector creation, and actor
spawning—uses the ordinary map path.

This boundary gives the generator the following observable contract:

```text
(seed, theme, difficulty, size, IWAD family, engine build)
                         -> one complete UDMF TEXTMAP
```

The IWAD family is part of the effective input because Ultimate Doom and Doom
II have different actors. Reproducibility is guaranteed within the same engine
implementation and game-data context; it is not promised across generator
algorithm revisions.

## 4. Pipeline overview

```text
Configuration and seed
         |
         v
Randomized spanning-tree substrate
         |
         v
Critical path + key limbs + side limbs + lock edges
         |
         v
Landmark expansion + same-stage circulation loops
         |
         v
Semantic cell-to-room composition
         |
         v
Room graph analysis + materials + dimensions + lighting
         |
         v
Encounters + weapon milestones + recovery + secrets
         |
         v
Closed chambers + corridors + doors + things
         |
         v
UDMF serialization -> MapData -> normal node builder/runtime
```

Each arrow represents a loss of freedom. Once a lock edge is planned, later
passes may decorate or geometrically realize it but may not move it to a
different progression stage. Once cells are composed into a room, later passes
may vary that room's silhouette but may not merge it through an unrelated lock.
This monotonic refinement sharply reduces the number of global repairs needed.

## 5. Deterministic random process

The generator owns an `FRandom`, which derives from the engine's SFMT random
implementation. `SetSeed` initializes it from the requested 32-bit seed.
Parameterless calls return the low eight bits of a newly generated 32-bit value;
modular selection uses `GenRand32() % n`. No wall-clock value is consulted by
the generation passes. Wall-clock entropy appears only in the explicit
“Randomize Seed” user action, which writes a new seed before generation.

Determinism depends on a stable order of decisions. The implementation therefore
uses row-major grid scans, ordered room arrays, fixed direction arrays, and
deterministic tie-breaking with the seeded stream. It does not iterate an
unordered hash container to make generation decisions. The map loader also
re-seeds immediately before `Generate`, preventing an earlier diagnostic or
ZScript call from advancing the stream used for the actual loaded map.

The determinism regression performs three generations:

```text
H(seed = 424242) == H(seed = 424242)
H(seed = 424242) != H(seed = 424243)
```

where `H` is SHA-256 over the complete emitted UDMF byte sequence. The first
equality proves repeatability for the reference case; the inequality prevents a
degenerate test in which the seed is ignored.

## 6. Mission graph construction

### 6.1 Canvas

For size `S` in `[1,20]`, the coarse canvas is

```text
W = 8 + 2S cells
H = 7 + S  cells.
```

and each cell is 256 map units wide. The outer one-cell frame is never used, so
the logical working set is `(W - 2)(H - 2)`. The rectangular aspect ratio and
eastward bias favor broad, directional footprints rather than a uniformly
dense square carpet. Difficulty changes landmark budgets, not canvas dimensions
or target route length; the finale reserves its footprint before secondary
arenas consume nearby empty cells.

### 6.2 Randomized spanning-tree substrate

The start coordinate is `(1, sy)`, where `sy` is randomly selected inside the
border. A depth-first traversal visits every interior cell. At each step, every
unvisited cardinal neighbor receives

```text
score = U[0,99]
      + 18 if east
      -  8 if west
      -  5 if adjacent to the north/south interior border.
```

The best candidate becomes the next child; when no candidate exists, the
traversal backtracks. The pass records `parent` and `depth` but does not yet
emit any level cell. This distinction is essential: the spanning tree is a
private route reservoir, not the final map.

### 6.3 Exit and critical path

The desired route length is

```text
L_target = 9 + 4S.
```

Every visited cell of depth at least seven is scored as an exit candidate:

```text
exitScore = 28x - 8|depth - L_target|
          + 100 if x >= W - 3
          +  18 if |y - sy| >= H/3.
```

Thus the exit tends to lie far east, near the desired path length, and often in
a different vertical band from the start. Following parent pointers back to
the start produces the critical path. Generation fails rather than emitting a
weak map if no sufficiently long path exists, if a parent chain is incomplete,
or if the result contains fewer than eight cells.

Every critical-path cell receives a monotonically increasing `pathRank` and is
connected bidirectionally to its predecessor. `pathRank` is the fundamental
progress coordinate used by later passes; physical BFS distance is computed
separately after room composition.

### 6.4 Optional limb growth

Branches grow from a known critical-path anchor. At each branch step, unused
cardinal neighbors are evaluated using openness and local contact:

```text
branchScore = U[0,30]
            + 7 * openNeighbors
            - 6 * keptNeighbors
            - 80 * unintendedMainPathContacts
            + 5 if on an interior border.
```

The heavy penalty for touching the critical path anywhere except the current
anchor prevents a branch from silently reconnecting beyond a future gate. Each
accepted branch cell inherits the anchor's `pathRank` and records a one-based
`branchDepth`.

### 6.5 Keys and locks

The requested number of keys depends on size and realized route length:

| Condition | Keys |
|---|---:|
| size at least 5 and path length at least 18 | 3 |
| size at least 3 and path length at least 13 | 2 |
| otherwise | 1 |

The order is blue, red, then yellow. For key index `k` out of `K`, the initial
gate rank is approximately

```text
gateRank(k) = clamp(|P|(k + 1)/(K + 1), 3, |P| - 2).
```

Its branch anchor is selected several ranks before the gate. Up to eight nearby
anchors are attempted, and the key is placed at the tip of a limb of roughly
`2 + floor(S/2)` cells. The tip becomes a key arena.

The lock is not a property of every wall around a “locked room.” It belongs to
the single directed boundary between critical-path ranks `gateRank - 1` and
`gateRank`. The later geometry pass therefore emits exactly two usable door
faces for each required key, not a ring of redundant locked doors.

The construction establishes three ordering facts:

1. a key anchor rank is lower than its gate rank;
2. the key limb is attached on the pre-gate side;
3. branch growth cannot touch the critical path elsewhere.

Consequently, the player can obtain the key before reaching its door, and the
key branch cannot itself become an unintended bypass.

### 6.6 Side branches and landmarks

The generator requests `2 + S` general side branches, approximately evenly
spaced along the critical path with small seeded jitter. Anchors adjacent to a
key branch are shifted when possible. Branch lengths scale from one cell to a
small size-dependent limb; alternating sufficiently deep limbs become arenas.

Selected progression beats are then expanded into multi-cell landmarks:

- the start becomes a hub;
- the first-third beat becomes another hub;
- the two-thirds beat becomes an arena;
- every key tip becomes an arena;
- the exit becomes the largest arena.

Landmark expansion greedily adds unused cells adjacent to the growing cluster.
It rewards multiple cluster contacts and penalizes Manhattan distance from the
landmark center, producing compact shapes rather than narrow tendrils. Added
cells inherit the semantic role and progression rank of the landmark.

The exit receives a boss only at difficulty 5, or at difficulty 4 on size 4–5.
This separates “final encounter” from “boss monster” and avoids forcing a boss
into every generated map.

### 6.7 Same-stage circulation loops

Let `G = {g0, ..., gK-1}` be the sorted gate ranks. A room rank `r` belongs to
stage

```text
stage(r) = |{g in G : r >= g}|.
```

Up to `1 + S` loops are considered over east and south adjacencies in three
passes. A missing connection can be opened only if both cells have the same
stage, their progression ranks differ by no more than five, and a seeded 38%
test succeeds. The stage equality is the decisive progression invariant: loops
can improve circulation, reuse, and cross-views inside a completed stage, but
cannot connect the pre-key side of a gate to the post-key side.

## 7. Semantic room composition

The mission graph operates on cells, but Doom spaces should read as rooms,
halls, and courtyards rather than as a chain of identical boxes. `MergeRooms`
groups cells into `RoomInfo` records in two priority passes: special cells first,
ordinary cells second. Special cells include starts, exits, bosses, keys, and
locks; giving them first claim preserves their intended landmark footprints.

### 7.1 Compatibility predicate

A candidate cell may join a seed room only if their roles and progression are
compatible. Important constraints include:

- locked cells merge only with an equivalent lock type;
- ordinary cells do not absorb a special cell;
- key, exit, boss, and start cells merge only into their own landmark rank;
- ordinary ranks differ by at most one;
- main-path and branch cells cross-merge only for an explicit hub/arena at the
  same rank;
- deep-branch cells may not differ by more than one branch-depth level;
- arena/hub differences are tolerated only inside one landmark rank.

This is a semantic flood fill, not a rectangular partition.

### 7.2 Target size and compactness

Target cell count depends on role. Locks remain one cell. Exits request
`4 + floor(S/2)` cells; keys and starts request `2 + floor(S/2)`; arenas and
hubs request approximately `3 + floor(S/2)`. Ordinary main-route and branch
rooms vary deliberately from one-cell closets to broad multi-cell halls.

At each growth step, a candidate is rewarded for already belonging to the same
local neighborhood, for having explicit mission-graph links to that
neighborhood, and for matching the seed's rank and route class. Extreme aspect
ratios above 4:1 are rejected. A small seeded term breaks otherwise equivalent
choices.

After composition, adjacent cells assigned to the same room are opened into
continuous floor space even if landmark expansion had not created an explicit
mission-graph edge. This is safe because the semantic compatibility predicate
has already established that they represent one room.

## 8. Room-graph analysis and spatial coherence

`ApplyCoherence` collapses cell connections into a room adjacency graph. It
collects semantic flags, the minimum progression rank, maximum branch depth,
and the start room. A breadth-first traversal computes `distFromStart` in room
edges. Rooms with one or fewer adjacent rooms become dead ends unless they are
the start or exit; main-path rooms with at least three neighbors become hubs.

Two independent progress measures are retained:

- `progressionRank` describes intended order on the mission path;
- `distFromStart` describes realized graph distance after merges and loops.

The visual pass primarily uses graph distance, while weapon scheduling uses
ordered main-route rooms and progression rank. This prevents a local circulation
loop from destroying the authored progression cadence.

## 9. Visual grammar

### 9.1 Progression phases and palettes

For maximum BFS room distance `D`, room phase is

```text
phase = clamp(floor(4 * distFromStart / (D + 1)), 0, 3).
```

Each theme has a 4×4 table for wall, floor, and ceiling materials. The first
dimension is progression phase and the second is a deterministic local
variant. Side rooms at branch depth two or greater may advance one palette
phase, making deep optional areas feel distinct without abandoning the theme.

Techbase progresses from STARTAN/brown surfaces through stone and metal toward
TEKWALL/computer motifs. Hell progresses from stone toward marble, vine, wood,
and hot/finale surfaces. All entries are compatible with Ultimate Doom; Doom
II-only variation is limited to actors where the IWAD can be identified safely.

### 9.2 Deterministic room identity

Each room computes a stable style hash from its ID, bounds, cell count,
progression rank, and branch depth:

```text
style = |37 id + 17 minX + 29 maxY + 13 cells
          + 7 progressionRank + 19 branchDepth|.
```

The hash selects one of eight dimension profiles, one of four surface variants,
one of five corner cuts, and several vertical variations. It does not consume
the shared RNG. Geometry identity therefore remains stable even when a later
random encounter decision changes its number of draws.

### 9.3 Dimension and corner profiles

Single-cell half-width/half-height profiles range from 80 to 120 map units:

```text
(80,88), (88,112), (112,88), (96,120),
(120,96), (104,104), (120,120), (88,104).
```

The selected profile is biased to follow a multi-cell room's dominant axis.
Arenas and exits use 120×120 cell footprints; hubs and keys receive minimum
broad dimensions; starts are normalized to 104×104 for a predictable staging
area; locks use a compact 88×88 profile.

Corner cuts are selected from 12, 16, 20, 28, and 36 units and clamped so at
least 56 units remain clear from a cell center. Accent textures are applied to
these chamfers. Interior corners disappear where two adjacent coarse edges are
fully open inside the same room, so a multi-cell room reads as one continuous
space rather than several octagons connected through narrow waists.

### 9.4 Vertical composition

Phase floor cadence is `{0, 8, -8, 16}` plus a small profile variation; deep
branches drop another eight units. Room clearances vary by role:

| Role | Typical clear height |
|---|---:|
| compact ordinary room | 96–128 |
| general room | 104–152 |
| hub | 144–176 |
| arena | 176–208 |
| exit/boss landmark | 208–240 |

The start is fixed at floor 0 and ceiling 128. Four relaxation sweeps constrain
adjacent room floors to Doom's 24-unit step limit. With the current small floor
cadence, this removes non-traversable height discontinuities while preserving a
visible vertical rhythm.

### 9.5 Lighting and outdoors

Base light begins at 192 and falls by eight per phase. Side rooms and deep
branches darken; hubs, arenas, starts, keys, and exits brighten. Values are
quantized to multiples of eight and clamped to 160–208. The lower bound is a
deliberate readability policy rather than an engine limit.

Every map makes the exit and at least one additional combat landmark outdoor.
Larger maps select more multi-cell arenas, hubs, and broad main-route rooms up
to an outdoor budget of `2 + floor(S/2)`. Outdoor sectors use at least light
192. Validation requires at least two sky sectors and a sky courtyard spanning
at least 400 units on one axis, preventing a token sky closet from satisfying
the open-area contract.

### 9.6 Semantic detail

Starts and hubs may contain a centered platform raised by 8 units. Arenas, key
shrines, and exits place the same final 16-unit elevation behind two concentric
8-unit sectors, turning a single curb into a legible stair dais. Indoor hubs may
receive a shallow ceiling coffer; sky landmarks preserve the sky ceiling. The
exit is a larger 144-unit, level-224 `GATE1` pad bounded by four `EXITDOOR`
edges and an outer `STEP1` tier, giving its invisible walk trigger an explicit
visual language.

Decoration is sparse and semantic:

- tech landmarks use lamps in Doom II and shared pillars/columns in Ultimate
  Doom;
- Hell key rooms use key-colored torches or a gold candelabra;
- Hell exits use an evil eye;
- Hell outdoor rooms use torch trees;
- secret rooms use reward-readable props;
- sufficiently populated arenas and secrets may contain a corpse.

Solid props are tested against every previously emitted gameplay thing with a
44-unit clearance and are placed near safe room corners, away from portal
centers. Non-solid corpses use a smaller clearance. This makes decoration
subordinate to combat and traversal.

## 10. Encounter model

### 10.1 Per-room pressure

The start has zero enemies. For an ordinary room, initial pressure is

```text
pressure = floor((difficulty - 1)/2)
         + [phase >= 2]
         + [difficulty >= 4 and main path and phase > 0]
         - deepBranchRelief
         + U[0,1].
```

Difficulty 2 receives a sparse deterministic extra point so it remains
distinguishable from difficulty 1. Counts are then bounded by semantic role:

| Room role | Encounter cap/range |
|---|---:|
| ordinary | 1–3 |
| early ordinary room | at most 2 |
| dead-end reward | reduced by difficulty |
| hub | 2–4 |
| arena or key room | 2–5 |
| locked transition | 1–4 |
| exit | 2–5 |
| boss support | capped at 1–3 |

The purpose of the cap is not merely performance. Doom difficulty grows
nonlinearly with monster composition and room geometry, so bounding local count
prevents a random room from consuming the entire map's pressure budget.

Monster tier is

```text
tier = clamp(1 + phase
               + [difficulty >= 4]
               + [boss room and difficulty >= 5], 1, 5).
```

Heavy rosters therefore arrive primarily through progression and only receive a
difficulty acceleration in the upper two settings. Single-cell rooms cap at
tier 2 and two-cell rooms cap at tier 3, preventing large bodies from appearing
inside connector-scale geometry.

### 10.2 Coherent monster families

Rooms select a family from room ID, progression rank, and branch depth. Doom II
families include early infantry/demons, middle infantry/demons/flyers, and late
bruiser/heavy/air groups. Within one room, enemy-index jitter varies individual
actors without mixing the whole bestiary indiscriminately. Arch-Viles are
excluded from random placement.

Ultimate Doom uses separate early, middle, and late arrays containing only
actors available in that IWAD. Boss selection is also IWAD-aware. Doom II uses
an easy Baron fallback through difficulty 3, a medium Baron/Hell Knight choice
at difficulty 4, and a Cyberdemon at difficulty 5 only when the composed finale
contains at least eight cells. Ultimate Doom never substitutes the Doom II-only
Hell Knight. The Spider Mastermind is excluded because its 128-unit radius is
not compatible with spawning at the center of a 256-unit coarse cell.

### 10.3 Boss policy

A boss is a separate thing in the exit landmark and does not replace all
ordinary enemies. Bosses occur only for difficulty 5 or large difficulty-4
maps. Arena, shrine, and finale cell budgets expand with difficulty, and a heavy
boss requires an eight-cell finale; otherwise selection falls back to the
medium roster. Support counts are capped. This avoids the common procedural
failure in which a heavyweight boss occupies a closet or combines with an
unrestricted ordinary encounter roll.

### 10.4 Ambushes and elevated ranged pressure

Every map selects at least one key for a reveal trap; additional keys have a
seeded 60% chance after the first. Crossing the key platform invokes
`Door_Open` on a uniquely tagged closed slab. If a compact key room has no spare
cell, a same-sector trigger ring surrounds the key and the reveal chamber is
placed in the nearest compatible broad room. The chamber contains two
IWAD-compatible ranged actors with `ambush = true`, so ordinary sound propagation
does not spend the encounter before its door opens.

A reveal host is accepted only when its room profile leaves at least 40 units
between the fixed inset footprint and every cardinal wall as well as the
chamfer diagonal. Landmark anchor cells and other authored feature cells are
reserved. The reveal opening is 64 units wide, twice the player's diameter,
so the closed slab cannot create the narrow squeeze observed in earlier output.

Open arenas are preferred for ranged perches, with sufficiently tall hubs and
broad route rooms as fallbacks. A perch rises 48 units on difficulties 1–3 and
64 units on difficulties 4–5 while preserving at least 80 units of headroom.
Its four two-sided boundaries use `blockmonsters` rather than `blocking`, keeping
the actor at elevation while allowing player shots and monster projectiles to
cross. Feature cells are removed from ordinary reward, enemy, and decoration
placement to prevent overlap with the authored geometry.

## 11. Weapon and resource economy

### 11.1 Guaranteed milestones

Main-route rooms are sorted by BFS distance, and side rooms form a separate
ordered list. Weapon assignment searches outward from a preferred milestone if
that room already owns another weapon. The schedule is:

| Milestone | Weapon | Condition |
|---|---|---|
| player start, 32 units forward | shotgun | always |
| first quarter | super shotgun | Doom II and at least two route rooms |
| first third | chaingun | at least three route rooms |
| midpoint | rocket launcher | size at least 2 |
| three quarters | plasma rifle | size at least 4 |
| deepest optional reward | BFG | size at least 5 and difficulty 5 |

The forward start shotgun gives immediate agency and is geometrically validated
against player angle and distance. Ultimate Doom omits the unsupported super
shotgun without leaving a broken thing type.

### 11.2 Phase-aware ammunition

Weapon rooms receive their weapon's ammunition family. Otherwise, early phases
choose shells or bullets, middle phases introduce rockets on suitable sizes,
and late size-4+ phases may use cells. Major fights—five or more enemies,
arenas, key rooms, or exits—upgrade small ammunition to a box or cell pack:

```text
shells -> shell box
clip   -> bullet box
rocket -> rocket box
cell   -> cell pack.
```

Ammo is guaranteed for weapon rooms, major fights, and every difficulty 4–5
room with at least three enemies; it appears on 60% of other main-route rooms.
Four-plus-enemy encounters emit a second pack.

### 11.3 Recovery and rewards

Major fights and rewards receive health; other main-route rooms have a 50%
health chance. Major encounters use stronger recovery and may emit two packs.
Keys and bosses receive armor, while deep dead ends have a 40% armor chance.
Boss rooms use the strongest armor type in the current table.

One or more deep optional dead ends become real sector-special-9 secrets. A
secret gets a wall-aligned secret door, ammunition, a stim/med recovery bundle,
and armor even when weapon scheduling selected a different branch. Secrets are
thus mechanically meaningful rather than only a hidden texture change.

## 12. UDMF geometry synthesis

### 12.1 Intermediate records

The emitter accumulates typed build records for vertices, sectors, sidedefs,
linedefs, things, and connection references. Only after all geometry and things
exist are these records serialized. This makes reference indices explicit and
allows later features such as door-face scaling to modify a sidedef before text
output.

Vertices are deduplicated by coordinate with a tolerance of 0.001. A sector
stores heights, textures, light, an optional special, and an optional UDMF ID.
A sidedef stores top, middle, bottom, offsets, and top-texture Y scale. A linedef
stores side indices, activation/monster-blocking flags, special, lock number,
and five arguments. Thing records can mark closet actors as deaf ambushers.

### 12.2 Coordinate system and chamber boundaries

Cell centers are calculated relative to the canvas center:

```text
x_world = ((x + 0.5) - W/2) * 256
y_world = ((y + 0.5) - H/2) * 256.
```

Each present cell emits one clockwise, chamfered chamber boundary using its
room's half-width, half-height, and corner cut. Clockwise winding ensures the
front sidedef faces inward. Same-room full openings remove internal perimeter
segments; inter-room connections reserve a centered aperture; absent
connections remain one-sided walls.

Every exposed wall is emitted through `AddWall`, which enforces:

- a one-sided linedef;
- `blocking = true`;
- a real `texturemiddle`;
- `dontpegbottom = true`;
- segment-centered 128-unit horizontal phase;
- row offset equal to the negative sector floor.

These constraints eliminate hall-of-mirrors failures from missing middle
textures and stop wall motifs from restarting at every split segment or
slipping vertically when floors change.

### 12.3 Explicit corridor sectors

Connections between different rooms are not represented by an ambiguous shared
grid edge. The emitter creates a corridor sector spanning the gap between inset
chambers. Room portals and corridor side walls share deduplicated endpoint
vertices. Corridor floor is the maximum adjacent floor; ceiling is the minimum
adjacent ceiling, raised to preserve at least 72 units of clearance when
necessary. Light is the average of adjacent room light within 160–208.

Doorless joins use aperture half-widths based on role:

- ordinary: 56;
- hub: 64;
- arena: 72;
- deep branch: 44.

The aperture is clamped to the smaller adjacent room dimension minus the larger
corner cut. Same-room connections consume the entire compatible edge.

### 12.4 Functional recessed doors

A door replaces the corridor sector with a closed sector whose floor and ceiling
start at the same height. The moving slab is 16 units deep. Static jambs fill
the remaining corridor span. Two one-sided track walls bound the slab, and two
two-sided faces operate the door.

Door faces use UDMF special 12 (`Door_Raise`) with:

```text
arg0 = 0      operate on the back sector
arg1 = 16     speed
arg2 = 150    delay
playeruse = true
repeatspecial = true.
```

The front side is the room; the back side is the initially closed door sector.
Locked faces carry lock number 1, 2, or 3 and use red, blue, or yellow track
textures. The same color extends across four static approach-jamb segments, so
each keyed doorway presents at least six colored border surfaces. Ordinary
tracks use `DOORTRAK`. Track linedefs are one-sided, bottom-pegged walls so the
tracks remain stationary while the sector ceiling moves.

Stock door art is 128 units wide. For a face of width `w <= 128`, the horizontal
offset is

```text
offsetX = round((128 - w)/2),
```

which centers the recognizable motif rather than cropping only one edge. For a
door whose visible vertical span is `h`, both face sidedefs use

```text
scaley_top = min(1, 128/h).
```

Tall doors therefore stretch the stock image once across the opening instead of
repeating it vertically. Secret doors use the adjacent wall material and the
same physical slab but set the linedef secret flag.

Door selection is bounded. Locks and secrets always request a door; the start
is closed off as a safe staging area; requested reward/deep-transition doors
and a small fraction of arena transitions consume a normal-door budget of
`2 + S`; and at most one door is created per room pair.

### 12.5 Landmark sectors and exit

Multi-cell starts and hubs may receive a centered raised platform sector.
Arenas, key shrines, and exits use two 8-unit tiers whose four boundaries are
two-sided, traversable, top- and bottom-pegged step edges. Secret rooms mark
their base sector with special 9. The exit platform uses `GATE1`, `EXITDOOR`,
light 224, and a single interior two-sided linedef with special 243
(`Exit_Normal`) and explicit `playercross` activation.

Reveal chambers are inset islands with two oppositely wound one-sided loops: a
counter-clockwise room boundary cuts a void moat, and a clockwise inner boundary
faces into the new playable chamber. A thin closed door sector bridges their
only opening. Tags 1000–1499 identify key traps and tags 1500–1999 identify
switch caches; `Door_Open` special 11 targets those IDs at speed 16. Switch use
lines occupy an exact centered 64-unit segment and carry the 64×128
`SW1COMP` or `SW1GARG` texture. Their zero origin, unit horizontal scale, and
`scaley_mid = 128 / wallHeight` show one switch motif in each axis; key pads
carry four player-cross activators. Raised perch sectors use IDs 2000–2999 and
four `blockmonsters` edges.

Optional lift sectors use IDs 3000–3999. A lift begins 32 units above its room,
owns a 64-unit square footprint and at least 64 units of raised-state headroom,
and exposes special 62 (`Plat_DownWaitUpStay`) with use/repeat activation on all
four faces. Each target is selected outside landmark anchors, reveals, perches,
keys, starts, exits, locks, bosses, and secrets. At least 40 units remain between
the platform and room boundary, so it can lower for its center reward without
ever becoming the only route through the room.

### 12.6 Thing placement

Critical things snap to known cell centers. The player faces the first connected
cardinal direction. The start shotgun is placed 32 units forward. Keys occupy
the authored key cell, and the boss occupies the exit cell.

Rewards use a repeating nine-slot pattern distributed across room cells.
Enemies use a twelve-offset pattern distributed by progression rank and room
cell count. Every offset is clamped to

```text
safeX = max(32, halfWidth  - cornerCut - 20)
safeY = max(32, halfHeight - cornerCut - 20),
```

so narrow and heavily chamfered profiles cannot push an actor outside the
chamber. Enemies face the center of their assigned cell. Decoration uses the
related 18-unit clearance margin and a collision check against all emitted
things.

## 13. Serialization

The output begins with `namespace = "zdoom"` and serializes vertices, sectors,
sidedefs, linedefs, then things. Every thing is enabled for skills 1–5 and for
single-player, cooperative, and deathmatch flags; procedural launch itself is
currently restricted to single-player because generation and network-session
coordination are separate concerns.

The emitter returns success only if at least one vertex, sector, and linedef
exists. The loader additionally rejects an empty string. Semantic and geometric
validity is established by the external validator and real map load described
below.

## 14. Correctness invariants

The pipeline is organized around the following invariants.

### 14.1 Progression invariants

1. There is exactly one critical path from start rank 0 to the chosen exit in
   the selected spanning-tree substrate.
2. Each required key is on a limb anchored before its gate rank.
3. The locked boundary is exactly the critical-path edge entering its gate
   rank.
4. Branch growth cannot reconnect to a non-anchor critical-path cell.
5. Added circulation edges connect only equal lock stages.
6. Room composition does not merge incompatible locks or special landmarks.

### 14.2 Geometry invariants

1. Every one-sided boundary is blocking and textured.
2. No two-sided line pretends to be a solid blocking wall.
3. All sidedef sector indices and linedef vertex/side indices are valid.
4. Every sector is referenced and the sector-adjacency graph is connected.
5. Every door sector starts closed, has two faces separated by 16 units, and
   has exactly two stationary track walls.
6. Each door face uses the correct special, activation flags, lock, texture
   crop, and vertical scale.
7. There is exactly one player start and one player-cross exit trigger.
8. Every generated map has a readable room-scale sky landmark and at least one
   real secret.
9. A Cyberdemon remains at least 96 units from the nearest solid wall, and the
   Spider Mastermind is never emitted by the coarse-cell boss policy.
10. At least one usable switch and one key crossing target distinct, existing,
    initially closed reveal-sector IDs with valid `Door_Open` arguments.
11. Every switch owns one exact 64-unit panel with a single fitted 64×128 motif,
    and every reveal owns a 64-unit door plus 40 units of serialized circulation
    clearance at cardinal and chamfer boundaries.
12. At least one perch rises 48 units above an adjacent room, owns four
    `blockmonsters` boundaries, and contains a ranged actor.
13. At least one lift rises exactly 32 units, owns four valid use/repeat action
    edges, has 64 units of headroom, contains a reward, and retains a 40-unit
    bypass.
14. Ordinary traversable sector boundaries have at least 56 units of headroom
    and no floor step above 24 units; closed doors, perches, and lifts are
    validated by separate contracts.
15. The exit trigger lies on a `GATE1` pad with four `EXITDOOR` borders and two
    complete 8-unit stair tiers, every present key color has at least six
    matching doorway-border segments, and every map contains at least two
    open-sky sectors.

### 14.3 Economy and compatibility invariants

1. The start room has no encounter and supplies a forward shotgun.
2. Random placement never emits an Arch-Vile.
3. Monster counts remain inside size-scaled lower and upper bounds.
4. Ammunition pickups and health/armor pickups each occur at least once per five
   ordinary monsters in the structural metric.
5. Standard and larger maps contain at least two useful weapon milestones.
6. Doom II maps contain a super shotgun; Ultimate Doom maps contain no Doom
   II-only monster, weapon, or lamp actor.
7. Difficulty pressure for the reference seed is nondecreasing from 1 to 5.
8. At fixed seed and size, the finale-room floor area grows strictly at each
   difficulty step.

## 15. Validation methodology

The primary regression driver is `test_procgen.sh`. It launches the actual
engine headlessly, requests a UDMF dump, parses the serialized document, and
also enters `PROCMAP` through the normal runtime loader. This is stronger than a
test that reads only internal room objects: serialization mistakes, unknown
textures, node-builder failures, and map-loader integration errors remain
observable.

### 15.1 Structural parser

An embedded Python parser extracts every UDMF block and verifies:

- reference ranges and nonzero lines;
- boundary winding consequences, middle textures, blocking, and pegging;
- centered horizontal texture phase, floor-aligned vertical texture rows, and
  exact single-copy switch-panel dimensions/scales;
- door topology, motion semantics, keyed tracks, fitted art, and slab depth;
- switch/key remote targets, activation modes, ambush actors, closed slabs,
  64-unit apertures, and 40-unit reveal circulation;
- perch elevation, monster-blocking boundaries, and ranged occupancy;
- lift dimensions, height, headroom, action semantics, reward, and bypass;
- ordinary traversal headroom and step height;
- exit activation/material language, complete stair tiers, and absence of
  obsolete specials;
- keyed doorway-border color coverage;
- sky size/light and global minimum light;
- secret sector and secret door presence;
- a substantial diagonal-line ratio;
- at least eight distinct one-sided boundary lengths and six clear heights;
- start shotgun position;
- decoration clearance;
- heavyweight boss clearance;
- sector connectivity and reference coverage.

Shell-level checks add key/lock cardinality, size-scaled sector/thing/monster
budgets, resource ratios, weapon milestones, theme semiotics, texture diversity,
and IWAD actor compatibility. For size 3–5, the validator requires at least
eight non-track wall textures, six floor textures, and five non-sky ceilings.

### 15.2 Representative matrix

The release validation matrix covers both themes and samples the complete size
range, including the maximum size-20 setting:

| Seed | Theme | Difficulty | Size | Sectors | Things | Monsters | Decorations | Locks | Keys |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | techbase | 2 | 1 | 55 | 91 | 36 | 19 | 2 | 1 |
| 42 | hell | 3 | 2 | 58 | 119 | 47 | 27 | 2 | 1 |
| 99 | techbase | 3 | 3 | 89 | 166 | 66 | 41 | 4 | 2 |
| 123 | hell | 4 | 4 | 100 | 229 | 94 | 47 | 4 | 2 |
| 999 | techbase | 5 | 5 | 116 | 273 | 123 | 45 | 6 | 3 |
| 20260713 | hell | 5 | 20 | 375 | 784 | 402 | 114 | 6 | 3 |

All six documents passed the complete structural validator in the 4.15.3
release worktree.

### 15.3 Difficulty experiment

Holding seed 2024, techbase theme, and size 3 constant produces:

| Difficulty | Monsters | Ammo pickups | Health + armor pickups | Finale area |
|---:|---:|---:|---:|---:|
| 1 | 46 | 26 | 25 | 269,976 |
| 2 | 47 | 23 | 26 | 396,416 |
| 3 | 65 | 25 | 27 | 523,904 |
| 4 | 77 | 33 | 33 | 650,064 |
| 5 | 83 | 30 | 38 | 781,464 |

The pressure curve is monotonic, and actual emitted finale floor area increases
at every difficulty step. The raw pickup count understates late support because
major fights upgrade individual pickups to boxes and cell packs. This experiment
demonstrates the intended count/space curve; it does not claim equal completion
rates for players of different skill.

### 15.4 Runtime and compatibility tests

Runtime tests enter small techbase, medium Hell, and large techbase maps through
`+map PROCMAP`, require the `PROCMAP` level banner, and reject generation,
texture, map, connection, and node-builder errors. Separate Ultimate Doom cases
exercise both themes at difficulty 4–5, explicitly reject every known Doom
II-only actor in the generator vocabulary, and load a high-difficulty Hell map
to verify the shared switch, exit, and keyed-border texture vocabulary.

The menu regression reads the packed `MENUDEF`, verifies every setup control,
checks native reinsertion into mod-replaced main menus, validates persistent
defaults and random seed generation, and enters a randomized Hell map through
the user-facing command. It also verifies that oversized replacement list menus
retain mouse-wheel, page, home/end, and selection-follow scrolling.

## 16. Complexity and performance

Let `C = (W - 2)(H - 2)` be interior canvas cells, `R` composed rooms, `V`
emitted vertices, `L` linedefs, and `T` things.

- Spanning-tree traversal, exit selection, final grid cleanup, adjacency
  extraction, and room BFS are `O(C)`.
- Branch and landmark budgets are bounded by size 1–20 in the shipping interface;
  generalized growth is linear in accepted cells times four cardinal neighbors.
- Room composition scans each growing room frontier repeatedly. With bounded
  target sizes it is effectively linear in `C`; without those bounds its
  conservative worst case is quadratic.
- UDMF emission is linear in cells and connections except vertex deduplication,
  which linearly scans prior vertices and is therefore `O(V^2)` in the worst
  case.
- Decoration collision checks scan prior things and can approach `O(T^2)`.
- Serialization is `O(V + R + L + T)` in record count and output size.

The largest supported canvas has 1,150 interior cells at size 20, although only
the selected route, branches, and landmarks are emitted. The
simple linear deduplication and collision structures remain practical at this
bound; spatial hashing for vertices and thing clearances is the clearest future
optimization target.

Memory use is linear in the canvas, room graph, intermediate UDMF records, and
final string. Generation is synchronous during map opening; it does not retain a
second parsed map after ownership transfers to `MapData`.

## 17. Failure handling and security posture

Generation clears its previous state and error string at the start of every
call. It fails explicitly on an unusable route, incomplete parent chain,
missing key branch, inability to host the required key trap, switch cache,
ranged perch, or safely bypassable lift, empty UDMF, or empty core geometry. The
map factory logs the error and returns `nullptr`, allowing the normal engine
path to reject the map.

Unlike a user-supplied UDMF, generator text is produced from fixed format
strings, bounded numeric inputs, fixed texture vocabularies, and internal actor
tables. Theme input influences table choice but is never interpolated as raw
UDMF syntax. This substantially limits injection risk. Nevertheless, the
generated text deliberately passes through the normal parser and node builder,
which provides the same structural checks used for external maps.

The singleton is synchronous and is not designed for concurrent generation.
Network games are rejected by the launch command because deterministic map text
alone does not implement peer negotiation, content verification, or synchronized
new-game lifecycle.

## 18. Limitations

The present system has deliberate boundaries:

- The coarse embedding is cardinal and grid-based. Chamfers, variable profiles,
  multi-cell composition, and landmarks disguise the grid, but arbitrary-angle
  room graphs are not synthesized.
- Only two visual themes are authored. The architecture supports additional
  tables, but a new theme also needs IWAD-safe textures and semantic props.
- Encounter balance uses counts and tiered families, not a formal estimate of
  hit points, damage exposure, infighting opportunity, or player inventory
  simulation.
- Multiplayer launch is unsupported.
- Generated things enable every skill flag because generation difficulty is
  applied while constructing the map; one serialized map is not a five-skill
  remix.
- Geometry validation proves topological and budget properties, but automated
  tests do not replace human evaluation of sightlines, combat rhythm, or visual
  composition.
- Determinism is version-scoped. Algorithm or table changes may intentionally
  change the map produced by an old seed.
- The vertex and decoration searches use quadratic worst-case algorithms, which
  are acceptable only because current sizes are tightly bounded.

## 19. Future work

Promising extensions preserve the staged architecture rather than collapsing
it:

1. Add a formal mission-graph verifier that symbolically tracks key inventory
   across directed edges in addition to construction-time guarantees.
2. Introduce theme packages containing palettes, semantic props, monster-family
   policies, and landmark templates with automatic IWAD capability checks.
3. Estimate encounter cost from monster hit points, projectile pressure,
   available cover, and supplied weapon damage rather than count alone.
4. Add spatial hashes for vertex deduplication and actor/decor clearance before
   supporting canvases larger than size 20.
5. Add visibility and crossfire metrics after node construction, feeding a
   bounded repair pass that can adjust portals or encounter anchors without
   changing progression.
6. Support deterministic cooperative generation through server-authored
   settings, map checksums, and explicit peer synchronization.
7. Store a generator schema/version beside shared seeds so older generation
   semantics can be reproduced intentionally.
8. Add automated play traces for reachability, key acquisition, door use,
   weapon pickup, and exit completion.

## 20. Reproduction

Build BiasedDoom and run the complete generator checks:

```bash
cmake --build build --config Release
./test_procgen.sh validate
./test_procgen.sh determinism
./test_procgen.sh balance
./test_procgen.sh doom1
./test_procgen.sh load
./test_procgen.sh menu
```

Inspect one document directly:

```bash
./build/biaseddoom -iwad /path/to/doom2.wad \
  +dumpprocudmf 42 hell 3 3 +quit
less /tmp/procmap_test.udmf
```

Start the same map through the normal loader:

```bash
./build/biaseddoom -iwad /path/to/doom2.wad \
  +procgen_seed 42 +procgen_theme hell \
  +procgen_difficulty 3 +procgen_size 3 +map PROCMAP
```

## 21. Implementation map

| Source | Responsibility |
|---|---|
| `src/common/maps/procgen.h` | cell/room state and generator interface |
| `src/common/maps/procgen.cpp` | CVars, console commands, in-memory `MapData` factory |
| `src/common/maps/procgen/procgen_core.cpp` | mission graph, embedding, branches, keys, locks, loops, landmarks |
| `src/common/maps/procgen/procgen_rooms.cpp` | room composition, graph analysis, visual grammar, pacing, economy, secrets |
| `src/common/maps/procgen/procgen_udmf.cpp` | sectors, chambers, corridors, doors, things, UDMF serialization |
| `src/common/maps/procgen/procgen_internal.h` | grid directions and shared actor tables |
| `src/p_openmap.cpp` | procedural-name interception in the normal map loader |
| `src/playsim/procgen_zscript.cpp` | native ZScript bridge |
| `wadsrc/static/zscript/procgen/procgen.zs` | public ZScript declarations |
| `wadsrc/static/menudef.txt` | player-facing generator configuration |
| `src/common/menu/menudef.cpp` | reinsertion into mod-replaced main menus |
| `test_procgen.sh` | serialized-geometry, balance, compatibility, menu, and runtime tests |

## 22. Conclusion

BiasedDoom's procedural generator treats a playable Doom map as a sequence of
contracts rather than a single random geometry problem. Progression is authored
first, embedded second, spatially composed third, paced fourth, and serialized
last. Locks and loops are constrained while the graph is still explicit;
materials and encounters operate on semantic rooms; and UDMF geometry is built
from closed, testable primitives. The result is a generator whose maps vary in
route, room scale, silhouette, height, materials, encounters, and landmarks
while retaining deterministic reproduction, key/lock safety, renderer-valid
walls, functional doors, bounded difficulty, and normal engine compatibility.

The architecture does not eliminate the need for human playtesting. It does,
however, move a large class of failures—unreachable exits, bypassed locks,
invisible walls, malformed doors, missing resources, unsupported actors, and
non-reproducible seeds—from subjective testing into construction rules and
executable validation. That separation is the principal result of the work.
