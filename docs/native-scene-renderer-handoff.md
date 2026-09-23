# Native scene renderer: handoff

Last updated: 2026-09-22. This records implementation progress, not a declaration of completion. Revalidate the worktree and process state before continuing.

*Corrected 2026-09-23: this handoff predates the full-frame renderer and the
`-O2` build fix. For the current state, measurements and gaps, read
[renderer-status.md](renderer-status.md). The `win-amd64-release` tree built
at `-O0` until `04ad5e2`, and when that started is not recorded, so FPS and
timing figures below may be unoptimized.*

## September 22 implementation resumed

The user expanded the Ghidra/Epistemic goal to resume renderer implementation
when supported by sufficient evidence. The inventory is at247 raw-reviewed
bodies/15,140 instructions, with165/186 explicit sites partially reviewed.
Unclosed callbacks and transitive ownership are not blockers for unrelated,
well-supported implementation boundaries.

Shader binding correction in `native_shader_binding.h`: retirement and
binding/dirty writes now precede defaults decoding, pixel dirty bits retain
their two-store sequence, and absent defaults do not write the constant mask.
An aliased-retirement fixture in `native_shader_binding_tests.cpp` failed before
the change and passed afterward. Payload snapshot/application aliasing and
malformed-input parity remain open; do not describe this as full shader parity.

Baseline runtime with owned material/activation/geometry/instance/pass/view/
camera/source/membership/selection paths reached440,000 published geometry
bypasses,37million audited instance reads and412/412 ready preload groups.
The scanned logs contain no error/critical or nonzero mismatch samples; reported
queue fallback counters are zero. The230-second capture shows city, player,
NPCs and HUD. Process17220 was stopped after executable-path verification.
Artifacts: `out/renderer-inventory/resumed-static-pass-results.json` and
`resumed-static-pass.bmp`. This ran the pre-correction executable. Baseline
D3D11/D3D12 scene tests and hook audits pass; changed shader unit tests pass.
The post-correction full build is tracked by
`out/renderer-inventory/renderer-shader-order-build.log`.

## September 21 continuation

Spatial publication continuation: `edf_native_scene_membership_owned` (default
false) attaches immutable list membership and retained hierarchy images to the
same scene publication as source data/assets. Native list traversal consumes
published headers/members; audit mode compares complete live lists. A global
membership revision invalidates selection after any tracked list mutation;
missing/stale lists use a logged fallback. Tree readers retain their epoch
check and live fallback after mutations. Tests cover cross-list moves, retirement,
address reuse, unchanged sharing and retained hierarchy sets. Scene and frame/tree
tests pass; full game build/runtime pending. Live hidden/duplicate flags and
unknown callbacks remain, so this does not yet establish independent selection.

Immutable selection continuation: `edf_native_scene_selection_owned` (default
false) resolves native source identities from `NativeScenePublication::by_source`.
`Resolve` retains the published native identity/geometry and applies the pass's
resolved material and exact world sample without mutating or querying the current
scene database. Missing lifetimes/geometry use the existing fallback until a
publication contains them. First and subsequent eligible direct draws use this
path. Tests render after producer retirement and verify retained publication
immutability and lifetime rejection on D3D11/D3D12. Full build/runtime pending.

Source publication continuation: `edf_native_scene_sources_owned` (default
false) attaches one immutable `NativeSceneSources` generation to each scene
publication. Static LOD selection, visibility, material membership checks and
direct world draws select that same generation. Producer events invalidate
the shared snapshot; unchanged events retain it. Old generations preserve
world/visibility values, metadata and membership through removal/address reuse.
Existing visibility/metadata/world audits remain, including an added exact
world check on the first published draw. Tests and full build pass. Runtime
reached 42.1 million published-source reads and 7,847,890 transform checks with
zero mismatches. Error/critical/nonzero mismatch scans were empty; the 320-second
screenshot shows city/player/effects/HUD. Process stopped with path verification.
Final samples: `out/renderer-published-sources-final-samples.txt`.
Unknown callbacks, live hidden/duplicate flags and asset freshness checks still
remain; this is not yet an entirely independent static pass.

Camera publication continuation: `edf_native_scene_camera_owned` (default false)
publishes the complete active-view camera set after `821A4DE8` finishes producer
updates. Render entry acquires an immutable generation; scene begin selects its
projection/view/view-projection matrices without reading the live scene.
Transform audit compares all matrix words against the scene-begin oracle.
Missing views use a logged fallback. Replacing the complete set retires removed
views while preserving acquired generations. This still uses the original
camera matrix producer and does not establish independent camera interpolation.
Camera generation/removal tests and full game build pass. Runtime recorded
over 2,000 audited camera reads, 5.3 million transform checks with zero mismatches
and 800,000 owned pass-view reads. No error/critical, nonzero mismatch, or camera
fallback entries were found. The 320-second screenshot shows the city/player,
smoke, and Stingray M1 reload HUD; input provenance was not controlled by this
test, so this is not a controlled movement/firing acceptance result. Process
stopped with path verification; `out/renderer-published-camera-results.txt`.

Pass-view continuation: `edf_native_scene_view_owned` (default false) carries
raw viewport/scissor words and native targets in the native pass cursor. Eligible
deferred-material groups consume that selection; render-state audit compares
every word and target field against current state. Unsupported groups invalidate
the cursor. Full game build and scene tests passed. Runtime reached 800,000
owned pass-view reads and five million transform checks without mismatches.
No error/critical or nonzero mismatch entries were found; final contract
counters were zero. The 320-second screenshot shows city/player/effects/HUD.
Process stopped with path verification. Final samples:
`out/renderer-owned-pass-view-final-samples.txt`.

Material handoff continuation: `edf_native_scene_material_deferred` (default
false, requires deferred geometry) activates native bindings directly from the
published program before drawing, without running CPU material activation.
Only that explicit pending state bypasses the legacy CPU shader-binding check.
At handoff, geometry is installed, CPU material activation runs, the final
world is restored, and the indexed tail consumes dirty state. The replay hook
skips redundant native binding uploads. Materials containing scissor enable
operations stay on the old path because rectangle geometry is not yet owned.
Tests cover activation/world ordering, retry and scissor exclusion; full build
and D3D11/D3D12 tests passed. Combined geometry/material runtime reached 2.2
million handoffs and 15.7 million transform checks with zero mismatches. No
error/critical or nonzero mismatch entries were found in the logs, and final
contract rejection/omission/error counters were zero. The 320-second screenshot
shows city/player/effects/HUD. Process stopped with executable-path verification;
final samples: `out/renderer-material-handoff-final-samples.txt`.

