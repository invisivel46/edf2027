# Native loading screen and latency investigation

## Scope and current result

User reports a black screen instead of EDF's loading UI and excessive loading
time. Neither symptom is fixed or conclusively attributed yet. This audit is
source inspection, not a timed runtime comparison. The new optional scalar-store
observer has not been runtime-validated and cannot explain earlier user reports.

## Presentation lifecycle evidence

In `src/native_graphics/guest_shader_bridge.cpp`, the C930 hook selects the
ordinary output surface and resets `scene.output.content_valid` to false.
The original recompiled C930 function ends the tiled pass and binds owner+112
as color and owner+120 as depth; binding does not itself initialize pixels.

Native XUI draws select the scene or ordinary output and preserve the target's
initialization status. Partial alpha geometry cannot establish that all output
pixels are initialized. The C840 hook invalidates the published image before
conditionally publishing ordinary output when `content_valid` is true.
`NativeHostSurface::Paint` explicitly clears its backbuffer black when no
published game image is available.

This establishes a possible black-output chain, not proof the loading screen
takes it. Also inspect direct C840 scene completion: the original function can
end either a tiled or ordinary pass, whereas ordinary native publication is
conditional on `active_output` matching its owner. Do not force validity true,
publish an arbitrary scene, or retain stale frames as a substitute for recovering
the actual loading-screen resolve/presentation contract.

Historical `native-gpu-boundary.md` records an actual loading-XUI rejection gap,
then scene/reversed-depth adapter acceptance. Submission counts established route
coverage, not the final visible loading image. Previous black captures described
as transitions did not establish that the missing UI was expected.

## Loading work and serialization evidence

`ImportTexture` snapshots the incoming image, calls the original loader, and
then creates a native DDS texture. This retains both guest loading work and
native resource creation; it is not a fully native resource-loading pipeline.
Native creation is inside both the submission and bridge-state locks.

`RegisterShaders` holds those same locks throughout its shader loop. It calls
`CompileNativeShader` once per entry and additionally for each vertex entry's
reversed-depth variant. `src/native_graphics/d3d11_effect.cpp` performs source
adaptation, reversed-depth preprocessing where applicable, optimizing D3DCompile,
reflection, and device shader creation. That function has no bytecode-cache
lookup. This does not mean compilation occurs on every draw: registered shaders
are reused, but each registration reaches this synchronous compilation path.

`VisitNativePresentationFrame` and `VisitNativePresentationContext` require the
same bridge-state mutex. Shader registration and native texture creation can
therefore delay host frame acquisition as well as loading progress. Lock scope
is verified; total cost, contention frequency and dominant bottleneck are not.

Earlier quadratic index-publication diagnostic scans were already reduced to
sampled scans (see geometry writer audit). No matched loading-time improvement
was measured. Remaining guest-memory comparisons are not established as the
cause of these loading symptoms.

## Evidence still required

1. Record the executable hash and user-visible mission-load start/end. Hidden
   automation has scheduled input delays and occlusion; its process lifetime
   is not a loading-time measurement.
2. Trace each loading frame's scene/output owner, XUI submissions, initialization,
   actual end-frame branch and publication outcome. Correlate with visible pixels.
3. Separate texture snapshot/original loader/native creation, shader compilation,
   model publication, file reads/decompression and lock waits. Timing must not
   double-count nested stages or conflate CPU Present return with scanout.
4. Use the observed failing contract to select a renderer fix. Use measured
   dominant costs to prioritize native loading ownership or shader caching.
   Any cache must include adapted source, include contents, entry, stage,
   depth variant and compile settings; guest handles are not durable cache keys.

## Opt-in eligibility trace

Added `--edf_native_loading_trace=true` (default false). At C840 entry, before
native scopes close, it counts eligible ordinary output, invalid ordinary output,
direct active-scene endings and unmatched endings. Logs contain the current
owner/scope, handoff availability and cumulative XUI/font/movie draw counts.
The first eight frames and then at most one sample per second are logged.
Counters retain routes skipped between samples. Counts are not publication
success, per-frame UI attribution or evidence of visible pixels.

The bridge object compiles through the configured CMake build. No game executable
was relinked or launched for this change. The trace still needs a runtime loading
capture; this is instrumentation, not a rendering fix. Native resource ownership,
validity, publication, pixel output and guest execution are unchanged.

## Runtime reproduction: direct-scene publication is missing

Built `edf2027-native-loading-trace.exe`, SHA256
`D83CAFAC09A2706555070F7E928836D2821F503B7A6C4335E033929EBF9A7B42`,
with scalar observation OFF. Launcher now accepts `-LoadingTrace`. Owned PID
28944 started at 05:10:22 with fresh user data, the existing movement/fire input
schedule, integer post centers enabled and pixel readback captures disabled.
This was hidden diagnostic execution, not a visible performance benchmark.

Evidence directory:
`out/native-bridge-run/binding-validation-20260910-051022-cd209d76`.
`early.png` is the new-data prompt; `loading.png` is mission/difficulty selection
(the name is not proof of loading). `mission-loading.png` shows black during the
actual load; `after-loading.png` shows the mothership intro afterward.

