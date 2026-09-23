# Renderer status

Last updated 2026-09-23, at commit `361f80b` on `native-scene-renderer`.

This is the one current statement of the native renderer effort. Where another
`docs/renderer-*.md` or `docs/native-scene-renderer*.md` file disagrees with it,
this file wins. Those files are kept as history (see the last section).

## Summary

- The full-frame native renderer (`--edf_native_renderer=native`, the default
  since `361f80b`) draws the 3D frame without running the guest render helper
  `sub_821A5080`. `--edf_native_renderer=off` restores the guest renderer. The
  helper's HUD phase loop and two view listeners are still guest code
  (section 4).
- On `-O2` builds, on the Mission 1 benchmark route, the native full frame at
  `7c6fe92` runs gameplay at a median of 201.7 FPS with no frame cap. The
  pre-native baseline `6e9c94b` runs at 68.6 (section 2).
- On `7c6fe92`, the A/B image gate passes: 31 native frames, 0 failing.
- The FPS figures recorded on this branch before 2026-09-23 came from `-O0`
  builds (guest and host). They are not comparable with the figures above
  (section 2.4).
- Only Mission 1 has been verified in game. Commits after `7c6fe92` have not
  been run in game (section 2.3).

## How to enable

One switch, `--edf_native_renderer=off|world|full|native`, replaces setting
the individual cvars in section 6. Since `361f80b` the default is `native`:
the full-frame renderer runs unless `--edf_native_renderer=off` restores the
guest renderer. Before `361f80b` the default was `off`.
The full frame has only run on the D3D12 scene backend: with
`--edf_native_scene_backend` other than `d3d12`/`d3d12-warp` (the settings
dialog offers `d3d11`), the `native` preset resolves to `off` and logs a
warning once at startup; `world` and `full` are left as asked.

- `world`: `edf_native_host`, `shader_bridge`, `seam_draws`,
  `material_activation`, `scene_queued`, `scene_preload`, the six
  `scene_*_owned` flags the static world pass requires (sources, membership,
  selection, camera, geometry, material), `scene_tree`,
  `scene_tree_published`, `scene_visibility`, `frame_dispatch`,
  `scene_group_order` and `static_world_pass`.
- `full`: `world` plus `model_publication`, `model_pass` (the rigid model
  pass) and `post_finish`. These still run inside the guest helper.
- `native`: `full` plus `full_frame`. The render helper hook runs the native
  frame (section 4) instead of the guest helper. Turning on `full_frame` also
  feeds the render registry, without `edf_native_render_registry`.

Not in any preset: every audit, `bucket_dispatch` and `map_effect_list` (their
in-game audit and census have not been run), and the optional
`scene_activation_owned`, `scene_instance_owned`, `scene_pass_owned`,
`scene_view_owned` and the deferred flags. Turn those on individually.

Rules (`src/native_graphics/native_renderer_preset.h`): a flag is on when its
own cvar is on or the preset includes it. An individual cvar can only add a
flag, never remove one the preset turns on; to drop one, use the smaller preset
and add the rest by hand. The preset is read once at startup (restart to change
it; an unknown name stops startup), and one line
`Native renderer: preset=... on=[...] off=[...]` logs the effective flags.

The measured runs in section 2 also unlock the frame rate (see
[framerate-unlock.md](framerate-unlock.md)):
`--edf_native_unlock_framerate=true --edf_native_vsync=false` with
`--edf_fps_cap=120` (capped runs) or `--edf_fps_cap=0` (uncapped runs).

## 1. Goal

"Native renderer" means one concrete thing: during a frame, the guest render
helper `sub_821A5080` and the callbacks it drives do not run, and the frame is
drawn from the scene publication (the data the game publishes at its
simulation cadence) by native code.

Earlier work replaced single callees inside the guest traversal, each with a
fallback to the guest. That removed no helper cost, because the helper still
ran. The `world` and `full` presets still work that way. The full frame
(`1d14bbd`, restructured in `c9aa2ac`) replaces the helper itself. The 3D
scene is now drawn that way. The HUD phase loop and the view listeners are
not (section 4).

## 2. Measurements

### 2.1 Build

