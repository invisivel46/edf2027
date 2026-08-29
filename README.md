# Earth Defense Force 2017 — PC port (v0.2.0)

A native PC build of the Xbox 360 game *Earth Defense Force 2017* (USA/Europe),
made with static recompilation on the [ReXGlue](https://github.com/rexglue) SDK.

**This package contains no game data.** You need your own copy of the game
(Xbox 360 disc, dumped as an `.iso`, title id `445007D3`).

## Quick start (Windows)

1. Unzip anywhere, run `edf2017.exe`.
2. On first run the setup screen appears. Click **Select disc image (.iso)…**
   and pick your dump. The game is extracted (about 6 GB) into your user
   folder; a progress bar shows the copy. If you already have the disc
   extracted (a folder containing `default.xex`), use
   **Select extracted game folder…** instead.
3. The game boots straight into the title screen. Press **Start twice** on the
   title (the first press skips the intro).

Your choice is remembered; later runs boot directly.

## Settings

* **F1** in game — EDF 2017 settings: display mode, window and render
  resolution, native/ultrawide Hor+/letterboxed/stretched aspect handling, VSync, FPS cap,
  refresh rate, anisotropic filtering, MSAA, FXAA, shader compilation,
  FidelityFX CAS/FSR when supported, audio, controls, and diagnostics.
  **Save** writes them to the config file; items marked `*` need a restart.
* **F2** — toggle the compact FPS overlay.
* **F4** — advanced ReXGlue settings (every runtime option).
* **F3** — debug overlay, **`** — console.
* `edf2017.exe --settings` opens the settings screen before the game boots.

Config file: `%APPDATA%\edf2017\edf2017.toml` (Windows),
`~/.local/share/edf2017/edf2017.toml` (Linux), `~/Library/Application Support/edf2017/` (macOS).
Extracted game: `<same folder>\game\`. Delete the config file to run setup again.

## Controls

Any gamepad SDL3 recognizes (Xbox, PlayStation, Switch Pro, …) works out of the
box; `gamecontrollerdb.txt` adds community mappings. Prefer XInput with
`--input_backend xinput`.

Keyboard & mouse (enable *Keyboard & mouse controller emulation* in F1 settings):

| Key | Pad | Key | Pad |
|---|---|---|---|
| W A S D | Left stick | Arrows | Right stick |
| Shift + arrows | D-pad | Space / ; | A |
| Backspace / ' | B | L | X |
| P | Y | 1 / 3 | LB / RB |
| Q / I | LT / RT | Enter | Start |
| Tab | Back | | |

Keys are remappable in the F4 settings (Keybinds category, `keybind_*`).
Controller vibration can be enabled or disabled in the F1 settings.
Press **Escape** at any time to quit the game.

## Command line (optional)

```
edf2017.exe [--game_data_root <folder>] [--settings] [--fullscreen true|false]
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
SDL3, Dear ImGui, extract-xiso (BSD), SDL_GameControllerDB (zlib). The port's
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

On Windows, `build.cmd` performs both steps using the Visual Studio developer
environment. Set `REXGLUE_SDK` first if the SDK is not in the default local
development location. It defaults to two parallel compiler jobs to avoid
exhausting memory on the large generated sources; set `BUILD_JOBS` to override
this. Optional arguments select the preset and target:

```cmd
build.cmd
build.cmd win-amd64-debug edf2017
```

The generated recompilation C++ is tracked. To regenerate it, edit
`edf2017_manifest.toml` so `game_root` and `file_path` point to your own
extracted disc, then build the `edf2017_codegen` target. Game data must never
be committed.

To create a redistributable package, also provide `extract-xiso`:

```powershell
cmake --preset win-amd64-release `
  -DCMAKE_PREFIX_PATH=C:\path\to\rexglue-sdk `
  -DEDF2017_TOOLS=C:\path\to\extract-xiso
cmake --build out/build/win-amd64-release --target package_release
```