Geometry handoff continuation: `edf_native_scene_geometry_deferred` (default
false, requires a native queue and owned geometry/material activation) defers
stream/declaration/index installation. The retained geometry path validates
the owned setup against the preload source instead of current CPU bindings.
World-only native draws use published material/geometry without waiting for
CPU dirty-state consumption. Before a fallback or group exit, the group is
submitted, then compatibility bindings are installed and the indexed CPU tail
runs once if native draws were accepted. The final world value is synchronized.
Non-world overrides force the handoff before their callback. Material CPU
activation still runs before native submission; compatibility work is deferred,
not eliminated. Ordering/fallback/retry tests, full build and D3D11/D3D12 tests
passed. The extended geometry-only run reached 10.1 million handoffs and 73
million transform checks without mismatches; 8.844 million tree checks also
matched and contract counters remained zero. The 320-second screenshot shows
city/player/effects/HUD. That process was stopped with path verification.
Final samples are in `out/renderer-geometry-handoff-final-samples.txt`.

Inherited pass-state continuation: `NativeSceneMaterialPassState::After` resolves
render and all sixteen sampler slots without mutating the incoming value.
`edf_native_scene_pass_owned` (default false) scopes a cursor to native group
traversal. Fully native groups advance it from the published material program;
unsupported activation/draws invalidate it, and the next eligible group imports
fresh pass state. The CPU setup mirrors still execute. State/sampler audit
compares outgoing render state and all sampler controls against those mirrors
before committing. The first integration run stopped on a raw sampler-record
mismatch with matching render state. Device records also contain texture
format/address bits, owned separately by retained textures. The revised pass
uses `NativeMaterialSamplerInputs`: the semantic sampler key plus the low two
mip-alias bookkeeping bits and all ancillary controls. Tests ensure descriptor
bits are excluded while alias and LOD changes still fail equality. The corrected
build/tests passed. The rerun completed over 320 seconds with 700,000 inherited
pass-input reads, 4.7 million transform checks and over six million visibility
checks without mismatches. `renderer-inherited-pass.bmp` shows city/player/HUD;
the process was stopped with executable-path verification. Failure evidence:
`out/renderer-inherited-pass-failure.txt`.
Tests cover inherited disabled blend factors, untouched slots/LOD and failed
transitions. D3D11/D3D12 tests and `renderer-inherited-pass-build.log` passed.
Runtime results are in `out/renderer-inherited-pass-results.txt`. A seeded
sequence test additionally checks 4,096 sampler operations with and without
descriptor projection (`renderer-sampler-projection-tests.log`).

Instance metadata continuation: `ReadNativeStaticSceneParts` now captures the
register index for an instance with exactly one four-register override pointing
at its owner's world storage. `edf_native_scene_instance_owned` (default false)
uses that lifecycle-owned metadata in static eligibility and subsequent direct
draws. Other overrides keep the live path. Transform-audit mode compares the
entire live list and reports any missing source event as an error. Tests cover
changed register indices, extra overrides and foreign storage. D3D11/D3D12
tests and `out/renderer-owned-instance-build.log` passed. Extended gameplay
reached 19.7 million audited metadata reads and 6.2 million transform checks
without logged mismatches. `renderer-owned-instance.bmp` shows city/player/
effects/HUD. That process was stopped with path verification before relinking.

Geometry setup continuation: `NativeSceneGroupGeometry` now retains optional
`NativeSceneGeometrySource` inputs alongside the draw. Preload publishes the
buffer/declaration/stride/count/material identities; setup uses these only
when the frame's geometry record is still the current record for the source
revision/backend. Other groups retain the original descriptor-reading path.
CPU buffer/declaration mirrors remain. Tests verify publication sharing and
replacement when the material identity changes, without mutating old frames.
`out/renderer-published-setup-tests.log` passed on D3D11/D3D12 and the full
`renderer-published-setup-build.log` build exited 0. Extended gameplay reached
one million owned setup groups/activations, 6.5 million transform checks,
7.1 million sampler checks and 3.1 million state checks with zero mismatches.
The 320-second `renderer-published-setup.bmp` shows city/player/effects/HUD.
That process was stopped with path verification before rebuilding.
Shader-link validation and published register plans are now cached per shader
and program lifetime instead of rebuilding on every activation.

Latest activation continuation: `edf_native_scene_activation_owned` (default
false, requires material ownership and material audit disabled) now activates
queued rigid groups from published constants/textures and explicit sampler
pass inputs. `ObservePublishedActivation` validates group revision, current
program/backend and shader generations, then refreshes the shared normal and
reversed bindings and register metadata. This preserves mixed-pass handoff;
the CPU material-activation mirror and CPU geometry setup still remain.
`NativeSceneMaterialProgram::ApplyBindings` is shared with material capture.
Tests cover stale constants, clearing omitted resources and restoring them;
the full build and D3D11/D3D12 scene tests passed. Runtime integration reached
one million published activations, 1.04 million geometry bypasses and 6.7
million transform checks with zero mismatches. The 320-second image
`out/renderer-published-activation.bmp` shows city/player/effects/HUD. That run
was stopped with executable-path validation before rebuilding.

The preceding combined retained-geometry run completed over 390 seconds:
1.15 million geometry bypass draws, 7.5 million transform checks with zero
mismatches, 6,629,548 owned tree reads and 861,743 tree checks with zero
mismatches. Explicit NaN parity logs confirmed matching register bits.
`out/renderer-native-world-registers.bmp` shows city, player, effects and HUD;
results are in `out/renderer-native-world-registers-results.txt`.

Latest continuation (supersedes older pending verification below): native
shader/default binding and render-state publication reached 1.36 million
material comparisons with zero mismatches/missing inputs; the gameplay image
was inspected (`out/renderer-state-publication-shader.bmp`). Native texture
binding now handles all 26 device slots. Its first integration exited during
initialization with the earlier 16-slot limit; the original initializer at
`82139A2C` clears 26 slots. The corrected build passed binding tests and reached
560,000 material comparisons, 4.8 million sampler checks and 2 million state
checks with zero mismatches. `out/renderer-native-texture-slots.bmp` shows city,
player, NPCs and HUD; results are in the matching `-results.txt`.

