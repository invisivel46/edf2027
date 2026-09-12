# Native presentation timing investigation

User confirms the mothership flicker is resolved by the normal-map correction;
excessive brightness and roughly 30 FPS remain.

## Controlled diagnostic run

Executable: `out/build/win-native-clean/edf2027-normal-fix.exe`.
Log: `out/native-bridge-run/native-vsync-off-20260909.log`.
Automated input: `tools/native-flicker-input.txt`.
Display VSync disabled, FPS cap zero, native hook timings enabled. No scene
capture or per-draw invalid-color probe enabled. Simulation clock unchanged.

Observed September 9, 2026:

- Menu windows at t=20/25/30 s: 60.0 FPS, average 16.65–16.68 ms.
- Mission prelude t=75/80 s: 30.0 FPS, average 33.34/33.32 ms.
- Prelude t=85/90 s: 31.2/30.8 FPS.
- 256-call swap timing windows: refresh wait 3389.7391 and 4508.4088 ms;
  GPU completion wait 784.9448 and 937.4898 ms. These are separate aggregate
  windows, not a per-frame CPU/GPU decomposition.
- Window capture `native-vsync-off-20260909-state.png` confirms the mothership
  prelude and visibly excessive brightness at the measured point.

The preceding VSync-enabled diagnostic run was 24.8–25.7 FPS much later in
the mission. That is NOT a matched-scene A/B baseline and does not establish
the magnitude of a VSync performance improvement.

## Implications and next measurement

Display VSync alone does not explain the near-30 FPS prelude. Native swap
completion still enforces the guest interval and phase threshold using the
60 Hz native clock. Inspect and record those actual arguments and elapsed
work before changing the pacing policy; do not force simulation speed.

Host presentation currently holds the bridge context mutex through DXGI
Present. This is a possible contention source, not yet a demonstrated root
cause. Do not move Present outside context synchronization without auditing
DXGI/immediate-context concurrency and state restoration.

The complete Xenos-removal goal, 60 FPS performance, and brightness fidelity
remain unverified/incomplete.

## Per-swap trace

Built `edf2027-pacing-audit.exe` with opt-in timing instrumentation only;
simulation/presentation policy unchanged. Log:
`out/native-bridge-run/native-pacing-audit-20260909.log`.
The bridge object compiled and the candidate linked successfully. Existing
18 CTest executables passed (4.25 s); those tests do not exercise the new
live timing log, which was verified in the running candidate.

At count 4096 in the prelude: mode=1, interval=1, phase_limit=0,
entry_ticks=4477, entry_ack=4476, entry_phase=43, exit_ticks=4478.
Thus the completion arrives one tick after the preceding acknowledgement,
already partway through that next tick; the phase rule defers acknowledgement
to tick 4478. The sampled refresh wait slept 8.0016 ms and spent only
0.0018 ms reacquiring the context lock. At count 4352 the same one-tick-late
entry pattern occurs; sleep=16.5355 ms, lock=0.0041 ms.

Menu samples instead enter with entry_ticks equal to entry_ack and release
one tick later. Prelude FPS windows at t=75/80 s were 29.7/29.9.
This supports the missed-deadline fallback explanation, not an explicit
two-refresh mode selection. Context lock reacquisition is not the dominant
cost in these VSync-off refresh-wait samples. It does not exclude contention
elsewhere or with VSync enabled. Next: profile the work before the swap
completion (including CPU submission and actual GPU execution), keeping the
normal-map fix and game-speed accounting intact.

The read-only worker inspector now accepts an explicit EDF candidate basename
while still requiring an exact path match inside the native build directory.
No process memory is written.

## Parameter-upload experiment

`edf2027-upload-cache.exe` reuses per-thread packing scratch and avoids the
second variable-name lookup in `SetGuestFloatRegisters`. Packing, padding,
bit preservation, dirty detection, and failure-before-write remain unchanged.
The D3D11 library and five directly relevant rendering test targets were
rebuilt; all 18 existing CTest tests passed (4.75 s).

Runtime log `native-upload-cache-20260909.log`: t=75/80 s is 29.8/30.0 FPS,
t=85/90 s is 30.8/30.9 FPS. This does not establish a meaningful FPS gain
over the pacing-audit run. The allocation reduction is not the main fix.

Next candidate `edf2027-gpu-span.exe` adds sparse GPU timestamp/disjoint
measurements when hook timings are enabled, one scene in 60. Span starts
before clear and ends after HDR resolve, excluding bloom/presentation.
Results are polled without waiting; unreliable samples are explicitly marked.
Alternate/discarded scenes are cancelled. The underlying timer flushes when
ending a sample, so this remains diagnostic rather than a clean benchmark.
GPU timestamp elapsed time includes submission gaps, not just busy GPU time.
Runtime log: `native-gpu-span-20260909.log`. Candidate compiled and linked;
runtime verification of scene samples remains pending at this entry.

Subsequent runtime verification: the GPU-span candidate produced reliable
samples in the mission prelude. Scenes 3841/3901/3961/4021 measured
11.867136/11.4944/10.97728/11.54848 ms. Scenes 4081/4141 measured
12.696576/13.514752 ms; later samples also include 4.302848 and 5.259264 ms.
FPS at t=75/80/85 s was 29.2/29.7/31.0. These measurements establish that
the scene interval often consumes much of a 16.7 ms deadline, but do not
separate busy GPU execution from CPU submission gaps. Bloom/presentation and
work before the first scene command are still outside the measured interval.

## Split scene/post measurements

`edf2027-post-span.exe` moves the end marker to the final output quad and
uses a middle timestamp after HDR resolve. This avoids flushing between the
two intervals. It also logs the sampled tone-history shader's live constants.
Candidate compiled, linked and ran through the prelude successfully; log is
`native-post-span-20260909.log`.

Examples (scene / post / total, milliseconds):

- Scene 4201: 6.539264 / 0.746496 / 7.285760.
- Scene 4261: 4.792320 / 0.711680 / 5.504000.
- Scene 4321: 1.810432 / 4.748288 / 6.558720.
- Scene 4381: 4.830208 / 0.737280 / 5.567488.
- Scene 4441: 4.385792 / 0.666624 / 5.052416.

At t=80 s FPS was 29.9; t=85 s was 31.0. These are sparse measurements,
not a percentile distribution. Many measured rendering intervals fit well
inside 16.7 ms despite the low frame rate. Work before the scene, subsequent
presentation, and CPU scheduling/pacing still need measurement. Do not call
the renderer GPU-bound based on the earlier scene-only samples.

Tone-history shader constants: menu MiddleGray=0.5, ToneMap=1; mission
MiddleGray=0.8, ToneMap=0.8. Those are observations, not proof of incorrect
exposure. Compare with the actual guest setup before editing their values.

## CPU wait breakdown

`edf2027-cpu-waits.exe` compiled and linked with four extra opt-in inclusive
timing phases: engine wait, completion polling, scene setup, and worker
service after the worker CPU tail. Log: `native-cpu-waits-20260909.log`.

Mission samples at 18:49:38 and 18:49:47 show engine wait totals of 0.9520
and 0.7683 ms per 256 calls respectively. Scene setup totals in nearby
256-call windows are 542.0183 and 573.8935 ms (about 2.1–2.2 ms/call).
Worker-service windows include 739.2263 ms/277 calls and 785.5410 ms/296
calls on a different thread. These overlapping windows must not be added
as exclusive frame costs. FPS t=85/90/95 s: 29.8/29.6/29.7.

Although engine and swap clocks have independently established epochs, the
near-zero mission engine-wait cost does not support changing clock alignment
as the main remedy. Menu engine waits are larger, but menus already reach
60 FPS. Next investigate scene setup and worker/completion scheduling, not
an unsupported simulation-clock change.

## Completion polling change

NativeCompletionQueue::Submit/SubmitRange and NativeSignalQueue::SubmitRange
already emit and flush their event queries. The bridge swap barrier, worker
service/drain loops, and pending-fence poll now use non-flushing GetData polls
instead of permitting another flush on every iteration. Submission flushes,
real event readiness, publication ordering and sleep intervals remain intact.
This is safe only because these queues submit/flush the event before polling;
it is not a general instruction to remove all D3D11 flushes.
Reference: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_async_getdata_flag

The native draw regression now drains worker signals with non-flushing polls
and checks ordered/exactly-once delivery plus preceding GPU rendering.
Bridge/test rebuilt; all 18 tests passed (4.83 s). Candidate
`edf2027-poll-once.exe` linked and launched with the same automated input,
VSync off and diagnostic settings. Runtime log:
`native-poll-once-20260909.log`. Performance benefit remains unmeasured.

REJECTED after runtime test: this candidate regressed even menu throughput.
FPS t=5/10/16/21 s was 19.1/23.7/5.1/4.2. Worker-service windows included
4075.2943 ms/256 calls (max 254.9933 ms) and 8969.5638 ms/256 calls
(max 247.4726 ms). Explicit submission flushes and a passing WARP queue test
were insufficient evidence for this live scheduling path. All five bridge
poll changes were reverted; C868 retains its original non-flushing enqueue
poll. Candidate process 59928 was stopped after verifying its exact path.
Do not distribute `edf2027-poll-once.exe` as an improvement. Retain the
non-flushing WARP test as narrow API coverage, not evidence of live performance.

## Native deadline correction — measured improvement

User proposed that timing feedback itself was enforcing the fallback. The
bridge was retaining the Xbox raster-phase completion rule despite native
DXGI presentation. With phase_limit=0, every completed late frame was delayed
to an additional 60 Hz tick. `CompleteNative(interval)` now acknowledges real
GPU completion immediately once the requested interval has elapsed; early
frames still wait for that interval. The engine elapsed-step accounting and
actual GPU barrier are unchanged. The reference Complete method remains for
tests of the old rule, not the live native swap path.

Tests cover all 100 observed phase positions, intervals 1/2/3, invalid inputs,
counter wrap, early releases, and 50 frames of 20 ms work producing 60 elapsed
simulation steps without artificial 30 FPS quantization. All 18 tests passed
(4.45 s); the added integrated 20 ms scenario was rebuilt and passed afterward.

Candidate `edf2027-native-deadline.exe`, log `native-deadline-20260909.log`,
same automated mission input, display VSync off, diagnostic timing enabled:

- t=70/75/80 s: 44.7/39.2/40.7 FPS.
- t=85/90 s: 52.7/50.5 FPS.
- Swap samples 4096/4352/4608/4864/5120 acknowledge at entry_ticks with
  zero added refresh polls, rather than deferring to the next tick.

Previous equivalent prelude windows were roughly 30 FPS. This establishes a
runtime improvement in this diagnostic scenario, not stable 60 FPS, an
all-mission benchmark, or verified real-time gameplay speed. Visual brightness
is not changed by this correction. VSync-enabled behavior still needs testing.

## VSync-on follow-up and concurrent-process caveat

Same deadline candidate and scripted input with VSync enabled, log
`native-deadline-vsync-20260909.log`. Owned diagnostic PID19700 started
19:01:31. Menu windows through t=60 s were 60 FPS; prelude t=65/70 s
was 53.9/42.9 FPS. This shows the deadline benefit is not limited to
disabling display VSync, but is not a stable-60 result.

A second copy (PID57768, same executable, start 19:02:47) appeared without
an assistant launch. Therefore t=75 s onward (38.9, 45.2, 56.3 FPS) is
contaminated by possible resource competition and cannot be used as a clean
VSync comparison. Only the assistant-owned PID19700 was stopped; the newer
process was left untouched. Do not launch competing diagnostic sessions.

Earlier VSync-off gameplay t=125/130/135/140 s measured 31.0/27.3/28.6/28.0
FPS. The prelude gain must not be described as 39–53 FPS throughout gameplay.

## Exposure source check

Read-only environment inspection of the restored baseline PID40956 found
manager 0x4018dc20, map 0x418b0030, eight 172-byte presets. The address chain
comes from VM command 101 and sub_820B5718, not guessed offsets. Presets
0/4/5/6 contain MiddleGray=0.8, LuminanceWhite=1.5, ToneMap=0.8, matching
the logged mission shader values. Other presets have different authored
MiddleGray/ToneMap values. This rules out treating the observed 0.8 values
as an accidental duplicate upload. It does not prove all lighting, texture
formats, exposure-history inputs or output transfer functions are correct.

## Retained tiling path audit

User asked whether retained eDRAM functionality explains remaining lag.
Confirmed call paths from recompiled source:
scene begin 8219C7A8 -> 8219C5A8 -> 821409A0;
intermediate scene end 8219C930 -> 82140E98.
Neither tiling routine previously had a direct native override.

821409A0 copies tile descriptors (device12472 and following), updates saved
surface/tiling state around12432–12932, changes recording fields10800/10804
and byte10808, calls worker/setup8214EC00, and writes through recording
cursor13008. 82140E98 traverses tiles, calls resolve8213FAF8 and recording
helpers, clears tile state, restores recording/viewport state and invokes
8214ECD8. These are retained CPU-side Xbox workflows, not native D3D11
eDRAM storage. Their cost is not yet isolated, and their shared worker state
means a blanket no-op is not justified.

Added opt-in tiling.begin/tiling.end inclusive timing scopes around those
original routines. Behavior unchanged; bridge object compiles. Next measure
their contribution and audit shared-state dependencies before replacement.

Clean deadline run (VSync on, hook/GPU profiling disabled, only one process):
t=75/80/85 s 42.1/39.6/54.2 FPS; t=145/150/155/160 s
28.4/29.6/27.7/30.3 FPS. Profiling overhead does not explain the later slowdown.

Tiling probes linked into `edf2027-tiling-audit.exe` and verified in
`native-tiling-audit-20260909.log` (single owned PID35028). Mission begin
samples: 534.2841 ms/259 calls, 505.6409/256, 501.7282/256, 499.4068/256;
approximately 1.95–2.06 ms/call. End samples: 64.0488/259, 35.7996/256,
30.7423/256, 33.3938/256, approximately 0.12–0.25 ms/call. These are
inclusive CPU wall times, including waits; not pure descriptor-copy cost.

