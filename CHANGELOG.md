# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

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