`edf_native_frame_dispatch` (default false) now selects native outer phase
orchestration in `native_frame_dispatch.h` instead of calling the original
`821A5080`. This still invokes the existing world, draw-bucket, overlay and
presentation callbacks: it does **not** establish independent rendering or
independent cadence. Phase-order, disabled-view and callback list-mutation
tests pass, as do the full build, D3D11/D3D12 scene tests and hook fingerprints.
First outer-dispatch gameplay verification reached 560,000 material comparisons
with zero mismatches; `out/renderer-native-frame-dispatch.bmp` was inspected and
shows city/player/NPC/HUD. That process was stopped before relinking.
The next build also replaces `821A3BA0` bucket redistribution/traversal, preserving
reversed insertion, descending buckets 255..1 and excluded bucket zero. These
ordering tests, the game build, scene tests and hook fingerprints pass. Combined
outer-and-bucket gameplay verification reached 6,000 native frames and 510,000
material comparisons with zero mismatches; the matching BMP shows gameplay.
That process was stopped before the tree build.

`edf_native_scene_tree` (default false) replaces root and recursive tree
traversal with native code. `native_scene_tree.h` preserves occupancy filtering,
accepted subtrees, child order, counters and post-callback root-range checks.
The AABB transform preserves `821B0258` arithmetic order separately from the
existing vec4 transform. Debug-tree callbacks retain the original path.
Unit tests and full build/scene tests pass. Tree runtime verification recorded
332,740 tree classifications and 4,377,254 leaf-visibility checks with zero
mismatches. Material comparisons: 489,999 exact matches, zero unexplained or
missing inputs, one separately counted animation-generation difference.
`out/renderer-native-tree.bmp` shows city/player/NPC/HUD; that process was stopped.
The latest source also moves `820B4310` world-phase order and `821C3BB8`
group-list traversal to native code. These additions pass unit tests, full
build, D3D11/D3D12 scene tests and hook audit. Their runtime verification is
running from `out/renderer-native-world-groups-process.json`; inspect its log
and scheduled BMP before accepting the combined path.
Later continuation: that process was stopped, and its 230-second BMP showed
gameplay. The longer run **failed the leaf-visibility audit**: mismatches began
around 294 seconds and reached 212 by 7,078,938 checks. Tree classification
still had 818,904 checks with zero mismatches; materials had 859,997 exact
matches, zero unexplained/missing inputs and three generation differences.
The screenshot predates the failure and does not establish acceptance.

`native_scene_tree_publication.h` now copies root metadata, occupied-node
bounds/children and empty-node occupancy into immutable regions. Publication
occurs after the world producer and at the simulation publication boundary.
Rebuild/update/node/list hooks invalidate an epoch; destruction retires owners.
The reader uses a current snapshot or falls back to live storage and can audit
every copied read. Tests cover copy isolation, audit detection, invalidation,
republication, retirement and cyclic topology. The build, scene tests and hook
audit pass. Runtime acceptance and actual owned-read coverage are pending.
Current run: `out/renderer-native-tree-publication-process.json`, with
`edf_native_scene_tree_published=true`; its screenshot is scheduled at 320
seconds to cover the later failure. New diagnostics distinguish visibility
source, transform and classification mismatches. Poll this exact process.
Subsequent diagnostic result: the broad-invalidation run used zero owned tree
reads, so publication coverage was not established. Its later transform
mismatch diagnostics showed NaNs on both sides. Ordinary float equality was
replaced with exact register-bit comparison; NaN payload differences still
fail, and regression tests cover that distinction. List invalidation now
checks tracked hierarchy membership; unrelated callbacks rely on the explicit
hierarchy writer hooks rather than invalidating every image unconditionally.
These fixes pass build, unit/scene tests and hook audit. The diagnostic process
was stopped. The **current** run is
`out/renderer-native-tree-publication-fixed-process.json`, with a 320-second
capture. Verify positive owned-read coverage and exact-bit audit results
before accepting either fix. The earlier 320-second image showed gameplay.
Latest result: the narrowed-invalidation run exercised 6,398,640 published
tree reads over 4,000 traversals, with 836,915 classifier checks and 7,770,433
leaf checks reporting zero mismatches. Remaining live tree-reader accesses
matched the mutable manager counter reads. Material comparisons ended at
959,838 exact matches, zero unexplained/missing inputs and 162 separately
counted animation-generation differences. Its 320-second BMP was inspected;
that process is stopped. An explicit NaN-match counter is in the next binary.

Published material definitions now own vertex register ranges as well as
constant bytes. Native group world setup uses that metadata, rejecting
ambiguous/overlapping/out-of-bank world ranges, instead of depending on the
last activation's register metadata. The change passes D3D11/D3D12 scene tests,
the full build and hook audit. Current combined retained-geometry/material run:
`out/renderer-native-world-registers-process.json`, 320-second capture. Material
comparison audit is disabled to exercise the retained-geometry bypass;
transform, visibility, state and sampler audits remain enabled. Check bypass
coverage, explicit NaN parity, errors and the screenshot. ObserveActivation and
CPU setup mirrors still remain; this is not an independent complete pass.
Hierarchy data still comes from live CPU storage, and leaf/material callbacks
remain. An immutable hierarchy publication and a complete independent pass
are still required.

Latest verification: `out/renderer-world-pass-generation-build.log` and
`out/renderer-world-pass-generation-tests.log` pass, including the pending
published-geometry draw bypass. Hook fingerprints and `git diff --check` pass.
The opt-in `edf_native_scene_geometry_owned` path validates retained geometry
and resource versions, resolves the published material, queues the first group
instance, and retains the indexed CPU tail without indexed mesh acquisition.
It requires owned materials and audit disabled; gameplay acceptance is pending.

The animation diagnostic (`renderer-animation-generation-audit-process.json`)
showed selected counter 3636 versus latest 3637, with selected water zero and
latest/visible water `0x3ada740e`. That process was stopped after path validation.
Animation publication now occurs after the producer callback completes, and
selection occurs at world-pass entry instead of relying on outer helper entry.
The new audited gameplay run is tracked by
`out/renderer-world-pass-generation-process.json`; poll that exact handle.
Do not claim animation equivalence until the new run's comparisons pass.

That world-pass run subsequently reached 579,878 exact comparisons, 122
mismatches and zero missing inputs (`out/renderer-world-pass-generation-results.txt`).
Sampled failures still showed the producer advancing after pass selection.
The process was stopped with executable-path validation. The audit now records
`animation_generation_differences` separately: only a full material rebuild
using the newer published animation inputs that exactly matches the captured
material qualifies. No other constants are changed, and the rendered material
keeps its original pass snapshot. These cases do not count as exact matches.
Build and D3D11/D3D12 tests pass in
`out/renderer-generation-separated-audit-{build,tests}.log`.
The current run is `out/renderer-generation-separated-audit-process.json`, with
a 230-second capture at `out/renderer-generation-separated-audit.bmp`.
Gameplay results for this classification and the geometry bypass remain pending.