At 05:11:26 the last sampled ordinary output was valid. From 05:11:27 through
05:11:49 the end-frame samples show active_scene=40001cd0, active_output=0,
ordinary eligible count fixed at 3298, and direct_scene increasing from 457 to
1783. XUI counts increase from 1149645 to 1171571 in that same interval. At
05:11:47.708 the XUI draw diagnostic explicitly reports scene=true,
reverse_depth=true and target_initialized=true. Invalid ordinary output and
unmatched scope counts remain zero. Ordinary publication resumes at 05:11:50,
with direct_scene fixed at 1818. Frame endings continue roughly 60 times/sec
during this approximately 23-second sampled interval, which is not a full
mission-load stopwatch measurement.

The runtime therefore exercises the missing direct-scene branch, not the
initially suspected ordinary-output validity failure. C840's native direct-scene
branch clears scope and optionally captures it, but does not publish. Its
original C678 call sends the scene resolve destination to 82140E98. In default
untiled mode that hook omits Xbox work on the assumption the enclosing hooks own
resolve/publication; C840 does not currently fulfill that assumption for this
branch. This is a renderer migration defect, not merely a stalled loading UI.
Pixel correctness of the source scene still needs a capture/fix validation.

Next implement the recovered direct end-frame destination/format contract.
The host handoff requires RGBA8; scene color uses RGBA16F, so passing the scene
texture straight to Publish will fail. Do not repurpose gameplay tone mapping
or mark ordinary output valid without doing the appropriate resolve/conversion.
The rendering fix must be checked against loading UI pixels and normal movie,
menu and mission presentation. Long-load cost attribution remains separate.

Research notes 0526 and 0525 distinguish the startup ContentData engine-window
message from resource-loading transactions and mission loading. Claims of XUI
coverage alone must not be generalized to every startup loading panel.

Stopped the exact owned PID after the intro capture; logs and captures remain.

## Direct resolve implementation and format correction

C840 now resolves its direct scene to the actual backbuffer handle selected by
owner+140 and the table at owner+124. Native converted targets are retained per
destination handle, erased on final guest resource destruction, and owned by
the scene lifetime. The original C840 CPU cleanup still runs. The C930 ordinary
output/post path is unchanged.

`ResolveNativeRgba8Frame` copies or natively resolves 1/2/4-sample RGBA16F to a
dedicated conversion surface, then converts to RGBA8 without exposure, bloom,
display gamma or CPU readback. It rejects aliasing, mismatched dimensions/formats
and device mismatch, and invalidates destination content for unwritten sources.
It does not overwrite the scene's separately resolved HDR history.

First candidate `3CEE42825FBF742931FDC2302EFB4411B8EAA56469D94EB8EB066298E2D0E311`
ran as PID57804 in `binding-validation-20260910-051648-90bb2df1`. Its initial
ordinary-format guard rejected actual direct destination `0x28280106`, starting
05:17:02. The exact owned process was stopped. No successful loading-screen
claim applies to that candidate.

The corrected path accepts the observed direct format only. Its low-six format
code is 6 (8:8:8:8) and its component selectors are 2,1,0,5; selector 5 supplies
constant one. Native logical-color storage preserves RGB and supplies opaque
sampled alpha via `CreateNativeOpaqueFrameTarget`. Format/swizzle constants were
cross-checked against the primary Xenia header:
https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/xenos.h
(TextureFormat and XE_GPU_TEXTURE_SWIZZLE). This is format research, not a new
runtime dependency or GPU emulation path.

WARP tests exercise both alpha-preserving and opaque conversions at 1/2/4 samples,
clamping/quantization of (-1,.5,2,.25), all pixels of a 9x3 target (including
compute-dispatch edge lanes), unwritten source behavior, dimensions and alias
rejection. All 18 tests pass in 15.78 seconds. They do not establish original
console rasterization equivalence or exercise the complete game-hook lifecycle.

Corrected candidate SHA256:
`E9C37552643AB828D7ED1163B32603ECF6E4BB4859947C25E1C5C4BB1D8CF21F`.
Runtime validation started as PID27292 at 05:20:37, directory
`binding-validation-20260910-052037-93a5bb81`, same diagnostic launcher/input
schedule. Runtime outcome pending below.

### Corrected candidate: loading UI visible

At 05:21:48, `selection.png` (misnamed: this is already loading) shows the
Flamethrower tip, EDF emblem, background art and Now Loading text. At 05:22:03,
`loading-second.png` shows the same tip with changed background/animation state.
Both captures are during the direct-scene interval, not ordinary-output frames.
The 05:22:03 trace still has eligible=3298, direct_scene=1722 and increasing XUI
draw totals. No `Native direct frame resolve` errors were reported. These images
validate the previously missing visible UI path, not original-console pixel
equivalence or reduced loading time. The diagnostic filename is not a claim
about game state; the image and frame trace establish that state.

At 05:22:30, `after-loading.png` shows the mothership intro. Ordinary output is
valid again; direct_scene is stable at 1938 and eligible rises normally. No
direct-resolve or frame-publication errors appear in this run's logs. Stopped
the exact owned PID27292 after this capture. No claim of full mission regression
coverage or visible FPS follows from this hidden startup/loading/intro test.

## Resource-load phase measurements

Added independent opt-in `edf_native_load_timings` (launcher `-LoadTimings`).
Existing per-thread HookTiming now reports each completed resource phase, not
the draw profiler's 256-call threshold. Texture snapshot, original loader,
lock acquisition and native DDS creation have separate scopes. Shader lock and
entry preparation (compiler, reflection, bindings, reversed variant) are nested
inside whole registration. These are inclusive CPU wall times; never add the
nested shader totals to registration. Synchronous logging overhead can perturb
the run, and registration includes its nested logging overhead.

