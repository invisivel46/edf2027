# Active geometry comparison boundary

## Per-draw geometry comparison replaced by a sampled schedule (2026-09-12)

The per-draw comparison is gone from the steady state. `CopyObservedSet` now
reuses a retained candidate **without reading guest memory** when the
subscription proves it unchanged, and keeps the comparison only on a bounded
schedule. `NativeBufferWrites::SnapshotPolicy` carries `verify_initial` and
`verify_interval`; the bridge sets them from `edf_native_geometry_verify_initial`
(default 8) and `edf_native_geometry_verify_interval` (default 256), and forces
`verify_interval=0` - compare every observation, the previous behaviour - while
the retirement revision audit or the mesh watch audit is enabled.

The trust predicate is the existing `revision_audited` one, now evaluated on
every observation rather than only under the audit flag: the candidate must be
the *same immutable* `shared_ptr` this subscription last published, and the
subscription's revision must be unchanged since. Everything else compares.

### What makes the removal safe, item by item

The earlier "Next implementation boundary" list required four things together.

1. **Layout-independent content snapshot with explicit lifetime.**
   `NativeVertexBuffer` holds a shared immutable CPU source, and
   `NativeModelBuffers` holds the canonical `vertex_contents`/`index_contents`.
   `Subscribe` resets the whole subscription (including the new observation
   counter), so a reused owner never inherits a baseline; `Retire` unsubscribes;
   a dropped snapshot leaves a weak reference that no longer locks.

2. **Covered producers.** This is now an *enumerable* argument rather than only
   a sampling one. Guest-side writes come from recompiled stores, all reachable
   through the audited provider hooks (821E8320, 821E8740, 821EA320, 821E9BA0,
   8213BDF8 and the buffer lock/unlock chain) or, with
   `EDF2027_OBSERVE_SCALAR_STORES=ON`, through the generated scalar/SIMD/atomic
   /inline-fill observer. Host-side writes can only come from the XEX's import
   table, which has 160 entries; the only two that can carry a bulk payload into
   a caller-supplied buffer are `NtReadFile` and `RtlFillMemoryUlong`, and both
   are already routed through native adapters (`WriterKind::FileRead` and
   `WordFill`). **There is no `XMemCpy`, `XMemSet`, `RtlCopyMemory` or
   `XMemDecompress` import**, so the game performs its own copies in recompiled
   code. The remaining importable writers (`Nt*`/`Mm*` query calls, Xam profile
   and content calls, the crypto digests, `XMsgStartIORequest`) write small
   caller structures, not model storage.

3. **Synchronization.** Unchanged: `WriterScope` exclusion still rejects the
   whole acquisition when any overlapping or unclassified scope is active, an
   aborted scope still advances every subscription's revision, and attachment
   still goes through `CommitObservedSet`. Trust only removes a *read*, never a
   guard.

4. **Validation against the live path.** Retained rather than retired. The
   comparison is the only oracle for writer coverage, so it is sampled instead
   of abandoned, and one disagreement (`unreported_change`) permanently revokes
   trust for the entire queue - not just that owner - and logs a warning. New
   `Trust()` counters report compared/trusted/unreported/revoked.

Unit coverage in tests/native_guest_memory_tests.cpp exercises the initial
window, unsampled trusted observations, detection of a planted uncovered write
at the next sampled observation, permanent revocation afterwards, unproven
candidates always comparing, and the window restarting on owner lifetime reuse.

### Live A/B, same binary and scripted Mission 1 route

Two owned runs of `out/build/win-amd64-release/edf2027.exe` with hook timings
on, audits off, `tools/native-movement-fire-input.txt`, 1280x720 hidden window,
each stopped after exact-path validation and WaitForExit.

| | `verify_interval=0` (compare every draw) | `verify_interval=256` |
|---|---|---|
| Run | `out/native-bridge-run/geom-always-20260912-173327` | `.../geom-sampled-20260912-173729` |
| mesh.acquire per draw | 964.7 ms / 620,858 = **1.55 us** | 386.1 ms / 768,381 = **0.50 us** |
| indexed.mesh per draw | 1.90 us | 0.84 us |
| indexed.native per draw | 2.89 us | 1.76 us |
| Indexed draws per second | ~124,000 | ~154,000 |
| FPS, t=126..186 s | 46.6..54.3, mean ~49 | 58.6..60.2, mean ~60 |

Final schedule counters for the sampled run: compared 90,317, trusted
16,702,811, unreported_changes 0, revoked false - 0.54% of observations still
read guest bytes. The always-compare control logged compared 8,404,520 and
trusted 0, confirming the switch actually selects the old behaviour.

Because this game's simulation advances a fixed amount per guest frame, the
46..54 FPS interval was literal slow motion, so this is a gameplay-speed fix and
not only a CPU saving. These are presentation-hook counts from hidden windows:
not scanout cadence, not a handheld measurement, and not a full mission.

Remaining: the sampled schedule bounds how long an uncovered writer could go
unnoticed (up to `verify_interval` observations of that owner), it does not
eliminate that possibility. Writer coverage for unregistered/nonphysical buffers
is unchanged - those still take the unguarded path and its full comparisons.

## Direct native VB/IB lock entrypoints (2026-09-11)

The buffer-lock extractor now hash-gates complete 82134958/82134A78 wrappers
and redirects their shared-helper call to the native CPU lock tail. Native
bridge entrypoints select these wrappers; legacy mode remains retail. The
inline-index scope still begins after lock return using the saved caller and
returned destination. Whole-buffer zero-length selection, VB address-bit mask,
IB access flag, caller stack scratch and ABI are retained.

The differential lock matrix now includes both wrappers with whole-buffer and
explicit offset/length requests, across thread, cache, fence and flag states.
It checks returned address/ABI, nonvolatile registers, resource headers, fence
calls, dirty ranges, header notification and absence of native cache packets.
The first full test run failed because the new buffer fixture dispatcher also
intercepted texture fixtures (both fixture flags are enabled there); dispatch
now preserves the texture-specific route. This was test routing, not a change
to production lock behavior. Revalidation passed: build and all 21 tests,
25.35s. Executable SHA256:
A198525DD1EDB34F7B800A13A074CA6BBF07DE10F03596C744A7621832FAB0EA.

This is direct wrapper integration, not a wholly native lock dependency chain:
8213C928, 821F9FC8 and 8213BDF8 remain in the shared helper. No geometry
comparisons were removed, and no live run of this revision has been performed.

## Unlock-chain live smoke validation (2026-09-11)

Run out/native-bridge-run/binding-validation-20260911-064418-3c3900e6 used
050285DD (native unlock chain, full draw chain and bulk-provider attribution),
RetirementAudit and the bounded movement/fire script. Owned PID42556 started
2026-09-11T06:44:18.8332123-03:00; exact executable/start identity was verified
before stopping, WaitForExit succeeded, and process absence was verified.

Inspected transition.png is a menu transition, not proof of loading-screen
appearance. intro.png already shows street gameplay with player/NPCs/HUD and
mothership; bright ground highlights remain. movement.png shows the player
farther along the street with ammo101/120, establishing movement and firing.
Startup logged three buffer updates (all model_tracked=false), exercising the
update path; branch-specific unlock coverage was not separately counted.

At06:48:12.565 guarded acquisitions16777216 had unavailable0, all registered
physical pairs; checkedVB16776451/IB16776450, without-baseline765/766 and missed0/0.
Latest indexed coverage06:48:27.439: requests18825633, all submitted. Immediate
coverage06:48:22.538: requests4864805, all submitted. No error/critical,
unreported-change, guest-header-fallback, unsubmitted-path or writer-site row
matched the retained logs. No new live writer target was discovered.

This is a bounded loading/gameplay regression check, not a full mission,
conflicting-writer/release test, loading visual validation or FPS benchmark.
Comparisons remain enabled; the absence of writer-site samples does not certify
immutability or complete producer/lifetime coverage.

## Native VB/IB unlock CPU chain (2026-09-11)

The unlock boundary is post-write, not an exclusive interval around caller
payload stores. NotifyNativeBufferUpdate invalidates aliases after header
coherency bookkeeping, so this boundary alone cannot certify version-only draws.

Native bridge VB/IB unlocks now directly execute extracted CPU implementations
of 821349B8/82134AD8 and shared 82134640. Full normalized function hashes gate
all three bodies in tools/extract-native-buffer-unlock.cmake. The shared helper
calls NativeCacheFlushCpu directly instead of retail 82141AB8 dispatch/cache-line
iteration. A scan of the generated chain finds no retail sub_ADDRESS calls;
ABI save/restore and runtime atomic/global-lock primitives remain. Original
header atomic decrement, dirty-range reset, address arithmetic, stack scratch
and CPU outputs are retained. Bridge-disabled paths still call retail wrappers.

Existing payload/header writer scopes, completion ordering and alias invalidation
are unchanged. The index inline-producer fixture retains its controlled provider
seam; the real header/CPU differential matrix executes the actual native chain.
That matrix compares the full fixture arena and PPC context against retail for
both buffer kinds, native/legacy modes, three lock counts, three dirty ranges,
physical-header flag on/off, and aligned/low-bit-set data addresses. It also
checks that native unlock makes zero retail cache-flush dispatches while legacy
dispatch counts match the reference. No gameplay or performance claim; writer
coverage, lifetime handling and geometry comparison removal remain unfinished.

Final build and all 21 tests passed in 25.23s; diff check passed with line-ending
warnings only. Executable SHA256:
050285DD327049DBF1AD8B0520FDB7DF095DC886008A44EA9BF32E5B3FBD0A3A.
No live run of this revision.

## Creator cross-check and remaining bulk-provider attribution (2026-09-11)

Rechecked direct generated calls to 822CFE58 and 822CFDC0: one each, from
the already-migrated model constructors. At 821D7700 the index constructor
passes format 1 in r5; 822CFE58 packs that format into header bits 29..31.
This does not expose a new 32-bit creator. It does not exclude indirect,
inlined or serialized headers. Registry width support is a prerequisite, not
evidence of an active missing creator; prioritize actual model writer coverage.

The backward inline branch of 821EA320 and bulk fill 821E9BA0 now capture
entry LR and attach provider/caller identity to completed-write notifications.
The existing audit flag still controls retention of that metadata. Forward
memmove delegates to the copy provider without duplicate outer notification;
self-copy remains non-writing. Existing scope and invalidation policy is intact.

Production-extracted tests verify caller/provider identity, notification after
payload completion and before scope exit, inventory counts, and unchanged
contents/return behavior across alignment, overlap, self-copy and zero-length
cases. Queue records now retain these providers in the test seam too.
Build and all 21 tests passed in 23.63s; diff check passed with line-ending
warnings only. Executable SHA256:
802707A610376617176700CDD850C22D513695778C644573CB3CF38B11C00E98.
No live samples collected. This adds attribution for already-guarded providers,
not coverage of new stores or proof of complete writer/lifetime handling.
Geometry comparisons remain enabled.

## 32-bit index ownership prerequisite (2026-09-11)

NativeModelBuffers::Publish now accepts index element widths 2 and 4, matching
the native index-buffer/draw implementation. Unsupported widths still reject.
The retail model constructor remains uint16; no additional guest creator is
registered by this change and the unregistered guest-header fallback remains.

CPU ownership tests cover both widths across explicit update, range, full and
page invalidation, retained snapshot lifetime and owner reuse. GPU tests publish
both formats, commit their native index storage, verify the mesh reuses that
same object, and render/verify the result. Both formats reject commits after
tracked modification, owner reuse and retirement.

Build and all 21 tests passed (23.89s). Executable SHA256:
F87E2A87713B76218984F765BF49A009FF2A632BAC32D9CDA24C6A8E0C81F15A.
No live gameplay run or performance claim. Geometry comparisons remain enabled;
creator registration, complete writer/lifetime coverage and safe conflict
handling are still required.

## Evidence from the September 10 registration probe

Source: `out/native-bridge-run/binding-validation-20260910-193339-804d6894/game.log`.
The 19:37:06.405 sample reports 2,199,322 vertex checks and the same number
of index checks. Candidate extents total 102,623,780,004 vertex bytes and
5,568,439,842 index bytes: **94.85% vertex**. These are cumulative lengths
offered to comparisons, not measured memory traffic or CPU time. The hidden
run reached the Mission 1 introduction, not a full mission or benchmark.

The last indexed sample reports 678 builds, 339 mesh entries and zero reported
vertex/index mismatches. A cache that mostly hits is still scanning guest data;
neither the hit count nor zero mismatches establishes writer completeness.
No worker registration was logged, so that branch did not provide a concrete
writer target for this sample. Its unresolved static edges remain in the audit,
but are not evidence that it causes the active comparison workload.

## Current implementation, rechecked

`PublishNativeModelBuffer` in `guest_shader_bridge.cpp` publishes metadata and
physical extents after `821D7530` (VB) / `821D76A8` (IB). Index publication
also creates a native CPU/GPU snapshot. Vertex storage is attached on first
mesh acquisition because conversion depends on declaration/shader semantics.

The indexed bridge still obtains live vertex/index spans before calling
`NativeMeshCache::Acquire`. On a layout-compatible hit, `d3d11_mesh.cpp`
calls `NativeIndexBuffer::Matches` and `NativeVertexBuffer::MatchesSource`.
Both test the live source contents. Retaining native storage currently avoids
reconstruction, not these comparisons.

`NativeModelBuffers::generation` identifies publication/lifetime.
`NativeBufferWrites::ObservedVersion` advances only for completed writes
reported to `Record`; `CommitObserved` guards attachment against those
notifications. Neither establishes that arbitrary writes were intercepted,
nor excludes a concurrent write between the store and its notification.
Using either token alone to bypass the existing comparisons would therefore
weaken correctness. The diagnostic generated-store observer explicitly does
not claim a complete ownership contract.

## Next implementation boundary

Prioritize the model VB publication-to-first-use and update paths behind
`821D7530`, rather than recursively expanding unobserved generic callbacks.
For native-owned contents to replace live validation, the implementation needs
all of the following together:

1. A content snapshot owned independently of any one shader/declaration's
   converted vertex buffer, with explicit lifetime and alias handling.
2. Covered producers that update/invalidate that content, including raw stores
   and native/imported providers—not merely constructor/destructor metadata.
3. Synchronization that orders writes and snapshot consumption, rather than
   treating a post-write notification as an exclusive access guard.
4. Validation against the existing live path before removing comparisons for
   the covered owners; unclassified owners remain explicitly outstanding.

This document records the active-path diagnosis and prioritization, not a
completed ownership implementation. No checks were disabled in this review.

## First ownership change: layout-independent CPU snapshots

`NativeVertexBuffer` now retains a shared, immutable CPU source vector instead
of embedding a mutable vector in each GPU conversion. When a retained vertex
buffer has matching live bytes but an incompatible conversion layout,
`NativeIndexedMesh` reuses its CPU snapshot and creates a separate correctly
converted D3D11 allocation. Device/stride/attribute compatibility is still
required to share the GPU buffer itself. Dynamic paths do not share GPU
allocations; updates allocate a new CPU snapshot and attach it only after
the upload succeeds, preserving previously retained CPU generations.

Construction still validates live bytes before sharing the snapshot. Steady
cache-hit comparisons are unchanged. Cache accounting conservatively charges
the shared CPU bytes per mesh, as with other shared storage. This is not yet
publication-time, registry-owned canonical VB contents or a complete write
contract, and no measured performance improvement is claimed.

Built `edf2027-native-scene-only.exe` and `edf_native_quad_tests`. The focused
quad test passes with new assertions for distinct GPU layouts sharing one CPU
snapshot, GPU clip-position readback matching an independently constructed
reference, changed source rejecting snapshot reuse, and dynamic updates leaving
old snapshots intact. The initial altered-layout fixture overlapped attributes;
corrected it to a valid non-overlapping range before the passing run. The
worker-callback adapter test also passed. No game playtest of this change yet.

Cache-transition verification: extended the rendering test to acquire two
different declaration keys for one VB, prove distinct GPU conversions share
one CPU source identity, compare cached clip output with an independent
reference, update each layout after a source change, and invalidate/recreate
the VB while retaining the old snapshot. All assertions pass, including that
the retired source generation is neither mutated nor revived by the cache.
Rebuilt the full native build's default target and ran the complete CTest suite:
20/20 passed (12.47 seconds). These tests do not establish full writer coverage,
concurrent guest-write exclusion, or gameplay/performance correctness.

Live smoke test of the snapshot-sharing build:
`out/native-bridge-run/binding-validation-20260910-194618-1322e37f/` retains
`game.log` and `mission.png`. Executable SHA256:
`1F8D681F5AB5DF15F159127B4899BCF6D5F9DA2F1B57DE94BD1F4FBE56BE8CCF`.
The isolated hidden run reached Mission 1's mothership introduction after
adding 65/70-second confirmations to the scripted menu sequence. Final sample
at 19:48:10.511: 2,186,000 indexed submissions, 344 mesh builds/entries,
zero reported upload errors and zero vertex/index mismatches. These counters
do not measure actual snapshot sharing or certify untested writers.

The captured scene is heavily overbright, consistent with the unresolved
brightness concern; no matched-reference comparison or root-cause diagnosis
was performed here. This is only a startup/intro smoke test, not combat,
full-mission, fidelity or performance validation. PID 50740 was stopped after
verifying its path and start time 2026-09-10 19:46:19, and exit was confirmed.
No user save directory was used; the run artifacts were retained.

## Publication-owned vertex contents (follow-up)

The model registry now retains an immutable CPU vertex snapshot independently
of converted GPU buffers. The successful `821D7530` publication hook captures
the complete source extent; physical owners require the observed-write token
to attach it. All explicit/range/page/global invalidations clear these contents,
and publication generation checks reject attachment to a recycled owner.

Full-buffer indexed acquisition passes the snapshot to mesh construction.
Construction compares it with live bytes before reuse; a stale snapshot falls
back to a fresh live copy. After full-buffer reconstruction, the bridge can
retain the refreshed snapshot with the same observed-write handshake. Offset
streams still follow the existing live path and cannot replace whole-resource
contents with their suffix. Dynamic conversion continues to own fresh snapshots.

This moves initial CPU contents into model-resource ownership, but does not
complete producer coverage or synchronization and removes no per-draw checks.
Publication now copies VB contents earlier and retains them for the resource
lifetime, including resources never drawn. Loading time and memory impact need
measurement; no performance benefit is claimed.

Full native build and 20/20 CTest passed after initial integration (12.65s).
New tests exercise first conversion sharing the publication snapshot, rejection
of an unreported content change, physical-token requirements, wrong extents,
stale observed revisions, lifetime reuse, and explicit/range/page/global
invalidation without mutation of previously retained immutable generations.
These tests are not full-game validation; the new build has not been playtested.
The subsequent bridge refresh integration was also rebuilt and the complete
20-test suite rerun successfully. Live comparisons remain authoritative.

### Live validation of publication ownership

Run `out/native-bridge-run/binding-validation-20260910-195642-9909f460/`
used a fresh user directory, load timings enabled, and executable SHA256
`2D8CE44E00B9B934DEBAC2370969E31475293F1915CE0B25610053A3DC6A8C36`.
The inspected `mission.png` shows the Mission 1 mothership/city introduction;
it remains strongly overbright. At 19:58:47.635, the last indexed sample had
2,843,000 submissions, 716 builds, 377 resident mesh entries, zero upload errors
and zero reported vertex/index mismatches. No error/critical log rows were found.
This is startup/intro coverage, not combat, a complete mission or writer proof.

The logged `map.820B4D58` body took 3876.917 ms (inclusive); this is not total
loading-screen duration or a controlled before/after measurement. Whole-process
private bytes grew from 532,770,816 near startup to 1,011,036,160 at shutdown,
across menu/resource/scene loading. Without resource-specific accounting and a
matched baseline these samples neither measure snapshot overhead nor establish
a leak. Mesh accounting alone excludes publication snapshots not used by meshes.

The last source-check sample still reports 2,799,284 checks of each kind, with
126,149,342,488 vertex and 6,859,946,034 index candidate bytes. These cumulative
extents are not measured memory traffic. The live path remains comparison-based;
the run does not establish how often the publication snapshot was reused.

Owned PID 47700 was stopped after exact executable-path and start-time
(`2026-09-10 19:56:42`) verification. WaitForExit returned true and the process
was absent afterward. Logs and capture were retained; no user saves were used.

## Offset-stream ownership

Native vertex storage now retains a byte offset and length into its immutable
CPU backing snapshot. Indexed acquisition passes the model publication snapshot
and stream offset for both full-buffer and offset streams. Construction checks
range bounds without addition overflow and compares the requested range with
live bytes before sharing. Stale/out-of-range candidates fall back to a fresh
live snapshot; GPU conversion reads only the validated range. Reusing CPU content
for a different declaration preserves its offset. Dynamic updates remain fresh,
zero-offset snapshots. Whole-resource registry refresh requires a full backing
view, so a suffix cannot accidentally become canonical resource contents.

Mesh accounting conservatively charges the entire backing vector per retained
conversion; small views may pin a larger resource. This does not remove cache-hit
comparisons, complete writer coverage, or prove concurrent-write exclusion.

Rebuilt the full native target and passed 20/20 tests. Added range-aware cache
and GPU clip-position readback checks, changed-layout offset preservation,
unchanged cache hits, changed-content rejection, end-of-buffer and SIZE_MAX
offset rejection, and immutable backing retention through invalidation. The
previous live smoke test predates this range change; no live range-usage or
performance result is claimed for this build.

Next producer migration: [native-model-construction.md](native-model-construction.md)
records the Ghidra-checked replacement of VB/IB constructor header bookkeeping,
its corrected runtime integration failure, and a passing Mission 1 intro smoke
test. That later build includes offset snapshots, but the smoke test does not
measure actual offset-stream usage. Payload pool storage and comparisons remain.

## In-progress bulk-copy attachment guard

The 821E8320 hook now enters `NativeBufferWrites::WriterScope` before invoking
the original copy when its destination maps to guest physical memory. The scope
remains active through the existing completed-write notification, and leaves on
normal return or exception. No renderer lock is taken and the scope allocates
no memory. Zero-length/nonphysical/native-disabled copies do not enter it.

Observation tokens include a monotonically increasing writer epoch. During any
active scope, Version returns no token and CommitObserved rejects attachment.
Afterward, tokens from before the scope remain invalid even if the provider
threw before its completed-write notification. Nested scopes cannot prematurely
clear the active state. Owners subscribed during a copy are also covered.
The epoch is deliberately queue-wide: unrelated physical copies may reject
attachment too. This can defer reuse; it is not a claimed performance gain.

Tests cover nesting, publication during a write, notification/drain while active,
fresh/stale commits, exceptional scope exit, disabled scopes and a real second
thread held inside a write scope. The native executable built successfully and
all 21 tests passed in 12.43s. Tests exercise the queue protocol; the production
8320 mapping/hook integration has not been playtested in this change.

This is an attachment guard, NOT guest-read exclusion: a mesh can still read
guest bytes while a provider writes, and a failed registry attachment does not
cancel that draw. No cache-hit comparisons were removed. Other bulk helpers,
raw stores and imported writers still need corresponding boundaries, followed
by a coherent snapshot-consumption protocol and complete writer coverage.

