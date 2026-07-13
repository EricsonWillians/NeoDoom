# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

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
