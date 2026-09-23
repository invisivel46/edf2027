# Framerate unlock investigation

Status: implemented for Direct3D 12, experimental and disabled by default.
Direct3D 12 can render independently
of the 60 Hz simulation with camera and model interpolation. Live traces show
distinct interpolated poses above 60 Hz and fixed-step counters advancing at
60 Hz during uninterrupted samples. The bounded completion checks are recorded
below; exhaustive coverage of every mission, weapon and display is not claimed.

Enable **Unlock frame rate (experimental)** in F1 settings, then choose an FPS
cap (120 or Off for rendering above 60). Save retains the selection. VSync still
limits presentation to the display refresh. The equivalent launch flags are:

```
--edf_native_unlock_framerate=true --edf_fps_cap=120 --edf_native_vsync=false
```

The setting applies live on Direct3D 12. Direct3D 11's fallback host is still
limited by its presentation ticker, so the checkbox is disabled on that backend.
Interpolation adds up to one simulation tick of latency. Camera and model paths
can be isolated with `edf_native_camera_interpolation=false` and
`edf_native_model_interpolation=false`. The investigation below records earlier
stages; later entries supersede provisional implementations.

Movie playback retains the locked heartbeat, swap and host presentation path.
Menus after a movie remain paced until a completed 3D scene restores the unlock.

## Observed timing gates

- `NativePacingClock::Sample` in `native_pacing.h` advances at exactly 60 Hz.
- Hook `821BEAB0` waits for at least one elapsed step, updates guest clock words
  `8257C300` / `8257C308`, resets the elapsed timer through `821FAC28`, and returns
  the retail clamped integer step count.
- Native swap completion independently uses the same clock type to enforce the
  requested guest swap interval before acknowledging acceptance. GPU completion
  and presentation credits are separate obligations and must remain intact.
- `edf_fps_cap` only adds a limit in the present wrapper; changing it cannot
  bypass either timing gate. See "Render cap limiter" below.

## Render cap limiter

`edf_fps_cap` is enforced in the hook on the guest present wrapper `82151460`
(`src/input_hooks.cpp`, logic in `src/frame_limiter.h`).

Previous limiter (until 2026-09-23): after the wrapper returned it computed
`max(now, previous deadline + period)`, called `std::this_thread::sleep_until`
until `edf_frame_pacer_spin_us` (250 us) before it, then spun with
`std::this_thread::yield`. The standard sleep is a millisecond `Sleep`, rounded
up and woken on the next system timer tick, so it overslept by up to about 1-2
ms, well past the 250 us spin margin. A late frame kept the grid (the next
deadline did not move), so the next frame was short by the same amount: the
120 FPS gameplay run `binding-validation-20260923-015919-ce696cac` shows p50
8.3-8.5, p90 9.2-9.5 and p99 10.0-10.5 ms with a histogram from 6.5 to 10 ms
around a mean of exactly 8.333 ms. Also, the wait ran after the present, so the
next scene submission (where `edf_native_frame_times` samples) came one
frame's work later; that work differs between frames with and without a 60 Hz
simulation step, which added its variance to every present interval.

Current limiter:

- Schedule (`FrameDeadlineSchedule`): absolute grid, slot k = origin + k * 1e9
  / cap computed from the slot index (no truncated-period drift). A release
  that arrives after its slot by at most an eighth of a period (at most 1 ms)
  goes at once and keeps the grid; one later than that is a hitch and restarts
  the grid at now (no catch-up burst). A cap change, cap 0 or a placement change
  restarts the grid. Cap 0 never waits.
- Wait (`PreciseSleeper`): a `CreateWaitableTimerExW(...,
  CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, ...)` relative timer until a margin
  before the deadline, then a `_mm_pause` spin on `QueryPerformanceCounter`.
  The margin (`SpinMarginController`) starts at 1 ms and follows the observed
  timer oversleep + 100 us: it rises at once to a larger oversleep and decays
  over about 32 waits, clamped to [`edf_frame_pacer_spin_us`, 2 ms]. Without the
  high-resolution timer (before Windows 10 1803) a plain waitable timer is used
  with a 1-4 ms margin.
- Placement: with the unlock active (`NativeFramerateUnlockActive`: unlock on
  and no movie pacing), VSync off and `edf_frame_pacer_before_present=true`
  (default), the wait runs before the wrapper, so the finished frame is
  submitted and published a short fixed time after each deadline whatever its
  own work cost. The cost is latency: the frame waits up to one period minus
  its work (about 3.8 ms at 120 FPS with 4.5 ms frames) before submission;
  `edf_frame_pacer_before_present=false` restores the after-present placement.
  With VSync on, with the unlock off, or while a movie paces the swap
  (`movie_pacing_active`), the wait stays after the wrapper as before, where
  the guest's own swap pacing has already run.
- Presenter interaction: the D3D12 host presenter runs on its own thread
  (frame-ready notification, waitable swap chain with maximum frame latency 1,
  tearing present with VSync off) and does not block the guest thread. The
  guest's `edf_native_swap_wait` bounds CPU lead with `edf_native_frame_latency`
  (default 2) GPU completions and polls them with 1 ms sleeps; with
  before-present pacing the older frame has had a whole period on the GPU, so
  that wait is normally already satisfied and adds no jitter. A GPU-bound frame
  still stalls there (outside the limiter).
- With `edf_frametime_log`, a `FRAMEPACER:` line beside `FRAMETIME:` reports the
  placement, timer kind, current margin, waited/paced frames, mean spin, mean
  and maximum timer oversleep, the worst exit past a deadline, overruns kept on
  the grid and hitch rebases.
