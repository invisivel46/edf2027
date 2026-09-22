> Historical as of 2026-09-22; current status in [renderer-status.md](renderer-status.md).

# Static-group completion: current implementation evidence

The full supported static-group goal remains active. This report updates the
earlier indexed/cache handoffs; it does not close geometry/material ownership or
claim the backend-only replay proves guest ingestion.

## Native CPU completion chain

CMake now compiles authored C++ for all six entries in the packet-free indexed
completion chain:

| Entry | Source | Owned effects |
| --- | --- | --- |
| Indexed completion | `native_indexed_completion.cpp` | Dirty-bank entry snapshots and ordered consumption |
| Main state | `native_main_state.cpp` | Shader selection, CPU state summaries, mode flags, helper dispatch and dirty-mask return |
| Derived state | `native_derived_state.cpp` | Two ordered device+10432 updates, device+10809 transition bit, dirty bit 256 |
| Cache completion | `native_shader_cache.cpp` | IDs, signatures and last-use fence protocol |
| Upload bookkeeping | `native_shader_upload.cpp` | Output summary callback, device+10810 mode bit and cached signatures |
| Output bookkeeping | `native_shader_output.cpp` | Gated interpolator-count nibble in CPU output word |

These retain the existing `edf_native_*_cpu_tail` ABI names so callers need no
interface migration. None of these six production entries executes its former
generated PPC body. The original routines and audited extractors remain available
as historical/oracle evidence; unsupported retail paths remain unchanged.

The output helper's skipped traversal only selected Xbox microcode patches. With
those patches already omitted on this native path, its remaining CPU write is
the output-count nibble. The upload helper no longer reads code-size/destination
metadata solely to discard an allocation or packet; it retains the output
summary, mode flag and signatures. These are native-path contracts, not claims
that Xbox microcode routines have no effects in retail rendering.

The main-state implementation keeps the original guest frame/backchain for its
CPU output scratch and retains ordered state writes. Existing original-vs-native
tests cover shader/cache branches and 64-bit dirty-mask return. The matrix now
also sets previously zero metadata to exercise linked-shader feature thresholds,
CPU output words, nonzero interpolator counts, and render-mode selection gates.
No unsupported material or resource case is silently admitted by these changes.

## Verification for this revision

Three focused suites pass in 41.23 seconds after replacing all four remaining
adapters and expanding the metadata matrix. This includes existing original
output/upload/derived comparisons, the main-state matrix, 320 indexed dirty-bit
cases, 24 shared group handoff cases, cache lifecycle and negative controls, and
retained replay on both WARP backends. CPU projection excludes stack scratch and
explicit packet/cursor storage; packet providers remain controlled where noted
in the indexed fixture. Exact logs and final build/offline status are under
`out/static-group-native-chain-*` and `out/static-group-native-chain-result.json`.

Epistemic session: `session-20260922192948-b975941d`.

## Remaining completion requirements

Material follow-up: `native_material_cpu_program.h` now contains the production
bounded reader, shader/constants/textures/states execution order, and disjoint
constant uploader. Deferred restoration calls native activation directly when
enabled and state/sampler audits are off; compatibility restoration is counted.
Native activation directly binds/publishes shaders. The group differential now
compares original `821B8E48` activation (including original shader/constant setters)
with shared production execution, for all four local/global vertex/pixel constant
lists. Empty/fence retirement and no-draw dirty masks are compared. Material
changes are no longer a one-word stand-in. Textures, explicit state lists, aliased
constants and allocator growth remain outside these configured group cases;
submission/world restoration were controlled seams at that checkpoint; the
integrated fixture below replaces them. This is not yet the
declared fully guarded supported-group gate. See `out/static-group-material-result.json`
and logs for final validation status and hashes.

Geometry follow-up: the retained published-geometry path now calls shared
`InstallNativeSceneGeometry` directly, bypassing stream/declaration/index guest
setter wrappers. Its original/native group comparison now executes all three
original setters and compares descriptor, dirty-bank, stride, binding and old
resource fence writes. Host publication is ordered after each completed binding.
Queue growth remains an explicit allocator callback; the group differential's
current cases exercise empty/fence retirement, not allocator growth. Material and
submission were controlled seams at that checkpoint. Exact geometry results are recorded in
`out/static-group-geometry-result.json` and the associated logs.

1. Enforce the declared first-group eligibility in production. The packet selects
   one supported opaque configuration; broader texture/state/default aliasing and
   allocator failure contracts retain their existing R09.shader, R09.effects and
   R09.retirement tasks. They must not be silently admitted as proven by this fixture.
2. Verify production shader-mirror publication at the milestone. The integrated
   fixture below now connects actual world restoration and GPU submission for
   the declared synthetic group; live reflected-register patching remains outside it.
3. Declare and verify the supported configuration against the compatibility
   counters below. A zero mask alone does not certify all transitive behavior.
