# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

## [4.15.5] - 2026-07-15

### Added

- Procedural savegames now archive the exact generated UDMF together with seed, theme, difficulty, size, layout, verticality, detail, and outdoor metadata, preserving the original base map across generator revisions.
- Added deterministic Layout Shape, Verticality, Architecture Detail, and Outdoor Spaces menu controls, each backed by an independent generation-effect and runtime/node regression.
- All five procedural themes now own architectural silhouettes, ceiling/elevation behavior, courtyard and interactive-feature cadence, colored lighting, landmark materials, trim, and prop rhythms; Corrupted Tech gains dedicated four-phase hybrid surfaces.
- Procedural routes now use broad multi-level terraces connected by full-width 8-unit stair sectors; structural regression checks require a 96-unit vertical range and size-scaled stair coverage.
- Procedural progression now audits every composed connection by lock stage and rejects any cross-stage opening that is not the single planned keyed gate.
- Structural validation removes keyed door sectors and proves that ordinary doors and open portals cannot reconnect either side of a key gate.
- Added Industrial, Gothic, and Corrupted Tech procedural themes with distinct material transitions and decoration vocabularies.
- Added size-scaled deep-branch survival caches, a guaranteed main-route recovery cadence, and substantially denser role-aware decoration.
- Added size-80 extreme-map generation with guarded UDMF coordinate limits and regression coverage for real runtime loading.
- Added a fixed-seed, all-theme size-80 regression for seed `1771465796`, including serialized passage-clearance and real node-builder checks.
- Added a five-seed maximum-size stress matrix with developer-level BSP diagnostics; renderer-dangerous coincident lines, open/branched sector boundaries, zero-area loops, and synthetic hole subsectors are now regression failures.
- Procedural secrets now use the engine's real `SECRET_MASK`, receive staged backpack, invisibility, berserk, soulsphere, computer-map, light-amplification, invulnerability, and Doom II megasphere rewards, and have dedicated structural/runtime regressions.
- Ordinary doors now select stock 64×72, 128×96, 128×112, and 64/128×128 profiles by theme and IWAD, including Doom II `SPCDOOR` variants.

### Changed

- Screenshot requests are captured after final 2D composition and before presentation, so full-screen automap, HUD, and console layers are included consistently by OpenGL, GLES, and Vulkan.
- Ordinary procedural room floors now follow a `0 → 32 → 64 → 96` terrace rhythm with deterministic branch offsets and a bounded 64-unit inter-room transition, replacing shallow per-room height jitter.
- Key-triggered ambushes and switch-opened opportunity caches now vary silhouette, floor/ceiling treatment, lighting, reveal-door prominence, and actor/reward layout; some cache switches are placed in a nearby room within the same lock stage.
- Procedural surface families now use broader IWAD-safe palettes and progression/role clusters. Continuous chamber walls retain one material, while connector, jamb, platform, and reveal accents change only at visible geometry seams.
- The procedural size slider now reaches 80, optional-branch density is higher, major fights receive more recovery, and ordinary rooms attempt one to three decorations instead of being mostly bare.
- Sizes above 40 now reflow excess horizontal growth into height and center the emitted bounds, retaining extreme capacity without placing starts against the UDMF coordinate edge.
- Industrial and Gothic now use dedicated four-phase wall, floor, and ceiling tables; every theme has a more varied semantic prop rhythm.
- Same-room joins now use explicit 224–256-unit hall portals instead of consuming whole coarse-cell edges, keeping huge four-way junctions topologically well-defined.
- Direct recovery now has a deterministic floor of one substantial pickup per four authored monsters, so high-difficulty huge maps scale their survival economy with actual encounter pressure.
- Door openings now inherit the selected stock texture's native width and height, with explicit lowered approach sectors forming real lintels on both sides instead of fitting one motif to every tall room.

### Fixed

- Hardware sky fog now uses a non-overlapping, continuously interpolated 32-strip hemisphere, eliminating concentric rings and translucent fan wedges when looking into the sky.
- GLES mapped-buffer subupdates no longer write through a missing CPU shadow allocation, preventing a crash when the new sky-fog gradient is uploaded through the compatibility renderer.
- Saving an in-memory procedural map no longer dereferences the invalid `-1` map-lump container and crashes. Loading stages the archived TEXTMAP before world restoration instead of depending on ambient procedural CVars.
- Automap screenshots no longer capture the hidden 3D view underneath the map overlay.
- Door thresholds that must remain level are normalized before emission; every other non-level room connection receives a traversable staircase instead of an impassable ledge.
- Room composition and UDMF emission can no longer turn a progression-stage boundary into a normal unlocked door or opening.
- Corridor support textures no longer begin in the middle of a flat chamber wall; 8-unit depth returns provide a natural architectural transition.
- Remote opportunity switches are assigned only to rooms proven to contain a full panel wall, preventing large-map generation failures in highly connected one-cell rooms.
- Solid decorations now reserve 112-unit approaches around passages, doors, lifts, and full stair routes, plus a tighter exclusion around shallow landmark tiers, instead of checking actor overlap alone.
- Maximum-width four-way joins no longer emit coincident solid lines or zero-area pinwheel boundaries that produced black floor/ceiling holes after GL-node construction.
- Exit and key chambers with sufficient physical space now always receive their authored landmark platform, including single-cell exits on huge seeds.
- Dramatic terraces on maximum Exploratory graphs now use a graph-distance fallback when cyclic local relaxation cannot converge, guaranteeing every adjacent rise remains within the eight-tread staircase bound.
- Every mission graph now reserves a one-door optional leaf before landmark expansion; loops and room merging cannot consume it, so compact seeds still contain a genuine hidden reward rather than a through-route secret flag.
- Compact maps may place perches and safely bypassable lifts on terrace cells when no level feature cell remains; each feature replaces the cell with its own validated platform or stair geometry instead of aborting generation.
- Mandatory theme landmarks use a collision- and passage-checked wall-bay fallback, keeping Hell finale markers and dense Gothic dressing present without blocking doors, stairs, or gameplay actors.
- Door art no longer extends into adjoining wall shoulders: each moving face matches its native 64- or 128-unit texture width, and every 16-unit slab retains a nonzero recessed approach on both sides.
- Secret supplies avoid landmark and combat-feature footprints, and the expanded reward-slot layout prevents multiple survival pickups from occupying the same coordinate.

