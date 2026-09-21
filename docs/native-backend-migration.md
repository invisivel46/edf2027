# Native renderer backend migration

## Direct texture/sampler binding slots - 2026-09-18

ShaderBindings now builds direct slot pointers to its reflected texture and
sampler map values. Named and pre-resolved setters use those pointers instead
of looking up the slot in an ordered map again. The maps keep ownership and
iteration order; their nodes are created during reflection and never erased or
moved during the binding object's lifetime. Slot tables add 144 pointers per
shader binding object. Generation checks still precede access through resolved
tokens, and missing inputs are still cleared by EndResourceUpdate.

Texture setters borrow their incoming shared pointer and acquire ownership only
when the binding changes. Material activation borrows the registry's pointer
under the existing bridge lock, eliminating the temporary ownership increment
and decrement when a texture is already bound. Registry lookup, live sampler
word reads, resource-generation changes, and D3D11 fallback views are preserved.

Release build passed (`out/resource-binding-slots-build.log`). Runtime checks and
benchmarks remain deferred; no FPS gain is claimed for this change. Follow-up
checks should cover unchanged/replaced/null textures, partial resource updates,
stale binding tokens, material retirement, and direct/recorded rendering.

## Recent indexed-mesh lookup cache - 2026-09-18

NativeMeshCache now checks a fixed 64-slot recent-entry table after its existing
same-as-last shortcut and before the owning map. Alternating meshes can reuse
their entry pointers without another tree traversal. All five key words are
hashed and compared; collisions fall back to the map. A shortcut only selects
a candidate: shader, declaration, stride, index format, and source checks remain
in place. Exact owned-source hits also avoid calling the matching method again
after the same identity has already been proven for diagnostic accounting.

Entry replacement, update failure, eviction, explicit resource invalidation,
and backend/cache clearing invalidate lookup pointers before erasing entries.
The owning map, memory budget, eviction order, and guest-write verification
policy are unchanged. The table adds fixed storage and no per-lookup allocations.

Release build passed (`out/mesh-recent-lookup-build.log`). The user reports that
the preceding optimizations work and performance is good. This new change has
not been tested or benchmarked; those runs remain deferred. Follow-up checks
should include alternating keys, collisions, shader/source replacement, eviction,
failed dynamic updates, resource retirement, and backend switching.

## Owned transient vertex handoff - 2026-09-18

Immediate meshes, post quads/strips, and XUI position triangles now hand their
converted vectors to the recorder through SetTransientVerticesOwned. The
parallel recorder swaps that allocation into an unused frame-owned image and
returns its retired allocation to the caller. This removes the conversion-buffer
to packet-image memcpy; replay still copies the bytes into GPU upload storage.
The ordinary span-based interface remains for callers that cannot give up their
storage. Direct D3D11/D3D12 recorders consume the owned overload synchronously
through their existing upload method.

Quad and position-triangle converters now also support caller-owned reusable
vectors, used by both recorded and direct stream draws. Original conversion
entry points delegate to them. Validation and triangle expansion are unchanged;
input must not alias the destination. Draw counts are captured before ownership
transfer because the returned scratch vector may contain unrelated retired bytes.

Published image slots are never swapped again in the same frame. Reset rewinds
the pool only after worker recording completes and saved states are discarded,
so subsequent conversions cannot overwrite pending draws. Retained capacity can
grow as draw sizes change and remains allocated for reuse.

Release build passed (`out/transient-vertex-ownership-build.log`), with three
getenv deprecation warnings in unchanged scripted_input.h. Tests and gameplay
measurements remain deferred; no frame-time gain is claimed. Later validation
should cover mixed copied/owned uploads, scratch overwrite after transfer,
multiple draws and flushes, saved-state restoration, reset/storage reuse, and
invalid conversion inputs on both renderer paths.

## Cached material constant upload plans - 2026-09-18

Material activation now caches the active constant list, destination binding
tokens, per-destination byte counts, maximum read extent, and canvas-scaling
classification for each material/shader pair. Normal-only and normal/reversed
uploads have separate plans. Repeated activations iterate that plan instead of
reconstructing destination arrays and sizing decisions for every parameter.
Parameters absent from both native destinations are counted as optimized out
without reading their guest values; entirely inactive groups need no read window.

Plans use the existing material ownership and shader lifetime boundaries.
Resolved binding vectors remain immutable after publication. Live source
pointers, global vector capacities, and payload bytes are still read on each
activation, with validation before upload. Instance patches and conversion of
active constants are unchanged; no guest value is assumed constant merely
because the material address repeats. Optimized-out counts are added per group
before active uploads, so partial-failure diagnostic totals can differ.

Release build passed (`out/material-upload-plan-build.log`). Tests and gameplay
benchmarks remain deferred; this is a reduction in repeated preparation work,
not a measured FPS improvement. Later checks should cover local/global pointer
changes, asymmetric normal/reversed shader layouts, optimized-out parameters,
invalid live vector extents, canvas scaling, and material/shader replacement.

## Shared draw binding snapshots - 2026-09-18

The parallel recorder now separates per-draw geometry/constant references from
pipeline, texture, sampler, target, and raster bindings. Binding setters invalidate
a cached snapshot only when their values change; the next draw copies those
bindings once into frame-owned storage. Subsequent draws retain a plain pointer.
Worker replay skips shared binding comparisons when consecutive packets point
to the same snapshot, while geometry and constant comparisons remain per draw.
A compile-time assertion requires the complete draw packet to occupy less than
half the size of the complete producer state.

Snapshots keep stable addresses in a deque and survive intermediate flushes and
PushState/PopState. Reset reuses storage only after recording workers finish and
saved/previous states are discarded. Forget flushes pending draws, invalidates
live and saved snapshots, and discards the serial previous-state cache so a
retired resource cannot be reached through it. Submission order is unchanged.
The pool retains its peak allocation until recorder destruction; workloads that
change bindings on every draw receive less benefit and need memory measurement.

Release build passed (`out/shared-draw-state-build.log`). Tests and gameplay
measurements remain deferred at the user's request; no FPS improvement is
claimed. Later validation should cover worker and serial replay, query draws,
state restoration across flushes, resource retirement, target/SRV aliasing,
buffer updates, and a matched combat capture.

## Constant-set preparation fast path - 2026-09-18

Recorded draw setup now checks one aggregate constant generation per shader
stage before walking its reflected buffers. ShaderBindings updates that
generation through the same Touch path as individual buffer versions, including
initialization, direct writes, guest register uploads/patches, and mirrored
constants. Consecutive draws with unchanged bindings skip the entire buffer loop.
Changed sets retain per-register checks, now using a fixed b0..b13 array instead
of hash lookups and allocated map nodes. The complete-set cache is recorded only
after every buffer has been checked/sent; individual slot writes invalidate it.
Frame and binding-generation changes still force constants to be sent again.

The release executable built successfully (`out/constant-fast-path-build.log`).
Tests and gameplay/performance validation are deferred at the user's request.
This removes repeated CPU preparation work; no measured frame-time or FPS gain
is claimed. Follow-up validation should cover shader switches with overlapping
registers, each constant mutation path, mirrored bindings, frame/state resets,
and a matched combat performance capture.

## Allocation-free pipeline cache hits - 2026-09-15

The D3D12 wrapper cache now looks up a stack-resident binary key through a
transparent string comparator. Previously, every material transition constructed
and then appended to an owning string before looking up an already compiled
pipeline. Only cache misses now allocate the owning key. The key's bytes and
primitive-topology distinction remain unchanged.
All 32 tests passed (34.43 s): `out/pipeline-hit-build.log`,
`out/pipeline-hit-tests.log`. The subsequent native 1080p approach run completed
without hook sampling or coarse frame tracing, with host cadence reporting.
Game seconds 140-240 recorded zero repeated/skipped images across 5,712 host
presentations, a minimum reported 59.9 FPS, and zero errors. The capture verifies
active city gameplay at 200/200 HP, with enemies farther away than some previous
captures. This establishes a clean short window, not a controlled measurement
of the cache change or a sustained combat lock. Artifacts:
`out/pipeline-hit-1080-summary.json`, `out/pipeline-hit-1080.bmp`.
Run: `out/native-bridge-run/binding-validation-20260915-113803-14692d16/game.log`
(PID 40104, stopped).

`tools/native-dodge-combat-input.txt` retains the same approach, then alternates
20-second lateral holds with short A presses through game second 440. This is
intended to improve survival for the longer validation. The resulting capture
and logs must establish active gameplay without mission death/restart; a final
living player alone is insufficient because A also confirms menu selections.
Completed run: `out/native-bridge-run/binding-validation-20260915-114327-8e4c3fa2/game.log`
(PID 42888, stopped), native 1080p, VSync off, host cadence reporting only.
Game seconds 140-440 recorded four repeated images, zero skipped sequences,
a lowest reported 59.2 FPS, and zero errors across 17,432 host presentations.
The final capture shows active gameplay at 200/200 HP. However, a burst of XUI
menu drawing begins near game second 321 and coincides with all four repeats;
the script's A presses may have confirmed a retry. No shader re-registration
occurred, so absence of registration cannot be used to rule out a cached restart.
Uninterrupted combat is therefore unproven. Artifacts:
`out/dodge-sustained-1080-summary.json`, `out/dodge-sustained-1080.bmp`.

The next route, `tools/native-retreat-combat-input.txt`, preserves the initial
approach then retreats along the same street while firing from seconds 240-440.
It sends no button presses after menu setup, eliminating automatic confirmation
of a retry. A surviving final capture can then provide stronger continuity
evidence, while the initial approach retains the existing comparison workload.
Completed run: `out/native-bridge-run/binding-validation-20260915-115426-69e13936/game.log`
(PID 34300, stopped), native 1080p, VSync off, host timing reports only. The final
capture shows mission failure and 0/200 HP against a building. Across seconds
140-440, host reports contained 17,735 presentations with no repeated/skipped
sequences, but the lowest game FPS sample was 58.2 and later samples include
the failure screen. This cannot validate sustained combat. Artifacts:
`out/retreat-sustained-1080-summary.json`, `out/retreat-sustained-1080.bmp`.

The current native dependency audit passed: `out/pipeline-hit-dependencies.log`.
All 32 native tests remain passing from the last code build. A representative
manual combat session or a reproducible surviving route is now requested from
the user; repeated blind scripted routes have not established a long combat
window. No game is currently running. The performance goal remains unverified.

For manual validation, `tools/start-native-binding-validation.ps1 -ManualInput`
opens the current release executable visibly with scripted input disabled and a
fresh test profile. It returns the exact process and log paths. A dry run checked
visible launch mode, removal of inherited scripted-input variables, skipping
script-file resolution, and restoration of the parent environment. Example
without host, hook, or coarse-frame timing instrumentation:

```powershell
& tools/start-native-binding-validation.ps1 -ManualInput `
  -RenderWidth 1920 -RenderHeight 1080 -NativeRenderSize `
  -ExtraArgs @('--edf_native_contract_coverage=false',
    '--edf_native_vsync=false', '--edf_native_host_timings=false')
```

## Frame-owned draw constants - 2026-09-15

