# BiasedDoom

![BiasedDoom launcher banner](wadsrc/static/widgets/banner.png)

## Next-Generation Modding for the Classic DOOM Engine

[![Build Status](https://github.com/ericsonwillians/BiasedDoom/actions/workflows/ci.yml/badge.svg)](https://github.com/ericsonwillians/BiasedDoom/actions/workflows/ci.yml)

BiasedDoom is a modern fork of **GZDoom** that expands the engine with **native glTF 2.0 support**, enabling skeletal animations, PBR materials, and seamless workflows with **Blender** and other 3D tools.  
Our mission: preserve the soul of DOOM while empowering modders with next-gen asset pipelines.

> [!IMPORTANT]
> **Disclaimer regarding AI-Generated Code**
> This project unashamedly leverages AI assistance for development. We prioritise results and functionality over the origin of the code. If you have a philosophical objection to AI-generated code, this project is not for you, and we kindly suggest you look elsewhere.

Special thanks to Coraline of the EDGE team for allowing us to use her [README.md](https://github.com/3dfxdev/EDGE/blob/master/README.md) as a template for this one.

### Source code licensed under the GPL v3
##### https://www.gnu.org/licenses/quick-guide-gplv3.en.html
---

## Features

- **glTF 2.0 Import**  
  Load `.gltf` and `.glb` files directly, no conversions required.  

- **Skeletal Animation**  
  Full support for armatures, multiple animations, bone weights, and blending.  

- **PBR Materials**  
  Metallic-roughness workflow for realistic rendering under OpenGL/Vulkan.  

- **Blender Workflow**  
  Export directly from Blender with the official glTF 2.0 exporter.  

- **Backward Compatibility**  
  Keep using MD2/MD3, voxels, and classic DECORATE/ZScript definitions.  

- **GPU Acceleration**  
  Hardware-skinned animation for smoother performance.  

---

## 📦 Installation

### Quick Start (Linux / macOS)

```bash
# 1. Clone the repository
git clone https://github.com/YOURNAME/BiasedDoom.git
cd BiasedDoom

# 2. Run the installer-assisted build script
./build.sh --release --clean --install

# 3. Launch from the install location
"${HOME}/.local/bin/biaseddoom"
```

`build.sh` now behaves as a user-friendly installer:

- performs preflight checks (compiler/CMake/packages, git repo health, missing deps)
- bootstraps/fixes [vcpkg](https://vcpkg.io/) when needed
- configures and compiles the project
- optionally installs to `~/.local` (or a custom prefix)

Useful shortcuts:

- `./build.sh --check` validates system dependencies and repo health without building.
- `./build.sh --deps-only --auto-install-deps` validates system dependencies only, and installs missing packages for you.
- `./build.sh --repair --auto-install-deps --release --clean` validates and fixes dependencies, then builds immediately.
- `./build.sh --release --install --install-prefix /usr/local` performs a full install to a custom path.
- `./build.sh --release --clean` builds without installation.
- `./build.sh --configure-only`, `./build.sh --build-only`, and `./build.sh --run` are available for advanced workflows.

### Dependency failures are handled by the installer

- If the build detects missing dependencies, it prints exact package commands for your current distro and optional alternatives.
- You can let the installer install them for you with:
  - `./build.sh --auto-install-deps`
  - `./build.sh --auto-install-deps --yes`
- If a repository is stale or broken, the script prints focused remediation steps before you continue.

---

### Launcher branding
The launcher top banner shown in the header is:

- `wadsrc/static/widgets/banner.png`
- Loaded with `Image::LoadResource("widgets/banner.png")` in `src/launcher/launcherbanner.cpp`
- Used in `src/launcher/launcherwindow.cpp` as the launcher header region.

To replace it with your BiasedDoom banner:

- Replace `wadsrc/static/widgets/banner.png` (PNG preferred).
- Rebuild:
  - `./build.sh --clean --release` (full clean/rebuild, safest)
  - `./build.sh --build-only` (skip configure when source build cache is already valid)
- For quick image-only iteration, use `./build.sh --build-only` when you already have a valid configured cache.
- If your banner has unusual dimensions (very wide/tall), this is the authoritative tweak point:
  - `src/launcher/launcherbanner.cpp` and `src/launcher/launcherwindow.cpp`.

The launcher uses `ImageBoxMode::Contain`, so it preserves aspect ratio:

- Wider image → centered with side padding.
- Taller image → centered with top/bottom padding.

For larger/new aspect-ratio banners, tune these constants before rebuilding:

- `src/launcher/launcherbanner.cpp`:
  - `BannerWidthUtilization`
  - `BannerHeightFloorFraction`
  - `BannerHeightWideFloorFraction`
  - `BannerHeightCeilFraction`
  - `BannerPreferredHeightFraction`
  - `BannerWideAspectThreshold`
  - `BannerTallAspectThreshold`
  - `BannerHeightFallbackAspect`
- `src/launcher/launcherwindow.cpp`:
  - `DefaultLauncherWidth` and `DefaultLauncherHeight` in `LauncherWindow::ExecModal()`.
- The launcher title text uses `GAMENAME` from `src/version.h`.

If you are specifically replacing old "gzdoom"-style branding:

- Keep `#define GZDOOM 1` in `src/version.h` for compatibility unless you have a hard dependency reason not to.
- Change the visible product name in:
  - `src/version.h`: `GAMENAME`, `WGAMENAME`, `GAMENAMELOWERCASE`, `APPID`
  - `src/launcher/launcherwindow.cpp` (window title uses `GAMENAME`)
  - `src/launcher/playgamepage.cpp` (welcome/version label uses `GAMENAME`)
  - `src/win32/zdoom.rc` (Windows binary metadata and fatal error title bar)
  - `src/posix/osx/zdoom-info.plist` (app bundle display metadata)
  - `src/posix/freedesktop/org.drdteam.biaseddoom.desktop` / `org.drdteam.biaseddoom.metainfo.xml`
- Keep legacy filenames such as `zdoom.rc`, `zdoom.xpm`, and `zdoom-info.plist`; they are historical and not user-visible when packaged.

### Other user-facing branding files

If you also want to replace the app icons and metadata:

- Windows icon: `src/win32/icon1.ico`
- Windows binary metadata (`FileDescription`, title bar, fatal error dialog): `src/win32/zdoom.rc`
- macOS app metadata and icon: `src/posix/osx/zdoom-info.plist`, `src/posix/osx/zdoom.icns`
- Linux desktop assets:
  - `src/posix/freedesktop/org.drdteam.biaseddoom.svg`
  - `src/posix/freedesktop/org.drdteam.biaseddoom.desktop`
  - `src/posix/freedesktop/org.drdteam.biaseddoom.metainfo.xml`
  - `src/posix/freedesktop/org.drdteam.biaseddoom-mime.xml`

#### Quick rebrand checklist

Use this quick pass:

- Replace `wadsrc/static/widgets/banner.png`.
- Replace icons above if desired.
- Keep `src/version.h` values aligned (`GAMENAME`, `WGAMENAME`, `GAMENAMELOWERCASE`, `APPID`, `BASEWAD`) if you want strings to show “BiasedDoom”.
- Rebuild:
  - `./build.sh --clean --release`

#### Where old "gzdoom" names are safe to keep

- `#define GZDOOM 1` in `src/version.h` is kept for compatibility.
- File names in source tree such as `zdoom.rc` / `zdoom.xpm` / `zdoom-info.plist` are historical and harmless; only user-facing strings and assets above are what players see.

Branding references to rename from "GZDoom"-style defaults are in:

- Core text labels
  - `src/version.h`
    - `GAMENAME` (`BiasedDoom`)
    - `WGAMENAME`
    - `GAMENAMELOWERCASE`
    - `APPID` (`org.drdteam.biaseddoom`)
- Window/title and welcome screen strings
  - `src/launcher/launcherwindow.cpp` (`GAMENAME` in window title)
- `src/launcher/playgamepage.cpp` (`GAMENAME` in welcome text)
- `src/version.h` (`GAMENAME`, `WGAMENAME`, `GAMESIG`, `BASEWAD`, `APPID`)
- Install/runtime naming and desktop metadata
  - `src/posix/freedesktop/org.drdteam.biaseddoom.desktop`
  - `src/posix/freedesktop/org.drdteam.biaseddoom.metainfo.xml`
  - `src/posix/freedesktop/org.drdteam.biaseddoom.svg`
- Platform icons
  - `src/win32/zdoom.rc` + `src/win32/icon1.ico`
  - `src/posix/osx/zdoom.icns`
  - `src/posix/freedesktop/org.drdteam.biaseddoom.svg`

Note:
- `#define GZDOOM 1` in `src/version.h` is a compatibility macro and should be left as-is unless upstream compatibility requirements change.

### Top-level branding quick references

- **Application name string**: `GAMENAME` in `src/version.h`
- **Window title**: `src/launcher/launcherwindow.cpp`
- **Launcher version string**: `GetVersionString()` in `src/launcher/playgamepage.cpp`
- **Windows executable metadata**: `src/win32/zdoom.rc`
- **Linux desktop name/metadata**: `src/posix/freedesktop/org.drdteam.biaseddoom.desktop`
- **Linux metadata XML**: `src/posix/freedesktop/org.drdteam.biaseddoom.metainfo.xml`
- **Legacy compat leftovers** (safe): `zdoom.rc`, `zdoom-info.plist`, `zdoom.xpm`

For logo/icon files that are directly visible to players:

- `src/posix/zdoom.xpm` (old X11 fallback icon, safe legacy)
- `src/posix/osx/zdoom.icns` (macOS icon file; can be replaced with your BiasedDoom icon without renaming)
- `src/win32/icon1.ico` (Windows application icon)
- `src/posix/freedesktop/org.drdteam.biaseddoom.svg` (Linux desktop icon)

---

### Prerequisites

| Tool | Minimum Version | Notes |
|------|-----------------|-------|
| CMake | 3.16 | Build system generator |
| C++ Compiler | GCC 9+ / Clang 11+ / MSVC 2022 | C++17 support required |
| Git | any | For cloning vcpkg and submodules |
| Ninja or Make | any | Ninja recommended for faster builds |

> ⚠️ **Important:** On Linux you need the **development** (`-dev`) packages, not just the runtime libraries. If CMake fails with "Could NOT find SDL2" or "Package 'glib-2.0' not found", install the packages below.

**Platform-specific packages:**

- **Linux (Debian / Ubuntu)**
  ```bash
  sudo apt update
  sudo apt install -y build-essential cmake git ninja-build \
      libsdl2-dev libvpx-dev libwebp-dev libgtk-3-dev \
      libglib2.0-dev
  ```

- **Linux (Fedora)**
  ```bash
  sudo dnf install -y gcc-c++ cmake git ninja-build \
      SDL2-devel libvpx-devel libwebp-devel gtk3-devel \
      glib2-devel
  ```

- **macOS**
  ```bash
  # Install Xcode Command Line Tools
  xcode-select --install

  # Install dependencies via Homebrew
  brew install cmake ninja sdl2 libvpx webp moltenvk vulkan-volk
  ```

- **Windows**
  - Install [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload.
  - Install [Git for Windows](https://git-scm.com/download/win).
  - Open a **Developer Command Prompt for VS 2022** and run:
    ```cmd
    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg\scripts\buildsystems\vcpkg.cmake
    cmake --build build --config Release
    ```

---

### Build Options

The `build.sh` script supports the following flags:

| Flag | Description |
|------|-------------|
| `--check` | Run dependency and repo diagnostics only |
| `--deps-only` | Check/install system dependencies only, then exit |
| `--repair` | Install missing dependencies automatically and continue with build |
| `--clean` | Remove the build directory before configuring |
| `--release` | Optimised release build |
| `--debug` | Debug build with full symbols (default) |
| `--relwithdebinfo` | Release build with debug symbols |
| `--no-gltf` | Disable glTF 2.0 support |
| `--install` | Install after successful build |
| `--install-prefix PATH` | Install into a custom prefix (defaults to `~/.local`) |
| `--jobs N` | Use `N` parallel compilation jobs (default: auto-detect) |
| `--auto-install-deps` | Prompt to install missing system libraries or show distro-specific commands |
| `--verbose` | Enable verbose CMake / compiler output |

You can also set environment variables instead of flags:

```bash
BUILD_TYPE=Release NUM_JOBS=8 ./build.sh
```

For a polished dependency setup flow on Linux, run:

```bash
./build.sh --auto-install-deps
```

If you prefer manual control, keep running `./build.sh` normally; it will print exact per-distro install commands when any required package is missing.

---

## 🚀 Releasing BiasedDoom (GitHub Releases)

BiasedDoom uses a GitHub Actions release pipeline in [`.github/workflows/release.yml`](.github/workflows/release.yml).

Recommended release flow (one command):

1. Bump version in source:

   ```bash
   ./tools/release.sh --minor
   ```

   Or choose a specific version:

   ```bash
   ./tools/release.sh --set 4.15.1
   ```

   This script will:

   - bump `VERSIONSTR` in `src/version.h`
   - refresh launcher/product version metadata (`RC_*` and macOS bundle version fields)
   - commit `src/version.h`
   - create tag `vX.Y.Z`
   - push changes + tag
   - trigger release workflow

2. Optional release modes:

   - Draft release:

     ```bash
     ./tools/release.sh --minor --draft
     ```

   - Prerelease:

     ```bash
     ./tools/release.sh --minor --prerelease
     ```

   - Both draft and prerelease:

     ```bash
     ./tools/release.sh --minor --draft --prerelease
     ```

3. The release workflow publishes:

   - Linux AppImage + tar bundle
   - macOS `.app` bundle tar bundle
   - Windows executable tar bundle
   - SHA-256 checksums for every package
   - GitHub release notes (from `CHANGELOG.md` if available)

You can also run the workflow manually from **Actions → Release** with the same version:

```bash
gh workflow run Release --field version=4.15.1
gh workflow run Release --field version=4.15.1 --field release_draft=true
gh workflow run Release --field version=4.15.1 --field release_prerelease=true
gh workflow run Release --field version=4.15.1 --field release_draft=true --field release_prerelease=true
```

Before shipping, keep these source strings up to date:

- `src/version.h` (`VERSIONSTR`, `VER_*`, and file version macros)
- `src/win32/zdoom.rc` (launcher/product metadata)

`GetVersionString()` already uses `VERSIONSTR` as a safe fallback, so launcher text continues to show a valid version even when git metadata is unavailable.

---

### Manual Build (without `build.sh`)

If you prefer to run CMake directly:

```bash
# 1. Bootstrap vcpkg (one-time)
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh

# 2. Configure
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DBIASEDDOOM_ENABLE_GLTF=ON \
    -DHAVE_VULKAN=ON

# 3. Build
cmake --build build --config Release --parallel $(nproc)
```

---

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BIASEDDOOM_ENABLE_GLTF` | `ON` | Enable glTF 2.0 model support |
| `BIASEDDOOM_BUILD_GLTF` | `ON` | Build experimental glTF implementation |
| `HAVE_VULKAN` | `ON` | Enable Vulkan renderer |
| `HAVE_GLES2` | `ON` (Linux/Windows) | Enable GLES2 renderer |
| `NO_OPENAL` | `OFF` | Disable OpenAL audio |
| `DYN_OPENAL` | `ON` | Dynamically load OpenAL |
| `WITH_ASAN` | `OFF` | Enable Address Sanitizer (GCC/Clang) |
| `WITH_UBSAN` | `OFF` | Enable Undefined Behaviour Sanitizer |

---

## 🎮 Running BiasedDoom

After a successful build, the executable is located at:

```
build/biaseddoom
```

You will need at least one **IWAD** file (the main game data) to play:

| Game | IWAD File |
|------|-----------|
| DOOM | `DOOM.WAD` or `DOOM1.WAD` |
| DOOM II | `DOOM2.WAD` |
| Final DOOM | `TNT.WAD` or `PLUTONIA.WAD` |
| Heretic | `HERETIC.WAD` |
| Hexen | `HEXEN.WAD` |

Place the IWAD in the same directory as the executable, or launch with:

```bash
./biaseddoom -iwad /path/to/DOOM2.WAD
```

Load mods (PK3, WAD, etc.) with:

```bash
./biaseddoom -file /path/to/mymod.pk3
```

---

## 🔧 Blender → BiasedDoom Workflow

1. **Create Your Model in Blender**  
   - Rig your mesh with armatures.  
   - Apply transforms (`Ctrl+A → Apply All Transforms`).  

2. **Export to glTF 2.0**  
   - `File → Export → glTF 2.0 (.glb)`  
   - Recommended settings:  
     - Format: Binary `.glb`  
     - ✓ Apply Modifiers  
     - ✓ Export Materials  
     - ✓ Export Animations  

3. **Use in BiasedDoom**  
   Define the model in your actor with ZScript/DECORATE:  

   ```cpp
   model MyCyberDemon
   {
       path = "models/cyberdemon.glb"
       animation = "Idle"
       scale = 1.0
   }
   ```

For a detailed step-by-step tutorial, see [`GLTF_QUICK_START.md`](GLTF_QUICK_START.md).

---

## 📚 Documentation

- [`GLTF_QUICK_START.md`](GLTF_QUICK_START.md) — Getting started with glTF models
- [`GLTF_BEGINNER_TUTORIAL.md`](GLTF_BEGINNER_TUTORIAL.md) — Beginner-friendly glTF tutorial
- [`GLTF_IMPLEMENTATION.md`](GLTF_IMPLEMENTATION.md) — Technical implementation details
- [`GLTF_V2_IMPROVEMENTS.md`](GLTF_V2_IMPROVEMENTS.md) — V2 improvements overview
- [`CHANGELOG.md`](CHANGELOG.md) — Release notes

---

## 🤝 Contributing

Contributions are welcome! Please open an issue or pull request on GitHub.  
When modifying code, follow the existing style conventions:

- Prefer `#pragma once` for header guards.
- Use `TArray<T>` and `FString` instead of `std::vector` / `std::string`.
- Prefix engine classes with `F` (e.g., `FModel`, `FString`).

---

## 📄 License

BiasedDoom is licensed under the **GNU General Public License v3** (or later).  
Original GZDoom portions retain their respective BSD-style headers.

---

*Happy modding!* 🚀