Until `04ad5e2`, the `win-amd64-release` tree built at clang `-O0`. Its CMake
cache held an explicitly empty `CMAKE_CXX_FLAGS_RELEASE`, so neither the 81
recompiled guest translation units nor the host code had an `-O` flag. The
baseline exe `edf2027-baseline-6e9c94b` was built the same way. `04ad5e2`
pins `CMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG` in `CMakePresets.json`. Configure
now fails for a Release, RelWithDebInfo or MinSizeRel tree whose flags carry
no `-O`, unless `EDF2027_ALLOW_UNOPTIMIZED_RELEASE=ON` is set. Every
measurement in 2.2 is `-O2` against `-O2`.

### 2.2 Performance (-O2)

All runs use the benchmark route `tools/native-benchmark-input.txt`: the
Mission 1 street, timed on the game clock. FPS values are the medians from
`tools/renderer-runtime-gate.py --phase all`. Frame times come from
`tools/frame-time-report.py`. Run directories are under
`out/native-bridge-run/`.

| Build | Renderer | Cap | Intro | Loading | Gameplay median (min) | Run |
|---|---|---|---|---|---|---|
| `6e9c94b` | guest (pre-native baseline) | 120 | 115.9 | 111.8 | 65.5 (55.3) | `binding-validation-20260923-005234-74ae26ab` |
| `3825033` | native full frame | 120 | 120.0 | 113.2 | 119.9 (118) | `binding-validation-20260923-010239-803d2a51` |
| `6e9c94b` | guest (pre-native baseline) | off | | | 68.6 | `binding-validation-20260923-013455-5c626456` |
| `7c6fe92` | native full frame | off | | | 201.7 (176.5) | `binding-validation-20260923-012450-19f6e574` |

Reading the table:

- The gameplay figure is the gate's window, entry +10..150 s. Later in the
  capped baseline run (t=320-596 s), gameplay samples read about 70-92, mostly
  around 80.
- With the 120 cap, the native frame held 119-120 FPS in every sample from
  t=141 s to t=591 s. The one low intro sample (7.2) was a single hitch. The
  final counters were 285 static draws, 64 model draws and 7 sky draws, with
  no declined groups and no `[error]` lines.
- `7c6fe92`, uncapped, gameplay frame times: p50 4.5 ms, p90 5.5, p99 7.0,
  p99.9 8.0 (106,800 frames). GPU time is about 1.0 ms of the 4.5 ms median
  frame (`--edf_native_gpu_timings`).
- Menus stay at about 58 FPS with the frame rate unlocked (section 8).

Spikes in the `7c6fe92` uncapped run (48 in 106,800 gameplay frames):

- The first gameplay frame takes 78 ms: 412 mesh builds and 10.7 MB of
  buffers. The frame after it takes 76 ms, with 45 pipeline creations. The
  first scene frame of the intro has the same 412-mesh upload (86 ms).
- Mid-gameplay, one frame at t=536.9 s takes 39 ms, with 2 pipeline creations
  (first use).
- Menus and loading have spikes of 45-60 ms, with 5-17 shader compiles.

A fix for the first-frame and first-use spikes is in progress. It is not on
this branch yet.

### 2.3 Correctness

`tools/compare-renderer-ab-captures.py` passes on `7c6fe92`: 31 native frames,
0 failing. The mean error is 0.038 against a motion noise of 0.39, and distant
geometry and the weapon are present. Run:
`binding-validation-20260923-011444-b358882a`. Captures are in
`out/renderer-ab/ab-7c6fe92`, and the worst pair is in
`out/renderer-ab/ab-7c6fe92-worst.png`. The capped `3825033` run was not
image-checked.

Not verified in game: the commits after `7c6fe92`. They are `3c2b59d` (effect
eye from the pass camera), `6ec6af6` (effect-pool view globals), `de90a8e`
(per-tick gating), `803e4ab` (vehicle weapons, turrets, treads and per-object
constants) and `9122e52` (grass). Each comes with unit tests, but no FPS or
image gate has been run on them. `361f80b` only changes the default preset. Missions other than Mission 1 have not been run
with the full frame at all.

### 2.4 Superseded -O0 measurements

Every earlier table in this file, and the FPS figures quoted in this branch's
commit messages up to `3825033`, were measured `-O0` against `-O0`.
That includes the "Baseline exe `6e9c94b` = 56.2" reference, the per-phase
table (intro 53.6, loading 57.3, gameplay 27.1), the all-scene-flags rows
(6.7-13.4 FPS) and the full-frame runs that were reported as matching or
beating the baseline. The earlier tables used the flicker script, not the
benchmark route. They are kept in the previous version of this file
(`e72da45:docs/renderer-status.md`) and in the epistemic KB. Do not compare them with section 2.2, and do not quote them as
parity or speedup results.