The generation-separated audit subsequently exceeded 959,000 exact material
matches with zero unexplained mismatches, missing inputs, or material rejections.
Generation differences remained separately counted (119 at 959,881 exact
matches). Final counters are in `out/renderer-generation-separated-audit-results.txt`.
The scheduled BMP was inspected and shows the city, player, NPCs and HUD;
this is a visual sanity check, not matched-frame fidelity proof. The owned
process was stopped. The next live run is
`out/renderer-owned-geometry-process.json`, with owned materials, audit disabled,
and `edf_native_scene_geometry_owned=true`. Check both bypass counters and
`out/renderer-owned-geometry.bmp` after its scheduled 230-second capture.

That run reached 980,000 live-binding bypasses with zero material rejections;
its BMP was inspected (city, player, NPCs, HUD). Geometry bypass successes were
absent. Bounded diagnostics in the next run reported `observed resource versions
unavailable`. The bypass had passed a zero-initialized `SnapshotPolicy`, whose
zero verification interval is explicitly rejected by `TryValidateObservedSet`.
It now uses the same configured interval/initial count and audit controls as
preload and regular indexed draws. Scheduled verification still uses fallback.
The corrected build and D3D11/D3D12 tests pass in
`out/renderer-geometry-policy-{build,tests}.log`. Current runtime verification:
`out/renderer-geometry-policy-process.json`, capture at 230 seconds to
`out/renderer-geometry-policy.bmp`. Geometry bypass acceptance remains pending.

The corrected geometry-policy run reached 840,000 successful bypasses;
`out/renderer-geometry-policy-results.txt` preserves the counters and the BMP
was inspected (city, player, NPCs, HUD). It was stopped after verification.
The next change moves the accepted first-instance submission before
`sub_821D9600`, whose audited body only uploads the instance parameter list.
The world-only predicate remains required, and the final owned world matrix is
synchronized before the next guest callback using the existing pending-world
mechanism. The indexed CPU tail remains. Build, D3D11/D3D12 scene tests, hook
audit and diff checks pass; runtime validation is tracked by
`out/renderer-first-instance-bypass-process.json`, with its 230-second BMP at
`out/renderer-first-instance-bypass.bmp`. This newer change is not yet accepted.

The first-instance bypass run subsequently reached 680,000 successful draws;
its capture was inspected and shows city/player/NPC/HUD. Counters are in
`out/renderer-first-instance-bypass-results.txt`. The process was stopped.
The native shader-bridge path now also implements declaration setter
`82149A90` directly: declaration at device+11536 and bit 51 in device+16,
preserving its volatile r11/r12 results. Its original body is fingerprinted in
the hook audit. Build and scene tests pass in
`out/renderer-native-declaration-{build,tests}.log`; hook/diff checks pass.
This final setter change has not yet had a gameplay run. No validation game or
build process remains active. Remaining group material activation still calls
`821B94E8`/`821B8E48`, and the outer original renderer helper is still required.

Opt-in `edf_native_material_activation` now replaces the original activation
traversal with bounded native operation lists and native sampler resolution.
Its first gameplay audit reached 2.4 million activations/state checks, 6 million
sampler checks and 690,000 material comparisons with zero reported mismatches;
`out/renderer-native-activation-results.txt` contains the counters. Its BMP was
inspected and shows city/player/NPC/HUD. Lower CPU setters remained in that run.
The next change replaces disjoint, aligned constant uploads with native byte
copies plus the original stage dirty mask and stack scratch stores. Aliased
uploads retain original vector ordering. Build, scene tests and hook checks pass
in `out/renderer-native-activation-constants-{build,tests}.log`. Current runtime
audit: `out/renderer-native-activation-constants-process.json`, capture after
230 seconds to `out/renderer-native-activation-constants.bmp`. Shader binding,
texture retirement, render-state setters, `ObserveActivation`, and the outer
helper remain dependencies. The activation feature defaults off.

The global-constant extent blocker below is repaired. Global inputs now use
native reflection bounded by live vector capacity; locals require exact native
binding extent, including when reconstructing either vertex variant. The new
global-capacity regression failed on the old decoder and passes with the fix.
The full game build, D3D11/D3D12 scene tests and hook-boundary audit pass.
Evidence: `out/renderer-material-{regression,fixed}-{build,tests}.log`.

The scripted gameplay run in `out/renderer-material-fixed-process.json` reports
**412/412 material groups ready, zero deferrals**. Material programs still rebuild
frequently (loaded operations rise while reused remains 408); this does not
establish independent rendering or useful material-program reuse. Separate
changing frame inputs from reusable shader/resource state during the remaining
material/pass integration. The 230-second Mission 1 capture
`out/renderer-material-fixed.bmp` was inspected: city, player, NPCs and HUD are
visible. This is a visual sanity check, not a matched-frame fidelity comparison.
The 199-241 second window averages 49.95 FPS with zero logged errors
(`out/renderer-material-fixed-fps.json`). No improvement or 120 FPS result is
claimed. The exact-path-validated diagnostic process was stopped after capture.

The older September 18 interruption notes and measurements below are historical.
The next architectural action is explicit native material/pass-state resolution,
followed by a complete static-world pass and independent frame scheduling.

### Native material pass resolution

`native_material_sampler.h` now owns ordered texture/sampler operations and
resolves them over explicit pass state. It retains the min/mag alias and
anisotropy controls, both LOD clamps, null-binding inheritance, and guest bias
conversion. Operations include resources optimized out of the native shader,
because those records can still affect a shared slot or subsequent material.
`--edf_native_material_sampler_audit=true` compares against original activation.
The Mission 1 run reached 17.6 million slot comparisons with zero mismatches;
see `out/renderer-sampler-audit-{process.json,results.txt,bmp}`. It was stopped
after visual inspection. This diagnostic still executes guest activation.

`native_material_render_state.h` resolves 36 audited render-state overrides,
including saved blend factors/enable flags and target-dependent depth and color
writes. Other overrides throw explicitly. `--edf_native_material_state_audit=true`
compares both effective and inherited CPU inputs with original activation. Its
Mission 1 run reached 6.7 million activations with zero mismatches; material
preload remained 412/412 with zero deferrals. See
`out/renderer-material-state-audit-{process.json,results.txt,bmp}`. The process
was stopped after capture inspection. These runs do not establish other-mission
coverage or a speedup.

`NativeSceneMaterialProgram::Resolve` now builds pipelines, instanced pipeline
variants, samplers and constant/resource bindings from explicit native pass
inputs. It returns final inherited pass state for the next material. D3D11/D3D12
scene tests compare the result with visible binding capture and render from the
resolved material. Full build and tests pass in
`out/renderer-material-resolve-{build,tests}.log`; sampler/state unit checks pass
in `out/renderer-sampler-tests.log` and `out/renderer-material-state-tests.log`.
The hook audit additionally fingerprints the three sampler setters and 36
render-state setters. Gameplay still uses its existing group setup: wire this
factory into group preparation, compare publication inputs with current pass
camera/constants, then bypass guest setup and traversal. Independent frame
scheduling and remaining content passes are still outstanding.