Provider triage this turn also inspected the three direct XeCryptSha imports:
LR 821FBB08 uses SP+96, 821FBBEC uses SP+144, and 82423D80 uses SP+96 as r9
output, each with r10=20. The inspected SDK XeCryptSha implementation reads its
inputs and copies at most 20 digest bytes to that output. These sites therefore
write stack scratch, not the input/model payload. This is a direct-site result,
not an exclusion of indirect calls, corrupted stacks or downstream copies.

### Extend scopes to the other known bulk providers

`BeginNativeBufferWrite` centralizes native-enabled/base/physical-extent checks.
In addition to 821E8320, scopes now surround 821E8740 forward copy, the backward
branch of 821EA320, 821E9BA0 fill, NtReadFile, and RtlFillMemoryUlong. Forward
memmove delegates to the already scoped 8320; equal-source/destination does not
open a write scope. Word fill uses its actual whole-word extent. NtReadFile
uses the requested extent and retains its existing all-return-status completed
notification. This depends on the inspected SDK's synchronous provider body,
not the return status alone. Genuine future asynchronous writes need completion-
lifetime tracking rather than a function-return scope.

The fixture now extracts both copy hook bodies as well as the existing move,
fill and import adapters. It substitutes physical mapping with a controlled
helper but uses the real WriterScope queue. Tests check scope presence inside
controlled main-copy/file/word-fill providers, through completion notifications,
and absence after return. Existing alignment/length/overlap/guard-byte and ABI
tests exercise the extracted forward-copy/move/fill wrappers. An injected file
provider exception after writing one byte verifies scope release and rejection
of the pre-write token without falsely reporting normal provider completion.

Initial fixture linkage failed for the main-copy import symbol; the controlled
provider was given a dedicated fixture endpoint. After correction the full
build succeeded and all 21 tests passed in 12.34s. No live run or performance
measurement was performed. These guards still do not lock snapshot reads,
cover every raw/imported writer, or authorize removing the comparisons.

### Live regression of bulk-writer scopes

Validated executable SHA256
`2A4597720811F1215AAE0F28E6E86E22F7EE77A2762B94BF22122BC1FEE88BBB`
in `out/native-bridge-run/binding-validation-20260910-223710-8d73fb08/`, fresh
profile, HookTimings and RetirementAudit. Owned PID 52480 started at 22:37:10.
The inspected `gameplay.png` shows Mission 1 player/HUD, soldiers/city and AF14
120/120. Final indexed sample at 22:39:41.713 reports 6,276,000 submissions,
717 mesh builds, 378 entries, zero upload errors, and zero vertex/index
mismatches. No error/critical/audit-failure, cursor-reset or transaction-counter
exhaustion rows were found. The run includes the preceding submission-generation
guards, but does not deliberately exercise a reset from an observer callback.

This is bounded startup/loading/intro/gameplay-scene regression, without player
movement/fire, not whole-mission coverage, a race stress test, or an FPS result.
The host reported DXGI occlusion; large submission counts are not visible frames.
Stopped only the exact path/start-matched process; WaitForExit returned true and
process absence was checked. All geometry comparisons remain enabled.

## Guard publication reads against tracked providers

`NativeBufferWrites::CopyObserved` now validates the registered physical start
and full byte extent, rejects unknown owners or any active tracked writer, and
copies the source into an immutable host vector while holding the same mutex
that WriterScope needs to enter. Its returned observation token describes that
copy. It does not wait for an active provider: publication already holds the
renderer registry mutex, which a provider might need before returning. No guest
callback or GPU operation is performed under the queue mutex. The lock covers
host allocation/copy only; allocation failure propagates with RAII lock release.

Model VB/IB publication uses this operation for physical buffers. A rejected
copy leaves metadata without a retained snapshot; there is no unguarded physical
publication-read fallback. VB retains the vector directly. IB builds its D3D11
resource from that vector after releasing the queue lock, then validates the
observation again when attaching it. Nonphysical publication retains its prior
behavior and is outside this physical-writer exclusion contract.

Tests cover idle/busy/unknown/retired owners, exact address/extent rejection,
alias publication during a write, immutable old generations, stale commits and
owner reuse. A concurrent writer performs 2,000 split-half updates with a yield
between halves while the reader attempts 2,000 guarded copies; accepted copies
must be uniform, and the final copy must contain the final full update. Rendering
tests consume a guarded VB snapshot and construct/attach an actual D3D11 index
buffer from the guarded host copy. Full build succeeded; all 21 tests passed in
12.42s. No game run has validated this publication-copy change yet; the preceding
live run predates it.

This establishes read exclusion only at this publication boundary and only
against scoped providers. Indexed cache hits/construction still read live guest
spans, and raw/unclassified writers remain outside the guard. Byte comparisons
remain enabled. The index publication path temporarily adds one CPU source copy;
there is no performance improvement claim.

## Track the known six-store inline index producer

Rechecked the existing Ghidra export for 8242D2B0 against generated shard 10.
The lock call returns at 8242D3A8; stores at D3B0/D3C0/D3C4/D3C8/D3CC/D3D0
write uint16 values 0..5 through the returned pointer. Unlock returns at D3DC.
No other store or callback intervenes in that twelve-byte payload interval.
The producer continues VB/declaration creation and its virtual callback only
after unlock. This is the previously identified generic small-buffer writer,
not a newly proven writer of model-constructor-owned geometry.

Added a native producer hook around 8242D2B0 that owns a stack/TLS
NativeBufferWriteFrame. The 82134A78 lock hook, only for LR 8242D3A8, begins
a physical writer scope over the returned pointer's twelve-byte range after
the original lock returns. The existing 82134AD8 unlock hook, only for LR
8242D3DC, checks the same owner and finishes the scope after original unlock
and alias invalidation. CPU instructions, six stores, register results and
resource-creation order remain in the original producer. Other lock callers
are not silently assigned this extent.

The frame restores its enclosing frame on nested calls and releases an active
scope on exceptional unwind. A normal return without unlock, missing producer
frame, duplicate begin or wrong owner fails explicitly. Nonphysical destinations
still track the call sequence with an inactive queue scope. This does not give
permission to retire/reuse payload memory while arbitrary saved aliases exist.

Tests cover nesting, wrong-owner/duplicate-lock rejection, frame/thread isolation,
normal unlock, missing unlock, exceptional unwind, stale observation rejection
and inactive mapping. Full native build and all 21 tests passed (12.55s).
A subsequent source gate checks the normalized producer fingerprint and required
hooks so changed stores/call sites force re-audit; that focused gate passed.
These tests do not execute the actual producer through a game callback. No live
run has validated this hook or the preceding guarded publication-copy change.
Draw-time reads and unclassified writers remain open; comparisons stay enabled.

### Combined movement/fire regression

Validated SHA256
`D2F39973E24EF294310BC30B2AB069823955B8153F351ED5DE276932180FB8DA`
with fresh profile, HookTimings, RetirementAudit and the existing
`tools/native-movement-fire-input.txt`. Run directory:
`out/native-bridge-run/binding-validation-20260910-225038-cea683e1/`.
Owned PID 55468 started at 22:50:38. Inspected `before-input.png` shows Mission
1 introduction/player/HUD with AF14 120/120; `firing.png` shows movement down
the street, firing and 030/120; `after-input.png` shows the player beside the
median, distant effects and 100/120. Input logs confirm all three analog phases
at 160029/180002/195017ms and return to neutral at 210002ms.

The final indexed sample at 22:54:50.369 reports 19,321,000 submissions, 767
builds, 428 entries, zero upload errors and zero VB/IB mismatches. Both game.log
and rotated game.1.log were checked; no error/critical, inline-writer sequence,
counter-exhaustion or audit-failure rows were found. game.1.log retains startup
from 22:50:39.279. The host reported DXGI occlusion, so counts are not unique
visible frames and this is not a performance measurement.

The run includes guarded physical publication and the inline producer hooks.
There is no dedicated positive inline-hook counter in this build, so absence
of guard failures does not prove that exact producer executed. This remains a
bounded movement/fire regression, not complete writer coverage, concurrent-write
stress, full mission, or console-image fidelity verification. After exact
path/start validation, the owned process was stopped; WaitForExit was true and
process absence was checked. No game remains from this run. Comparisons remain.

Next alias frontier: [geometry-subset-wrapper-ownership.md](geometry-subset-wrapper-ownership.md)
records concrete shallow copies of model VB/IB wrappers in subset-container
construction/assignment. Live versus empty-template use and allocation lifetime
must be classified before treating constructor publication as complete coverage.

## Exceptional writer exit invalidates retained geometry

WriterScope now compares the uncaught-exception count at entry and exit. An
escaping exception can mean a partial write skipped its normal completion
notification. Under the existing queue mutex, unwind marks the pending batch
as all-dirty and advances every subscribed revision before releasing the active
writer count. It neither allocates nor calls the renderer. Because a scope does
not retain an exact extent, this intentionally invalidates all retained geometry.
Aborted scopes have a separate batch counter, not successful-provider attribution;
nested unwinds may conservatively invalidate more than once. A normal exit,
including an exception caught inside the scope, does not trigger this fallback
and still relies on the existing completion notification contract.

Tests cover revision advancement, pending/all-dirty delivery, nested failures,
normal exit, and stack/TLS inline-producer unwind. The extracted NtReadFile hook
fixture writes one byte and throws; its abort batch reaches model ApplyWrites
and invalidates the owner without reporting successful provider completion.
Full native build succeeded and all 21 tests passed in 12.54s. No live game run
was performed for this change; exception recovery is not certified. This closes
the missing dirty notification on exceptional scope exit, not untracked writers
or draw-time read exclusion. Geometry comparisons remain enabled.

## Coherent multi-resource snapshot primitive and draw-path findings

The indexed hook still takes submissions then registry/context mutexes before
creating guest VB/IB spans. NativeMeshCache::Acquire reads both on cache hits;
before_snapshot is called only on construction/update, and only samples version
tokens. It cannot retroactively protect those reads. Wrapping Acquire in the
queue mutex is invalid: the observer reenters the queue and construction issues
D3D calls. Waiting on an active writer under the registry mutex is also invalid:
NotifyNativeBufferUpdate takes that mutex before an inline writer's Finish.
Throwing a busy-snapshot result through the current indexed catch is not an
acceptable retry protocol; a draw must not silently become an unsubmitted guest
packet path. Releasing locks to retry additionally requires preserving command
order, resource lifetimes and the bound state, not just reusing saved pointers.

Added CopyObservedSet for a fixed set of full registered resource extents. It
validates every owner/start/size before copying any bytes, then copies all sources
under the same writer-entry mutex and assigns a common writer epoch. An active
provider or invalid owner rejects the entire set. The existing single-buffer
publication method delegates to its one-element form, retaining its behavior.
This is coherent only relative to tracked scopes, not proof of raw-store coverage.
Two separate CopyObserved calls would not by themselves give this shared boundary.

Tests exercise paired VB/IB capture, busy rejection after only VB has changed,
invalid second extent, retained immutable generations, retirement/reuse, and
2,000 paired provider updates with yields between VB and IB against 2,000 reader
attempts. Accepted pairs must have uniform matching contents and epochs; final
contents are checked after joining the writer. The D3D fixture builds a mesh
from an observed pair after the backing sources change, checks retained original
VB/IB data and draw bounds, and rejects attachment of its now-stale index token.
All 21 tests passed after a full build in 12.35s. No live game was launched.

The multi-resource form is preparatory and not yet connected to production draw
submission. It does not reduce comparisons or improve FPS. The next integration
must provide ordered busy-writer handling outside renderer locks and a validated
snapshot-reuse policy; copying whole geometry on every draw is not the intended
final native ownership path. Unclassified writers remain a separate blocker.

## End inline payload scope before renderer bookkeeping

The concrete inline completion lock dependency is now removed. Frame Begin
retains the exact virtual destination returned by the audited index lock.
After original 82134AD8 returns, its native hook queues that destination's twelve
completed bytes while the writer scope remains active, then finishes the scope,
then calls the existing NotifyNativeBufferUpdate. A draw that acquires renderer
state before this last notification can drain the exact queued write. Existing
owner/alias bookkeeping still runs; duplicate conservative invalidation is allowed.
If the queue notification throws, Finish does not release the scope, allowing
the enclosing producer's exceptional unwind to invalidate all retained geometry.

Original unlock inspection: 82134AD8 loads owner+24 and tail-calls 82134640 with
r5=0. The latter retains atomic header bookkeeping and its conditional 82141AB8
cache-range call. These original instructions are not removed by this change.
The scope still covers original unlock. This audit does not certify every
original/provider dependency as wait-safe or authorize blocking inside a draw.

Tests use two threads with the renderer mutex deliberately held: the writer
queues its completion and finishes before attempting that mutex; the reader
can then copy the completed bytes and drain their exact range while still
holding the mutex. Additional tests cover a throwing completion callback.
The fixture extracts the actual 82134AD8 hook, replacing its original provider
and notifications with controlled endpoints; it checks scope/order, preserved
original return value, entry owner after register clobber, captured destination,
twelve-byte extent, and the unchanged nonmatching-caller path. Initial fixture
declaration/linkage errors were corrected. Full build and all 21 tests passed
in 12.32s. No live game run was performed. No comparisons were removed.

## Native constant-time cache-maintenance endpoint

Inspected original 82141AB8, called by the retained 82134640 unlock bookkeeping.
It is not a GPU packet producer: dcbf and sync have no generated host operation.
The emitted body nevertheless walks eight-line groups and a remainder, updating
volatile registers/condition state and stack scratch at SP-8/-12/-16. It neither
reads nor writes the addressed payload. This is distinct from the device dirty-
range packet helper 8213C5F0 that was replaced earlier.

Native bridge mode now hooks 82141AB8 with NativeCacheFlushCpu. The helper keeps
the early-return interval (0x7f100000..0x86ffffff inclusive), aligned signed line
calculation, final 64-bit register arithmetic, carry/condition state and scratch
values, computing loop endpoints directly. Non-native mode retains the original.
Resource-header lock count/dirty-range bookkeeping in 82134640 remains original;
no CPU payload write notification, simulated GPU flush, or new renderer lock is
introduced. Intermediate repeated stack scratch stores are intentionally omitted.

572 differential cases compare the complete PPCContext and fixture memory with
the extracted original, covering address/early-return boundaries, wraparound,
alignment, zero/partial/full groups, sign-extended arguments and initial carry/SO.
These are bounded cases, not exhaustive input proof or concurrent stack observers.
A source gate pins the original normalized body to SHA256
fea0e017e1244dd6f5c58a9d95941fcf47331c1e48c61ebcb2350ddb8b55f9ae
so newly meaningful generated cache operations force re-audit. Full native build
and all 21 tests passed in 12.15s; git diff --check passed with pre-existing line-
ending warnings. No game was launched and no FPS gain was measured. This removes
obsolete cache-line iteration in native mode, not steady geometry comparisons
or the remaining snapshot-integration/writer-coverage blockers.

## Combined live regression after writer failure/order and cache-loop changes

Validated executable SHA256
950C5273BB0D8615D7F94D23563B49AA8BEED6A5D2783289BBDA25A025279B03
with fresh profile, HookTimings, RetirementAudit and native-movement-fire-input.
Run: out/native-bridge-run/binding-validation-20260910-232747-6a14979c/.
Owned PID 54612 started 2026-09-10 23:27:47. before-input.png shows the bright
mothership/city introduction; firing.png shows player movement, firing effects
and AF14 111/120; after-input.png shows the player beside the median, AF14
100/120 and neutral input. These images do not establish console image fidelity;
the very bright introduction remains an outstanding visual comparison question.

Both game.log and rotated game.1.log were inspected. Scripted analog phases
occurred at 160019/180009/195011ms and neutral at 210017ms. Final indexed sample
at 23:31:59.477 reports 19,306,000 submissions, 769 mesh builds, 430 entries,
zero upload errors and zero VB/IB mismatches. No error/critical, audit-failure,
inline-sequence or counter-exhaustion rows were found. The window was reported
DXGI-occluded; submission counts are not visible frames and no FPS/latency gain
is established. There is no dedicated positive cache-flush/inline-hook execution
counter, so this run is bounded integration regression, not exact hook coverage,
intentional failure injection, full-mission coverage or writer completeness proof.

Stopped only the path/start-matched owned process; WaitForExit(5000) returned true
and process absence was confirmed. No game remains from this run. This validates
the accumulated changes sufficiently to resume implementation, not to remove
comparisons: production draw snapshot integration and unknown writer coverage
remain open. No source changes were made during the live run.

## Consume native snapshot identity without rescanning its own bytes

NativeIndexBuffer now retains an immutable shared CPU snapshot, matching vertex
storage ownership. Its optional contents argument must own exactly the supplied
span (pointer and full extent), otherwise construction rejects it. Physical index
publication passes the guarded CopyObserved contents directly, removing the
second vector copy previously made inside NativeIndexBuffer. Non-owned input
still gets copied before conversion. GPU construction remains outside queue locks.

Index Matches recognizes its own retained span only with matching device, width
and full extent. Vertex MatchesSource recognizes only its retained span at the
exact conversion offset and length. All other inputs still compare bytes. CPU
snapshot reuse during vertex construction likewise skips equality only for an
identical native span. These identities are addresses of live retained host
allocations, not guest resource handles, guessed immutability or write versions.
Callers must uphold the existing immutable-contents contract; mutable aliases to
an explicitly supplied const snapshot are not supported.

Mesh-cache accounting separates native identity hits from byte-check counts and
candidate-byte extents. A D3D test builds/reuses a mesh from retained VB/IB spans
and asserts one identity hit per stream with zero byte checks. It also rejects
wrong index contents identity, wrong width, partial ranges and unrelated pointers.
Existing guest-backed cache tests continue requiring VB/IB byte comparisons;
stale-byte tests remain enabled. Physical publication's shared index snapshot
identity is explicitly tested. Full build and all 21 tests passed in 12.78s.
No live game was launched; the preceding runtime validation predates this change.

Production draws still supply guest spans, so their steady comparisons remain.
This establishes the native-only cache consumption path for forthcoming paired
snapshot integration, without blanket comparison bypass or a measured FPS claim.

## Connect guarded pairs to production indexed draws

The indexed hook now attempts CopyObservedSet when both bound model resources
have registered physical extents. It supplies the full VB and IB with retained
CPU snapshots as candidates. Under the writer-entry mutex, each candidate is
still byte-compared against guest memory; equality reuses that immutable vector,
otherwise a fresh vector is copied. All source metadata is validated first and
both observations share one tracked-writer boundary. Thus this does not copy
all unchanged geometry each draw or trust notification coverage prematurely.

On success, Acquire receives only the resulting native spans (with stream offset
applied to the full VB), and matching mesh storage can use the native identity
path. The before_snapshot callback preserves the pair's original tokens rather
than resampling a later epoch. Registry attachment still rejects subsequent
tracked writes. The queue mutex is released before GPU/cache construction.
Tests verify candidate identity reuse, detection of an unreported byte change,
and active-writer rejection even with retained candidates; existing pair/GPU
tests cover immutable consumption and stale attachment rejection.

This is deliberately incomplete busy-writer integration: unavailable pairs still
use the existing live validation path, as do resources outside registered physical
model ownership. No busy wait, dropped draw, new exception or reordered retry was
introduced. The original live-read race on those paths is not solved. Optional
guest watch/color diagnostics also remain outside snapshot read exclusion.
New sampled guarded/unavailable counters expose this frontier for live validation.

Full build and all 21 tests passed in 12.54s. No game run yet validates this new
production connection. The next run must inspect guarded/unavailable counts and
geometry results; this is not permission to claim comparison removal. For guarded
steady inputs, guest comparisons have moved into snapshot acquisition and mesh
cache identity can avoid a second scan. Complete writer coverage and ordered
busy-writer handling remain necessary before removing those guest comparisons.

## Live validation of production paired snapshot consumption

Validated executable SHA256
9D16882CEF75C472632E9073DF7FB207502D630BBFFE35C12D00DD3971944D26
using fresh profile, HookTimings, RetirementAudit and movement/fire script.
Run out/native-bridge-run/binding-validation-20260910-233829-ab190ade/;
owned PID 49436 started 2026-09-10 23:38:29. Inspected before-input.png shows
Mission 1 player/HUD at AF14 120/120; firing.png shows forward movement under
the footbridge and AF14 112/120; after-input.png shows the median, distant green
enemy effects and AF14 101/120. Analog phases were logged at 160017/180002/
195010ms, followed by neutral at 210005ms.

The 23:42:30.087 acquisition sample reports 16,776,316 guarded successes and
900 unavailable attempts (16,777,216 total attempts at this sampled boundary).
This positively establishes that production draws use the new path and that
the fallback is genuinely exercised. The counter aggregates all rejection
reasons; it does not prove every rejection is an active writer. At 23:42:18.321
mesh cache byte-check counters were only 870 per stream, but the guarded path
still compares guest candidates in CopyObservedSet. Lower cache counters are
NOT removal of the guest scans, total memory traffic, or an FPS improvement.

Final indexed sample at 23:42:36.113: 17,584,000 submissions, 767 builds,
428 entries, zero upload errors and zero vertex/index mismatches. Both game.log
and rotated game.1.log were checked; no error/critical, audit failure, inline
sequence failure or exhausted-counter rows were found. The hidden/occluded run
is not a frame-pacing or visible-FPS benchmark, full mission, or complete writer
coverage test. No source was changed during execution. Exact path/start-matched
process was stopped, WaitForExit(5000) returned true and absence was confirmed.
Next: classify snapshot rejection reasons before replacing the live fallback
with ordered handling; do not infer that rare rejection means it can be ignored.

## Classify rejected snapshot acquisitions

CopyObservedSet optionally returns a mutex-consistent failure classification:
unknown tracking, active writer, missing owner, or extent mismatch. It clears
prior failure state on each call and includes the relevant owner for metadata
failures and active scope count. Unknown tracking takes precedence over active
writers; this is a reason for rejection, not a list of every condition present.
Allocation exceptions still propagate and are not mislabeled as busy. Production
logs cumulative per-reason counts at the first eight and power-of-two rejections.
Tests cover missing/extent classification, success clearing, active candidates
and nested active counts. Build and all 21 tests passed in 12.35s. The permanent
unknown-tracking allocation/overflow condition was not injected by these tests.

Short stationary Mission 1 run:
out/native-bridge-run/binding-validation-20260910-234456-974a0471/,
SHA256 E67D53B969C8745634C53C8D3B163B4F9C7DC9D0E0844AC9D70DD8F48F868163,
owned PID 52784 started 2026-09-10 23:44:56. At 23:46:54.666, all first 64
rejections were ActiveWriter, with zero unknown/missing/extent failures; the
sampled active count was one. This establishes actual write/read overlap at the
queue level, not overlap with the requested geometry: scopes are still global.
Next work should identify writer extents/admission dependencies and avoid stalling
unrelated geometry, while retaining conservative handling of unknown ranges.

Final indexed sample at 23:47:09.078 reports 3,769,000 submissions, 717 builds,
378 entries, zero upload errors and zero VB/IB mismatches. Inspected gameplay.png
shows Mission 1 player/HUD, soldiers, ships and AF14 120/120. No error/critical,
audit failure or counter-exhaustion rows were found. This was stationary startup/
intro/gameplay-scene classification, not movement/fire, full mission or an FPS
benchmark. Stopped the exact path/start-matched process; WaitForExit returned true
and absence was confirmed. No game remains. No fallback or comparison policy was
changed in this turn; the new evidence narrows the next synchronization work.

