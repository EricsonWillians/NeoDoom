# AGENTS.md

This file provides essential guidance for AI coding agents working with the BiasedDoom codebase. The reader is assumed to know nothing about the project.

## Project Overview

**BiasedDoom** is a modern fork of [GZDoom](https://zdoom.org/) (version 4.15pre) that extends the classic DOOM engine with native **glTF 2.0 support**, enabling skeletal animations, PBR materials, and seamless Blender workflows while maintaining full backward compatibility with traditional DOOM assets (MD2, MD3, voxels, DECORATE/ZScript).

Key differentiators:
- Native `.gltf` and `.glb` file loading via `fastgltf`
- Skeletal animation with bone weights and blending
- PBR metallic-roughness rendering under OpenGL/Vulkan
- GPU-skinned animation for performance
- Direct Blender export workflow support

The project was previously named "NeoDoom" and was renamed to "BiasedDoom". The executable produced is `biaseddoom`.

**License**: GNU General Public License v3 (or later). Most source files carry a 3-clause BSD-style header for the original GZDoom portions.

## Technology Stack

| Layer | Technology |
|-------|-----------|
| **Language** | C++17 (primary), C (third-party/embed), Objective-C/C++ (macOS) |
| **Build System** | CMake 3.16+ |
| **Dependency Manager** | vcpkg (with manifest in `vcpkg.json`) |
| **Graphics APIs** | OpenGL, Vulkan (via ZVulkan), GLES2 |
| **Audio** | OpenAL (dynamic/static), ZMusic (internal) |
| **Windowing** | SDL2 (Linux/Windows), Cocoa (macOS native) |
| **Scripting** | ZScript (custom VM), DECORATE (legacy), ACS |
| **Model Formats** | MD2, MD3, IQM, OBJ, KVX (voxels), UE1, **glTF 2.0** |
| **Compression** | bzip2, LZMA, miniz (zip) |
| **Debugging** | cppdap (Debug Adapter Protocol) |
| **Networking** | Custom netcode (`d_net.cpp`) |

**Internal Libraries** (in `libraries/`):
- `ZMusic` — Audio/music playback system
- `ZVulkan` — Vulkan abstraction layer
- `ZWidget` — UI widget system
- `asmjit` — JIT compilation for the script VM
- `discordrpc` — Discord Rich Presence
- `cppdap` — Debug Adapter Protocol client
- `bzip2`, `lzma`, `miniz`, `webp` — Compression and image formats

## Build System

### Prerequisites

- CMake 3.16 or newer
- C++17 compiler (GCC 9+, Clang 11+, or Visual Studio 2022)
- Git
- vcpkg (bootstrapped automatically by `supreme-build.sh`)
- Platform-specific dependencies:
  - **Linux**: `libsdl2-dev`, `libvpx-dev`, `libwebp-dev`, GTK2/GTK3 dev packages
  - **macOS**: MoltenVK, Vulkan-Volk, libvpx (via Homebrew)
  - **Windows**: Visual Studio 2022 with C++ workload

### Build Commands

**Quick build (using the provided script):**
```bash
./supreme-build.sh
```

**Manual build:**
```bash
# Configure
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Release

# For development builds
cmake --build build --config Debug
```

**Clean build:**
```bash
rm -rf build/
cmake -B build -S .
cmake --build build
```

**Build script options (`supreme-build.sh`):**
- `--clean` — Remove build directory before building
- `--release` — Release mode
- `--debug` — Debug mode (default)
- `--relwithdebinfo` — RelWithDebInfo mode
- `--no-gltf` — Disable glTF support
- `--jobs N` — Use N parallel compilation jobs
- `--verbose` — Enable verbose output

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BIASEDDOOM_ENABLE_GLTF` | ON | Enable glTF 2.0 model support |
| `BIASEDDOOM_BUILD_GLTF` | ON | Build experimental glTF implementation |
| `HAVE_VULKAN` | ON | Enable Vulkan support |
| `HAVE_GLES2` | ON (OFF on macOS) | Enable GLES2 support |
| `NO_OPENAL` | OFF | Disable OpenAL sound support |
| `DYN_OPENAL` | ON | Dynamically load OpenAL |
| `OPENAL_SOFT_VCPKG` | OFF | Use OpenAL from vcpkg |
| `LIBVPX_VCPKG` | OFF | Use libvpx from vcpkg |
| `FORCE_INTERNAL_ZMUSIC` | ON | Use bundled ZMusic |
| `FORCE_INTERNAL_ASMJIT` | ON | Use bundled asmjit |
| `FORCE_INTERNAL_CPPDAP` | ON | Use bundled cppdap |
| `ZDOOM_ENABLE_SWR` | ON | Enable software renderer |
| `WITH_ASAN` | OFF | Enable Address Sanitizer (GCC/Clang) |
| `WITH_MSAN` | OFF | Enable Memory Sanitizer (Clang only) |
| `WITH_UBSAN` | OFF | Enable Undefined Behavior Sanitizer |

### vcpkg Features

Defined in `vcpkg.json`:
- `gltf-support` — Pulls in `fastgltf` for glTF 2.0 loading
- `vcpkg-libvpx` — Use vcpkg-provided libvpx
- `vcpkg-openal-soft` — Use vcpkg-provided OpenAL Soft

## Source Code Organization

The project contains approximately **1,195 source files** (~596 `.cpp`, ~574 `.h`) under `src/`.

### Top-Level Directories

| Directory | Purpose |
|-----------|---------|
| `src/common/` | Shared engine components (rendering, audio, scripting core, filesystem, textures, models, etc.) |
| `src/rendering/` | DOOM-specific rendering code (hardware and software renderers) |
| `src/playsim/` | Game simulation: actors, physics, AI, effects, ACS scripting |
| `src/scripting/` | Scripting engine: ZScript compiler, DECORATE parser, VM backend, codegen |
| `src/gamedata/` | Game data definitions: weapons, keys, map info, skills, DEHACKED, textures |
| `src/sound/` | Sound system integration |
| `src/menu/` | Menu system |
| `src/console/` | Console and command system |
| `src/maploader/` | Map loading (UDMF, nodes, polyobjects, slopes) |
| `src/intermission/` | Intermission screens |
| `src/launcher/` | Game launcher UI |
| `src/g_statusbar/` | HUD and status bar |
| `src/posix/` | POSIX-specific code (Linux, macOS) |
| `src/win32/` | Windows-specific code |
| `src/utility/` | Additional utilities including node builder |
| `src/r_data/` | Rendering data: sprites, colormaps, translations, models registry |

### Key Subsystems in `src/common/`

| Directory | Purpose |
|-----------|---------|
| `common/models/` | **Model loading system** — MD2, MD3, IQM, OBJ, KVX, UE1, **glTF 2.0** (`model_gltf.cpp/h`, `model_gltf_render.cpp`, `model_gltf_debug.cpp/h`, `model_gltf_helpers.cpp`) |
| `common/rendering/` | Rendering subsystem — OpenGL (`gl/`), GLES (`gles/`), Vulkan (`vulkan/`), hardware renderer (`hwrenderer/`) |
| `common/scripting/` | Scripting VM backend, JIT, frontend parser, DAP integration |
| `common/audio/` | Audio abstractions (sound and music) |
| `common/textures/` | Texture management, material system, PBR materials (`hw_material_pbr.cpp/h`) |
| `common/filesystem/` | WAD/PK3 virtual filesystem |
| `common/platform/` | Platform abstraction — `win32/`, `posix/sdl/`, `posix/cocoa/`, `posix/osx/`, `posix/unix/` |
| `common/engine/` | Core engine utilities (CVars, scanner, random, etc.) |
| `common/utility/` | General utilities: `TArray`, `FString`, vectors, matrices, memory allocators |
| `common/thirdparty/` | Embedded third-party code (animlib, earcut, libsmackerdec, math libs, rapidjson, utf8proc) |

### glTF-Specific Files

- `src/common/models/model_gltf.h` / `model_gltf.cpp` — Core glTF model class (`FGLTFModel`)
- `src/common/models/model_gltf_render.cpp` — glTF rendering integration
- `src/common/models/model_gltf_debug.cpp/h` — Debug visualization helpers
- `src/common/models/model_gltf_helpers.cpp` — Utility functions for glTF processing
- `src/common/rendering/hw_material_pbr.cpp/h` — PBR material system for metallic-roughness workflow
- `src/playsim/gltf_zscript.cpp` — ZScript native bindings for glTF animation control

## Code Style Guidelines

### Header Guards
- **Prefer `#pragma once`** for include guards. This is the dominant convention in the codebase (~193 files use `#pragma once` vs ~99 using `#ifndef` guards).
- Legacy files may still use `#ifndef __FILENAME__` style guards.

### Naming Conventions
- Classes: `F` prefix for engine classes (e.g., `FModel`, `FString`, `FGameTexture`)
- Structs: Often plain names or `S` prefix
- Global functions: Often `I_` for system interface, `P_` for playsim, `R_` for rendering
- Member variables: No strict prefix, but often descriptive names
- Constants: `ALL_CAPS` or `kCamelCase`
- Enums: Often plain names or `E` prefix

### Containers and Strings
- Use `TArray<T>` (custom dynamic array from `tarray.h`) instead of `std::vector`
- Use `FString` (custom string class from `zstring.h`) instead of `std::string`
- Use `TMap<K,V>` for hash maps
- Use `TDeletingArray<T*>` for arrays that own their elements

### Memory Management
- The engine uses a custom memory allocator (`M_Malloc`, `M_Free` in `m_alloc.h`)
- Many objects are garbage-collected via the `DObject` hierarchy
- Use `new`/`delete` for non-GC objects; be careful with ownership

### Math Types
- Vectors: `DVector2`, `DVector3`, `FVector2`, `FVector3`, `FVector4`
- Matrices: `VSMatrix`
- Rotations: `DRotator`
- Quaternions: `FQuat`
- Fixed-point: `fixed_t` (legacy DOOM)

### Include Style
- Use quoted includes for project headers: `#include "actor.h"`
- Use angle brackets for system/standard headers: `#include <math.h>`
- Include paths are relative to `src/` due to CMake `include_directories`

### File Organization
- One major class per file (generally)
- Header and source file names match (e.g., `model_gltf.h` / `model_gltf.cpp`)
- Platform-specific code is segregated into `common/platform/` subdirectories

## Testing Strategy

**There is no traditional unit test suite in this project.** Testing is primarily integration-based:

1. **CI/CD Builds** — GitHub Actions (`.github/workflows/continuous_integration.yml`) builds on every push and PR:
   - **Windows**: Visual Studio 2022 (Release, Debug)
   - **macOS**: macOS-14 with Xcode (Release, Debug), requires MoltenVK and Vulkan-Volk
   - **Linux**: Ubuntu-22.04 with GCC 9/12/latest and Clang 11/15/latest (multiple build types)
   - AppImage generation on Linux for distribution

2. **Manual Testing** — The engine is tested by running it with various WAD files and verifying:
   - Map loading and gameplay
   - Model rendering (glTF, MD2, MD3, etc.)
   - Script execution (ZScript, ACS)
   - Audio playback
   - Renderer correctness (OpenGL/Vulkan/software)

3. **Build Verification** — The `supreme-build.sh` script verifies the executable is produced and checks for glTF symbols via `nm`.

## Deployment / Distribution

- **Linux**: AppImage packages are generated in CI; manual installation via `cmake --install`
- **Windows**: Portable zip with `.exe` and `.pk3` files
- **macOS**: `.app` bundle
- **PK3 Files**: Built from `wadsrc/`, `wadsrc_bm/`, `wadsrc_lights/`, `wadsrc_extra/`, `wadsrc_widepix/` via CMake `add_pk3()` custom commands

## Important Files for Agents

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Root build configuration |
| `src/CMakeLists.txt` | Source-level build configuration (1,638 lines) |
| `vcpkg.json` | Dependency manifest |
| `src/version.h` | Version and build info (`4.15pre`) |
| `src/doomdef.h` | Core engine definitions and constants |
| `src/d_main.cpp` | Main entry point and game loop |
| `src/common/models/model.h` | Base model class (`FModel`) |
| `src/common/models/model_gltf.h` | glTF model class (`FGLTFModel`) |
| `src/common/rendering/hw_material_pbr.h` | PBR material definitions |
| `src/common/utility/tarray.h` | Dynamic array container |
| `src/common/utility/zstring.h` | String class |
| `src/common/utility/vectors.h` | Vector math |
| `src/common/scripting/vm/vm.h` | Script VM interface |
| `src/playsim/actor.h` | Actor base class |
| `src/gamedata/gi.h` | Game info definitions |
| `supreme-build.sh` | Automated build script with vcpkg bootstrapping |
| `CLAUDE.md` | Additional AI assistant guidance (includes glTF implementation architecture) |

## Security Considerations

- The engine loads user-provided WAD/PK3 files and now also `.gltf`/`.glb` files. Any file parsing code is a potential attack surface.
- The scripting VM (ZScript) executes user-provided code. The VM has sandboxing but native function bindings should be reviewed carefully.
- Network code (`d_net.cpp`) handles multiplayer; buffer sizes and protocol parsing should be validated.
- The project uses `stricmp`/`strnicmp` macros mapped to `strcasecmp`/`strncasecmp` on POSIX systems.
- See `SECURITY.md` for vulnerability reporting (references upstream GZDoom security policy).

## Platform-Specific Notes

**Windows**:
- Static linking with MSVC runtime (`/MT`)
- Prebuilt libvpx in `bin/Windows/vpx/`
- Uses Win32 APIs for input, windowing, and crash handling

**macOS**:
- Can use native Cocoa backend (`OSX_COCOA_BACKEND=ON`) or SDL2
- Requires MoltenVK and Vulkan-Volk for Vulkan support
- Uses `.mm` files for Objective-C++ integration

**Linux**:
- SDL2 for windowing and input
- GTK2/GTK3 for IWAD picker dialog (can be disabled with `NO_GTK`)
- Position-independent executable (`-fPIE`) enabled by default

## Development Workflow Tips

- Use the `supreme-build.sh` script for the easiest first-time build experience; it bootstraps vcpkg automatically.
- If modifying glTF code, ensure `BIASEDDOOM_ENABLE_GLTF=ON` (default).
- The `FASTMATH_SOURCES` list in `src/CMakeLists.txt` marks files compiled with fast-math flags; be careful with floating-point assumptions in those files.
- Precompiled headers are used for a large set of common source files (`PCH_SOURCES` in `src/CMakeLists.txt`); adding new commonly-included headers may benefit from PCH inclusion.
- Generated files (`xlat_parser.c`, `zcc-parse.c`, `sc_man_scanner.h`) are produced by `lemon` and `re2c` tools during build.
- The `revision_check` CMake target updates `src/gitinfo.h` from git metadata on every build.
