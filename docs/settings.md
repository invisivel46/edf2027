# Settings, controls and command line

The full reference behind the README's short version. Back to the [README](../README.md).

## Settings (F1)

**F1** in game, or **Back + Start** on a controller (configurable), opens the
settings menu. While it is open the game is paused: the simulation, the game
clock and (by default) the sound stop, the mouse is freed, and neither the
keyboard, the mouse nor the pad reach the game. Resume with the Resume button,
Esc, F1, B or Start.

- Sections: Display, Graphics, Performance, Controls (with the key-binding
  table), Audio and Advanced.
- Most settings apply immediately. The ones the game reads at startup carry a
  **RESTART** badge, and the bottom bar offers *Restart now*.
- Display-mode and window-size changes revert after 10 seconds unless you keep
  them.
- Graphics has Performance, Balanced, Quality and Ultra presets. Every section
  can be reset to its defaults.
- Settings are saved when you resume.
- With a controller, LB and RB switch sections, A selects and B goes back.

Other keys:

- **F2** shows or hides the performance overlay: frame rate, a frame-time
  graph, and CPU and GPU time. Its level of detail is set in F1 >
  Performance.
- **F4** opens the advanced ReXGlue settings (every runtime option).
- **`** opens the EDF2027 console: spawn enemies, collapse buildings, set off
  explosions, read performance numbers, change any setting, and run command scripts
  (see [console.md](console.md)). The console's game commands are cheats and
  mark the run as such.
- **F3** opens the debug overlay, and **F9** the ReXGlue log console.
- `edf2027.exe --settings` opens the settings before the game boots.

### Frame rate and latency

*Performance > Frame rate* offers 60 (locked, the original), 120, 144, 165, 240
or Uncapped. The game always simulates at 60 Hz. Above 60, the extra frames
are drawn in between and motion is smoothed, which adds up to one simulation
tick (16.7 ms) of delay. This needs Direct3D 12 and is still marked
experimental. Menus stay at about 60 FPS. The details are in
[framerate-unlock.md](framerate-unlock.md).

- **VSync** (`edf_native_vsync`, on by default) waits for the display's
  refresh. With VSync on, the display's refresh rate is the ceiling. With it
  off, frames go out as soon as they are ready, and a G-Sync or FreeSync
  display stays smooth.
- **Low latency** (`edf_low_latency`, on by default) shows the newest frame as
  late as the display allows. With VSync on, it keeps the game at most one
  frame ahead of the display. Measured with VSync on, uncapped, aiming with the
  mouse: the median input-to-photon time went from 42 ms to 18 ms, and the
  99th percentile went from 52 ms to 29 ms. With VSync on and a frame rate
  above the display's, it can cost frame rate when the GPU is the limit. With
  VSync off, only the mouse part applies (aim takes the newest motion): the
  presentation part would cost about 5% of uncapped frame rate for no gain.
- **Frame pacing** sets how a cap is held with VSync off: *Even* for steady
  frame times, or *Low latency*.
- **CPU priority** keeps the game's two busiest threads on the performance
  cores of hybrid Intel CPUs (the default).

### Graphics

- **Renderer.** *Native* (`edf_native_renderer=native`, the default) is the
  port's own full-frame renderer. It is faster, and FSR needs it. *Classic*
  (`off`) replays the game's original draw calls, for comparison or if Native
  shows a problem.
- **Graphics API.** Direct3D 12 is the default. *Direct3D 11 (fallback)* runs
  the Classic renderer at 60 FPS only.
- **Upscaling / anti-aliasing.** *Native AA* runs AMD FSR 3.1 as temporal
  anti-aliasing at full resolution (`edf_native_fsr=native_aa`), with an
  adjustable sharpness. The Quality, Balanced, Performance and Ultra
  performance choices currently also run as Native AA, because render scaling
  is not implemented yet. Turning FSR on needs a restart, because it switches
  the scene to single-sampled rendering. FSR is loaded from
  `amd_fidelityfx_dx12.dll` in the game's folder. The release ships AMD's
  signed build of that DLL.
- **Scene anti-aliasing.** The game's default 2x MSAA, off, 2x or 4x.
- **Texture filtering.** The game's default, or anisotropic filtering from 1x
  to 16x.

### Resolution and aspect ratio

Any window size or fullscreen resolution works, and the game renders in the
window's shape by default:

- **Aspect ratio** (`edf_aspect`). *Fill window* shows more at the sides on
  screens wider than 16:9 (21:9, 32:9) and more above and below on narrower
  ones (16:10, 4:3). The other choices are *Ultrawide* (pure Hor+),
  *Letterbox* (16:9 with bars) and *Stretch*.
- **HUD and menus** (`edf_hud_safe_area`). *16:9* keeps the HUD, menus, text
  and videos in the centred 16:9 area at their own shape. *Full* stretches
  them over the whole screen.
- **Render resolution.** The size the game is drawn at. The default is
  *Match window*: the window's own size (`--edf_native_render_height=-1`).
  The other choices use the window's shape: *Original (720 lines)*, the
  game's own 1280x720 on a 16:9 screen (`--edf_native_render_height=0`), then
  900, 1080, 1440, 1800 or 2160 lines. You can also set a fixed custom size
  from 640x480 up to 8192 on either axis (16.7 million pixels at most, for
  example 5120x2880). The image is then scaled to the window, so a size above
  the window's is supersampled. This setting needs a restart. On a slow GPU
  with a large screen, a lower setting such as 1080 lines is faster.
- **Scaling filter** (`edf_present_filter`). *Auto* keeps whole-pixel
  placement, scales exactly at whole factors and area-filters when the render
  is larger than the window. *Bilinear* is the original single bilinear
  sample.

## Controls

Any gamepad that SDL3 recognizes (Xbox, PlayStation, Switch Pro and others)
works out of the box, and `gamecontrollerdb.txt` adds community mappings.
XInput (Xbox-compatible pads, up to four) is the alternative: set *Controls >
Controller API* in F1, or pass `--input_backend=xinput`. The change takes
effect after a restart.

Both APIs support:

- hot-plugging (the first pad connected is player 1, the next player 2);
- vibration;
- navigating the F1 menu with the pad;
- per-stick dead zones and a trigger threshold;
- a per-player button remap (*Controls > Controller mapping*): swap or clear
  buttons and triggers, swap the sticks, invert axes. It is saved as
  `edf_pad_remap_p1` and `edf_pad_remap_p2`.

The pad chord that opens F1 always uses the physical buttons.

Keyboard and mouse are native, not an emulated gamepad. Keys go straight into
the game's own input channels. The mouse is added to the soldier's aim as an
angle, so it has no stick dead zone, no turn-rate cap and no acceleration. A
gamepad keeps working alongside. Keyboard and mouse are on by default
(*Keyboard and mouse* in F1). They use the game's **Technical** control type,
which is selected for player 1.

| Key | Action | Key | Action |
|---|---|---|---|
| W A S D | Move | Mouse | Aim |
| Left mouse / X | Fire | Right mouse / Z | Weapon zoom |
| Space | Jump / roll, confirm in menus | Middle mouse / Q | Next weapon |
| E | Enter vehicle (pad Y) | Backspace | Cancel in menus (pad B) |
| Arrows | Menu navigation (d-pad) | R | Pad X |
| Enter | Start: title screen, pause | Tab | Back: retire |
| F / C | Stick press (L3 / R3) | G | Reload (optional, see below) |

- Mouse sensitivity 1.0 turns 0.05° per mouse count. A zoomed weapon divides
  that by its magnification, as the game does for the stick. The in-game stick
  sensitivity does not affect the mouse. *Invert vertical aim* is in F1.
- In vehicles, the mouse falls back to driving the right stick as a rate,
  because how each vehicle turns that stick into aim has not been worked out
  yet.
- Menus are keyboard-driven. There is no mouse pointer in them.
- Fire, zoom, jump and next weapon follow the in-game controller settings. If
  you move *Fire* to another pad button there, the Fire key moves with it.

Everything above can be rebound in F1 under **Controls > Key bindings**:

- *Set* replaces a binding, and *Add* gives an action a second key.
- Each mouse button can be pointed at any action.
- Bindings apply immediately and are written to the config file when you
  resume.
- A binding needs the modifiers it names and ignores the others. So `W` still
  moves while Shift is held, and `Shift+W` needs Shift.
- Binding a key that another action already uses flags both rows, since only
  one of them would respond in game.

The same settings can be edited as raw `kbm_*` cvars in the F4 overlay.
`--edf_kbm=false` turns native keyboard and mouse off.

**Manual reload (optional, not in the original game).** EDF 2017 only reloads
when a magazine runs dry. *Controls > Gameplay additions > Manual reload*
(`edf_manual_reload`, off by default, applies at once) adds a Reload key and a
controller button:

- The key is **G**, and it can be rebound under Key bindings.
- The button is whatever you map under *Controller mapping > Extra actions >
  Reload*. Otherwise it is a click of the right stick
  (`edf_manual_reload_pad`), which the game itself leaves unused.
- A press starts the current weapon's reload exactly as an empty magazine
  would: the full reload time and gauge, then a full magazine. The rounds that
  were left are replaced.
- It does nothing with a full magazine, mid-reload or mid-burst, in a vehicle,
  for weapons that never reload or that refill at once (grenades), or while
  C-bombs or sentry guns are deployed.
- When it is off, the key and the button do nothing, and every button reaches
  the game as before.

Press **Escape** at any time outside the F1 menu to quit the game. It cannot
be rebound.

## Command line (optional)

```
edf2027.exe [--game_data_root=<folder>] [--settings] [--fullscreen=true|false]
            [--window_width=N --window_height=N]
            [--edf_native_unlock_framerate=true --edf_fps_cap=N]
            [--edf_native_vsync=true|false] [--edf_low_latency=true|false]
            [--edf_native_fsr=off|native_aa] [--edf_aspect=native|ultrawide|letterbox|stretch]
            [--edf_hud_safe_area=16:9|full] [--edf_native_render_height=-1]
            [--edf_native_render_width=W --edf_native_render_height=H] [--edf_show_fps=true|false]
            [--edf_native_renderer=native|off] [--audio_mute=true|false] [--log_file=run.log]
```

- Write config options on the command line as `--name=value`. Boolean options
  also accept `--name` (true) or `--no-name` (false).
- Do not write `--name false`: the separate word does not turn the flag off.
- Passing the same option twice does not mean the last one wins, so pass
  each option once.
- `--edf_fps_cap=0` with the frame rate unlocked is uncapped.
- `--edf_native_scene_backend=d3d11 --edf_native_backend=d3d11` selects the
  Direct3D 11 fallback.

The renderer's diagnostic switches are described in
[renderer-internals.md](renderer-internals.md).