## Range-aware active write exclusion

WriterScope now optionally records a checked physical range in an intrusive
doubly linked list owned by the queue. Linking/unlinking is allocation-free and
serialized with snapshot reads. Non-LIFO destruction is supported; invalid,
empty or overflowing extents become unknown, not an exclusion bypass. Exceptions
still invalidate all subscribed geometry conservatively. Scope entry epochs and
Version/CommitObserved remain queue-global and conservative for attachment.

Snapshot acquisition validates all source metadata first, then rejects only
overlapping or unknown active ranges. Half-open physical intervals use widened
end arithmetic, so adjacent unrelated writes are allowed. The mutex remains
held through candidate validation/copy, preventing a new overlapping writer from
entering during the read. Failed acquisitions report counts of overlapping and
unknown scopes separately. Metadata failures now precede active-range rejection;
unknown tracking still takes precedence over both.

The checked mapper supplies exact extents only to the audited 821E8320/821E8740
copies, backward 821EA320 and 821E9BA0 fill. The forward move's nested copy is
already scoped; equal-pointer move has no write. This relies on those reviewed
payload extents, not arbitrary provider output discovery. NtReadFile, word-fill
import and inline producer retain unknown ranges for now; do not infer their
additional outputs/lifetimes are certified by the primary destination alone.
The extracted adapter fixtures now model the explicit exact/unknown selection.

Tests cover unrelated/adjacent ranges, one-byte overlap, simultaneous unknown
and exact scopes, malformed ranges, non-LIFO exits, preserved conservative
attachment rejection, and 2,000 paired ranged writes against snapshot attempts.
Full build and all 21 tests passed in 12.94s; diff check passed with existing
line-ending warnings. No game was launched after this exclusion-policy change.
Next live validation must inspect remaining overlapping/unknown rejections.
Actual overlapping/unknown busy attempts still use the previous live fallback;
writer completeness and final guest-comparison removal remain unfinished.

## Live validation of range-aware exclusion

Validated SHA256
52A9BBF4593BE8D64D4C75666520911CED3BC88DAE0FCB9812C960FB1F5F9822
in out/native-bridge-run/binding-validation-20260910-235239-b35156e6/ with
fresh profile, HookTimings, RetirementAudit and movement/fire input. Owned PID
36748 started 2026-09-10 23:52:39. Inspected before-input.png shows Mission 1
player/HUD at AF14 120/120; firing.png shows movement and AF14 028/120;
after-input.png shows the median, distant enemies, AF14 100/120. Input log
confirms turning phases at 180006/195010ms and neutral at 210001ms.

At 23:56:37.075 the power-of-two acquisition sample reports 16,776,640 guarded
and 576 unavailable attempts. Rejection counters at the first 512 failures
classified all as ActiveWriter, none as unknown tracking/missing owner/extent.
The range-detail samples showed overlapping=0, unknown=1; these detail fields
are snapshots, not cumulative classification of every failure. This points to
the still-unknown provider scopes as the next audit, not proof that every failed
attempt had that cause. Provider identity is not yet included, so distinguish
NtReadFile, word fill and inline producer before narrowing those exclusions.

Final indexed sample at 23:56:43.553: 17,588,000 submissions, 767 builds,
428 entries, zero upload errors and zero VB/IB mismatches. Both current and
rotated logs were checked with no error/critical/audit-failure/exhaustion or
nonzero mismatch rows. This is a bounded movement/fire regression, not full
mission or writer completeness. The diagnostic run does not establish visible
FPS, pacing, memory-traffic reduction or image fidelity. Stopped only the exact
path/start-matched process; WaitForExit returned true and absence was confirmed.
No source changes during execution and no game remains. Guest candidate scans
and the actual-overlap/unknown-range fallback remain enabled.

## Identify unknown-range providers

Active scopes now carry a bounded WriterKind tag: unspecified, bulk, file read,
word fill or inline indices. Failed snapshot acquisition counts unknown ranges
by provider under the queue mutex; the bridge logs these counts at sampled
rejections. Tags do not change ranges, exclusion, notifications or fallback.
Tests cover every provider tag and clearing counts between failures/success.
The actual import adapters and frame supply their tags; full build and all 21
tests passed in 12.68s, and diff check passed with existing line-ending warnings.

Short live classification run
out/native-bridge-run/binding-validation-20260911-000002-8b3e3cb9/,
SHA256 A647F160887DB316328436998E2B12A42F72C740D9D2E41152B21799FD39DF05,
owned PID 21640 started 2026-09-11 00:00:02. Provider-tagged rejection samples
through rejection 32 show file_read=1, all other providers zero. This identifies
NtReadFile as the observed blocker, not proof about every unlogged rejection.
Final indexed sample at 00:02:18.459: 4,079,000 submissions, 717 builds,
378 entries, zero upload errors and VB/IB mismatches. No error/critical/audit-
failure/exhaustion rows were found. This short stationary run was for provider
classification; no screenshot, movement/fire, full-mission or FPS claim. Stopped
only the exact path/start-matched process; WaitForExit returned true and absence
was confirmed. No game remains and no source changed during execution.

Re-read SDK xboxkrnl_io.cpp NtReadFile_entry: XFile::Read executes synchronously,
then writes status/information to the optional I/O status block; invalid-handle
and failure paths can also write that block. The function may enqueue an APC
and signal an event. Thus narrowing the primary data destination alone is not
a complete output audit. Next inspect the Read/APC/event paths and all secondary
outputs before narrowing this scope. The word-fill body, separately inspected,
only stores floor(length/4) swapped words; it was not the sampled blocker.

## File-read secondary output guard

Further SDK inspection: XFile::Read holds file_lock_ through ReadInternal. The
physical data branch calls ReadSync and then physical invalidation callbacks on
success. Completion ports enqueue a host notification under their mutex and
release a host semaphore; XEvent::Set forwards to its host event. EnqueueApc is
different: it allocates a guest XAPC, initializes it, and links it to the guest
thread APC list. The scheduled host callback is only a wakeup hint, with actual
guest APC routines delivered later by DeliverAPCs. These bodies do not establish
the complete physical-alias/lifetime exclusion of APC/thread/list writes or all
memory callback bodies. Narrowing the entire import to just its data range is
therefore still unjustified on this audit alone.

Fixed a concrete missing output: NtReadFile's optional r7 I/O status block is
eight bytes (status/pointer union plus information in SDK xio.h). The wrapper
now captures that pointer before the provider, opens a separate conservative
scope when its extent maps physical, and notifies its eight bytes after return
on every status. This also protects status-only physical writes when the data
extent is empty or nonphysical. Null status pointers add no active scope/write.
The original provider's return/register state is unchanged. Both scopes remain
unknown-range until the complete import boundary is safe to narrow.

The extracted import fixture writes both outputs and clobbers r7/r8/r9, testing
captured-pointer notification, zero/nonzero data lengths, success/pending/failure
results, exact status range, revision invalidation and stale-token rejection.
Existing partial-failure and data-only tests still pass. Full native build and
all 21 tests passed in 12.74s; diff check passed with existing line-ending warnings.
No game was run this turn. This closes one missing writer notification; it does
not claim all file-read completion writes are covered or remove any comparison.

Follow-up allocation/list audit is recorded in
[native-file-read-completion-ownership.md](native-file-read-completion-ownership.md).
New APCs, created thread objects and normal PCR storage use the virtual system
heap. Queue insertion additionally writes the existing tail node, so that fact
alone does not exclude every indirect destination. EDF's generated direct imports
contain no named guest APC queue APIs; physical invalidation callbacks remain
another concrete review boundary. No runtime policy changed for this research.

The follow-up in that audit completes the identified physical invalidation
callback bodies and introduces exact data/status ranges for NtReadFile calls
that cannot enqueue an APC. APC-capable calls remain conservative. Actual-hook
fixture tests cover the routine low-bit/context decision and unrelated snapshot
admission during the provider; all 21 tests passed in 12.48s. No live run of
this change yet. Guest comparisons and overlapping/unknown fallback remain.

## No-APC live validation and exact SDK word-fill ranges

The subsequent short stationary diagnostic used
out/native-bridge-run/binding-validation-20260911-001319-e83c5318/,
SHA256 2D9383E49F429515473E629437C1A54E46815C4B163888FFBD07CC920007B935.
Its 00:17:17.925 acquisition sample reached 16,777,216 guarded attempts with
zero unavailable. The indexed sample at 00:17:48.364 reached 20,986,000
submissions, 718 mesh builds and 379 entries, with zero upload errors or VB/IB
mismatches. This supports the no-APC range narrowing in this bounded workload,
not complete writer coverage, visible FPS or fidelity. The path/start-matched
owned process 53920 was stopped; WaitForExit succeeded and a later process
lookup confirmed absence. The run predates the word-fill change below.

Re-read SDK xboxkrnl_rtl.cpp RtlFillMemoryUlong_entry (lines 76-85): its only
guest output is the destination loop of length >> 2 swapped 32-bit words.
There are no secondary outputs, nested providers or callbacks in that body.
The native adapter now requests an exact mapped range of length & ~3u, rather
than an unknown/global range. It retains the original provider, captured entry
arguments, completion notification, and conservative malformed-range handling.

The actual extracted hook fixture now tests snapshot admission inside this
provider: unrelated geometry and the first unwritten trailing byte are readable;
an overlapping registered owner is rejected with one exact overlap and zero
unknown writers. Empty fills remain inactive. Existing lengths 0, 1, 3, 4, 5,
7, 8, 31 and 32 also check return registers, notification extent and unchanged
trailing bytes. Full native build and all 21 tests passed in 12.51s. No live
gameplay validation or performance claim for the word-fill change.

The remaining inline producer is not equivalent to a bare 12-byte store loop.
The existing Ghidra export for 82134640, called by 82134AD8 unlock, includes
an atomic header write at owner+0 and conditional stores at owner+0x14 and
owner+0x18, in addition to calls to 82141AB8. Any exact-range replacement must
account for that header's physical mapping/aliases and the complete lock/unlock
boundary, not just the six index stores. Its conservative scope stays enabled.
APC-capable file reads, complete raw-writer coverage and overlapping-write
fallback are also still open. Guest snapshot byte comparisons remain enabled.

## Inline index unlock header coverage

Read Ghidra exports 82134A78, 82134408, 82134AD8 and 8242D2B0 alongside the
current hooks. Index unlock loads r4 from owner+0x18, sets r5=0 and tail-calls
82134640. Consequently its header outputs are the atomic word at owner+0 and
conditional dirty-range reset at owner+0x14; the helper's owner+0x18 store is
unreachable on this wrapper. Its cache-flush calls use the already-native
82141AB8 hook. The original helper's stack writes are not header outputs.

For the identified inline producer, the unlock adapter now opens a separate
exact mapped owner/24-byte header guard before the original unlock and queues
that extent after return, while still guarded. This independently covers a
physical header even when its payload scope is inactive. Malformed mappings
remain conservative. The header scope ends before the payload Finish and before
renderer state locking. Payload completion remains inside its original scope.
No lock/unlock CPU semantics or native buffer update is removed.

The extracted actual-hook test covers tracked/untracked callers and both active
and inactive payload scopes, captured owner despite provider r3 clobber, header
then payload notification ordering, exact queued extents, and all scopes released
before renderer bookkeeping. The full native build and 21 tests passed; no game
was launched for this change.

This does not yet narrow the payload frame or cover lock-entry header writes.
82134A78 calls the shared lock helper before the existing payload frame begins.
The native 82134408 CPU tail retains header dirty-range/atomic updates and fence
handling while omitting Xbox cache packets. The index wrapper forces flag bit 2,
excluding the original cache-packet branch even independently of that native
replacement. Full lock-entry tracking must preserve and audit the retained fence
path (8213C928) rather than treating lock entry as only a header store.

## Exact post-lock inline payload interval

Inspected current 8213C928 native hook and guest_fence.h, not only retail Ghidra:
WaitNativeResourceFence reads guest fence state and delegates submission and
wait accounting. The end-accounting hook 82139508 can resolve and invoke a guest
callback. Polling publishes completion and progresses worker signals. Thus a
whole-lock exact-header-only classification would exclude retained operations
without proving their writes. Lock-entry coverage remains a separate audit.

That wait has returned before 82134A78 opens the existing payload frame. Between
this return and index unlock, 8242D2B0 writes only the six identified halfwords;
the original index unlock's header writes now have their own exact guard.
NativeBufferWriteFrame::Begin therefore accepts an optional physical range, and
the production inline lock hook supplies the mapped 12-byte payload range.
The frame still retains the original guest destination for completion notification;
invalid mappings remain unknown, and unmapped payloads retain an inactive scope.
Default frame callers without a range remain conservative.

Tests check adjacent-range admission, exact overlap rejection with zero unknown
writers, distinct guest/physical addresses, completion inside the scope and
post-completion contents. The renderer-lock concurrency test now uses an exact
payload range. The extracted actual unlock-hook tests also use exact payload
frames and verify independent header-overlap rejection with both active and
inactive payload guards. Full build and all 21 tests passed after these changes.
No live game was launched, and no FPS improvement is claimed.

This removes the normal inline payload's unknown/global exclusion, not guest
byte comparisons. Pre-lock/header writers, fence callbacks, APC-capable file
reads, remaining raw-store/alias coverage and actual-overlap fallback remain.

## Fence accounting writer and consumer audit

Current native_fence_records.h redirects only the six opaque record words to
host storage. StoreDoubleWord explicitly forwards non-record accesses to its
guest reader; it does not notify NativeBufferWrites. EndNativeFenceRecord in
native_fence_poll.h writes an eight-byte accumulated tick counter at device+20032
for kind 3, or device+20024 for other nonzero kinds. Kind zero exits without this
write. It then reads device+13068 and may invoke the retained profile callback.
Do not classify all accounting storage as host-owned merely because its record is.

The earlier stationary run binding-validation-20260911-001319-e83c5318 includes
sampled native fence records with device=40002780 and callback=00000000, including
kind 14 and kind 13. This observed device is outside the physical geometry aliases;
these samples are not proof of all device allocations or a permanently null
callback. A search for literal 13068 found its generated read, but no direct
positive-offset setter; that does not exclude initialization, indexed/bulk stores
or indirect callback registration. No callback implementation is certified here.

Found a concrete second side of the accounting ownership boundary:
generated/default/edf2017_recomp.25.cpp sub_82139228 reads both counters, resets
them with 64-bit stores, and copies their low words to device+20040/+20044. It
also updates nearby frame-time fields. No matching native hook or counter migration
was found in current src/tools. Therefore moving the counters to host storage
only in EndNativeFenceRecord would leave a real guest consumer reading stale data.
Any ownership migration must include this consumer/resetter, or keep and track
the guest counters. Existing accounting tests verify selection, wrapping and
callback-after-update ordering, not physical-alias exclusion or writer tracking.

Next implementation boundary: isolate lock-header tracking after the retained
fence service returns, rather than wrapping all fence services as an exact header
write. Separately cover or migrate the accounting producer/consumer pair and
resolve profile callback provenance before claiming complete lock-call coverage.
This turn was source/log research only; no executable changed or game was run.

## Post-fence lock header tracking implemented

The hash-gated native 82134408 CPU tail now enters an exact mapped owner/28-byte
writer scope at loc_82134570. This point follows both retained resource-fence
services and off-thread dirty-range accumulation. Its remaining suffix updates
owner+0x14 or owner+0x18 and atomically increments owner+0; there are no retained
service calls inside that suffix. It queues the completed header extent after
the atomic loop and releases the scope before the return-register/stack epilogue.
The original guest routine remains unchanged; the extraction script inserts only
native tracking calls around the previously retained suffix. Invalid physical
mapping remains conservative and nonphysical headers add no queue scope.

The existing 576 lock differential cases now exercise the actual extracted tail
with a real NativeBufferWrites queue and a registered header. They verify that
the fence callback sees no header guard, the completed notification observes the
atomic update while guarded, the queued extent is owner/28 bytes, and the scope
ends before return. Original comparisons of header/parent state, off-thread dirty
ranges, wait arguments, return ABI and preserved registers still run. This covers
VB/IB/texture access types, parent fences, cached aliases, flags and dirty slots.
Full native build and all 21 tests passed in 12.34s; diff check passed. No live
gameplay validation was run for this change.

This closes the final header-update suffix, not every writer reached from lock
entry. Fence accounting/callbacks and off-thread dirty-range services remain
separate ownership boundaries. Geometry byte comparisons remain enabled.

## Off-thread packed dirty-range writer tracked

Read the complete generated sub_8213BDF8 body in edf2017_recomp.49.cpp.
It computes the unsigned minimum lower bound and maximum upper bound of a packed
64-bit value at r3. Both conditional-store paths target that same eight-byte
word; mismatched observations retry. There are no guest service calls, secondary
destinations or stack stores in this body. The lock's only direct call passes
device+11544 (0x2d18). The previous lock differential fixture used a simplified
provider and did not validate these atomic semantics.

The bridge now hooks 8213BDF8 with an exact mapped eight-byte write scope around
the unchanged original routine and notifies that captured extent after return.
This includes no-change unions, which still perform a conditional store.
Physical aliases get revision/completion tracking; nonphysical destinations keep
the mapper's inactive behavior, and malformed ranges remain conservative.

The test extractor includes the actual generated routine and actual hook.
256 combinations of old lower/upper bounds and incoming bounds verify packed
unsigned min/max, boundary values, input/return registers, completed extent,
scope release and rejection of stale observed versions. The notification fixture
also checks that the scope remains active during completion. These are serial
atomic tests, not forced-contention retry coverage. The existing lock differential
fixture still uses its controlled provider separately.

Full native build and all 21 tests passed in 12.45s; diff check passed. A subsequent
comment-only relocation restores the forward-copy comment to its own hook.
No gameplay run or FPS claim. Fence accounting/profile callbacks, remaining raw
writers and alias coverage, and actual-overlap fallback remain open; guest
geometry comparisons are still enabled.

## Accumulated writer-guard movement/fire regression

Rebuilt the current source and ran tools/native-movement-fire-input.txt with
HookTimings and RetirementAudit using a fresh profile. Run directory:
out/native-bridge-run/binding-validation-20260911-003345-c2136e29/.
Executable SHA256: 51F8D67BE3BBA3928F1DB38D7DFAC61C225819666925452328AF4F8E5D72D046.
Owned PID 19292 started 2026-09-11 00:33:45. No source changes during execution.
This build includes exact word-fill, inline payload/header, post-fence lock-header
and off-thread dirty-range guards added since the earlier no-APC run.

Viewed before.png (Mission 1 mothership/intro, ammo120/120), firing.png (changed
street position, ammo040/120) and after.png (further movement, ammo100/120 and
distant enemies/effects). These establish bounded intro, movement and weapon-use
coverage, not a full mission, reference-image equivalence or absence of flicker.

At 00:37:43.363 the acquisition checkpoint reached 16,777,216 guarded attempts
with zero unavailable. Final indexed sample 00:37:44.198: 16,886,000 submissions,
766 mesh builds, 427 entries, zero upload errors and zero VB/IB mismatches.
Both game.log and game.1.log were searched for error/critical, nonzero error or
mismatch counters, audit failures and exhaustion; none matched. No snapshot
fallback was reported at the sampled checkpoint. Guest candidate scans remain
enabled, so this is not proof of complete writer coverage or safe scan removal.

Stopped only the exact path/start-matched process after capture; WaitForExit
returned true and process absence was confirmed. No game remains. This hidden
diagnostic is not a visible FPS/frame-pacing benchmark and supports no performance
claim. The active next work remains complete writer/alias coverage, including
fence accounting/profile callbacks, and replacement of actual-overlap fallback.

## Fence counter accumulation tracking

EndNativeFenceRecord now accepts an accumulation service separately from profile
dispatch. The production 82139508 hook scopes the selected eight-byte counter
before its read/modify/write, retains NativeFenceRecordAccess range checks, queues
the completed physical extent and releases the scope before optional guest
callback dispatch. Mapping and malformed-range behavior use the same existing
helpers as other exact writers. The five-argument compatibility overload retains
the old direct accumulation service for callers that do not supply tracking.

Counter values remain in guest storage so 82139228's existing consumer/resetter
continues to see them. No accounting semantics, callback ABI, synchronization of
the original counter arithmetic, or elapsed-time selection has been changed.
This covers native accumulation only, not the resetter or callback body.

The 40 production-template accounting cases now supply a real writer queue and
verify the selected address/ticks, overlapping snapshot rejection, exact completed
extent, stale-version rejection, zero-kind bypass, and guard release before the
profile callback. Counter wrapping, argument values and untouched host-owned
record storage checks remain. These tests exercise the service contract with a
controlled mapping, not the full kernel/physical mapper in the production hook.
Full native build and all 21 tests passed in 12.45s; diff check passed. No live
run for this change. Full Xenos replacement and geometry scan removal remain
unfinished; the guest counter resetter, callback provenance and other writer/
alias coverage still require work.

## Counter resetter control-flow and test-boundary audit

Read the complete generated 82139228 body (recomp.25.cpp) and its sole direct
generated caller in 82151460 (recomp.26.cpp, return LR82151A08). The current built
native_present.cpp still calls 82139228; the native presentation extractor does
not remove it. It is therefore not enough to reason from removal of VdSwap or
Xenos swap-packet storage to conclude the accounting resetter is inactive.

The callback-free accounting region starts at loc_8213927C. It increments
device+20000, conditionally updates device+20004, publishes timebase halves at
device+20008/+20012, reads and clears eight-byte counters at +20024/+20032, and
stores their low words at +20040/+20044. Its device output envelope is therefore
48 bytes starting at device+20000, with untouched gaps. It also uses stack
temporaries at frame+80/+88. The only service within this region is the host
timebase query. End the device guard after the +20040 store, before the branch
to loc_82139444 or the profiling setup path; do not span the remainder of the
routine with an exact counter guard.

The wider routine has distinct writes at device+15144/+15148 and +20060..+20088,
conditional indirect callbacks before accounting, query/profiling allocation and
submission calls after accounting, then 82138858 and another optional indirect
callback. These are not covered by a 48-byte accounting-region guard. It would
be incorrect to call such an isolated guard full 82139228 writer coverage.

Current native_present_tail_tests.cpp uses STUB(sub_82139228,0x39228). Its passing
presentation tests verify that the call is retained, not the counter-reset body
or the effect of new guards there. The next implementation needs an extracted
real-body differential fixture, controlled timebase/callbacks, and explicit checks
that guard acquisition/release stay inside the accounting region. Preserve both
zero/nonzero previous timestamps, wrapping counters, the two prior callback
branches and the profiling state transitions rather than simply replacing the
whole routine with a counter reset.

This turn completed source/generated-output/test inspection only. No executable
or writer policy changed, and no new game/build run was required.

## Real counter-resetter test fixture

Added tools/extract-native-counter-reset-test.cmake, included only by the
presentation TEST_MODE extractor and declared as a fixture build dependency.
It extracts the complete 82139228 body under a test name, gates its normalized
source SHA256 c44ff0e1a7c2634e343bf167d925530b20caa881a822a9ee2c1643a86f3ef796,
and replaces only the timebase query and indirect dispatch with controlled test
services. The existing wrapper-order stub remains separate. Guest game source
and production resetter behavior are unchanged.