Further source audit: tiling begin calls 8214EC00, which first invokes
8213CF60 and 8213BC10, saves five recording-descriptor words at13020,
initializes recording descriptors13000/12960/12980, and writes a252-byte
tile header (including240 bytes of tile descriptors). This identifies the
recording transition as a concrete next replacement target. Do not remove
8213BC10 until its ordering/resource lifetime role is audited.

Synchronization audit follow-up: complete8213BC10 is only six word copies
from device40/44/13508/13504/13496/13500 to13040..13060. It contains no wait.
CF60's optional issued-2 fence wait is guarded by global82580000-29432;
read-only inspection of live PID35028 at19:12:37 found this global zero.
The inspector now reports this field explicitly. That optional branch cannot
explain the measured cost in that snapshot.

CF60 unconditionally calls CDC0, whose direct-submission branch captures a
fence via C788 and queues a descriptor via C868; its recording branch writes
a command-range reference into13008 instead. These paths still interact with
the live native completion/worker mechanism. Next isolate the CF60/CDC0 cost
and replace the tiling recording transition as a unit; do not simply suppress
shared descriptor submission or manufacture fence completion.

## Untiled lifecycle experiment: rejected visual result

The opt-in `edf_native_untiled_scene` experiment bypasses the audited scene
callers of 821409A0 and 82140E98, with begin/end pairing checks. It remains
disabled by default. The candidate `edf2027-untiled-scene.exe` reached the
mission, but `native-untiled-scene-state-20260909.png` shows the right half
black and severely overbright content on the left. Its higher FPS is not
valid evidence of a performance gain with equivalent rendering. The owned
diagnostic process was stopped after inspection. The previously delivered
native-deadline executable was not overwritten.

Next audit the viewport/scissor and other CPU state transitions lost with
the tiling routines before attempting another replacement. The precise
cause of the half-frame regression is not yet established.

### Viewport clamp confirmed in the failing path

Added a bounded, opt-in observer around 821371D0; it does not change setter
behavior. Rebuilt the bridge and relinked the experimental candidate. Owned
PID22804, log `native-untiled-viewport-audit-20260909.log`, at19:22:39:
caller8219C828 requests1280x720 at0,0, but stored viewport is640x720.
Surface40007c00 has descriptor09fc16f8. Scissor is disabled, with raw
rectangle0,0,2147483647,2147483647. Repeated frames reproduce the mismatch.
The diagnostic process was stopped after obtaining this evidence.

Source821371D0 (recomp.6.cpp) selects full tiling extent12900/12904 only
when device10808 flags and saved surface matches identify tiling. Otherwise
it decodes dimensions from surface+36, then clamps the requested viewport
before storing12376..12396 and computing viewport transform10376..10396.
Thus the bypass exposes the640-pixel tile descriptor to viewport clamping.
This establishes a concrete cause of the half-frame regression, not proof
that all untiled fidelity issues share that cause.

Next replace this scene viewport path with native target dimensions and
retain its required CPU transform/scissor state. Do not merely change the
D3D11 viewport after the fact: the stored CPU viewport and transforms would
still disagree. The called scissor setter821370E0 also invokes82134BD8,
which emits command packets, so its CPU state and GPU producer work need
separation rather than reinstating tiling flags to mask the mismatch.

### Native full-scene viewport replacement

Implemented an experimental native CPU replacement for caller8219C828.
It validates the matching untiled device scope and scene owner, uses owner
84/88 full-frame dimensions, stores viewport12376..12396, computes the
six viewport transform floats10376..10396, refreshes packed scissor10308/12
with signed intersection semantics, and preserves/sets dirty bits24|0xfc.
The raw scissor rectangle and enable state are unchanged. The replacement
does not call371D0,370E0 or34BD8, allocate command records, or set Xbox
tiling flags. Other viewport callers are not yet replaced.

New focused tests cover1280x720 without640-pixel clamping, transform values,
disabled scissor, signed scissor intersection, preservation of unrelated
packed bits, extent mismatch rejection and reversed depth. Rebuilt bridge
and effect tests; all18tests passed in4.68s. These tests do not establish
whole-scene visual parity. Live candidate logs its1280x720 native viewport;
visual validation follows separately.

Live validation PID62892 (`native-untiled-viewport-fix-20260909.log`) confirms
the replacement runs at1280x720. However, captured
`native-untiled-viewport-fix-prelude-20260909.png` STILL shows a half-black,
overbright prelude, with some glow extending into the right half. Therefore
fixing the scene-start setter is insufficient; the previous claim that its
clamp alone explains the entire visual regression was too strong. Further
viewport resets, scene draw bounds, and resolve/post-processing bounds must
be checked. Do not promote the experiment or claim an equivalent-rendering
FPS gain. The owned process was stopped after capture. The initial
`native-untiled-viewport-fix-state-20260909.png` was captured during a black
transition and is not the visual test result.

### Draw-time viewport reset isolated

Added draw-time viewport dimensions to the existing opt-in indexed capture
trace. Paired runPID61664 (`native-untiled-paired-20260909.log`) reports
draws97570 onward at640x720 against a1280x720 scene. Viewed paired
`untiled-paired-20260909.scene-color.60.bmp` and `.output.60.bmp`: the scene
already has a black right half before post-processing. The output adds
strong exposure/glow. Scene resolve validates equal full-frame dimensions
and uses whole-resource resolve/copy, not a half-width copy box. Stopped
the diagnostic process after capture.

Extended the native viewport setter to subsequent calls only when their
device has an open untiled scope, matches the active scene owner, and its
bound color surface matches that scene. Other render targets retain their
own dimensions. CPU helper now supports automatic/sentinel extents and
offset viewports, clamped to the native extent without unsigned overflow;
the initial scene caller still requires its exact full-frame contract.
Added tests for automatic1280x720 and offset100,200 with oversized extents.
Rebuilt bridge/effect tests; all18tests passed in4.54s. Live validation is
separate; the experiment remains off by default.

Live follow-upPID58316 (`native-untiled-live-viewport-20260909.log`) identifies
the later caller821BE978 (recomp.9.cpp) and replaces its1280x720 request
without tile clamping. Selected indexed draws now report1280x720 against
the1280x720 scene. Viewed paired captures
`untiled-live-viewport-20260909.scene-color.180.bmp` and `.output.180.bmp`:
both render the full width; the half-black regression is absent. Final
output remains overbright relative to the pre-postprocessing capture.
This validates the specific missing-half correction, not complete visual
parity or brightness correctness. The diagnostic process was stopped.

Next run a clean capture/profiling-disabled comparison and broaden visual
coverage before enabling untiled mode by default. No FPS gain is claimed
from this capture-heavy run. Both tiling lifecycle routines remain bypassed
in this successful full-width experiment; restoring Xbox tile flags or
command recording was not needed for the viewport correction.

## Clean untiled performance: regression, not a release candidate

CandidateSHA256:
`1C6B9A6875773DCF6478A2ADDF20B46D06CC1D6AE4A56329188BCDCF4744FB8A`.
The first attempted clean runPID3840 was invalid: the empty CLI argument
`--edf_native_scene_capture=` consumed the following timing flag as its
string value, producing capture files with that prefix. Stopped it and
excluded `native-untiled-clean-20260909.log` from performance conclusions.
Do not pass an empty assignment to disable this option; omit it (default
empty). Its unintended diagnostic files were not deleted.

Corrected runPID59492, `native-untiled-clean2-20260909.log`, started19:36:02.554.
Same scripted inputs, VSync on, cap0, hook/GPU profiling off, no capture
prefix. Verified only this game process running. Frame-time logging works.
Startup/menu near60FPS. Relative seconds/FPS:
66.3/50.1,71.3/47.7,76.3/38.4,81.3/22.4,86.4/21.7,91.4/24.3.
Later gameplay samples at19:38:24/29/34 are54.13/57.62/57.05ms
(18.5/17.4/17.5FPS), and remain about57ms through19:38:59.
Earlier clean native-deadline baseline was42.1/39.6/54.2FPS around76/81/86s
and28.4/29.6/27.7/30.3FPS around147/152/157/162s. This is a serious observed
regression, not proof of the precise bottleneck or a frame-exact A/B test.

Read-only worker snapshot at19:37:59: worker job callback/count/active/busy
all zero; pending pacing0, swap interval1, issued10495/completed10491.
The untiled log has no worker-signal completion reports, whereas the old
baseline does. This motivates auditing the submission/worker behavior lost
with the tiling recording lifecycle. It does not establish that worker
serialization is the cause; profile before changing synchronization.

Captured and viewed `native-untiled-clean2-end-20260909.png` only after the
timed samples: full-width gameplay with character, world and HUD, no missing
half. This is additional visual coverage, not full fidelity proof. Stopped
the owned process afterward. Keep untiled mode disabled by default. Next
profile the slow path and compare the same candidate with untiled mode off
to isolate lifecycle changes from build/run differences.

## Same-executable control and submission probes

ControlPID63128 uses the same SHA256 candidate above with only untiled mode
disabled; same input script, VSync, uncapped native frame setting and no
capture/profiling. `native-untiled-control-20260909.log` starts19:39:56.993.
At19:41:13/18 frame times24.10/24.73ms (41.5/40.4FPS), and later
19:42:08/13 are36.90/36.73ms (27.1/27.2FPS). This reproduces the slowdown
as a mode-dependent result within the same binary, rather than merely a
comparison to an older build. It still is not a frame-exact workload match.

Full source audit of8213C928 confirms: when requested fence equals current
issued fence and device12944 is nonzero, it returns without flushing or
waiting; without recording it callsCF60 and may enter the39688 poll loop.
This is a concrete synchronization semantic difference to measure, not
permission to bypass waits or pretend GPU work completed.

Added opt-in inclusive timing scopes for C928 (`fence.wait`), CF60
(`submission.flush`) and CDC0 (`descriptor.submit`). The C928 timing ends
before optional diagnostic fence validation. Phase names have compile-time
count validation. No queue/fence/recording behavior is modified. These new
probes are not present in the currently measured control executable.

Control continued through late gameplay:38.63/39.61/45.14ms at19:42:33/38/43
(25.9/25.2/22.2FPS), so it too degrades with workload; do not describe27FPS
as sustained throughout the control. Captured/viewed
`native-untiled-control-end-20260909.png` after samples, confirming same
stationary gameplay area with full scene/HUD, then stoppedPID63128.
Built the new timing scopes after the benchmark ended. All18tests pass.
Linked them separately as `edf2027-submission-profile.exe` so the tested
untiled/control executable is preserved. Next collect opt-in hook timings
with this diagnostic candidate; no synchronization fix is yet claimed.

## Submission timing falsifies the fence-wait hypothesis

Opt-in profilePID51864, `native-untiled-submission-profile-20260909.log`:
at19:45:32 C928 fence.wait256calls totals0.0318ms; scene.setup256 totals
3137.239ms (~12.25ms/call); descriptor.submit256 totals706.689ms (~2.76ms).
At19:46:51 scene.setup256 totals4720.878ms (~18.44ms), descriptor.submit
999.986ms (~3.91ms). At19:47:01 fence.wait256 totals0.0313ms. Thus the
recording-related C928 early return is a real semantic difference but is
not the measured slowdown in these samples. Do not bypass it as a fix.
GPU timeline samples during the prelude span roughly11-15ms, including
submission gaps; these are not pure GPU-busy measurements. Profiling adds
overhead, so use the earlier clean runs for FPS comparisons.

StoppedPID51864 after the late samples. Added nested scene.setup.original,
scene.setup.native, scene.setup.lock, and scene.clear timers to separate
original engine work, lock acquisition, and native color/depth clears.
No synchronization/rendering behavior changed. Rebuilt and relinked the
diagnostic candidate, then started `native-untiled-setup-profile-20260909.log`.

Setup split runPID54264 identifies the expensive interval as the call to
the original scene-begin function (including nested native hooks), not the
subsequent native clear/setup. At19:49:19,256calls:
scene.setup.original2828.245ms (~11.05ms/call), scene.setup.native2.6606ms,
scene.setup.lock0.0408ms, scene.clear0.9319ms. At19:50:52 original4160.908ms
(~16.25ms/call), native2.5056ms, clear0.9442ms. Tiling-begin bypass itself
is also negligible (~0.14ms/256calls). These rule out native clear cost and
the measured outer lock acquisition as explanations in this run.

Important: the original interval includes our new371D0 viewport hook. Do
not attribute all its time to unmodified engine code. Next split the scene
color/depth setters and native viewport hook, including its internal mutex
acquisition and guest stores. Source path is C7A8 -> C5A8 -> color37F98
(tail37988 ->378E0 ->371D0), depth37CB8, tiling-begin bypass,360D8, then
full-frame371D0. Native clear comes afterward. Read-only VirtualQueryEx at
19:50 found host device page140002000 protect4/allocationProtect4 (ordinary
read/write), not write-combined; this does not support a WC-store hypothesis.
StoppedPID54264 after collecting the split evidence. No rendering or
synchronization fix is claimed from these timing-only changes.

## Viewport state-write bottleneck and native CPU stores

Added viewport hook/read/write/lock and color/depth setter timings. RunPID61172
(`native-untiled-viewport-cost-20260909.log`) identifies the new viewport
write path itself as expensive: at20:00:57,256writes total2098.883ms, with
scene.setup.original2102.505ms. Another render thread records2084.518ms
for256writes. Read/lock and target setter work are small. This corrects any
interpretation of the original-scene interval as exclusively old engine code.

