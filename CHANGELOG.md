# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Changed
- **Project Rename**: The project has been renamed from "NeoDoom" to "BiasedDoom".
  - Executable is now `biaseddoom`.
  - Build scripts updated to produce `biaseddoom` binary.
  - CMake variables and macros updated to `BIASEDDOOM_` prefix.
  - All documentation updated to reflect the new name.
- **SBARINFO Support**: Added custom mugshot scaling and positioning support (from previous commits).

### Added
- **Release Packaging**: Added a tagged GitHub release pipeline that publishes a Linux AppImage, a Windows x64 zip with `biaseddoom.exe`, macOS packaging, and SHA256 checksums.
- **Windows Build Helper**: Added `tools/build-windows.ps1` to bootstrap vcpkg, build with Visual Studio 2022, and create a shareable Windows zip.
- **Windows MinGW Cross Build**: Added `tools/build-windows-mingw.sh`, a MinGW-w64 toolchain, CI coverage, and release packaging for a Linux-built Windows x64 `.exe` zip.
- **glTF Support**: Complete integration of glTF 2.0 model loading using `fastgltf`.
  - Support for .gltf and .glb files.
  - Initial skeletal animation support.
  - Material rendering fixes.