## 3. Rules

- A change counts only when it passes the runtime gate against the baseline
  (median FPS no worse, same input script, both `-O2`). Where the change draws,
  it must also pass the image A/B gate. For native-pass work on the guest-helper
  route, also pass `--min-native-groups`, so that a run which silently falls
  back does not count, and `--expect-drop` on the phase the change is meant to
  shrink.
- Performance comparisons use `tools/native-benchmark-input.txt`. The
  default script of `tools/run-renderer-ab.ps1` is still the flicker script,
  so pass `-Script tools/native-benchmark-input.txt`.
- A capped run shows only that the cap is reached. Measure headroom uncapped,
  with `--edf_native_frame_times`.
- Rebuild the baseline exe at `-O2` before gating against it. The old
  `edf2027-baseline-6e9c94b` exe is `-O0`.
- Offline suites (`validate-renderer-offline.cmd`, the differential fixtures,
  the unit tests that transcribe guest functions) are necessary but not
  sufficient. They prove narrow contracts. They do not show that the game got
  faster or draws the same image.
- The gate's default `--tolerance` is 0.05. Treat any drop as a failure unless
  it is explained.

## 4. Full-frame architecture

With `edf_native_full_frame` (preset `native`), the render helper hook runs
`NativeFullFrame` on the scene that `8219C7A8` opened, instead of
`sub_821A5080`, and leaves the scene for `8219C840` to publish. Frames that
`edf_native_ab_alternate` puts on the guest side still run the guest helper.

Per view, `BeginView` first writes the effect pool's view globals, as the
guest scene begin `821BE8D0` would through `821A17F8` and `821A19F0`
(`6ec6af6`). The native passes then run in this order
(`kNativeFramePassOrder`, `native_full_frame.h`):

| Pass | What it draws | Main commits |
|---|---|---|
| `sky` | `clSky` (z write off), then the `clMapEffectManager` walk: `clElectricWire` mode-0 strips and mode-0 grass in list order; mode-2 grass is filed into the transparent sequence | `50f10a9`, `46f7b7b`, `1df6ce5`, `8ab939a`, `20a7e24`, `9122e52` |
| `static_world` | Static opaque world from the published tree, visibility, LOD and cached group materials; clustered culling | `f9c7132`, `708ad3e`, `c77e71e` |
| `models` | Every model in the render registry's tick snapshot, rigid and skinned, with persistent draw state across frames, attachments (faces, weapons, vehicle weapon groups, alien tank turrets, tank treads, mothership spheres), pose interpolation and per-object shader constants | `73ba81e`, `c25ceab`, `0f11fcc`, `413d929`, `cd6307c`, `0f7755d`, `b84dba6`, `803e4ab` |
| `effects` | `clEffectObjectManager`'s walk: mode 0 drawn in place, other items filed; `clSpark02`, `clEffectEtc01`, `clSmokeLine` and the ribbon/billboard classes | `f7c3689`, `9014e82`, `0e47d32`, `9318048`, `3c2b59d` |
| `transparent` | One keyed sequence of model transparents, filed effects and filed map-effect items, in the guest's filing order | `9014e82`, `9122e52` |
| `post` | The finish/post chain and bloom (`820B0B80`), with zero guest calls; the guest finish stage runs only if the native post fails | `33d9c6f`, `63f335e` |

Guest code the full frame still runs:

- After the post, the output is bound on the guest device (`8219C930`,
  `82135530(device,0)`, as `820B0B80` does; `45af766`). Then the guest runs
  the view listeners and the HUD phase loop on the output.
- The view listeners are `8216DA80` (`clSatoCallback`, which draws the
  `clItem01` pickups) and `820D3FD0` (`clPlayerCamera` icons, trajectory and
  lines). They read the view globals that `BeginView` writes.
- The HUD phase loop draws movies, fonts, XUI and Utility 2D. Its draws go
  through the per-draw hooks. `e81aa3d` and `f2acec5` cut their overhead and
  batch draws whose state is identical.
- The scene's output binding and publish (`8219C7A8`/`8219C840`).

On the simulation side, `821A4DE8` publishes the scene, the static walk and
the render registry each tick. The registry tick runs on its own worker
(`edf_native_registry_overlap`), and the static preloads are checked in
parallel (`edf_native_preload_workers`; both `05c9007`). The render thread
holds the bridge locks in short slices (`f8334ba`).