First replacement uses validated ordinary CPU-owned state blocks instead
of per-word interlocked publication stores, matching the original viewport
setter's ordinary writes. Shared fence/worker/completion StoreWord and
StoreDoubleWord implementations are unchanged. New StoreGuestCpuWords tests
cover big-endian layout, both halves of dirty flags, neighboring fields and
rejection without mutation. All18tests pass in4.39s.

InterimPID44872 (`native-untiled-cpu-state-profile-20260909.log`) reduces
menu writes from~2.1ms/call to~0.66ms, but mission writes still cost~3ms.
This does NOT isolate atomics as the cause: batching also reduced the number
of VirtualQuery calls. Stopped that diagnostic before the next build.

Added fresh SDK writable-range validation for ordinary virtual heaps only,
requiring committed read/write allocation metadata. Physical/XEX heaps do
not use this proof; OS validation remains for them and for failed SDK proof.
In particular, physical write watches must not be bypassed using allocation
protection alone. No cached permission results and no relaxed completion
ordering. Native memory tests exercise virtual-only admission, read-only
middle pages, decommit and release. All18tests pass in4.66s.

RunPID50992 (`native-untiled-sdk-writes-20260909.log`) now reports256viewport
writes totaling0.1533ms and300totaling0.2438ms in menus; scene.setup.original
falls to~0.008ms/call. The change in validation path removes the residual
cost, supporting repeated OS queries as the major overhead here, rather
than a need to emulate Xbox tiling or skip real GPU fence completion.
Mission validation follows separately. Candidate is `edf2027-untiled-cpu-state.exe`.

Mission validation in the same SDK-write run: at20:07:35,256viewport writes
total0.1652ms; at20:08:30,256total0.1487ms and original scene-begin totals
4.020ms (~0.016ms/call). The multi-millisecond viewport regression is removed
without tile recording or relaxed GPU completion semantics. Profiled
prelude samples20:07:34/39 are19.61/23.18ms (~51/43FPS); late gameplay
20:08:39..59 is32.96-33.93ms (~29.5-30.3FPS). Do not claim sustained60FPS
or a clean benchmark from these profiled data.

Viewed `native-untiled-sdk-writes-gameplay-20260909.png`: full world,
character and HUD remain visible. The preceding `...-state-...png` is a
black transition frame, not a failed gameplay capture. StoppedPID50992
after the late samples. CandidateSHA256:
`1C8B67ECDFC33A2EEEA47A615747AE97F6B7EA5BFAD9FCE55D875B865D59C3A7`.

Strengthened writable metadata tests: actual host writability on virtual
allocation, explicit rejection of physical write watches, restored RW
permissions, RW decommit (retaining protection), reserve-only RW, release,
cross-heap/null/overflow. Rebuilt memory tests; all18tests pass in4.52s.
Next clean benchmark with this candidate and profiling/captures off, then
decide whether further native-state publication validation costs warrant
separate optimization. Atomic fence and worker stores remain unchanged.

## Corrected untiled clean benchmark

PID60728, same candidateSHA1C8B67EC...D59C3A7, log
`native-untiled-cpu-clean-20260909.log`, started20:10:07.484. Same mission
entry script, VSync on, cap0; profiling and capture prefix omitted/off.
Verified only the owned game process, zero logged errors and zero capture
or hook-timing reports. No build/tests ran during measurement.

Relative seconds/FPS versus the earlier untiled-off control:

| Approximate elapsed time | Control | Corrected untiled |
| --- | ---: | ---: |
| 76.5s | 41.5 | 50.5 |
| 81.5s | 40.4 | 47.3 |
| 86.5s | 51.7 | 45.8 |
| 91.5s | 51.7 | 55.6 |
| 141.6s | 23.2 | 34.2 |
| 146.6s | 26.2 | 34.2 |

Later corrected samples20:12:49/54/59 and20:13:04 are28.81/29.01/28.77/
28.67ms (~34.5-34.9FPS). Control147-162s was~25-26FPS. This supports a
useful measured improvement in the stationary gameplay section after fixing
the viewport validation regression. It is a same-script, same-machine
comparison, not frame-exact A/B or a guarantee for all missions. The prelude
does not improve at every sample. Sustained60FPS remains unachieved.

Captured/viewed `native-untiled-cpu-clean-end-20260909.png` after the timed
samples, then stoppedPID60728. Full-width gameplay/HUD remains present.
Do not enable untiled mode globally solely on this benchmark: broaden visual
and lifecycle coverage and address remaining native submission contracts.
Next candidate optimization: reuse validated virtual-heap write permission
proof for remaining completion-publication stores while preserving their
atomic operations/order, rather than repeating costly OS queries. That
optimization has not yet been implemented or verified.

## Shared publication-write permission validation

Implemented the preceding optimization in GuestReader::WritableBytes. Ordinary
virtual heaps use fresh committed read/write metadata; physical/XEX heaps and
failed proofs retain OS validation. StoreWord and StoreDoubleWord retain their
InterlockedExchange/InterlockedExchange64 operations and byte order. No fence,
queue, GPU completion, or pacing semantics were relaxed.

Validated candidate `edf2027-native-write-validation.exe`, SHA256
`B9393D9E956E06E54DF3CAB126ED98C94B5F3E03E7E63B341EBB498FF07975E7`.
Owned PID52920 was revalidated live, then stopped after measurement and capture.
Run started20:16:19.692 with untiled enabled, same stationary input script,
VSync on/cap0, hook profiling off and no scene capture prefix. The logger rotated:
read BOTH `native-write-validation-clean-20260909.1.log` and the base `.log`;
the base file no longer starts at process initialization.

Late pre-capture samples20:19:21..20:20:16 are19.07-20.03ms (~50-52FPS).
This improves on the predecessor's observed34-35FPS stationary section, but
elapsed workload is not frame-exact, so it is not an isolated universal speedup
or proof of sustained60FPS. Screenshot at20:20:28,
`native-write-validation-clean-end-20260909.png`, was viewed and shows full-width
world/player/HUD. Exclude subsequent frame samples from clean comparison.
Read-only worker snapshot: issued27547/completed27545, interval1, pending0,
vblank count=ack14838; this snapshot is not an atomic synchronization proof.

All18 tests passed again in4.20s after stopping the game. Initial ctest invocation
outside the VS environment failed command lookup; the VS-environment rerun passed.
Untiled mode remains experimental/off by default. Combat and scene-transition
coverage, brightness, sustained60FPS and remaining submission/statistics/capture
contracts still prevent claiming full Xenos replacement.

## Untiled movement, firing and pause/resume smoke test

Added `tools/native-untiled-combat-input.txt`: same mission-entry schedule,
then bounded trigger holds, lateral/forward movement, camera turn and pause/resume.
No mission quit/retry selection. Ran the same B9393D9E...07975E7 candidate,
PID45964, `native-untiled-combat-20260909.log`, untiled on, profiling off,
VSync/cap0. Process started20:22:32; first input epoch was about20:22:33.628.
The pause/resume rows were added through supported live reload before their
scheduled times; elapsed time was retained. Stopped the owned process after
the resumed-world capture and checked the retained log files for native errors
and nonzero unsubmitted counts; neither was reported.

Viewed window captures under `out/native-bridge-run/`:

- `native-untiled-combat-before-20260909.png`: loading/black transition, not
  evidence of gameplay failure.
- `native-untiled-combat-fire-20260909.png`: player off median in roadway,
  full-width world/HUD; ammo120 at this instant alone does not prove firing.
- `native-untiled-combat-moving-20260909.png`: player advanced beyond footbridge,
  camera turned, ammo85/120. Together with logged RT255 input, verifies firing
  affected gameplay as well as movement/camera change.
- `native-untiled-combat-after-20260909.png`: visible Pause overlay on scene,
  ammo110/120 after another trigger hold/reload interval.
- `native-untiled-combat-resumed-20260909.png`: overlay gone, world/HUD and
  approaching enemies visible after scripted START at235007ms.

Post-resume frame samples were17.85-18.50ms, but this is a screenshot-containing
functional smoke test, not a controlled FPS benchmark. No half-width regression
was visible. Brightness still needs correction. This adds combat/pause coverage,
not mission teardown/recreation, every effect, or every mission. Untiled mode
remains off by default until the remaining lifecycle and fidelity work is verified.

## Untiled mission retry verification

Added `tools/native-untiled-retry-input.txt`. Same candidate B9393D9E...07975E7,
owned PID29196, start20:27:57, log `native-untiled-retry-20260909.log`, untiled
enabled and profiling off. Script paused at145s, selected Retry Mission at150s,
and opened confirmation at153s. Viewed `native-untiled-retry-confirm-20260909.png`:
expected Restarting mission dialog, default No.

Initial live-added180/183s confirmation events were missed: reload happened at
183274ms, after both pulses expired. The unchanged dialog in `...-return-...png`
is therefore NOT a renderer failure. Rescheduled Up/A to260/263s; resource-release
logs at20:32:23 and continued indexed submissions follow acceptance. Viewed
`native-untiled-retry-accepted-20260909.png` at20:32:56: returned to Mission1
gameplay, full-width world/player/HUD, ammo120/120. No logged native errors or
nonzero unsubmitted counts in retained run logs. Stopped exact owned process.

This proves the tested retry returns to rendering; it does not prove every
renderer-owner teardown/recreation path was exercised. The E140 source calls
D1C8 before unbinding and releasing resources. D1C8 still writes an Xbox packet,
then waits via C928 and device10868. Future removal must preserve the real native
completion and CPU-worker wait contract, not merely omit the whole function.
Brightness, full lifecycle coverage and complete native submission remain open.

## Correction: drain already migrated; untiled promoted to native default

The preceding D1C8 remaining-packet claim was incorrect: the original generated
routine contains the packet, but the native-host hook already routes it to
`edf_native_device_drain`. `tools/extract-native-device-reset.cmake` removes the
reservation/two-word packet and cursor write, retains C928 and the CPU busy loop,
and inserts native worker-signal draining. The immediate-tail tests cover native
drain/reset. Do not reimplement or remove the preserved waits based on original
generated code alone.

Changed `edf_native_untiled_scene` default to true after the documented clean,
combat, pause/resume and mission-retry coverage. Native-host guards remain; an
explicit false selects the legacy comparison path. This intentionally moves the
normal native path off Xbox CPU tile recording despite the separately unresolved
brightness/full-fidelity work. It does not claim complete Xenos replacement.
Rebuilt bridge object; all18 tests passed in4.57s. Separate candidate output is
`edf2027-native-untiled-default.exe`; older delivered executables are preserved.

Candidate SHA256 `EC645F0FF7662665286ED444856C76E13E8479487F88459F755004900C49B9C5`.
Startup validation PID58028 omitted the untiled flag entirely. At20:35:29.061,
`native-untiled-default-20260909.log` reports the native untiled1280x720 viewport
without tile packets, confirming default selection. No logged startup error.
Stopped the owned startup run; this was not another full-mission test.

## Remaining cache-packet caller reachability

Audited full8214ECD8: its C5F0 call returns at8214ED04. Stack80 is cache word
count, stack84 is packet address. The latter is also used on either subsequent
C328 allocation failure, while nonzero word count drives C868 atEDF8. The later
EE44 submission carries a native worker signal. Thus simply applying CF60's
zero-word helper here is not a complete caller migration; allocation fallback,
CPU list cleanup and native callback submission must be handled together.

Added opt-in, power-of-two bounded C5F0 original-path return auditing, gated by
native host plus hook timings. It reports caller/count/returned words without
changing original execution or return values. Built separate
`edf2027-native-cache-audit.exe`, ran PID59304 through Mission1 with untiled
default and profiling enabled. `native-cache-audit-20260909.log` contains no
retained-cache-packet or pending-signal-audit calls through observed gameplay.
Viewed `native-cache-audit-gameplay-20260909.png`: full world/player/HUD.
Stopped the owned process after collecting the sample.

This rules out that caller as a measured cost in this run, not every game mode.
Do not describe removing this inactive path as a gameplay performance fix.
At20:40:32, indexed.native617259calls totaled1733.63ms over its reporting window;
indexed.original617262calls totaled60.60ms. Viewport.write256calls totaled0.1964ms
and fence.wait256calls0.0335ms. These are inclusive CPU wall-time buckets, not
additive/exclusive frame costs or a clean FPS benchmark. Active native draw and
shader submission are better next profiling targets than the absent cache path.

## Native texture/sampler binding runs

ShaderBindings::Bind previously called SetShaderResources/SetSamplers once per
declared slot on every bind. It now emits one call per contiguous declared run,
for both VS and PS, without caching context state or skipping actual rebinds.
Explicit nulls remain in runs; undeclared gaps are not touched. Constant-buffer
updates and GPU synchronization are unchanged. `binding_runs.h` validates slot
limits before emission and uses bounded stack pointer storage.

CPU tests cover sparse runs, nulls, preserved gaps, empty/full sets and invalid
slots rejected before any call. A WARP test compiles a shader with t0/t1/t3 and
s0/s1/s3, seeds slot2, binds slot1 null, and reads all four actual D3D11 bindings
back to check exact state. Rebuilt effect/draw tests pass; the new WARP and CPU
tests reran together in0.24s. An earlier18-target ctest pass was4.48s, but only
effect/draw executables had been refreshed against this library change.
Linked separate `edf2027-native-binding-batch.exe`; game benchmark and hardware
visual validation are pending. No measured FPS improvement is claimed yet.

## Binding-batch clean run: no demonstrated FPS gain

Candidate SHA256 `F96DDF64DF0C1A501E4BE11B15E34BAF34396C9607146F1F745B5E84B81F33FE`,
PID57852, started20:45:13. Same stationary mission-entry script, VSync/cap0,
profiling off, no capture prefix, untiled default. No builds/tests/screenshots
during timed samples. `native-binding-batch-clean-20260909.log` at20:47:36..48:11
reports21.35,21.25,21.11,20.37,20.16,20.28,20.33,20.38ms (~47-50FPS).
Earlier write-validation candidate at comparable elapsed141..177s reported
19.65..20.38ms (~49-51FPS). This does NOT establish a performance improvement;
not frame-exact and runs separated in time, so a causal regression is also
unproven. Keep delivered untiled-default executable unchanged.