New read-only `tools/summarize-native-load-timings.ps1` reads rotated game logs,
groups phases, and optionally filters completion timestamps with Since/Until.
It warns about overlap and missing rotated records. A boundary-crossing call is
included in full by its completion timestamp, not clipped to the interval.

Candidate `edf2027-native-load-timings.exe` SHA256
`8EBDC773E212037F7C2DA3BCBE53A0E7F1B195E27E343290F12280DAF3B0CDD6`
built successfully and ran as owned PID4196 from 05:24:39 through mission intro.
Directory: `binding-validation-20260910-052439-fe18bd5a`. Same hidden/fresh-user
input schedule, corrected loading resolve, scalar observer OFF. The first 191
texture loads accumulated 5480.7574ms original work, 73.4037ms native creation,
1220.7393ms lock acquisition and 6.7939ms snapshots. Early shader registration
totaled 160.5149ms across four registrations.

For completion timestamps 05:25:45 through 05:26:15, covering the latter mission
loading interval and transition into the intro:

| Phase | Calls | Total CPU wall ms | Maximum call ms |
| --- | ---: | ---: | ---: |
| Texture original | 407 | 7516.7919 | 52.7317 |
| Texture native creation | 407 | 99.4342 | 20.7864 |
| Texture lock acquisition | 407 | 260.6878 | 17.7171 |
| Texture snapshot | 406 | 20.6776 | 0.8287 |
| Shader registration | 17 | 811.6141 | 205.9614 |
| Shader entry (nested) | 43 | 643.6217 | 49.5172 |
| Shader lock (nested) | 17 | 164.5080 | 15.1050 |

`phase-check.png` at 05:26:14 shows the intro city scene after loading. The
missing snapshot call relative to original/create follows completion-time
filtering; phases of a single invocation may straddle the lower bound.

The retained original texture loader is the largest measured resource phase,
not native shader compilation. This is evidence to prioritize its remaining
conversion/synchronization work, not proof of the complete load bottleneck or
an attribution to any specific wait. CPU wall time includes blocking. File
reads/decompression/model creation outside these scopes remain unmeasured.
Inspect 82201458's downstream texture creation/upload/conversion calls next;
do not simply delete the original call, which still establishes guest resource
metadata, handles and CPU-side effects. Stopped exact owned PID4196; evidence
retained. The known user-testable loading-resolve binary was not overwritten.

### Nested attribution: upload work is not the dominant measured cost

Added loader-thread-scoped attribution for original allocation 8213B730,
upload branches 821FFB38/822001E0 and preparation 822009B0. RAII depth tracking
restores the scope on return/exception and does not change original execution.
Allocation measures the original allocator only, excluding native registration
after it. The upload labels identify these branches, not exclusive attribution
of every downstream operation. All 18 suites pass in 14.49 seconds.

Candidate SHA256 `6D814E035FEE941383EA5CBB5029586F90DA799A0BA3A74433C93C7D67A1DBDE`
ran as PID47452, directory `binding-validation-20260910-052902-ead49418`, from
05:29:02 through 05:30:53. It returned to ordinary output after mission loading.
Stopped exact owned process. For completion timestamps 05:30:07..05:30:40:

| Phase | Calls | Total CPU wall ms |
| --- | ---: | ---: |
| Original texture loader | 449 | 9272.9200 |
| Original allocation (nested) | 449 | 26.3368 |
| Upload2D (nested) | 4748 | 85.0850 |
| Preparation (nested) | 449 | 0.6226 |
| Native texture creation | 449 | 103.0360 |
| Native texture lock | 449 | 78.9009 |
| Shader registration | 17 | 775.0518 |

There were no volume-upload records in this sample. Nested timings do not
cover all loader instructions, temporary-resource creation/release, all bridge
callbacks or synchronization. Thus 'original loader time' is NOT evidence of
9 seconds of CPU pixel conversion. The instrumented allocation/upload work is
small compared with the enclosing duration.

A concrete synchronization candidate was found: the native swap barrier retains
`state.submissions` across GPU completion and refresh pacing sleeps, releasing
only `state.mutex` during polling. The native registration tail of 8213B730 and
final-resource destruction 82134220 also acquire submissions before the registry
lock. For example, a 63.8491ms texture load at 05:29:16.670 contains tiny upload
calls separated by ~16ms gaps. This is consistent with loader bookkeeping
waiting behind swap pacing; individual lock attribution is still needed to
quantify how much of the remainder it explains.

Next separate metadata-only lifetime/registration work from command-order
barriers where ownership permits it. Do not release the swap barrier wholesale:
it orders actual rendering commands. Do not remove guest image conversion on
the mistaken premise that these measurements proved it consumes the wall time.

## Metadata-only texture lifetime operations leave the submission barrier

Removed `state.submissions` acquisition from exactly two native bookkeeping
sections: successful texture creation registration (8213B730 after its original
allocator) and final-resource registry retirement (82134220 before its original
destructor). Both retain `state.mutex` for atomic lookup/lifetime invalidation.
Audited the retirement body and mesh/model/declaration retirement helpers: they
erase registry/cache owners and release references, without issuing immediate
context commands. Native draw/resolve users consume registry objects under the
same state mutex. Submitted native graphics work retains its resource lifetime
independently of these registry entries. Original guest calls and CPU cleanup
are unchanged. The swap's command-order barrier and all pacing remain intact.