- Tests: `edf_frame_limiter_tests` (fake-clock schedule: grid, no drift over an
  hour, small overrun, hitch rebase, cap changes, clock anomaly, a simulated
  loop with 3-7 ms work; margin controller; a loose real-timer check that can be
  skipped with `EDF_FRAME_LIMITER_SKIP_TIMING=1`).

## Static caller evidence

The startup code in `edf2017_recomp.15.cpp` constructs the heartbeat with divisor
1 (`820B2140`) and installs it through `821A49E8`. That setter stores the timing
object at engine offset 2224 (the adjacent shared-owner word is at 2220).

The engine loop `821A6508` (`edf2017_recomp.4.cpp`) calls timing vtable slot 0 at
`821A6894`, retains its returned steps in r30, and skips its normal work when
r30 is zero. It passes r30 to `821A4BA0` at `821A65D8`. The latter function
(`edf2017_recomp.9.cpp`) repeats its callback traversals once per supplied step
and returns immediately for zero. This is a concrete step-dispatch boundary;
the actual gameplay/physics callback targets still require identification.

`821A5920` (`edf2017_recomp.1.cpp`) brackets `821A5080` with backend virtual
callbacks and elapsed-time samples. Existing hooks label `821A5080` a resource
helper; it cannot yet be assumed to be a pure, repeatable render function.
`821A53A8` dispatches listener vtable slot 20 once after the step-dispatch phase.
`821A5A10` performs list cleanup and object retirement. Their thread ordering and
relationship to actual frame production remain to be established.

Returning zero from an unblocked heartbeat alone would skip the normal engine
path. Returning one unconditionally would advance fixed-step callbacks too
quickly. Neither is a correct unlock.

## Boundary trace added

`--edf_native_loop_trace=N` records at most N coarse heartbeat, step-dispatch,
and helper-dispatch calls (0 disables, maximum 10000). Each selected call logs
paired entry/exit events with sequence, thread identity, monotonic timestamps,
object, caller and incoming step count. It does not change guest state or
timing behavior. A shared sequence identifies overlap across dispatcher threads.
The count is a process-wide startup window; a later gameplay window will need
an explicit start condition if startup exhausts it.

Release build passed: `out/unlock-loop-trace-build.log`. The first isolated trace
is `out/native-bridge-run/binding-validation-20260918-143102-7b7ffdb7/game.log`
(owned PID 25756, stopped). It confirms heartbeat/step dispatch on one thread
and overlapping helper dispatch on another, both using manager `40019D40`.
The read-only live snapshot `out/unlock-live-callbacks.json` identifies service
vtable `82001EF0`: slot 0 is `821BE868` (calls scene begin `8219C7A8`), slot 20 is
`821BE9F0` (calls `8219C840`), and slots 32/36 call `821397B8`/`821397F8`.
This establishes graphics participation, not purity of the entire helper path.

The experimental heartbeat is restricted to the observed outer-loop caller
`821A6894`. It keeps a thread-local real step budget, returns a nonzero render
token, and supplies the real budget to `821A4BA0` only at caller `821A65D8`.
Zero elapsed steps therefore skip its fixed-step traversals without skipping
normal render orchestration. Other per-frame listeners still require audit.
GPU completion and frame-credit checks remain before unpaced swap acceptance.

The first experimental run (`binding-validation-20260918-143339-498e8a6a`, PID
16972, stopped) remained at 60 FPS despite removing heartbeat waits. Inspection
found a third gate: NativeUiTicker requests host presentation at 60 Hz, backing
up the ordered scene queue. Experimental mode now changes its active polling
period to 1 ms; the default and occluded periods remain unchanged. This polling
approach is provisional; frame-ready wakeups would avoid redundant paints and
the residual polling ceiling. The D3D11 fallback ticker has not been unlocked.

Release builds passed (`out/unlock-experimental-build.log`,
`out/unlock-host-ticker-build.log`). Native pacing tests passed with unpaced
acceptance and restoration of paced intervals covered. These do not prove
gameplay timing. The faster-host run is tracked in
`out/unlock-fast-host-process.json`; inspect its live process before continuing.

The faster-host run (PID 41344) was subsequently identity-checked and stopped.
Menu samples reached 119.8-120.0 FPS. Host reports consumed approximately 600
new sequences per five seconds with zero skips, but also presented thousands
of repeated snapshots because of the 1 ms polling period. This proves removal
of the 60 FPS production gate in that window, not distinct scene motion or
correct gameplay timing. Summary: `out/unlock-fast-host-summary.json`.
Next: frame-ready host wakeups, the remaining per-frame listener audit, then
gameplay timing and interpolation. The unlock remains experimental and opt-in.

The provisional 1 ms host polling has now been replaced with frame-ready
notifications in experimental mode. Publishing a frame wakes the host ticker;
consuming a frame also requests another paint when queued frames remain.
Requests coalesce while a paint is queued/executing, survive that paint, and
use weak ticker state so late notifications cannot retain a stopped host.
Notifications run outside the frame-queue lock. Occluded windows retain their
250 ms cadence, and a 60 Hz fallback keeps UI updates moving without new frames.
Release build passed: `out/unlock-frame-ready-build.log`. Runtime cadence and
shutdown behavior of this notification path have not yet been validated.

The notification path was subsequently exercised in isolated run
`binding-validation-20260918-144144-e8228517` (PID 37580, identity-checked and
stopped). Steady menu intervals reported 601 host presents in about five seconds,
zero repeated images, zero skipped sequences, and approximately 120 FPS. A
read-only application tick sample advanced 600 ticks in 10 seconds. After the
scene list became populated, another sample advanced 601 application ticks and
1,192 scene render counters in 10.015 seconds. Loading intervals and heavier
scenes varied in throughput; these observations do not prove interpolation or
gameplay correctness. No error-level log records were found. Evidence is in
`out/unlock-frame-ready-summary.json` and the adjacent callback snapshots.