After timing, viewed `native-binding-batch-clean-end-20260909.png`: full-width
gameplay/player/HUD. No logged errors, hook timing or output-capture activity.
Stopped exact owned process. Batching remains a source candidate, not a claimed
60FPS solution. A better next step is isolating binding CPU cost or precomputing
immutable slot runs, rather than assuming fewer API calls imply higher FPS.

User playtest reports this build hovering around55FPS with uneven frame pacing.
Treat that as separate human workload evidence, not the stationary benchmark.
The automated samples also contain25-31ms maxima against~20ms averages. Next
investigate frame-time distribution and CPU/GPU/presentation wait attribution;
do not substitute an average-FPS target for smooth delivery or force simulation
timesteps. Full test refresh used edf2027_tests (the build target), not the
edf2027_unit_tests CTest name; the first target-name attempt failed before build.

## Host delivery instrumentation and occlusion limitation

The FRAMETIME metric is sampled after the game's82151460 swap wrapper, not at
native host presentation. NativeHostSurface runs on a16ms UI timer; its frame
visitor holds the bridge mutex through context isolation, composition, overlays
and Present. This is a possible contention point, not yet a proven pacing cause.

Added opt-in `edf_native_host_timings` (defaultfalse). Reports five-second host
Present-return cadence, acquisition-plus-context-isolation and Present durations,
including maxima, plus repeated images and skipped publication sequences.
Sequence counts refer to handoff publications, not necessarily simulation frames.
These are CPU timestamps, NOT scanout timestamps. Occluded intervals are excluded;
bounded separate occlusion reports explicitly state that visible pacing is unavailable.
No timer, VSync, simulation or locking behavior was changed.

Initial diagnostic PID49128 (`native-host-pacing-20260909.log`) reports
presented=false/image=true at20:52:06.738. No successful-delivery buckets appeared
through the observed run. Thus the automated environment is DXGI-occluded; its
game FPS cannot validate the user's visible pacing. Previous screenshot-based
fidelity checks remain pixel evidence, not proof of actual monitor presentation.
Stopped the owned process, added explicit occlusion reporting, rebuilt host object
and host-lifetime test (pass0.30s), and relinked `edf2027-native-host-pacing.exe`.
The final occlusion-reporting revision has not yet been run. Visible-user-session
telemetry is needed to attribute the reported pacing; no pacing fix is claimed.

## Clean-folder no-Xenos startup

Staged `out/native-no-xenos-smoke-20260909/` with only the host-pacing executable,
rexruntime.dll and amd_fidelityfx_dx12.dll. Dumpbin dependencies on all three
contain no Xenos import. rexruntime imports the AMD library; it is not a Xenos
module and was not arbitrarily removed. Other imports are Windows/VC runtime.

PID62388 launched with stage as working directory and PATH restricted to
C:\Windows\System32;C:\Windows. Read-only module enumeration confirmed D3D11
from System32, rexruntime and AMD DLL from the stage, no Xenos. At20:55:07.452,
`native-no-xenos-smoke-20260909.log` reports image=true, Xenos_loaded=false.
New occlusion reports execute (count1..32 observed); visible pacing unavailable.
Stopped the exact owned process. This proves staged startup, not portable
redistribution on a fresh machine or every mission/optional graphics contract.

Stage executable SHA2561338544E44956741253F10E28EB2E98795D494D2B3AF58BD54AFCD34FD5AFD33;
rexruntime2450B54107EE2E831C3F7A18FE2D5699FB6F36ED07C2EA5A9C4B161371624F8A;
AMD8989E22FD1197619C522320EB09A9D3EC61340A752B345E1EE4D3641331B9B36.
Added `Run-Pacing-Test.cmd` for user-session launch with synthetic input disabled,
host diagnostics on, hook profiling off, cap0/VSync on, and a new nonexisting
visible-pacing log filename. It uses existing local game/user/cache paths and
is a local diagnostic launcher, not a redistributable package. Not yet user-run.

## Precomputed reflected binding runs

No visible-pacing log was present at the next check. Continued native binding
work without asserting a visible pacing fix: ShaderBindings now calculates its
immutable texture/sampler runs once in construction. Owning ComPtr maps remain;
set/clear operations update bounded raw-pointer slot arrays backed by those maps.
Bind uses the stored runs/arrays, eliminating per-bind map traversal, range
validation and pointer repacking. It still re-emits every declared slot and
does not cache external context state. Sparse gaps remain untouched.

Extended WARP tests check clear-all, setting a previously null slot and rebinding
after external resource-slot mutation. The first test used ClearState and broke
later fixture readback setup; replaced it with targeted external slot mutation
so unrelated render-target setup is preserved. Draw/effect tests pass after this
fixture correction. Rebuilt bridge object for the changed ShaderBindings layout.
No game candidate linked yet; other dependent objects must be refreshed before
linking. No performance gain or visible pacing improvement is claimed.

## User visible-session evidence: host delivery is slower than game FPS

Full normal build refreshed19dependent/link steps after codegen reported no
changes; all18tests passed5.04s. Preserved previous edf2027.exe as
edf2027-before-binding-layout-20260909.exe before replacing normal output.

User tested host-pacing executable with edf_native_host_timings=true and reports
~55FPS. Found the visible run in `out/build/win-native-clean/logs/edf2027_009.log`
(20:56:02..20:57:49). This is NOT the automated occluded run. At20:57:13..38,
host buckets contain199-201 successful returns per5s, average25.01-25.18ms,
max44.78-47.39ms; game reports52.2-55.0FPS. Present averages0.40-0.42ms;
acquire-plus-isolation averages0.09-0.12ms (max1.3-2.8ms in these buckets).
Host repeats13-20images and skips80-95publication sequences per bucket. These
are CPU delivery/publication counts, not exact displayed-frame/scanout counts.

This establishes a host-scheduling delivery deficit in addition to any game
throughput limit. Startup/loading also has~175ms acquire maxima; do not conflate
those with steady gameplay's25ms cadence. NativeHostSurface uses Win32 WM_TIMER
at16ms inside SDK's SDL_WaitEvent-based UI loop. The installed-source loop is
src/ui/windowed_app_context_sdl.cpp. Next replace or appropriately wake the host
scheduler through the SDK's UI-dispatch contract, with bounded queueing and
close/lifetime tests; do not claim Present blocking caused this run's40Hz cadence.

## SDK-dispatched native UI pacing candidate

Native host now uses a60Hz steady-clock ticker to enqueue paint through
WindowedAppContext::CallInUIThreadDeferred, waking SDL's event loop. The worker
never renders and never waits for UI execution. At most one callback is pending
or executing; missed deadlines do not accumulate catch-up callbacks. Weak host
references and shared cancellation state make queued work inert after Stop.
Stop joins only the nonblocking dispatch worker; it does not wait for UI queue
drain. Occluded mode remains250ms. No simulation, VSync or GPU-fence bypass.
Win32 timer fallback remains for callers/tests without an SDK dispatcher; the
actual app supplies the dispatcher and disables that timer.

Full build/all18tests passed4.55s. Added real host lifecycle cases for bounded
queueing, Stop before queued execution and Stop/destruction inside deferred
overlay; expanded lifetime test passes0.43s. Preserved a named candidate
`edf2027-native-ui-pacing.exe`, SHA256
`E69624F4137889337CF10F5A2658279A9378742BAC6B013720AB27EBF76C6BE0`.
Startup PID2136 publishes an image with Xenos_loaded=false and no logged errors.
Automated window remains occluded, so visible cadence improvement is unverified.
Stopped owned process. User-visible re-test with host timings is still required.
The normal edf2027.exe now also includes this candidate and precomputed bindings;
older named delivered binaries and before-binding-layout backup are preserved.

## Visible UI scheduler validation

User-owned PID53352 (`edf2027-native-ui-pacing`) was observed live/responding;
no builds or screenshots were run while it was active. New user-session log
`out/build/win-native-clean/logs/edf2027_010.log` ran21:05:19..21:06:46 and ended
with title termination/hard exit. Agent did not close that process.

Steady host samples21:06:25..45:300-301 successful returns per5s,
interval_avg16.662-16.670ms, max19.55-21.39ms; Present~0.41ms and acquisition
~0.09-0.17ms. Compared with prior visible run's25ms mean and~46ms maxima, this
verifies removal of the fixed40Hz host delivery deficit in the observed run.
It does not prove exact scanout cadence or every workload. Game FPS in overlapping
buckets is49.9,49.8,42.6,41.2; repeated images48-96, skipped_sequences0. Hence the
remaining throughput limitation is real:60Hz host updates are not60unique game
frames. Startup still contains an84ms host gap/68ms acquisition spike.

After authoritative process termination, hardened ticker shutdown: a rejected
SDK dispatch now retires the worker instead of retrying a closed UI loop. Added
rejection test (exactly one dispatch attempt), expanded host test passes0.45s.
This shutdown-only refinement is source/test-built, not yet in delivered candidate.
Rendering performance, brightness and remaining Xbox contracts are still open.

## Native indexed mesh cost isolation

Added opt-in indexed.mesh and indexed.bindings scopes. Mesh scope includes
guest range validation for declaration/VB/IB, cache acquisition/comparison or
upload, and draw-range validation. Binding scope includes state lookup/creation,
target, render state, viewport and shader bindings. No rendering behavior changes.
Full rebuild and all18tests pass4.60s. Separate `edf2027-native-draw-cost.exe`
includes the ticker shutdown refinement and precomputed bindings.

Owned PID9848, log `native-draw-cost-20260909.log`, same mission-entry script,
profiling on, no capture prefix. At21:10:59,216906calls: mesh741.10ms,
bindings112.72ms, indexed.native961.63ms. At21:11:04,202540calls: mesh743.41ms,
bindings108.03ms, native955.22ms. These aligned inclusive reporting windows put
~77% of indexed native-hook time inside mesh preparation; they are not exclusive
whole-frame costs and profiling has overhead. Initial build/upload samples are
excluded from this steady comparison. Stopped owned diagnostic after late samples.

Next isolate range checks versus full buffer comparisons inside mesh preparation.
Do not skip comparisons on a cache hit without a verified mutation/invalidation
contract; dynamic contents and guest address reuse must remain correct. Existing
ValidateRange already has a whole-mesh min/max proof before its per-index fallback,
so blindly replacing it with another min/max cache would duplicate existing work.

## Mesh split: cache acquisition dominates range validation

Added mesh.ranges, mesh.acquire and mesh.draw_range opt-in scopes; hoisted the
same validated spans before Acquire. Source/ABI and actual validation behavior
otherwise unchanged. Rebuilt normal game and ran owned PID34040 with profiling
on and the same mission script, log `native-mesh-cost-20260909.log`.

At21:15:21,~202692calls: ranges64.891ms, acquire662.337ms,
draw_range7.590ms. At21:15:16,~216831calls: ranges68.933ms,
acquire677.442ms, draw_range7.662ms. Acquisition contains map lookup, exact
declaration/index/vertex comparisons and any rebuild/update. These inclusive
buckets identify the interval, not a breakdown inside the comparator. SDK
QueryRegionInfo scans forward beyond requested bytes in some cases, but this
run does not support it as the main mesh cost. Do not weaken memory checks.
Stopped the owned process after samples. Next isolate/optimize cache-hit content
comparison without losing detection of mutation or resource-address reuse.

## Fixed-width mesh-key comparator

Inspected optimized COFF object with llvm-objdump: declaration/index/vertex
std::equal already produces three memcmp calls. Do not claim replacing that
spelling with memcmp is a new optimization. Cache tree key ordering, however,
called out-of-line __std_mismatch_4 for each five-word key comparison (including
two call sites in Acquire and insertion sites). Added NativeMeshKeyLess using
numeric unsigned lexicographic comparisons with an inline fixed five-word loop.
All buffer comparisons, cache invalidation, byte snapshots and mutations remain
unchanged. This is a narrow lookup optimization, not a content-validation bypass.

Tests compare against std::array ordering across all five first-difference lanes,
equal keys, unsigned high-bit extremes and conflicting suffixes. Full dependent
build refreshed. Disassembly confirms generic mismatch calls removed while the
three memcmp content comparisons remain. Runtime gain still requires measurement;
do not attribute the entire previously measured Acquire cost to key comparisons.

## Key-comparator profile: only a small observed difference

Preserved BBA4C745...044094 as `edf2027-native-mesh-key.exe`, ran ownedPID59352,
same mission script and hook profiling, `native-mesh-key-20260909.log`.
Three acquisition buckets:223537calls666.53ms,217166calls663.77ms,
202263calls655.34ms. Prior corresponding workload buckets were223801calls675.13ms,
216831calls677.44ms,202692calls662.34ms. Per-call difference is roughly1-2%;
one non-frame-exact run does not establish a reliable speedup. Stopped owned run.
No visible pacing or60FPS claim follows from this profile.

Current mesh invalidation hook covers resource destruction (34220), not every
VB/IB data writer. The exact content comparisons remain necessary until a broader
write-tracking contract is implemented and tested. Read-only analysis search found
mesh layout/index research but is not by itself evidence of runtime immutability.

## Isolated SDK write-watch contract checks

Extended native_guest_memory_tests on separately allocated A/C/E physical heaps.
Actual volatile guest writes invoke a page-covering notification, subsequent
writes are one-shot in that alias, rearming fires again, and unregistering prevents
further callbacks. Reads retain access. Direct TranslatePhysical host writes
change the guest-visible bytes without firing a callback, as the SDK documents.
The targeted rebuilt test passes (0.06s), without loading rexgpu-xenos.dll.