This does not remove locking from texture uploads, binding changes, draw calls,
or any other hooks. No guest-memory comparison was disabled by this change.

Build and all 18 existing suites pass in 14.29 seconds; those tests are not a
complete multithreaded game-lifetime proof. Candidate `edf2027-native-loader-locks.exe`
SHA256 `BC6596A804B1B5ECA7A314FACCC748F6F21D93730C4277D853C1D16B6D1A5405`
started as PID51424 at 05:33:19 in
`binding-validation-20260910-053319-d7b92b18`, with the same hidden validation
schedule, integer centers and load/frame diagnostics. Runtime results follow.

### Matched loader-lock comparison

Both the preceding baseline and candidate reached 694 texture loads, 6980
upload2D calls, 21 shader registrations and 65 shader entries. All recorded
texture phases at that checkpoint (ms, across threads):

| Phase | Baseline | Metadata-lock candidate |
| --- | ---: | ---: |
| Texture original | 19847.1848 | 448.3187 |
| Texture lock | 774.0268 | 8314.8046 |
| Texture native creation | 190.1811 | 181.4091 |
| Texture snapshot | 28.4052 | 28.9046 |
| These non-nested texture phases summed | 20839.7979 | 8973.4370 |

Nested upload2D totals remain small (125.7571 vs 114.2271ms); shader registration
is similar (935.0058 vs 980.5831ms). The matched call counts strengthen this
comparison, but it is one instrumented hidden run per candidate, not a controlled
visible loading-time benchmark. Do not claim a matching seconds reduction in
end-to-end loading time. Logging/scheduling may change contention distribution.

The original-loader phase no longer contains large pacing delays. Some wait
time now accumulates at the remaining ImportTexture submission lock, while the
combined measured texture total drops substantially. That lock surrounds native
DDS resource creation and registry publication; audit its device-only work next
before deciding whether it also needs command-order serialization.

`loading.png` at 05:34:42 shows the Shotgun tip and Now Loading UI. No texture
error or direct-resolve error was found in retained logs. Despite its filename,
`intro.png` still shows loading (the Grenade Launcher tip), NOT the mission intro.
Stopped the exact owned PID51424 after that capture. This run was stopped too
early to establish eventual mission entry; a longer regression run is required.
No mission-entry/combat regression or user-visible FPS result is claimed.

### Longer metadata-lock regression

Repeated the unchanged BC6596 candidate as PID53480 from 05:36:15 to 05:39:47,
directory `binding-validation-20260910-053615-35662f7b`. At the 694-texture
checkpoint original loading totaled 473.2985ms, native creation 180.0859ms,
snapshot 28.7337ms and remaining upload-lock waits 8442.8746ms. This repeats the
earlier observation, including the relocated contention at ImportTexture.

`load-first.png` actually shows the city intro, then `gameplay-check.png` shows
the second loading interval. `gameplay-after-second-load.png` at 05:39:31 shows
the player away from the starting position, city/radar/AF14 HUD and RELOAD during
the movement/fire schedule. Thus the metadata-lock candidate passes this bounded
mission-entry/input regression. It does not establish an entire mission's
fidelity or a visible-frame-rate benchmark. Stopped exact owned PID53480.

## Native DDS creation also leaves the submission barrier

Audited CreateNativeDdsTexture: CPU DDS preparation plus ID3D11Device
CreateTexture2D with initial data and CreateShaderResourceView; no immediate
context operations. Removed only ImportTexture's submission lock. Its state
mutex still protects native registry publication/replacement and device access.
Native draw/resolve submission barriers and pacing are unchanged. This is
device resource creation, not an order-dependent context upload operation.

All 18 tests pass in 13.19 seconds. Candidate
`edf2027-native-resource-loads.exe`, SHA256
`FC137BDD770936C05F8035DFCCBC1E4D9123FE12F8C067E5CC4EE5CE6832CD01`,
started PID28720 at 05:39:47 in `binding-validation-20260910-053947-20aa243b`.
Same hidden launcher/settings/input schedule; runtime validation pending below.

### Native-resource-creation checkpoint

The resource-loads candidate reached the same 694 textures, 6980 upload2D calls,
21 shader registrations and 65 entries. All captured non-nested texture phases:

| Phase | CPU wall ms |
| --- | ---: |
| Original loader | 349.2932 |
| Native creation | 143.1605 |
| Snapshot | 25.7203 |
| Registry lock | 0.0289 |
| Sum | 518.2029 |

Compared with 8973.4370ms for the metadata-only candidate, this removes the
remaining measured submission-barrier wait from ImportTexture. Shader
registration remains ~975.0551ms and has not been optimized by this change.
These are texture-phase totals across threads, not a 0.52-second total mission
load or visible-machine performance promise. File I/O, model loading and game
script timing remain separate. The run uses instrumented hidden presentation.
`loading.png` at 05:41:20 actually shows the mothership intro; gameplay regression
still pending at this checkpoint.

At 05:42:42, `second-load-check.png` shows actual gameplay after the second
loading stage: player displaced from spawn, allies/city/radar/AF14 HUD present,
111/120 ammunition during scripted movement/fire. Ordinary output is valid and
direct_scene stable at 1961. No texture/direct-resolve/frame-publication errors
were found in the retained logs. Stopped exact owned PID28720 after this bounded
gameplay regression. This does not certify all missions, sustained visible FPS,
original-console image fidelity or an end-to-end instantaneous load.