Draw packets now reference immutable constant images owned by the recording
frame. Previously, copying each packet copied a 3-by-14 array of shared pointers
and adjusted the reference counts of its populated slots on the producer thread.
The new deque keeps image addresses stable while later bindings are appended;
packets and saved states contain plain pointers. Intermediate flushes preserve
the images. Reset retires them after workers have joined and the backend has
copied their bytes into its fenced upload storage. This also removes the separate
shared-pointer control-block allocation from each constant update. Images now
remain alive until frame reset, including bindings superseded in earlier batches.
Reset then rewinds the pool's used count, retaining vector capacities for later
frames. After the pool grows to the workload's size, constant updates reuse that
storage instead of allocating a new byte vector. Peak pool capacity remains
allocated until the recorder is destroyed, like the packet vector's capacity.

All 32 tests passed (34.20 s), including GPU readback after saved-state restoration
across a worker flush and 128 replacement constant bindings. Logs:
`out/frame-constants-build.log`, `out/frame-constants-tests.log`.
The first 1080p approach run (before storage reuse), with hook sampling and frame
tracing disabled, recorded a 58.4 FPS minimum, 11 repeated images, zero skipped
sequences, and zero errors across 5,709 host presentations (game seconds 140-240).
Its capture shows active city gameplay at 150/200 HP. Artifacts:
`out/frame-constants-1080-summary.json`, `out/frame-constants-1080.bmp`.
This is not a locked-60 result or a controlled proof of improvement.

The storage-reuse follow-up passed all 32 tests (33.63 s):
`out/pooled-constants-build.log`, `out/pooled-constants-tests.log`.
A longer 1080p run completed with host timings, hook sampling, and frame tracing
disabled, using `tools/native-heavy-sustained-input.txt`. It recorded a minimum
58.8 FPS and zero errors over game seconds 140-440, but the final capture shows
the mission-failed screen and 0/200 HP. The time of death is not established;
later samples cannot count as sustained combat. This is a failed validation of
the five-minute combat requirement, not a locked-60 result. Run:
`out/native-bridge-run/binding-validation-20260915-111522-aac10c07/game.log`.
PID 40152 is stopped. Artifacts: `out/pooled-constants-sustained-1080-summary.json`,
`out/pooled-constants-sustained-1080.bmp`. The 140-240 subset is in
`out/pooled-constants-approach-1080-summary.json`; it has the same FPS minimum,
but no separate capture establishes the player's state at its end.
The shorter pre-pool run's worker counters show 0.612 ms producer wait per batch
over 5,400 batches (`out/frame-constants-worker-summary.json`). Overlapping
recording with preparation can only remove that measured portion on average;
serial preparation and game execution still require attention.

The optional coarse frame trace now includes `engine_extra_steps`: additional
simulation steps returned by the engine heartbeat in a single update. The retail
tick accounting and clamp remain unchanged. This counter can establish whether
coalesced updates accompany production dips before choosing a pacing change.
It is cumulative across engine callers and sampled between swap completions;
it is not an exact association with one rendered image. Long pauses clamped to
one returned step contribute zero. The reporter summarizes totals, affected
swap windows, and maximum extra steps, and accepts old traces without the column.
Synthetic old/new-schema checks and all 32 native tests passed (35.08 s).
Logs: `out/engine-step-trace-build.log`, `out/engine-step-trace-tests.log`.
The shorter 1080p approach route is now tracing into `out/engine-step-1080.csv`,
with a capture scheduled for 183 seconds at `out/engine-step-1080.bmp`.
Completed run: `out/native-bridge-run/binding-validation-20260915-112429-8da83f57/game.log`
(PID 9500, stopped). Game seconds 140-180 contained 2,392 swap samples, four extra
simulation steps in four windows, and a lowest reported 58.4 FPS. Mean swap
interval was 16.722 ms; the maximum was 85.674 ms, with 85.545 ms outside the
swap hook and zero engine wait in that window. Engine waiting averaged 3.275 ms
across the window, but these cross-thread totals must not be subtracted as a
partition of producer execution. The host recorded nine repeats, no skips, and
no errors. The capture shows active city gameplay at 200/200 HP. Artifacts:
`out/engine-step-1080-summary.json`, `out/engine-step-1080-host-summary.json`.
No mesh rebuild occurred across the long gap at 11:27:07.279-07.353. The four
coalesced ticks do not by themselves establish the cause of that stall.

Windows Performance Recorder CPU sampling could not start: policy enablement
failed with 0xc5585011. A subsequent status query confirmed no recording active.
The coarse trace now optionally queries process and swap-thread CPU accounting
once per swap, with the thread identity recorded. The reporter includes these
values for the ten longest windows, to distinguish CPU work from wall-clock
stalls. Process CPU includes all threads and can exceed elapsed wall time;
Windows accounting granularity also limits interpretation of individual frames.
No CPU query runs when the coarse trace is disabled. All 32 tests passed (33.62 s),
and synthetic checks cover all three trace schemas. Logs:
`out/frame-cpu-trace-build.log`, `out/frame-cpu-trace-tests.log`.
Completed validation: `out/native-bridge-run/binding-validation-20260915-113221-1e1b96b4/game.log`
(PID 38296, stopped), writing `out/frame-cpu-1080.csv` and capturing
`out/frame-cpu-1080.bmp` at 183 seconds. Game seconds 140-180 produced 2,399 swap
samples with a 16.668 ms mean, 25.109 ms maximum, and zero extra simulation steps.
The host recorded no repeated or skipped images across 2,103 presentations;
the minimum reported game sample was 59.9 FPS. The capture verifies normal city
gameplay with the player alive at 200/200 HP. No errors were logged.
Process CPU averaged 21.630 ms per swap across all threads; the swap thread
averaged 3.530 ms. Accounting increments are 15.625 ms here, too coarse to
attribute individual small hitches. The earlier 85 ms stall did not recur, so
this run does not establish its cause. Artifacts: `out/frame-cpu-1080-summary.json`,
`out/frame-cpu-1080-host-summary.json`. These results predate the pipeline-key
allocation change and do not establish a sustained combat lock.

## Preparation cost reduction - 2026-09-15

The sampled approach run (`binding-validation-20260915-103657-db1166a5`, game
seconds 140-180, period 64) estimates 4.62 ms per target-60-Hz frame in indexed
native preparation, 2.69 ms in activation, 2.23 ms in immediate draws, and
1.32 ms in instance overrides. These inclusive estimates locate work; nested
phases must not be summed. `out/approach-1080-sampled-hooks.json` was produced
by `tools/report-native-sampled-hooks.py`, which only uses complete report
windows per thread/phase. The sampler's report threshold now allows infrequent
phases to report without waiting for 256 samples; full timing retains its old
threshold. The profile process is stopped.

Three repeated costs were removed from preparation:

- Instance overrides decode their parameter list once. The second decode and
  shape fingerprint were used only by the optional batching audit; the audit
  now fingerprints the same decoded list only when enabled.
- Recorded draws use the pipeline's cached blend-factor requirement instead
  of decoding all render-state words again. D3D11 now exposes its existing
  cached requirement through the common pipeline interface, with conformance
  coverage for both backends.
- Constant uploads and partial patches convert each complete Xbox register
  together and compare/copy its reflected lanes in one operation. Full uploads
  still zero padding; partial patches preserve it. Ownership and range checks
  remain before mutation, and float bits are not numerically converted.

All 32 tests passed after these changes (34.55 s), including scalar/vector/array
padding, matrix patches, invalid extents, signed zero, NaN payloads, infinity,
and blend-factor readback. The dependency audit passed. Logs:
`out/preparation-final-build.log`, `out/preparation-final-tests.log`,
`out/preparation-final-dependencies.log`.

The subsequent 1080p run, with hook sampling and frame tracing disabled
(`binding-validation-20260915-104639-9cca1fa3`, game seconds 140-240), reached
a minimum reported 59.6 FPS, with two repeats and zero skipped sequences across
5,708 host presentations. No errors were logged. Its capture verifies close
combat among ants at 200/200 HP. This improves on the earlier 57.0 minimum and
15 repeats with the same route/resolution, but does not prove a perfect lock.
Artifacts: `out/preparation-1080-summary.json`,
`out/preparation-1080-validation.bmp`. The process is stopped.

A further indexed-draw optimization now asks the write registry to reuse the
existing immutable snapshot using metadata before validating guest mappings.
The fast path requires the exact owner lifetime, physical extent, immutable
candidate and revision baseline, no active writer, and an unsampled observation
under the existing policy. It does not read guest memory. Rejected attempts
consume no observation and fall back to mapped-range validation followed by
the original guarded comparison. Periodic verification and global revocation
after an unreported write remain unchanged. Duplicate owners use the original
sequential path. No SDK heap lock is acquired under the writer mutex.

Regression tests cover initial/periodic verification, revoked trust, strict and
audit policies, extents, candidates, partial pair rejection, active writers,
tracked revisions, commit tokens, retirement and owner reuse. All 32 tests
passed (33.31 s): `out/snapshot-reuse-build.log`,
`out/snapshot-reuse-tests.log`. The subsequent exact 1080p route completed with a
minimum reported 57.1 FPS, 18 repeated images, zero skipped sequences, and zero
logged errors across 5,709 host presentations (game seconds 140-240). Artifact:
`out/snapshot-reuse-1080-summary.json`. This does not establish an FPS improvement
from metadata reuse, and the run is stopped.

## 1080p frame-production profiling - 2026-09-15

The same forward-approach route was replayed at native 1920x1080 with VSync off,
coarse swap tracing enabled, and display-statistics tracing disabled
(`binding-validation-20260915-102308-f83b8835`). Game seconds 140-240 contained
5,978 swap samples: mean interval 16.731 ms, p99 19.108 ms, maximum 45.224 ms.
Between-swaps wall time averaged 16.148 ms and peaked at 35.067 ms; end-of-frame
GPU wait averaged 0.0047 ms and peaked at 0.0721 ms. Submission averaged 0.127 ms.
The lowest five-second game FPS sample was 55.8 (ending at game second 160).
Host reports contained 5,711 presentations, 22 repeats, zero skipped sequences,
and zero errors. The capture verifies close combat with ants and 150/200 HP.
Artifacts: `out/approach-1080-frame-summary.json`,
`out/approach-1080-host-summary.json`, `out/approach-1080-profile.bmp`.

This reproduces the shortfall without DXGI display-statistics queries. It rules
out the end-of-frame GPU barrier as its principal cost, but between-swaps time
also contains other waits and must not be called CPU execution time.

Coarse frame traces now include engine heartbeat waiting, actual guest-fence
poll sleeps, shared-surface reservation time, and the backend's frame-resource
wait total. These diagnostics are enabled only by `edf_native_frame_trace` and
add no per-draw timing. Counters span participating threads and are sampled
between consecutive completed swaps, so their values are not an additive
partition of one thread's execution. The reporter retains support for old CSVs.
The build and all 32 tests passed (33.55 s); logs are
`out/coarse-wait-build.log` and `out/coarse-wait-tests.log`. The more detailed
wait-profile run is complete. No performance fix is claimed from tracing.