This does not prove concurrent writer coverage, shared alias allocation changes,
or every live-game provider's explicit invalidation behavior. No production mesh
comparison was removed and no new game binary or FPS improvement follows from
this test. Any watch-based fast path must cover direct host writers and allocation
lifetime, with exact comparison fallback where the contract is unproven.

## Native mesh writer audit and allocation lifetime regression

Latest mesh-key log ends with 717 builds / 3,973,283 hits, 379 entries,
41,947,976 cache bytes and zero budget/entry evictions. Enlarging this cache
would not address the measured steady acquisition cost.

SDK source XFile::ReadInternal (src/system/xfile.cpp) bypasses guest protection
for physical reads but explicitly triggers exact-range callbacks after successful
ReadSync and before completion notification. ReadScatter delegates reads to that
path. XMA PrepareOutputRingBuffer uses TranslatePhysical for audio output;
its ownership/reuse contract still needs separation from mesh ranges. Source
search found no TranslatePhysical/physical_membase use in this project's src.
This search is not an exhaustive proof for arbitrary raw pointers or SDK binaries.
XFile's failure/partial-write behavior also remains outside the successful-read
invalidation proof. Do not promote unconditional watch-only caching from this audit.

Extended isolated A/C/E heap tests to exercise the explicit provider completion
API, verify its one-shot behavior, require release invalidation, reuse the exact
released allocation address and verify a newly armed guest write. Rebuilt targeted
test passes in 0.06s. These checks validate the installed runtime contract, not
just the separate SDK source. No live mesh comparisons have been bypassed.

## Native physical-buffer version metadata

Added GuestPhysicalVersions: scoped SDK notification registration and 4 KiB
physical-page atomic versions over the SDK's 512 MiB physical address space.
Callbacks use no renderer locks, and request no excess-page unwatching. Range
queries return the newest notification version touching that range. This is
native resource invalidation metadata, not a GPU command processor or eDRAM model.

Installed-runtime tests exercise writes on A/C/E heaps, one-shot/rearm behavior,
unrelated-page isolation, invalid ranges and the explicit lack of coverage for
direct host physical writes. The helper must be destroyed before SDK Memory.
It is not yet integrated into live mesh caching: matching versions alone do not
prove safety against unnotified writers or concurrent snapshot/upload writes.
Next integration should audit versions against exact comparisons before enabling
any skip, with lifetime tied explicitly to the runtime rather than static teardown.

## Live shadow mesh-watch integration

Added opt-in edf_native_mesh_watch_audit (default false). GuestMeshWatchAudit
snapshots physical VB/IB spans, compares every observed byte and tracks stable,
invalidated, missed-notification, unsupported and bounded-cache-reset counts.
Neither it nor GuestPhysicalVersions can authorize skipping production validation.
Snapshots are capped at 64 MiB / 1024 ranges; declaration checks remain unchanged.
The application owns the audit after OnPostSetup, the bridge holds only a weak
reference, and OnShutdown detaches under the bridge mutex before releasing it.
SDK ReXApp::OnDestroy invokes OnShutdown before joining the guest and destroying
runtime Memory. Callbacks themselves acquire no renderer locks.

Tests deliberately inject an unnotified physical write and require a missed count;
ordinary guest writes must instead increment invalidated, unchanged bytes stable,
and ordinary virtual memory unsupported. Full game build and all18tests pass4.87s.
Preserved candidate edf2027-native-mesh-watch-audit.exe, SHA256
2C07F3BD6D7DFC856D030042AA5A30B5257DAAD580E88CC3CA69B95ADDF33035.
Owned diagnostic PID57848 launched with mission-entry script and audit enabled,
hook timing disabled. This audit adds work/protection faults; its FPS is not a
clean performance comparison. Live evidence follows after the run.

Live run reached full Mission 1 world/HUD (native-mesh-watch-audit-20260909.png).
At21:40:30: checked15,940,000, unsupported0, stable15,938,275, invalidated5,
missed0, resets1. The reset is the audit snapshot budget/cap, not a production
mesh-cache eviction. Invalidated counts notification-version changes, not proof
that the corresponding bytes changed. No error/critical lines found in this run.
Requested normal close of the exact owned PID57848 via CloseMainWindow.
The run covers entry and stationary gameplay, not combat/destruction or all
provider failures. No production comparison skip or clean FPS claim is enabled.
Normal close did not terminate the process: it remained responsive and logged
new draws afterward. Revalidated the exact executable path and stopped owned
PID57848; WaitForExit returned true. Do not claim normal shutdown was verified.

## Mesh-watch combat and pause/resume coverage

Ran the same 2C07F3BD...F33035 audit candidate as ownedPID54064 using
native-untiled-combat-input.txt. Log native-mesh-watch-combat-20260909.log.
At21:46:15: checked28,400,000, unsupported0, stable28,398,141, invalidated5,
missed0, resets1. No error/critical log lines found. Audit remains shadow-only.

Captured firing/advancement under the footbridge with15/120 ammunition in
native-mesh-watch-combat-fire-20260909.png. Final capture shows player farther
down the road,110/120 ammunition after reloading, world/HUD intact. The filename
native-mesh-watch-combat-pause-20260909.png is misleading: it was captured AFTER
resume, not during the pause overlay. Logs record START at220001ms and235007ms;
indexed audit output pauses21:45:34.142..21:45:49.361, then resumes. This verifies
the transition without claiming a captured pause-overlay image. Stopped the exact
owned process after path validation; WaitForExit returned true.

Direct-writer audit: this title imports XMACreateContext (8252D5FC) and
XMAReleaseContext (8252D5EC), NOT XMAInitializeContext. The creation call in
edf2017_recomp.70.cpp returns at823C1C48 and writes each context handle through
record+64. Release loop sub_823C1388 reads record+64 and returns from the import
at823C13C8. Therefore intercepting the SDK initialize export alone cannot establish
audio output ownership for this title. Buffer configuration must be traced through
the game's inline context writes. SDK SDL audio_mute only silences queued output;
it does not disable decoding, so this run does not intentionally exclude XMA work.
SDK Win32FileHandle::Read reports bytes only on ReadFile success, so its interface
alone cannot prove that every failure left destination bytes untouched.

This extends observed coverage to movement/firing and pause/resume, not all enemy
destruction, all missions, every I/O failure, or a general concurrent-writer proof.
Next cache integration still needs explicit direct-writer handling; no FPS claim
or production comparison bypass follows merely from zero observed misses.

## Inline XMA output-page exclusion

Traced sub_823C1108 in generated/default/edf2017_recomp.72.cpp. Inputs are count,
12-byte descriptors, flags and output-owner pointer (r3..r6). On success owner+0
is count and owner+8 points to96-byte records. Record+68 retains the guest output
pointer, record+28 receives MmGetPhysicalAddress at823C1284, and record word0
bits22..26 encode the256-byte output block count, matching installed SDK
XMA_CONTEXT_DATA / XmaContext::PrepareOutputRingBuffer. Record+32 is work storage,
which the inspected native decoder does not use for output-ring writes.

Added a pass-through hook observing successful setup in audit mode. It marks the
output's physical pages permanently ineligible for that audit lifetime. Reuse may
cause conservative false exclusions, never a new eligibility claim. Counters now
include excluded separately from unsupported. Tests cover same-page overlap,
unnotified writes on excluded pages, unaffected next pages and range overflow.
All18tests pass5.93s after full game rebuild.

This is NOT yet a production writer-lifetime handoff: the constructor may already
activate contexts before returning, and other inline output reconfiguration still
needs audit. Existing full mesh comparisons remain. The hook preserves original
results and only logs/excludes in opt-in shadow mode.

Preserved edf2027-native-audio-exclusion.exe SHA256
D18733D8C478090F629D971F435FCF00A52EAEBC37854D466FA8F7EC5AFC893E.
Owned startup PID35268, native-audio-exclusion-20260909.log, confirms the hook
observed one real owner/one record at21:51:16.550. Startup XUI and immediate draws
continued, no error/critical lines found in inspected log. This is hook-reachability
evidence, not combat coverage for this new build. Stopped exact owned PID after
path validation; WaitForExit true. No game process intentionally left running.

Audio follow-up: factored pending/live output enumeration into
guest_audio_output_ranges.h and added a pre-call823C1F88 observer. The original
routine copies pending records into live contexts when required, then signals the
decoder. Both pending and live output ranges are conservatively excluded before
that call; construction coverage remains for deferred contexts. Tests cover
different pending/live buffers, null deferred context, zero capacity and overflow.
Full rebuild/all18tests pass4.63s. This hook has not yet received a live-run check;
the previous startup evidence predates it. Diagnostics remain disabled by default.

Work then returned to concrete command-encoding removal. The ECD8-specific C5F0
replacement and32 paired CPU-contract checks are documented in native-gpu-boundary.md.

## Allocation-free native instance constant patches

PatchGuestFloatRegisters previously allocated a vector for the entire variable,
copied all its bytes, patched selected lanes, then looked up/copy-compared the
whole variable again via SetConstant. The native instance-parameter hook calls
this for both ordinary and reverse-depth shader bindings.

Replaced it with prevalidated in-place lane patches: validate the furthest target
lane before mutation, endian-swap each four-byte lane without floating arithmetic,
compare only that lane, and mark the owning GPU constant buffer dirty on change.
Unpatched bytes/padding remain untouched. No buffer invalidation shortcut or
context-state cache is involved. Empty and optimized-out behavior is preserved.

WARP tests draw partial row/column matrix, vector-array and scalar-array updates,
including shortened last slots. Repeated/empty patches and rejected overrun are
checked; failed update leaves the rendered result intact. Additional bit readback
checks signed zero, NaN payload and infinity preservation. Full dependent build
and all18tests passed5.37s before the additional bit-pattern test; targeted rebuilt
draw test also passes. Runtime FPS impact remains to be measured, not inferred
from removing an allocation.

### Sequential clean comparison

Candidate `edf2027-native-constant-patch.exe` SHA256
EA7D67E9A0327810A2A1748B53A97C4EA9F6D43A903763F1B82B4D8B3072ED20
ran as owned PID38140; baseline `edf2027-native-cache-packet-free.exe` SHA256
FEBA97A02115D5F1E65AD88AB235ADE850D6EA3485FDE2E9A6136B774F6D1B3E
ran afterward as owned PID54112. Logs are respectively
`native-constant-patch-clean-20260909.log` and
`native-constant-baseline-clean-20260909.log` in out/native-bridge-run.
Both used native-flicker-input.txt, untiled=true, mesh audit/hook timings=false,
frametime logging=true, cap=0, native vsync=true, mute=true, fullscreen=false,
the same asset/user/cache roots, and no capture prefix. No concurrent builds.

For six five-second reports at t=145..170s, candidate FPS was
52.4,52.1,51.7,51.1,52.6,52.2 (mean52.017); baseline was
52.1,51.1,50.3,49.2,49.5,48.7 (mean50.15). This is a modest directional result,
not a causal or repeatability claim: simulation was not frame-identical, the
baseline also predates unsupported-caller guards, and visible scanout was not
measured. Neither demonstrates 60FPS headroom. Screenshots after measurement
show the stationary Mission1 player/world/HUD in both builds, at different
mission times. Both exact-path-validated owned processes were stopped and
WaitForExit returned true. The later candidate54..56FPS windows must not be
compared against earlier baseline windows.

### Native mesh ownership boundary investigation

Research note0330 supersedes the earlier one-subset file heuristic. Confirmed
against generated/default: sub_821D7530 (shard1) receives owner/source/stride/count,
allocates owner+32 storage through821D4700, copies stride*count bytes into the
pointer at owner+48, creates the resource through822CFDC0, and associates that
storage through822D01C0. It writes stride/count/size at owner+56/+60/+64.
Index creator821D76A8 (shard66) similarly copies count*2 bytes, creates through
822CFE58 and associates through822D01C0; count is owner+56. Neither creator is
currently hooked in src. Existing34220 destruction hook invalidates mesh keys.

These creators are a concrete candidate for native resource ownership, but a
constructor is NOT proof of immutability: the associated CPU buffer remains
accessible. Next trace822D01C0 and subsequent update/release paths before using
creation-time copies to replace per-draw comparisons. Preserve dynamic geometry
and address-reuse behavior; no comparison bypass has been enabled by this audit.

Lifecycle implementation: traced821D7468/821D75F8 (shards79/19), the vertex/index
owner cleanup invoked by both creators. They optionally unbind and free owner+32
storage via821D3EA0, without calling34220. Added native mesh retirement before
both original cleanup bodies; bridge locks are released before calling guest
code because stream unbinding reenters the bridge. Cleanup of an empty owner is
safe and conservative. This closes an embedded-owner lifetime gap; it does not
yet replace creation/upload or prove immutability of associated storage.
Full game build/all18tests pass4.77s. Added WARP cache tests for both VB/IB handle
retirement, repeated cleanup and identical-byte same-address recreation; rebuilt
quad test passes0.23s. These test cache semantics, not live reachability of the
new cleanup hooks. Combat/reload validation of this change is still outstanding.

## Native index-resource generations

Separated NativeIndexBuffer from shader-specific NativeIndexedMesh. One immutable
generation owns the D3D11 index buffer, original endian bytes, decoded index values
and min/max extent. NativeMeshCache weakly indexes generations by guest IB handle;
mesh/shader variants share matching generations. Vertex-only rebuilds retain the
index generation. Content/width changes create replacements rather than modifying
storage retained by earlier meshes/draws. Retirement removes the handle mapping
and dependent meshes; external references and queued D3D work retain old storage
until safe to release. Expired weak entries are pruned during construction.

Removed duplicate per-mesh CPU index snapshots; comparisons now use the native
resource's source bytes. Full comparisons are STILL performed per cache acquisition:
this is resource ownership separation, not yet producer-driven dirty tracking or
an FPS claim. Cache accounting conservatively charges shared storage per mesh,
preserving a bound without claiming exact physical resident bytes.