## [4.15.4] - 2026-07-13

### Added

- Procedural map sizes now use a 1–20 slider, extending deterministic generation from compact missions through colossal maps.
- Procedural landmarks can include switch-operated supply reveals, key-triggered ambush closets, raised ranged perches, broad stair tiers, and optional reward lifts with permanent bypasses.
- Player-facing mugshot controls now provide 0.25x–4x scaling, horizontal/vertical positioning, and one-action reset for stock ZScript and legacy SBARINFO status bars.
- Autoaim now has explicit off support and independently tunable horizontal and vertical assistance.

### Changed

- Procedural ordinary rooms begin at broader multi-cell targets, while hubs, arenas, key rooms, and finales grow with map size and combat difficulty.
- Procedural encounters use safer room-aware pressure, stronger major-fight support, and larger finale floor areas at every higher difficulty step.
- Player skin selection now survives gameplay-mod player replacements and remains visible on the actual actor in first-person state changes and third-person views.
- Procedural texture phases are centered per segment so opposite walls, doorway shoulders, chamfers, and accent surfaces align symmetrically.
- Exit landmarks, keyed-door borders, outdoor spaces, and room silhouettes now have clearer visual language and greater variation.

### Fixed

- Raised procedural areas now include traversable stairs or lift/bypass routes instead of leaving required spaces unreachable.
- Cyberdemons require a finale of at least eight merged cells, and Spider Masterminds are no longer selected for generated finales.
- High-resolution mugshots no longer need global texture edits to fit classic status-bar slots.
- Tight procedural rooms no longer receive heavyweight bosses or disproportionate encounter caps.

## [4.15.3] - 2026-07-10

### Added

- Six fog presets with quality, height-falloff, turbulence, and sky-horizon controls.
- Adaptive third-person shot-impact crosshairs with depth cueing, target colors, and viewport clamping.
- A persistent Procedural Game setup and launch menu with deterministic seeds, theme, difficulty, and size controls.
- Mission-graph-first procedural levels with staged keys, lock-safe loops, hubs, arenas, outdoor landmarks, secrets, and guaranteed weapon progression.
- A detailed procedural-generation implementation and evaluation paper under `docs/engine/`.
- **Release Packaging**: Added a tagged GitHub release pipeline that publishes a Linux AppImage, Windows x64 packages, macOS packaging, and SHA256 checksums.
- **Windows Build Helper**: Added `tools/build-windows.ps1` to bootstrap vcpkg, build with Visual Studio 2022, and create a shareable Windows zip.
- **Windows MinGW Cross Build**: Added `tools/build-windows-mingw.sh`, a MinGW-w64 toolchain, CI coverage, and release packaging for a Linux-built Windows x64 `.exe` zip.
- **glTF Support**: Integrated glTF 2.0 model loading through `fastgltf`, including `.gltf`/`.glb`, initial skeletal animation, and material rendering fixes.

### Changed

- Main list menus now scroll with wheel, arrows, Page Up/Down, Home, and End, keeping oversized mod menus fully accessible.
- Procedural rooms now vary their cell composition, footprint, chamfers, floor elevation, clear height, surfaces, accents, lighting, landmarks, and decoration.
- Procedural encounters use gentler per-room caps, later heavy-monster tiers, stronger major-fight ammunition, and more consistent recovery support.
- Procedural menu entries are restored after gameplay mods replace the engine main menu.
- **Project Rename**: Renamed NeoDoom to BiasedDoom, including the `biaseddoom` executable, CMake variables, build scripts, and documentation.
- **SBARINFO Support**: Added custom mugshot scaling and positioning support.

### Fixed

- Tall procedural door textures no longer tile vertically; narrow door motifs are centered instead of asymmetrically cropped.
- Closed map geometry, wall winding, texture alignment, functional keyed doors, IWAD-specific actor compatibility, and deterministic map loading now have expanded regression coverage.