### Unlocked frame rate under the full frame

- The camera is interpolated (`821CDDF8` hook), as on the guest route. Since
  `cd6307c`, the registry also keeps the previous tick's pose for every
  published pose (model, attachments, instanced worlds). The models pass
  blends between the two poses.
- Since `de90a8e`, `NativeTickGate` picks, per frame, the render that
  advances per-render guest state: the first render after a simulation step.
  Only that render commits the effect lifetime (`clEffectEtc02` +612) and
  draws the tone adaptation (`PS_Downsample_Tone`). Only that render lets the
  radar shake (`82176708`) and the cursor fade (`8218ED68`) advance. Unit tests
  compare these fields with a locked 60 Hz run. This behaviour has not been
  checked in game (2.3).
- Registry re-reads on render-only iterations are limited to new,
  resubscribed and retrying objects (`9b5fe2e`,
  `edf_native_render_registry_idle_skip`).

## 5. Guest-helper route (presets world and full)

The per-callee passes of the older plan still exist behind the `world` and
`full` presets: the static world pass at `821C3BB8(owner+240)` (`71fc0a2`),
the native octree walk, the rigid and skinned model pass at `821C9C20`
(`e13aee4`, `3c6d26b`), the native post finish (`b11c420`), bucket dispatch
(`037acbf`) and the map-effect list walk (`79f1e50`). None of them has been
measured at `-O2`. On this route, the static world pass still hands
guest-queued, scissor and ineligible groups to the guest group callback
`821D96D8`, then re-imports the pass from the device mirrors (`71fc0a2`).
That handoff has not been image-checked.

## 6. Cvars added on this branch

All are in the `EDF2027` category and were added after `6e9c94b`. Booleans
default to `false` unless the table says otherwise. "Requires" lists what must
also be on for the cvar to have an effect.

**Full frame and registry** (added after `b908b96`)

| Cvar | Default | Purpose | Requires |
|---|---|---|---|
| `edf_native_renderer` | `native` (string; `off` before `361f80b`) | Preset: `off`, `world`, `full`, `native` | none |
| `edf_native_full_frame` | false | Run the native frame instead of the guest helper `821A5080` | `edf_native_host`, `edf_native_shader_bridge` |
| `edf_native_render_registry` | false | Track render objects from constructor, destructor and update subscription, and publish a per-tick renderable snapshot at the end of `821A4DE8` (implied by `full_frame`) | none |
| `edf_native_render_registry_idle_skip` | true | On unlocked render-only iterations, re-read only new, resubscribed and retrying objects | the registry |
| `edf_native_registry_overlap` | true | Run the registry's per-step tick on its own thread beside the step's publication and preloads | the registry |
| `edf_native_preload_workers` | -1 (int, -1..16) | Helper threads that check the static preloads' groups each step; -1 picks 3 on 8+ cores, 1 on 4+, else 0 | `edf_native_scene_preload` |
| `edf_native_scene_static_walk` | false | Publish a per-world static walk plan at the step and drive the native visibility walk from it | native visibility walk |
| `edf_native_model_pass_skinned` | false | Also draw palette-skinned models in the guest-route model pass | `edf_native_model_pass` |
| `edf_native_transient_batching` | true | Append UI/immediate list draws that differ only in vertices to the draw before them | none |
| `edf_native_thread_qos` | 1 (int, 0..2) | Engine and render helper thread QoS: 0 OS default, 1 opt out of execution-speed throttling (HighQoS), 2 also prefer performance-core CPU sets | none |

**Diagnostics** (added after `b908b96`)

| Cvar | Default | Purpose |
|---|---|---|
| `edf_native_gpu_timings` | false | D3D12 timestamps at the full frame's pass boundaries and per HUD phase; logs `Native GPU timing:` lines |
| `edf_native_memory_log` | false | Log private bytes, working set, handles and the simulation tick count beside the FPS line (`34403ba`) |
| `edf_native_frame_times` | false | Present-to-present frame times: `Native frame times:` windows with percentiles and a histogram, and one `Native frame spike:` line per frame over 25 ms or twice the rolling median, with that frame's pipeline, shader, mesh and upload counters |
| `edf_native_model_source_audit` | false | Full-frame models: fetch every kept draw's program and geometry again from the providers and log each one that differs |
| `edf_native_render_registry_idle_audit` | false | Follow each render-only registry tick with a full one and log every entry the light tick missed |
| `edf_native_render_registry_audit` | false | Walk scene+84 and scene+100 each tick and count mismatches with the registry |
| `edf_native_scene_static_walk_audit` | false | Compare the published static walk plan with the live walk's reads |
| `edf_native_post_finish_audit` | false | Compare the native finish plan with what `820B0B80` issues |