The wait profile (`binding-validation-20260915-102956-05925740`, game seconds
145-160) averaged 18.226 ms between swap entries and 17.891 ms outside the swap
hook. Engine waiting averaged 0.059 ms, guest-fence sleeps and backend frame
resource waits were zero, shared-slot reservation averaged 0.0036 ms, and the
end-of-frame GPU wait averaged 0.0051 ms. These measured waits do not explain
the production shortfall. The early-approach capture shows normal city gameplay
at 200/200 HP, not a loading screen. Artifacts:
`out/approach-1080-waits-summary.json`,
`out/approach-1080-waits-host-summary.json`, `out/approach-1080-waits.bmp`.

`--edf_native_hook_sample_period=64` now samples one in 64 active hook timing
scopes independently of full hook/load timing flags. Phase offsets avoid timing
all nested scopes on the same draw. Sampled reports use a distinct prefix and
include the sample period, so existing full-timing reports cannot silently
treat sample counts as complete counts. This is an estimator for locating CPU
cost, not a benchmark result; nested scopes remain inclusive. Zero (default)
disables sampling. All 32 tests passed after adding the sampler (34.03 s;
`out/sampled-hook-build.log`, `out/sampled-hook-tests.log`). The same approach
is now running as `binding-validation-20260915-103657-db1166a5`, writing
`out/approach-1080-sampled.csv`; sampled reports with period 64 are confirmed.
Analyze game seconds 140-180, then stop this diagnostic process. Earlier profile
processes are stopped. No preparation optimization or locked-60 result is yet
claimed from these diagnostic changes.

## Display feedback and VSync-off support - 2026-09-15

`--edf_native_display_trace=path.csv` optionally records raw DXGI frame statistics
after presentation, including query status, display/refresh counts, QPC timing,
the last submitted Present count, and the game image sequence. Empty (default)
disables it. No flush/wait is added to force display statistics to update.
`tools/report-native-display-trace.py` aligns complete CSV rows with the game
clock. It reports unchanged observations and wider counter steps separately;
neither is automatically classified as a dropped image. Failed/disjoint queries
reset comparison history. Live reads exclude a partially written final row.

The single attached display was independently queried with EnumDisplaySettings:
2560x1440 at 165 Hz. The 720p VSync-on combat trace (game seconds 140-210) had
4,200 successful queries, with 2,800 adjacent present-count observations. Their
refresh spans were 1 (33), 2 (897), 3 (1,848), and 4 (22). There were also 699
unchanged observations and 700 two-count steps, so this is incomplete per-image
coverage. Median feedback refresh estimate was 164.93 Hz, while the aggregate
was 158.18 Hz: the feedback is not precise enough to claim perfect scanout.
Host reports remained at 60.0 FPS with zero repeats/skips over 3,907 presentations.
Artifacts: `out/display-feedback-summary.json`,
`out/display-feedback-host-summary.json`, `out/display-feedback-combat.bmp`.
At fixed 165 Hz, 60 FPS cannot occupy an equal whole number of refreshes per
image. The usual two/three-refresh alternation alone produces uneven spacing.

The D3D12 VSync-off path previously used interval zero without allowing tearing,
omitting the flags required for variable-refresh support. It now queries
IDXGIFactory5 capability, includes ALLOW_TEARING at swap-chain creation when
supported, and passes DXGI_PRESENT_ALLOW_TEARING only with VSync off. Resize
recreates the chain with the same capability policy. The host uses windowed or
borderless presentation; DXGI exclusive fullscreen is not used. VSync on keeps
interval one and zero present flags. VRR still requires a capable and configured
display/driver; the feature query does not prove VRR is active. Settings explain
this choice and the fixed-refresh cadence constraint.

Microsoft references: [frame statistics](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getframestatistics)
and [variable refresh rate requirements](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays).
All 32 tests passed after the VSync-off fix (34.14 s), including host resize and
VSync off/on/off transitions under GPU validation. The dependency audit passed.
Logs: `out/vrr-present-build.log`, `out/vrr-present-tests.log`,
`out/vrr-present-dependencies.log`.

The 1080p forward-approach run (`binding-validation-20260915-101716-bcbe3421`,
native render size 1920x1080, VSync off, game seconds 140-240) averaged 59.83 FPS
from rounded logs, with a lowest five-second sample of 57.0. Its 5,709 host
presentations repeated 15 images and skipped zero sequences; acquisition/copy
CPU time peaked at 0.528 ms. No errors were logged. The capture confirms close
combat with ants and the player alive at 150/200 HP. The display feedback still
ran at approximately 164.96 Hz, so VRR activation is not established here.
Artifacts: `out/vrr-1080-host-summary.json`,
`out/vrr-1080-display-summary.json`, `out/vrr-1080-approach.bmp`.
This run changes both resolution and route relative to the earlier 720p test;
it establishes a remaining production shortfall, not which change caused it.
Next: coarse frame tracing of this exact 1080p route to separate CPU preparation
from GPU/resource waits before choosing the next optimization. Both diagnostic
game processes are stopped. The locked-60 goal remains incomplete.

## Ordered presentation handoff - 2026-09-15

The longer overlap run exposed a separate cadence defect: sampling the newest
shared surface repeated 733 images and skipped 733 sequences in 5,710 host
presentations (game seconds 140-240), despite every game FPS report reading 60.0.
See `out/sustained-latest-image-summary.json`. Producing frames at 60 Hz alone
does not ensure that the host displays each image once.

The handoff now uses three reusable shared surfaces and an ordered queue. The
presenter initially buffers two images for a one-frame timing cushion, then
consumes the oldest image. A full visible queue blocks the producer instead of
overwriting a pending image. Slots retire only after the host GPU copy completes.
The queue has its own mutex, so presenting no longer takes the renderer's lock
during guest loading. Minimized/stopped consumers retain only the latest pending
image, while outstanding GPU readers still pin their surfaces. The optional
preview samples the latest image independently without consuming the main FIFO.

Queue regression coverage checks ordering, bounded producer blocking, primary
and preview copy lifetimes, background publication, and failure after possible
GPU submission.

The queued run (`binding-validation-20260915-095833-5187dd95`, game seconds
140-440) reported 60.0 in all 59 complete game FPS samples. Its 58 complete host
report buckets contained 17,440 presentations, zero repeated images, zero
skipped sequences, and zero logged errors. Mean Present-return interval was
16.66664 ms, maximum 20.215 ms; maximum frame acquisition/copy CPU time was
0.5286 ms. The final capture confirms the player alive in the city mission.
Artifacts: `out/queued-sustained-summary.json` and
`out/queued-sustained-combat.bmp`. The aggregate FPS calculation uses rounded
logged durations; use the explicit 60.0 FPS samples rather than interpreting
its 60.12 aggregate as a speedup. This run predates the optional mirror-reader
addition, which does not participate in normal main-window presentation.
The final build, including mirror-reader regression coverage, passed all 32
CTest tests in 33.58 seconds. The earlier legacy D3D11 presenter timeout did
not recur. The packaged dependency audit also passed (run from the Visual Studio
developer environment so `dumpbin` is available). Logs:
`out/queued-final-build.log`, `out/queued-final-tests.log`, and
`out/queued-final-dependencies.log`. The validation game process is stopped.
CPU Present-return timestamps do not prove scanout cadence or performance in
other missions; those remain open requirements for the locked-60 goal.

## Frame pipeline redesign in progress - 2026-09-15

Goal: sustained 60 FPS in combat, verified at both frame production and host
presentation. This section is an ongoing experiment, not a completion claim.

Coarse tracing (`--edf_native_frame_trace=path.csv`) records swap-entry intervals,
wall time between swaps, lock/submission time, GPU-barrier wait, and pacing time.
It performs no per-draw timing. `tools/report-native-frame-trace.py` aligns the CSV
with the game's logged FPS clock; swap-entry intervals are not scanout timestamps.

Baseline combat (160-210 seconds, 2,443 samples): mean interval 20.47 ms, p99
28.77 ms. Between-swaps wall time averaged 14.86 ms; submission/lock entry 0.105 ms;
GPU wait 5.49 ms (p99 12.91 ms); pacing 0.013 ms. The swap drained the current
GPU frame before letting the CPU begin the next frame, serializing these costs.
This is direct evidence for overlapping frame preparation and GPU execution;
it does not establish that all between-swaps time is CPU work.

The D3D12 prototype separates presentation credits from resource completion.
`--edf_native_frame_latency=1` retains the current-frame drain for comparison.
`=2` (new default) permits preparation of the next CPU frame while one submitted frame remains
unfinished. Credits retire in order and bound CPU lead. Guest resource fences,
worker callbacks, upload rings, and shared-image copy fences still require their
actual GPU completion; credit acceptance does not publish those completions.
The native pacing counters account for accepted presentation work in this mode.

The first overlap test exposed a descriptor-capacity defect: four workers divided
the original 65,536 descriptor budget into five 13,107-entry slices, including
the recorder handling all UI draws. That run was stopped after allocation errors
and is not a performance result. The scene factory now preserves a 65,536-entry
budget per recorder (up to a 1M-entry heap), with fence-based retirement unchanged.
At four workers this allocates 327,680 descriptors instead of 65,536.

Baseline artifacts: `out/frame-architecture-baseline.csv`,
`out/frame-architecture-baseline-summary.json`, and
`out/native-bridge-run/binding-validation-20260915-092100-a3987d72/`.
Failed capacity experiment: `binding-validation-20260915-092555-0977a63b`.

Corrected overlap run: `binding-validation-20260915-092843-d9b97b75`, with
`out/frame-architecture-overlap-v2.csv`, `out/frame-architecture-overlap-v2-summary.json`,
and `out/frame-architecture-overlap-v2.bmp`. All 32 tests passed (33.77 s), no
render errors or descriptor stalls were logged, and the final gameplay capture
shows the scene and HUD intact.

Combat swap-entry rate rose from 48.85 to 60.00 Hz. Mean GPU-barrier wait fell
from 5.494 to 0.00495 ms. The 3,001-sample combat window averaged 16.6667 ms per
entry, but p99 was 18.457 ms and max 21.844 ms. Every five-second FPS sample in
that combat window reported 60.0. These results justify enabling two credits,
but do not prove locked presentation: host repeated-image/scanout cadence and
remaining preparation/pacing outliers still need verification. Earlier gameplay
also contains a temporary dip around 115 seconds. The full goal remains active.

Default-mode presentation verification (no per-draw or swap tracing):
`binding-validation-20260915-093358-6d88996c`. During combat, nine complete host
reporting windows covered 2,705 presents: zero repeated images and zero skipped
sequences, mean Present-return interval 16.6666 ms, maximum 21.8023 ms. Game FPS
samples all rounded to 60.0; the report computes 60.06 from rounded five-second
bucket durations. No rendering errors. Results: `out/frame-flight-host-summary.json`
and `out/frame-flight-default-fps.json`. These are host Present-return statistics,
not scanout timestamps.

A transient hitch remains around game time 115 s: the host reported a 284.98 ms
maximum interval and 267.87 ms maximum acquire/copy phase in that reporting
window, with game FPS falling to 47.7 for five seconds. Acquire/copy includes the
bridge mutex, frame import, and copy submission; that aggregate does not isolate
the precise cause. Next work must separate lock wait from resource preparation
and GPU copy waits, then eliminate the long stall. Combat at 60 alone is not
completion of the full locked-60 goal. All validation processes are stopped.

