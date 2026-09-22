> Historical as of 2026-09-22; current status in [renderer-status.md](renderer-status.md).

# Renderer coverage runtime audit

2026-09-22. Two isolated D3D12 runs used the existing post-shader-ordering-fix
executable, SHA-256 `E4D6C0A88EC82AF51B930D7A57032A64C1228E589946F4DC2A8E1907192D7F7A`.
Both used fresh user directories, the existing game assets, 1280x720,
queued/preloaded native scenes, unlocked rendering with a 120 cap, and diagnostic
timings. This was an audit of the current hybrid path, not a run with all original
rendering disabled. In particular, `edf_native_frame_dispatch` remains at its
false default. No renderer implementation was changed during this audit.

Process metadata, input schedule, source-pinned logs, GPU captures and parsed
results are under `out/renderer-coverage/`. Both owned processes were stopped
after checking their live executable paths. No other process was stopped.

## What was exercised

| Scenario | Observed result | Evidence and limits |
|---|---|---|
| Startup/menu/loading to Mission 1 | Both runs reached the city; loading trace contains direct-scene eligibility, movie/font/XUI activity | Logs establish activity, not every loading pixel or successful publication. |
| City, player, soldiers/civilians, environment and large UFO | Visible in inspected captures; native preload and indexed submission continue | `gameplay.bmp`, `probe.output.2100.bmp`, `.2400.bmp`, `.2700.bmp`. External class identities are not inferred from appearance. |
| Movement and aim probe | Analog events delivered; view changes across captures | Second run movement scheduled at 180000 ms was delivered at 183403 ms. Intro camera transition overlaps the start; controlled displacement is not isolated. R10.input addresses this. |
| Weapon/fire/reload | AF14 120/120 before probe, Stingray M1 with smoke and RELOAD during RT255 + button 0x0200 | `.2700.bmp` versus `.3300.bmp`; later `.4500.bmp` shows Stingray 002/002. Specific projectile/particle methods and reference parity remain unknown. |
| Pause/resume | Complete host image shows Pause, Return to Game, Retry Mission, Quit Mission and Game Settings; later image shows gameplay without pause | `pause.bmp` and `.4500.bmp`. Frozen simulation was not measured. |

First run: 11:16:29–11:23:43 local log span, PID 27284. Second run:
11:24:23–11:29:35, PID 30800. The second produced 27 periodic scene-output
captures plus the one-shot final host pause capture. Periodic outputs explicitly
log `frame_complete=false`; visible HUD in a sampled image does not turn that
capture mechanism into proof of complete composition. The existing window
capture script failed with `Game has no main window` for the hidden first run.
R10.capture specifies a complete-host-frame burst instead.

Both runs contain zero `[error]`/`[critical]` records and zero sampled nonzero
shader/texture/geometry failure counters after distinguishing ordinary pipeline
cache misses from missing shaders. These are log observations, not a fidelity
certificate. Transform counters show `checks=0`, so their `mismatches=0` is not
an oracle comparison. The final second-run sample has 416/416 geometry and
material groups ready, no preload deferrals, 3,109,000 indexed submissions and
zero logged vertex/index mismatches. The first run reaches 418/418 groups;
neither run's group count is a complete content-family census.

Hook timings observe `render.pose`, `render.model`, `render.mesh`,
`render.overlay`, `render.listener`, `render.ui_listener`, `render.finish`,
`engine.render_helper`, and original/CPU-tail phases. The source inventory
shows forwarding model/pose/overlay/finish wrappers. Thus successful native
submission during these scenarios does not close original orchestration work.
Timings identify named phases, not every callback instruction/target/receiver;
R10.trace remains open.

## New work exposed by execution

- **R10.input:** establish gameplay/control readiness before scripted actions;
  retain intended and actual polling times. Menu success and delivered input
  cannot certify movement or firing by themselves.
- **R10.capture:** bounded, timestamped final-host GPU capture bursts with frame
  sequence IDs; distinguish partial outputs from final presentation.
- **R10.trace:** associate callback targets, family methods, pass identities and
  fallback reasons with scenario intervals. Aggregate successful draws do not
  prove family coverage.
- **R08.loading:** loading traces count eligibility and cumulative UI activity,
  not successful presentation. Add output-success/sequence evidence to the
  transition test.

Untested acceptance remains explicitly assigned: controlled destruction/LOD,
retry/reload/mission results, other maps/enemies/vehicles/weapons, shadows,
multi-view modes, resize/device recovery, full movie variants, other backends,
and independent-cadence/non-audit performance. The diagnostic second run ends
with a 35.6 FPS sample; instrumentation and captures make it unsuitable for a
performance conclusion or a 120 Hz claim.

## Audit and test results

The upstream structural audit initially failed two current-source fingerprint
checks for `native_shader_binding.h`, left stale by the preceding ordering fix.
Regenerated complete inventory, reentry inventory and resolved-site join; all
upstream checks now pass. The new coverage validator reruns that upstream audit
and inspects `structural_pass`, because the old script's exit code alone does
not signal a failed structural check.

The planning validator checks exact row coverage and hashes, feature/mode/task/
scenario references, required source surfaces, completion fields and dependency
cycles. Negative fixtures reject a missing task, dependency cycle and stale
source fingerprint. Existing shader-binding and frame-dispatch unit executables
both pass. No full renderer feature is declared complete from those tests.

Reproduce the planning results:

```powershell
python tools/build-renderer-coverage.py
python tools/audit-renderer-coverage.py
python tools/summarize-renderer-coverage-run.py
```

The second gameplay probe used the event sequence now preserved in
`tools/renderer-coverage-input.txt`. To repeat with a **new** output directory
(captures refuse to overwrite), invoke `tools/start-native-binding-validation.ps1`
with that input file, `-LoadingTrace -HookTimings`, and these extra arguments:

```text
--edf_native_unlock_framerate=true --edf_fps_cap=120
--edf_native_scene_queued=true --edf_native_scene_preload=true
--edf_native_scene_adapter_audit=true --edf_native_motion_trace=10000
--edf_native_scene_capture=<new-absolute-directory>/probe
--edf_native_output_capture_interval=300 --edf_native_output_capture_limit=48
--edf_native_host_capture=<new-absolute-directory>/pause.bmp
--edf_native_host_capture_after_ms=235000
```

Save the returned process metadata and verify the actual executable path before
stopping the owned process after the resume capture. Default assets/executable
paths come from the existing launcher. Capture timing varies by startup speed;
inspect actual input timestamps and scene content before assigning results.

The 32-feature catalog and concrete task dependency/test ledger are in
[renderer-coverage-audit.md](renderer-coverage-audit.md). Reviewed scenario
statuses are in [renderer-coverage-scenario-results.json](renderer-coverage-scenario-results.json)
and survive catalog regeneration.