With `edf_native_hook_timings`, the engine region probe (`a487f07`) also
reports, for the step dispatch `821A4BA0` and the frame transition `821A4DE8`:
on-CPU share, core class, migrations, guest waits and a calibration kernel.

**Static world pass (guest-helper route)**

| Cvar | Default | Purpose | Requires |
|---|---|---|---|
| `edf_native_static_world_pass` | false | Draw the static opaque world pass natively in published group order; unsupported groups run their guest group callback | `edf_native_frame_dispatch`, `edf_native_scene_tree`, `edf_native_scene_tree_published`, `edf_native_scene_queued`, `edf_native_scene_visibility`, `edf_native_scene_sources_owned`, `edf_native_scene_membership_owned`, `edf_native_scene_selection_owned`, `edf_native_scene_camera_owned`, `edf_native_scene_geometry_owned`, `edf_native_scene_material_owned`, `edf_native_scene_preload`, `edf_native_scene_group_order` (logs the first missing one) |
| `edf_native_frame_dispatch` | false | Own the outer render phase dispatch in native code; remaining phase callbacks are kept | `edf_native_shader_bridge` |
| `edf_native_scene_material_owned` | false | Build queued rigid scene materials from published programs and native pass inputs | native scene path |
| `edf_native_scene_activation_owned` | false | Activate queued scene shader bindings from published material inputs | `edf_native_scene_material_owned`, audit off |
| `edf_native_scene_geometry_owned` | false | Use published geometry for the first native group draw, keeping the indexed CPU tail | `edf_native_scene_material_owned`, a publication, audit off |
| `edf_native_scene_instance_owned` | false | Use lifecycle-owned world-only instance register metadata | a published source with a world |
| `edf_native_scene_pass_owned` | false | Carry explicit render and sampler pass state between native groups | native scene path |
| `edf_native_scene_view_owned` | false | Carry viewport, scissor and target selection across native material groups | native scene path |
| `edf_native_scene_camera_owned` | false | Consume immutable producer camera matrices at native render entry | native scene path |
| `edf_native_scene_geometry_deferred` | false | Submit eligible native groups before installing compatibility geometry bindings | `edf_native_scene_activation_owned`, `edf_native_scene_material_owned`, `edf_native_scene_material_audit` off |
| `edf_native_scene_material_deferred` | false | Submit eligible native groups before CPU material activation and restore state at handoff | a pending geometry handoff (so `edf_native_scene_geometry_deferred`) |
| `edf_native_material_activation` | false | Run material activation from a native operation list with native sampler resolution | `edf_native_shader_bridge`; the material state/sampler audits off |

**Publication and preload**

| Cvar | Default | Purpose | Requires |
|---|---|---|---|
| `edf_native_scene_sources_owned` | false | Select static sources, LOD, visibility and world from one scene publication | a publication with sources |
| `edf_native_scene_membership_owned` | false | Take spatial lists and hierarchy from the same publication as sources and assets | a publication with membership |
| `edf_native_scene_selection_owned` | false | Resolve published static instances without mutating the current scene database | a publication |
| `edf_native_scene_tree_published` | false | Read the immutable spatial hierarchy published by the world producer | `edf_native_scene_tree` |
| `edf_native_scene_group_order` | false | Publish each world owner's static group walk order (owner+240) at the simulation step | none |

The preload itself (`edf_native_scene_preload`) predates this branch; its
change-driven behaviour (`665e3e1`) has no cvar.

**Eligibility**

| Cvar | Default | Purpose | Requires |
|---|---|---|---|
| `edf_native_scene_reject_compatibility` | false | Diagnostic: reject counted static-group compatibility boundaries before calling them | native queued scene path |

**Walk, buckets and map effects (guest-helper route)**

| Cvar | Default | Purpose | Requires |
|---|---|---|---|
| `edf_native_scene_tree` | false | Native spatial tree traversal and culling; leaf callbacks stay explicit | `edf_native_shader_bridge` |
| `edf_native_bucket_dispatch` | false | Insert sort-mode 1/2 objects into the guest depth buckets natively instead of `sub_821C0C00` | none |
| `edf_native_map_effect_list` | false | Walk the map-effect list (`sub_820B35A0`) natively; each object still goes through the hooked `sub_821C0C00` | none |