### Loading-transition classification and longer validation

A targeted capture at the apparent 115-second hitch shows the game's **Now
Loading** screen (`out/host-acquisition-hitch.bmp`). The earlier description of
this as a gameplay hitch was incorrect. That window tears down and reloads many
textures/resources between the intro and the playable mission. The full goal
still requires longer combat verification, but the loading dip is not evidence
of a combat frame-rate failure.

The host's acquisition diagnostics now separate registry-lock wait, consumer
copy, and producer queue-wait insertion. In the load-timing run
`binding-validation-20260915-094108-1e3d9801`, maxima were 269.29 ms for the lock,
0.201 ms for the copy, and 0.007 ms for queue-wait insertion. This confirms lock
contention during resource loading; it is not a GPU copy lasting hundreds of ms.
Load instrumentation logs each loader call and must not be used as a normal
combat performance baseline.

The final release build passes all 32 tests (33.60 s) and the dependency audit.
`tools/native-sustained-combat-input.txt` extends the test to five minutes of
continuous firing and alternating movement, after the loading transition.
`tools/report-native-host-cadence.py` reports complete FPS and presentation
windows from rotated logs, including repeated/skipped images. The long-run
results will determine whether further renderer work is necessary.

## Serial material and mesh preparation - 2026-09-15

Material activation now updates the resource set in place. Equal textures and
samplers retain their resource generation, allowing the recorded draw to reuse
its existing bindings. Slots omitted by the next material are cleared at the
end of the update; explicit nulls and changed resources invalidate reuse.
Failures still clear both binding sets. Guest texture handles and sampler words
are read on each activation, so texture publication, resolves, and live filtering
changes remain visible.

An activation resolves its material and shader owners once. Each shader also
keeps its most recently used parameter plan, keyed by weak shared-owner identity;
retirement/republication at the same guest address cannot reuse a retired plan.
Constant payloads and their pointers remain live, including per-instance patches.
Reflected constant-buffer descriptions are built once and returned as a span over
live bytes, removing two temporary vector allocations per recorded draw.

The mesh cache accelerates consecutive identical keys, while still checking the
shader, declaration, stride, index format, and both source generations/contents.
Invalidation, replacement, eviction, backend changes, and clear retire the shortcut.
Mesh internal timing calls now run only when hook timings are enabled. Geometry
write observation and sampled verification retain their existing policy.

Validation: release build and all 32 CTest tests pass (33.69 seconds).
Binding regressions cover unchanged resources, omitted slots, explicit nulls,
foreign/retired binding tokens, and live constant bytes behind cached descriptions.
Existing mesh tests cover byte changes, shader reloads, lifetime invalidation,
clear, dynamic updates, and both budget and entry-count eviction.

Hardware comparison: same movement/fire script, fresh user directories, 1280x720,
four workers, hook timings off, host/GPU timings off, contract collection off.
Normal FPS reporting and built-in counters remain enabled in both runs. The final
capture occurs after the measured combat window. No rendering errors were logged;
the capture shows the gameplay scene and HUD intact.

| Window (game seconds) | Before FPS | After FPS | Resource binding reuse before/after |
| --- | ---: | ---: | ---: |
| 90-150 | 52.25 | 52.67 | 57.9% / 77.4% |
| 160-210 (movement/fire) | 47.62 | 48.50 | 65.8% / 79.8% |

This single before/after pair confirms reduced binding work, but the small FPS
change is not sufficient to establish a reliable speedup or locked 60. Runtime
workload and scheduling vary between runs; FPS samples are five-second averages,
not per-frame deadline measurements. Resource reuse percentages use the counter
snapshots inside each window, whose spans differ slightly from the FPS windows.

Artifacts:
- Baseline: `out/native-bridge-run/binding-validation-20260915-090349-e0b53ec4/`
- Final: `out/native-bridge-run/binding-validation-20260915-091226-d3ad619b/`
- Comparison: `out/serial-preparation-comparison.json`
- Capture: `out/serial-preparation-host.bmp`
- Build/tests: `out/serial-preparation-build.log`, `out/serial-preparation-tests.log`

## Multithreaded geometry recording - 2026-09-15

The D3D12 scene backend now captures resolved draw state into immutable
packets and records contiguous ranges with persistent CPU workers. Configure
`--edf_native_geometry_workers=0` for direct recording, `=1` for a serial
packet baseline, or `=4` for four recording workers (current default).
Presentation continues to use its own direct backend.

Each packet owns its constant bytes and preserves its pipeline, geometry bindings,
textures, samplers, targets, viewport, and clipping state. Worker command lists
execute in original packet order. Resource destruction drains pending packets
before releasing CPU wrappers; uploads, copies, resolves, and queries establish
ordering boundaries. Occlusion-query draws stay on their owning command list.
Small batches remain on the direct list to avoid per-draw worker dispatch and
GPU submission overhead. The first hardware run exposed that overhead in XUI;
it was stopped and the scheduler corrected before performance comparison.

Resource transitions are local to each recording list. At submission, ordered
barrier preambles reconcile first-use states with preceding lists; workers do
not mutate a shared resource-state map. The shared sampler table cache is
synchronized. Tests cover reversed CPU recording order, concurrent access to
shared resources, resource destruction before submission, and indexed packets
with distinct constants and scissors. Conformance also forces the worker path
for small scenes and compares it with D3D11 and direct D3D12. The mixed
small-batch path is tested separately, including buffer updates and sampled
target reuse. Worker exceptions join every job and permanently reject further
submission of that failed stream; partial command lists cannot be retried.

Pipeline-cache hits now return before shader reflection, render-state decode,
or input-layout allocation. Previously those operations ran again for every
pipeline request, even though the native pipeline itself was cached. The
wrapper key also preserves the exact primitive topology.

The producer still reads guest state and performs mesh lookup/decode. Workers
perform command recording, constant uploads, descriptor preparation, and local
barrier recording. This does not make all bridge work parallel.

### Hardware results

Matched runs use the same release executable, 1280x720, VSync, fresh user
folders, `tools/native-movement-fire-input.txt`, hook timings, contract coverage,
and the FPS overlay. The comparison includes complete FPS reporting intervals
within game seconds 90 through 150. Hook costs use complete per-thread reporting
buckets within that window and are inclusive; nested phases cannot be summed.
These are single-run diagnostic comparisons, not all-map or handheld benchmarks.

| Recording mode | Average FPS | Indexed hook CPU wall time / call | Producer wait / parallel batch |
|---|---:|---:|---:|
| Direct (`=0`) | 49.62 | 2.87 us | n/a |
| One packet worker (`=1`) | 48.95 | 2.77 us | 0.908 ms |
| Four packet workers (`=4`, default) | 50.92 | 2.77 us | 0.438 ms |

The four-worker run reports `worker_mask=0xf` and `max_concurrent=4`; the
one-worker control reports `0x1` and `1`. Four workers roughly halve producer
wait per recording batch versus one worker. The FPS difference is modest:
about 4.0% versus one packet worker and 2.6% versus direct recording in this
window. The larger gain came from fixing pipeline-cache hits: the preceding
direct run measured 36.70 FPS and about 9.18 us per indexed hook. Do not
attribute that cache improvement solely to multithreading.

All three final runs complete the movement/fire schedule with 28 submitted
rendering contracts and no logged errors. GPU captures of the four-worker and
direct paths show the scene, firing, HUD, and SDK overlay rendering correctly.
No upload or descriptor stalls were observed in these runs.

Evidence under `out/native-bridge-run/`:

- Four workers: `binding-validation-20260915-084025-8f5a2f22`; capture
  `out/parallel-geometry-host-v3.bmp`.
- Direct: `binding-validation-20260915-084436-4bc4d392`; capture
  `out/parallel-geometry-host-direct-v2.bmp`.
- One worker: `binding-validation-20260915-084832-75fae9cc`.
- Earlier direct run before the pipeline-cache fix:
  `binding-validation-20260915-083308-a274b6cb`.
- Report: `out/parallel-geometry-comparison-final.json`.

Regenerate a comparison with
`python tools/report-native-geometry-runs.py <four/game.log> <direct/game.log> <one/game.log> --output report.json`.
The reporter includes rotated log files and excludes partial timing buckets.
The release build, 32 CTest checks (including expanded parallel conformance),
and native dependency audit pass.

## Current D3D12 runtime - 2026-09-15

The default runtime uses D3D12 for scene rendering, display gamma, aspect
fitting, SDK overlays, presentation, guest completion fences/signals, and GPU
profiling. It creates no D3D11 device. Shader compilation and reflection keep
CPU metadata; executable shaders, buffers, textures, and pipelines belong to
D3D12. The optional preview also consumes owned D3D12 snapshots with gamma.

The frame path is:

`D3D12 scene queue -> owned D3D12 snapshot -> D3D12 gamma/UI -> D3D12 swap chain`

The host has its own device/queue so presentation does not hold the scene
recording lock. The producer publishes a shared RGBA8 image and signals a
fence; the host copies it and signals a completion fence before producer
reuse. Repeated paints retain the snapshot. The cross-runtime D3D11 round
trip has been removed from the default path.

### Completion and lifetime

- Guest fence and callback queues mark actual submitted D3D12 work. Capturing
  a guest command range does not submit or complete it. A later completed
  range cannot bypass an earlier unsubmitted fence.
- GPU timings use D3D12 timestamp queries and the queue's timestamp frequency.
  Query heaps and readback resources retire behind GPU fences, including when
  a timing span is cancelled.
- Backend owners outlive all scene resources and query objects. The host and
  SDK drawer share ownership of their backend. Resize, closure during an
  overlay callback, exception cleanup, and deferred-paint lifetime are tested.
- Texture creation during an open frame uploads immediately; creation outside
  a frame is staged until the next frame. SDK font/texture creation is covered.

### D3D11 fallback boundary

The explicit fallback is selected with
`--edf_native_scene_backend=d3d11 --edf_native_backend=d3d11`.
The settings menu saves both choices together and requires a restart. It has
no empty/None renderer choice, and the selected next-launch backend does not
replace the running scene backend in place.

D3D11 is delay-loaded only for the fallback. The package audit rejects eager
D3D11 imports in the executable and packaged DLLs. A tested import hook
rejects calls to D3D11 fallback exports while D3D12 is selected. Windows or
other process components may still load the system D3D11 DLL; module presence
alone is not evidence that the game renderer called it.

### Verification

- Release build and all 32 CTest checks pass.
- D3D11/D3D12 compositor pixel comparisons cover both display-gamma modes,
  stretching, and letterboxing. UI tests cover blending, clipping, texture
  sampling, invalid ranges, and multiple batches before submission.
- Full-size WARP D3D12-to-D3D12 transfer tests cover repeated snapshots,
  producer overwrite, source replacement, and resize. They no longer use the
  D3D12-to-D3D11 path whose full-size WARP readback hung.
- Completion/signal tests cover capture versus submission, contiguous guest
  fence ordering, callback acknowledgement, timestamp spans, and cancellation.