72 direct real-body cases cover entry flags 0/0x100/0x400, success/failure
callback returns, profiling states 0/1/2, zero/nonzero previous timestamps and
optional final callback. They verify frame-count wrap, both 64-bit counter clears,
low-word history publication, timebase halves/delta behavior, callback counts/sites,
stack/LR and preserved registers. Query/allocation services are controlled stubs;
their implementations and exact callback side effects are not certified by this
fixture. These cases establish the retail baseline; they are not yet a paired
native/retail resetter differential or tracking test.

Full native build and all 21 tests passed, including after adding the explicit
extractor dependency and CMake regeneration. No game was run. Next add the native
accounting-region guard and execute both bodies with the same controlled services,
checking notifications and release before every following service boundary.

## Native accounting reset guard and differential verification

Renamed the shared extractor to tools/extract-native-counter-reset.cmake and
included it in production and test presentation outputs, with explicit build
dependencies. Its existing whole-body hash gate remains. Native-host 82139228
now selects the extracted CPU body; host-off calls the original. The extracted
body opens a mapped device+20000/48-byte guard at loc_8213927C and queues that
extent after the device+20040 store, before the following branch. Scope release
precedes all following profiling/query services and the final callback. Earlier
callback branches rejoin outside the scope. Checked GuestReader::Add computes
the extent, and the existing physical mapper retains inactive/unknown behavior.

The original accounting arithmetic, guest values, surrounding writes, callbacks,
query work and stack/return behavior are retained. This tracks the accounting
region only, not all output destinations reached from the broader routine.

All 72 baseline cases now also execute the native extracted body from identical
initial state and controlled services. They compare full PPCContext, the full
committed 1MiB low-memory fixture, service trace and callback count. Checks cover
one exact completed notification, stale-version rejection, reset values present
while still guarded, and no guard held in indirect callbacks or traced services.
The original presentation-order stub remains separate. Full build and all 21
tests passed; diff check passed with the existing CMakeLists line-ending warning.
No live gameplay validation for this change.

Both fence accumulation and this accounting reset region now notify mapped
physical writes, preserving the guest consumer relationship. Other outputs of
82139228, profile callback provenance, broader writer/alias completeness and the
actual-overlap fallback still remain. Geometry candidate comparisons stay on.

## Remaining direct resetter device fields tracked

The hash-gated native 82139228 extractor now replaces its direct 32-bit stores
to device+15144/+15148, +20076/+20080/+20084/+20088 and its four query-slot
stores via r29 (device+20060..+20072) with a scoped native word-store service.
Each mapped physical four-byte extent is guarded before StoreWord and notified
afterward. Guards end inside that service, before subsequent guest operations.
The accounting-region guard stays separate; stack writes and called functions
are not included in this replacement. Inspection of the generated production
output confirmed all ten audited direct-store sites use the service.

The 72 native/retail differential cases still match full CPU context, captured
memory and service/callback ordering. They additionally check per-branch word
notification counts (two for nonzero entry flags, eight for profiling setup,
one for the exercised profiling-state-2 advance), provider call counts and exact
merged write extents. Callback/service checks continue to require no active
guard. The production word-store service uses GuestReader and the existing
physical mapper; the fixture models that store with the real writer queue and
controlled physical coordinates. Full build and all 21 tests passed in 12.46s;
diff check passed. No live gameplay run for this change.

This completes tracking of the directly identified device-field stores in this
resetter, not all writes reachable through its callees, callbacks or stack aliases.
No geometry comparisons were disabled and no native resource was declared immutable.

## Unconditional 82138858 callee audit

Read the complete generated 82138858 body in recomp.23.cpp, plus complete helpers
821387E8 (recomp.11.cpp), 821F9FD8 (recomp.27.cpp) and 821FA060 (recomp.19.cpp).
82138858 is a state dispatcher, not a simple accounting write. State at
82578CF8 selects 11..17 branches. Other values skip those branches but still
reach optional command-46 indirect dispatch with device+20000 as its argument.
Therefore an idle state alone cannot justify replacing the whole function with
a return. Its provider selection uses the pointers rooted at 8200071C and
82000800; this is distinct from the fence-profile callback at device+13068.

The state branches can drain 8213D1C8, invoke provider commands 56/57/27, call
821F9FD8/821FA060 (provider commands 37/38), log through 82145078, or restore
query state through 82142398. State 11 and successful state 13/15 paths call
821387E8. That helper drains, acquires an interface through 82144E20, publishes
it at device+19956, and invokes its vtable+40 method with a translated physical
completion address derived from device+10768. States 12/14 may invoke vtable+36
with device+13452, release the interface and clear device+19956. State 14 also
has an error flag/callback/trap path. These bodies contain no direct PM4 encoding,
but their callees and interface methods are not thereby proven native-owned.

Direct outputs identified in 82138858 are the global state word, device+19956,
device+10809 and stack storage. Indirect provider outputs remain unresolved.
Current presentation tests still stub 82138858, so the 72 resetter differential
cases do not validate this dispatcher's internals. Next resolve provider/interface
registration and reachable native-mode states before choosing native replacement
semantics; do not classify the complete call tree as an exact counter writer.
This was source research only: no code, executable or runtime policy changed.

## PIX capture interface identified from image and implementation

Read the analysis guest_image.bin directly as big-endian words/ASCII. Strings
used by 82138858 include unnamed.pix2, crashdump.pix2 and PIX!Trace. These identify
the special state machine as a PIX capture/debug facility, not ordinary geometry
submission. The image's global words at 8200071C and 82000800 are 30003000 and
30002000 respectively; these are image contents, not a new live observation.

82144E20 does not fetch an arbitrary implementation: it optionally calls the
provider at (*(*8200071C))+12 to allocate 840 bytes, writes vtable82009C74 and
zeros 64 three-word records after the vtable. The image vtable's entries +36
and +40 resolve to 82144B80 and 82144240. The latter's complete body stores its
r4 physical completion-address argument at object+804, clears other metadata
words in +772..+836 and masks object+776; it does not dereference that completion
address or call other code. The physical argument alone was therefore not proof
of a GPU/geometry write. 82144B80 conditionally calls 82144308, clears 64 record
fields, and computes a result from flags; the downstream 82144308 remains a
separate review boundary.

SDK xboxkrnl_module.cpp initializes KeDebugMonitorData (ordinal0059) and
KeCertMonitorData (ordinal0266) with null objects when their corresponding options
are disabled. Enabled modes allocate and zero their structures; callback
trampoline installation is still commented out. Debug monitor comments describe
an offset24 two-argument handler, consistent with the observed command-dispatch
shape. This layout agreement is not yet an independently resolved import mapping
for the two image globals. Do not equate an address from the analysis image with
every runtime allocation or claim callback absence from a default alone.

The September9 project notes independently recorded a null object behind
82000800 in one run and associated its offset24 handler with hardware-statistics
event39. That historical snapshot supports prioritizing native diagnostics/capture
availability over emulating Xbox hardware-counter packets, but does not certify
all configurations. No production code changed during this audit. Next resolve
the globals' export binding and capture activation, then replace this optional
platform facility at its native availability boundary rather than emulate PIX
packets or treat all indirect methods as unidentified geometry writers.

## Loader-confirmed monitor export bindings

Used the existing loader's debug logging in a fresh startup-only run:
out/native-bridge-run/import-binding-20260911-005732/game.log.
At 00:57:33.504 it reports xboxkrnl ordinal0266 KeCertMonitorData mapped to
30003000, and ordinal0059 KeDebugMonitorData mapped to 30002000. These match
the two distinct pointer values read from the analysis image at 8200071C and
82000800. This resolves the prior layout-based inference to named kernel
monitor exports in the inspected build. The SDK initialization and PIX strings
can now be associated with certification/debug-monitor services specifically,
not an unidentified gameplay renderer provider.

Launched owned PID54372 at 2026-09-11 00:57:32 with log_level=debug, fresh user
storage and no scripted gameplay input. The run reached XUI startup. Stopped
only the exact path/start-matched instance after collecting the mappings;
WaitForExit returned true and absence was confirmed. No code changed. This
startup observation does not prove immutable/null monitor contents after startup,
capture-state reachability, callback output coverage or gameplay performance.

Next native replacement decision can be made at explicit debug/certification
monitor availability and PIX capture handling. Preserve normal renderer/accounting
semantics; do not recreate Xbox capture packets merely to satisfy an optional
monitor contract. Full Xenos replacement remains unfinished.

## Real PIX dispatcher idle-path fixture

Added TEST_MODE-only tools/extract-native-pix-test.cmake with the complete
82138858 source gated by SHA256
7ee084c493d6cb01912366a4234644f3d9f212b05e7c4aa3983aee8f5fe25ddf.
The fixture includes real save/restore helpers, models indirect dispatch, and
rejects entry into special capture services. Production PIX behavior is unchanged.

25 cases exercise five non-capture states (0,1,10,18,UINT32_MAX) and five provider
configurations: neither monitor, debug only, certification only, certification
object with null handler plus debug, and both usable. They verify event46 with
the frame-count argument and original LR, selected callback target/count, unchanged
state and preserved stack/LR/nonvolatile registers. Certification takes priority;
an existing certification object with null handler suppresses debug fallback.
The debug path's null handler behavior and active states11..17 are not covered
by these cases, and this is not a native/retail paired dispatcher test yet.

Full native build and all 21 tests passed. No game was launched and no runtime
policy changed. This supplies an executable idle/monitor-selection baseline for
the native replacement rather than assuming the whole dispatcher is a no-op.

## Native idle monitor controller

Added native_pix_monitor.h DispatchNativePixIdle and a native-host 82138858 hook.
For states outside11..17, the native controller reads the monitor exports and
dispatches the ordinary event46/frame-count callback without executing the retail
state machine or its guest stack-save stores. It preserves certification priority,
repeated certification pointer sampling and no fallback when a certification
object exists with no handler. Debug dispatch retains the original null-handler
error boundary rather than silently treating it as a successful no-op.

The production callback adapter reserves the original 128-byte frame depth,
supplies event46 and frame count, LR82138B8C and the original dispatcher-local
nonvolatile values in its copied context, and propagates callback r3. Caller
nonvolatile registers remain unchanged. The controller itself writes no guest
data; callback bodies retain their existing dispatcher and are not certified
write-free. This is not byte-for-byte reproduction of the old stack-save area.

The 25 actual-retail idle cases now compare native callback selection/count/frame
arguments with that baseline. Seven further cases require each active capture
state11..17 to remain unchanged and unhandled. Full build and all 21 tests passed
in12.43s; diff check passed. These test the controller template and retail baseline,
not full production callback ABI execution. No gameplay run this turn.

Active capture states and host-off still call the original routine. This is an
implemented native idle path, not complete PIX/capture replacement or full Xenos
removal. Guest callback provenance and geometry writer completeness remain open.

## Callback ABI verification and vertex-unlock writer closure

The pending monitor adapter build completed successfully: all 21 tests passed
in 12.28s. RunNativePixIdle is now the shared production/test adapter. The 25
idle cases compare its callback entry stack, arguments, LR, CTR and r27..r31
against the real retail dispatcher, plus returned r3 and caller preservation.
This does not reproduce or certify the removed retail stack-save bytes, active
capture paths or arbitrary callback writes.

Returned to the concrete 8242D440 small-buffer vertex update. Inspection of
generated shard12 confirms lock return 8242D4A8, a 120-byte 821E8320 copy
from object+144 at 8242D4B4, and VB unlock 821349B8 at 8242D4BC. The payload
copy already uses the scoped bulk hook. The VB unlock header writes did not.

Read complete 821349B8 (shard71) and 82134640 (shard44). The wrapper fixes
r5=0: the helper atomically updates owner+0 and conditionally resets owner+20;
its owner+24 store is unreachable with that argument. The retained cache-flush
call has the existing native CPU implementation. Added an exact 24-byte header
scope and completion notification to the VB unlock hook for native bridge mode,
ending before NotifyNativeBufferUpdate takes the renderer mutex. Entry owner is
captured before the original routine clobbers volatile registers. Host-off has
no new writer extent. Stack stores are outside this header coverage claim.

The immediate fixture now extracts both real retail routines and the actual
production hook. 24 paired cases cover native on/off, three lock-count fields,
dirty sentinel/non-sentinel and two physical-address flags. Full arena memory
and PPCContext match retail; tests require header-only notification, stale-token
rejection, one renderer update with the original owner, and released guards at
that update. Existing completion checks require notification inside the guard.
These are serial differential tests, not forced atomic-contention coverage.

Full native build and all 21 tests passed in 18.29s; diff check passed. No game
was launched. This closes the identified VB-unlock header writer, not arbitrary
geometry aliases, untracked payload writers or overlapping-write fallback.
Guest geometry comparisons remain enabled. The previous user-facing status-only
turn was no implementation progress; this continuation verifies the pending ABI
work and implements/tests a concrete missing write boundary.

## Index-unlock header coverage extended beyond inline producer

The previous turn made verified implementation progress on VB unlock. This
continuation inspected the complete IB wrapper 82134AD8 (generated shard16):
it loads r4 from owner+24 and unconditionally sets r5=0 before 82134640.
The previously audited 24-byte header extent therefore applies to every caller
of this wrapper, not only LR8242D3DC. Extended its native header guard and
completion to all callers. The special twelve-byte payload scope still requires
the known inline producer; no arbitrary payload extent or ownership is inferred.

Added the real IB wrapper to the immediate fixture and extended the paired
VB/IB matrix to 48 cases. These compare the actual guarded hooks to the original
wrappers/helper across native on/off, lock counts, dirty metadata and physical
flags. Full arena bytes and PPCContext match. IB tests check an active scope
before the retail provider and at completion, exact owner/24-byte notification,
stale-token rejection and scope release before renderer update. Existing inline
tests now also require a header notification for ordinary native callers while
retaining independent mapped/unmapped payload-scope cases.

Full build and all 21 tests passed in 23.70s; diff check passed. No gameplay run.
Geometry byte comparisons are unchanged. WriterKind still uses the existing
InlineIndices category for this IB header hook; that label must not be treated
as proof that every classified call is the twelve-byte inline payload producer.

Read-only direct-call search identifies four generated wrappers tail-calling
82134640: VB821349B8, IB82134AD8, surface82139B90 and indirect-surface82139BA8.
The surface wrappers (shards52/68) pass masked owner+32/+48 addresses in r4/r5;
82139BA8 first follows entry owner+24. Unlike VB/IB, these can reach the helper's
owner+24 store and need a 28-byte header analysis. They are not newly discovered
geometry payload writers. Direct-call enumeration does not exclude indirect
entry to 82134640, concurrent pointer replacement or other resource writers.

## Explicit alias updates invalidate observation revisions

The previous turn made verified implementation progress on general IB unlock.
Review of NativeModelBuffers::NotifyUpdateAliases found that its synchronous
cache invalidation did not itself advance NativeBufferWrites content revisions.
Current buffer-unlock guards already advance the global writer epoch, and byte
checks remain enabled, so this is not evidence of observed stale gameplay data.
It is a missing local contract for future per-resource version-based reuse.

Added InvalidateObservedRange for the registry-owned synchronous path. It advances
revisions of every exact overlapping subscription before ApplyWrites retires the
corresponding cached owners under the existing registry lock. It deliberately
does not enqueue a duplicate cache retirement or synthesize a writer transaction.
Record and token-only invalidation share the same interval lookup, including
invalid-range and index-allocation-failure fallback. Source lifetime and the
preceding write still require producer coverage/exclusion.

The alias registry test now uses a real writer queue. It requires stale-token
rejection for updated/overlapping owners, acceptance for an adjacent owner,
exactly one content-revision increment with unchanged writer epoch, no pending
duplicate batch, and no invalidation of an owner republished at another address.
Full native build and all21 tests passed in23.95s. Resulting executable SHA256:
001D89F728E1AE85206269F3CE7F8A0460D89B3706DB10B89AFF2F26D2BAC669.
This newest change has test/build verification, not a live gameplay run.

## Accumulated unlock/monitor movement-and-firing regression

Run: out/native-bridge-run/binding-validation-20260911-011627-3cd1460b.
Executable SHA256 FE2F16845715CE651C6C25793365202B5DD14A608DB598D21ECF5299BA40E6A9.
Owned PID21212 started 2026-09-11 01:16:27 with fresh user storage, the movement/
fire script, HookTimings and RetirementAudit. It includes the prior VB/IB unlock
and monitor changes, but predates the observation-revision change above.

Inspected before.png (mothership introduction), firing.png (moved player,110/120
ammo) and after.png (further movement,101/120 ammo, enemies/effects). Intro remains
visibly overbright; this is not a fidelity pass. Last indexed sample at01:20:34.301
reported17,563,000 submissions,771 builds,432 entries, zero errors and zero vertex/
index mismatches. Both logs were searched for error/critical, nonzero geometry
errors/mismatches, audit failures and exhausted counters; none matched.

Stopped only the exact path/start-matched process at01:20:34. WaitForExit returned
true and absence was confirmed. This is a bounded gameplay regression, not a
complete mission, visible-FPS benchmark or proof of complete writer coverage.
No geometry comparisons were removed and no game remains running.
The01:20:28.301 snapshot checkpoint reported16,777,216 guarded acquisitions and
zero unavailable acquisitions. Guest candidate comparisons were still enabled.

## Atomic paired geometry attachment

The previous turn made verified implementation and regression progress. This
continuation inspected the production indexed draw after CopyObservedSet:
vertex conversion, canonical vertex contents and index conversion were attached
with separate queue handshakes. Each had stale-token checks, but attachment as
a whole had no all-or-none contract. This was not an observed rendering failure.

Added NativeBufferWrites::CommitObservedSet. Under one queue lock it validates
all owner lifetimes, revisions and writer epochs and rejects active/unknown
tracking before invoking the commit callback exactly once. Single-owner commits
delegate to the same implementation. NativeModelBuffers::CommitObservedGeometry
also checks model generations, vertex/index kinds, physical ownership, non-null
GPU storage and canonical full-vertex extent before attaching all three shared
objects inside that handshake.

Production guarded indexed draws now use this paired attachment when any retained
object differs. The full observed vertex snapshot is retained even for a draw
with a stream offset; converted storage keeps its own source offset. Already
attached objects do not incur a new handshake. Unguarded/unclassified fallback
keeps its prior behavior. This reduces up to three attachment handshakes to one,
not every draw's total locking or its guest-byte comparisons.

Queue tests cover a stable pair, a stale second owner, explicit first-owner
invalidation, active/ended writer epochs, missing/reused owners and recovery with
fresh observations; rejected attempts never invoke the commit callback. D3D
fixtures verify stale-pair rejection leaves all retained objects unchanged,
reject wrong model lifetime and partial canonical contents, and attach all three
objects from a fresh paired acquisition. Existing source-offset GPU tests remain
separate from these paired-attachment cases. The initial full build and all21
tests passed in24.08s. No gameplay run or performance claim for this change.
After formatting the fallback branch, the final rebuild and all21 tests passed
again in24.50s; diff check passed.

Writer/alias completeness and actual-overlap fallback remain open. Both guarded
candidate comparisons and unclassified live-source checks are still enabled.

## Busy-writer admission: physical fault-lock dependency

The prior turn made verified paired-attachment implementation progress. This
continuation investigated whether exact callback-free bulk copies could be
declared safe to wait for inside the indexed hook. Read complete 821E8740 from
generated shard46: it contains aligned word/byte loops and no direct/indirect
guest service calls. Its native completion requests no physical-version callback.
That source-level property is insufficient to certify independent progress.

The indexed hook still holds submissions and registry/context locks during
CopyObservedSet. NativeAliasWatch::Fault acquires the SDK global critical-region
mutex before examining watched physical pages, invoking observers and restoring
page protection. SDK xmemory.cpp AccessViolationCallback/TriggerPhysicalMemoryCallbacks
likewise pass a held global lock into physical callbacks. Generated
REX_ENTER_GLOBAL_LOCK uses this same recursive mutex. Therefore a plain store
inside a bulk copy can gain a host lock dependency through a write fault even
without a generated CALL. Inspection of GuestPhysicalVersions confirms its own
invalidation callback does not take renderer locks; that does not remove the
global-lock dependency or certify other callback providers.

Added a two-thread regression using the actual SDK global critical-region mutex
and the real NativeBufferWrites queue. One thread holds that mutex; a producer
on the other thread enters an exact Bulk scope and then requests the same mutex,
modeling the watched-store fault boundary. Snapshot acquisition must promptly
reject the active overlapping writer without reading its source. After releasing
the global lock and joining the producer, acquisition sees all completed bytes.
This models the lock dependency, not an injected Windows page fault or proof that
normal gameplay currently reaches this lock ordering.

The guest-memory test target rebuilt and passed in6.55s; diff check passed.
No production behavior or executable changed and no game was launched. The next
admission design must establish inherited lock ownership and callback progress,
or capture an ordered, lifetime-safe draw request that can wait outside those
dependencies. Do not mark a provider wait-safe solely from an exact destination
range and absence of guest calls. Overlap fallback and geometry comparisons
remain unchanged; this evidence rules out that proposed shortcut.

## Physical backing release is not an unconditional watch notification

The prior turn made source-audit/test progress on fault-lock dependencies. This
continuation checked backing lifetime beyond NativeModelBuffers::RetireAllocation,
which recognizes the embedded allocation record at owner+32. Complete 821D3B68
(generated shard71) unlinks a chunk, loads its backing pointer from node+8, and
calls 8212FC28 before destroying bookkeeping. It does not enumerate published
native geometry owners. Generated-source search confirms six direct callers of
8212FC28:821D4380,821D18E0,8212F4B8,821D1890,821D3A40,821D3B68. These are direct
edges, not complete lifetime/alias coverage or a claim that each frees live geometry.

Read the SDK MmFreePhysicalMemory implementation: it calls the selected heap's
Release. PhysicalHeap::Release holds the global critical region, releases the
parent allocation, queries the alias region size, and invokes TriggerCallbacks
before the alias BaseHeap::Release. TriggerCallbacks returns without dispatch
when that range has no watched host pages. The callback's boolean is
unwatch_exact_range, not an allocation-free event discriminator. Registering a
callback therefore does not create unconditional native allocation retirement;
calling into renderer locks from it would also need a lock-order audit.

Extended the actual SDK-backed write-watch lifetime fixture. Existing cases
verify notification when freeing an armed allocation and fresh notification
after address reuse. The new case consumes the watch on every host page through
real guest-alias stores, then verifies Release succeeds without another callback.
An initial assertion after only one store failed: larger guest allocations still
had watched host pages. The corrected test explicitly walks all4096-byte host
pages before checking absence of the release notification. It passes across the
fixture's three physical alias heaps. This is a bounded SDK contract test, not a
full native-renderer deallocation regression or proof of a live stale-resource bug.

The guest-memory target rebuilt and passed in7.03s; diff check passed. No game
or production executable changed. Native backing retirement needs an explicit
allocation/free boundary with extent and alias-generation handling; it cannot
be delegated solely to these one-shot watch callbacks. Geometry comparisons
and allocation retirement policy remain unchanged.

## Retire all published aliases at model allocation release

The previous turn made SDK-contract research/test progress. This continuation
implements alias retirement at existing model/pool release boundaries, without
introducing a renderer-lock acquisition inside the SDK free callback.

