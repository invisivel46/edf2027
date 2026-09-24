# Renderer internals and diagnostic switches

This page keeps the renderer notes that used to open the README. It is for
people working on the renderer. Players do not need any of it. The current
state of the renderer is in [renderer-status.md](renderer-status.md), and
[native-scene-renderer.md](native-scene-renderer.md) and
[native-backend-migration.md](native-backend-migration.md) have the history.

## What draws the frame

- `--edf_native_renderer=native` (the default) runs the full-frame renderer.
  Native code draws the 3D frame from the scene the game publishes. The guest
  render helper `sub_821A5080` does not run. The passes run in this order:
  sky, models, static world, effects, transparent, post, then the game's own
  HUD. The HUD phase loop and the two view listeners are still guest code.
- `--edf_native_renderer=off` restores the guest ("Classic") renderer. The
  game's own draw calls are replayed through the native D3D12 backend.
- `world` and `full` are the older hybrid presets. Each one replaces single
  callees inside the guest traversal, and the guest helper still runs.
  [renderer-status.md](renderer-status.md) lists the flags that each preset
  turns on.
- `--edf_native_scene_backend=d3d11 --edf_native_backend=d3d11` selects the
  Direct3D 11 fallback. The default path creates no D3D11 device: `d3d11.dll`
  is delay-loaded. The full frame has only run on D3D12, so on D3D11 the
  `native` preset resolves to `off` and logs a warning. The F1 menu offers the
  same choice under *Graphics > Graphics API*.

No Xenos GPU plugin is selected or packaged. Old build folders may still hold
DLLs copied there earlier, so use a fresh output folder to check the native
distribution.

## Frame pacing and presentation

- `edf_native_vsync` controls display synchronization. It is separate from the
  legacy GPU-plugin `vsync` option. It does not change the fixed 60 Hz
  simulation clock.
- The D3D12 renderer uses two frame credits by default, so CPU preparation can
  overlap the previous GPU frame. `--edf_native_frame_latency=1` restores the
  per-frame GPU drain for comparison. The ordered three-surface presentation
  queue keeps each frame until the host GPU copy completes.
- `edf_low_latency` (on by default) makes the presenter wait for the display
  just in time on its own thread and show the newest frame. With VSync on, the
  renderer stays at most one frame ahead of the display. It was measured with
  VSync on, uncapped, on-foot mouse input. Input-to-photon p50 went from
  42.1 ms to 17.8 ms, and p99 went from 51.9 ms to 28.9 ms (commit `148f616`).
  `--edf_native_input_latency_trace=true` logs the per-stage percentiles, and
  `tools/latency-report.py` reads them.
- With VSync off, D3D12 enables the variable-refresh flags where they are
  supported. VRR also needs the monitor and the driver set up for it. On a
  fixed-refresh display, a refresh rate divisible by 60 avoids the uneven
  spacing of 60 FPS at 165 Hz.
- `--edf_native_display_trace=path.csv` records display feedback.
- `--edf_native_host_timings=true` reports host delivery intervals, image
  repeats and CPU acquisition and Present waits.
- The unlocked frame rate, the interpolation and the frame limiter are
  described in [framerate-unlock.md](framerate-unlock.md).

## Geometry, state and reads

- D3D12 geometry recording uses four CPU workers by default. For comparison,
  `--edf_native_geometry_workers=0` records directly, and `=1` uses a single
  packet worker. The [migration notes](native-backend-migration.md) explain
  the ordering, the validation and the measured performance.
- Native reads check the live SDK commitment and protection metadata. Ranges
  that are untracked or unsupported are checked through Windows instead.
  `--edf_native_guest_heap_reads=false` forces the slower Windows-query path
  for diagnostic comparison.
- Native render-state ownership is on by default. Draw-state words,
  scissor-enable and blend-factor colors come from CPU snapshots that the
  setters own. `--edf_native_owned_render_state=false` restores the older
  live-state readers for regression diagnosis, and
  `--edf_native_render_state_audit=true` compares the native state with those
  readers.
- Indexed geometry is not re-read and compared on every draw. A model's vertex
  and index buffers are snapshotted once. A later draw reuses the snapshot as
  long as the write-tracking registry shows nothing has changed since it was
  taken. The comparison is sampled, not dropped. The first
  `--edf_native_geometry_verify_initial` observations of each buffer, and
  every `--edf_native_geometry_verify_interval`-th observation after that,
  still read and compare the guest bytes. One disagreement turns full
  comparison back on for the rest of the run and logs a warning.
  `--edf_native_geometry_verify_interval=0` compares every draw.
- The guest's `PA_SU_VTX_CNTL` pixel-centre mode is applied to the game's
  full-screen post-processing passes. The half-texel sampling offsets in its
  downsample chain assume that mode. `--edf_native_pixel_centers=false`
  restores the unshifted viewport for regression diagnosis.

## Coverage accounting

Rendering coverage is counted, not assumed. Every draw the native renderer
cannot handle is recorded as a distinct *contract* (shaders, declaration,
topology, stride) instead of being silently dropped from the frame. Reaching
the retention limit is counted too, so it cannot hide the draws after it.
`--edf_native_contract_export=<path>` writes the observed declarations to a
catalog, and `edf_native_geometry_check` replays that catalog offline against
every vertex shader on the disc. See [native-coverage.md](native-coverage.md).
The full-frame coverage census (`--edf_native_coverage_census=true`) counts
objects by status, class and reason. It is described in
[renderer-status.md](renderer-status.md).

## Dependency audit

From a Visual Studio developer shell,
`cmake --build <build-dir> --target audit_native_dependencies` checks the
game's transitive non-system PE imports. It rejects Xenos DLLs, and it rejects
dependencies that resolve outside the executable's folder. To check a separate
staging folder, run
`cmake -DEXECUTABLE=<absolute-exe-path> -P tools/audit-native-dependencies.cmake`.
Windows system libraries count as prerequisites. The audit does not replace
runtime testing, and it does not detect every dynamic plugin load.

## Validation

- Offline, without launching the game: `.\validate-renderer-offline.cmd`
  from the repository root. See
  [renderer-offline-validation.md](renderer-offline-validation.md).
- In game: `tools/run-renderer-scenario.ps1` runs one scenario from
  `tools/renderer-scenarios.json` (benchmark, benchmark-skipintro, cave,
  ufo-swarm, vehicle, soak) with a scripted pad. Seeded saves come from
  `tools/make-edf-save.py`. Its variants are `ab`, `unlocked`, `soak-ab` and
  `soak-unlocked`. It prints the follow-up commands:
  `tools/renderer-runtime-gate.py` (FPS and mission gate),
  `tools/compare-renderer-ab-captures.py` (guest/native image A/B),
  `tools/frame-time-report.py` and `tools/soak-report.py`.
- Parallel renderer work uses [bounded dispatch packets](renderer-dispatch.md),
  with pinned source, explicit file ownership, model routing and independent
  review. `python tools/dispatch-renderer.py status` shows the queue once it
  has been initialized.

## Earlier measurements

A five-minute Mission 1 validation, locked at 60 with two frame credits,
reported 60.0 FPS throughout, with no repeated or skipped images across 17,440
host presentations. A closer-combat run at native 1920x1080 still dipped to
57 FPS on that build. Both runs predate the full-frame renderer and the `-O2`
build fix. Current figures are in [renderer-status.md](renderer-status.md).
