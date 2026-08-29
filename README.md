# EDF2027 — Earth Defense Force 2017 PC port (v0.2.0)

A native PC build of the Xbox 360 game *Earth Defense Force 2017* (USA/Europe),
made with static recompilation on the [ReXGlue](https://github.com/rexglue) SDK.

**This package contains no game data.** You need your own copy of the game
(Xbox 360 disc, dumped as an `.iso`, title id `445007D3`).

The dump this port is developed against:

| | |
|---|---|
| Size | 7,835,492,352 bytes |
| SHA-1 | `3F5DADC9BD11399E3EF260018A058F420EAFB1F6` |

Other dumps of the same release should work — the setup screen checks the title
id in `default.xex`, not the image hash — but this is the one that has been
tested. Verify yours with `Get-FileHash -Algorithm SHA1 <file>.iso` on Windows
or `sha1sum <file>.iso` elsewhere.

## Quick start (Windows)

1. Unzip anywhere, run `edf2027.exe`.
2. On first run the setup screen appears. Click **Select disc image (.iso)…**
   and pick your dump. The game is extracted (about 6 GB) into your user
   folder; a progress bar shows the copy. If you already have the disc
   extracted (a folder containing `default.xex`), use
   **Select extracted game folder…** instead.
3. The game boots straight into the title screen. Press **Start twice** on the
   title (the first press skips the intro).

## OS X

 ... Soon
## Linux 
 ... Soon

## Settings

* **F1** in game — EDF2027 settings: display mode, window and render
  resolution, native/ultrawide Hor+/letterboxed/stretched aspect handling, VSync, FPS cap,
  refresh rate, anisotropic filtering, MSAA, FXAA, shader compilation,
  FidelityFX upscaling when supported, audio, controls, and diagnostics.
  **Save** writes them to the config file; items marked `*` need a restart.
  Upscaling offers CAS, FSR 1.0, and the experimental FSR 2/FSR 3 paths, with
  sharpness, EASU pass count, and (FSR 2/3 only) a quality mode. FSR only
  upscales when the game renders below the output size, so pair it with a lower
  render resolution scale — at 1:1 it just sharpens.
* **F2** — toggle the compact FPS overlay.
* **F4** — advanced ReXGlue settings (every runtime option).
* **F3** — debug overlay, **`** — console.
* `edf2027.exe --settings` opens the settings screen before the game boots.

Config file: `%APPDATA%\edf2027\edf2027.toml` (Windows),
`~/.local/share/edf2027/edf2027.toml` (Linux), `~/Library/Application Support/edf2027/` (macOS).
Extracted game: `<same folder>\game\`. Delete the config file to run setup again.

## Controls

Any gamepad SDL3 recognizes (Xbox, PlayStation, Switch Pro, …) works out of the
box; `gamecontrollerdb.txt` adds community mappings. Prefer XInput with
`--input_backend xinput`.

Keyboard & mouse (enable *Keyboard & mouse controller emulation* in F1 settings):

| Key | Action | Key | Action |
|---|---|---|---|
| W A S D | Move | I J K L | Look (mouse-look fallback) |
| Mouse | Look (right stick) | Arrows | D-pad |
| Left mouse / Ctrl / X | Fire (RT) | Right mouse / Alt / Z | Zoom (LT) |
| Space | Jump / roll (A) | R | Reload (B) |
| Q | Change weapon (X) | E | Enter vehicle (Y) |
| 1 / 3 | Radio chat (LB / RB) | F / C | Stick press (L3 / R3) |
| Enter | Pause (Start) | Tab | Retire (Back) |

Everything above is rebindable in the F1 settings under **Controls → Key
bindings**: *Set* replaces a binding, *Add* gives an action a second key, and
each mouse button can be pointed at any pad action. Bindings apply immediately
and are written to the config file on **Save**. Modifiers are matched exactly,
so a binding of `W` does not fire while Shift is held — bind `Shift+W` for that.
The same settings are also editable as raw `keybind_*` cvars in the F4 overlay.

Controller vibration can be enabled or disabled in the F1 settings.
Press **Escape** at any time to quit the game; it cannot be rebound.

## Command line (optional)

```
edf2027.exe [--game_data_root <folder>] [--settings] [--fullscreen true|false]
            [--window_width N --window_height N] [--draw_resolution_scale_x N --draw_resolution_scale_y N]
            [--vsync true|false] [--edf_fps_cap N] [--audio_mute true|false] [--log_file run.log]
            [--edf_show_fps true|false] [--edf_frametime_log true|false] [--edf_rumble true|false]
            [--edf_trace_input true|false] [--edf_frame_pacer_spin_us N]
```
Every option in the config file can also be given as `--name value`.

## Troubleshooting

* *"That disc image is not Earth Defense Force 2017"*: the title id in
  `default.xex` did not match `445007D3`; only the USA/Europe release is supported.
* Black window / GPU error: the Xenos GPU plugin needs a D3D12-capable GPU on
  Windows (Vulkan on Linux/macOS).
* Logs: `--log_file run.log` writes next to the exe.

## Licenses

`LICENSES/` contains the licenses of the bundled components: ReXGlue SDK,
SDL3, Dear ImGui, AMD FidelityFX SDK (MIT), SDL_GameControllerDB (zlib). The port's
own code is provided as-is; the game and its data remain the property of
D3 Publisher / Sandlot.

## Building from source

Requirements: CMake 3.25 or newer, Ninja, Clang, and ReXGlue SDK 0.10.0.
Point `CMAKE_PREFIX_PATH` at an installed SDK (or set `REXSDK_DIR` to an SDK
source checkout), then configure and build:

```powershell
cmake --preset win-amd64-release -DCMAKE_PREFIX_PATH=C:\path\to\rexglue-sdk
cmake --build --preset win-amd64-release
```

The FidelityFX CAS/FSR settings need an SDK built with
`-DREXGLUE_ENABLE_FIDELITYFX=ON`; against any other SDK the port builds and runs
normally but those controls stay disabled. Configure prints which case applies.
Note that `find_package` caches `rexglue_DIR`, so switching a build directory
between SDKs needs its `CMakeCache.txt` deleted first.

On Windows, `build.cmd` performs both steps using the Visual Studio developer
environment. Set `REXGLUE_SDK` first if the SDK is not in the default local
development location. It defaults to two parallel compiler jobs to avoid
exhausting memory on the large generated sources; set `BUILD_JOBS` to override
this. Optional arguments select the preset and target:

```cmd
build.cmd
build.cmd win-amd64-debug edf2027
```

The generated recompilation C++ is tracked. To regenerate it, edit
`edf2017_manifest.toml` so `game_root` and `file_path` point to your own
extracted disc, then build the `edf2017_codegen` target. Game data must never
be committed.

Disc-image extraction is built in (`src/xdvdfs.h` reads the XDVDFS game
partition directly), so a release package needs no external tools:

```powershell
cmake --build out/build/win-amd64-release --target package_release
```