NativeModelBuffers::RetireBackingAliases collects every published physical
overlap before changing ownership, then retires their registry entries, write
subscriptions and mesh-cache mappings under the caller's existing registry lock.
RetireAllocationAliases resolves the embedded owner+32 allocation record and
delegates to it. The 821D3DC8 hook now uses this operation before calling the
original pool release instead of retiring only the directly named owner.

Normal 821D7468/821D75F8 cleanup also uses RetireBackingAliases: its previous
owner-only retirement erased the extent before the later pool hook could find
aliases. Unknown owners retain the old mesh-key invalidation fallback. No bridge
lock spans the subsequent guest cleanup/unbind/free operations. All overlap
collection/allocation happens before any retirement; callbacks are restricted
to mesh invalidation and may not reenter the registry or publish guest objects.

Tests cover a containing VB and nested IB via different guest physical aliases,
an exactly adjacent block, unknown and repeated releases, stale observation
tokens, republishing an owner at the same physical address with a fresh lifetime,
normal cleanup followed by pool release, and unmapped/empty resources. No new
queue tombstone or delayed address-only retirement can affect a reused owner.
The initial pool-only full build passed all21 tests in24.55s; the combined normal
cleanup/pool integration rebuilt successfully and passed all21 tests in24.80s.

This retires published aliases at these known allocation-release boundaries.
It does not enumerate unregistered aliases or handle whole backing frees that
bypass both model cleanup and 821D3DC8. Generic 8212FC28/MmFreePhysicalMemory
coverage still needs allocation extent, ordering and lifetime handling. Geometry
comparisons remain enabled. No live gameplay run for this change.

## Whole-pool backing retirement

The previous turn made verified alias-release implementation progress. Read
complete generated 821D3A40 (shard46), its direct caller 821D3F90 (shard60),
821D4380 (shard15), insertion helper 821D42F0 (shard24), and backing constructor
821D3FF0 (shard27). 821D3F90 passes pool+28 to list destruction. 821D3A40 loads
the sentinel from list+4, detaches the list, then walks nodes through node+0;
each nonnull node+8 backing is freed through 8212FC28 before node bookkeeping.
821D3FF0 stores the backing address/requested size at chunk+0/+4 (node+8/+12).
SDK allocation size may include rounding, so requested size is not the free extent.

Added ReadNativePoolBackings and a native 821D3A40 hook. It collects nonnull
backing addresses while the original list still exists, detects null/cyclic node
chains, resolves each physical heap allocation's base/state/full allocation_size,
and checks its physical mapping. All descriptors are resolved before registry
retirement. Then, under the existing registry mutex, RetirePhysicalRange removes
every published overlap and its observation subscription/mesh mapping. The lock
ends before the original routine is invoked with the unchanged context. No
renderer lock is acquired by an SDK release callback. Caller-owned list/lifetime
synchronization is still required; this adds no concurrent-list mutation protocol.

Tests cover empty lists, ordered multiple chunks and null payloads, malformed
node chains, interior/aliased resources without a wrapper at the allocation base,
adjacent-block preservation, invalid extents and address reuse. Actual SDK heap
fixtures verify allocation_base/allocation_size and full-range physical mapping
feed the same ownership retirement for all three physical alias heaps. These
exercise the shared collector, registry and SDK mapping; they do not execute the
complete retail pool destructor or establish its indirect-entry coverage.

The change covers whole-pool destruction via821D3A40. The separate chunk removal
821D3B68, arbitrary8212FC28/MmFreePhysicalMemory entries and allocator concurrency
still need their own coverage conclusions. Do not infer them from pool teardown.
Guest geometry comparisons remain enabled; no gameplay run this turn.
Full native build and all21 tests passed in24.41s; diff check passed.

## Individual chunk removal retires the actual free argument

The previous turn made verified whole-pool retirement progress. Re-read complete
821D3B68 from generated shard71: entry r5 is a packed iterator, with its low word
naming the selected node. The routine checks sentinels, unlinks that node, loads
node+8 into r3, and calls8212FC28 with LR821D3BEC only for a nonnull backing.
Rather than predict that target by re-reading the iterator before mutation,
the new8212FC28 hook observes the actual free argument at this audited call site.

Factored whole-pool extent validation and native retirement into shared
RetireNativePoolBackings. The individual chunk call passes its captured r3 as a
single-element backing list; SDK allocation queries occur before registry locking,
and that lock ends before the original8212FC28 forwards to MmFreePhysicalMemory.
Native-off and all other return addresses keep their prior behavior. Whole-pool
destruction still performs its earlier complete-list retirement; its direct free
call is not reclassified by the new hook. This is not a blanket physical-free
interceptor, and inherited caller lock/lifetime assumptions remain relevant.

The immediate fixture extracts the actual new hook and the real8212FC28 argument
adapter, with a controlled SDK free provider and retirement service. Sixteen
cases cover native on/off, four call-site values and two physical alias addresses,
checking retirement before free, exactly-once provider forwarding, original r3/r4
ABI transformation, caller LR and untouched r5, and propagated provider outputs.
A seventeenth injected retirement failure requires no SDK free call afterward.
These validate hook ordering/ABI, not a live SDK deallocation or full821D3B68
iterator semantics. Full build and all21 tests passed in24.17s; diff check passed.

No gameplay run. Other8212FC28 callers, direct MmFreePhysicalMemory imports,
unregistered aliases and concurrent overlap fallback remain open. Geometry byte
comparisons stay enabled; full Xenos replacement is not complete.

## Physical-free inventory and owned-buffer replacement/cleanup

The prior turn made verified individual-chunk retirement progress. Read complete
821D18E0,821D1890 and8212F4B8 from generated shards33,43,41. The first two own
an address/size pair: replacement frees the old nonnull address before clearing
fields and allocating through8212FB98; cleanup frees that address then clears
both fields. Their free call return addresses are821D190C and821D18B4. The generic
8212F4B8 instead tests flag bit31 and tail-dispatches physical or CPU-heap release;
it preserves its outer LR and does not establish allocation provenance.

Extended the existing8212FC28 hook to the two explicit owned-physical-buffer
sites. They use the same SDK-validated full allocation extent and pre-free native
overlap retirement as chunk removal. Other callers are unchanged. The actual-hook/
retail-adapter fixture now has24 forwarding/policy cases and three injected
retirement-failure cases, covering all three selected sites. The retirement
service/SDK provider are controlled in this fixture, not a live free workload.

Added docs/geometry-physical-free-sites.csv and a read-only source checker,
tools/check-geometry-physical-free-inventory.ps1. It matches ten emitted sites
by caller, callee and explicit/inherited return address: six8212FC28 edges and
four direct MmFreePhysicalMemory edges. The latter comprise the adapter, two
82145A40 device-field frees (20104/20108 with type2), and the82151460 persist-display
branch (type1). Their producer/alias/native-branch conclusions remain separate.
The ledger explicitly retains open boundaries rather than treating listing as
semantic certification. Registered/indirect calls are not counted as closed.

The checker passed against current generated source and rejected missing-row,
duplicate-row, wrong-LR and missing-boundary fixtures. It was rerun successfully
after the policy update. It is a standalone audit, not a new CTest entry.
Full native build and all21 tests passed in24.36s; diff check passed.
No gameplay run this turn; geometry byte comparisons remain enabled.

## Gameplay regression after allocation-retirement integration

Rebuilt the current native target and reran all21 CTests: all passed in24.25s.
The physical-free inventory checker still matches ten emitted direct sites.
Added sampled logging in RetireNativePoolBackings, under the registry mutex:
first eight calls, powers of two, and every call retiring any model owner. The
message explicitly describes pre-free retirement, not successful guest release.

Ran the explicit scene-only executable (historical filename, full native renderer)
with HookTimings, RetirementAudit, fresh user data and the movement/fire script.
Evidence directory: out/native-bridge-run/binding-validation-20260911-015852-2d6000b3.
Executable SHA256: C4AC9E0CEE216FB5EA7664402C32478E8C9BAACAD1B28AB93C5A755AF09DA52C.
Owned PID17564 started2026-09-11 01:58:53; path and start time were checked before
stopping it at02:03:13. WaitForExit returned true and PID absence was verified.

Inspected before.png, firing.png and after.png: intro street remains overbright;
subsequent gameplay shows movement, firing effects, enemies and HUD, with ammo
105/120 during firing and100/120 afterward. Last indexed-upload record at02:03:13.012
reports19,074,000 uploads,771 mesh builds,432 entries and zero errors/vertex/index
mismatches. Both game.log and rotated game.1.log were searched for error/critical,
nonzero mismatch/error counters, audit failures and exhaustion; no matches.
These are hidden-run draw counters, not a visible FPS or fidelity benchmark.

Rotated game.1.log contains retirement samples1..8,16,32 during02:00:44, each for
one backing and zero published model owners. This establishes that the shared
retirement hook ran and gameplay subsequently continued, but does not establish
which caller invoked it, successful individual frees, or live geometry-alias
retirement. A controlled teardown/reload workload with nonzero retired owners is
still required. Process termination is not guest teardown validation. Comparisons
remain enabled; this run does not certify writer completeness or remove fallback.

## Conditional pool-growth temporary free now retires native overlaps

The prior turn produced gameplay regression evidence, including the limitation
that sampled backing retirements had zero registered model owners. This turn
re-read complete821D4380 (shard15),821D42F0 (shard24), and821D4248 (shard73).
Growth initializes SP+96 to zero, passes that temporary descriptor to insertion,
then reloads SP+96 and conditionally calls8212FC28 with LR821D4404. Insertion
copies the descriptor's address/size into the node and invokes bookkeeping copy;
the latter invokes821D3CC0. We have not certified all nested effects or declared
the conditional free unreachable.

Extended the actual-free hook to LR821D4404. If the branch is taken, its actual
nonnull r3 backing receives the same SDK allocation-base/extent validation and
native overlap retirement as the other selected physical frees. The bridge lock
ends before the original adapter calls the SDK. No predicted temporary address
or assumption of zero contents is used. Native-off and unrelated callers retain
their previous behavior. The inventory now records this implementation while
keeping reachability and concurrent ownership explicitly unresolved.

Updated the actual-hook/retail-adapter fixture: its24 forwarding cases now expect
retirement for this fourth selected caller as well, and a fourth injected-failure
case verifies that failed retirement prevents forwarding to the free provider.
These use controlled retirement/free services, not full pool-growth execution.
Full native build and all21 CTests passed in24.14s; the ten-site inventory checker
and diff check passed. No game launched for this change. Geometry comparisons,
generic physical-release provenance, direct device frees and live teardown/reload
coverage remain open; full Xenos replacement is not complete.

## Generic release routing tested against the retail wrapper

The preceding turn implemented conditional pool-growth retirement and passed the
full suite. This turn inspected the complete8212F4B8 body in shard41. It tests
bit31 of the low32 bits of r4; physical release skips a null r3 and tail-dispatches
to8212FC28 otherwise. CPU release tail-dispatches to8212FC40 even for null. Neither
tail call assigns LR, so the original caller identity survives the adapter.

The immediate-tail fixture now extracts this actual generated wrapper under an
isolated name and substitutes only its two free endpoints.48 cases cover eight
flag patterns, three addresses (including null), and two caller LR values. They
check heap selection, null policy, unchanged full-width r3/r4/r5/LR arguments at
the endpoint, propagated provider r3, and retained caller state. High64-only flag
bits do not select physical release. The fixture target rebuilt and passed
in15.44s; diff check passed. This is a routing/ABI test, not a real heap release,
lock-order test or implementation of native retirement at this wrapper.

A source scan finds763 emitted direct calls to8212F4B8. That count includes CPU
release paths and does not mean763 geometry owners or763 unresolved writers.
The shared renderer registry uses a nonrecursive mutex; adding a generic hook
that always takes it requires checking inherited renderer/allocator locks across
the physical branch's callers. Current evidence neither proves a live deadlock
nor certifies that a blanket hook is safe. The next implementation must preserve
the verified selector/null/ABI contract while establishing safe retirement order.
No production executable or comparison policy changed, and no game was launched.

## Generic physical release gains pre-free native retirement

The prior turn established the generic selector's retail ABI. A lexical bridge
scan this turn found two direct guest-call expressions inside state.mutex scopes:
sub_821FAC28 in821BEAB0 and original821BEB38 in its hook. Manual inspection shows
both use PacingState, not the renderer Bridge registry mutex. ImportTexture calls
its original before acquiring the registry mutex; model cleanup/pool release
likewise forward after retirement's lock ends. No direct guest call was found
lexically inside a Bridge registry lock scope. A scan of generated regions from
REX_ENTER_GLOBAL_LOCK through REX_LEAVE_GLOBAL_LOCK found no sub_* or
REX_CALL_INDIRECT_FUNC calls. An earlier exploratory scan used the incorrect EXIT
spelling and its apparent matches were discarded. These are bounded textual
checks, not an interprocedural proof of imported/indirect lock inheritance.

Added a native8212F4B8 hook. Low-word r4 bit31 plus nonnull r3 selects the existing
SDK-validated backing retirement before the unchanged retail wrapper executes.
CPU release and physical-null handling remain retail. All heap extent queries
precede acquisition of the registry lock; that lock ends before forwarding. This
covers the generic physical branch without guessing its inherited LR or caller's
resource type. The original full-width context is passed through unchanged.
Indirect concurrency and live lifecycle behavior remain validation obligations.

The fixture extracts the actual hook and invokes the actual retail selector,
with only retirement/free endpoints controlled.96 cases cover native on/off,
eight flags, three addresses and two LR values. Physical endpoints require the
retirement event already present when expected; CPU endpoints reject retirement.
Tests also preserve argument/return ABI and reject forwarding after injected
retirement failure. Full native build and21/21 tests passed in24.41s; strengthening
the endpoint-order assertions then rebuilt/passed the immediate target in14.99s.
The ten-site free inventory and diff check passed. No live game run this turn.
Comparisons remain enabled; direct device frees, unregistered aliases, concurrent
overlap fallback and live teardown/reload validation remain open.

## Generic-retirement live mission lifecycle regression

The prior turn implemented generic physical release retirement and passed its
build/tests. This turn ran the current executable with HookTimings and
RetirementAudit, fresh user data and a reloadable lifecycle input script.
Run: out/native-bridge-run/binding-validation-20260911-021357-601abf24.
SHA256: E941BA692D0BFD4C5E10A0645F7C133125CC4166F5FF29B02AAD49869E6657C1.
PID54780 started2026-09-11 02:13:57. Verified executable path and start time before
stopping at02:24:31; WaitForExit returned true and absence was verified.

Inspected pause.png and retry-confirm.png before navigating. The first live
confirmation update arrived at241808ms, missing the240000ms Up;243000ms A chose
No, confirmed by retry-state.png. Reopened Retry at300000ms, selected Yes at303000
and confirmed at306000. Texture releases followed at02:19:06.922 and restarted.png
shows gameplay. Retry alone did not establish full geometry teardown.

Paused again at400000ms. Inspected quit-pause.png and quit-confirm.png; two Down
presses selected Quit Mission, then Up/Yes and confirmation at513000ms. At
02:22:33.277 the sampled native model-cleanup counter reached8192 completed
releases. quit-result.png shows EDF Headquarters. Selected Start Mission and
the defaults at560000/563000/566000ms. reentered.png shows the mission1 mothership
intro rendering again, with the known overbrightness still visible.

At02:24:14.266 the renderer reported20,354,000 indexed uploads,1058 mesh builds,
344 entries and zero errors/vertex/index mismatches. Retained game.log/game.1.log
had no error/critical, nonzero error/mismatch, audit-failure or exhaustion matches.
This hidden diagnostic run is not an FPS benchmark or full visual-fidelity test.
Backing-retirement samples were exercised (including startup and mission load),
but none logged nonzero model_owners. Normal model cleanup may retire ownership
earlier; the logs do not prove that explanation or alias/address-reuse coverage.
Successful quit/re-entry strengthens lifecycle regression evidence without closing
writer completeness, overlap fallback or comparison removal.

Added tools/native-retirement-lifecycle-input.txt with the observed navigation,
including cancellation/reopening of the first dialog. Its final consolidated
schedule has not been separately replayed from startup; this run used live
updates whose schedule clock was retained. No production code changed this turn.

## Remaining direct frees: capture ownership and removed display persistence

The prior turn supplied live quit/re-entry evidence. This turn traced the two
remaining device-field frees beyond their numeric offsets. Complete82145A40
(shard6) selects separate two-bit policies at device+20404 for buffers20104/20108.
Policy0 calls MmFreePhysicalMemory(type2), policy1 calls generic8212F4B8 with
flags0xB1800000, and policy2/3 skip freeing. The routine then clears312 bytes at
device+20104, preserves304, and initializes six words at8..28 toUINT32_MAX.

Complete821459C0 (shard29) establishes the policy: try physical allocation type2,
flags1028, alignment4096; on failure try8212F420 with0xB9800000 (policy1); on further
failure use a nonnull caller-provided address (policy2). Output address/policy are
published via entryr5/r6. The end of82145B20 (shard72) calls this helper twice,
passing the two device-field addresses, and packs the policies into bits31..30
and29..28 of device+20404. Failure calls82145A40 atLR8214622C. The other emitted
cleanup call is821462D0 atLR821465A8, after its capture completion/file lifecycle.

This connects the fields to the legacy capture/file-output research already in
docs/native-gpu-boundary.md, section47028: capture initialization82146CC0/45B20,
conversion821465C8, file output821FA450, finalization821462D0. They must not be
silently classified as ordinary VB/IB allocations or freed when borrowed.
Native readback/conversion, file format and completion/handle ownership still
need replacement. This classification does not prove absence of cross-resource
aliases and does not make full Xenos replacement complete.

Also revalidated the third direct import edge,82151460 LR821519A4: the native
presentation extractor removes the entire51928..519B0 placeholder persistence
region and retains flag acknowledgement. Current native_present.cpp contains
neither VdPersistDisplay nor MmFreePhysicalMemory calls. The present-tail fixture
checks retail success/failure traces and removes those allocation/free events
from the expected native trace; rerun passed in0.86s. The native present still
calls82147028, so removed display persistence does not imply removed capture.

Updated the free-site ledger accordingly: two direct frees belong to legacy
capture, and one is already removed from native presentation. These are evidence
corrections, not three newly implemented fixes. No production code changed.

## Shadow revision audit detects silently repaired writer gaps

The prior turn classified remaining direct frees. Reviewing the writer inventory
against CopyObservedSet exposed an evidence gap: its retained guest-byte check
can replace a stale snapshot silently, before mesh mismatch counters see it.
Therefore previous zero-mismatch runs cannot tell whether that repair occurred.

Added opt-in revision validation to CopyObservedSet, enabled by the existing
edf_native_retirement_audit diagnostic flag in indexed acquisition. Each
subscription holds only a weak reference to its last audited immutable snapshot
and that observation's revision. If the exact retained candidate differs from
guest bytes while the subscription revision is unchanged, the returned snapshot
flags an unreported change and the bridge logs owner, VB/IB, lifetime and revision.
The live comparison still repairs the snapshot; no comparison is bypassed.
Unrelated global writer epochs do not suppress this evidence. Resubscription
clears the baseline; expired/different candidates cannot be blamed. Audit-off
draws perform no weak-reference locking or baseline updates.

Tests cover an unreported vertex change, unchanged index contents, a notified
write, unrelated writer activity, and reuse of the owner with a fresh lifetime.
The full build and21/21 tests passed in24.36s before the last two assertions;
the expanded guest-memory target was rebuilt and rerun separately. This audit
does not identify the responsible instruction, prove coverage on zero reports,
or observe every change whose candidate/baseline is unavailable. A live run with
it is still required. Previous lifecycle runs predate this diagnostic.

## First live shadow-revision audit

The previous turn implemented the shadow audit and passed its tests. Ran the
current scene-only executable with HookTimings, RetirementAudit, fresh user data
and native-movement-fire-input.txt. Evidence directory:
out/native-bridge-run/binding-validation-20260911-023241-22846e1b.
SHA256:1C83DFFBE5F9D8A4C86D15D9261759744AE0DD4FAEC697931621EC9ECD93B799.
PID47124 started2026-09-11 02:32:41. Exact path/start identity checked before
stopping at02:37:00; WaitForExit returned true and PID absence was verified.

Inspected before.png, firing.png and after.png: gameplay, player movement,
firing with102/120 ammo, and subsequent100/120 with enemies/effects rendered.
At02:36:42.370 indexed uploads reached16,486,000 with767 builds,428 entries and
zero errors/vertex/index mismatches. The02:36:44.604 acquisition checkpoint
reported16,777,216 guarded and zero unavailable. No revision-audit warning,
error/critical, nonzero error/mismatch, audit-failure or exhaustion match appeared
in the retained game.log/game.1.log search after the scripted sequence.

This yields no concrete missed-writer target in the exercised workload. The audit
flag was enabled for this executable, but the diagnostic does not count valid
baseline comparisons separately; do not equate all draw acquisitions with audited
candidate pairs. Missing/different/expired candidates remain outside its report
condition, and one mission segment cannot certify writers/aliases globally.
Comparisons remain enabled. Repeating this same zero-report workload alone is
not a substitute for closing static writer provenance and overlap fallback.
No production change or further build occurred during this live validation turn.

## Rechecked rejected-acquisition path and four-byte fill provenance

Source review after the bit-vector follow-up confirms indexed acquisition holds
both state.submissions and state.mutex before CopyObservedSet. A rejection leaves
vertices_bytes/indices_bytes pointing to guest memory; meshes.Acquire then uses
the existing live validation path. before_snapshot only obtains version tokens;
it is not a read lock. Mesh-watch auditing also reads before guarded acquisition.
Thus removing the guarded std::equal alone neither removes all comparisons nor
makes rejected acquisition safe. Waiting at this point while holding renderer
locks is not an established completion protocol; releasing those locks would
require revalidating bindings and resource lifetimes before submission. Neither
silently skipping the draw nor using an arbitrary older pair is an equivalent
replacement. No fallback behavior was changed in this review.

The same review finished the direct caller connection for four-byte vector fill
BA368: BAFD0 comes only from the local bitset insertion chain, and C88A0 comes
only from the classified C8A20 append family. Complete C88A0 and the append call
setup preserve the vector owner and fill-value pointer and construct an end
iterator. The writer inventory and four fill rows now record these normal
CPU-heap-backed paths, retaining indirect-entry/alias/concurrency limits. This
narrows writer candidates; it is not runtime comparison removal. Documentation
and CSV changes only; no executable or performance claim follows.

## Revision-audit denominator implemented

ObservedSnapshot now reports revision_audited only when the audit is enabled,
the previous immutable candidate is still the exact supplied candidate, and
the subscription revision is unchanged. unreported_change is a mismatch within
that eligible set. First observations, changed revisions, new lifetimes and
different candidates are not counted as checked. Audit-off does not claim checks.

Indexed acquisition accumulates checked, without-baseline and missed counts
separately for VB and IB while auditing is enabled. It logs them at the existing
first-eight/power-of-two acquisition checkpoints. These counters describe only
successful snapshots; rejected acquisitions remain in the separate unavailable
counter and still use the existing fallback. Without-baseline also includes a
changed revision: it means no usable same-revision baseline, not no byte read.
No counter implies exhaustive writer coverage. All normal byte comparisons remain.