Further callback audit:
- Application end-of-frame listener `820A4EB8` calls viewport setup `821A7440`,
  drawing-scope entry/exit `821A7270`/`821A73F8`, and `820BD278`, which clears
  bytes at application offsets 188 and 232. The drawing-scope counter is balanced;
  it is not an elapsed gameplay counter. The meaning of the cleared flags still
  needs checking against their readers.
- The application fixed-step listener `820A5BB8` increments a 64-bit counter at
  `*(82580000 - 31164) + 976`, conditional on engine pause flags. This is the
  independently sampled tick witness above.
- The scene at list offset 0 uses vtable `82012C5C`: simulation slot 8 resolves
  to `8217BDC8` (delegating to `8217B298`), render slot 16 to `8217B0B0`.
- List 44 render callbacks `820B3610`, `820D4850`, and `820B4310` lead into
  visibility tests, render-queue insertion, and resource handling. In particular
  `820B4038` uses the render sequence as a duplicate-visit marker at object +48;
  this sequence must continue advancing with rendering.
- Listener `8216DA80` delegates to `8217A728` / `82122640`, which traverses
  additional virtual callbacks. Those targets and the scene render traversal
  remain audit work; they cannot yet be classified as entirely timing-neutral.

## Next work and completion evidence

### Camera motion evidence and integration point

`--edf_native_motion_trace=N` now fingerprints both matrices consumed by the
scene backend callback `821BE8D0` before each original call. It is disabled by
default and bounded to 10,000 submissions. `tools/report-native-motion-trace.py`
groups records by scene/viewport and reports changes in one-second windows.
The release build passed (`out/unlock-motion-trace-build.log`); parsing checks
covered unchanged samples, changed samples, and an empty trace.

Isolated run `binding-validation-20260918-144656-04681fc8` (PID 15732,
identity-checked and stopped) used a scripted camera/movement input. Multiple
steady one-second windows show 118-120 submissions but exactly 60 changed camera
matrix pairs. This directly demonstrates that the current scheduling unlock
does not yet interpolate camera motion. Two scene identities were observed;
projection changed with view in the second. Report: `out/unlock-motion-summary.json`.
No error-level log records were found.

Live matrices and generated-code tracing identify:
- Scene +32: projection; +96: view; +416: camera world transform, with translation
  in the final row. Live +96 is the rigid inverse of +416.
- `821CDDF8` derives projection, view, combined matrix, camera copy and culling
  data from +416 and the viewport/FOV fields beginning at +480. It is called
  by end-of-loop publication `821A4DE8`, after the helper join, and by constructor
  `821CE168`. This is a candidate for interpolating a render snapshot while
  retaining the authoritative camera transform for simulation.
- `8217B298` updates camera position/rotation at fixed-step cadence and delegates
  to `820D33C0`, which applies camera shake into +416.
- Scene render slot 16 (`8217B0B0`) is conditional debug geometry, not the whole
  scene renderer. Slot 12 (`8217B078`) copies camera debug endpoints. Actual
  scene rendering also traverses list 44 and the additional listener tree.

Next implementation: keep previous/current camera poses at the publication
boundary, interpolate position and rotation with a fractional simulation phase,
then derive consistent shader and culling matrices. Handle camera replacement,
cuts, projection changes, pause, and missed ticks without blending unrelated
states. Object and animation interpolation remain separate required work.

Camera interpolation is now implemented in `native_camera_history.h`, integrated
at `821CDDF8` only for publication caller `821A4EB0` in unlocked divisor-1 mode.
The heartbeat captures the fractional 60 Hz phase alongside its integer tick.
The history blends positions and FOV linearly and rotations along the shortest
quaternion arc. It keeps a one-tick render history; simulation speed is unchanged.
The hook temporarily supplies the blended +416 pose/+480 FOV to the original
matrix/frustum builder, then restores those authoritative inputs byte-for-byte
with an exception-safe scope. Derived render matrices retain the interpolated
values. Constructor/other callers invalidate reused scene addresses.

First samples, missed ticks, viewport changes, non-rigid/nonfinite input and
same-tick replacement reset history. Jumps over 50 world units, rotation changes
over 90 degrees, and FOV changes over 0.25 radians also reset as camera cuts;
these thresholds still need gameplay evaluation. At most 16 camera histories
are retained per engine thread. `edf_native_camera_interpolation=false` disables
this part for diagnostic comparison while leaving the scheduling unlock active.
The default locked mode is unaffected.

Release build passed (`out/unlock-camera-build.log`). Pacing/camera checks passed
for phase alignment, rigid midpoint pose, translation/FOV, discontinuity resets,
invalid matrices, and shortest rotation arcs across 180 degrees on all axes.
The current runtime verification is tracked by `out/unlock-camera-process.json`;
its live state must be checked before launching another instance.

That run (`binding-validation-20260918-145408-4eed2669`, PID 6180) was
identity-checked and stopped after collecting camera motion and update-counter
evidence. Several complete moving-camera windows show 120 submissions and 120
changed view matrices per second, compared with exactly 60 changes in the
noninterpolated baseline. The second camera also changes its projection smoothly
at submission cadence. An independent 10-second memory sample measured 600
application ticks and 1,174 scene render iterations. No error-level log records
were found. Evidence: `out/unlock-camera-summary.json` and
`out/unlock-camera-callbacks.json`. This verifies camera-matrix interpolation and
the sampled application clock, not all object motion or visual correctness.
Object/animation interpolation, aiming consistency, pause/transitions and
visual inspection still remain. Known per-instance upload hook `821D9600`
reports `g_mWorld` at vertex registers 0..3; its lifetime/identity and skinning
parameters need tracing before adding corresponding transform history.