**Models and post (guest-helper route)**

| Cvar | Default | Purpose | Requires |
|---|---|---|---|
| `edf_native_model_publication` | false | Capture model draw layouts at first sight and publish per-tick pose snapshots | none |
| `edf_native_model_pass` | false | Draw rigid published models natively at `821C9C20`; unsupported objects run the original draw | `edf_native_model_publication` and the static world pass scene flags |
| `edf_native_post_finish` | false | Replace the post chain `820B09B0` and the bloom quad of the finish stage `820B0B80` with a native loop issuing the planned passes; a preflight failure runs the original for that frame | none |

**A/B**

| Cvar | Default | Purpose | Requires |
|---|---|---|---|
| `edf_native_ab_alternate` | 0 (int, 0..1000) | Alternate guest and native frames in runs of N indexed output frames from the capture start frame; odd runs are native, 0 is off. With the full frame, guest-side frames run the guest helper | scene capture settings for the paired captures (see `compare-renderer-ab-captures.py`) |

**Audits of the guest-helper route (diagnostic, all default false)**

| Cvar | Compares |
|---|---|
| `edf_native_material_sampler_audit` | Native material sampler programs with original activation |
| `edf_native_material_state_audit` | Native material render-state programs with original activation |
| `edf_native_scene_material_audit` | Published programs with explicit pass-time constants and visible group setup; turns off the owned activation, geometry and deferred paths |
| `edf_native_scene_group_order_audit` | Published static group order with a live walk at world-pass entry |
| `edf_native_bucket_dispatch_audit` | Native mode-1/2 bucket key and insert with `sub_821C0C00` |
| `edf_native_model_publication_audit` | Published model layouts and poses with live memory at model draw entry |
| `edf_native_map_effect_census` | Tallies map-effect objects by (vtable, mode, slot-4 method) and logs the top classes every 600 frames |

## 7. Tools

- `tools/renderer-runtime-gate.py` compares a candidate `game.log` with a
  baseline log from the same script. `--phase all` (the default when both logs
  show the mission-load marker) reports intro, loading and gameplay FPS
  separately under `phases_fps`. It also finds mission entry in full-frame
  runs (`98027fa`) and reads the rotated `game.N.log` parts that long unlocked
  runs produce (`258864c`). Its other options are the window (`--start`,
  `--end`, default 10..150 s after mission entry), `--tolerance` and
  `--min-native-groups`. With `--edf_native_hook_timings=true` in both runs,
  it adds a `phases` report (including the `frame.native.*` and `sim.*`
  sub-phases). `--max-phase PHASE=MS` and `--expect-drop PHASE` gate on that
  report.
  `python tools/renderer-runtime-gate.py cand/game.log --baseline base/game.log --phase all`
- `tools/frame-time-report.py` merges the `Native frame times:`, `Native frame
  spike:` and `Native GPU timing:` lines per mission phase, using the gate's
  markers. It prints p50/p90/p99/p99.9/max, spikes with their counters, and
  GPU time per pass. `--json` prints the same as JSON.
  `python tools/frame-time-report.py out/native-bridge-run/<run>/game.log`
- `tools/run-renderer-ab.ps1` runs baseline and candidate exes interleaved on
  the same script, gates each pair and writes
  `out/renderer-ab/<timestamp>-<Name>/summary.json`. Only one game may run at
  a time; `-Seconds` (default 360) must cover loading plus 150 s. It passes
  `--edf_native_renderer` explicitly: `-BaselineRenderer` defaults to `off`
  (the guest renderer) and `-CandidateRenderer` to `native`, because since
  `361f80b` an executable given no preset runs `native`. The launcher
  `start-native-binding-validation.ps1` does the same with `-Renderer`
  (default `off`); an `--edf_native_renderer=...` in `-ExtraArgs` replaces it,
  and an executable older than the preset (`42823d7`) gets no option.
  `powershell -File tools/run-renderer-ab.ps1 -Baseline <o2-baseline> -Candidate win-amd64-release -Script tools/native-benchmark-input.txt -Repeat 2`
- `tools/compare-renderer-images.py` compares one guest image with one native
  image (BMP/PNG): share of pixels over `--threshold`, max difference, PSNR,
  optional `--mask`; exits nonzero above `--limit`.