- Hardware gameplay with `tools/native-movement-fire-input.txt` reaches
  Mission 1, moves and fires, and reports 29 submitted rendering contracts
  with zero rejected draws, omissions, or bridge errors. The no-D3D11-device
  run is in `out/native-bridge-run/binding-validation-20260915-075612-e22f4e6c`;
  its GPU capture is `out/full-d3d12-host-v5.bmp`, including the SDK FPS overlay while the
  optional D3D12 preview also runs.
- Actual SDK settings and first-run setup screens render on D3D12:
  `out/full-d3d12-settings-v2.bmp` and `out/full-d3d12-setup-v2.bmp`.
  The settings menu identifies the D3D12 default and separate D3D11 fallback.
- Host pacing diagnostics report snapshot acquisition/copy, CPU Present
  return cadence, and repeated/skipped source images on D3D12.
- Explicit D3D11 fallback boots and renders the intro without bridge errors:
  `out/native-bridge-run/binding-validation-20260915-080610-cadb898f`
  (`out/full-port-d3d11-fallback-v1.bmp`, captured during the logo fade).
- `audit_native_dependencies` passes with two packaged non-system DLLs,
  no eager D3D11 import, and no Xenos import or staged plugin.

This verifies the rendering API migration. It does not certify every map,
long-session stability, or handheld performance. Diagnostic mission runs are
below 60 FPS; no performance improvement is claimed without a matched
comparison. Previously unimplemented native quality/upscaling features are
outside this API migration.

## Previous scene integration checkpoint - 2026-09-15

The Windows default is now `edf_native_scene_backend=d3d12` with recorded
scene draws enabled. `edf_native_backend=d3d12` presents the window. The
D3D11 compositor remains responsible for display gamma and SDK overlays;
this is a scene/backend migration, not removal of every D3D11 dependency.

Completed in the final integration:

- Publish the resolved RGBA8 target rather than its HDR working surface.
- Copy the shared D3D12 frame into the host's owned snapshot. Producer and
  consumer fences order the copy and prevent the next overwrite. Repeated
  host paints retain the image and run the compositor and overlays.
- Retain imported textures and fences. Reopening/releasing a 1280x720 import
  every frame reproduced a hardware GPU hang; retaining imports fixes the
  standalone hardware regression. Owned duplicate handles and kernel-object
  comparison also protect against handle-value reuse on resize.
- Initialize tone-map history and upload pitched YUV movie planes through the
  backend. Without the history seed, every final output remained invalid even
  while millions of draws were counted as submitted.
- Reuse ordinary output targets by backend-resource identity, not a null D3D11
  view. Cache constants by register, not shader reflection order. Restore the
  viewport scissor when scissoring is disabled on either recorded backend.
- Keep BMP output/movie/font capture working through backend readback and
  reject direct draws paired with a non-D3D11 scene backend at startup.

### Validation

- Full release build and all 27 CTest checks pass.
- Backend conformance matches D3D11 and D3D12 pixels, including the new
  scissor-disable regression.
- Constant-cache regression checks register identity across shader changes.
- Handoff tests check history initialization, gamma metadata, source overwrite,
  repeated visits, resizing and full-size hardware transfers.

### Runtime evidence and limits

A fresh-user hardware run using `tools/native-movement-fire-input.txt` reached
Mission 1, completed the movement/fire schedule, and captured the player,
city, sky, radar, health and weapon HUD while firing at 1280x720. The run
reported 28 submitted contracts, zero rejected draws, zero distinct rejections,
zero omissions and zero bridge errors. The host GPU capture contains the real
composited image, not an offline renderer fixture.

This is functional integration evidence, **not performance certification**.
The diagnostic run started at 60 Hz and fell to roughly 29-47 FPS in mission
content. It ran with contract accounting and other diagnostics enabled; no
matched D3D11 performance claim is made from it. All-map content coverage,
long-session stability and handheld performance still need separate testing.

The full-size asynchronous D3D12-to-D3D11 handoff regression is run with
`edf_native_presenter_tests --hardware`. WARP still hangs on the full-size
cross-runtime readback case; the normal WARP suite exercises small shared
copies and the separate backend rendering conformance suite. D3D12 WARP is an
offline diagnostic backend, not a certified game/presentation fallback.

For the hardware D3D11 fallback use
`--edf_native_scene_backend=d3d11 --edf_native_backend=d3d11`.
Adding `--edf_native_seam_draws=false` selects the older direct scene path.

## Historical planning and implementation notes

The notes below record the investigation as it happened. The current status
above supersedes their earlier defaults, blockers and undecided work.

## Why

The draw thread is the constraint, not the GPU. Measured in steady Mission 1
gameplay at 60 fps on a 20-core i7-14700:

| | |
|---|---|
| draw-thread hooks, summed top-level | **62.8%** of one thread |
| `indexed.native` | 26.4% (1.83 us x 142,385/s) |
| `activation.native` | 18.7% (4.16 us x 44,917/s) |
| draws per frame | ~2,370 |
| GPU span per frame | 12.4 ms of 16.67 (includes submission gaps) |
| idle logical processors | 27 of 28 |

Two D3D11 properties cap what can be done about that:

- **Per-draw driver cost.** *This estimate was wrong; see "What the per-draw
  cost actually is" below.* It originally read: roughly 1-2 us per draw is
  spent inside the runtime on validation, state resolution and hazard
  tracking, so at 2,370 draws a frame that is ~3 ms we cannot optimise away
  from the outside. Measured, it is 0.30 us per draw and ~0.71 ms a frame.
- **One immediate context.** Draws cannot be issued from more than one thread.
  Deferred contexts exist but are widely slower for many small draws, which is
  exactly this game's profile, so they are not a shortcut.

D3D12 and Vulkan move validation to pipeline build time and make multithreaded
command recording a first-class path. That is the only route to using more than
one of those 28 processors for submission.

**Vulkan is the better second backend if only one is built.** The stated target
is handheld PCs; handheld means Steam Deck means Linux, where D3D12 only runs
through Proton.

## What the per-draw cost actually is

`edf_native_backend_bench` runs the same workload through both APIs: 2,370
draws a frame, a material activation every 1.6 draws as measured, one texture
per draw cycled through 64, a blend state change every 16 draws, and trivial
geometry so the number is submission cost and not shading. Hardware, 300
frames, after a discarded warm-up. Stable to about 2% across runs.

| | per draw | per frame |
|---|---|---|
| D3D11 (dynamic constant buffer, WRITE_DISCARD) | 0.298 us | 0.71 ms |
| D3D12 (root CBV from the upload ring) | 0.080 us | 0.19 ms |

D3D12 is **3.7x cheaper per draw**, which is a real result and it holds when
the workload changes state rather than repeating one bind.

**But the frame-level claim above was wrong, and it was mine.** I estimated
1-2 us of driver cost per draw and ~3 ms a frame. The measurement says 0.30 us
and 0.71 ms, so moving to D3D12 saves about **0.52 ms of a 16.67 ms frame** -
worth having, and nothing like the headline the "Why" section implied.

Two consequences worth taking seriously:

- The draw hook costs 1.83 us per draw in the game. If only 0.30 us of that is
  the API, then **1.5 us is our own work** - guest reads, mesh lookup,
  parameter decode - and that is where the larger prize is, not in the API.
- Stage 0 makes this smaller still. Collapsing 77.4% of draws into instanced
  ones cuts the API cost to roughly 0.16 ms a frame on D3D11 alone.

What survives untouched is the argument that actually motivated the migration:
**a single immediate context cannot submit from more than one thread.** 27 of
28 logical processors are idle. That is a structural ceiling no amount of
per-draw tuning reaches, and it needs stage 3. The per-draw saving is a bonus,
not the reason.

## Does multithreaded recording actually pay?

This is the argument the per-draw correction left standing, so it was measured
too, with `--threads=N --bridge-us=1.5`. The simulated 1.5 us of non-API work
per draw is the difference between the game's measured 1.83 us draw hook and
the 0.30 us of API cost above: guest reads, parameter decode, mesh lookup.
2,370 draws a frame, 120 frames, hardware.

| | ms/frame | vs one thread |
|---|---|---|
| D3D11, one thread (today) | 3.77 | - |
| D3D12, one thread | 3.75 | 1.00x |
| D3D11, 4-thread decode, serial submit | 2.02 | 1.87x |
| D3D12, 4 recorders | 1.05 | 3.59x |
| D3D12, 8 recorders | 0.73 | 5.13x |

Three things fall out, and the third is the one that matters.

**Threading the API calls alone buys nothing.** With no simulated work, going
from 1 to 4 recorders moved 0.197 ms to 0.133 ms. There is not enough
submission work to be worth splitting - the per-frame barrier costs about as
much as it saves.

**The win is in threading our own work, not the API.** Put the 1.5 us back and
four threads take 3.77 ms to 1.05 ms. That is the whole prize, and almost none
of it is the graphics API.

**But D3D11 cannot collect it.** The honest control is D3D11 with the same
decode split across four threads and submission left serial, which is allowed:
2.02 ms, only 1.87x. It stalls on the one thing D3D11 cannot parallelise. At
eight threads it is 1.81 ms and barely moving, while D3D12 reaches 0.73 ms.

So **D3D12 is worth about 0.97 ms a frame beyond what restructuring alone can
get** - 48% of the restructured frame. Not the headline the original "Why"
section implied, and not nothing either.

### What this says about the order of work

Restructuring the bridge to do its per-draw work off the submit thread is the
prerequisite for both, and it pays 1.87x on D3D11 as it stands, with no
backend migration at all. It should come first. D3D12 then removes the ceiling
that restructuring runs into.

### The caveat, answered: why the current code cannot be threaded at all

The simulated work above is perfectly parallel - no shared state, no locks.
The real work is not, and the reason is sharper than "some shared state".

Every registered shader owns one `ShaderBindings`, and inside it **one GPU
constant buffer per constant slot**, with a CPU byte image beside it. A
material activation writes that byte image; the draw that follows calls
`Bind`, which does `UpdateSubresource` into that one buffer and clears a
dirty flag. So a frame's 44,917 activations are not independent work items:
they are overwrites of a single buffer per shader, and correctness depends
entirely on each draw being submitted between one activation and the next.

Two consequences, and the second is the important one.

**Threads cannot be added to the current code, on either API.** This is not a
data race that a mutex would fix. One buffer can hold one material's values
at a time, so two draws with the same shader cannot be in flight with
different constants no matter how they are synchronised. This game reuses
shaders heavily - 130 shader entries across ~2,370 draws a frame - so that is
the common case, not an edge one.

This also corrects the D3D11 control in the table above. It assumed the
non-API work could be split off and submitted serially. In the code as it
stands it cannot: the "decode" *is* the mutation of the shared buffer. The
1.87x is what a restructured D3D11 could reach, not what today's could.

**The fix is the thing D3D12 already does.** Making draws independent means
each carrying its own resolved constant bytes instead of pointing at one
shared buffer. That is exactly `NativeBackendRecorder::SetConstants(stage,
slot, bytes)`: bytes go into the fenced upload ring and their address into a
root CBV, so every draw has its own copy and nothing is overwritten in place.
The seam was designed that way because D3D11's renaming had no equivalent,
and it turns out to be the same change parallelism needs.