## Model resource bookkeeping separated from refresh pacing

Audited PublishNativeModelBuffer: heap/address validation, draining completed
write notifications, registry/cache invalidation and NativeIndexBuffer creation
via ID3D11Device::CreateBuffer with immutable initial data. It issues no native
immediate-context commands. RetireNativeModelBuffer and pool-release 821D3DC8's
native section likewise perform only cache/registry retirement.

Removed the submission-lock acquisition from these three sections, retaining
the registry mutex throughout each transaction. Write notifications still drain
before publication; generation/address reuse, alias invalidation, retained
storage and live source comparisons are unchanged. Guest allocation/copy/cleanup
calls still run, and command-submission pacing remains unchanged. Updated the
RetainIndexStorage locking comment to name the registry lock actually protecting
these operations. This is not removal of byte validation.

Build and all 18 tests pass in 12.21 seconds. The suites cover model publication,
retirement, allocation ownership and alias invalidation, but not exhaustive
concurrent game-lifetime behavior. Candidate `edf2027-native-model-loads.exe`
SHA256 `97DD21777C77C02668EBC17491E1E8EC605A7E41093E7DA1543A7FF0C05DF920`.

The diagnostic launcher refused to start because a game was already running.
Read-only inspection found PID51920 using this candidate, started 05:44:39.
This process was not launched by the current diagnostic attempt; left it alone.
No scripted runtime validation or measured model-loading speedup is claimed yet.

### Owned model-loading candidate runtime verification

After the non-owned process exited, launched unchanged candidate as owned
PID51408 at 05:46:00 using LoadingTrace, LoadTimings, IntegerPostCenters and
native-movement-fire-input.txt. Run directory:
`binding-validation-20260910-054600-5e036220`.

The sampled direct-scene intervals were 05:47:04.226 to 05:47:12.317 and
05:47:55.709 to 05:48:03.866: approximately eight seconds each. These are
once-per-second frame-route samples, not exact mission loading boundaries.
The model bookkeeping change does not establish instantaneous loading or a
separately measured model-loading speedup.

Across the retained full-run records, 1192 texture calls account for 645.096ms
original loader, 213.3912ms native creation, 51.4232ms snapshot and 0.0460ms
registry lock (909.9564ms non-nested total). Forty shader registrations account
for 1986.0216ms inclusive time. These span multiple stages and cannot be
subtracted directly from either eight-second interval.

Viewed `gameplay-check.png`: actual city gameplay, player, allies/enemies,
radar and AF14 HUD with 100/120 ammunition after scripted movement/fire.
Retained logs contain no `[error]` or `failed` matches; indexed upload reports
zero errors and vertex/index mismatches. Stopped exact owned PID51408 after
verification. Hidden instrumented presentation is not a visible FPS benchmark.

Next diagnostic target is exact load lifecycle timing, including resource
workers and readiness waits. Analysis note
`0657-resource-workers-follow-desired-actual-ready-lifecycle.md` identifies
manager desired/actual/ready bytes and persistent versus one-shot workers;
these are research leads, not yet a measured cause of the remaining delay.

## Resource worker boundary measurements

Added opt-in `edf_native_load_timings` hooks preserving all original calls:
821A4BA0 coordinator phase, 821A5080 persistent helper phase, 821A4FC0
one-shot registered-record walk, and 821A4DE8 actual/desired transition edges.
The one-shot logs begin/end with manager identity; transitions snapshot the
two fields before calling the original. No scheduling, readiness or resource
ownership behavior changes. Updated the timing summarizer's nesting warning.

Candidate `edf2027-native-load-workers.exe`, SHA256
`1F277623979AB4D8B75628923B88C3414647E3CA49DB08BFD2318B3D9B34D038`.
Build and all 18 tests passed in 15.04s. Owned diagnostic PID52160 launched
05:53:24 with the same loading flags and scripted input, run directory
`binding-validation-20260910-055324-e28e9250`.

For manager 0x40019d40:

| Boundary | First mission load | Second mission load |
| --- | ---: | ---: |
| One-shot begin | 05:54:29.108 | 05:55:19.920 |
| One-shot inclusive duration | 7588.9773ms | 8283.6032ms |
| Stop transition inclusive duration | 13.8342ms | 6.9287ms |

The first load's 05:54:29..37 completion window contains 466 texture calls:
256.4561ms original, 79.7971ms creation, 20.0801ms snapshot, 0.1008ms lock;
17 shader registrations total 803.6033ms inclusive. The second load's
05:55:19..29 window contains 506 texture calls: 288.8118ms original,
73.3358ms creation, 25.8991ms snapshot, 0.0380ms lock; 19 shader registrations
total 1028.1908ms inclusive. Windows are not exclusive-thread critical paths.

This rules against a multi-second final one-shot join in these two loads.
It does NOT prove that the entire one-shot duration is active asset work:
821A4FC0 invokes owner virtual slots and registered-record slot 6, which can
contain internal waits. Their identities and internal work/waits are the next
profiling boundary. Persistent helper/coordinator durations overlap other work
and are not additive loading costs.

Viewed `check.png` showing gameplay and `gameplay-fire.png` showing displacement,
firing/ejected casings and AF14 87/120. No error/failed/snapshot-unavailable
matches in retained logs. Stopped exact owned PID52160 after bounded validation.
No claim of instant loading, visible FPS or complete Xenos replacement.

## Callback identity correction: one-shot is the loading compositor