Binary research correction: `work/recomp/default.exe` is already a mapped
image. Read constants/tables at VA minus `0x82000000`, not PE raw-section
offsets. The bias scale (`0x82003198`, 32.0), color scale (`0x8200964c`,
`0x3b808081`) and filter table were checked. The first raw-section lookup of
the state table returned instructions and was rejected; the mapped table is
retained in `out/renderer-material-state-table-mapped.txt` and Epistemic.

### Material publication timing

The first gameplay material-factory comparison did **not** pass: 328,112
comparisons matched and 801,888 mismatched, with zero missing programs. Reasons
include disagreement between camera matrices, published versus visible camera,
and material bindings. See `out/renderer-published-material-results.txt`.
The displayed rendering was unchanged; its Mission 1 capture was inspected and
the diagnostic process stopped. Do not replace guest setup based on preload
readiness alone.

Material definitions now exclude constants. `NativeSceneGroupMaterial` owns
constant values for its publication, while successive publications can share
the same shader/resource program. `Capture` and `Resolve` require explicit
constant spans. Tests verify changed constants preserve the old publication,
share its program, and still render the retained red material after retirement.
Full build and D3D11/D3D12 scene tests pass in
`out/renderer-material-publication-split-{build,tests}.log`.
The next diagnostic reads constants immediately before group activation and
logs the named inputs that changed since simulation publication. This is a
timing diagnostic, not the final independently published pass input source.

That run completed **1,190,000 comparisons with zero mismatches or missing
programs**. Changed inputs (excluding per-object world) were VS globals
`g_mProjection`, `g_mView`, `g_mViewTranspose`, `m_WaterTime`, and PS global
`g_SignalBrightness`. All 412 material groups remained ready. Program loads
stayed at 412 initially and reached 1,130 later, with 2,188,650 reuse events;
resource/program changes can still create generations. See
`out/renderer-material-pass-input-results.txt`. The Mission 1 capture was
inspected and PID 24988 stopped after checking its executable path. The audit
still executes original activation and displays its established output.
Next, provide camera and animated globals from explicit native pass state,
then replace group setup using the already compared material resolver.

### Native pass input construction

`native_scene_pass_inputs.h` builds camera registers from owned scene matrices:
projection/view/view-projection are transposed into guest register layout;
view-transpose is copied directly. Scene-begin snapshots these matrices before
the original callback updates global parameter storage. The camera-only audit
passed 170,000 material comparisons with zero mismatches/missing programs
(`out/renderer-native-pass-camera-results.txt`); its process was stopped before
the scheduled capture to validate the animation inputs next.

Constructor `820B6068` stores signal and water parameter nodes at owner+360 and
owner+352. Callback `820B4250` selects signal brightness from bit 6 of owner+364
(0 or 10), increments that counter, publishes owner+356 water time and advances
it by the float at `0x820022A8` (`0x3ADA740E`). Both published vectors have
remaining components `(0,0,1)`. Native animation inputs now reproduce those
registers from source values captured before the callback, retained through the
enclosing render helper because bucket consumption follows the world callback.
The original callback still advances state and traverses the world.
Unit tests cover matrix orientation, local-parameter exclusion, signal phase
boundaries and water register contents. The original five camera/animation
functions are now fingerprinted by the hook audit.
The first combined run and the longer-scope rerun both exited after their first
four successful comparisons. Instrumentation then identified the missing
native animation pass. The update callback `820B4250` and render gather callback
`820B4310` are separate vtable entries; a helper-local snapshot cannot carry
their state. Animation inputs now travel through an immutable scene publication,
selected by owner in the render callback and retired by destructor `820B5FA8`.
Old/new publication isolation and retirement tests pass; gameplay validation
of this publication change is still pending. Per-group input preparation uses
published constants and owned source snapshots without rereading material
parameter records. Original activation still runs for the comparison.

`--edf_native_scene_material_owned=true` now constructs displayed rigid-group
materials/cameras from these inputs and decodes object transforms from published
world registers. It validates world-only instance overrides and uses reflection
to decode row/column layout. With auditing enabled it compares material, camera
and world against the original capture; without auditing supported first draws
do not capture live bindings. Guest activation and `RecordDrawSetup` still run.
The initial owned-material run (`out/renderer-owned-material-process.json`)
uses the older animation transport and logs that fallback explicitly; do not
treat its successful material comparisons as complete pass coverage.
That run reached **1,090,000 constructed native materials with matching
material/camera/world comparisons**. Its screenshot was inspected (city,
player, NPCs and HUD visible), and its process was stopped. The explicit
animation fallback remains in `out/renderer-owned-material-results.txt`.
The new publication transport and rejection counters need a separate run.

That publication run removed the missing-animation error but did not fully
match: **1,018,343 comparisons matched and 1,657 material-binding comparisons
failed**. Rejected draws retained the existing path. Capture inspected and
process stopped; evidence is `out/renderer-owned-material-publication-results.txt`.
The next diagnostic names the differing constant and records both word values
(`out/renderer-material-difference-process.json`). Do not claim full animation
equivalence or disable auditing for acceptance before resolving these failures.

The non-audit owned path now bypasses `RecordDrawSetup` for native scene groups,
so it no longer binds the live shader constants/resources before native scene
rendering supplies its own state. Fallback draws still run that setup. The
`live_binding_bypasses` counter records successful owned constructions on this
route. Build and scene tests pass in
`out/renderer-material-binding-bypass-{build,tests}.log`; non-audit gameplay
validation remains pending the mismatch diagnosis.

The named diagnostic identified `m_WaterTime` and `g_SignalBrightness`.
For example native water bits `0x3b5a740e` lagged visible `0x3bda740e`;
subsequent samples lagged by one or two updates. The scene publication was
holding older animation values. Animation now has its own immutable generation,
updated by the source callback and acquired once at render-helper entry under
the producer/reader lock. Render gathering selects from that generation.
Tests verify updates between scene publications, unchanged-generation reuse,
old-generation retention and retirement. Build and D3D11/D3D12 tests pass in
`out/renderer-animation-pass-publication-{build,tests}.log`. The gameplay rerun
is `out/renderer-animation-pass-publication-process.json`; verify it before
claiming that this timing correction resolves all mismatches.

## Goal and current status