### Object and skeletal palette ownership audit

Added bounded `edf_native_instance_motion_trace` observations and
`tools/report-native-instance-motion-trace.py`. Disabled by default, the trace
records at most 256 instance sources and 64 palette record/source pairs, then
reports between-frame and within-frame changes after the requested scene-frame
window. The reporter distinguishes an unfinished trace from a completed sample.
Release builds passed (`out/unlock-instance-trace-build.log` and
`out/unlock-palette-trace-build.log`); parser checks covered complete/incomplete
samples and within-frame palette changes.

Instance run `binding-validation-20260918-145930-4398739c` (PID 27052, stopped)
sampled 2,400 scene frames. All first 256 records were unchanged `g_mWorld`
values. Thirty data addresses were shared by multiple instance-list records.
This sample covers static geometry; it does not establish animated-object
identity. Evidence: `out/unlock-instance-summary.json`.

Palette run `binding-validation-20260918-150345-2fd5f03c` (PID 39836, stopped)
sampled 1,200 scene frames. Twenty-five material records all read the same
204-register scratch palette at `400DE920`. Individual records changed hundreds
or thousands of times within a scene frame. Neither the scratch address nor a
material record is an object identity. Evidence: `out/unlock-palette-summary.json`.
Neither run logged an error-level message.

Decoded disc shaders (`edf_native_shader_check --source`, saved locally as
`out/unlock-disc-shaders.txt`) confirm that Blend/SingleBlend vertex paths use
`g_mWorldArray`: 68 column-major float4x3 matrices in the common layout, three
float4 registers per bone. Some effects declare other array layouts, so names
alone must not select a packing rule.

The concrete palette producer is `821C9C20`: r4 is a source matrix vector with
begin/end at +4/+8. It calls `821A1738` at return address `821C9D24`, passing the
source pointer and matrix count. That helper packs source 64-byte row-major
matrices into the shared 48-byte column-major palette, clamped by capacity at
destination-vector +16. The same model draw routine uploads individual source
matrices through `821A17D8` at `821C9CB8`/`821C9D88` for rigid submeshes.
These boundaries allow a future native interpolated upload without mutating
simulation transforms. The generic float-vector copier `821A16D8` is not this
bone-packing path. Next: tie the source vector to model ownership/retirement,
publish the render timeline across engine/helper threads, and interpolate the
source poses for both palette and rigid-submesh uploads.

### Model pose interpolation implementation

`native_model_pose_history.h` now retains native pose copies per source vector.
It interpolates rigid rotation/translation and positive orthogonal row scales.
Nonfinite, singular, reflected or sheared matrices retain their source values.
Skeleton-size changes and timing discontinuities reset history. A source that
changes within one simulation tick is classified as render-dependent and passes
through until its history resets, avoiding a second interpolation of camera-
dependent attachments. Unchanged poses and endpoints preserve exact source bits.

Integration scopes history around model draw `821C9C20`, checks vector storage,
mesh-reference identity and visibility gaps, then overrides only destination
shader storage after `821A1738`/`821A17D8`. The source matrices are never written.
`821C8F10` explicitly frees vector begin through `820B2510`; a new hook retires
the matching native history before that free. History is bounded to 2,048 source
allocations, with at most 1,024 matrices accepted per vector. Other producer and
retirement paths remain coverage work.

The main engine thread publishes tick/fraction after `821A4DE8`. The render
helper snapshots that metadata on `821A5080` entry, and restores its previous
thread-local context on exit. Model uploads use the same publication phase as
the camera instead of sampling a concurrently advancing wall clock. Optional
`edf_native_model_interpolation=false` isolates this feature for comparison.

Release and pose tests passed (`out/unlock-model-final-build.log`,
`out/unlock-model-tests-build.log`). Checks include scaled poses, unchanged source
data, skeleton replacement, unsupported shear, and render-dependent bypass.
With `edf_native_motion_trace=N`, the first 16 sources report original/rendered
pose changes every 120 samples, bounded by N observations per source.

Live run `binding-validation-20260918-151410-02fc1b33` (PID 39356, stopped) showed
several complete 120-sample / 60-tick intervals with exactly 60 original pose
changes and 120 rendered pose changes. Sampled stationary models remained
unchanged. Evidence: `out/unlock-model-summary.json`. The independent counter
sample includes about four seconds where both rendering and application ticks
stopped; after that gap, application ticks resumed at about 60 Hz. It is not a
continuous 10-second simulation-speed result. No error-level messages occurred.
This run preceded the render-dependent bypass and helper-scope restoration;
those final changes have build/unit coverage but still need a runtime pass.
Visual animation, shadows, aiming, transitions, and uncovered transform paths
remain unverified.

### Final interpolation runtime pass and settings

Run `binding-validation-20260918-152510-c009df61` (owned PID 8336, stopped)
includes the render-dependent bypass and helper-scope restoration. The model
report now preserves the `render_dependent` field. Once the sampled attachment
was classified, its blended count stayed fixed while source/rendered changes
continued together. Other sampled models retained intermediate rendered poses.
Evidence: `out/unlock-final-summary.json`, `out/unlock-final-camera-summary.json`.

The uninterrupted gameplay sample advanced exactly 600 simulation ticks and
456 render iterations in 10 seconds (`out/unlock-final-callbacks.json`). Host
reports also show about 228 new images per five seconds, with repeated presents
accounting for the higher presentation counter. This full mission is limited
to about 46 distinct FPS on this run; it is not evidence of 120 FPS gameplay.
Earlier lighter scenes had complete windows of 120 changing camera poses per
second. No error-level log entries occurred. The finished host capture
`out/unlock-final-host.bmp` shows a coherent mission scene, player, NPCs and HUD;
a still image cannot establish correct animation or aiming behavior.

