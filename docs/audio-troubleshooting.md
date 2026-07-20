# Audio Troubleshooting

BiasedDoom's official Windows packages include OpenAL Soft and the compressed
audio decoders used by ZMusic. They do not need separately downloaded
`openal32.dll`, `sndfile.dll`, or `libmpg123-0.dll` files. Mixing DLLs copied
from another source port can select an incompatible ABI or omit the codecs a
mod needs, so remove those loose files from the BiasedDoom directory.

## Create a diagnostic log

When OpenAL cannot initialize, BiasedDoom automatically creates
`%LOCALAPPDATA%\biaseddoom\biaseddoom-audio.log` on Windows (or
`~/.config/biaseddoom/biaseddoom-audio.log` on Linux). It includes decoder
status and subsequent automatic recovery attempts. For a complete report even
when startup succeeds, open Command Prompt in the extracted game directory and
run:

```cmd
biaseddoom.exe -iwad C:\Games\Doom\DOOM2.WAD -stdout -audiodiagnostics -norun
```

The process initializes the audio stack, writes the same automatic AppData log,
and exits. If AppData cannot be written, BiasedDoom tries
`biaseddoom-audio.log` beside the executable and tells you which path worked.
The report contains:

- whether OpenAL is linked into the executable or loaded from a DLL;
- the selected playback endpoint, OpenAL vendor/version, extensions, and
  available sources;
- whether libsndfile and the optional standalone mpg123 decoder path are
  compiled and available;
- the complete device list, with the active and configured entries marked.

Attach that log when reporting a problem. To choose a different destination,
pass an absolute logfile path, for example
`+logfile "%USERPROFILE%\Desktop\biaseddoom-audio.log"` in Command Prompt.

## Recover from a stale device setting

An audio endpoint can disappear after a GPU-driver update, switching between
HDMI and motherboard audio, or unplugging a USB headset. BiasedDoom falls back
to the Windows default endpoint when the configured one cannot be opened. To
make that choice permanent, open the console, set `snd_aldevice Default`, and
run `snd_reset` or restart the game.

If an active endpoint disappears while the game is running, BiasedDoom uses
OpenAL Soft's in-place device reopen support so loaded buffers, playing
channels, and music streams survive the switch. When no endpoint is available
yet, it keeps silent output alive and retries with bounded backoff instead of
remaining permanently on the null renderer. Recovery attempts and their ALC
errors are written to the console and active logfile.

Use `snd_listdrivers` to list the endpoints OpenAL can see. `snd_status` prints
the current backend plus decoder status and records it in an active logfile.

Bundled OpenAL Soft normally uses Windows WASAPI and also includes its
DirectSound backend. If the default endpoint still cannot be opened, a useful
one-session diagnostic is to force DirectSound from Command Prompt:

```cmd
set ALSOFT_DRIVERS=dsound
biaseddoom.exe -iwad C:\Games\Doom\DOOM2.WAD -stdout -audiodiagnostics -norun +logfile biaseddoom-dsound.log
set ALSOFT_DRIVERS=
```

The diagnostic report records any `ALSOFT_DRIVERS` override. If DirectSound
works while the normal launch does not, attach both logs to the report; that
isolates the remaining problem to the WASAPI/driver path.

## Diagnose a custom sound

If a mod's logical sound name is known, run these commands in the console:

```text
snd_status
cachesound mod/soundname
playsound mod/soundname
```

When a resource cannot be decoded, BiasedDoom now logs the logical sound name
and its WAD/PK3 path instead of silently substituting the empty sound. Official
Windows builds exercise real OGG and FLAC files during CI, and MinGW packages
are rejected if they regain loose OpenAL or decoder DLL dependencies.

## Maintainer regression probe

The Windows build helpers enable `BIASEDDOOM_BUILD_AUDIO_TESTS` and run
`biaseddoom-audio-probe`. The probe initializes OpenAL, creates a playback
source, reopens the active device in place while preserving that source, and
decodes real OGG and FLAC fixtures. The Linux-hosted MinGW helper runs the
Windows probe under Wine when Wine is installed; otherwise it still verifies
that OpenAL Soft and every compressed-audio codec are statically linked. For a
manual native build, configure with that option and run the probe against the
repository fixtures:

```bash
cmake -S . -B build -DBIASEDDOOM_BUILD_AUDIO_TESTS=ON
cmake --build build --target biaseddoom-audio-probe
ALSOFT_DRIVERS=null ./build/biaseddoom-audio-probe \
  wadsrc/static/sounds/dsquake.ogg \
  wadsrc/static/sounds/dssecret.flac
```

`ALSOFT_DRIVERS=null` selects OpenAL Soft's headless output only for the test;
normal game launches continue to use the configured Windows playback endpoint.