- `tools/compare-renderer-ab-captures.py` is the image-correctness gate next
  to the FPS gate. It reads the captures of an `edf_native_ab_alternate` run
  and takes each frame's side from the `ab_alternate frame=F native=0|1` log
  lines, or from `--period N [--start S]` when there is no log. Each native
  frame is compared with its nearest guest frames P and Q on 8x8 block means,
  in 40x40 px tiles. A tile is bad when its native error (the smaller of the
  errors against P and Q) is more than `--noise-ratio` 2 times the P-vs-Q
  error, plus `--floor` 6. The P-vs-Q error is the motion noise, and each tile
  takes the largest value among itself and its neighbours. A native frame fails
  when more than 8% of its tiles are bad (`--max-bad-tiles`), when its largest
  connected bad region is more than 5% of the frame (`--max-region`), or when
  it is blank while its guest frames are not. The gate fails on any failing
  native frame (`--max-failing-frames` 0). It also fails on any tile that is bad
  in at least half of the native frames. That check catches small losses that
  last, such as one missing HUD element. A control judges every guest frame
  against its own guest neighbours. It never fails the gate but warns when the
  capture moves too much for the thresholds. The tool prints JSON like the
  runtime gate: `passed`, `failures`, `warnings`, `summary`, `control`,
  per-frame `frames`. `--diff-image` writes the worst native frame: guest,
  native, bad tiles in red, and each block's error over its limit (red above
  it). Exits 0 on pass, 1 on fail, 2 on unusable input. `--self-test` runs
  `tools/test_compare_renderer_ab_captures.py`.
  `python tools/compare-renderer-ab-captures.py out/renderer-ab/ab-7c6fe92 game.log --prefix cap --diff-image worst.png`
  Calibration: `ab-5c7d6e9` is the known-bad set, in which native frames lose
  static geometry beyond about 100-150 m. It fails in 31 of 31 native frames,
  with 41-43% of tiles bad and 244 persistent tiles. Its guest-as-native
  control passes: 30 frames, the worst at 3.7% bad tiles and a 3.5% region
  (a civilian crossing close to the camera). Guest-as-native frames with
  synthetic damage fail in 30 of 30 frames each for a black frame, a missing
  radar, a far region cut to sky and a 160x120 px cut. A missing ammo-text box
  fails through the persistent-tile check.
- Scenario runs beyond Mission 1 (`34403ba`, merged in `a02f5ad`). None of
  these has been run yet.
  - `tools/make-edf-save.py` writes a save seed that clears campaign missions
    1..N-1 on Normal, selects mission N and can add armour for long runs.
    `start-native-binding-validation.ps1 -SaveSeed` copies it into the fresh
    user directory.
  - `tools/run-renderer-scenario.ps1 -Scenario S -Variant V` runs one
    scenario from `tools/renderer-scenarios.json`. The scenarios are
    `benchmark` (Mission 1, M202), `cave` (Mission 11, M301, ants at close
    range), `ufo-swarm` (Mission 6, M212) and `vehicle` (Mission 9, M211,
    drives and fires a tank). The variants are `ab` (image A/B captures),
    `unlocked` (120 cap, frame times and GPU timings), `soak-ab` and
    `soak-unlocked`. `-DryRun` prints the plan only.
  - `tools/soak-report.py` reports memory, renderer cache sizes and frame-time
    percentiles per 5-minute window. It has gates for growth, late stutter and
    leaving gameplay. It reads the `edf_native_memory_log` lines.
  - `renderer-runtime-gate.py` gains `--expect-mission`. It handles missions
    without a pre-mission scene and places its markers on the input script's
    clock.
- `cmake/edf_optimization.cmake` holds opt-in build experiments. All are empty
  by default, so the default build flags do not change.
  - `EDF_GUEST_OPT_PROFILE` and `EDF_HOST_OPT_PROFILE` take `O3`, `v3`
    (`-march=x86-64-v3 -ffp-contract=off`) or `O3-v3`.
  - `EDF_LTO=thin` turns on ThinLTO.
  - `EDF_PGO=generate|use` turns on clang IR PGO.

  None of them has been measured in game. Micro-benchmarks put `O3` about
  equal to `-O2`.

## 8. Known gaps

- **Mode-1 grass** and filed electric wires are not drawn by the full frame.
  The map-effect walk counts them as unsupported (`9122e52`). A class census
  found no `clGrassMap` on Map01. The grass path has not been exercised in
  game.