New WARP tests cover variant sharing, vertex-only replacement, index-width and
byte mutation, old-generation rendering after retirement, same-handle recreation,
and rejection of incompatible shared storage. Full game build and all18tests pass
5.37s after the final snapshot removal. No live gameplay run of this refactor yet.
Next validate combat/reload, then separate vertex storage and connect explicit
producer updates; do not infer buffer immutability from a successful constructor.

Live correctness run: preserved edf2027-native-index-ownership.exe SHA256
329397D16BCB6A6369BC8599C32EC58261ED42DD51A563074F825726C29D5AD2,
owned PID13920, native-index-ownership-combat-20260909.log. Used the combat script,
untiled=true, audit/hook timings=false, vsync=true, cap0. Viewed snapshots show
Mission1 player/world/HUD, then movement/turning with110/120 ammo and the pause
overlay, then the same changed view after resume with enemies visible. The first
snapshot named "firing" actually shows120/120 ammo and does not itself prove
firing; the later110/120 snapshot does show ammunition consumption. START events
at220/235s and subsequent indexed submissions confirm pause/resume progression.
No claim of pixel equivalence or full-game coverage follows from these images.
Stopped exact-path-validated PID after resumed gameplay; WaitForExit true.
This covers entry, bounded firing/movement and pause/resume, not mission reload
or proof that embedded-owner cleanup was reached. Gameplay remained below60FPS;
this was not a controlled performance comparison. Next: vertex ownership and
explicit update boundaries, plus reload validation of resource retirement.

## Layout-aware native vertex ownership

NativeVertexBuffer now owns the D3D11 allocation, original source bytes and the
conversion attributes/strides. NativeIndexedMesh retains the shader input layout
and references vertex/index storage. Removed the per-mesh duplicate vertex source
snapshot. Immutable vertex generations are shared by VB handle only when owning
device, guest/native strides, full conversion attributes and exact source bytes
match. Integer/default attributes are included; signed/unsigned layouts may share
identical packed bits, but float versus integer defaults must not. Incompatible
candidates produce a separate allocation rather than overriding shader layout.

Dynamic allocations are never shared between meshes. Their existing DISCARD
update converts/validates before mapping, updates source bytes only after success,
and leaves queued draws to D3D11's renaming semantics. No extra per-update source
allocation was introduced. Resource retirement clears both weak VB/IB mappings;
conservative per-mesh accounting still bounds cache storage. Source comparisons
remain on every Acquire until explicit producer mutation coverage is established.

Full dependent build/all18tests pass5.16s. Additional rebuilt WARP tests pass0.25s:
compatible vertex sharing, immutable replacement, old-generation pixel readback,
VB retirement/recreation, dynamic isolation, and float/int/uint default-component
conversion compatibility across ordinary/reversed shaders. Existing queued-DISCARD
readback and malformed-update tests also pass. No live gameplay validation of the
vertex change yet; the preceding combat run covers only the index refactor.

Live vertex follow-up: edf2027-native-vertex-ownership.exe SHA256
670C461B8F5CFFDCE109E6675BE82EDBEA69FAF483D5D284F2B3E82560277A96,
owned PID5220, native-vertex-ownership-retry-20260909.log. Used the existing retry
script with untiled=true, audit/hook timing=false, vsync=true, cap0. Viewed the
actual Restarting mission confirmation (default No), followed by scripted Up/A
at260/263s. Post-retry screenshot shows Mission1 player, civilians, ships and HUD;
indexed submissions continue (8,127,000 submitted/errors0 at22:43:27). Across
current/rotated logs there were zero error/critical lines. Exact-path-validated
owned process stopped; WaitForExit true. This validates retry progression with
the vertex refactor, not full unload/recreation of every resource owner, combat
coverage of this candidate, pixel equivalence, or improved FPS. All per-Acquire
source comparisons still remain.

## Model creation publishes native buffer metadata

Added NativeModelBuffers, a typed owner/address/size/stride/generation registry.
Hooks821D7530 and821D76A8 publish only after the complete model creator returns,
using its CPU allocation pointer at owner+48 and preserved input stride/count.
The creator's memcpy has already completed. Both embedded cleanup hooks and
final34220 retirement erase metadata. Size multiplication and address extents are
checked before publication. Registry generations distinguish owner recreation;
they are NOT content versions and do not authorize skipping byte comparisons.

Indexed rendering now gets address/size from native records for tracked model
buffers rather than decoding their Xbox fetch descriptors. Unknown producer
buffers retain the existing descriptor path. Stream offset/stride and draw ranges
are still validated, as are the source bytes by the existing native mesh cache.
First20 indexed-input logs report native_vb/native_ib to verify live routing.
Low-level Xbox header creators remain for CPU compatibility; this does not yet
remove those original functions or implement explicit content updates.

Tests cover typed lookup, exact byte sizing, invalid-address/overflow rejection,
retirement and owner reuse. Live verification of the new creator hooks and native
draw metadata routing remains outstanding. Prior vertex retry predates this change.

Live model-record verification: edf2027-native-model-records.exe SHA256
0D4E2146C8F4CCE6F1DA6285EC9C622AE967BA42E846E9F958A6854E14A75A44,
owned PID60292, native-model-records-20260909.log. First20 indexed draw records
all report native_vb=true/native_ib=true, proving the creator hooks feed the new
metadata route for those draws. Continued to Mission1 gameplay; viewed screenshot
shows player, soldiers, ships, buildings and HUD. Zero error/critical log lines.
Stopped exact-path-validated process; WaitForExit true. First20 routing evidence
is not a census of every later buffer. No combat/reload or pixel-equivalence
claim for this candidate. Gameplay samples at150/155/160s were54.2/53.9/52.7FPS;
this was a correctness run, not a matched performance comparison. Explicit content
update tracking and removal of source comparisons remain unfinished.

### Underlying model allocation lifetime

Traced821D4700 (shard0) through821D3DC8 (shard47) and821D4490 (shard62).
The creator supplies owner+32 as the pool record. Allocation rounds to16bytes,
stores pool/list bookkeeping at record+0/+8 and publishes its data pointer at
record+16 (=owner+48). Generic release accepts pool/record in r3/r4, frees the
recorded range, and clears record+16 under the guest critical section. Observed
model addresses such as f298f990/f298fa40 share physical pages; page invalidation
alone is not an exact per-resource content change. No relocation claim follows
from this allocator trace.

Added a generic821D3DC8 pre-hook to retire native metadata and dependent meshes
when r4 exactly matches a registered owner's allocation record. Unrelated pool
allocations are ignored. Bridge locks end before original guest code enters its
critical section. This covers direct pool release/reallocation that need not pass
through the higher-level cleanup hooks. Tests cover underflow, non-owner records,
repeated release and both buffer kinds. This remains lifetime tracking, NOT a
content-write notification and does not remove comparisons. Live reachability and
runtime cost of the additional pool hook have not yet been measured.

## Explicit VB/IB unlock update notifications

Traced vertex Lock82134958 (shard24), index Lock82134A78 (shard76), and their
Unlock wrappers821349B8 (shard71)/82134AD8 (shard16), both forwarding to82134640.
The vertex caller at8242D4A8 locks120bytes, calls memcpy, then unlocks at8242D4BC.
The index caller locks at8242D3A8, writes six16-bit indices and unlocks at8242D3DC.
These are actual producer boundaries, unlike inferring changes from draw calls.

Added post-original Unlock hooks: notify registered model metadata of a completed
update and retire dependent native meshes so the next draw acquires fresh storage.
Unregistered buffers also invalidate their native mesh handle. Lifetime generation
stays separate from observed update count; recreating an owner resets update count.
Original coherency/wait code remains unchanged, and bridge locks are not held over
guest calls. First8 notifications log owner and whether model metadata was present.

This does NOT establish that model/other buffers only change through Lock/Unlock;
the model loader itself copies directly. Full source comparisons remain until
writer coverage is established. Unit tests cover known/unknown notifications and
lifetime/content separation; live reachability of the new hooks remains pending.

## Native VB/IB lock cache-packet removal

The shared lock helper82134408 still encoded render-thread cache packets in
loc821344E8..82134570. Added a fingerprint-guarded extracted CPU tail removing
that block only. Native operation10/12 (VB/IB wrappers) dispatch to the new tail;
other operation kinds and non-native mode retain the original implementation.
Resource waits, off-thread dirty-range accumulation, resource dirty bounds, alias
selection, atomic lock-count increment and return ABI remain in the extracted
body. The render-thread packet reservation/cursor advance is intentionally absent.

48 paired fixtures run original and native bodies across VB/IB, same/other thread,
cached/ordinary resources and flags0/1/2/16/17/4096. They compare resource state,
dirty-range output, returned address, stack/LR/nonvolatile registers; native packet
bytes remain zero and its command cursor does not advance. Thread-ID and atomic
range provider are controlled stubs; resource-fence values are zero in these
fixtures, so this is not a new live fence-completion test. The wait code itself
was not edited. Full game build/all18tests pass5.55s. Live lock/unlock reachability
and regression validation remain pending; no FPS gain claimed.

Buffer-lock fence follow-up: expanded to96 paired cases with zero/nonzero resource
fences. A controlled C928 provider records device/fence/access-type/resource and
asserts it runs before lock-count modification or packet cursor advance. Both
read/write fence selections (+8/+12, flags4112 mask) and operation10/12 agree
between original/native. Targeted rebuilt tail test passes3.42s. This verifies
wait dispatch and ordering, not actual GPU completion latency or live lock-path
reachability. Moved these fixtures inside the test exception reporter as well.

Live update follow-up: edf2027-native-buffer-updates.exe SHA256
E8DEF95CFE3183C9F66F19032C4E0BBEBD7569F4B7025A6271C5DE11C857E5B9,
owned PID43280, native-buffer-updates-20260909.log. Explicit unlock notifications
occur during startup and report model_tracked=false. Thus the hooks are reached,
but they do NOT establish that model-buffer mutations are covered by Unlock.
Continued to Mission1; viewed screenshot shows world/player/soldiers/ships/HUD.
No error/critical lines in current/rotated logs. Stopped exact-path-validated
process; WaitForExit true. Gameplay remained about53FPS in the inspected settled
samples; no performance win claimed. This run validates entry/rendering with the
pool-release and lock/unlock changes, not all resource-release paths, combat,
or live nonzero-fence completion. Next focus remains direct model data writers,
not treating successful Unlock instrumentation as permission to skip comparisons.

## Deferred bulk-write notifications

Hooked the actual821E8320 copy routine after its original body returns. Physical
destinations are resolved through SDK heap/address translation (including aliases)
and queued without acquiring renderer locks or retaining guest pointers. Model
publication records a checked contiguous physical extent where available. At an
indexed submission boundary the renderer drains completed ranges and retires
overlapping model meshes, incrementing observed-update counts once per batch.
Nonphysical copies are not tracked by this hook. Invalid physical extents or a
full256-range queue cause conservative whole-registry invalidation, not dropped
notifications. First8 drain batches report range/overflow/affected-owner counts.

Queue tests cover nonoverlapping writes on a shared page, physical-coordinate
overlap, duplicate batch ranges, writes spanning owners, empty drains, concurrent
producers and conservative overflow. Full game build/all18tests pass5.76s; the
additional bounded batch logging was rebuilt afterward. Live cost/reachability
has not yet been measured. Initial creation copies can still be queued when their
new owners publish, so these may conservatively invalidate a newly created owner;
affected counts do not prove post-creation mutation. Notifications also do not
synchronize concurrent copies with rendering. Inline/scalar stores, other copy
entry points and native physical writers remain uncovered. Full byte comparisons
therefore remain mandatory; this is producer coverage, not a safe fast-path proof.

Bulk-write live run: edf2027-native-bulk-writes.exe SHA256
CA8A63C9F56304F0B596FD345D455650A4DB97404E215BD20E8EDE405F480F25,
owned PID48468, native-bulk-writes-20260909.log. Initial drain overflowed256ranges
and conservatively affected7932 registered owners; the next seven logged batches
each held14ranges/affected0. Mission1 screenshot shows intact player/world/HUD;
zero error/critical logs. At23:17:03 there were721mesh builds/377entries, not a
continuous all-mesh rebuild pattern. Settled samples150..181s were52.0..54.6FPS,
not a matched performance comparison. Stopped exact-path-validated process and
WaitForExit returned true. Bulk notifications are reached, but initial overflow
and newly published owners prevent attributing the first batch to later mutations.

The run exposed avoidable work: full owner scans for unrelated writes. Replaced
that lookup with a lazily rebuilt sorted physical interval index with prefix-max
ends. Binary search plus bounded backward overlap traversal handles nested/aliased
ranges without missing an earlier long interval. Matched owners are deduplicated
per batch; creation, replacement and either retirement path dirty the index.
Overflow still invalidates all. Added240 queries checked against a linear oracle
over96 overlapping ranges, including retirement/recreation and empty writes.
Runtime impact of this indexed lookup has not yet been measured.

### Independent forward-copy producer

Audited generated `sub_821E8740` (shard46): a standalone forward-copy loop
with byte alignment, aligned/unaligned-source word loops and byte tails. There
are27 direct generated call sites; it does not delegate to821E8320. Connected
completed physical writes to the same deferred native-buffer notification queue
as821E8320, sharing address/alias/extent validation. Entry r3/r5 are saved before
calling the original, since r5 is consumed by the leading-byte loop. Non-native
execution remains original and no renderer locks are acquired by this producer.

The test fixture now extracts the actual8740 routine and verifies2080 combinations
(4destination alignments x4source alignments x130lengths, including zero), checking
copied contents, returned destination and leading/trailing guards. This proves the
copy extent ABI used by the hook, not live model-buffer mutation coverage or the
full hook's integration. Full game build and all18CTest entries passed (5.71s).
No live run or FPS comparison was performed for this change. Scalar/inline and
other native writes remain uncovered; per-draw comparisons remain enabled.