The objective is a complete native scene renderer: the game publishes scene state at simulation cadence; native code owns geometry, materials, object state, visibility and LOD; native rendering and interpolation run independently at 120 Hz. Migrate static world first, followed by animated objects, effects, shadows and UI. Remove the original render helper from displayed-frame generation.

**This goal is incomplete.** Opt-in native outer dispatch now bypasses the original helper, but nested callbacks and CPU setup mirrors remain. Standalone rendering tests, retained draw assets and opt-in preload coverage do not establish an independent gameplay renderer. The user asked for a larger architectural push after successive helper optimizations. Do not reduce the goal to another cache optimization or claim 120 FPS has been reached.

At the September 18 interruption, gameplay exposed a register-count validation bug: only **17 of 412** material groups became ready. That bug and the unbuilt edits from that interruption are covered by the September 21 continuation above.

At handoff, the last build had exited successfully and no `edf2027` process was running. The material gameplay process was deliberately stopped before its scheduled screenshot. Check again before building or launching.

## Historical blocker: global constant extent validation

File: [native_scene_material.h](../src/native_graphics/native_scene_material.h), `ReadNativeSceneMaterialInputs`.

The September 18 code incorrectly applied this check to both local and global parameters:

```cpp
if(bytes%16 || bytes>size_t(parameter.registers)*16)
  throw std::runtime_error("native scene material parameter extent mismatch: "+parameter.name);
```

Gameplay reports extent mismatches for `g_mView` and `g_mViewTranspose`. Xbox shader metadata can declare fewer registers than the compiled native shader requires, while the live global backing vector has sufficient capacity.

Use the existing bridge as the authoritative behavior: `RegisteredShader::ResolveUploads` and `UploadParameters` in [guest_shader_bridge.cpp](../src/native_graphics/guest_shader_bridge.cpp).

- Global groups (`group & 1`): copy native reflection's required bytes. Validate against the live vector's `value.available`, including its existing upper bound. Do not reject against `parameter.registers`.
- Local groups: preserve the ordinary bridge's semantics. It uploads `parameter.registers * 16`; `ShaderBindings::SetGuestFloatRegisters` requires the exact native binding extent. Do not silently truncate or pad a local mismatch.
- Preserve unused-parameter skipping and alignment checks. The preload requirements currently use the maximum of normal/reversed VS requirements; verify compatibility for both variants.
- Add a regression case where the global descriptor register count is smaller than the native requirement but backing capacity is sufficient, plus rejection when backing capacity is insufficient.
- Rebuild, run the native scene rendering tests and hook audit, then rerun gameplay to measure actual material coverage.

Latest sampled failure log:

```text
[2026-09-18 22:52:24.866] Native scene material preload:
groups=412 ready=17 loaded=42619 reused=17 deferred=990660
(owned inputs; pass state still explicit)
```

The counters are cumulative operations, not unique assets. Empty error-level logs do not make this run successful: repeated material deferrals are the failure.

## Verification ledger

All paths below are relative to the repository. Files under `out/` are local artifacts and may not survive a fresh checkout.

| Work | Evidence | Result and limits |
| --- | --- | --- |
| Latest material build | `out/native-scene-material-inputs-build.log` | Exit 0; predates edits listed below. |
| Latest material tests | `out/native-scene-material-inputs-tests.log` | D3D11/D3D12 scene rendering tests passed at that revision. |
| Latest material gameplay | `out/native-scene-material-inputs-process.json` | PID 27128 stopped early; only 17/412 material groups ready. No completed Mission 1 screenshot or FPS result. |
| Geometry preload | `out/native-scene-geometry-preload-{build.log,tests.log,process.json,summary.json,fps.json,bmp}` | 412/412 groups ready, zero deferrals; Mission 1 image visually correct. PID 43988 stopped. 55.15 FPS for game seconds 199–241. |
| Unseen-part population | `out/native-scene-group-assets-final-*` | 44,497 records created before their own first draw across scene lifetimes; examined 146,363, rejected 0; 23,289 live objects last sample. 56.75 FPS. PID 47980 stopped. |
| Membership audit | `out/native-scene-membership-*` | 683,404 lists and 31,619,986 node visits, zero membership mismatches; 11,438,821 culling checks, zero mismatches. Owned processes stopped. |

Exact failed material log:
`out/native-bridge-run/binding-validation-20260918-224925-ec212133/game.log`.

Geometry preload's first publication was `ready=412 loaded=412 reused=0 deferred=0`; last sampled publication was `ready=412 loaded=14293 reused=2694607 deferred=0`. Last sample had 24,381 native objects and 45,603 group-created records before draw. There were no reported untracked writes, direct retries, queue fallbacks or logged errors in that verified run.

Do not claim an improvement from 56.75 to 55.15 FPS or compare unmatched builds/scenes as a controlled benchmark. Earlier default-path measurements were around 80 FPS; opt-in scene paths have mostly been around 50–62 FPS. None demonstrate the final target.

### Edits after the latest successful material build/tests

These require rebuilding; the existing binary does not validate the current source:

- `global` tags on owned constant/texture inputs, their population, and test assertions.
- Backend identity check when reusing a material program.
- Preload cvar description updated to mention material inputs.
- Comments and documentation.
- Texture binder `8213BA98` fingerprint added to the hook audit; rerun that audit.

## Architecture map

| File under `src/native_graphics/` | Responsibility |
| --- | --- |
| `native_scene.h/.cpp` | Lifetime-safe database, immutable scene snapshots, renderer, interpolation, culling and instancing. |
| `native_scene_bindings.h/.cpp` | Reflected material capture; separates world/camera matrices from immutable material bytes. |
| `native_scene_adapter.h/.cpp` | Owner/generation/LOD/part mapping, asset interning, source transforms and publication. |
| `native_scene_sources.h` | Constructor/model/destructor identities, LOD parts, worlds, bounds and group membership/revisions. |
| `native_scene_geometry.h` | Static group geometry descriptor reading. |
| `native_scene_material.h` | Owned material input decoding and independent binding reconstruction. |
| `native_queued_scene.h` | Frame-local native queues and fallback restoration of guest links. |
| `native_scene_membership.h` | Event-owned spatial-list membership and immutable ordered snapshots. |
| `native_scene_visibility.h`, `native_scene_cpu_window.h` | Audited visibility behavior and short-lived page-validation reuse. |
| `guest_shader_bridge.cpp` | Gameplay hooks, preloading, native/guest integration. |

Tests live in [native_scene_tests.cpp](../tests/native_scene_tests.cpp). The longer implementation history is in [native-scene-renderer.md](native-scene-renderer.md); early sections describe historical intermediate states, not necessarily the current integration.

### Geometry and object population already implemented