Added bounded, opt-in callback identity snapshots before 821A4FC0. They use
validated guest reads, preserve the original traversal, and do not invoke or
replace callbacks. Snapshot failures are diagnostic only. The list owner is
r4, sentinel at owner+2232, node object at +12 and callback at vtable+24.

Candidate `edf2027-native-load-callbacks.exe`, SHA256
`511BF79660F7AA139FECFC12C1D13CEAD228D1B854E5F682975877BA3CF59ECB`.
All 18 tests passed in 12.13s. Owned PID13896 launched 05:58:22, run directory
`binding-validation-20260910-055822-aa39dc50`, same diagnostic settings.

Menu and both mission-load snapshots identify four registered objects. Three
use 8252B718, whose generated implementation immediately returns. The remaining
object uses 8216EBC0 (vtable 820118D4). Inspection of that function establishes
that it initializes loading UI, calls 821A3B70 to set manager+2263, then loops
while actual or desired remain nonzero. The loop renders UI and calls
8219C840/8219C1F8. Owner callbacks 821BEA78/821BEA88 forward to graphics-context
thread ownership helpers 821397B8/821397F8.

Consequently, the previous 7.59/8.28-second one-shot measurements describe the
loading compositor's lifetime, NOT the asset loader's processing duration.
The short final join conclusion remains valid, but optimizing this loop alone
would not address asset load latency. In this concrete path ready+2263 means
the loading compositor is initialized, not that all mission assets are ready.

Recovered next boundaries: 821A4170 sets desired and mode; 821A41E8 clears
desired. Mission BeginLoading 820C8DD0 calls start with mode 1 and waits for
ready on subsequent VM ticks; menu 820C5C18 uses mode 0. Profile the script/
resource work between those boundaries on its actual executing thread rather
than treating the loading-screen worker as the asset worker.

Viewed `gameplay-check.png`: displaced player, city/NPCs/radar and AF14 41/120
after scripted movement/fire. No error/failed/snapshot-unavailable/truncated
matches in retained logs. Stopped exact owned PID13896 after bounded runtime
verification. Callback snapshots are identities, not per-callback timings.

## Script timing isolates LoadMap; shader registration no longer waits on submission

Removed RegisterShaders' submission lock after auditing CompileNativeShader and
ShaderBindings construction: compilation/reflection and ID3D11Device resource
creation only, no immediate-context commands. Registry mutex, replacement
invalidation and original guest shader construction remain. ForgetOwner still
has a separate submission lock; shader lifetime work is not fully decoupled.

Added opt-in menu 820C7220 and mission 820D1518 dispatch timers preserving
original calls and capturing mode r6/index r7 before dispatch. Logs include only
calls >=1ms and report inclusive wall time. Added start/armed and finish/disarmed
markers around 821A4170/821A41E8, with manager, mode and original caller.

Candidate `edf2027-native-script-loads.exe`, SHA256
`E1DCCA3134CFDD3456781944A960D987A97DACFFE74F34B73054E31AD7A095B4`.
All 18 tests passed in 14.55s. Owned PID13676 launched 06:03:57 in
`binding-validation-20260910-060357-d782acdd` with the same diagnostic settings.

Both mission stages execute asset calls on thread 42448, separate from their
loading compositor thread. First stage: native100 LoadMap 7347.2544ms,
native101 weather 154.8577ms, two native220 object calls 757.5854/114.3834ms.
Second stage: LoadMap 9307.9298ms, weather 109.9849ms, player native200
553.2564ms, major native220 calls 762.4618/115.7707ms, native301 471.3958ms.
Thus LoadMap dominates the captured loading work; these are measured function
durations, not an assertion of I/O versus CPU versus lock breakdown.

First stage 06:05:01..10 has 17 shader registrations, registry lock total
0.0036ms, inclusive registration 847.1305ms. The old submission wait at this
site is removed, but no total loading speedup is established by this run.

Native100 resolves through 820D1CE4 to 820CBD28. Internal calls include
821B8090, 820BE628, 820B4D58, 820B3B00 and 821A6158, alongside string and
allocation helpers. Next profile these boundaries to locate the multi-second
map work and any remaining guest graphics-resource construction.

Viewed gameplay-check.png (mission entry) and gameplay-fire.png (displaced
player, muzzle flash/casings and AF14 4/120). No error/failed/snapshot-unavailable
matches in retained logs. Stopped exact owned PID13676 after validation.
No visible FPS, full image-fidelity or instant-loading claim.

## LoadMap internal boundary profile

Added thread-local, exception-safe LoadMap scope at 820CBD28 and opt-in timing
hooks for its major direct callees plus 821B7278/821B2850. Original calls and
register effects are preserved. Timings use the existing >=1ms filter, include
nested work, and omit work on other threads; absent output is not a zero-cost
or not-called certificate.

Candidate `edf2027-native-map-loads.exe`, SHA256
`C88D232572C1B9DF0FB2C7B0192575BE843FB3BBADCB21AD92A381527AD082F9`.
All 18 tests passed in 14.43s. Owned PID62980 launched 06:09:35, directory
`binding-validation-20260910-060935-818c81a4`.

| Captured phase | First load ms | Second load ms |
| --- | ---: | ---: |
| Mission native100 LoadMap | 6699.2314 | 6407.2576 |
| 820B4D58 map-object manager | 6699.1349 | 6407.1614 |
| Nested 821A6158 resource flush | 60.5455 | 59.3617 |