Tests now check eligibility for initial observations, stable candidates, missed
updates, notified writes, lifetime reuse, disabled auditing and equal-content
but different immutable candidates. Fixed a test-only array-to-vector constructor
error on the first build. Subsequent full build and all 21 tests passed (25.96s).
Current scene-only executable SHA256:
8B16FAC5EF3F6B03FFBA6D8C8B0150A67B5AD428DE764348AF98CBDF490A195C.
No game run yet for this executable; the next live audit can now quantify its
eligible observations instead of treating total acquisitions as audited pairs.

## Live audit with measured baseline coverage

Run out/native-bridge-run/binding-validation-20260911-025430-485e1097 used the
above 8B16FAC5 executable, fresh user data, HookTimings, RetirementAudit and
native-movement-fire-input.txt. PID46356 started September11 02:54:31; checked
exact executable path/start time before stopping at02:59:05. WaitForExit returned
true and process absence was verified. No other process was stopped.

Inspected before.png (mission street intro), firing.png (player moved to a side
street, ammo120/120), and after.png (another street position, ammo101/120, visible
enemies/effects). The script's movement/fire sequence completed. The image named
firing.png alone does not establish a shot at that instant; the final ammo and
changed positions support the completed sequence.

At02:58:30.519 the paired acquisition checkpoint was16,777,216 guarded and zero
unavailable. Audit counters were16,776,450 checked VB /16,776,429 checked IB,
766/787 without usable same-revision baselines, and zero missed VB/IB changes.
These are repeated observations, not distinct resources or writer instructions.
At02:59:05.656 indexed uploads reached21,368,000 with767 mesh builds,428 entries,
zero errors and zero vertex/index mismatches. The retained game.log/game.1.log
search found no unreported-change, error/critical, nonzero error/mismatch,
audit-failure or exhaustion matches during the inspected run.

Unlike the earlier run, this demonstrates substantial eligible audit coverage
instead of relying on total acquisitions alone. It supplies no missed-writer
target for this workload and does not exercise rejected-acquisition fallback.
Other missions, resource classes, alias paths and concurrent writes remain
uncertified. Comparisons remain enabled; this hidden diagnostic is not an FPS
benchmark. No further source change or build occurred during live validation.

## Rejected acquisition: current lock and lifetime constraints

Rechecked the indexed hook, NativeBufferWrites, NativeBufferWriteFrame, and
NativeMeshCache::Acquire after the collision callback body review. This is a
concrete remaining concurrency boundary, independent of callback enumeration:

- The indexed hook acquires state.submissions then state.mutex before resolving
  stream/index bindings, native_vb/native_ib pointers, shaders and viewport.
- CopyObservedSet takes the writer queue mutex, validates both subscriptions,
  and rejects active overlapping or unknown writers before reading either source.
- On rejection the hook leaves vertices_bytes/indices_bytes as guest spans.
  NativeMeshCache::Acquire compares those spans and can copy them into new
  storage. before_snapshot only requests Version tokens; it does not exclude
  a writer or convert rejected acquisition into safe source access.
- The retained vertex_contents candidate does not silently override unequal
  guest bytes: NativeIndexedMesh checks pointer identity or byte equality before
  adopting it. This check still reads the guest source on the fallback path.
- Successful acquisition replaces both spans with immutable snapshots, but
  optional mesh-watch diagnostics currently read the guest spans earlier, before
  acquisition. Startup index-word logging and clip-input diagnostics also read
  payload bytes directly. They must be included in any no-unguarded-read claim.

An in-place wait inside CopyObservedSet is not yet justified while both renderer
locks are held. NativeBufferWriteFrame::Finish explicitly releases its writer
scope before NotifyNativeBufferUpdate takes state.mutex; this one ordering is
verified, not a blanket proof for every provider and its nested services.
Bulk hooks notify the queue rather than taking the registry directly, while
NtReadFile also runs SDK file/event/APC bookkeeping and optional write-watch
notification. Their transitive lock behavior must not be inferred from the
small wrapper alone.

Conversely, releasing renderer locks and resuming at the failed call is invalid:
native_vb/native_ib and shader references were obtained from mutable registries,
and stream/target/material state was selected before the release. A correct
retry must either retain an independently owned complete draw submission before
releasing locks, or restart state resolution with an established ordering that
preserves the original draw. It must distinguish same-thread/reentrant writers
from writers that can finish independently. MissingOwner, ExtentMismatch and
UnknownTracking are not waitable ActiveWriter conditions.

Next implementation should establish that submission/lifetime boundary or prove
a restricted producer exclusion contract before changing rejection behavior.
Skipping the draw, using an arbitrary older VB/IB pair, or retrying with stale
registry pointers does not satisfy replacement fidelity. This review changes
no executable and does not claim that writer rejection has been fixed.

## Clip-input diagnostic moved to native snapshots

NativeIndexedMesh::CaptureSourceFloat3 now reads the retained source snapshot
and decoded native index values, honoring source stride, snapshot subrange,
first index, signed base vertex and attribute offset. The indexed clip probe
uses this method and its already owned declaration bytes instead of rereading
guest declaration/VB/IB memory. The logged input therefore describes the same
generation as the captured draw, even if the original guest arrays change.
The probe is bounded to 96 vertices and validates draw/float3 extents.

Tests cover 16-/32-bit indices, signed base, changed original arrays, nonzero
snapshot offset, nonzero first index and invalid base/index/attribute ranges.
The first test run caught use of converted GPU stride instead of source stride;
fixed before the final full build. All 21 tests then passed in 24.34 seconds.
Executable out/build/win-native-clean/edf2027-native-scene-only.exe SHA256:
7A9E1E8E8384B19170447EEB1EAA03B9E3EB52A2D7257E7574FE3D4125628D6E.
No game playtest or performance measurement was run for this build.

This removes the clip diagnostic's extra guest reads only. Startup logging,
mesh-watch audit placement, rejected-acquisition fallback, writer completeness
and steady per-draw candidate comparisons remain outstanding as described above.

## Startup index logging uses retained mesh contents

The first-five-draw resource logger now runs after mesh acquisition and reads
its first three index words from IndexStorage::SourceSnapshot. It reports the
resolved index width instead of rereading the guest header. Both the startup
logger and clip-input probe now describe retained draw contents, not a second
potentially changed guest payload. Bounds are checked against snapshot size.
Normal geometry validation and rejected-acquisition behavior are unchanged.

Full GuestMeshWatchAudit::Check inspection identified why simply moving that
audit to the returned snapshot would be incorrect: it obtains watch versions
and arms pages inside Check, after the snapshot would already have been copied.
A write between copying and Check could label older bytes with a newer watch
version. Its next observation could then report a false miss. That audit needs
an explicit byte/version observation contract, including arming and foreign-write
handling, rather than a mechanical relocation. No audit call was moved or disabled.

Full build and all 21 tests passed (24.07 seconds). Current scene-only executable
SHA256: 040D5595D9273C01F57099824A03E7A09E11DAB0866F9151E7D89BBCA591A2A5.
No live playtest or FPS measurement was run. This is diagnostic source ownership
cleanup, not removal of steady geometry comparisons or the fallback race.

## Write-watch audit captures versions before owned bytes

GuestMeshWatchAudit now separates Begin(address,length) from Finish(token,bytes).
Begin validates eligibility and arms the physical/alias watch without reading
the payload, then captures its version. Finish consumes immutable captured bytes,
rejects an intervening version change and clears that baseline, and checks for
foreign writes and late changes before accepting a reliable baseline. It never
assigns a newer version to an older captured vector. Disable/exclusion and
drawing-thread eligibility still apply. The old Check convenience wrapper is
retained for single-threaded tests, not used by the indexed renderer.

The indexed bridge begins VB/IB watch observations before CopyObservedSet and
finishes only with that successful paired acquisition's owned bytes. Rejected
or unsupported acquisitions do not invoke the audit on guest spans. The log
clarifies that checked counts begun observations, not completed comparisons;
unregistered/nonphysical resources are no longer audited by this bridge path.
This diagnostic coverage restriction is not evidence those resources are safe.

Added regression coverage for a notified write after capture but before Finish,
discarding the raced baseline, a fresh stable snapshot, and wrong snapshot size.
Existing audit tests still cover unnotified mutations, foreign writes, excluded
pages and disabled tracking. Full build and all 21 tests passed in 24.77 seconds.
Executable SHA256:
BACF136C451066BBE884926224601048AFDC0D1CD0345F4B177A1B128F24310F.
No live game test or FPS measurement was performed. Per-draw geometry candidate
comparisons and the renderer's rejected-acquisition fallback remain unchanged.

## Bulk-copy completion is not proved by its guest call graph alone

Read the complete generated 821E8320 body: its alignment, word/block and byte
tails have no emitted guest direct/indirect calls. It saves the destination on
the stack and copies through REX_LOAD/STORE operations. Its native hook holds
WriterScope across that body and the completed-write queue notification; the
wrapper does not directly acquire either renderer lock.

That does not yet justify a wait under renderer locks. SDK xmemory.cpp routes
physical access callbacks through PhysicalHeap::TriggerCallbacks and dispatches
registered physical invalidation callbacks. The inspected registration and
unregistration paths use the SDK global critical region. The SDK shared-memory
implementation registers MemoryInvalidationCallbackThunk; its complete callback
takes a global critical-region lock, invalidates page flags and calls FireWatches.
GuestPhysicalVersions also registers an invalidation callback, whose direct
Invalidate path updates atomic version/foreign-write metadata.

Thus even a call-free guest copy has a host callback/lock completion boundary.
The shared-memory callback's existence in SDK source is not proof that it is
registered in the native-host run, nor proof of an actual deadlock. Before
enabling wait-under-renderer-locks, establish the active registrations and their
transitive watch callbacks, and exclude same-thread reentrancy. No wait was
enabled on the assumption that a leaf guest function is a callback-free host
operation. This source review changes no executable.

## Native-host setup excludes the SDK graphics callback family

Following the actual startup path narrows the preceding callback concern.
src/main.cpp creates Edf2017App. Its OnPreSetup clears config.gpu_plugin and
forces the native host/shader bridge. SDK ReXApp::SetupPresentation invokes
OnPreSetup before its conditional LoadGpuPlugin call; that call requires a
nonempty plugin name. No config.graphics assignment was found in the port's
source. SDK Runtime::Setup initializes a graphics system only if config.graphics
is supplied, otherwise explicitly enters native rendering mode. The port's
immediate drawer uses its own D3D11 presentation context.

The SDK source registration search found physical invalidation registrations
in graphics/shared_memory.cpp and graphics/primitive_processor.cpp, plus the
registration implementation itself. The port's source registration is in
GuestPhysicalVersions. Consequently the SDK graphics registrations are not
reachable through the normal native-host graphics setup traced here. Do not
keep treating SharedMemory::FireWatches as an active native-host writer blocker
merely because its implementation exists in the SDK. This is source-path
evidence, not a new runtime callback-list capture or a claim about injected
nonstandard graphics configurations.

Remaining completion checks should focus on GuestPhysicalVersions and its
NativeAliasWatch callbacks, other exception/MMIO paths allowed by the particular
writer's source/destination ranges, and same-thread nesting. The generated
821E8320 memory macros are direct volatile loads/stores (with endian conversion
and the Windows physical alias offset), not explicit guest calls. Host fault
dispatch therefore remains relevant even after excluding SDK graphics setup.
No wait policy, geometry comparison or executable changed in this review.

## Native callback closure exposes a concrete three-thread wait cycle

Read the complete NativeAliasWatch implementation and its production construction
in GuestPhysicalVersions. Fault takes the SDK recursive global critical region,
looks up the protected page, queries access, invokes observers, restores the
page, and erases the one-shot entry. Restore can call SDK physical callbacks
before VirtualProtect. The only production observer supplied by this port calls
GuestPhysicalVersions::Invalidate, which updates atomic page/version/foreign
metadata and does not acquire renderer locks. Arm and destruction also use the
SDK global lock. Test-only observers are not production callback registrations.

However, waiting for the copy while holding state.mutex is still unsafe even
if neither copy callback directly enters the renderer. Existing bridge methods
NativeSignalSubmissionScope's constructor and PollNativeWorkerSignals acquire
the SDK global lock and then state.mutex. The following permitted lock ordering
would deadlock an added wait:

1. Draw D holds state.mutex and waits for active bulk writer W to finish.
2. Signal thread S holds the SDK global lock and waits for state.mutex.
3. Writer W faults on a watched page and waits for the SDK global lock.

This is a source-derived possible cycle, not a reproduced runtime hang. The
scope constructor releases its local global guard after updating the depth;
that does not eliminate the interval in which it waits for state.mutex.
PollNativeWorkerSignals likewise has an explicit global-to-registry acquisition.

Therefore marking leaf memcpy scopes as wait-safe based only on their own
callees is insufficient. Do not add a condition-variable wait under the current
renderer locks. The next synchronization implementation must move waiting to a
boundary without those locks while preserving submission identity/lifetimes,
or redesign the global/renderer lock ordering across these signal paths as well.
The SDK wrapper exposes std::recursive_mutex, not a current-thread recursion
query; TryAcquire cannot prove that the caller did not already own that lock.
Same-thread writer reentrancy remains a separate reason not to wait blindly.
No executable or comparison policy changed during this review.

## Signal queue separation requires retaining delivery ownership

Reviewed all signal_queues/signal_submission_depth references in the bridge and
the complete d3d11_signals.h/.cpp implementation. Capture is CPU metadata, but
SubmitRange creates a D3D11 query and calls immediate-context End/Flush; Poll
calls GetData. Therefore moving the existing queue wholesale to an independent
signal mutex would remove immediate-context serialization with draws. Queue
lookup alone is not the critical section's whole contract.

Poll currently removes returned signals from batches and decrements count_ before
the bridge writes device+10900 and wakes a guest event. The existing combined
global/registry ordering prevents a concurrent submission-depth change during
that selection. Moving Poll before acquisition of the SDK global lock without
retaining undelivered signals could select work before a new submission scope,
lose a selected signal if the single guest publication slot is occupied, or
let device reset see pending()==0 while delivery is still outstanding.

The reset hook rejects pending signals and erases signal_queues under submission
and registry locks. Drain and worker-continuation loops also depend on pending
and unsubmitted counters. These consumers must include any completed-but-not-
delivered queue if completion polling and guest delivery are split.

A viable separation must preserve three distinct phases: captured CPU ranges,
submitted GPU queries, and completed signals awaiting guest publication. GPU
polling stays serialized with the D3D11 context; guest publication uses the SDK
global lock without acquiring the renderer mutex. A separate CPU delivery
registry must own submission depth, device lifetime and undelivered counts, and
must be included in reset/drain. This review rules out the mutex-only shortcut;
it does not claim that the three-phase queue is implemented. No source behavior
or executable changed.

## Completed signal storage implemented; bridge split still pending

NativeSignalQueue now has completed_ storage, PeekCompleted and
AcknowledgeCompleted. Peeking transfers completed GPU query entries into owned
CPU storage without decrementing pending(). Repeated peeks retain delivery order;
only acknowledgement retires the requested completed prefix. Over-acknowledgement
throws before mutation. Storage is reserved to queue capacity at construction,
so transferring a completed batch requires no allocation; failure while copying
the return vector leaves the completed entries owned by the queue.

The existing Poll API is a compatibility wrapper that peeks then acknowledges.
The bridge still calls Poll, so its guest delivery, lock ordering and reset/drain
behavior have not yet been separated. Callers of the new two-phase API must
serialize selection/acknowledgement and retain the same queue lifetime; a count
is not an across-reset delivery token. Connecting a CPU delivery registry and
partial CPU-mask event progress remains necessary before the bridge can release
renderer locks between selection and delivery.

The real D3D11 signal test now exercises two-phase delivery with reused command
addresses and limited one-signal publication: it checks pending before ack,
repeated peeks, rejection of oversized ack, and final ordered delivery. Geometry
comparison and writer-wait policies remain unchanged.

Full build and all 21 tests passed (24.71 seconds). Scene-only executable SHA256:
E0F24C5AFA6E38996BA674EDB52C12AA5320D71E15295EC6885692A122300461.
No live game or performance test was run for this build.

## Bridge CPU signal delivery handoff connected

NativeSignalDelivery now owns one completed signal, submission nesting depth,
and remaining CPU-mask bits. Successful wake calls clear their bit; a later
exception retains only the remaining CPUs. This is not an across-process crash
recovery protocol, and wake callbacks must not report failure after committing
their side effect. All bridge accesses use signal_delivery_mutex.

PollNativeWorkerSignals now first holds renderer mutex then delivery mutex to
peek one GPU completion, enqueue it in CPU storage, and acknowledge GPU-queue
ownership. It releases both before taking the SDK global lock and delivery mutex
to inspect the guest publication slot and wake events. A busy slot retains the
CPU signal. Submission scopes now update CPU delivery depth under global then
delivery mutex, not global then renderer mutex. Delivery never acquires the
renderer mutex. This removes the two internal global-to-renderer acquisitions
identified in these signal methods; callers with preexisting outer locks and
other bridge paths are not thereby certified safe for geometry waits.

Worker-continuation polling, drain and capture capacity include CPU-pending
signals. Reset rejects CPU-pending delivery before erasing GPU tracking and
retains submission-depth bookkeeping, matching nested reset's existing scope
contract. The handoff is atomic to these consumers under renderer then delivery
locks. CPU storage is a nonallocating optional; an occupied slot cannot be
overwritten. GPU immediate-context calls remain under renderer serialization.

Added tests for nested submission deferral, payload preservation, an injected
failure after one CPU wake, resuming only remaining CPUs, and no repeated
delivery after completion. Full build and all 21 tests passed (25.30 seconds).
Scene-only executable SHA256:
DB31970C2AE6DE34FDF3FA2736CBCD665B30BE4A5609C05F185B6C04868313B3.
This build has not yet been playtested. Geometry comparison policy and writer
wait behavior remain unchanged; live worker/drain/reset validation is still due.

## Gameplay smoke run after CPU signal handoff

Run out/native-bridge-run/binding-validation-20260911-041305-e539ad26 used the
DB31970C executable with HookTimings, RetirementAudit, WorkerCallbackAudit and
native-movement-fire-input.txt. PID56524 started at04:13:05. Its exact executable
path and start time were checked before stopping after the final screenshot;
WaitForExit returned true and process absence was verified. No other process
was stopped. The retained logs end at04:17:15.668.

Inspected intro.png (Mission 1 street introduction), movement.png (player moved,
firing effects and ammo54/120), and after.png (another street position with
ammo101/120 and visible enemy effects). The bounded movement/fire input completed.
Final indexed upload count17,557,000, mesh builds766, entries427, errors0 and
vertex/index mismatches0. No error/critical or unreported-change matches were
found across retained game.log/game.1.log. Warnings were present: no emulated
GPU interrupt callback, absent ShaderDumpxe device, dismount stubs, and missing
GAME:\MISSION\M202\MISSION.CAM. Do not describe this run as warning-free.

Crucially, no worker callback registration, worker entry/return, or completed
worker-signal log was found despite the enabled diagnostics. This run establishes
startup/introduction/movement smoke coverage, not live execution of the new
signal-delivery handoff or reset/drain race. Direct D3D11/CPU delivery tests remain
the exercised evidence for that path. No geometry rejection was observed in the
sampled acquisition checkpoints, so the fallback race was not exercised either.
This hidden run is not an FPS benchmark and does not prove full writer coverage.

## CPU-ready polling bypasses renderer acquisition; handoff test exercised

PollNativeWorkerSignals now checks delivery state under only the CPU delivery
mutex first. A retained CPU-ready signal proceeds directly to global/delivery
locking, without first acquiring state.mutex. If GPU polling is required, the
precheck releases delivery_mutex before taking renderer then delivery locks and
rechecks state there. Dispatch rechecks depth/pending again. This avoids making
already completed delivery wait on renderer progress while preserving lock order.

The real D3D11 signal test now transfers each completed query through
NativeSignalDelivery before delivery, rather than testing the two components
only separately. It verifies total outstanding work across GPU acknowledgement,
rejects overwrite of the occupied CPU slot, defers completed work during a new
submission scope, and checks exact payload/CPU identity on delivery. Existing
tests cover completion order, query-covered rendering and partial multi-CPU wake
failure. These tests do not execute the full guest event/bridge concurrency path.

Full build and all 21 tests passed (24.33 seconds). Executable SHA256:
16F68591AEC7BC215395CF5A5B847C35BB54891DEE68E4AEBB51863B6580C588.
No live game run for this revision. Geometry comparisons remain enabled and no
writer wait has been introduced; remaining lock/lifetime checks are still required.

## Same-thread writer rejection classified

WriterScope records its creating thread. CopyObservedSet reports
same_thread_writers for active overlapping or unknown scopes created on the
acquiring thread; unrelated/adjacent ranges do not contribute. Rejection logging
includes this count. The current production scopes are synchronous/stack-bound;
a future ownership transfer would require a stronger thread-ownership contract.
This field is evidence, not permission to wait: zero same-thread conflicts does
not exclude a writer blocked on an SDK lock held by the caller or a third party.

Tests check nested same-thread scopes, exclusion of unrelated same-thread ranges,
reset of rejection fields after success, and zero same-thread attribution for
the existing background-writer/SDK-global-lock regression. That regression
continues to require immediate rejection, never waiting while its caller owns
the memory lock. No snapshot fallback or comparison policy changed.

Full build and all 21 tests passed (25.12 seconds). Scene-only executable SHA256:
EFD539C56A9C7EFE0AC2808CC49AB8A46FF8FDD81BE7F8B8B47A6C6F924034E4.
No live game run or performance claim for this revision.

## Submission gate alone does not pin guest backing storage

Rechecked NativeModelBuffers::Buffer/Find and concrete bridge retirement paths.
Buffer owns shared D3D11/CPU snapshots but only numeric address/physical extents
for guest backing. Find returns a pointer into the mutable registry. A copied
Buffer or retained vertex/index snapshot would keep those native objects alive,
not the guest allocation needed for a later snapshot after waiting.

The following paths retire under state.mutex without state.submissions, then
release that lock before the guest allocator/free call:

- 82134220 final resource destruction retires the model mapping and meshes.
- 821D7468/821D75F8 call RetireNativeModelBuffer, which retires backing aliases;
  CleanupNativeModelBuffer can then unbind streams and call 821D3EA0 to release.
- 821D3DC8 retires allocation-record aliases before its original pool release.
- RetireNativePoolBackings validates SDK physical allocation extents before
  taking state.mutex and retiring overlapping owners; its callers then free.

These are deliberate existing lock boundaries: no bridge lock is held across
guest free/unbind, and metadata-only retirement does not wait for refresh pacing.
Consequently a proposed draw retry that retains state.submissions but releases
state.mutex does not exclude backing retirement/reuse. Reusing native_vb/native_ib
pointers or their saved addresses after such a release is unsafe. Generation
validation after a copy would detect some changes too late to protect the read.

Before implementing that retry, an allocation-level lease must be honored by
owner cleanup, alias retirement, pool-suballocation release and whole-pool free,
or the draw must already own the exact content generation before releasing locks.
Pinning only the registry entry or D3D11 object does not satisfy this requirement.
Adding waits to free hooks also needs the same lock/reentrancy audit; deferring
frees changes allocator visibility and cannot be introduced as mere metadata
retention. This review changes no executable or comparison policy.

## Pool lease boundary: suballocation reuse precedes physical free

Read complete generated bodies for 821D3EA0, 821D3DC8, 821D3748,
821D3928, 821D3B68 and 821D4490, and cross-checked the direct resize
wrapper 821D4700. This refines the proposed allocation lease, not writer coverage.