Simulation publication after `821A4DE8` loads all registered static groups without invoking guest setters, material activation, instance callbacks or draw callbacks. Only registered physical `NativeModelBuffers` resources are accepted. Guarded `NativeBufferWrites` snapshots and `CommitObservedGeometry` retain immutable native generations; there is no unguarded payload fallback. Geometry is interned in the same pool used by later visible draw capture.

Reuse validates descriptor fields, group revision, shader bytecode, declaration identity, buffer generations and lifetime/revision. Cache hits do not depend on the global writer epoch; scheduled comparisons use `CopyObservedSet`. `NativeSceneCpuWindow` caches permission validation briefly, never guest payload contents.

`NativeScenePublication::group_geometry` owns group assets independently of the instance snapshot. Failed loads remove the current asset and retry later; old publications remain immutable.

`PopulateGroup` can create eligible unseen parts using a group's captured geometry/material and simulation-published worlds. It requires one vertex-stage world matrix and exact world-only overrides. It does not add parts to the current visible queue or write guest constants. Its cache keys include group revision and geometry/material identity. Owner retirement invalidates only affected groups; do not restore broad invalidation, which previously caused millions of unnecessary examinations. Entirely unseen groups still need material resolution before they can become fully renderable.

### Material programs currently implemented

`NativeSceneMaterialInputs` owns shader identities, named constant byte streams, texture handles/slots/settings, optional LOD range and ordered state override records. Constant bytes remain in guest big-endian float4 format. Global/local tags identify their origin.

`NativeSceneMaterialProgram` owns the backend, shader prototypes/bytecode/reflection and exact native texture generations. Its `Capture` method creates independent CPU `ShaderBindings`, validates linking and reconstructs constants/resources.

**Capture does not resolve or execute state overrides or inherited sampler state.** Its caller must supply an already resolved pipeline, blend factor and samplers based on explicit pass state. A material program is not yet a complete gameplay material/pipeline.

The adapter publishes `NativeSceneGroupMaterial { group, revision, program }` through `group_materials`; it supports publish/lookup/retire. `PruneGroupGeometry` now prunes material programs too despite its name.

`PreloadStaticSceneMaterialsLocked` runs after geometry preload at simulation publication. It uses the loaded group's material address, published parameter schema and native shader reflection. Textures must be registered, content-valid and have a native handle. Reuse compares owned inputs, texture generations, shader bytecode identities and now backend identity. Failure retires the current program and retries next publication. Logging occurs every 120 ticks.

Tests already cover local/global indirection, unused unreadable parameters, settings/order/LOD/state copying, source immutability, retained old snapshots, capacity/slot/state-count rejection, and reconstruction/rendering after original bindings/resources change. They did not cover the descriptor-count/global-capacity distinction that failed in gameplay.

## Remaining architectural work

`edf_native_frame_dispatch` bypasses `__imp__sub_821A5080`; the original remains the default fallback. Scene queues/publication are scoped around that hook. The native `821D96D8` path still performs CPU stream/declaration/index and material-activation mirrors. Owned activation and geometry paths now use published group inputs, while unsupported groups retain live setup/capture. Selection can still mix a publication with current adapter records.

There is no complete independent static-world pass or independent frame scheduler. Do not render the entire database snapshot blindly: it includes different LOD parts and potentially different passes. Preserve pass membership/order, camera state, hidden flags, duplicate marks, LOD and render state.

Remaining milestones (material programs, explicit sampler/state resolution and owned group reconstruction have been implemented and audited):

1. Make native pass state authoritative, including inherited state across groups and handoff to remaining passes; remove CPU setup mirrors and live instance metadata reads.
2. Complete owner/LOD/pass membership and assets in one selectable scene snapshot, including unknown callbacks and transitions.
3. Render a complete static-world pass from that snapshot without legacy setup/submission dependencies.
4. Establish independent frame scheduling, camera updates and interpolation, and migrate animated objects, effects, shadows, UI and presentation.
5. Verify gameplay movement/firing, transitions/removal, visual fidelity and sustained 120 Hz independently of simulation. Current scripted audits and isolated rendering tests do not prove these gates.

`820B4250` (RenderWorld) increments object+364 and advances float+356 before effect parameters. The whole helper is not side-effect-free; move/preserve simulation effects before bypassing it.

*Corrected 2026-09-22: see [renderer-status.md](renderer-status.md).*

## Audited data layouts and traps

### Static group geometry

- Descriptor = `Word(group + 12)`.
- Vertex resource is embedded at descriptor+4; index resource at descriptor+84.
- Declaration container = `Word(descriptor+72)`; selected node = `Word(descriptor+76)`. Reject missing values or node equal to `Word(container+4)`; declaration = `Word(node+28)`.
- Stride = `Word(descriptor+60)`; signed index count = `Word(descriptor+140)`, positive and truncated to complete triangles.
- Material = `Word(Word(descriptor)+16)`; pass = `Word(material+108)`; vertex shader = `Word(Word(pass))`.
- **`82149A90` is the declaration setter; `821375C0` is the index setter; `82137410` is the stream setter.** Earlier notes mixed the first two roles.

### Material activation and inherited state

Audit `821B8E48` in `generated/default/edf2017_recomp.38.cpp` (around line 8303; use `rg`, line numbers can move).

- Pass = `Word(material+108)`. VS = `Word(Word(pass))`; PS = `Word(Word(pass+4)+4)`.
- Schema groups: VS locals, VS globals, PS locals, PS globals.
- Local constant data starts at record+4; available count is metadata `registers`.
- Global constant storage follows `Word(record)` to a vector; data = `Word(vector)`, capacity = `Word(vector+8)`.
- Local textures are 28-byte records: name, handle, slot, bias bits, mip, min, mag.
- Global textures are `[node, slot]`; node+28 is handle, +32/+36/+40/+44 are bias/mip/min/mag.
- Texture LOD range is header+44 masked by `0x3fc`; null binding preserves inherited range.
- Material state vector: base `Word(material+96)`, count `Word(material+104)`, ordered 8-byte `[offset,value]` records. Guest activation looks up callback `Word(device+56+offset)`. Retained records must be decoded into native semantics, not executed as guest callbacks.

Sampler words are at device+1024+slot*24, offsets 0,12,16,20. Canonical masks in `native_sampler_decode.cpp`: `0x7fc00` addressing; `0xfff80000` filter/aniso; `0xfffffffc` LOD/bias; `0x1ff` fourth word.