Analysis note assets/0302 and generated code identify 820B4D58 as the map-object
manager Open operation, loading the .xRes bundle and constructing objects from
embedded SGO images through 821A6278. The measurements isolate the containing
operation, not yet its expensive child or whether that child is CPU/I/O/wait.

Viewed gameplay-check.png: player displaced down the road, allies/city/radar,
AF14 reload after scripted fire. No error/failed/snapshot-unavailable matches.
Stopped exact owned PID62980. A subsequent diagnostic build ran after both
measured map calls completed, overlapping gameplay only; no FPS claim.

Expanded the map scope to 821ACBD0, 820ADD28, 821B24C0, 820B77A0, 820B6458,
820B6FA8, 821A6278, 820B4858, 821C7B20, 821C7D80 and 821C7E30. Candidate
`edf2027-native-map-stages.exe`, SHA256
`3034416D3D1CF1D6F9DF2801DC87B4A20C81AF66BDC136F36A30E411D574A4F5`,
all 18 tests passed in 15.61s. Launched owned PID41232 at 06:12:56, directory
`binding-validation-20260910-061256-c57f6e20`, same diagnostic settings.
This deeper run is pending; do not infer success from the prior candidate.

### Map-stage result: bundle dominates

Revalidated owned PID41232 live and collected both stages. 821ACBD0 consumed
5932.9173ms of the first 6522.1657ms LoadMap and 5900.7324ms of the second
6492.1181ms LoadMap, approximately 91% each. First-stage 820ADD28/821B24C0
reported 21.6300/21.5644ms (nested), 820B77A0 3.8619ms, 821A6158 59.1231ms,
and 820B4858 199.3150ms. Timings below 1ms are omitted, not proven absent.

821ACBD0 is the .xRes resource-bundle branch from map Open. It resolves through
821ACAD8, which looks up the resource and invokes 821AB708 on a miss. The
latter includes file/archive setup and multiple resource-processing callbacks.
The containing duration alone cannot distinguish I/O, conversion or waits.

Viewed gameplay-check.png: displaced player, city/NPCs/radar and AF14 20/120
after movement/fire. No error/failed/snapshot-unavailable matches in retained
logs. Stopped exact owned PID41232 after verification.

Added map-scoped opt-in timings for 821ACAD8, 821AB708, 821D6448, 821D6AE0,
821D6C20, 821AA130, 821AAB70, 821B5568, 821AB520 and 821CB6A0. No original
resource behavior replaced by this diagnostic expansion. Candidate
`edf2027-native-bundle-stages.exe`, SHA256
`418F263266B5FED9725172FEBEACB81331AFAA69AC1D48A8AE10C1FA32A0F6D8`,
all 18 tests passed in 12.78s. Launched owned PID39576 at 06:16:05 in
`binding-validation-20260910-061605-b9e21351` with the same diagnostic flags.
Runtime results pending; preserve/revalidate that handle before another launch.

### Bundle trace result and metadata publication change

Revalidated PID39576 and collected both stages. First LoadMap 7025.4667ms,
bundle 6430.3128ms; second LoadMap 6973.5858ms, bundle 6382.6593ms. In the
first stage, 228 logged calls to 821CB6A0 sum to 5633.5532ms, with many around
15-18ms. File/archive setup 821D6448 reports 514.1036ms. Nested totals must
not be added to parent durations; calls below 1ms are omitted.

821CB6A0 resolves through 821CB550 to 821D88C0, which processes mesh-resource
data through 821D85B8/821D7C18/821D7850/821D79D8/821D8770/821B3C98. Auditing
the bridge found submission locks still surrounding CPU-only material metadata
publication/retirement and declaration publication. Their native registries
copy metadata into owned/shared snapshots and never issue context commands.

Removed submission lock acquisition from PublishNativeMaterialParameters,
RetireNativeMaterialParameters and PublishNativeDeclarationContents. Retained
state.mutex throughout, original guest constructors/destructors and all byte
validation. Device binding publishers and actual submission paths unchanged.
This change is justified by ownership/command audit; the refresh-sized callback
durations are supporting evidence, not individual lock-wait measurements.

Viewed baseline gameplay-check.png: displaced player, city/enemies/radar, AF14
100/120 after scripted movement/fire. No error/failed/snapshot-unavailable
matches. Stopped exact owned PID39576 after validation.

Candidate `edf2027-native-metadata-loads.exe`, SHA256
`340E45733F811EC8B640A41B8DE2F7F4494345D4D48A17B7E38ADA294BC21937`.
All 18 tests passed in 13.64s. Launched owned PID61296 at 06:19:59, directory
`binding-validation-20260910-061959-f306f99c`, same diagnostic settings.
Runtime comparison pending: no measured speedup or gameplay verification for
this new candidate yet. Preserve and revalidate PID61296 before another launch.

### Metadata-loads measured comparison

Revalidated PID61296 live and collected both mission loading stages using the
same diagnostic setup. No concurrent build during these measurements.

| Inclusive call | Prior bundle-stages ms | Metadata-loads ms |
| --- | ---: | ---: |
| First LoadMap | 7025.4667 | 3250.0312 |
| Second LoadMap | 6973.5858 | 3214.5070 |
| First bundle | 6430.3128 | 2665.0133 |
| Second bundle | 6382.6593 | 2632.9029 |