4. Verify production eligibility for the integrated retained group below.
   Publication-failure installation retries are now covered as described below.
   The combined CPU/GPU fixture covers
   repeated/out-of-order generations and reused logical source addresses; it
   does not certify arbitrary aliased metadata or allocator failure recovery.
5. Audit every first-packet completion requirement, refresh source-derived atlas
   and dispatch provenance, and perform applicable gameplay validation at the
   static-group milestone. No gameplay boot has occurred during this iteration.

## Restoration retry and execution accounting

The combined handoff now restores material/world before clearing its geometry
obligation even when no draw was accepted. Previously a throwing restoration in
that case could leave material pending while a retry returned early. Four new
regressions cover activation/world failures with and without draws, requiring
one geometry installation, no premature consumption/invalidation, and no repeated
completed activation. The initial focused run passed all three suites in 40.51s
(`out/static-group-restore-retry.log`).

`NativeSceneExecution` counts named boundaries per group. The production scope
logs completed/aborted status, successfully recorded batches, call count and a
boundary mask, including exception exits. Recorded batches do not imply backend
submission or pixel correctness. Compatibility wrappers may select native code;
the counters deliberately do not label every counted call as original execution.

| Mask bit | Boundary |
| --- | --- |
| 0 | Original group implementation |
| 1 | Nonpublished geometry setup wrappers |
| 2 | Material activation wrapper/original activation |
| 3 | Instance setup fallback |
| 4 | Indexed draw fallback |
| 5 | Retirement queue allocator |
| 6 | Aliased or unaligned constant upload |
| 7 | Unsupported material state callback |
| 8 | Original constant/state audit oracle |
| 9 | Original texture binding |
| 10 | Original shader binding |
| 11 | Configuration outside the declared first-group contract |

The opt-in diagnostic cvar `edf_native_scene_reject_compatibility` records and
throws before these calls. It is off by default; unsupported configurations keep
their compatibility behavior. This is a diagnostic guard, not a transactional
preflight: prior native writes may already have occurred. Tests exercise every
counter in permissive and rejecting modes, independent group state, and zero
binding-wrapper entries in the configured native group differential.

The instrumentation does not yet prove eligibility for arbitrary operation-table
aliasing, partial geometry installation retries, or all callbacks reachable from
the original group path. Those remain completion work, not silently completed
statuses. Final validation for this change is recorded in
`out/static-group-execution-result.json` and Epistemic.

## Integrated CPU and GPU fixture

`native_immediate_tail_tests` now links the WARP renderer through
`tests/native_static_group_gpu.cpp`. Its group submission callback renders the
same arena's geometry and material inputs that the original/native CPU comparison
uses. Geometry passes through production big-endian `NativeIndexedMesh` ingestion,
material input selection uses `ReadNativeMaterialCpuProgram`, and retained output
uses `NativeSceneAdapter` and `NativeSceneRenderer`. This closes the earlier
separation between a CPU-only group test and a backend-only pixel replay.

The declared configuration is a position-only indexed opaque quad, fixed identity
world, no textures or explicit material state lists, disjoint local/global
constants, and empty/fence retirement. It compares inherited red and changed blue
materials against an independent 64x32 integer rectangle oracle. Every group runs
on D3D11 and D3D12 WARP. Current/previous/current publication replay happens after
producer release, with reused logical source addresses and weak-reference release
checks. A changed expected pixel is passed through the real pixel comparator and
must be rejected. The existing ordering and CPU-word negative controls remain.

CPU geometry, activation and indexed completion still compare against extracted
original routines. Full 64-byte world restoration now uses
`RestoreNativeSceneWorld`, shared with production, and compares against an
independent raw copy. The fixture's host shader-mirror callback verifies CPU write
ordering; it does not execute the live bridge's reflected shader-register patcher.
Fallback cases execute original `821D9600` instance constants and `821FE358`
indexed work after the handoff, compare non-packet state and count both boundaries.
Their legacy GPU packets remain excluded from the native pixel oracle.

The matrix retains 24 cases (0/1/3 native draws, changed/inherited material,
fallback/no fallback, injected tail failure/no failure). The first integrated run
passed the focused suites in 45.31s. Final strengthened validation and hashes are
recorded in `out/static-group-cpu-gpu-result.json`. This suite is now part of the
single offline command: 27 suites, with about 45 seconds added for the CPU/GPU
differential. No game boot is needed for this gate.

## Partial installation and recording failures

Geometry installation now retains six CPU/publication steps in the live group's
`NativeSceneGeometryInstallState`. A throwing stream, declaration or index
publication retries that publication and continues; it does not retire the newly
bound resource or repeat earlier completed publications. Retry inputs and device
identity must match. Six regression cases cover each publication boundary with
fence and deferred-queue retirement, including exact queue records and unchanged
new-resource fence words. The initial focused run passed all three suites in
43.19s (`out/static-group-install-retry.log`). Valid reader accesses and the live
nonthrowing audit observer are preconditions; arbitrary allocator mutations on
failure remain in R09.retirement.