821D3EA0 forwards the allocation record as r4 and the global pool as r3.
821D3DC8 enters the pool critical section (return PC821D3DE8) before inspecting
record+16. For an occupied record it calls 821D3748 (return PC821D3E30):
that helper clears the block node's allocation state, increases available bytes,
and merges adjacent free nodes. Only afterwards does 821D3928 check whether
any occupied block remains, optionally calling 821D3B68 to release the chunk.
The pool critical section is not left until return PC821D3E88. Consequently,
intercepting only MmFreePhysicalMemory cannot protect a suballocation: its
bytes may become reusable even when the backing chunk remains allocated.

821D4490 takes the same pool critical section (return PC821D44AC), selects
or splits a free block through 821D4090, and fills the caller's 20-byte
allocation record before leaving the lock. The record contains two iterator
pairs at +0/+8 and the payload address at +16; it is NOT the block node,
whose +16 is allocation state. Successful paths store the payload at
PC821D45B0 or PC821D46D8, before releasing the pool lock. A lease keyed only
by the numeric allocation-record address would also need an allocation
generation: 821D4700 releases and reallocates into the SAME record, through
calls returning to PC821D4724 and PC821D4734 respectively.

Implementation constraint: protect the allocation before the 821D3748
free/merge transition, not merely at backing free, and preserve that protection
across release/reallocation of the same embedded record. Existing registry
generation checks do not keep either iterator node or payload alive. Nor can
a retry acquire the guest pool lock while holding state.mutex: the release
path can reach RetireNativePoolBackings (which acquires state.mutex) while
still owning the pool lock. This is a concrete pool-to-renderer lock edge,
in addition to the SDK-global-memory-lock constraints discussed above.

The next lifetime implementation must therefore avoid renderer-to-pool lock
acquisition and cover suballocation release BEFORE entering its original
routine, together with the already identified whole-pool paths. It must also
resolve outer/reentrant pool ownership before introducing a blocking lease
wait. No wait, delayed free, comparison removal, or executable change was
introduced by this trace.

## Invalid-RGB attribute probe consumes native snapshots

The post-draw invalid-RGB diagnostic still decoded its declaration from guest
memory, then reread live IB/VB bytes for normal/tangent attribute samples.
It now decodes the retained native declaration and uses the mesh's immutable
source generations through CaptureSourceFloat3. This removes another payload
read outside the snapshot acquisition boundary; diagnostic results now describe
the inputs actually retained for that draw, not a potentially later guest write.

CaptureSourceFloat3 now accepts up to 65536 indexed samples and validates with
primitive width 1, preserving the RGB probe's original sample budget even when
the limit cuts a triangle. Clip replay still has its separate 96-vertex limit.
The first-three-sample log identifies index_position rather than claiming the
sample ordinal is a vertex ID. Zero/nonfinite counting is unchanged.

Added tests for single-index sampling, the full 65536-sample budget and rejection
above that budget. Existing mutated-source, signed-base, 16/32-bit index,
source-offset and invalid-range tests also pass. Build and all 21 tests passed
in 24.95 seconds; executable SHA256:
48F6E4909C6DA3A364631727A602E648242669723AB46B761C706253FA602256.
No live playtest or FPS measurement for this revision. The active-writer draw
fallback and steady per-draw comparisons are unchanged: dropping a conflicting
draw is not a substitute for the required retry/lifetime implementation.

## Constructor rejects failed allocation before payload copy

The native constructor previously ignored 821D4700's result and checked the
payload address only indirectly during publication, after the copy. Complete
821D4490 source has explicit success r3=1 and failure r3=0 exits; 821D4700's
epilogue preserves that return after releasing/reallocating the embedded record.
ConstructNativeModelBuffer now throws before copying when that result is zero,
or when a nonempty returned extent has a null/unaligned address or wraps 32-bit
guest addressing. This is a deliberate failure-path hardening over the original
constructor, which also ignored allocation failure, not a claim of retail
failure-path equivalence. Successful and zero-count fixture paths remain tested.

The production-extracted constructor fixture now models the actual allocator
status. Added VB/IB cases cover failure with a stale nonzero record, successful
status with null or unaligned storage, and overflowing extent. They require
cleanup/allocation only, no payload copy/publication, unchanged caller context,
and no new resource header. These tests stub the allocator; they do not prove
allocator concurrency or mapping lifetime. No rollback/deferred free is added.

Build and all 21 tests passed in 25.24 seconds. Executable SHA256:
2213ED84FBA5C1F5823BAAD1197FFD5A54382CA2286D2FC3A4FBE2EA17026FF7.
No live run for this revision. Directly retaining the constructor's source copy
still needs an explicit version/publication handoff; the existing guarded
destination snapshot and per-draw comparisons remain in place.

## Guarded attachment accepts pre-existing disjoint writers

CopyObservedSet already accepted active providers whose known physical ranges
were disjoint from every requested source. CommitObservedSet nevertheless
rejected any active provider, preventing attachment of those successfully
acquired native snapshots. Commit now validates active ranges against each
subscribed owner under the same queue mutex and rejects overlapping or unknown
ranges. It retains the global writer-epoch, owner lifetime and revision checks.
Thus a disjoint scope already present at acquisition may remain active through
attachment, but ANY newer scope entry still rejects the older observation.
Version() remains conservative and withholds tokens during all active writers;
only the guarded snapshot path can supply tokens for this newly accepted case.

Tests cover disjoint/adjacent scopes, attachment after a non-LIFO disjoint exit,
and rejection of the pre-entry epoch. A deliberately adjusted test token using
the current epoch of a separate disjoint owner isolates overlap rejection from
epoch rejection: an active one-byte overlap still cannot invoke the attachment
callback. Existing unknown-writer, owner-reuse, revision and paired-commit tests
pass. This is an attachment/reuse improvement, not permission for untracked
guest reads or proof of full writer coverage.

Build and all 21 tests passed in 24.51 seconds. Executable SHA256:
3F4DF74A904DAB7D5BA0F8BB566A3910FAE8B2891FBADCE2BD4A4E858B3E7D49.
No live run or measured performance benefit for this revision. Per-draw source
comparisons, active-conflict fallback and allocation-lifetime work remain open.

## Gameplay smoke run of guarded-attachment build

Run out/native-bridge-run/binding-validation-20260911-043922-9f571b4f used
3F4DF74A with HookTimings, RetirementAudit and WorkerCallbackAudit plus
native-movement-fire-input.txt. PID41836 started at
2026-09-11T04:39:22.2877771-03:00. Exact path/start identity was checked before
stopping it after the scripted sequence; WaitForExit succeeded and process
absence was verified. Logs and three inspected screenshots remain in that run.

intro.png shows the mothership introduction, visibly overbright; movement.png
shows street gameplay and ammo117/120; after.png shows a changed position/view,
ammo104/120 and enemy effects. This exercises successful construction and
movement/fire, not merely menus. It does not establish visual parity or FPS.

At04:43:22.568 the acquisition checkpoint reported guarded16777216,
unavailable0, checkedVB16776451/IB16776430, without-baseline765/786,
missedVB0/IB0. Final upload sample at04:43:40.022 reported19105000 uploads,
766 builds,427 entries,errors0 and vertex/index mismatches0. No allocation
failure, invalid allocation extent, snapshot rejection, unreported change,
error or critical log matched in the retained logs. Existing warnings include
ignored emulated-GPU interrupt registration, missing ShaderDumpxe device,
dismount stubs and missing M202/MISSION.CAM.

No worker registration/entry/return or completed worker-signal event was found.
Nor do these logs prove that the newly permitted disjoint-writer attachment
case executed; its direct tests remain the evidence for that interleaving.
The active-conflict fallback was not exercised. Comparisons remain enabled,
writer completeness is not established, and brightness remains unresolved.

## Actual suballocation release retires wrapper-independent overlaps

The early 821D3DC8 hook resolves extents through allocation_record-32 in the
model registry. If that wrapper is absent, it cannot identify surviving aliases.
Added a 821D3748 hook on the audited 821D3DC8 call edge (LR821D3E30), before
free-state/coalescing writes. Complete generated caller/callee bodies establish
that this edge holds the pool critical section and passes chunk in r3 plus the
packed block iterator in r4. Generated direct-call search finds this one edge;
other/unproven callers retain original behavior.

ReadNativePoolBlock checks the iterator list against chunk+8 and rejects a
null/sentinel node, then reads payload/size at node+8/+12 before the original
can unlink it. The hook maps that actual physical suballocation and retires
all registered overlapping owners, their subscriptions and cached meshes,
without requiring a wrapper at the allocation base. Null/wrapping payload
extents and invalid physical mappings reject before release. Zero-size blocks
do not map or retire an extent. The original helper is then called with the
original context, after the renderer lock has been released.

This uses the already established pool -> renderer order; it adds no waiting,
retained pointer or deferred free. It closes an alias-retirement gap at the
suballocation transition, but is NOT an allocation lease for a draw that drops
renderer locks. Early wrapper retirement and whole-backing retirement remain.

Tests exercise packed iterator decoding, null/sentinel/wrong-list rejection,
and retirement of interior VB/IB aliases without a base wrapper while preserving
an adjacent owner and rejecting stale lifetime tokens. The first build caught
a test inserted into the wrong fixture scope; placement was corrected. Full
rebuild and all 21 tests passed in24.45s. Executable SHA256:
F030634AEA6B66E80390EE904B97202CB9D9776318EAA7C4ECAB583EC304BF3C.
No live run for this revision. Tests do not execute retail pool locking or
prove arbitrary indirect release coverage. Per-draw comparisons remain enabled.

## Production block-release hook ordering fixture

Extended the existing production-source extractor to include the complete
821D3748 hook body alongside the native constructor. The test supplies explicit
memory mapping and renderer seams, but uses the actual NativeModelBuffers and
NativeBufferWrites registry/subscription behavior. The simulated original
helper checks its entry context and that expected alias retirement has finished
before performing its first free-state store. It also requires the renderer
lock to be released; mesh invalidation requires that lock to be held.

Cases cover enabled/audited release, disabled bridge, unaudited caller, and
invalid physical mapping. They check original-helper invocation count, retained
adjacent owner, invalidated old owner token, original return-context propagation,
and no free-state store when mapping rejects. This is direct production-hook
ordering coverage, not execution of the retail allocator or its pool lock.

Build and all 21 tests passed in24.48s; diff check passed. Only test/extractor
and documentation changed this turn. The executable remains the F030634A
revision, still without a live run of the new suballocation hook. Allocation
leases, safe conflicting-writer retries and comparison removal remain open.

## Live suballocation-hook execution confirmed

Added sampled invocation numbering to block-retirement logging, including
zero-owner releases; previously these left no evidence that the hook executed.
All 21 tests passed in32.37s (game startup overlapped the tail of the suite;
this time is not a performance comparison). Executable SHA256:
B6100B6A8BEE85AE9DBF42560055796960A52B4D14071E18A07EDAF5A36DD25A.

Run out/native-bridge-run/binding-validation-20260911-045130-98bc456f enabled
RetirementAudit and WorkerCallbackAudit with native-movement-fire-input.txt;
HookTimings was omitted. PID2916 start2026-09-11T04:51:30.3265939-03:00 had
its exact path/start identity verified before stopping after the scripted
sequence. WaitForExit succeeded; process absence was checked.

Block-release sequence4096 logged at04:53:22.969, establishing at least4096
actual hook invocations during loading. All retained sampled releases reported
owners0; no positive-owner release line matched, so the wrapper-independent
alias case remains fixture-tested, not live-observed. intro.png actually
captures EDF's Radar/Now Loading screen; gameplay.png shows street gameplay,
enemy effects and an active reload after scripted firing.

Latest acquisition checkpoint at04:54:19.502: guarded8388608, unavailable0,
checkedVB8387885/IB8387864,without-baseline723/744,missed0/0. Final upload sample
at04:55:19.061: uploaded15893000,builds766,entries427,errors0,mismatches0/0.
No error/critical, snapshot rejection or unreported-change lines matched the
retained logs. This validates execution/progress through the new release hook
and into gameplay, not full allocator-race coverage, visual parity or FPS.
Comparisons and the unsafe active-conflict fallback remain unchanged.

## Guest block state cannot serve as a draw lease count

Read complete 821D4090 allocation/split, 821D3FF0 backing initialization,
821D3928 occupancy query and 821D3A40 whole-pool destruction bodies to check
whether the existing allocator has a usable pin count. Within this audited
block family it does not: node+16 is a free/allocated discriminator. 821D3FF0
initializes the free record with0; 821D4090 selects only state0 and writes1
both for whole-block allocation and for the new allocated split record.
821D3748 clears the state to0 before merging neighbors, without decrementing
or consulting a reference count.

More importantly, 821D3928 reports an occupied chunk only when a node state
is EXACTLY1 (load at821D3970, comparison immediately following). Replacing1
with2 to represent an extra native reference would skip that node in the
occupancy query; release of another block could then classify the chunk as
empty and send it to 821D3B68. A nonzero test elsewhere does not make this safe.
821D3A40 independently detaches and frees all backing chunks without consulting
individual block states, so preserving state1 would not pin whole-pool teardown.

Decision for the retry implementation: do not increment or repurpose guest
block state, allocation-record+16 (payload address), or the chunk's available-
byte counter. Any new allocation lease must be native side metadata respected
by BOTH suballocation and whole-backing release, with generation identity and
the existing lock-order constraints. This eliminates a proposed shortcut; it
does not prove that unrelated engine object families lack reference counts.
No runtime change, wait or comparison removal was made in this trace.

## Pool destruction does not participate in the allocation critical section

Read complete 821D3F90 destructor and 821D3F28 constructor, and inspected
direct teardown/recreation edges in 8219CC68 and 8219E358. 821D3F28 creates
the sentinel, zeros pool+36 and initializes the critical section at pool+0.
Unlike 821D4490/821D3DC8, 821D3F90 has no Enter/LeaveCriticalSection calls:
it optionally reports pool+36, calls 821D3A40 with pool+28, frees the sentinel
and clears pool+32. 821D3A40 likewise does not take that critical section.
Therefore taking the guest pool lock alone cannot pin whole-pool destruction,
even independently of the renderer/pool lock inversion already established.

The optional report is a call to 8219F7A0 returning at821D3FBC. Complete
8219F7A0 loads a global receiver and conditionally dispatches its vtable+4
method at return PC8219F804; it is not a demonstrably callback-free log macro.
The callback runs before the 821D3A40 retirement hook. This does not establish
that the callback mutates geometry, but it rules out holding a non-reentrant
native lifecycle lock across the destructor on the assumption that no guest
callback can run.

8219CC68 calls the destructor at return PC8219CC94, frees the 40-byte pool,
clears its global pointer, and allocates/constructs a replacement. 8219E358
calls it at return PC8219E384 then frees/clears the same global pool. A native
pool identity must distinguish these lifetimes, even if host/guest addresses
are reused. Destruction admission should begin at or before 821D3F90, while
actual per-backing retirement remains at the existing 821D3A40/free boundaries.
Callback reentry and the earlier 8219E140 call in shutdown remain separate
edges to resolve before implementing a blocking drain. No code or comparison
policy changed in this trace; this is an additional lifecycle constraint.

## Shutdown drain is not pool admission control

Read complete 8219E140 (called by 8219E358 before pool destruction), complete
8213D1C8 and 82139760, the current native hook bodies, and the complete
extract-native-device-reset.cmake transformation. When shutdown owner+8 has a
device, 8219E140 first calls 8213D1C8, then clears bindings, releases three
shared resource pairs, destroys two global helper objects, and releases the
device through 82139760 before clearing owner+8. The resource pairs at
100/104,108/112,116/120 have their own CPU reference counts; these are not
allocation-record or block-node references and cannot be assumed to pin the
model pool.

Native 8213D1C8 routes to the hash-gated device-drain replacement. That retains
the issued-fence wait through 8213C928 and the device+10868 busy loop, omitting
the Xbox packet. Its inserted edf_native_drain_worker_signals polls GPU/CPU
pending signals and rejects unsubmitted work, then returns when pending is0.
Neither this helper nor the outer shutdown routine publishes a native pool
closing state or prevents subsequent writer entry. This is completion of
issued graphics work, not an allocation read lease or a proof that every CPU
geometry producer has stopped.

The whole-pool recreation route 8219CC68 also reaches destruction without this
particular 8219E140 prelude. Consequently the prelude cannot be the sole
lifetime boundary even if further caller analysis establishes thread quiescence
for normal shutdown. Keep device completion draining and allocation admission
as distinct requirements. No executable or comparison policy changed here.

## Block release excludes overlapping guarded snapshot acquisition

The audited 821D3748 hook now creates an exact physical WriterScope before
registry retirement and keeps it alive through the original block free/merge
helper. AllocationRelease has a separate WriterKind and diagnostic counter.
The scope itself retains no payload pointer; entry/exit briefly lock the write
queue, with neither queue nor renderer mutex held across guest execution.
It prevents CopyObservedSet from reading the released range even if an owner
is newly subscribed after the initial alias retirement. Existing scope-unwind
behavior conservatively invalidates observations on an exception.

Extended the production-hook fixture: the original-helper seam creates an
overlapping subscription after retirement and requires snapshot rejection,
while an adjacent owner still acquires successfully. After return the scope
must be gone, adjacent lifetime/revision unchanged and a fresh token usable.
Global writer epoch still advances for the release; older adjacent epoch
tokens remain conservatively invalid, as for every other tracked provider.
The original release context/lock-order tests continue to pass.

Build and all 21 tests passed in24.29s. Executable SHA256:
FB824750D38E0431CE759277B4ECFA44616F1106BCC6FCBDCE525E7E5AB52754.
No live run for this revision. This scope ends with 821D3748: subsequent chunk
free in its caller and whole-pool destruction still need their own exclusion/
lifetime coverage. It is NOT a persistent freed-range tombstone, a reader lease,
or protection for the current unguarded draw fallback. Comparisons remain on.

## Whole-backing free retains exclusion through guest execution

RetireNativePoolBackings now returns owned release scopes instead of ending its
responsibility at metadata retirement. After validating all SDK allocations and
physical extents, it establishes exact AllocationRelease scopes for every range,
then retires native overlaps. The 821D3A40, audited 8212FC28 callers and physical
8212F4B8 hook keep those scopes alive through their original guest routines.
No queue/renderer mutex spans the guest free. The return is nodiscard so a
future caller cannot silently drop protection at the helper boundary.

NativeBufferWrites::BeginReleaseSet owns stable-address WriterScope objects
through unique_ptrs, allowing the returned vector to move without invalidating
its intrusive entries. Tests cover moved ownership, new overlapping subscriptions,
non-LIFO partial scope removal, adjacent ranges and exception cleanup with
conservative invalidation of both aborted scopes. Existing extracted free-hook
fixtures needed their helper declaration/definition changed from void to the
scope return type; the first build caught that mismatch. Those fixtures still
return empty scopes and test forwarding/order, not the production mapping
helper. The guest-memory tests directly exercise multi-range scope lifetime.

Corrected rebuild and all 21 tests passed in24.73s. Executable SHA256:
E56E9AED6B8BEF1F50982AB9B05400EC5D75CC1A689151EB9283A1E4B6A3B8CA.
No live run for this revision. This closes the guarded-acquisition window
during these known free calls, not arbitrary indirect frees, the earlier pool
destructor callback, post-free stale address use, or the unguarded draw fallback.
Reader leases, safe retries and comparison removal remain unfinished.

## Extracted backing-free hooks retain real scopes in tests

Replaced the empty-scope retirement seam in native_immediate_tail_tests with
real NativeBufferWrites release scopes over a synthetic physical range. Both
the generic physical-free callback and MmFreePhysicalMemory seam now attempt
an overlapping guarded snapshot INSIDE the original provider and require
exclusion when native retirement is enabled. CPU/non-native/unaudited routes
require acquisition to remain possible. Each caller checks again after return
to catch leaked scopes. Retirement exceptions are now injected after scope
creation and checked for cleanup before any provider invocation.

This exercises production-extracted 8212FC28 and 8212F4B8 hook scope ownership,
including temporary return/move lifetime, rather than only the range-container
unit test. It still stubs SDK mapping/physical free and does not execute the
whole-pool 821D3A40 hook or actual allocator races. Existing ABI/caller routing
checks remain. Build and all 21 tests passed in24.40s; diff check passed.
No runtime code changed this turn; executable remains E56E9AED, without a
live run of whole-backing release scopes. Comparisons remain enabled.

## Detected allocation-release conflict cannot use live draw fallback

SnapshotFailure now counts relevant AllocationRelease scopes separately from
ordinary writers (known disjoint releases do not count; unknown release ranges
are conservative). The indexed draw checks that count on rejected paired
acquisition and throws before mesh-cache acquisition can read live VB/IB bytes.
The existing indexed-error handler counts/logs the failure and leaves that draw
unsubmitted. This deliberately fails a draw racing a detected release; it is
NOT a claim that dropping draws implements the required safe retry solution.

Tests check classification in the production block-release fixture and the
multi-range release tests, including clearing the count on a subsequent
successful disjoint acquisition. The full indexed draw is not extracted into
those fixtures; branch placement was source-reviewed before mesh acquisition.
Build and all 21 tests passed in25.51s. Executable SHA256:
E9BF34C7B877185C5FB436663ED94AB9068A55E57B5016582D0E5D4ABBF84F73.
No live run for this revision.

Important limits: missing/unregistered owners can bypass the paired guarded
path; earlier subscription/extent rejection is not release classification.
Ordinary active-writer fallback is unchanged. Post-free stale-address use,
reader leases and complete writer coverage remain unresolved. Steady per-draw
comparisons stay enabled. The acquisition log now distinguishes rejected
release conflicts from other cases that still use existing live validation.

## Live release-scope and rejection-policy smoke run

Run out/native-bridge-run/binding-validation-20260911-051303-7f3c36d5 used
E9BF34C7 with RetirementAudit, WorkerCallbackAudit and the bounded movement/fire
script. PID32532 started2026-09-11T05:13:03.9211286-03:00. Exact executable
path/start were verified before stopping after script completion; WaitForExit
succeeded and process absence was verified. intro.png shows the mothership
introduction (still overbright); movement.png shows gameplay movement, firing
effects and ammo94/120 at a different street position.

Both block and physical-backing retirement sampled counters reached4096, so
both scoped release paths executed. Latest paired acquisition checkpoint at
05:16:56.500: guarded16777216,unavailable0; checkedVB16776450/IB16776429,
without-baseline766/787,missed0/0. Final upload sample05:17:06.105:
uploaded18067000,builds767,entries428,errors0,mismatches0/0. No error/critical,
snapshot rejection, live-fallback rejection or unreported-change line matched
the retained logs. This run did not trigger the new draw-rejection branch;
it shows the release scopes do not disrupt this normal loading/gameplay path.
No performance or full writer/race-coverage claim follows. Comparisons remain
enabled, and reader leases/safe retries remain unfinished.

## Indexed ownership coverage distinguishes guest-header fallback

Added renderer-serialized counters at the indexed VB/IB lookup boundary:
both registered, only VB registered, only IB registered, neither registered,
and both mapped to physical extents. Power-of-two summaries count resolved
attempts, not submitted draws; earlier shader/binding failures and excluded
outside-scene work are not included. Fallback categories log bounded samples
of resource IDs, caller and vertex shader before reading guest headers.