### Native index binding producer

Audited SetIndices821375C0 (generated shard31): r3=device/r4=resource,
including null unbind. It writes device+12164 after stamping the old resource's
fence or adding its deferred-retirement record. The new post-call hook retains
that original CPU bookkeeping and publishes a per-device native index binding.
Indexed native draws consume this binding instead of reading device+12164.
A missing producer is an error (`map::at`), not silent reuse or a guest-slot
fallback. Diagnostic guest-slot reads and original CPU consumers remain.
This owns the binding only, not all resource contents or the whole device API.

Build/all18tests passed (5.49s). Live candidate
`edf2027-native-index-binding.exe`, SHA256
45D79F60B83E9325DB9992034D6A403852E93668A57B5C0B236F3364BD6BF977,
owned PID6452, native-index-binding-20260909.log. With native-flicker-input,
first indexed draws reached native VB/IB metadata; by23:31:14 the run submitted
11,143,000 indexed draws with0errors,721builds/377entries/noevictions.
`native-index-binding-world-20260909.png` shows Mission1 player, city, mothership
and HUD. The earlier screenshot during asset loading was black and was not
treated as a successful render. Exact-path-validated process was stopped after
the world capture. This also exercises the prior interval-index and secondary
copy-hook changes together, but is not a matched FPS test or teardown coverage.

### Current-cost profile after native index binding

Reused the exact45D79F60 candidate above with hook timings enabled, otherwise
the same entry script/native-untiled/vsync/audio-muted flags. Owned PID50520,
`native-cost-profile-20260909.log`; world screenshot confirms player, civilians,
ships and HUD. Exact-path-validated process stopped and WaitForExit returned true.

Report timestamps23:36:00..23:36:30 (30s window), inclusive CPU wall-time totals:

| Phase | Calls | Total ms |
| --- | ---: | ---: |
| indexed.native | 3962499 | 10378.05 |
| activation.native | 1332555 | 8003.68 |
| indexed.mesh | 3962499 | 6744.10 |
| mesh.acquire | 3962499 | 5141.19 |
| indexed.bindings | 3962499 | 1857.97 |
| instance.native | 3567430 | 1838.95 |
| mesh.ranges | 3962499 | 1257.66 |
| activation.original | 1332551 | 829.18 |

Indexed subphases are nested, not additive. Reporting buckets can straddle window
boundaries and trailing calls are absent. Instrumentation adds overhead; observed
later FPS51.8..54.3 is not a clean-build benchmark. Mesh acquisition includes
lookups, declarations, source comparisons and occasional builds, not comparisons
alone. Native material activation costs more than acquisition in this window,
so eliminating comparisons alone must not be described as eliminating the frame
bottleneck. Next performance work should isolate material parameter/texture binding
costs alongside the unfinished buffer mutation contract, rather than continue
moving individual device-field reads and imply significant speedup.

Also checked the SDK source memory-protection path: system/xmemory.cpp ToPageAccess
maps readable writable/write-combine guest allocations to kReadWrite; core/
memory_win.cpp maps that to PAGE_READWRITE, without PAGE_NOCACHE or WRITECOMBINE.
This code does not support the hypothesis that retained guest caching flags make
these CPU comparisons uncached. No live page-protection survey was performed.

### Direct full constant uploads

SetGuestFloatRegisters now validates the furthest reflected lane before mutation,
then endian-converts directly into owned native constant-buffer bytes. Removed the
thread-local scratch vector's repeated zero-fill, full-buffer comparison and copy.
Unlike partial patches, full uploads still zero all reflected padding, including
array/matrix gaps and a truncated final slot. Changed lanes/padding mark dirty.
No source-comparison bypass or persistent guest-memory cache was introduced.

Full game build/all18tests passed (6.08s). Added repeated full uploads, rejected
short upload preserving prior output, and signed-zero/NaN-payload/infinity checks.
An additional WARP test poisons each reflected matrix/vector-array/scalar-array
variable with0xcd, performs a full upload, and reads back the actual GPU constant
buffer to compare every reflected byte against the previous packing algorithm's
zero-padded result. That draw test also passed (0.29s).

Candidate edf2027-native-constant-direct.exe SHA256
52EE0581D537BE6067BCFAA1CE68FD043E9ACE2D702965ABDA503198A1FC5BD3,
PID60668, native-constant-direct-profile-20260909.log, same scripted entry and
profiling flags as the prior baseline. Screenshot confirms player/city/civilians/
mothership/HUD; no error/critical logs. Stopped exact-path-validated process and
WaitForExit returned true. Candidate report window23:43:11.564..23:43:41.564
matches baseline window's elapsed154.159..184.159s from first log timestamp:

| Phase | Baseline average us/call | Candidate average us/call |
| --- | ---: | ---: |
| activation.native | 6.0063 | 5.6201 |
| indexed.native | 2.6191 | 2.6184 |
| mesh.acquire | 1.2975 | 1.2811 |

Material activation's observed average fell about6.4%; this is one sequential
profile pair, not a repeated controlled benchmark. Candidate155..180s samples
were51.7..53.2FPS, baseline155..181s50.0..52.3. Later candidate samples dipped to
45.5/46.8FPS at195/200s, unlike baseline: no stable60FPS or broad pacing improvement
is established. World evolution and scheduler variation are not controlled;
profiling overhead remains. The comparison supports retaining the simpler direct
upload provisionally, but not treating this small change as the performance fix.

### File-read producer gap

Audited non-graphics SDK TranslatePhysical users: XMA output remains an untracked
native writer (the existing audit excludes its pages); file reads bypass virtual
protection in XFile::ReadInternal. That function notifies physical watchers only
on XSUCCEEDED. HostPathFile delegates to the platform FileHandle and maps false
to END_OF_FILE; no untouched-destination guarantee was established for failures.
NtReadFile_entry currently uses `if (true || file->is_synchronous())`, completing
Read before returning, including handles for which it subsequently reports PENDING.

Added edf_native_NtReadFile adapter: capture original r8 destination/r9 length,
call the original SDK import unchanged, then queue a conservative completed-write
notification regardless of returned status. Physical-extent validation and queue
overflow behavior are shared with the two copy adapters. No renderer locks enter
the SDK call. This does not serialize concurrent reads/draws or make version-only
reuse safe; a future genuinely asynchronous SDK implementation requires re-audit.

CMake privately redirects __imp__NtReadFile for edf2027_recomp only, covering the
title's direct calls and generated function registration without editing generated
code or the SDK. The bridge still resolves the original import symbol. Object
symbol inspection confirmed shard23 and edf2017_init reference the adapter.
No NtReadFileScatter import was found in this title; other future imports/native
XFile callers are not implicitly covered. Full rebuild and18tests passed (5.42s).
The tests do not simulate failed physical file reads through the complete adapter;
live I/O verification remains outstanding. Per-draw comparisons stay enabled.

Added an extracted fixture of the actual edf_native_NtReadFile adapter (not a
hand-written imitation). Its controlled synchronous provider mutates one byte
even on failure, clobbers r8/r9, and returns success/PENDING/END_OF_FILE. Nine
status/length combinations (0/1/16bytes) verify notification after provider
completion, preserved entry extent, unchanged returned status/register state,
and queue-to-model-owner invalidation even for failure. Zero-length reads do not
invalidate. The fixture substitutes physical-coordinate translation and the SDK
provider, so it establishes adapter ordering/ABI and queue integration, not actual
filesystem failure behavior or concurrent I/O safety. Full build and18tests pass.

Live integration: edf2027-native-file-watch.exe SHA256
A7427FD428196319D3612201BABF7162765961FB8B0BD35F5B831C19CCC0D1C0,
owned PID23448, native-file-watch-20260909.log. Same entry script/native-untiled/
vsync/audio-muted flags, with mesh_watch_audit=true and hook_timings=false.
Mission1 screenshot native-file-watch-20260909.png shows player/city/mothership/HUD.
At23:56:43.956 the shadow audit reported19,000,000checks,18,998,569stable,
581invalidated,0missed/unsupported/excluded/resets. Audio exclusion callbacks
were reached both after construction and before decoder submission (ranges1/2).
No error/critical logs. Stopped the exact-path-validated process; WaitForExit=true.
This exercises normal loading with the redirected read import and prior changes,
not forced filesystem failures or all missions. Audit mode is not a speed test.

Concurrency boundary confirmed in SDK mmio_handler.cpp: an unrecognized protected
memory access calls the physical access-violation callback, then returns to retry
the faulting CPU instruction. Version notification is therefore pre-write, not
completion. A snapshot that observes the new version before that write executes
must not be treated as permanently current. No-miss runs do not establish this
ordering contract. Next ownership work must address concurrent writers explicitly
(for example, rejecting foreign-thread-written pages from a version-only path),
as well as native physical providers. Comparison bypass remains disabled.

### Foreign-thread callback classification (2026-09-10)

GuestPhysicalVersions now claims one drawing-thread lifetime token and permanently
marks pages invalidated by another thread. Tokens are not reused when OS thread
IDs are recycled. Flags publish before the callback returns to retry a faulting
store and never clear on re-arm. The shadow audit rejects later drawing-thread
changes and foreign-written pages; it also rechecks foreign flags around snapshot
validation. New `foreign` counters distinguish this from unsupported/audio pages.
This is still shadow-only, not a complete ownership proof or a fast path.

Real SDK memory tests cover same-thread writes, worker-thread faulting writes,
re-arm persistence, alternate-alias writes and attempts to change audit owner.
The first cross-alias test failed, exposing an additional real coverage hole:
PhysicalHeap::EnableAccessCallbacks only protects aliases with writable guest
page metadata. An unallocated alternate alias can nevertheless be host-writable.
A worker's store through such a C alias changed the original allocation's bytes
without advancing its watched physical version or marking it foreign. The test
now explicitly records that SDK bypass behavior rather than asserting all aliases
are watched; allocated writable aliases must deliver the foreign notification.
Therefore even single-drawing-thread classification cannot yet authorize reuse.
Full build/all18tests passed (5.57s) with that bypass retained as a known limitation.
No live run of the new classification was performed. All earlier no-miss live
results remain observations of those runs, not proof against this reproduced case.

### Supplemental native alias watcher (2026-09-10)

Added NativeAliasWatch to GuestPhysicalVersions, currently instantiated only by
the opt-in shadow audit (and tests). It protects committed PAGE_READWRITE host
pages in A/C/E aliases whose guest metadata is inaccessible, filling the SDK's
unallocated-alias gap without claiming allocated/read-only pages. A first-chance
write handler notifies the requesting trackers, restores writable protection and
retries the original CPU instruction. Registry, protection, fault and teardown
operations share the SDK's recursive memory lock. Pages are one-shot; overlapping
trackers share protection until the last observer retires. Real guest read-only
protection is not relaxed. An alias that subsequently gains an SDK watch has that
watch notified/cleared before restoring writable guest protection.

The previously failing cross-alias test now requires a version increase and a
foreign-thread marker, including through unallocated aliases, and passes. Added
overlapping watcher destruction, post-destruction writes, original writable
protection restoration, and stale-registry re-arm tests. Re-arm notices a page
externally made writable, invalidates old snapshots and actually re-protects it.
Full build/all18tests passed (9.47s). No live test of this new exception handler
has run yet. It does not cover direct physical-provider writes or turn pre-write
notifications into completion notifications; it closes the reproduced alias
case, not the entire comparison-removal contract. Default rendering is unchanged
because mesh_watch_audit remains false, and comparisons remain mandatory.

### Physical-provider completion and fail-closed audit (2026-09-10)

Completed NtReadFile writes now also notify armed SDK physical aliases through
NotifyPhysicalProviderWrite when mesh_watch_audit is enabled. The helper clips
the physical extent to each A/C/E alias under the SDK memory lock. A controlled
direct-physical write test verifies version advancement after completion. The
extracted file adapter fixture requires the version-notification flag. This is
completion notification, not serialization against an in-progress provider.

Audio output observation errors now permanently disable the shadow audit rather
than merely logging and continuing to classify buffers as stable. Disable clears
snapshots and all subsequent checks count as unsupported; repeated disable and
checks across previously excluded/foreign ranges are tested. Native rendering
still retains byte comparisons. Full rebuild and all 18 tests passed (12.02s).

Live supplemental watcher validation remains incomplete: PID60032, preserved
edf2027-native-alias-watch.exe, native-alias-watch-retry-20260910.log. Captures at
00:21 and 00:22 show the title's Press START screen, not Mission1; approximately
60 FPS UI logs must not be reported as gameplay performance. Live input reload
is confirmed and further entry events were appended to the same process's script.
This preserved executable predates the provider-completion and fail-closed edits.

### Combined watcher gameplay validation (2026-09-10)

Stopped exact-path-validated PID60032 (WaitForExit=true), which did not establish
model coverage after repeated title-entry attempts. Launched current edf2027.exe,
SHA256 100CB4EFA6D06441CDE8F0FBEBB5E19C8EC0B68D9E020B33CC0834303CE7078D,
PID38324, with native-flicker-input.txt, input tracing and mesh_watch_audit enabled.
Log: native-watch-input-trace-20260910.log (include rotated .1.log). This build
contains supplemental alias watches, physical file completion notification and
fail-closed audio observation. Guest input tracing confirms START reaches user0;
the normal early entry schedule reaches New Game and Mission1. The precise cause
of the previous late-entry loop is not established; do not claim an input fix.