- **Per-object constant carry-over between draws.** Under investigation: when
  per-object constants (`803e4ab`) are carried from one draw to the next.
- **Tone history under unlock, guest-helper route.** `de90a8e` holds the tone
  adaptation to once per tick in the native post. On the guest-helper route
  (presets `world`/`full`), the tone history is not held that way under
  unlock.
- **Menus** run at about 58 FPS with the frame rate unlocked.
- **Guest code in the frame.** The HUD phase loop and the view listeners
  `8216DA80` and `820D3FD0` are still guest code (section 4).
- **Coverage.** Only Mission 1 on the benchmark route has been verified in
  game. Other missions, maps, weathers, vehicles and bosses have not been run
  with the full frame. The scenario tooling for missions 6, 9 and 11 and for
  a 30-minute soak exists (section 7) but has not been run. `803e4ab` and `9122e52` add draws for classes that
  Mission 1 does not show.
- **Spikes.** A 78 ms first gameplay frame and a 39 ms first-use pipeline
  hitch (2.2). A fix is in progress.
- **Unmeasured commits.** Everything after `7c6fe92` (2.3).
- **Loading time.** The movie-pacing fix (`cdf0c66`) has not been measured
  separately at `-O2`.

## 9. Corrections to older docs

These replace statements in `native-scene-renderer.md` and
`native-scene-renderer-handoff.md`. The affected lines carry a
"Corrected 2026-09-22" or "Corrected 2026-09-23" note pointing here.

- `820B4250` is a per-simulation-step update of `clMapObjectManager`. It is not
  part of the render helper, and it is not "RenderWorld". Its +364 and +356
  writes are the `g_SignalBrightness` step counter and the `m_WaterTime` clock,
  used only as shader constants. Skipping the helper does not drop them.
- `world+372` holds map objects that are not in the octree.
- The "overlay+48" list is a game-object manager list, not an overlay leaf
  list.
- The static world is drawn by the final call of `820B4310`,
  `821C3BB8(owner+240)`. That call is the boundary for a native static world
  pass on the guest-helper route.
- (2026-09-23) "The current helper still executes for each render iteration"
  is no longer true with `--edf_native_renderer=native`: the full frame skips
  `sub_821A5080` (section 4). The helper timings quoted there (about 11 ms,
  13.88 ms, 7.04 ms for `821C3BB8`) may come from unoptimized builds (2.1).

## 10. Older docs

Nothing has been deleted. The files below are historical as of 2026-09-22.
Planning and inventory docs carry a one-line banner saying so. When the
release tree started building at `-O0` is not recorded, so FPS and millisecond
figures in all of them may be unoptimized (2.1).

Planning, inventory and progress reports (bannered, historical):

- `renderer-completion-inventory.md`, `renderer-coverage-audit.md`,
  `renderer-coverage-runtime.md`, `renderer-critical-path.md`,
  `renderer-remaining-work.md`, `renderer-replacement-plan.md`
- `renderer-first-replacement-task.md`,
  `renderer-static-group-completion-progress.md`,
  `renderer-static-group-handoff-result.md`
- `renderer-dispatch.md`, `renderer-task-guide.md`,
  `renderer-task-workflow.md`
- `renderer-re-automation-research.md`, `renderer-scalar-automation-pilot.md`,
  `renderer-setter-automation-pilot.md`, `renderer-setter-family-backlog.md`,
  `renderer-setter-family-expansion.md`
- JSON companions of these (`renderer-coverage.json`,
  `renderer-coverage-scenario-results.json`,
  `renderer-dispatch-first-wave.json`, `renderer-getter-acceptance.json`,
  `renderer-setter-acceptance.json`, `renderer-scalar-policy.json`,
  `renderer-tally-reconciliation.json`) are historical data; no banner.

Implementation notes for single changes (accurate for that change, not a
status; no banner): `renderer-constant-performance.md`,
`renderer-native-indexed-completion.md`, `renderer-native-shader-cache.md`.

Tool references (still describe how to run existing tools; no banner):
`renderer-atlas.md`, `renderer-offline-validation.md`,
`renderer-scalar-pipeline.md`, `renderer-tally.md`.

`native-scene-renderer.md` and `native-scene-renderer-handoff.md` remain the
detailed engineering record (data layouts, audits, evidence paths). They are
not status; the lines contradicted by section 9 are marked in place.
`render-helper-performance.md` profiles the guest helper, possibly on an
unoptimized build; it carries a banner saying so.