F1 now exposes the opt-in Direct3D 12 unlock alongside the existing FPS cap.
The host registers a flag-gated frame-ready callback at creation, allowing
live toggles instead of requiring a restart. Release build passed
(`out/unlock-settings-build.log`); interactive toggle coverage remains pending.
The next performance investigation should compare the same full mission with
interpolation on/off, and distinguish renderer cost from presentation repeats.

### Interpolation cost comparison and cache budget

Run `binding-validation-20260918-153024-9b2f6a48` (owned PID 42520, stopped)
used the same input script, 120 FPS cap and VSync off, with both interpolation
flags disabled. Its uninterrupted mission sample produced 469 render iterations
and 601 simulation ticks in 10 seconds (`out/unlock-no-interpolation-callbacks.json`).
The preceding enabled run produced 456 iterations and 600 ticks. These sequential
runs are not a controlled statistical benchmark, but the approximately 3% difference
does not explain the gap to 120 FPS. Host reports agree: roughly 232–234 new images
per five seconds, with no skipped sequences. No error-level entries occurred.
Profile the renderer in this mission next; removing interpolation is not a fix
for the dominant rendering cost.

Model histories now have a 64 MiB retained-storage limit as well as the 2,048
source limit. Accounting uses allocated vector capacity, including capacity
retained when a skeleton shrinks or history resets. Retirement subtracts the
source's storage; clearing resets the total. If an insertion/growth exceeds the
budget, the cache clears after producing the current draw's locally owned output.
Temporary draw vectors and map overhead are outside this history-storage budget.
Pacing/pose tests passed (`out/unlock-history-budget-tests-build.log`).

### Mission profile and host toggle coverage

Run `binding-validation-20260918-153416-85fe90c1` (owned PID 36740, stopped)
enabled hook timings and frame-time logging. Complete game-time windows 140–180
seconds averaged 41.425 FPS with no logged errors. Timings are instrumented,
inclusive CPU wall times and cannot be added as exclusive costs. Evidence:
`out/unlock-profile-summary.json`. The report tool now accepts `--all-phases`.
Existing swap, completion and native lock-wait timers were small; draw hooks
alone did not account for the observed frame time.

A 500-location sample of helper thread 39556 found 212 locations inside
`NtWaitForSingleObject`. A subsequent sample of raw stack words at that wait
consistently contained return-address candidates in `821D57B8` and `82132ED0`.
The former is the guest worker event loop, and the latter calls the guest wait
import. These are stack-word candidates, not a formal unwind. This points to
worker idle time pending main-thread work rather than evidence to remove a GPU
wait. No synchronization was removed. Symbol resolution used an independently
linked map whose `.text` SHA-256 exactly matched the sampled executable:
`066e7c8419bed01c4d255cf6742a25fdd0c1d51e383d398a1b4580a178d179cc`.
Raw evidence is in `out/unlock-profile-rips.json` and
`out/unlock-profile-stack-addresses.json`.

New hook phases `engine.simulation_dispatch`, `engine.render_helper`, and
`engine.frame_transition` expose those boundaries under normal hook timings.
They complement the load-only resource timers and leave scheduling unchanged.
Their runtime profile is the next measurement needed to identify the main cost.

The D3D12 host test now supplies the frame-ready registration seam and unlock
cvar, exercises toggling in both directions, and invokes a retained notification
after Stop. Resize, lifetime and GPU validation checks pass
(`out/unlock-host-tests-build.log`). This covers the host integration, not manual
interaction with the F1 settings widget.

### Overdue frame limiter correction

Run `binding-validation-20260918-154315-bd5498d5` (owned PID 11256, stopped)
recorded 41.04 FPS in the 140–165 second reporting window. Complete dispatcher
buckets averaged 13.948 ms per render-helper call, 3.709 ms per simulation
dispatch, and 0.435 ms per frame transition. Simulation and rendering overlap;
these inclusive averages must not be added. Evidence:
`out/unlock-dispatch-summary.json`.

Inspection of `input_hooks.cpp` found that the existing FPS limiter rebased a
missed deadline to the current time and then added a full period before waiting.
A frame already longer than two 120-FPS periods therefore received another
8.333 ms delay. The limiter now advances the previous deadline by one period and
clamps it to the current time. Overdue frames proceed immediately; subsequent
fast frames still obey the cap, without saved catch-up credits. Cap changes and
disabling the cap reset the schedule.

Unit tests cover fast, slow, consecutive slow, recovery, cap-change and disabled
cases. They pass (`out/unlock-limiter-tests-build.log`), and the release build
passes (`out/unlock-limiter-build.log`).

Corrected run `binding-validation-20260918-154651-7bf8a63e` (owned PID 42188,
stopped) sustained about 63–65 distinct FPS in the same full mission, versus
about 46 in the earlier uninstrumented run. The independent 10-second sample
recorded 651 render iterations and exactly 600 simulation ticks
(`out/unlock-limiter-callbacks.json`). Host reports show roughly 317 new images
per five seconds with no skipped sequences; extra repeated presents are not
counted as new rendered frames. The 120-FPS lighter scene remains capped near
120. No error-level entries occurred. Cadence evidence:
`out/unlock-limiter-cadence.json`. These sequential runs show the expected
improvement but are not a statistical performance benchmark. Broader unlocked
combat, pause/resume, movie and transition checks remain outstanding.

### Unlocked combat and pause/resume smoke check

Run `binding-validation-20260918-155048-25c9556d` (owned PID 41656, stopped)
used `out/unlock-combat-input.txt`: firing and movement at 140–165 seconds,
Start at 180 seconds, Start again at 190 seconds, then firing/movement at
200–215 seconds. The finished host capture `out/unlock-combat-pause.bmp` shows
the pause menu over the mission, a changed player position beside the rail,
and 110/120 ammunition. This confirms input handling and ammunition consumption,
but is not a weapon-rate or collision-accuracy measurement.