So the honest summary is better than the one the measurement alone gave:
restructuring is required either way, it is the larger part of the work, and
on D3D11 it buys 1.87x and then stops on serial submission, while the D3D12
backend needs no further change to go past it.

What remains genuinely unmeasured is contention elsewhere in the draw path -
`model_buffers` retain/acquire, the render-state cache, the write-ownership
registry. Those are ordinary shared containers and can be sharded or made
lock-free; none has the one-buffer-per-shader property that makes the
constant path structurally serial.

## Measured surface to replace

```
D3D11-specific code      3,529 lines across 14 files
API-independent code    10,630 lines (effect parsing, formats, guest logic)
distinct context methods    47
distinct D3D11 types        20
```

The renderer is already split usefully: `edf_native_effects` (SGSL/DXSL parsing,
movie/XUI/font effects) and every `guest_*.h` are API-independent and stay. Only
`edf_native_d3d11` and the bridge's call sites move.

Of the 47 context methods, about ten are **getters** (`VSGetShader`,
`OMGetRenderTargets`, `CSGetShaderResources`, `SOGetTargets`, ...). They exist to
save and restore state around foreign work. See the state-ownership consequence
below: they have no equivalent to port.

## What the shaders actually bind

`edf_native_shader_check <game> --slots` reports the widest slot any disc
shader uses, across 44 effects / 130 shader entries. A D3D12 root signature is
fixed at creation, so it has to be sized from this rather than from the API
maximum:

| | used | D3D11 maximum |
|---|---|---|
| PS constant buffers | 1 | 14 |
| PS textures | 7 | 128 |
| PS samplers | 7 | 16 |
| VS constant buffers | 2 | 14 |
| VS textures / samplers | none | - |
| widest constant buffer | 3,824 bytes (`m_Water01_DNDNAC.dxsl:PS_Main`) | - |
| vertex input elements | 9 (`c_Mech01.dxsl:VS_Blend`) | 32 |

This is small enough to change the design rather than merely inform it. The
three constant buffers can be **root CBVs** - a GPU virtual address written
straight into the root arguments, with no descriptor heap traffic at all. At
44,917 activations a second that removes the single highest-frequency piece of
descriptor work in the frame, which is the opposite of what consequence 4
assumed when it said this path was the most likely place to get lifetime wrong.
The lifetime problem stays (the memory is still fenced ring memory); the
descriptor problem disappears.

Textures still need a descriptor table, because a root SRV can only address a
buffer. Seven per draw, from a per-frame descriptor ring.

Caveat on record: these are the **disc** shaders. The renderer also compiles
its own HLSL for UI, font, movie and post, which this tool does not enumerate.
The root signature is therefore sized from evidence but **verified against
reflection at pipeline creation**, so a shader that needs more fails loudly
instead of silently losing a binding.

## Five consequences that decide the design

**1. There is no device state to query, so we must own it.**
The getters above read state back out of the D3D11 context. D3D12 and Vulkan
keep no such state. Every one of those call sites becomes "read our own recorded
state", which means the backend interface has to be the authority on bound
state rather than a thin pass-through. This is the single largest structural
change and it touches the bridge, not just the backend.

**2. `SwapDeviceContextState` isolation becomes separate command lists.**
`d3d11_frame_handoff.cpp` swaps into an isolated D3D11.1 context state so
presentation work cannot clobber renderer state, then restores. Neither target
API has this. The replacement is recording renderer and presentation work into
separate command lists, which is cleaner, but it is a redesign of the handoff
rather than a translation of it.

**3. Independent state objects become pipeline objects.**
Blend, depth-stencil, rasteriser, input layout and the shader pair are set
independently today. In both target APIs they fuse into one pipeline object that
must be created ahead of use and cached on the whole combination. `RenderStateWords`
is already a six-word key with a cache behind it, so the shape exists - but the
key has to grow to include the shader pair and input layout, and pipeline
creation is far more expensive than `CreateBlendState`, so a cold miss during
gameplay is a visible hitch rather than a small cost.

**4. Dynamic constants need an upload ring and fences.**
`UpdateSubresource` and `Map(WRITE_DISCARD)` currently carry constants at
**44,917 activations/s**. Both targets require explicitly managed upload memory
plus fencing so a buffer is not rewritten while a frame still reads it. This is
the highest-traffic path in the renderer and the most likely place to get
lifetime wrong.

**5. Hazard tracking becomes explicit barriers.**
D3D11 inserts barriers implicitly. Every render-target write, resolve, copy and
subsequent sample needs an explicit transition. Missing one is a silent
corruption that appears only on some hardware, which makes this the hardest
class of bug to catch in this project's usual way.

## Staged plan

Historical plan: the 77.4% figure below is a candidate estimate from the old
12-word bridge audit, not a verified instancing eligibility rate. Its key covers
geometry handles, shader identities, draw ranges, and four render-state words.
It does not compare textures/samplers, render targets, viewport/scissor, buffer
revisions, or complete constant payloads. Actual batching would need compatible
recorded state plus shaders that consume distinct instance constants, with
ordering and resource lifetime preserved. The current backend supports explicit
instanced draws but does not automatically merge the game's draws.

**Stage 0 - instancing (do first, independent of this).** 77.4% of draws are
collapsible into a preceding instanced draw, with zero register-shape breaks
across 14.7M draws. It helps D3D11 now and makes every later stage cheaper:
fewer pipeline binds, fewer command-list entries, less to record per thread.

**Stage 1 - the interface.** *Done, and with two backends behind it rather
than one; see the status sections below.* Introduce a backend interface at the level the
bridge actually needs (resources, pipeline state, bindings, targets, clears,
draws, copies, dynamic uploads, queries) rather than a 1:1 mirror of D3D11's 47
methods. A D3D11 implementation sits behind it and keeps working. This is where
consequence 1 gets paid, and it is worth doing on its own merits.

**Stage 2 - the second backend.** Vulkan preferred. Pipeline cache, descriptor
management, upload ring, barriers.

**Stage 3 - multithreaded recording.** The actual payoff, and only reachable
from stage 2.

## Where the D3D12 backend actually is

Built and tested on WARP, so the suite runs on a machine with no D3D12
hardware. Selected by name through the registry (`d3d12`, or `d3d12-warp` to
force the software rasteriser), which is how a rendering difference gets
attributed: reproduces on WARP, the bug is ours; does not, it is the driver's.

Working, each verified by reading pixels back rather than by inspection:

- Device, queue, per-frame command allocators, fences. 32 frames through 3
  slots, readback matching the frame that wrote it.
- Fenced upload ring, shared by constants, buffer and texture staging.
- Root signature sized from the shader measurement; constant buffers as root
  CBVs, so a material activation costs no descriptor work at all.
- Pipeline cache on the fused description, with scissor deliberately excluded.
- Every shader validated against the root signature by reflection before it
  can reach a pipeline.
- View descriptor ring, sampler tables cached by combination.
- Flat triangle: 2,016 of 4,096 pixels where half is expected.
- Textured draw: a 2x2 texture uploaded and sampled, all four quadrants
  checked so a flip or row swap cannot pass.
- Indexed instanced draw with a per-instance input element: four instances,
  one per quadrant, nothing in the centre. This is the stage-0 primitive.
- Presentation: flip-discard, three buffers, nine frames to a real window.
- Parallel recording from two threads: 1,024 pixels from each, neither
  writing into the other's half.
- Occlusion query returning 2,016 samples, matching the pixel count the flat
  triangle test measures independently.
- Mipped texture upload, checked by sampling level 1 explicitly.
- 4x multisampled target, resolved and sampled: 2,016 fully covered pixels
  matching the single-sample count, plus 64 partially covered ones - exactly
  the length of the diagonal, and values single-sample rasterisation cannot
  produce.

Not done, and loud rather than silent about it:

- **The bridge still calls D3D11 directly.** The game can now create and
  report a backend (`--edf_native_backend`), but nothing draws through one
  yet. See "How the port actually has to happen" below.
- No vertex-stage textures or samplers (no disc shader uses any, so this is a
  declared limit rather than a missing feature).
- Wiring is the 13 touchpoints and is the largest remaining piece, and it
  cannot be a translation: see the constant-buffer finding below.

### Consequence 5 is no longer a prediction

Twice now, disabling resource barriers produced **pixel-identical, fully
passing output** - the same 2,016 covered pixels, every colour check green -
and only the drained validation messages caught it. That is why validation
draining is on the seam rather than in one backend, and why the tests read it
instead of leaving it in the debugger where an automated run never looks.

### Seam gaps found by building against it

The interface was written before any backend existed, and four things in it
were wrong. Each is now fixed rather than worked around:

- It required pipelines everywhere and created them nowhere.
- It had `Submit` but no `BeginFrame`. D3D11 needed none; everything else does.
- It declared a sampler type with no way to create one.
- It could not read its own output, so a backend could not be compared against
  the one it replaces.

## Backend compatibility: what the seam can and cannot express

Every capability the renderer uses is now expressible through the seam and
produces **bit-identical output on both backends**. Checked by running the same
rendering through each and comparing pixels, not by inspection:

flat draw, textured draw, block-compressed (BC1) textures, mipped upload
sampled at an explicit level, indexed instanced draws with a per-instance
element, constant blend factor, depth testing against a depth target,
two render targets from one draw, 4x multisample resolve, occlusion queries.
Zero differing pixels on every one, including the resolve, where a tolerance
for differing sample positions turned out to be unnecessary.

Four real gaps were found by checking the renderer's own state rather than
assuming, and each would have rendered wrongly rather than failed:

- **BC row pitch computed in texels rather than blocks.** A 2x2 level of a BC
  format still costs a whole 4x4 block, so the second row of blocks came from
  the wrong offset.
- **BC format numbers shifted by one**, putting BC4's 8-byte blocks in with
  BC3's 16-byte ones. This would have halved every BC3 mip offset - the game's
  commonest texture format. The BC1 test passed anyway, which is why the format
  table is now proved by static_assert rather than by a test.
- **No constant blend factor.** The decode computes `requires_blend_factor` and
  the renderer honours it; the backends bound a hardcoded white. Those
  materials would have drawn in the wrong colour. A draw that needs one and was
  not given one is now refused, as the renderer refuses it.
- **No MIRROR_ONCE address mode**, which the guest sampler decode does emit.

Deliberately still absent, with reasons:

- **Stencil.** The renderer's own decode refuses it too, so the backends are as
  capable as the thing they replace. It would have to be added to both at once.
- **Vertex-stage textures and samplers.** No shader uses any - not the 44 disc
  effects, and not the renderer's own UI, font, movie and post HLSL, which bind
  only t0/s0/b0. Reflection at pipeline creation refuses a shader that needs
  more, so this fails loudly if it ever stops being true.

## What stands between here and D3D12 by default

Compatibility is no longer the blocker. The renderer is.

Every resource the game draws with is still made of D3D11 objects created by
these files, and D3D12 cannot borrow them - unlike the D3D11 backend, which can
adopt the renderer's own device. They have to be built through the seam:

| file | lines | D3D11 references |
|---|---|---|
| `d3d11_texture` | 555 | 84 |
| `d3d11_mesh` | 527 | 50 |
| `d3d11_ui` | 156 | 44 |
| `d3d11_render_state` | 124 | 30 |
| `d3d11_quads` | 136 | 27 |
| `d3d11_frame_compositor` | 115 | 25 |
| `d3d11_bindings` | 332 | 24 |
| `d3d11_frame_handoff` | 64 | 14 |
| the rest (`gpu_timer`, `completion`, `signals`, `presenter`, `effect`) | 552 | 32 |

Until those are ported there is nothing for D3D12 to draw, so making it the
default would mean a device that costs memory and start-up time and renders
nothing. The default stays D3D11 until the port lands; the flip itself is one
line and the conformance test is what says it was clean.

*(That table is the state before the port started. What has actually moved is
recorded under "Step 2" below; what has not is presentation.)*

### Port progress

The files above are mostly guest logic wrapped in a thin D3D11 shell - the DDS
loader was 96 lines of which ten were D3D11 - so the port is much less new code
than the line counts suggest. The guest halves are being lifted out first,
because each one is shared, safe, and verified by the tests that already exist:

| done | what moved out |
|---|---|
| render state decode | guest blend / depth / raster words |
| guest vertex streams | validation, endian swap, quad-to-triangle expansion |
| DDS decode | header, formats, channel masks, cube faces, mip chains |
| mesh input layout | neutral, retained, owns its names, fingerprinted |
| shader constants | `ConstantImages` - the bytes a draw carries itself |

The thin half was expected to be one connected change rather than five
separable ones - a mesh drawn through a recorder needs a pipeline, which needs
the layout and the render state, which needs the targets' formats - and it
mostly was. It landed in that order and each step was playable: buffers and
textures, then targets and material bindings, then the frame lifecycle, then the
draws. What did separate cleanly is the last piece, the compositor and the
presenter, which are still D3D11 and are what "What is left" below describes.

Doing it on the adopted D3D11 backend first means the game keeps working and
any difference is a wiring mistake rather than a backend one - and the
conformance test already says the two backends agree on everything the
renderer does.


## The frame-time finding that none of the reasoning found

Everything above about threading was reasoned from per-draw arithmetic in a
menu scene. Scripted combat, seven minutes of it, found something none of that
arithmetic could have:

| | frames | fps | `immediate.context_wait` |
|---|---|---|---|
| present inside the renderer's lock | 3,072 | 7.3 | 11.043 us/call |
| present outside it | 24,508 | **58.4** | **0.021 us/call** |

The draw thread was spending 21.4 seconds of a 420-second run blocked on the
renderer's context mutex - about 11 microseconds on each of 1.9 million
immediate draws. `immediate.native` fell from 12.250 to 1.239 us per call, and
the entire difference is that wait. The work was always about 1.2 us.

The cause was the host surface presenting inside the frame visitor, which
holds the renderer's lock. Necessary for the D3D11 compositor, which shares
the context; not necessary for a backend presenting from its own device, and
expensive, because that present waits on a fence for a frame slot - holding
the renderer's lock while waiting on a GPU.

### Why the menu measurements missed it

A menu scene is 97% vsync idle. Contention on a lock shows up as a wait, and
a thread that has nothing to wait for does not contend. Every per-draw figure
taken there was accurate and every conclusion drawn from it about where frame
time goes was wrong, because the scene had no frame-time pressure to expose.

The lesson is cheap to state and was expensive to learn: **measure the
workload that matters, even when a representative one is inconvenient to
produce.** `EDF_INPUT_SCRIPT=tools/native-combat-input.txt` drives it without
a human at the controls, and it cost seven minutes.

### What the binding work is actually worth, measured in combat

A run that reached mission geometry - 10.5 million indexed draws - says both
optimisations land on nearly everything:

  binding reuse    bound 684,912   skipped 35,315,088   98.1%
  material reuse   36,000,000 draws, 32,164,091 constants-only   89.3%

Higher than the 77.4% the batch audit predicted, because that figure counted
draws matching on a full twelve-word key including index ranges, while these
only need the target, render state and material to be unchanged.

`indexed.native` costs 1.65 us per draw in that run, against the 1.83 us
measured before this work.

### A measurement trap worth knowing about

Frame counts here come from summing `calls=` across `swap.refresh_wait`
reports, and a phase only reports once per 5 seconds. If a run stops early -
one did, hanging after 79 seconds of an 800-second script - the log still
looks complete and the arithmetic silently divides real frames by an assumed
duration. That produced a "5.6 fps in combat" figure that was wrong by a
factor of ten; the run was at 56.3 fps and had simply stopped.

Always check the span between the first and last log timestamp against the
intended run length before dividing by it.

### Where the frame goes now

At 58.4 fps, per frame: 8.6 ms waiting for vsync, 3.1 ms in the game's own 60
Hz pacing sleep, 2.35 ms waiting for the GPU, and about 1.5 ms of our CPU work
across the immediate and XUI paths. The game is at its frame cap.

That is a UI-heavy scene, though - the run recorded no indexed draws at all,
so the geometry path and the two binding optimisations built for it were
unmeasured here. The combat script runs to 775 seconds and the run was cut off
at 420, which is the whole reason: it measured the menu. The mission itself
begins at 507 seconds; runs that reach it are recorded under "the scene's own
resources" below, where `indexed.native` measures 1.6-1.7 us per call across
roughly 740,000 indexed draws per sampling window and `immediate.context_wait`
stays at 0.019-0.020 us - the lock fix above still holding under a million
draws a minute.

## Threading: three designs, and why only one of them can work

The simulation said 3.59x at four threads. Getting there needs a design that
fits how the game actually calls us, and two of the three obvious ones do not.

**Fan out each hook.** Dead. There is no batch to spread: a material
activation carries about 2.5 parameter uploads and each draw is its own call.
Dispatching 0.63 microseconds of work to a pool costs more than doing it.

**Decode ahead of the draw.** Also dead, and less obviously. The idea was that
the hook copies the guest bytes it must read now - the game may overwrite them
the moment it returns - and queues the conversion, so following draws submit
while it happens beside them. Measured against the real call order, there are
no following draws to overlap with: the hooks strictly alternate, activate,
draw, activate, draw. The draw that would hide the latency is the one waiting
for it. Nothing overlaps.

**Record, then submit.** The only one left. Hooks record what each draw needs -
pipeline, constant bytes, textures, buffers, draw arguments, viewport, target -
into a per-frame buffer, and the frame is decoded and submitted at the end.
That gives a batch, which is what both dead designs were missing, and it is
what makes parallel recording on several command lists reachable.

The record format is the seam. A recorded draw is a `NativeBackendPipelineDesc`
plus the arguments to `SetConstants`, `SetTexture` and `DrawIndexed`, which is
what those calls were shaped for. So **threading and the port are the same
work**, not two things to schedule against each other.

### What is built

`NativeDecodeWorkers` is the pool that design needs, and it is finished and
tested: 4,000 contended jobs, out-of-order completion, drain, destruction with
work outstanding. Retirement only advances past tickets nothing is still
working on - the naive "highest finished ticket" would let a draw bind
constants that were never written, and the test catches that mutant by name.

Zero workers runs every job inline, so threading off is the same code path
rather than a special case.

### What the ceiling actually looks like

Measured per activation, in a menu scene where the sub-phases are visible:

| | us/call |
|---|---|
| `activation.native` total | 2.438 |
| of which `activation.original` - the guest's own recompiled code | 0.778 |
| `activation.textures` | 0.577 |
| `activation.resolve` | 0.518 |
| `activation.params_ps` + `params_vs` | 0.627 |
| `activation.bind` - must stay on the submitting thread | 0.314 |

Roughly a third of an activation is the game's own function and cannot move at
all, and the bind cannot leave the submit thread. That is a smaller
parallelisable fraction than the simulation assumed, so 3.59x should be read
as an upper bound that nothing will reach rather than a target.

These are menu numbers. Gameplay is the case that matters - it is where
`indexed.native` dominates and where the earlier 62.8% draw-thread figure came
from - and the same breakdown has not been taken there yet. It should be, before
the record buffer is sized or the worker count is chosen.

## How the port actually has to happen

Two facts decide this, and neither was obvious before a backend existed.

**Backends cannot be mixed inside a frame.** A D3D12 render target cannot be
composited by a D3D11 path, and two D3D11 devices cannot share a texture
either. Taken alone, that would force the whole 3,529-line port to land as one
change that either works or does not.

**But a backend can adopt the device the renderer already owns.** With
`AdoptNativeD3D11Backend`, ported paths and unported paths draw into the same
targets, so the port can go one path at a time with the game playable after
each. `--edf_native_backend=d3d11` does exactly this in the game today.

So the order is:

1. **Backend selection in the game.** Done. `--edf_native_backend` creates a
   backend inside the real process and reports it; an unknown name is refused
   with the reason and the valid names in the log.
2. **Move paths onto the seam, D3D11 adopted underneath.** Each path
   verifiable by playing. The renderer keeps working throughout, and any
   rendering change is a wiring mistake, because the backend underneath has
   not changed.
3. **Restructure constants as each path moves.** Not a separate step: a path
   on the seam already carries its own constant bytes through `SetConstants`,
   which is what makes draws independent. `ShaderBindings::ConstantImages`
   supplies them.
4. **Flip to D3D12.** One change, once nothing calls the context directly.
   The conformance test is what says the flip was clean.
5. **Then threads**, which only step 3 makes possible.

### What has to move, measured

The bridge itself barely touches D3D11 directly - about 17 context calls. The
work is in what it hands the context to: 69 sites pass `*state.context.Get()`
to a helper, and those helpers are the port.

| | |
|---|---|
| `state.context` / `state.device` references in the bridge | 137 |
| direct context calls in the bridge | ~17 |
| sites handing the context to a helper | 69 |
| D3D11-specific lines across the renderer | 3,529 |

Helpers to move, roughly in dependency order: `d3d11_render_state` (already
decoded through the shared path, so only the objects remain),
`d3d11_bindings`, `d3d11_mesh`, `d3d11_texture`, `d3d11_quads`, then the
effect paths (`d3d11_ui`, font, movie, XUI) and the presenter.

### Step 2, started: the scene's own resources

The scene's textures and its meshes are created through the seam now. Nothing
about how they are drawn has changed yet; what has changed is who owns the
storage, and that is the half that decides whether a second backend can hold
the game's resources at all.

**A second cvar, because the two questions are different.**
`--edf_native_backend` is what the player selected. `--edf_native_scene_backend`
is what the half-ported scene can share targets with, and it must stay `d3d11`
until the last draw path has moved: a texture created on a second device cannot
be sampled by a draw that is still direct D3D11. Collapsing them into one would
have made selecting d3d12 mean "load the game's textures somewhere the renderer
cannot read them", which looks like a black screen, not like a half-finished
port.

**What each piece needed from the seam.**