These counters separate missing native creator/lifetime ownership from paired
snapshot rejection, which previous guarded/unavailable telemetry could not do.
They do not scan payloads or establish writer completeness. Existing behavior
is unchanged: unknown owners still use guest-header reconstruction. The next
live ownership audit can identify concrete missing creator/retirement paths
instead of inferring coverage from indexed-upload totals.

Build and all 21 tests passed in24.12s. Executable SHA256:
89B917A86E47367882F7103932BA906489FEE28F0B4FA24CFF4B8414BFCF5CF7.
No live run of these counters yet; no ownership-coverage or performance result
is claimed. Per-draw comparisons and the remaining fallback paths stay enabled.

## Mission 1 indexed ownership audit

Run out/native-bridge-run/binding-validation-20260911-052022-65a0dfaf used
89B917A8 with RetirementAudit and the bounded movement/fire script. PID59124
started2026-09-11T05:20:22.1039587-03:00. Exact executable/start identity was
verified before stopping after script completion; WaitForExit succeeded and
absence was checked. Inspected intro.png (overbright mothership introduction)
and movement.png (street gameplay, changed view/position, enemy effects,
ammo120/120). The latter does not by itself prove firing; the run used the
same scripted movement/fire input as earlier smoke runs.

Latest ownership checkpoint at05:23:11.689 covers8388608 resolved attempts:
both8388608,physical_pairs8388608,vb_only0,ib_only0,neither0. Matching guarded
count8388608 has unavailable0; revision audit checkedVB8387886/IB8387865,
without-baseline722/743,missed0/0. No guest-header-fallback line matched any
retained log, nor any snapshot rejection, live-fallback rejection, unreported
change, error or critical line. Final upload sample05:24:13.231:
uploaded16176000,builds766,entries427,errors0,mismatches0/0.

This identifies no missing registration path in the exercised Mission1 flow.
The next comparison-removal work for this path should focus on content-version
and lifetime handling, not infer that millions of draws need creator migration.
It does NOT certify other missions/resources, all indexed entry paths, producer
coverage or safe no-comparison rendering. No fallback policy changed.

## CPU index snapshots have independent native ownership

NativeModelBuffers::Buffer now retains index_contents separately from its
D3D11 index_storage, paralleling canonical vertex contents. RetainIndexContents
requires matching model generation, full extent and an observed queue token.
Publication attaches the guarded CPU index snapshot before GPU construction;
failure/rejection of later GPU attachment no longer inherently loses that CPU
owner. The indexed acquisition prefers this retained CPU candidate, with the
existing GPU snapshot as compatibility fallback when CPU ownership is absent.

CommitObservedGeometry validates and atomically attaches both canonical CPU
snapshots with both native GPU objects. Explicit updates and all/range/page
write invalidation clear index_contents alongside the other retained objects;
retirement erases it with the owner. A draw holding a shared immutable snapshot
keeps its bytes alive independently of registry invalidation. This does not
pin the guest allocation or certify an unreported writer.

New tests retain index contents without any GPU object, reject partial extents
and wrong generations, cover all four invalidation forms and reject old tokens
after owner reuse. Paired D3D11 tests now pass/verify canonical index contents.
Build and all 21 tests passed in24.53s; diff check passed. Executable SHA256:
7BC353A6B4E6352B6AA5F11CB5D73E5F2657DDE9C7C2CD16373330CFD491ACD6.
No live run or performance measurement for this revision. Guest source
comparisons and safe-retry/lifetime requirements remain unchanged.

## Canonical CPU index contents reach mesh GPU construction

Inspection found that the indexed draw supplied its guarded index snapshot as
a span only. When no reusable GPU index storage existed, NativeIndexedMesh
created a second CPU copy, losing canonical source identity for later cache
lookups. NativeMeshCache::Acquire and NativeIndexedMesh now accept an optional
shared index_contents owner and forward it to NativeIndexBuffer construction.
The bridge supplies this owner only from successful paired guarded acquisition.
Both entry points reject pointer/extent mismatches, including cache hits.
Existing compatible GPU storage may still be reused; this does not eliminate
the guarded guest-byte comparison or change unknown-writer fallback policy.

D3D11 tests cover cold construction retaining the exact CPU owner, a subsequent
identity hit with zero index byte checks, rejection of a different input address
on a cache hit, changed contents replacing storage without mutating the old
generation, and cache clearing/recreation retaining the supplied CPU owner.
Build and all 21 tests passed in 24.87s; git diff --check passed (line-ending
warnings only). Executable SHA256:
B6C76A3D77AFBCA0FC454C2361C79CCD8DA8D3B772034ADB4A1F2496B8EA5B34.
No live gameplay run or performance measurement for this revision. Writer
coverage, guest allocation lifetime, and safe retry remain open requirements.

## Guarded acquisition failure no longer falls through to native live copying

CopyObservedSet now classifies physical writer conflicts before checking owner
subscriptions. An allocation release remains visible after Unsubscribe or an
extent change. Tests explicitly cover both transitions inside a live release
scope, followed by the existing successful acquisition after scope completion.

In the registered physical VB/IB draw path, every failed paired acquisition now
throws before NativeMeshCache::Acquire. Previously only a classified allocation
release did so; ordinary active writers, missing subscriptions, extent mismatch,
and unknown tracking could still lead to unguarded native comparison/copy.
No waiting is introduced under renderer/submission locks. This is an explicit
native submission failure, NOT a retry or permission to render an old generation.
Missing registry owners/nonphysical pairs still bypass paired acquisition and
are not covered by this change. The hook's existing final guest routing also
remains: unsubmitted draws call the original retail helper, not the native-only
CPU tail. Removing that packet fallback and supplying correct conflict handling
remain part of full Xenos replacement.

Build and all 21 tests passed in 24.34s; git diff --check passed with line-ending
warnings only. Executable SHA256:
34495D570EB7B92CD4F50FC899B612A50D936C203E45D90073693B543281BD20.
No live run of this revision. Comparisons remain enabled; no performance,
complete writer coverage, no-dropped-draw, or full replacement claim follows.

## Rejected guarded geometry omits the retail indexed draw loop

The indexed hook now records native_geometry_rejected only when paired guarded
acquisition fails. In native-host mode, either actual submission or this explicit
rejection selects edf_native_indexed_cpu_tail instead of the original indexed
helper. Constant ownership still requires actual submission: rejection does not
claim that native rendering consumed dirty constants. Other unsubmitted paths
and non-native-host routing are unchanged.

Inspection of the generated tail confirms the preserved CPU prefix ends before
the index-header/packet loop. The existing actual-retail-versus-extracted-tail
test matrix now puts an out-of-arena index handle in device+12164 for every
CPU-tail execution, restores it only for the subsequent device-state comparison,
and verifies the tail did not change the binding. The matrix covers counts
3/6/6000, dirty/clean state, both index widths and command-buffer rollover,
without requiring native constant ownership. Existing checks compare CPU state,
ABI restoration, and the absence of inline draw packet writes. Helper packet
dependencies remain controlled: this is not proof of zero packets transitively
from every dirty-state helper, or of safe retry/full native rendering.

Build and all 21 tests passed in 25.57s; git diff --check passed (line-ending
warnings only). Executable SHA256:
CBD4658F6B7E7E02DB4E354754B1DCBD1A1EB0A1FF83BAA732297BF69B69B7A5.

### Live smoke validation of guarded failure routing build

Run out/native-bridge-run/binding-validation-20260911-053958-7557f591 used
CBD4658F with RetirementAudit and the bounded movement/fire input script.
PID39964 started 2026-09-11T05:39:58.7344525-03:00; exact executable and
start identity were verified before stopping after script completion. Process
WaitForExit succeeded and absence was verified. intro.png shows the mothership
with the existing overbrightness; movement.png shows street gameplay and
ammo102/120, confirming firing occurred.

Final acquisition checkpoint at05:43:56.110: guarded16777216,unavailable0;
all16777216 resolved attempts had both physical registered owners. Revision
audit checkedVB16776452/IB16776451, without-baseline764/765, missed0/0.
Final upload sample05:44:04.342: uploaded17895000,submitted17895000,errors0,
builds765,entries426,vertex/index mismatches0/0. No error/critical, snapshot
rejection, unreported-change, guest-header-fallback or geometry_rejected=true
line matched the retained logs. Thus normal Mission1 flow remains functional,
but the new failure branch was not triggered by this run. This is not an FPS
benchmark or proof of complete writer coverage. Comparisons remain enabled;
safe retry, other guest fallback paths and full native replacement remain open.

## Native-host indexed routing no longer depends on successful submission

OnPreSetup in edf2017_app.h clears the GPU plugin and forces native-host and
shader-bridge flags. The retail indexed packet loop therefore cannot provide
missing rendering in the shipped Windows configuration. The indexed hook now
always selects its CPU-only tail when edf_native_host is true, including empty,
unsupported, outside-scene and failed submissions. Only actual submission still
claims NativeConstantOwnership. Non-native-host routing is unchanged.

Expanded actual-retail-versus-CPU-tail tests cover primitive values1/2/4/13 and
counts0/3/6/6000 across dirty state, index width and rollover, retaining the
invalid index handle test on CPU-only executions. All CPU device-state and ABI
comparisons passed, with no inline indexed draw packet writes from the tail.
Build and all21 tests passed in24.52s; git diff --check passed with line-ending
warnings only. Executable SHA256:
246AEC114AAE986EA940DAE69F680EAF9A736B876477E2F170F717BCFD71CCBA.
No live run of this revision. Prior Mission1 coverage sample at05:43:57.798 had
16999782 requests, all submitted, so it did not exercise newly changed routing.

Remaining packet work is not eliminated by this change: unowned dirty-state
helpers can still encode packets; the immediate FD8F8 tail still retains its
original allocation/copy/packet behavior when native submission fails. Missing
native draws remain coverage errors, not successfully replaced rendering.
Geometry comparisons, safe retry and complete writer/lifetime coverage remain
unfinished.

## CPU-tail packet routing is independent of rendering success

NativeConstantOwnership::ForCpuOnlyTail now explicitly selects audited packet
helper routing from device/native-host mode, without a submission-success input.
Both native indexed and immediate hook tails use it. Failed/empty/unsupported
native draws consequently retain the same CPU-only helper replacements as
successful draws, rather than resuming Xbox state/shader packet generation.
Unsubmitted/error accounting is unchanged: this routing does not claim a native
draw was rendered or provide safe retry.

The factory intentionally requests no legacy immediate allocation suppression
extent. Native immediate tails do not call the guest allocator anymore. Tests
verify that a nested CPU-only scope clears the outer allocation extent, that a
nonnative scope masks native routing, and that both packet routing and legacy
allocation extent are restored at the correct scope exits. Actual immediate
CPU-tail tests use the factory in their native/nonnative routing matrix. Existing
real generated helper tests continue to cover packet omission versus preserved
CPU metadata/dirty-state behavior.

Build and all21 tests passed in24.48s; git diff --check passed with line-ending
warnings only. Executable SHA256:
D2CDB6554036A2AD105432FAB0EB3FD62AB0FA7B2CA2786C9D6A019F74EBE340.
No live run of this revision. Exact caller/bank/extent gates remain on individual
helpers: this is not blanket packet suppression or proof that all remaining
paths are native. Writer coverage, allocation lifetime, safe retry and geometry
comparison removal remain unfinished.

## Native main-state preparation directly calls native CPU helpers

The hash-gated ECB0 extractor now omits its three EAB0 program-load calls and
directly calls the native E950 upload and D750 derived-state CPU implementations.
It no longer dispatches through those retail entry points or depends on ownership
hooks to redirect them. Existing CPU-state transitions and ABI remain unchanged.
The main-state test matrix still compares full CPU memory (excluding command
buffer/cursor), return dirty masks and nonvolatile state against the retail path.
It now separately checks equal shader-cache callback order and verifies no
retail upload/derived wrapper was reached from the native path. Direct helpers
are the same implementations used by those wrappers in the reference path.

Build and all21 tests passed in24.50s; git diff --check passed with line-ending
warnings only. Executable SHA256:
61AB4BACC775136F85D90608E38DE04D089BD93B6E1AA8C52CD00234F83B932A.
No live run of this revision.

The native main-state tail now has only two retail helper calls, both EB68.
Its complete generated body was read: declaration/stride cache match, fence-age
rejection, E070 shader patch on refresh, optional atomic declaration-ID allocation,
and CPU cache/stride/timestamp publication. E070 is still suppressed by the outer
native packet scope. A direct native EB68 replacement must retain the ID-zero
allocation path and cache-busy return, not just the nonzero-ID cases currently
used by the main-state fixture. This is the next remaining dependency in this
particular chain, not a claim that all render helpers or geometry checks are gone.

## Shader-cache CPU replacement closes main-state retail helper dispatch

Added a whole-function SHA256-gated EB68 extraction retaining cache match,
fence-age rejection, atomic declaration-ID allocation, stride/cache publication
and return ABI, but omitting E070's Xbox instruction patch call. Native ECB0
now calls this implementation directly. A scan of generated native main-state,
shader-cache, shader-upload, shader-output and derived-state tails found no
remaining sub_ADDRESS(ctx,base) retail calls; ABI save/restore and runtime
primitives remain. This finding applies to this helper chain only.

New actual-code tests exercise ID-zero refresh for both shader variants and
counter seeds0/7/fffffffe/ffffffff, comparing native versus retail-with-patcher-
suppressed CPU memory and global ID state. They verify reserved-ID wraparound,
cache publication and return/nonvolatile/stack/LR preservation. The shader-code
destination is out of the committed arena, and native execution has no ownership
scope; retaining an accidental patch dereference would fail the test.

Main-state matrix now uses real cache hit/refresh/busy paths exclusively (the
two former synthetic cache-response modes are removed), compares the same full
CPU memory/dirty return/ABI, and runs native execution outside any ownership
scope. It requires zero retail cache/upload/derived tracing-wrapper calls.
The reference path retains its scope solely to omit the audited Xbox writes.

Build and all21 tests passed in23.50s; git diff --check passed with line-ending
warnings only. Executable SHA256:
599D151B86822211A84F832579C99F32BC3C22BEFB4DF722C992E11B8DE952AF.
No live run of this revision. Draw CPU prefixes still dispatch other audited
state encoders through hooks, and the larger geometry writer/lifetime/comparison
and safe-retry requirements remain unfinished.

## Draw prefixes omit five audited packet helpers directly

Added shared native-draw-prefix.cmake transformation to the indexed and immediate
CPU-tail extractors. It omits DF00 float constants, DB60 render words, DDA0 fetch
descriptors and DC20 vector packet calls. D938 is replaced by its required
dirty-mask return r3=r4&~0x100. CPU dirty clears, argument setup and mixed CPU-state
work remain. Immediate extraction already hashes its full source functions;
indexed extraction now also requires the full normalized FE358 SHA256 before
removing helper calls. Build dependencies include the shared transformation.

Existing actual prefix tests now require zero native constant/render packet
dispatches while retaining their CPU/ABI comparisons. New fully-dirty cases for
both tails execute without ownership scope, with invalid guest geometry pointers
and an exhausted packet cursor. They require every dirty bank cleared, restored
stride/failure flags, unchanged GPU scratch/cursor, no allocations/copies/packet
bytes and restored stack/LR. ECB0 remains a controlled CPU helper in these prefix
tests and has its separate real full-memory equivalence matrix. The only retail
call remaining in each generated draw tail is ECB0; direct integration is next.

Final build and all21 tests passed in23.63s; git diff --check passed with
line-ending warnings only. Executable SHA256:
1CC4EF7E1165362D65190B75C54F09E20B56B8E9EA0EAB1AB68339347FB16698.
No live run of this revision. No geometry comparison or writer/lifetime policy
changed, and safe retry/full Xenos replacement remain unfinished.

## Draw CPU prefixes directly integrate the native main-state chain

The shared prefix transformation now calls native ECB0 directly. Both generated
draw tails declare that native entry. A scan of the generated indexed, immediate,
main-state, shader-cache, shader-upload, shader-output and derived-state tails
finds no remaining retail sub_ADDRESS(ctx,base) calls. Runtime primitives and
ABI save/restore remain; this does not cover every renderer entry point.

Native-host draw hooks no longer create the thread-local packet-routing scope.
The direct CPU chain needs none. Non-native-host calls still explicitly mask an
inherited legacy scope before running retail code. Native submission and error
accounting are unchanged.

Draw tests now initialize real declaration/program/cache/fence state, and the
reference ECB0 wrapper calls the actual native CPU implementation rather than a
one-bit dirty-mask stand-in. The initial test failure exposed the stand-in's old
three-render-helper expectation: actual CPU preparation produces four in the
reference cases. This expectation was corrected; native counts remain zero.
Indexed comparison now covers all low fixture CPU/shader/cache memory below
the stack, excluding only packet bytes/cursor, in addition to ABI checks. Fully
dirty native draw cases execute the actual complete chain without ownership
scope and retain their no-copy/no-packet/dirty-clear/stride/scratch assertions.

Final build and all21 tests passed in23.78s; git diff --check passed with
line-ending warnings only. Executable SHA256:
28D2254F9CA107639BA769094173E211CF2D9BDCFD2DA69D1A274AFBB2B47597.
No live run of this revision. Geometry comparison removal, writer/lifetime
coverage, safe retry, visual correctness and broader replacement remain open.

## Bulk-copy writer inventory retains guest provider/caller identity

WriterSite now has explicit provider/caller fields separate from generated file/
line. Record deduplicates by all site fields plus owner/lifetime, with the existing
64-key batch cap and overflow counter. Null filenames are safe in comparison and
logging. The 8320 and independent8740 copy hooks capture entry LR before calling
the provider and attach provider/caller identity to completed-write notifications.
NotifyCompletedNativeBufferWrite strips provider metadata unless retirement audit
is enabled; ordinary invalidation/version behavior does not change.

Queue tests cover distinct callers/providers, repeated calls, physical aliases
and correct provider-versus-generated accounting. Production-extracted adapter
tests cover all source/destination alignments and lengths0..129, exact caller
attribution and zero-length omission. The main-copy mock deliberately clobbers
LR; the hook preserves that output while reporting the pre-call caller. Initial
build failure from nested aggregate defaults was fixed by explicitly supplying
all four default WriterSite fields in Record.

Final build and all21 tests passed in23.63s; git diff --check passed with
line-ending warnings only. Executable SHA256:
7A9357A6DF82D2939016D14B674D1352032101621C8540D588A182E4215B8F7A.
No live provider-site samples collected yet. This adds attribution, not new
producer coverage. It captures only writes overlapping owners at notification;
pre-publication writes, other providers and arbitrary raw stores remain separate.
The preceding Mission1 retained logs yielded no writer-inventory/provider_hits
matches, so do not assume repeating that workload will expose a new writer.
Geometry comparisons and lifetime/safe-retry requirements remain unchanged.

### Direct native render-state chain live validation

Run out/native-bridge-run/binding-validation-20260911-060512-cb755e83 used
599D151B, RetirementAudit, LoadingTrace and the bounded movement/fire script.
PID43176 started2026-09-11T06:05:12.3256294-03:00. Its executable/start identity
was verified before stopping after script completion; WaitForExit succeeded and
process absence was checked. transition.png shows the mothership introduction,
not the earlier loading screen, with existing overbrightness. movement.png shows
street gameplay/HUD/enemies and ammo100/120. LoadingTrace reports valid output
eligibility but that alone is not proof of loading-screen appearance/publication.

Latest coverage: indexed17411987 requests/allsubmitted at06:09:10.814;
immediate4835212 requests/allsubmitted at06:09:15.839. Last guarded checkpoint
16777216,unavailable0; revision checkedVB16776446/IB16776445,without-baseline
770/771,missed0/0. Final upload06:09:19.614: uploaded18638000,errors0,builds771,
entries432,vertex/index mismatches0/0. No error/critical, snapshot-rejection,
unreported-change, guest-header-fallback or unsubmitted-path line matched the
retained logs. This verifies the exercised normal Mission1 render-state path
after direct native helper dispatch, not every shader/resource/mission or failed
draw branch. No FPS claim follows, and geometry comparisons remain enabled.

## Native immediate wrapper uses an extracted CPU-only tail

Added tools/extract-native-immediate-tail.cmake, compiled into the game and
actual generated-code tests. SHA256 gates cover the complete normalized retail
FD428 and FD8F8 functions, not only boundary labels. FD428's CPU prefix is
retained through dirty-bank updates; its temporary stride is restored exactly
as at FD6C0. The new preparation tail returns zero guest storage and restores
the original ABI, without command rollover CF60, allocation C328, draw packets,
GPU restore bit4096 or scratch13076/13080/13088. The wrapper retains its original
prologue/call/epilogue but removes its now-unreachable E8320 copy, scratch cursor
commit and B718 flush. No allocation attempt means no allocation-failure flag.

Native-host FD8F8 now always selects this CPU-only wrapper; non-native-host
routing remains unchanged. NativeConstantOwnership still depends on actual
submission, so failed native rendering is not labeled as consumed state. The
old shared allocator hook remains for other legacy call paths; this new wrapper
does not call it. Dirty-state helper packet removal remains separate work.

Expanded immediate wrapper tests execute both original and extracted tails,
with and without constant ownership, rollover/no-rollover, six strides and
two vertex counts. CPU-only input uses an out-of-arena vertex pointer to expose
any accidental copy. Assertions cover CPU helper sequence, stride/failure flags,
dirty clears, no guest allocations/copies, unchanged GPU scratch/cursor, no
inline packet bytes and nonvolatile/stack/LR restoration. Controlled helper
dependencies mean this does not establish absence of all transitive packets.

Final build and all21 tests passed in24.58s; git diff --check passed with
line-ending warnings only. Executable SHA256:
E4B15C5500CC6369B995F79CF79BEF6352E90877498D67858AF31FAA9A5B83D5.
No live run of this revision. Geometry comparisons, safe conflict retry,
complete producer/lifetime coverage and remaining packet helpers are unfinished.

### Immediate CPU-only tail live smoke validation

Run out/native-bridge-run/binding-validation-20260911-055112-73acfc90 used
E4B15C55, RetirementAudit and the bounded movement/fire script. PID55076
started2026-09-11T05:51:12.5105111-03:00. Verified its exact executable/start
before stopping after script completion; WaitForExit succeeded and absence was
checked. menu.png shows mission/difficulty selection. loading.png was captured
too late and actually shows the mothership introduction, still overbright; it
does not validate loading-screen appearance. movement.png shows street gameplay,
HUD/enemies/effects and ammo100/120, confirming firing occurred.

Latest immediate coverage at05:55:06.147: requests4652591,submitted4652591,
unsubmitted0. Indexed coverage at05:55:01.036: requests15768846, allsubmitted.
Final upload sample05:55:07.744: uploaded16674000,errors0,builds769,entries430,
vertex/index mismatches0/0. Last guarded checkpoint8388608,unavailable0;
revision checksVB8387885/IB8387884,without-baseline723/724,missed0/0.
No error/critical, snapshot-rejection, unreported-change, guest-header-fallback
or unsubmitted-path line matched retained logs. This validates the exercised
normal immediate path after removing guest allocation/copy/inline draw packets;
it does not exercise failed submissions or prove all transitive packet helpers
are gone. No FPS benchmark or full writer/lifetime coverage claim follows.