The measured LoadMap calls are approximately 54% faster. This is an
instrumented hidden-run comparison, not total menu-to-gameplay time, a visible
FPS benchmark, or instant loading. First-stage logged 821CB6A0 calls >=1ms
fell from 228 totaling 5633.5532ms to 99 totaling 1828.3691ms. The lower count
reflects the logging threshold and cannot imply resources were omitted.
File/archive setup 821D6448 remains 519.2053ms, similar to prior 514.1036ms.

Viewed gameplay-check.png showing normal mission entry, player, city/NPCs,
radar and AF14 HUD. Scripted movement/fire verification follows below.

Viewed gameplay-fire.png: displaced player beside a building, firing/projectile
effects and AF14 34/120. Retained logs report no error/failed/snapshot-unavailable
matches; sampled shader counters include zero parameter/texture-binding errors
and missing textures. Stopped exact owned PID61296 after this bounded runtime
verification. Candidate remains available for user testing. Other missions and
original-console image fidelity are not certified by this regression.

## Shader retirement decoupled; deeper mesh-resource profile prepared

Removed ForgetOwner's submission lock after confirming that both mesh-cache
Clear implementations only erase CPU cache entries and retained COM objects;
there are no immediate-context calls. Retained state.mutex, owner erasure,
active/linked shader invalidation and cache clearing. Native users remain
serialized, and bound D3D resources retain their API references.

Added map-scoped opt-in timers for 821CB550, 821D88C0, 821DB008, 821D85B8,
821D7C18, 821D7850, 821D79D8, 821D8770 and 821B3C98 to split the remaining
mesh-resource cost. Original guest processing is unchanged.

Candidate `edf2027-native-resource-stages.exe`, SHA256
`5D3D1875E10F376FD18F843B9B9C0DD83F5BB3E4B63BB19487841F414AE16BB9`.
Build and all 18 tests pass in 12.29s. Diagnostic launcher refused because
non-owned PID6508 was running `edf2027-native-metadata-loads.exe`, started
06:24:00. Left that process untouched. No new diagnostic process was launched;
runtime verification and measured benefit of shader retirement change pending.

## Buffer-update notifications decoupled from submission pacing

Audited NotifyNativeBufferUpdate -> NativeModelBuffers::NotifyUpdateAliases /
ApplyWrites -> NativeMeshCache::Invalidate. These operations invalidate owned
CPU snapshots and cache entries, including physical aliases, and issue no GPU
commands. Removed only NotifyNativeBufferUpdate's submission lock. Registry
mutex, alias invalidation, fallback cache invalidation, original unlock hooks
821349B8/82134AD8 and live byte comparisons remain unchanged.

The existing guest-memory tests cover overlapping/nonoverlapping aliases,
unknown owners, allocation retirement and update generation behavior. All 18
tests pass in 13.05s; these tests are not a full concurrent gameplay proof.
Candidate `edf2027-native-buffer-notifications.exe`, SHA256
`B17C7B0AED67CA22457308628C5B1FE3B61FBEFB7DC7A4D7E88AEE698042F506`.
It also includes the preceding shader-retirement change and resource timers.

Revalidated non-owned PID6508 still running metadata-loads; left it untouched.
No new runtime test launched. Both new lock changes still need runtime
comparison; the last runtime-verified candidate remains metadata-loads.

## Surface creation registry decoupled from submission

Audited 8213B850's native section and CreateNativeDepthTarget: metadata
replacement, device sample-support validation, CreateTexture2D and
CreateDepthStencilView only. No clears, binds or other immediate-context work.
Removed that section's submission lock, preserving registry mutex, original
guest allocation, dimensions/format validation and the existing depth-format
limitation. Actual clear/bind/resolve/draw locks remain unchanged.

Candidate `edf2027-native-surface-ownership.exe`, SHA256
`7F91BEF63955FFC6D829F04EDC6750255B473F54B119336D62CD7A07A17B7276`.
All 18 tests passed in 12.98s, including native depth creation and selective
depth/stencil readback tests. This is not concurrent game-lifetime verification.
Revalidated non-owned PID6508 still running metadata-loads; did not stop or
replace it. Surface creation, buffer notifications and shader retirement
changes are built but still awaiting combined runtime verification.

### Model-stage interpretation for the pending profile

Cross-checked analysis assets/0329-the-dxm-header.md with current generated
821D88C0 callees. Do not treat the entire model loader as disposable GPU
emulation: the file supplies materials, hierarchy and meshes needed by gameplay.

| Profile address | Recovered responsibility / evidence |
| --- | --- |
| 821D88C0 | DXM model initialization; header version 257, arrays for materials/nodes/meshes |
| 821D85B8 | Material-array allocation/setup, including effect/material helper calls |
| 821D7C18 | Material binding setup: effect/technique lookup and parameter helpers |
| 821D7850 | Allocates and initializes 304-byte runtime node records |
| 821D79D8 | Recursive hierarchy construction; local transforms and child links |
| 821D8770 | Allocates 52-byte runtime mesh records; invokes 821CA378 and 820AC158 |

Array allocation strides were checked in generated code; node hierarchy/local
transform meaning is also supported by analysis assets/0588 and 0589. Those
notes describe a separate native analysis implementation, not code already
integrated into this port. Use the pending runtime stage timings to choose the
graphics-resource boundary to replace; preserve animation/object transform
state while migrating its ownership rather than simply skipping the loader.

Non-owned PID6508 remained live during this read-only audit. No additional
runtime changes or game launches were made in this audit pass.