The live recording path retains `group.objects` until rendering succeeds. A
failed recording is remembered by `NativeSceneExecution::RecordBatch`; subsequent
attempts are rejected and the group cannot be marked complete. This deliberately
does not retry potentially partial GPU commands, which cannot be rolled back.
Successful recordings remain counted, failed attempts do not increment the count.
The regression injects a failure after recording starts and verifies the callback
is never re-entered and completion is rejected. Final validation is recorded in
`out/static-group-failure-result.json` and Epistemic.

## Production eligibility

`AssessNativeStaticGroup` now runs before published geometry can be deferred.
It admits nonempty indexed triangle geometry, aligned device storage, an opaque
inherited blend, constants-only material lists, matching shader identity, and
empty/fence retirement. Shader defaults cannot alter the six inherited pass
words. Existing retained geometry, program, pass and publication checks still
apply; this preflight does not replace them.

The guard traces the actual material program and shader-default reads. Captured
operation tables and payloads must not overlap CPU device writes, the guest
stack scratch window or old-resource fence writes. Shader defaults extend the
protected device range when their destination is outside the usual mirror.
The preflight is read-only. Supported integrated fixtures must pass this same
guard; tests reject geometry/alignment/pass errors, texture/state lists,
non-fence retirement, device/stack aliases, fence aliases and default writes
aliasing inputs.

Rejected configurations increment boundary bit 11 and log reason values:
1 geometry, 2 texture/state lists, 3 alignment, 4 retirement mode, 5 aliased
input, 6 inherited/default pass state. Their material work is not deferred and
uses original activation, shader and texture binding paths, preserving sequential
reads. Missing published geometry also keeps material compatibility mode. These
are explicit fallbacks, not claims that all other static materials are ported.

The GPU fixture now consumes the same six render-state words as the CPU fixture,
rather than a hardcoded opaque pipeline. Its seed explicitly supplies the opaque
state required by the guard. Final validation is recorded in
`out/static-group-eligibility-result.json`; runtime milestone evidence is still
required before closing the static-group goal.

## Milestone failed: loading and scene throughput (2026-09-22)

The native street image is existing functionality, as the user corrected. It is
not evidence of a completed static-group cutover. The user reports very slow
loading and roughly 1 FPS. The run's game FPS log shows sustained approximately
6-8 FPS after mission entry, with some later variation. Host presentation around
57 Hz repeatedly displays the same image and must not be reported as scene FPS.

Evidence: `out/native-bridge-run/binding-validation-20260922-173921-2c5daf8a/game.log`
and `out/static-group-milestone-process.json`. First nonempty group logging is at
17:42:14.164, about 171 seconds after process launch. This includes scripted menu
delays; it is not a measured mission-loading duration. All 32 logged samples with
nonzero recorded batches have compatibility calls. Zero-call empty groups do not
meet the completion criterion. The 27 passing offline suites did not cover this
performance failure or representative material eligibility.

The new preflight rejects texture/state lists and checks incoming blend before
material overrides. The rejection then forces original material, shader and
texture routines. This routing is verified in source; its share of runtime cost
is not measured. The launch also enabled adapter auditing. Do not attribute all
loading or frame time to the eligibility change without a profile.

Read-only capture `out/static-group-live-materials.json` contains 11 real material
samples, all with stable before/after headers. They include texture and state
lists absent from the integrated synthetic fixture. Capture is non-atomic and
does not include all indirect payloads, so it is input-shape evidence, not yet a
complete replay oracle. Process 43912 was no longer running when checked after
the user's report; no replacement game was launched.

Required follow-up tasks, in order:

1. **Separate deferred-group eligibility from native operation support.** Inspect
   `AssessNativeStaticGroup` and the four material/texture/shader hook gates in
   `guest_shader_bridge.cpp`. Preserve alias and ordering protections, but do not
   force established native operations to originals solely because a material is
   outside the constants-only deferred fixture. Depends on reviewing the actual
   captured state/texture shapes. Completion: offline cases with those shapes and
   inherited nonopaque state followed by opaque material overrides exercise the
   intended native route, while unsafe aliases still take counted fallback.
2. **Measure loading and producer costs separately.** Use existing hook timing
   phases for texture lock/create, shader registration, material activation,
   scene publication and rendering; inspect submission-lock ownership and audit
   overhead. Depends on task 1's route correction and offline validation. Add
   explicit mission-load start/ready markers before the next milestone so menu
   script delays are excluded. Completion: a profile attributes loading wall
   time and scene producer time to measured phases, including waits, with audit
   settings recorded. Host Present cadence is a separate metric.
3. **Make the failure reproducible offline.** Complete the captured material
   fixture with required indirect data and expected original/native results;
   exercise repeated frames and resource reuse, not just an untextured quad.
   Depends on task 1's routing contract and task 2's measured expensive phases.
   Completion: the single offline command rejects the broad-original fallback
   regression and unnecessary repeated resource work. One subsequent milestone
   run must demonstrate nonempty zero-legacy groups, acceptable loading and
   measured producer frame times before this group can be called complete.