The continuous read-only trace (`out/unlock-combat-callbacks.json`, 211 samples)
shows the scene counter stopping through the pause and resuming afterward.
The application counter continues at about 60 ticks per second while paused,
as expected for the active menu loop; it is not itself proof that world physics
stopped. Scene-counter suspension plus the pause capture establish the observed
pause transition. Intro playback, menus and loading reached the mission without
errors; precise movie/audio synchronization remains unmeasured.

The 140–210 second reporting window averaged 71.77 FPS (including the pause),
with a lowest five-second sample of 60 FPS, zero skipped image sequences and
zero logged errors (`out/unlock-combat-cadence.json`). It is a smoke check, not
an exhaustive gameplay-equivalence test. README now explains the F1 control,
60 Hz simulation, interpolation latency and Direct3D 12 limitation.

### Movie pacing regression and correction

Matched launch attempts exposed different presentation timing before gameplay.
Locked run `binding-validation-20260918-155638-b0dfdb3b` (PID 39320, stopped)
took 3.146 seconds between movie draw counts 30 and 120. The previous unlocked
combat run took 0.750 seconds. Source metadata from local `ffprobe` reports
29.97 FPS for D3LOGO.wmv and Demo.wmv, and 24 FPS for Sample.wmv. Submission
counts alone do not prove decoded-frame progression, but bypassing movie pacing
clearly failed to preserve the original presentation path.

The bridge now marks movie pacing active when a movie draw succeeds. That state
preserves the original heartbeat, swap acceptance pacing, and host ticker rather
than just one of those gates. It persists across UI-only swaps because movie
drawing and the main UI loop can run on different threads. A valid completed
scene containing indexed geometry clears the state and restores the requested
unlock. Menus following movie playback remain paced until a 3D scene takes over.
GPU credits and completion checks are unchanged.

Preserving only swap timing, or swap plus heartbeat, still produced 60 movie
submissions per second; those intermediate runs (PIDs 23328, 41112 and 34960)
were stopped and superseded. Final build passes (`out/unlock-movie-host-build.log`),
as do pacing tests and host resize/lifetime tests
(`out/unlock-movie-host-tests-build.log`).

Verification run `binding-validation-20260918-160746-4a70fdcb`
(`out/unlock-movie-host-process.json`, owned PID 42868, stopped) took 3.140 seconds between
movie draws 30 and 120, matching the locked cadence. Its first indexed geometry
appeared about 151 seconds after launch, consistent with the locked run. The
completed 3D scene restored unlocked rendering: reporting windows 155–180 seconds
averaged 119.2 FPS, with zero skipped sequences and no errors. An independent
10-second sample recorded 1,199 scene iterations and 601 application ticks.
Evidence: `out/unlock-movie-host-cadence.json` and
`out/unlock-movie-host-callbacks.json`.

The attempted weapon/movement comparison is inconclusive. At the same host
capture time, the locked run was still in the mission introduction while the
intermediate unlocked run was already in gameplay. First indexed geometry
occurred about 152 seconds after launch in the locked run, versus 70 seconds in
that intermediate unlocked run. Future matched inputs must wait for gameplay
in both runs; fixed startup deadlines were not equivalent. Captures are
`out/unlock-equivalence-locked.bmp` and `out/unlock-equivalence-unlocked.bmp`.

Follow-up: that rule kept menus and mission loading at 60 Hz from the intro
movie until the first 3D frame, making loads 50–100 seconds. Movie pacing now
also ends when draws stop. Each swap compares the movie draw count with the
previous swap (`NativeMoviePacing` in `native_pacing.h`), and eight consecutive
swaps without a new draw clear the flag. Paced swaps run at 60 Hz, so a 29.97
FPS movie leaves one idle swap between draws and a 15 FPS movie leaves three.
Eight covers a decode stall of about 100 ms without releasing mid-movie, and
adds about 133 ms of pacing after the movie ends. Any new draw re-arms pacing
straight away. The 3D-frame clear is unchanged. `native_pacing_tests` covers
the 30 and 15 FPS cadences, the release and the re-arm. Not yet measured in a
run.

### Gameplay-triggered baseline measurement

Locked run `binding-validation-20260918-161237-e07434db` (owned PID 40392,
stopped) still displayed loading at the scheduled 248-second capture. Its
guest memory arena was at host base `0x200000000`, not the address used by prior
diagnostic scripts; the initial read failed and was discarded. Reading the
logged arena base later found the gameplay scene vtable `82003FA4` active.
Only then were new input events scheduled through the existing live script reload.

Five seconds firing and ten seconds forward movement were followed by pause.
Windows PrintWindow returned a blank image and provides no visual evidence.
A read-only heap scan subsequently found 12,043 aligned values equal to 60.
After unpausing and firing for 1.998 seconds, exactly one of those candidates
changed into the 30–42 range: guest address `43D60E64` changed from 60 to 36.
The immediately preceding word is 120. The controlled response and adjacent
maximum identify this as the ammunition counter with strong confidence, giving
24 rounds per 1.998 seconds, approximately 12 rounds per second. No guest memory
was written. Evidence: `out/unlock-ammo-candidates.json`,
`out/unlock-ammo-after.json`, `out/unlock-ammo-actions.json`, and input log times
555103 ms (trigger on) / 557102 ms (trigger off).

Final unlocked comparison ran as owned PID 5696 (now stopped), tracked in
`out/unlock-final-equivalence-process.json`. The input file is
`out/unlock-final-equivalence-input.txt`, with firing at 230–235 seconds, forward
movement through 240 seconds, pause at 242 seconds, and host capture at 248 seconds
to `out/unlock-final-equivalence.bmp`. The capture confirms these events reached
gameplay: the player moved, the pause menu is visible, and ammunition is 60/120.

