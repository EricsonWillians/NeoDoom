# IWAD Discovery

BiasedDoom can locate installed game data without requiring an absolute IWAD
path. Discovery is read-only: it never copies, moves, or modifies a game
installation.

## Quick start

Print every directory searched and every validated IWAD found:

```bash
./build/biaseddoom -findiwads
```

Then either launch the normal picker:

```bash
./build/biaseddoom
```

or select a discovered IWAD by its short filename:

```bash
./build/biaseddoom -iwad doom2
```

An explicit path still works and always takes precedence:

```bash
./build/biaseddoom -iwad /path/to/DOOM2.WAD
```

`-find-iwads` and `--find-iwads` are aliases for `-findiwads`.

## What is searched

The engine combines these sources in deterministic order:

1. Direct and recursive paths in `[IWADSearch.Directories]` in the user
   configuration.
2. `DOOMWADDIR` through the default configuration entry.
3. Every directory in `DOOMWADPATH` (`:`-separated on POSIX and
   `;`-separated on Windows).
4. Platform store discovery for GOG, Steam, and the legacy Bethesda launcher
   where supported.

Candidates are checked for readability and then identified from their actual
archive contents. A random PWAD named `doom2.wad` is therefore not accepted as
an IWAD merely because its filename looks correct.

Duplicate search directories and candidate paths are suppressed. Recursive
search is only performed for paths the user explicitly marks as
`RecursivePath`; automatic Steam discovery deliberately targets known game
directories instead of walking an entire multi-terabyte library.

## Steam discovery

Steam support reads both modern and legacy `libraryfolders.vdf` layouts. It
also reads each supported game's `appmanifest_<appid>.acf`, using its
`installdir` value instead of assuming the display/install folder was never
renamed. External Steam libraries are followed from the metadata.

Recognized layouts include:

| Platform | Roots |
|----------|-------|
| Linux | Native Valve and distribution installs, including `~/.steam/debian-installation`, `~/.steam/root`, `~/.steam/steam`, and XDG locations |
| Linux sandbox packages | Steam Flatpak and Snap user-data roots, subject to sandbox filesystem permissions |
| macOS | `~/Library/Application Support/Steam` plus `STEAM_DIR`/`STEAM_HOME` |
| Windows | Steam registry locations, `Program Files` fallbacks, and `STEAM_DIR`/`STEAM_HOME` |

The shared app catalog covers Ultimate Doom, Doom II, Final Doom, the 2019 and
2024 rereleases, Doom 3 BFG IWADs, Heretic, Hexen, Deathkings, Strife, Master
Levels, and the combined Heretic + Hexen release.

For the Debian-packaged layout, for example, BiasedDoom automatically reaches:

```text
~/.steam/debian-installation/steamapps/common/Ultimate Doom/base/DOOM.WAD
~/.steam/debian-installation/steamapps/common/Ultimate Doom/base/doom2/DOOM2.WAD
~/.steam/debian-installation/steamapps/common/Ultimate Doom/base/tnt/TNT.WAD
~/.steam/debian-installation/steamapps/common/Ultimate Doom/base/plutonia/PLUTONIA.WAD
~/.steam/debian-installation/steamapps/common/Ultimate Doom/rerelease/
```

`STEAM_DIR` or `STEAM_HOME` can point at an unusual portable Steam root. Steam
library metadata beneath that root is still parsed normally.

## Custom directories

For a temporary shell-only search path:

```bash
export DOOMWADPATH="$HOME/Games/doom:/mnt/games/iwads"
./build/biaseddoom -findiwads
```

For a persistent recursive directory, add it to the configuration:

```ini
[IWADSearch.Directories]
Path=/home/me/Games/iwads
RecursivePath=/mnt/archive/doom-games
```

Prefer direct `Path` entries when possible. `RecursivePath` is intentionally
powerful and may be slow on a large tree.

## Troubleshooting

- Run with `-findiwads` and check whether the expected directory appears under
  “Direct search directories.”
- If the directory appears but the IWAD does not, check file readability and
  confirm that the file is a supported game IWAD rather than a PWAD.
- For Steam Flatpak, grant the application access to external library mounts;
  a sandbox cannot discover paths it cannot read.
- If Steam metadata is temporarily malformed while Steam is rewriting it, the
  engine continues through fallback roots rather than aborting startup.
- Explicit `-iwad /absolute/path/file.wad` remains the final override.

Maintainers can exercise modern external-library and renamed-install-folder
parsing against any supported IWAD with:

```bash
./tools/test-iwad-discovery.sh --iwad /path/to/DOOM2.WAD
```