At 00:27:47.088: 5,280,000 checks, 4,530,957 stable, 748,316 foreign-thread
exclusions, zero missed/invalidated/unsupported/excluded/resets. Screenshot
native-watch-input-trace-world.png shows player, city, mothership and live HUD.
No error/critical lines were found in the current/rotated logs at that checkpoint.
This is actual combined gameplay validation, but not a concurrency proof, all-game
coverage or a speed benchmark (both shadow comparison and input tracing enabled).
Foreign exclusions rise during gameplay: a blanket version-only fast path is not
authorized by these results. Mutable resources require a stronger ownership/write
contract; eligible immutable resources may be handled selectively. Comparisons
remain enabled. Stopped exact-path-validated PID38324 after collecting evidence;
WaitForExit=true.

### Native declaration binding (2026-09-10)

Related follow-up: see Native shader binding below; UI snapshot readers still
contain redundant guest declaration/shader identity checks and remain to migrate.

Model creators 821D7530/821D76A8 copy source bytes into pooled owner+48 storage,
then associate the embedded resource. Their metadata publication does not itself
establish immutability. No model comparison bypass was enabled from this review.

Moved vertex-declaration binding ownership to the native bridge. Generated-code
search finds two explicit writers of device+11536: 82149A90 (direct setter) and
8214B0A0 (FVF conversion via 82147BA0). Both now publish after original CPU work.
The FVF path publishes returned r4, not the input FVF. Indexed, movie, immediate
and relevant quad draw consumers use the native per-device binding map; missing
producer state throws instead of falling back to the guest slot. Two diagnostic
reads remain, as do declaration-content reads/comparisons and CPU dirty flags.
This transfers binding state, not declaration storage ownership or content reuse.

Extracted both actual generated routines and actual bridge hooks into the tail
fixture. Twelve paired cases cover direct/FVF setters, two devices, replacement
and explicit null binding. A controlled FVF provider returns a distinct handle;
checks establish publication after CPU slot update, identical guest memory and
preserved return/stack/nonvolatile state. Full build and all18tests passed12.10s.

Live executable SHA256 2FCEE01ADC9B5965499D15AFC8F37E5F8F0D7A70ED6584AFB11A9B8B3EC691CD,
PID15204, native-declaration-binding-20260910.log. Normal early entry script,
untiled scene/vsync enabled, audit and hook timings disabled. At00:35:10.115:
6,714,000 indexed submissions, zero errors,722mesh builds,378entries,noevictions.
native-declaration-binding-world.png confirms Mission1 player/city/mothership/HUD.
An earlier intro/transition capture at00:34:14 showed a small tiled image within
black surroundings; later frames were black during further loading, then gameplay
became visible. The cause of that transient image is not established: do not claim
full intro visual fidelity from the successful gameplay capture. No error/critical
logs found. Not a controlled FPS comparison. Stopped exact-path-validated PID15204;
WaitForExit=true. Byte comparisons remain enabled.

### Native shader binding (2026-09-10)

Added NativeShaderState and post-original hooks for vertex setter821498C8 and
pixel setter82149608, the two explicit generated stores to device+12420/+12416.
Original constant application, dirty flags, old-resource fences and deferred
retirement remain intact. The native state distinguishes missing publication from
explicit null unbind and keeps devices separate. Indexed draw validation, immediate
draw shader selection and instance-constant ownership use these native identities,
not the guest device shader slots. This does not remove shader-link validation or
mesh comparisons. UI snapshot readers still read/compare shader/declaration fields;
their migration is a remaining consumer-side task, not completed by these hooks.

Extracted actual generated setters and bridge hooks into the tail fixture.24paired
cases cover stage/device/replacement/unbind/old-resource-fence combinations against
original CPU memory and return state. Shader metadata payloads are zero in these
fixtures (embedded-constant paths are not newly proven); deferred allocation is a
controlled throwing provider, not exercised. Missing/partial pair and explicit null
state are tested. Full build/all18tests passed12.13s.

Live SHA256170E1C04EF9D8A7C96F855B4D9516A9AC3136D3E811CF0718569E1B4CF413046,
PID39312, native-shader-binding-20260910.log; early title-entry script, native
untiled/vsync enabled, audit/hook timings disabled. At00:41:48.584:4,973,000indexed
submissions,zeroerrors,721builds,377entries,noevictions. Screenshot
native-shader-binding-world.png shows Mission1 player/city/mothership/liveHUD.
No error/critical logs found in current/rotated logs. Earlier mothership screenshot
remains visibly overbright; brightness is not fixed by binding ownership. Stopped
exact-path-validated PID39312;WaitForExit=true. No controlled FPS claim.

### UI consumers use native binding identity (2026-09-10)

Removed shader/declaration identity from GuestXuiDeviceWords. XUI, font and Utility
draws now use the same setter-owned native shader/declaration state as other draws,
without duplicate comparisons against device+11536/+12416/+12420. Surface, texture,
viewport and render-state validation remain. The snapshot range and outer native
draw windows now end at12416, before the old shader slots. Existing diagnostic
reads on error/unsubmitted paths remain, not rendering fallbacks.

The snapshot fixture changes the old declaration slot and truncates memory before
the shader slots, yet obtains identical remaining state. One-byte truncation of
the required viewport range still rejects; updated surface/texture/render-state
and overflow tests pass. Full build/all18tests passed12.38s. This removes identity
reads, not declaration-content decoding or geometry byte comparisons.

Live candidate started as PID3632, SHA256
2EF6770058D3109F4BB45A5709F18823C1A08914BCA1499F2891DE466083E1CC,
native-ui-binding-20260910.log, early entry script, native untiled/vsync enabled,
audit/hook timings disabled. Menu capture native-ui-binding-menu.png shows mission
selection and text. At00:47:49.685,7,316,000indexed submissions,zeroerrors,722builds,
378entries,noevictions. native-ui-binding-world.png shows player/city/mothership
and HUD in Mission1. No error/critical lines found in current/rotated logs. Intro
still overbright. This is integration coverage, not an FPS benchmark or full-game
visual proof. Stopped exact-path-validated PID3632;WaitForExit=true.

### Native declaration content ownership and indexed comparison removal (2026-09-10)

Traced allocated declaration constructor82149AB0: count terminated12-byte input
records, allocate56+count*12, publish count at+24, copy to+52, return owner or0 on
allocation failure. FVF converter82147BA0 writes declaration contents in place at
entry r4; its sole direct caller is8214B0A0. Research0331 locates model declarations
through the actual subset loader, not scanned patterns.

Both producers now publish immutable NativeDeclaration copies after original CPU
work. NativeDeclarations maps handles to shared immutable identities; publication
replaces identity, final resource destruction retires lookup, existing native/cache
references retain old storage. Indexed draws take count/contents from this native
record, not guest declaration memory. Missing publication fails; no silent fallback.
NativeMeshCache accepts an optional immutable declaration identity, validates that
it owns the supplied span, and uses identity equality on hits instead of declaration
byte comparison. Legacy span callers retain exact comparisons. Vertex/index source
comparisons remain enabled. Other draw paths still decode guest declaration contents;
their native-content migration remains outstanding. Arbitrary writes outside the
declaration API are not tracked by this ownership contract.

WARP cache tests cover copied source independence, identity reuse, unrelated-span
rejection, malformed publication preservation, retirement, handle reuse and drawing
with an older retained identity. Actual generated constructors and actual hooks are
extracted for16paired cases plus allocation-failure cases. Initial textured-FVF
fixture crashed because its fixed lookup at8200BFF8 was unmapped. Sparse mapping and
a controlled size/type table fix the fixture; these tests establish publication and
CPU ABI, not the retail lookup contents. Final all18tests passed15.12s. Production
build passed earlier; subsequent changes were fixture-only, live executable unchanged.

Live SHA256DFB9649A2E6B0F55A1424F6FD351570C738CD5DCD9A31ABDCA73536251C5A89C,
PID28944, native-declaration-owner-20260910.log, normal early entry script, native
untiled/vsync enabled, audit/hook timings disabled. At00:57:08.475:16,307,000indexed
submissions,zeroerrors,723builds,379entries,noevictions. Screenshot
native-declaration-owner-world.png confirms Mission1 player/city/mothership/HUD.
No error/critical logs found. Stopped exact-path-validated PID28944;WaitForExit=true.
No controlled FPS benchmark or all-game visual proof. This removes indexed
declaration comparison, not vertex/index-buffer comparison or all Xenos dependencies.

### Remaining declaration-content consumers (2026-09-10)

Movie, XUI, font, Utility2D/3D and ordinary immediate draws now obtain declaration
contents/counts from NativeDeclarations. NativeDeclaration provides bounded endian
word decoding; short and overflowing offsets reject, including reads from empty
declarations. Font checks count before reading its two elements. Layout validation
still checks native content against supported layouts; this is not guest memory
comparison. Immediate diagnostic element decoding now runs only for the first five
draws. Only unsubmitted/error diagnostics retain guest declaration-content reads.

Utility3D passes its immutable declaration identity into NativeMeshCache, removing
its declaration-byte cache comparison too. Utility2D still creates a specialized
converted native layout and uses the span cache path; that native-to-native layout
comparison remains, as do vertex/index comparisons. No claim of removing all draw
validation. Full build/all18tests passed12.44s, including bounded native word reads,
endian/count checks, empty reads and existing ownership/cache/producer fixtures.

Live SHA2568EB1BCE0C19C80BCA83169C150CFEEA777BCE9D779F2970F35B736AF62E28478,
PID22300, native-declaration-consumers-20260910.log. Early entry script, native
untiled/vsync enabled, audit/hook timings disabled. At01:03:21.107:6,568,000indexed
submissions,zeroerrors,723builds,379entries,noevictions. Screenshot
native-declaration-consumers-world.png confirms Mission1 player/city/mothership/HUD.
No error/critical lines found in current/rotated logs. Intro still overbright. No
FPS benchmark or full-game visual claim. Stopped exact-path-validated PID22300;
WaitForExit=true.

### Utility2D converted layout ownership (2026-09-10)

Utility2D conversion is now lazy, once per source NativeDeclaration identity and
solid/textured variant, using call_once. Converted immutable declarations have
independent shared lifetime; exceptions leave only that variant uninitialized.
The bridge uses the converted identity in NativeMeshCache, removing per-draw
Utility2D layout reconstruction and declaration-byte cache comparison. Vertex and
index byte comparisons are unchanged. Source generation replacement naturally
gets fresh derived variants; there is no global address-keyed conversion cache.

Solid/textured WARP fixtures request concurrent first conversion from four threads,
require one published identity and exact equality with the original converter,
reject the wrong variant repeatedly without poisoning the valid variant, release
the source and reuse the converted layout in NativeMeshCache. Existing pixel
readback and line topology assertions now draw via that identity-backed cache.
Full build/all18tests passed14.84s. No new live game or FPS run was performed;
no game process remains running. Prior gameplay results are for the preceding
candidate, not proof of this last integration change across the whole game.

### Native generated index ownership (2026-09-10)

Utility2D/3D no longer rebuild quad, line or strip uint16 indices per draw.
NativeGeneratedIndexCache owns immutable generated patterns keyed by topology and
vertex count, bounded to64entries/4MiB with least-recently-used eviction. Oversized
patterns remain uncached; external mesh users retain storage across eviction.
NativeGeneratedIndices can only be constructed from a supported native pattern,
not arbitrary guest bytes. Counts are bounded to65536, with topology divisibility
checks and existing strip winding preservation.

NativeMeshCache accepts the generated identity, requires its exact owned span and
uint16 width, and compares identity on hits instead of index bytes. Raw guest index
callers retain exact comparison. Geometry construction may still compare bytes when
finding reusable GPU storage on a cache miss. Vertex-source comparisons remain.
These changes do not authorize a model-buffer comparison bypass.

Tests cover quad/strip output, uint16 upper boundary, invalid count bounds, identity
reuse, entry eviction and over-budget behavior, retained old generations, foreign
span/width rejection and solid/textured Utility WARP pixel readback through the
combined native declaration/index identity cache. Full build/all18tests passed15.63s.
No new live gameplay/FPS run yet; no game process running. The latest live proof
predates both this and the Utility2D conversion-cache changes.

### Combined owned-draw validation (2026-09-10)

Subsequent live run validates the combined Utility2D layout/generated-index build:
SHA256 8B16E6B4CDB09C94F18A3A72CA29B69A998C297C39AF50D6BE24DE05E3F3030A,
PID16616, native-owned-draw-profile-20260910.log, first timestamp01:10:59.334.
Normal early-entry script; untiled scene, vsync, hook timings and frame-time logging
enabled, mesh audit disabled, application FPS cap0. Captured and visually inspected
native-owned-draw-profile-world.png: Mission1 player, street, buildings and HUD.
This screenshot does not establish intro brightness or whole-game fidelity.

Matched elapsed154.159..184.159s window (01:13:33.493..01:14:03.493):

| Phase | Calls | Total ms | Microseconds/call |
| --- | ---: | ---: | ---: |
| indexed.native | 4,069,980 | 10,004.36 | 2.458 |
| activation.native | 1,368,319 | 7,496.10 | 5.478 |
| mesh.acquire | 4,069,980 | 5,105.35 | 1.254 |
| swap.gpu_wait | 1,587 | 3,591.45 | 2,263.046 |

Inclusive nested CPU wall-time buckets: do not add these totals. Bucket boundaries
can straddle the requested window. FPS at t155..180s:53.1,52.6,52.9,52.5,53.2,52.9.
Later t286..321s reports59.6..60.0; different scene/time, not proof the heavy window
is fixed. Instrumented single-run results, not a clean repeated benchmark. The FPS
counter in input_hooks.cpp counts sub_82151460 presentation-hook invocations, not
independently verified unique simulation updates.

Final01:18:30.951:45,637,000indexed submissions,zeroerrors,722builds,378entries,
41,712,460mesh bytes,noevictions. No error/critical lines in retained current/rotated
logs at shutdown. Rotation retains only part of the run; timing figures above were
read before older buckets rotated away. Exact executable path revalidated before
stopping PID16616; WaitForExit=true. No production code changed during this audit.