### Completion audit

The final unlocked gameplay window (220–240 seconds) averaged 63.45 FPS, with
zero skipped sequences and no errors (`out/unlock-final-equivalence-cadence.json`).
After the five-second firing capture, a read-only probe repeated the baseline's
two-second burst. Trigger-on/off log times were 290887/292894 ms (2.007 seconds).
Of 12,054 original values equal to 60, one changed to exactly 36, at `43F2C024`;
another unrelated candidate changed to 35. The unique 24-round decrease
corroborates the approximately 12-round/second baseline. This is controlled
counter evidence alongside the visible five-second ammunition result, not a
general-purpose weapon-state decoder. Evidence: `out/unlock-final-ammo-after.json`,
`out/unlock-final-ammo-actions.json`, and the final run log.

| Requirement | Evidence and scope |
| --- | --- |
| Rendering can exceed 60 FPS | 119.2 FPS after the movie-to-3D transition; 1,199 scene iterations in 10 seconds; final gameplay 63.45 FPS. Host sequence accounting excludes repeated presents. |
| Motion changes above 60 Hz | Camera/model traces show intermediate rendered poses; stationary models stay unchanged; render-dependent attachments bypass interpolation. |
| Simulation timing is retained | Approximately 600 application ticks per 10 seconds; locked/unlocked five-second firing both leave 60/120 rounds, with matching two-second burst decreases. |
| Simulation transforms remain authoritative | Interpolation operates on native pose copies and shader-upload destinations. Tests preserve source matrices; temporary camera input is restored. Movement/pause smoke checks show coherent scene geometry, but exhaustive collision-equivalence testing is deferred. |
| GPU/resource ordering is retained | Completion tests cover ordered queue consumption, no reuse before host-copy completion, bounded frame credits and signal delivery. Runtime reports show zero skipped sequences/errors. |
| Pause and transitions work | Scene updates stop during pause and resume; final capture confirms the pause menu. Completed 3D output restores the unlock after movie pacing. |
| Movie pacing is preserved | Locked and final unlocked movie draws 30–120 take 3.146 and 3.140 seconds respectively; the original heartbeat, swap and host ticker remain in use for playback. |
| User controls and compatibility | F1 opt-in control, saved config, independent FPS cap/VSync; Direct3D 11 is explicitly excluded in the UI. Default remains locked. |

Final build and targeted checks pass (`out/unlock-final-checks-build.log`):
`edf2027_tests`, `edf_native_pacing_tests`, `edf_native_backend_host_tests`, and
`edf_native_backend_completion_tests`. All owned diagnostic game processes were
stopped. Remaining broader regression work includes all missions/weapons,
precise A/V synchronization and detailed collision comparisons; these are not
represented as completed by the smoke checks above.

## Input latency with VSync on, uncapped (2026-09-23)

Report: noticeable keyboard/mouse delay with VSync on, "Uncapped" (unlock on, cap 0),
D3D12. Nothing below was measured in a game run; the numbers come from reading the code
and from the fake-clock pipeline model in `tests/input_latency_tests.cpp`, which drives the
real `NativeBackendFrameQueue`. Measure with the trace (below) before trusting them.

### Where the time goes (legacy path, frames faster than the display, P = refresh period)

1. UI-thread delivery. Keyboard and mouse events arrive on the SDL UI thread
   (rexglue `windowed_app_context_sdl.cpp:90`, `SDL_WaitEvent`), which is also the thread
   the presenter paints on. The legacy Present waits for the frame-latency object inside
   the paint (`d3d12_backend.cpp:1681`), so the UI thread sleeps on the display for most
   of every refresh and events queue behind it: about P/2 added on average (8.3 ms at
   60 Hz, 3.5 ms at 144 Hz). The trace reports it as `ui_block_pct`/`ui_block_delay_ms`.
2. Waiting for the next simulation step. The mouse accumulates in `SharedInput`
   (`native_kbm.cpp`, `AddMouseDelta`) and is taken by the pad poll at the start of each
   60 Hz step (`sub_821B09A0` -> `MergeIntoDevice`); the step's callback slot 0
   (clNoguchiCallback `sub_820A6B10` -> `sub_8219E6A8` -> the input manager) runs before
   the object list whose soldier aim update (`sub_820DC890`, `InjectAim`) applies it,
   both inside `sub_821A4BA0`. Uniform input waits 0-16.7 ms, 8.3 ms mean. Motion
   arriving between that poll and the aim update waited a further tick.
3. The engine loop's own pipeline. `821A6508` presents the previous frame (+24 ->
   `edf_native_swap_wait`), kicks the render helper for this frame, runs the steps, joins
   the helper, ends the frame (+20; the scene image's slot `Reserve`,
   `guest_shader_bridge.cpp:2383`), then `821A4DE8` publishes the camera for the NEXT
   render. A step's input is therefore rendered one loop later and submitted/published at
   the start of the loop after that (`SubmitSceneFrameLocked`, `:2454`): about 2 loops.
4. Presentation queue. Three slots, FIFO, primed with two (`native_backend_frame_queue.h:20`,
   `:76`). With frames faster than the display the producer blocks in `Reserve` and every
   image waits behind the ready ones: about 3P.
5. Presenter. The paint takes the oldest image right after its previous Present returned,
   then waits for the frame-latency object inside Present (maximum latency 1,
   `d3d12_backend.cpp:1604`), and the flip lands a refresh after that: about 2P from
   acquisition to photon.