Texture binder `8213BA98` (file 36, around 4545) preserves addressing and other inherited fields, inserts texture LOD bits and clamps against device byte+11678 per slot. Mag setter `82136888` (file 4, around 4389) and min setter `82136700` (file 33, around 4509) have alias/aniso behavior involving byte11652+slot and a lookup table; this is not fully decoded. Mip inserts `(mip<<23)&0x1800000` in sampler word+12. Bias setter `82136C20` (file 35, around 4434) scales/truncates and inserts 10 bits at word16 bits12–21; verify its scale constant rather than assuming 32.

`NativeRenderStateProducerFields` in `native_render_state_snapshot.h` identifies audited setters for the six native packed render fields.

### Visibility/membership work and remaining tree migration

Native membership is event-owned, preserves list order/endpoints and publishes changed lists after simulation. Hooks: ctor `821C4EB8`, copy `821C5D28`, insertion `821A1628`, removal `821A1678`. Native leaf `820B4038` uses snapshots until an unknown dispatcher; then it discards the snapshot and follows live links. Invalidate permission windows before guest callbacks.

Visibility preserves audited PPC rounding, sphere/OBB behavior, LOD, bounds, hidden and duplicate semantics, but guest spatial-tree traversal and unknown callbacks remain. Source lifecycle hooks include `820B33B0`, `820B2AC0`, `820B2870`, `820B2DF8`, `821BEF10`, `821C0B88`, and publication `821A4DE8`.

For the next tree audit: manager+48 level vector, begin+52/end+56, 32-byte records. Nodes are 144 bytes: center+32, half-extents+48, radius+64, eight children+84..112, occupancy+116, list header+120/end+132. Relevant functions: root walk `821C61D8`, recursive cull `821C5FC8`, accepted subtree `821C56C0`, manager construction/rebuild `821C7740`, spatial update `821C5730`, bounds recompute `821C5488`, hierarchy initializer `821C4F80`, AABB classifier `821C3178` using transform `821B0258`. Preserve debug manager+108 behavior calling vtable+24 or fall back.

**`821C75D0` is a level-vector resize helper, not a manager destructor.** Actual manager lifetimes/writers still need auditing. Nonhierarchy lists at world+372 and overlay+48 are not all tracked.

*Corrected 2026-09-22: see [renderer-status.md](renderer-status.md).*

## Build, test and gameplay workflow

Workspace: `D:\roms2\edf2027`, Windows PowerShell. Preserve the extensive dirty worktree and untracked source files. Do not reset, clean or overwrite unrelated work. Generated guest code must not be edited. No applicable `AGENTS.md` was found in this session; recheck when resuming. The user updated the environment policy: follow the current tool permissions rather than the historical `never` policy. Do not spawn agents unless user or applicable instructions authorize them.

Do not relink the executable while a game is running. Build helper `out/build-native-scene.cmd` initializes Visual Studio 18 Community and builds `edf2027` and `edf_native_scene_tests` with parallelism 2:

```powershell
cmd /c out\build-native-scene.cmd > out/UNIQUE-build.log 2>&1
```

Wait for exit 0 before testing or launching; otherwise stale binaries can mislead. If the local helper is missing, initialize `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat` in a cmd environment, then run:

```text
cmake --build out/build/win-amd64-release --target edf2027 edf_native_scene_tests --parallel 2
```

```powershell
& out/build/win-amd64-release/src/native_graphics/edf_native_scene_tests.exe > out/UNIQUE-tests.log 2>&1
$sceneExit = $LASTEXITCODE
Get-Content out/UNIQUE-tests.log -Tail 3
exit $sceneExit
```

Run the hook audit and targeted whitespace checks. `git diff --check` does not cover untracked files.

```powershell
cmake -DSOURCE=D:/roms2/edf2027/src/native_graphics/guest_shader_bridge.cpp -P tools/audit-native-hook-boundaries.cmake
```

Gameplay launch:

```powershell
$run = & tools/start-native-binding-validation.ps1 `
  -InputScript out/renderer-performance-input.txt `
  -ExtraArgs @('--edf_native_unlock_framerate=true','--edf_fps_cap=120',
    '--edf_native_scene_queued=true','--edf_native_scene_preload=true',
    '--edf_native_host_capture=D:/roms2/edf2027/out/UNIQUE.bmp',
    '--edf_native_host_capture_after_ms=230000')
$run | ConvertTo-Json | Set-Content out/UNIQUE-process.json
```

Use a unique artifact prefix. The script creates a fresh userdata/run folder. Assets are at `D:/roms2/edf3-translation-project/work/x360`. Typical timing: startup 130–145 seconds, menu selection 150–190, Mission 1 from about 199. Inspect the 230-second image, then stop the owned game around 250 seconds. Keep individual waits short enough to report progress. Logs rotate: collect `game*.log`, extract strings and sort timestamps before choosing the last sample.

To stop an owned run safely, read its JSON, look up the actual PID, verify executable path, then kill/wait. Saved metadata alone does not prove a process is alive:

```powershell
$ownedRun = Get-Content out/UNIQUE-process.json -Raw | ConvertFrom-Json
$ownedProcess = Get-Process -Id $ownedRun.Id -ErrorAction SilentlyContinue
if ($ownedProcess) {
  if ($ownedProcess.Path -ne $ownedRun.Executable) { throw 'Process identity mismatch' }
  $ownedProcess.Kill()
  $ownedProcess.WaitForExit()
}
```

```powershell
python tools/report-native-geometry-runs.py $run.Log --start 199 --end 241 --output out/UNIQUE-fps.json
```

Do not use audit-heavy or incomplete runs for performance conclusions. `edf_native_scene_queued`, `edf_native_scene_preload`, `edf_native_scene_visibility` and `edf_native_scene_visibility_audit` default to false. Visibility remains opt-in because the mixed path was slower; do not enable it in a preload comparison without explicitly accounting for that change.

Hook fingerprints normalize CRLF to LF, slice from `DEFINE_REX_FUNC(sub_ADDRESS) {` to before the next `\nDEFINE_REX_FUNC(`, then SHA256. Relevant entries:

- `821B8E48`, file 38: `1382e9cfc357408d69ec2b59776db047cc256215fc6977d8d371b6e8cd1ebf36`.
- `8213BA98`, file 36: `8f11248594563e3bca0be3d3ca72618b4b2997b6b6265ceda6a574684a1fcb74` (added after last material build/audit).

## Resume checklist

1. Read this handoff and inspect current diffs; preserve all existing work.
2. Recheck the latest process state and animation comparisons; the global constant capacity regression is already repaired.
3. Build the current source, run scene tests and hook audit.
4. Run gameplay and report material coverage honestly, including deferrals and screenshots.
5. Continue toward an independently submitted static-world pass, then independent frame cadence. Asset readiness is a prerequisite, not completion.