| | what was missing | why it was not optional |
|---|---|---|
| textures | array slices and cube faces | the game ships cube maps; a texture desc describing one 2D slice cannot create what the renderer loads |
| textures | uploads covering every subresource | the first version filled face 0 and left the other five undefined |
| meshes | a dynamic buffer that means something | `dynamic` was declared and ignored, so every immediate-geometry rewrite would have gone through `UpdateSubresource` |

The dynamic-buffer gap is the one worth recording. `NativeBackendBufferDesc`
had a `dynamic` flag from the start and the D3D11 backend ignored it entirely:
every buffer was `USAGE_DEFAULT` and every update an `UpdateSubresource`. Both
are correctly ordered against queued draws, so nothing would have rendered
wrongly - it would simply have made the driver wait, on the path the renderer
rewrites every frame. The flag now creates `USAGE_DYNAMIC` and updates rename
with `WRITE_DISCARD`, and both backends refuse a *partial* update of one,
because the untouched bytes of a renamed buffer are undefined and a caller that
got away with it on D3D12 would have found that out on D3D11.

**What the tests had to become.** Each test that covered this already existed
and was passing; each was checking the old path.

* The cube DDS test now creates through the seam and reads back all 24
  subresources. Swapping the upload loop to level-major fails it on
  "compressed face/mip payload changed".
* The conformance test draws twice from one dynamic vertex buffer, rewriting it
  between the draws, into two targets. Changing `WRITE_DISCARD` to
  `WRITE_NO_OVERWRITE` fails it on "the draws either side of the dynamic
  rewrite overlap on 2016 pixels, so the rewrite reached the first one" - which
  is the actual defect, named.
* `edf_native_texture_check` validates all 83 disc DDS assets through the
  backend rather than a device; still 0 failures.

**In the game.** Two scripted combat runs, one per half, each reaching the
mission rather than stopping in the menus the way every earlier measurement in
this document did.

Compared at the same point in the script - 1.2 million indexed draws in, so
the same content has been drawn - rather than at whatever each run happened to
reach:

| | textures loaded | indexed draws | mesh builds | mesh bytes | errors |
|---|---|---|---|---|---|
| textures on the seam | 176 | 1,203,000 | 308 | 32,725,928 | 0 |
| meshes on the seam too | 176 | 1,184,000 | 308 | 32,725,928 | 0 |

The second run was stopped at 7,072,000 indexed draws, still 0 errors and 0
mismatches, 717 mesh builds against 7,071,283 cache hits.

The second row is the one that says the mesh change did nothing but move the
storage: same 308 builds, the same 32,725,928 bytes of converted geometry, and
zero vertex or index mismatches, against a cache-hit rate of 99.97%. Per-draw
cost across the two runs' sampling windows: `indexed.native` 1.699 us (741,005
calls) before the mesh change and 1.603 us (735,043 calls) after. These are
separate windows in separate runs, not a controlled A/B, so read them as "no
regression", not as an improvement. Offline,
`edf_native_geometry_check` builds a real mesh for all 858 (declaration, vertex
entry) pairs through a WARP *backend* now - 858 constructed, no declaration
left unbindable.

**One difference that was not free.** Immutable vertex and index buffers were
`D3D11_USAGE_IMMUTABLE` and are now `USAGE_DEFAULT`, because the seam has no
immutable flag and inventing one without a measurement to justify it would be
guessing. And the DDS path now concatenates the decoded levels into one block
before handing them over, where D3D11 took the level pointers directly - one
extra copy of each texture, at load, bounded by the asset's own size. Both are
recorded here rather than hidden: neither has been measured, and a later run
that finds load time worse should look here first.

**What is still D3D11 inside these two files.** The draw itself
(`IASetInputLayout` / `IASetVertexBuffers` / `DrawIndexed`), the dynamic vertex
write, the D3D11 input layout, and the stream-output clip-position capture. Each
reaches its resource through an unwrap helper - `NativeD3D11Buffer`,
`NativeD3D11TextureView`, `NativeD3D11BackendDevice` - which returns null on a
backend that is not D3D11 and is checked, so selecting d3d12 for the scene
fails with a sentence rather than binding nothing. Those helpers are the
remaining port, and they disappear with it.

### Step 2, finished: every scene draw records through the seam

`--edf_native_seam_draws` makes every scene draw path - indexed geometry, scene
immediate geometry, XUI, font, movie and the post chain's full-screen quads -
build a pipeline and record into a recorder instead of binding four D3D11
objects and calling the context. With `--edf_native_scene_backend=d3d11` both
draw the same thing on the same device, which is what makes it the A/B control:
a difference is a wiring mistake, because the backend underneath has not
changed.

In the game: 466,000 indexed draws, zero errors, and the same 294 mesh builds
and 31,944,056 bytes of converted geometry as the direct path reached at the
same point in the same script.

**What recording cost.** Recorded draws started at 2.60 us each against 1.62 us
direct, over 510,278 and 771,082 draws of the same script. The difference was
not the backend: the direct path skips 98% of its binding work when the draw
before it bound the same things, and the recorded path was doing all of it every
draw. Teaching it the same skip took that to 2.33 us over 680,547 draws. Not
rebuilding the pipeline description per draw, and not re-staging constants whose
bytes have not changed, came after that and are **not yet measured** - the run
that would have measured them was spent on the D3D12 selection instead. Two
things make the skip trustworthy:

* a material's resources change *inside* one bindings object when an activation
  re-points a texture, so the comparison is against a counter the bindings keep,
  not against the object's address;
* on the adopted D3D11 backend the recorder and the direct paths share one
  context, so a direct bind invalidates what the recorder believes is set - the
  existing bind generation already tracks exactly that.

### What selecting d3d12 for the scene found

`--edf_native_scene_backend=d3d12` now starts, creates the scene's targets and
its textures, builds pipelines and records draws through the menus. It has not
reached the mission - see "What is left" - so "every draw" is not yet a claim
anyone can make. Getting this far turned up four defects that only exist on a
backend with no D3D11 handles, and every one of them was a question asked of a
null pointer:

| what asked | what it did on D3D12 |
|---|---|
| "does this shader sample the target it draws into" | compared null to null, answered yes, refused every draw - and dereferenced that null to ask |
| "does this brush have a native texture" | looked for a D3D11 view, found none, refused |
| the XUI batch audit's shape hash | hashed the same null for every texture |
| frame publication and the HDR captures | dereferenced a null texture |

It also replaced one guess with a measurement and closed one capability gap.
The upload ring was 16 MB with a comment saying it was a guess and that the
high-water report should replace it; a real recorded frame wants more, so it is
a setting now, set from the figure the ring reports when a frame does not fit.
And the post chain's three converting targets resolved through a compute shader
and an unordered-access view - the one operation in this renderer
the seam cannot express, and one it never needed: the kernel read its source at
the dispatch coordinate and wrote the result, which is a full-screen draw
reading its own pixel coordinate. The arithmetic is unchanged.

### Presentation, and a misattribution worth recording

The scene's finished frame used to reach the window only as an
`ID3D11Texture2D` handed to `NativeFrameHandoff`. The seam could *consume* a
shared surface - `OpenSharedTexture` and `WaitSharedFence` are how the D3D12
preview samples a D3D11 frame - but not produce one, so a scene on one backend
could not reach a window on another. `CreateSharedSurface`, `CopyToShared` and
`SignalShared` are the mirror image, on both backends, and the bridge now
publishes by whichever route the scene can take. That route gives up the
compositor's display gamma, which is a visible difference rather than an
oversight.

**The stall was not presentation.** It was recorded here as "no frame is ever
published, probably", and that was wrong twice over. The first wrong turn was
blaming the presenting device: the removal reason, once it was printed in words
instead of as `0x887a0005`, said *the GPU hung on this device's own work*, and
the device it said it about was the scene's. The second was measuring with
`--edf_native_backend_present=false` to "isolate" the renderer - which is the
1.2 fps path this document already measured a hundred pages up. Every isolation
run was crippled by the flag chosen to isolate it, and the 1 fps that seemed to
indict D3D12 reproduced exactly on D3D11.

**One real cause found and fixed.** A D3D12 resource created with initial
contents is staged until the next frame opens, because there is no command list
to copy with before then - and the staging list held a raw pointer to a resource
the *caller* owns. This renderer destroys textures during loading every time the
guest reuses a handle. The next frame then read a freed object and copied its
contents into the command list: not a crash, a GPU hang, seconds later, with
nothing pointing back at it.

### Three hangs, and how each was found

None of them were found by reading the code, and all three were the same shape:
a D3D12 object holding something it did not own, or being handed something that
was not there yet.

| what held what | how it presented | what found it |
|---|---|---|
| the staged-upload list kept a raw pointer to a resource the caller owns, and this renderer destroys textures whenever the guest reuses a handle | `DEVICE_HUNG`, seconds later, nothing in the log | printing the removal reason in words instead of `0x887a0005`, which said *which device* |
| the recorder kept a raw pointer to every bound texture, across draws, and only rewrote the table when something marked it dirty | the same | a bisect: the same 75 textures with `--edf_native_seam_draws=false` do not remove the device, so it was the draws and not the uploads |
| the window took the *composited* frame for the scene's own, presented it directly, and never ran the composite that fills it | the presenting device hung on the **default** path | a bisect after a run meant only to confirm the default path failed: clean at the commit before, failing at the commit after, still failing after a full clean rebuild |

The third was mine, introduced while fixing the first two, and it broke the
configuration everybody actually uses. It is also the one I had already
declared safe.

What made the first two findable at all was giving the failures something to
say. A hung GPU used to present as a frozen process: the fence wait was
`INFINITE`, so the process stopped with no message and no clue which of a
hundred changes did it. It has a ten-second deadline now and reports the
device's removal reason, and the debug layer - with GPU-based validation, which
is the half that catches what a *shader* did rather than what the API was
asked - can be turned on for the hardware device instead of only for WARP.

### What is left

The scene on D3D12 hangs the GPU at the first recorded XUI draw, reproducibly,
at the same texture load every run. The same draw path on the adopted D3D11
backend runs a full mission, so it is not the wiring: it is something
D3D12-specific in that draw. That is one defect, precisely located, and it is
all that stands between here and the flip. Until it is found,
`--edf_native_scene_backend` stays `d3d11`, which is what the cvar has said
since it was added.

**Diagnostics that are D3D11 by nature** - the HDR range and BMP captures, the
depth inspection, the stream-output clip-position replay, the occlusion
visibility query - refuse with a reason on a recorded or non-D3D11 path rather
than reporting a number that means nothing. They are capture-time tools and are
not on the path to the flip.

## What this replaces

A renderer that currently works: 60 fps held, 18.9M indexed draws submitted with
zero unsubmitted, zero rejected contracts, 21/21 tests. That is the bar a second
backend has to meet before it can become the default, and the contract ledger
plus the geometry checker already give a way to measure it: a backend that drops
draws will show up as rejected contracts rather than as someone noticing a
missing mesh.

## Not decided

- Whether to vendor an existing dual-backend render interface rather than write
  one. A game-agnostic RHI is the reusable part of comparable projects, unlike
  their renderers, which are welded to their own guest interception. Confirm
  what such a layer actually is and its licence terms before committing; this
  repository already vendors third-party licences under `LICENSES/`.
- Whether D3D12 is built at all, or Vulkan only.