6. Interpolation. The camera (`native_camera_history.h`, sampled at
   `guest_shader_bridge.cpp:8443`) and model poses render `lerp(tick-1, tick, fraction)`,
   so a step's rotation ramps in over one tick: half of it shows ~8.3 ms later, all of it
   16.7 ms later, independent of the display.
7. Frame credit. `edf_native_frame_latency=2` (`NativeFrameFlight`) lets the CPU run one
   GPU frame ahead; with frames faster than the display it never waits (the queue
   backpressures first). When the GPU is the limit it adds up to one GPU frame.
8. Frame limiter. Cap 0 returns at once (`input_hooks.cpp:244`) and the before-present
   placement needs a cap and VSync off (`:277`): no cost here.
9. Mouse mapping. On foot the counts become an exact angle per tick
   (`native_kbm_logic.h:179`, `AimInputForCounts`): linear, no smoothing, acceleration
   or clamp; one tick of accumulation (item 2). Vehicles use a right-stick fallback
   (`kStickPerCount`, clamped to +-1 per poll, `native_kbm_logic.h:213`): a rate with
   saturation, not an angle. SDL relative mode delivers raw counts; the SDK scales them by
   the display density (`window_sdl.cpp:562`), a sensitivity factor, not a delay.

Model, pad poll to photon, loop work 0.4P: legacy 7.0P (116.7 ms at 60 Hz, 48.6 ms at
144 Hz). Adding items 1, 2 and 6 (about 8 + 8 + 8-17 ms), legacy input-to-photon at 60 Hz
is on the order of 140-150 ms; at 144 Hz about 70-80 ms.

### edf_low_latency (F1 > Performance > Low latency; off by default)

- Presenter just in time, off the UI thread: the ticker thread waits for the frame-latency
  object before dispatching a paint (`ui_ticker.h`, `NativeBackendHost::Create`); Present
  spends that wait as a credit (`native_present_slot.h`) instead of waiting. The paint
  starts right after a flip and the UI thread never sleeps on the display.
- Newest image: the presenter takes the newest ready image and drops older ones, no
  priming (`NativeBackendFrameQueue::Visit(..., newest)`), so nothing queues behind the
  display.
- Frame credit (`NativeFrameCreditPolicy`): two frames in flight while the loop keeps up
  with the display, one frame while the GPU is the limit and the loop is slower than the
  display refresh (measured from the just-in-time waits). A fixed latency 1 was modelled
  and rejected as the default: it lengthens the loop by every frame's GPU tail, and the
  loop's render lag makes that cost twice (36.7 vs 41.3 ms with an 8.3 ms GPU at 60 Hz),
  while it wins once the GPU cannot keep up (62.9 vs 74.7 ms at 21.7 ms). Applied live
  after each submit; with frame latency 1 the swap polls by yielding for its first 4 ms.
- Mouse: the aim update also takes motion that arrived after the step's pad poll
  (`MouseRouter::OnAim` with a late sample), only when that poll routed the mouse to the
  on-foot aim.
- Rejected: pacing the producer to the display (start the next loop only once the
  presenter took the previous image). The model shows it adds a refresh (3.0P vs 2.0P),
  because the render lags its step by one loop; with the newest-image presenter, frames
  do not pile up anyway.

Model with low latency: 2.0P pad poll to photon (33.3 ms at 60 Hz, 13.9 ms at 144 Hz)
versus 7.0P, item 1 removed and item 2 shortened for motion near the tick. The cost: with
frames faster than the display, the ones it cannot show are dropped rather than queued
(they were rendered for nothing either way); a cap saves that GPU work.

### Camera late latch: designed, not implemented

Rotating the rendered view by the mouse counts accumulated since the tick that produced
the camera would remove the interpolation ramp (item 6) for mouse rotation. Where it would
go: the camera's derived matrices, frustum and culling data are built on the engine thread
in the `sub_821CDDF8` hook at publication (`guest_shader_bridge.cpp:8443`), from the pose
the interpolation hands the guest builder. Rotating that pose there, before the builder
runs, would carry the latch into the view, the guest frustum at camera+288 that the full
frame's static and model culling read, the motion vectors and FSR (both derive from the
published camera) and the interpolation (the latch is recomputed from the authoritative
pose at every publication, so nothing accumulates, and a camera cut resets the history
and with it the latch); the crosshair is screen-centred and lands where the next tick aims.
Why it is not done:
- the latest point it can apply is publication, one loop before the render, so it gains
  only the counts that arrive during one loop's work plus the ramp;
- EDF's camera is third person: the view orbits the soldier, so a rotation about the eye
  is not what the next tick does; it would draw the soldier a few pixels off where the sim
  puts it, snapping back every tick (visible jitter while turning);
- the pitch clamp, the weapon zoom divisor (unit+896) and vehicle cameras each need the
  latch to reproduce guest behaviour exactly, and none of it can be checked without
  running the game. Revisit with the trace: if `tick_record` plus the half-tick ramp
  dominate after low latency, implement it in that hook, orbiting the soldier's pivot,
  behind `edf_camera_late_latch`.

### Measuring it

`--edf_native_input_latency_trace=true` logs `Input latency:` lines every 5 s per input
kind: p50/p90/p99/max per stage (input_tick, tick_record, record_submit, submit_acquire,
acquire_present, present_photon, total) plus the UI-thread block. Photons are DXGI frame
statistics when the swap chain reports them, else the next blocking return of the
frame-latency object (counted in photon_estimated). `--edf_native_input_latency_csv=PATH`
also writes every event. `--edf_kbm_test_sweep=N` stands in for a mouse: N counts/ms,
right 500 ms, still, left, still, from a 1 kHz thread, only while the soldier's aim update
runs. `python tools/latency-report.py game.log [after.log --compare]` summarises (exact
percentiles from the CSV, count-weighted window percentiles from the log).
