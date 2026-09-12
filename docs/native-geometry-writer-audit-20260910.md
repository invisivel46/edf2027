# Geometry writer boundaries

## Physical notification mapper integration tests

Extracted the existing completed-write address checks into
`MapCompletedNativePhysicalWrite` in native_physical_write_notify.h. The bridge
uses this helper for bulk/generated/unregistered-unlock notifications. This is
a behavior-preserving extraction: zero/nonphysical ranges are excluded; malformed
ranges beginning in a physical heap produce the whole-registry sentinel; valid
same-heap contiguous ranges produce physical address plus byte length. Mapping is
not a permission check and does not prove that a write has completed.

The SDK-backed memory test now allocates real pages in A/C/E physical aliases,
maps cross-page extents through this exact helper, and sends them through
NativeBufferWrites to NativeModelBuffers invalidation. It also checks cross-heap
extents produce conservative invalidation, empty writes are excluded and virtual
and XEX heaps do not enter the physical registry. The first test attempt used an
invalid one-byte vertex stride; correcting the fixture to a four-byte stride
preserved the production metadata invariant. Build and all 18 tests then passed
in 12.25 seconds. This does not cover arbitrary raw writers or establish an
immutable content certificate. No source comparisons were disabled.

## Unregistered buffer-header alias notifications

An explicit unlock previously invalidated physical aliases only when its owner
was registered in NativeModelBuffers. For an unregistered owner it invalidated
that mesh key alone. A generic VB/IB header can describe memory overlapping a
registered model buffer without sharing the model's owner key; source comparisons
were still needed to catch such content changes.

The two buffer unlock hooks now pass base and buffer kind to the notifier. For
unregistered owners only, it decodes address/size words +24/+28 and queues the
physical extent through the existing checked CPU-write mapper. The next indexed
consumer drains those notifications before looking up retained model storage.
Registered owners retain the existing immediate alias invalidation behavior.

VB extent decoding masks address with `0xfffffffc` and packed size with
`0x03fffffc`, matching lock wrapper 82134958 and unlock 821349B8. IB decoding
preserves plain address and byte size, matching 82134A78/82134AD8. This is a
conservative whole-buffer notification even for nested or partial unlocks, not
an immutable content-version certificate. Nonphysical/unmapped destinations
retain the mapper's existing limitations; arbitrary raw writes remain uncovered.

All 18 tests passed in 12.21 seconds after building. Added tests cover packed VB
versus plain IB decoding, unknown-owner lookup, and mapped alias-range invalidation
without invalidating an unrelated model. The range test supplies physical mapping
explicitly; it is not an end-to-end runtime test of a generic alias unlock.
Runtime validation of this revision remains pending. Comparisons remain enabled.
Rebuilt `edf2027-native-lock-timings.exe` SHA256:
`F35403E9258126A1F41761C5DFAC799EC49B6788592BD45183B474A4A5706F46`.

Runtime follow-up: this hash ran as owned PID 42644 from 15:31:20 in
`out/native-bridge-run/binding-validation-20260910-153120-1a0c5d11`, with the
movement/fire script, fresh defaults and hook timings off. Startup logged three
unregistered-owner unlock notifications without failure. This establishes that
the new decoding path executed, not that an overlapping model alias existed in
that workload. Captures show gameplay before input (120/120 ammunition) and after
movement/fire (100/120, changed position and enemy effects). The 15:34:57 sample
reported 15,738,000 indexed submissions, 765 builds, 426 entries, zero indexed
errors, source mismatches or evictions. System32 D3D11 was loaded; no Xenos-named
module appeared in the inspected module list. The packaged PE audit passed with
rexruntime/FidelityFX as the two non-system DLLs and no Xenos import or staged DLL.
The process was stopped after exact-path verification and completion of the input
script. This is a bounded gameplay regression test, not full alias-writer coverage,
visible FPS measurement, complete-mission validation or brightness certification.

## Rejected storage is no longer rechecked during rebuild

`NativeMeshCache::Acquire` now remembers index/vertex storage that failed its
existing-cache source comparison and excludes that same identity from the
construction candidates, including weak-resource-cache candidates. Previously,
construction could compare the rejected source again. A distinct supplied native
storage candidate is still validated normally. Cold builds, unchanged cache hits,
dynamic updates and the first write-detection comparison retain their semantics.

Tests restore modified vertex/index bytes at the before-snapshot callback. This
makes a repeated constructor comparison match, so asserting a fresh storage
identity detects accidental reconsideration of a rejected candidate. They also
verify that the new allocation contains the restored snapshot. This callback is
a deterministic test boundary, not a claim of arbitrary concurrent-write safety.

This removes redundant mismatch/rebuild checks, not the steady per-draw checks.
No FPS improvement is established. Complete producer ownership is still required
to remove those remaining checks safely.

Build and all 18 tests passed (12.17 seconds), including the new snapshot-boundary
cases. The rebuilt `edf2027-native-lock-timings.exe` now has SHA256
`52C14C138DD05B8096101A2E32AFC41C4ED5F69DC63018856DA66B81D461BE7C`;
this supersedes the executable bytes used in the earlier hidden timing run.
This revision has not yet been playtested.

## Current measurement follow-up: cache-hit comparison volume

Added separate vertex/index check counts and candidate-byte totals to
NativeMeshCache::Acquire. They count source-validation attempts on existing
compatible cache entries, not construction/upload copies or actual memory
traffic (a mismatch can short-circuit). Native-generated index identities are
excluded from index-byte counts because those paths already avoid source scans.
The bridge reports these totals every 100,000 indexed submissions. Counters are
monotonic like the existing hit/build counters; cache Clear does not reset them.

Tests check one cold build followed by one hit and independently verify that a
native generated-index hit contributes no index comparisons. No comparison was
removed by this instrumentation. Runtime measurements are needed to prioritize
the remaining vertex versus index producer-ownership work; lifetime publication
and sampled zero mismatches are still not complete writer certificates.

## Evidence inspected

### Constructor/header relocation trace, September 10 follow-up

Inspected the complete model VB constructor `821D7530` (generated shard 1),
IB constructor `821D76A8` (shard 66), header builders `822CFDC0` (shard 5) and
`822CFE58` (shard 0), and relocation helper `822D01C0` (shard 80).

Both constructors retire the old model owner, allocate via `821D4700` using
the embedded allocation record at owner+32, load its CPU address at owner+48,
and bulk-copy the model bytes using `821E8320`. VB size is stride*count; IB size
is count*2. The header builders clear the first 32 bytes and write CPU resource
metadata; no GPU command submission occurs in those builders. The VB header
starts with type 1, address word 3 and size/format word
`0x10000002 | (bytes & 0x03fffffc)` for this constructor's zero flags. The IB
header starts with `0x20000002`, a zero address word and the supplied byte size.
Both set refcount word +4 to 1 and dirty sentinel +20 to `0xffff0000`.

`822D01C0` calls the type decoder `82134190`. Its type-1 branch adjusts the
address at +24 while preserving the low two bits; its type-2 branch adds the
address directly. Both model constructors call it with their allocated CPU
address after building the header, then publish count/stride metadata and return.
The current native hooks call `PublishNativeModelBuffer` after that return.
Thus these known relocation calls precede publication; they are not evidence
of an untracked post-publication relocation.

An exact direct-call search across current generated/default found only these
two calls to `822D01C0`, at shard 1 line 9235 and shard 66 line 9184. This does
not exclude indirect calls. Its other resource-type branches were not established
as model-buffer update paths and must not be replaced by the model-specific rule.

Cleanup `821D7468` is already hooked to retire native storage before guest cleanup;
the IB counterpart `821D75F8` is likewise hooked. Header attachment and lifetime
cleanup therefore do not supply the missing proof of immutable vertex contents.
The remaining investigation must follow aliases of the payload itself, including
owner+48 and the resource-header address, rather than treating CPU header setup
as residual GPU emulation. No runtime behavior was changed for this trace.

The analysis project's notes/render/0330-the-mesh-array-and-the-ten-broken-meshes-that-we.md
identifies the model subset loader and the VB/IB constructor arguments. A direct
call search of current generated/default (including ignored files) finds one
static call to each constructor, both in edf2017_recomp.29.cpp:

- sub_821D7530 at line 8677: model vertex creation.
- sub_821D76A8 at line 8719: model index creation.

This bounds direct constructor calls, not indirect calls or subsequent writes.
The current bridge publishes metadata after the complete constructor and retains
the owner+48 CPU address. That address remains guest-accessible; constructor
coverage does not prove immutable contents.

## Unlock semantics, checked against generated code

Additional direct access-path trace: the current generated code contains one
direct call to each buffer lock wrapper. In shard 12, the VB caller locks 120
bytes at offset zero, copies owner+144 through `821E8320`, and unlocks. In shard
10, the IB caller allocates 12 bytes and writes six uint16 indices (first value
from r30, followed by 1,2,3,4,5) directly through the returned lock pointer before
unlocking. The latter is a concrete inline writer, not a bulk-copy call. The
unlock notification covers its extent even with generated scalar observation off.
Neither direct-call count excludes indirect dispatch or payload-pointer access
outside the lock API. `82134840`, used by model cleanup, returns a referenced
device through an output pointer; it is not a buffer-payload lock.

sub_821349B8 (shard 71) passes owner+24 with the low two bits masked as r4,
sets r5=0, and tail-calls sub_82134640. sub_82134AD8 (shard 16) passes owner+24
without masking and likewise sets r5=0.

sub_82134640 (shard 44, line 4392) atomically subtracts 0x100 from owner+0.
It processes dirty ranges only when the previous count field (mask 0xF00)
equals 0x100. For that final unlock it reads owner+20, replaces a non-sentinel
range with 0xFFFF0000, decodes 128-byte range units, and calls sub_82141AB8.
An optional second range at owner+24 is processed only when r5 is nonzero.
The two buffer wrappers therefore do not exercise this second-range path.

There are four direct calls to sub_82134640: the two buffer wrappers above,
plus sub_82139B90 (shard 52) and sub_82139BA8 (shard 68). The latter wrappers
derive page-masked addresses from resource+32/+48; 82139BA8 first dereferences
its argument+24. They are not evidence of an additional model-buffer writer.
Indirect calls remain outside this static count.

## Consequences for native ownership

Keep the original unlock CPU bookkeeping. Current buffer notifications occur
after every wrapper return, including nested unlocks; they conservatively retire
cache entries, not certify that an entire producer transaction has finished.
Do not promote notified_updates into an immutable content version on that basis.

Bulk-copy and file-read notifications cover their completed physical writes,
but the bridge explicitly does not cover arbitrary scalar/inline stores through
the model CPU pointer. The next ownership boundary to establish is the set of
consumers of those published data pointers, including indirect writers—not more
constructor hooks or a blanket unlock bypass. No comparison was removed, no
game binary changed, and no performance claim follows from this static audit.

## Follow-up: pointer access and actionable mismatch identities

Inspected the nearby model lifecycle routines rather than assuming address
proximity implies a data accessor. 821D74F8 and 821D7670 call the respective
cleanup routine, then release the allocation record through 821D3EA0; that
routine tail-calls the already hooked 821D3DC8. 821D75B8 initializes the index
owner: 82476DE8 zeros the five words of its owner+32 allocation record, then
the constructor clears its initialized flag/count. These are lifecycle paths,
not newly established geometry writers.

A narrow getter-pattern search found 8243DF88 returning argument+48 and
82360E98 returning argument+16. Neither is established as a model accessor:
the former has no direct generated calls, and matching an offset does not
establish receiver type or indirect-call use. Do not add ownership hooks on
that evidence. A complete pointer-consumer trace remains unfinished.

Added last-mismatch resource keys to NativeMeshCache. They are recorded only
on the existing failed source checks, with no additional byte comparison.
The indexed bridge emits the last VB/IB/declaration/VS/variant for each kind
when cumulative mismatch counts change at an existing logging interval.
This is bounded latest-event evidence, not a complete event stream or writer
PC attribution. It cannot report changes hidden by prior explicit invalidation.
Tests assert vertex/index identity recording and independent retention. This
will let a future detected mutation identify the actual resource to investigate
instead of relying on an aggregate mismatch count.

Rebuilt edf2027-native-instance-bindings.exe; all 18 tests passed in 12.58
seconds. No game was launched. This diagnostic change does not remove any
remaining comparison or establish a speed improvement.

## Load-time native index ownership

Model index publication now creates NativeIndexBuffer after the complete loader
constructor. NativeModelBuffers retains its shared generation, allowing the first
mesh and later variants to reuse it. Index conversion requires no shader layout.
Model retirement releases ownership; explicit updates and overlapping completed
writes clear it. Earlier meshes retain their immutable generation. Live model
storage is outside mesh-cache eviction, subject to the existing per-resource
extent limit; resident memory needs measurement.

Mesh Acquire still checks the published allocation against live guest bytes and
falls back to fresh storage on mismatch. No geometry comparison is disabled.
Queued copy notifications are drained before publication so initialization does
not immediately retire the newly published generation. This is not complete
writer coverage or protection from arbitrary concurrent stores. Later updates
currently clear ownership and rebuild on demand, not republish immediately.

Tests cover first-use sharing, reuse after cache clear, stale source rejection,
retained-generation immutability, explicit/overlapping update invalidation and
retirement. Full build/all 18 tests passed in 11.98 seconds. Current candidate:
822970482DD771F5E95F93AC6A83C64CD7831CD3C8CFA8250AFE33DE35232AB6.
Gameplay validation and actual first-draw reuse measurement remain pending.
No FPS gain is claimed.

## Load-time consumption measurement

Added counters for published index reuse/rejection on mesh builds (cache hits
excluded), plus publication-time retained count and accounted storage bytes.
These bytes include CPU source/value storage and GPU payload, not driver
allocation overhead, and overlap mesh accounting when shared. Counters are
tested. Full build/all 18 tests passed in 12.68 seconds. Candidate SHA256:
AD873991C92E4B7E7771007143D5AA22A63BA59DCC931E6AAC14D2BF307F18E9.

Owned PID15740 ran 03:39:12 through 03:41:57. Artifacts:
out/native-bridge-run/binding-validation-20260910-033912-76a44fae/.
Inspected gameplay.png; zero error log lines. Final sample: 2,825,000 indexed
draws, 722 builds, 2,824,278 hits, 378 entries, zero geometry mismatches or
evictions. Only THREE builds reused published indices, zero rejected generations.
Across 7,956 publication records, maximum retained count was 3,940 and maximum
accounted storage 6,097,392 bytes. These maxima need not describe the same instant.
Process working set at the ending sample was 1,028,517,888 bytes, not a delta or
GPU residency measurement. Stopped exact-path-validated owned PID and waited.

This proves the new path executes, but shows poor retention to mesh consumption.
Publication logs repeatedly drop retained counts during loading. The bounded
256-range write queue can conservatively invalidate every owner on overflow;
an all=true batch is observed early, but later batch logs are capped. That is
a concrete candidate cause, not yet proof of every later retention drop.
Next: preserve exact write coverage while coalescing duplicate/overlapping
ranges, and expose later all-invalidations. Do not remove source checks to hide
the issue. No visible performance conclusion (hidden/occluded run).

## Exact write-interval coalescing

NativeBufferWrites now keeps sorted disjoint nonadjacent intervals and merges
overlap/adjacency before checking its 256-slot capacity. Duplicate writes at
capacity no longer overflow; a bridging range can release slots. No unwritten
gap is filled. Invalid extents and genuinely disjoint overflow still set all=true;
that condition remains sticky until Drain. Synchronization and pending semantics
are unchanged. Recording may scan/shift up to 256 intervals under the queue lock;
runtime cost versus reduced invalidation remains to be measured.

Tests cover 400 concurrent duplicate writes, 257 disjoint ranges overflowing,
duplicate/bridging writes at capacity, physical end adjacency and overrun, and
40 unordered batches checked against an independent byte-union oracle. Existing
model overlap tests still pass. Later all-invalidations now log at power-of-two
counts even after the initial eight-batch log cap, including before publication.

Full build/all 18 tests passed in 12.27 seconds. Candidate SHA256:
2C37C6E2F5C55044E5B3E389D4E5C66B0B929883BD2422259656E7121CBE94F8.
No game run of the coalescing change yet; improved model index retention is not
claimed until measured. Geometry source comparisons remain enabled.

## Coalescing runtime result

Ran unchanged 2C37C6E2...1CBE94F8 as owned PID46592, 03:45:55 through 03:48:46.
Artifacts: out/native-bridge-run/binding-validation-20260910-034555-05192a64/.
Inspected gameplay.png; all log files contain zero error lines. Final indexed
sample: 4,178,000 draws, 721 builds, 4,177,279 hits, zero source mismatches.
Published index reuse remains THREE builds, zero rejected generations: no
observed retention improvement over the prior smoke run.

New logging confirms a full 256-range all=true batch at 03:47:19.901 invalidated
7,906 model owners before publication (cumulative all_batches=4). This supplies
direct evidence of a large blanket invalidation during mission loading, rather
than inferring it solely from falling retained counts. Initial and earlier full
batches also appear. Coalescing is insufficient for the remaining disjoint writes.

Next implementation candidate: a bounded physical-page bitmap for valid-range
overflow, retaining conservative page-overlap invalidation without touching every
unrelated owner. Invalid/unknown extents must retain the all=true fallback.
Page false positives must be explicit; no source checks may be removed on this
basis. Stopped only exact-path-validated PID46592 and waited for exit. This hidden
run is not a visible FPS benchmark; no binary changed during measurement.

## Bounded page overflow fallback

Valid-range overflow now allocates a 16 KiB physical-page bitmap. It records
all prior exact ranges and the overflowing write, then accumulates subsequent
valid writes in page mode until Drain. Consumers receive a shared const bitmap;
the producer drops its mutable owner at Drain and starts a fresh batch. Invalid
physical extents and bitmap allocation failure retain sticky all=true behavior.
Normal batches retain exact interval semantics without allocating a bitmap.

NativeModelBuffers invalidates only owners overlapping a touched page in this
mode. Same-page false positives are intentional; unrelated pages are preserved.
Owners spanning multiple touched pages receive one notification. Batch logs now
include touched-page counts and report page-fallback batches after the normal cap.

Tests verify earlier/subsequent writes, cross-page and physical-end coverage,
unrelated owner preservation, a spanning owner, and immutable bitmap lifetime
across Drain. Existing exact-union and invalid-extent tests remain. Allocation
failure behavior is implemented conservatively but not fault-injected in tests.
Full build/all 18 tests passed in 12.05 seconds. Current candidate SHA256:
3030419EB16034D434E5896A653D749D43D4EF0163D8C962130FDB6FAEF1A5CC.
Runtime reuse/memory/performance measurement remains pending. Geometry byte
comparisons remain enabled; no FPS improvement is claimed.

## Page fallback runtime verification

Ran unchanged 3030419E...AEF1A5CC as owned PID45788, 03:51:57 through 03:55:01.
Artifacts: out/native-bridge-run/binding-validation-20260910-035157-7825b1bb/.
Inspected gameplay.png. All log files contain zero error lines and zero all=true
write batches. Overflow now reports page counts and bounded affected-owner sets.
The late sampled batches affected 0/1/2/16 owners rather than the earlier global
invalidation of 7,906 owners. These are separate runs, not frozen-state replays.

Final indexed sample: 5,394,000 draws, 718 builds, 5,393,282 hits, 379 entries,
zero source mismatches or evictions. Published index consumption: 698 reused
builds, zero rejected generations, compared with three reused builds in the
preceding coalescing-only run. Thus model-owned storage now reaches the great
majority of observed mesh builds. Cache hits are excluded from this counter.

Across 7,956 publication samples, retained count peaked at 3,985 and accounted
index storage at 9,034,776 bytes; maxima need not be simultaneous. This includes
CPU snapshots and GPU payload, excludes driver overhead, and overlaps mesh
accounting where storage is shared. Increased retention is expected, not a
measured total-memory regression. Stopped only exact-path-validated PID45788
and waited for exit. No executable changed during the run.

This verifies useful load-time index ownership for startup/mission opening and
stationary gameplay. Full combat/writer coverage, vertex storage ownership and
visible performance remain unverified; source comparisons stay enabled.

## First-use model vertex ownership

Model vertex owners now retain their last native conversion allocation after a
successful mesh Acquire. Unlike index publication, conversion still occurs on
first use because attributes depend on the native shader signature. Acquire can
accept this retained storage after cache eviction or from another compatible
mesh. Existing device/layout/stride/source checks remain authoritative; different
conversion contracts or bytes allocate fresh storage. Only the latest conversion
is retained by the model; older mesh/draw references own their generations.

Retirement, republishing and explicit/exact/page/all write notifications release
model vertex storage as well as index storage. Attachment checks owner kind and
lifetime generation; bridge operations remain serialized under submission/state
locks. This is not an asynchronous update API or proof of complete writer coverage.
Resident storage now extends beyond mesh-cache eviction and needs measurement.

Tests cover reuse through an independent cache, stale-source rejection, retained
generation immutability, explicit-update clearing and rejection after owner
generation reuse. Full build/all 18 tests passed in 12.30 seconds. Candidate:
E62A0BD994C2115E31C8112606189A9E23CADC88EC9CE6EB2F2BFC37E8CB2ADB.
Runtime validation and memory/reuse measurements remain pending. No geometry
comparison was removed and no FPS gain is claimed.

## Model vertex ownership measurement

Added retained-vertex reuse/replacement counters at mesh construction, excluding
cache hits, plus periodic model-owned vertex/index payload totals. Counters have
unit coverage. Full build/all 18 tests passed in 12.51 seconds. Candidate SHA256:
1E17CE680F3BDECFCB561A758BE4759A84A876C9FBF53F99B19002098DD3BF7C.

Owned PID40508 ran 04:01:27 through 04:04:33. Artifacts:
out/native-bridge-run/binding-validation-20260910-040127-581f67cd/.
Inspected gameplay.png; all log files contain zero error lines. Final indexed
sample: 5,801,000 draws, 718 builds, 5,800,282 hits, 379 entries, zero evictions
or source mismatches. Published index reuse remains 698 builds. Retained vertex
reuse: ONE build, zero replacement builds. This demonstrates the path executes
but not a substantial additional benefit in this non-evicting smoke workload.

Final retained storage: 379 vertex allocations accounting for 37,536,632 bytes;
3,985 index allocations accounting for 9,034,776 bytes. These include CPU/GPU
payload shared with mesh references, not additional memory deltas or driver
residency. Exact-path-validated owned PID was stopped and WaitForExit completed.
No visible FPS claim, full-combat coverage or complete writer ownership follows.

## Restoring index ownership after updates

After a successful live-validated mesh Acquire, the bridge now attaches its index
allocation back to the matching model owner when different/missing. Previously,
write notifications cleared model index ownership and later replacement storage
remained only mesh-owned until the model was reloaded. RetainIndexStorage checks
owner kind and lifetime generation. It is called under the same submission lock
as invalidation; lifetime identity is not a substitute for source validation.

Tests cover update invalidation, attachment of the validated replacement, reuse
after cache clear, wrong-kind rejection and rejection after owner-address reuse.
Full build/all 18 tests passed in 12.15 seconds. Current candidate SHA256:
597ED4B22C7E454D4FCC943F8A542954E1F94BA1113702B2E41AB518538AEC56.
Publication occurs on next validated use,
not immediately at every writer boundary; complete writer ownership and removal
of remaining byte comparisons are still unfinished.

## Expanded input validation of restored index ownership

Owned PID22392 ran 04:07:36 through 04:14:10 with candidate 597ED4B2 above.
Artifacts: out/native-bridge-run/binding-validation-20260910-040736-a6e270a6/.
The launcher now accepts InputScript; tools/native-movement-fire-input.txt adds
timed forward/strafe/turn and right-trigger inputs after the intro. Twelve events
were accepted. Inspected after-input.png: street gameplay renders, AF14 ammunition
is 101/120, supporting that firing affected gameplay. A final still does not
establish every movement event, particle correctness or full combat coverage.

Final sample: 34,423,000 indexed draws, 765 builds, 34,422,235 hits, 426 entries,
zero source mismatches or evictions. All retained log files contain zero error
lines. Published index reuse reached 745 builds with zero rejections; retained
vertex reuse remained one build with zero replacements. Model-owned payload:
426 vertex allocations / 39,909,672 bytes and 3,995 index allocations /
9,114,360 bytes, shared with mesh references, not incremental residency.

Stopped only the exact-path-validated owned process and waited for exit. This
hidden-window diagnostic is not a visible FPS benchmark. Remaining live-byte
comparisons stay enabled; zero mismatches does not prove complete writer coverage.

## Bound publication diagnostics during model loading

Source inspection found VisitIndexStorage traversing the entire registry after
every nonempty index publication solely to report retained payload. Thousands
of publications therefore incurred quadratic diagnostic work as ownership grew.
The detailed publication log now runs for the first eight publications and then
at powers of two, with a cumulative publication number. For 7,956 publications
this is 17 registry scans/log lines instead of 7,956. The counter is process-wide
and serialized by the existing state lock. Periodic indexed-draw ownership totals
remain unchanged; creation, publication, invalidation and source checks are not
conditional on sampling.

Build and all 18 tests passed in 11.94 seconds. Candidate SHA256:
E663925F40975CC17604FF7E8788324F6F702346F56ABA540CADD64E7EC7F443.
No runtime loading-time or visible FPS measurement of this change yet. This
removes unnecessary diagnostic work, not a remaining GPU emulation contract.

## Removing duplicate index source validation

NativeMeshCache previously scanned a retained index candidate with Matches and
then passed it to NativeIndexedMesh, which immediately scanned it again. Cache
builds now request IndexReuse::ReplaceStale: the constructor performs the single
authoritative device/width/source validation and creates fresh storage if stale.
Direct construction keeps RequireMatch by default, preserving its mismatch error.
Reuse/rejection telemetry uses the resulting allocation identity, not another
source scan. Cache-hit comparisons and vertex source checks are unchanged.

Tests retain coverage of changed guest bytes, retained-generation lifetime,
published rejection/reuse counters and strict direct-constructor rejection.
Added direct replacement-mode tests for fresh allocation from changed bytes,
preservation of old contents, and reuse of unchanged storage. Build/all18tests
passed12.41s. Independent candidate edf2027-native-index-validation.exe SHA256:
AB7004602194DD5D4E3382175F1941389B1EBA3FB507B8DD987B35EEB8C83290.
No runtime or FPS measurement of this candidate yet. This removes one redundant
scan on retained-index mesh builds; it does not remove per-draw guest-memory
change detection or establish complete writer ownership.

## Backward move writer notification

Found an uncovered bulk writer: 821EA320 (generated shard25). Equal addresses
return without writing; signed destination < source delegates to already tracked
821E8320. Signed destination > source copies backward inline (byte alignment,
aligned or assembled words, byte tails), consuming r5. The new hook captures
entry destination/count, preserves original execution and queues completion only
for this independent backward branch. Forward notifications are not duplicated.
The existing physical-extent validator/queue handles actual invalidation.

Added the unchanged generated routine to the fixture. 3,640 backward-copy cases
cover four alignments, seven positive displacements, lengths0..129, overlapping
and disjoint copies. Output/guard bytes match std::memmove; destination return
is preserved. Build/all18tests passed12.08s. Candidate index-validation SHA256:
FA3DED2B7D6F90C8B17571C6513AD61FCC6E8AD75EA60328267609C2131A29EF.
These tests establish copy semantics, not yet the extracted production hook's
notification order/extent or a live model writer hit. Scalar/inline writes and
other providers remain outside coverage, so per-draw comparisons remain enabled.

## Backward-copy adapter completion tests

The fixture now extracts the production A320 hook, substituting only its physical
notification endpoint with a controlled observer. 4,160 backward/self-copy cases
exercise that actual hook and unchanged generated copy body. The observer checks
the original destination/count and all expected output/guard bytes at notification
time, establishing that notification follows the complete write despite consumed
r5. Equal-address copies produce no notification. Three disjoint forward cases
verify delegation to the existing copy endpoint without an extra notification.
The forward endpoint remains controlled in this fixture, and physical alias
resolution/queue integration is covered separately, not exercised by this observer.

Build/all18tests passed12.46s. No game executable changed. Runtime hits on model
storage and completeness of all writer boundaries remain unproven. Further bulk
writer candidate identified for inspection: 821E9BA0 in generated shard16 (called
with destination/fill/count by 821EFB28); no change made to that candidate yet.

## Bulk-fill writer completion

Audited 821E9BA0 (shard16): inline byte alignment prefix, repeated-byte 16-byte
stores, remaining words and byte tails; no call to either tracked copy routine.
It preserves r3 but consumes r5 on alignment. Added a hook preserving original
execution and passing the captured entry destination/count to the existing
completed-physical-write notification after return. Zero extents are discarded
by that existing endpoint, and nonphysical fills do not enter its queue.

The fixture extracts the actual production hook and unchanged generated fill,
substituting only a completion observer. 6,192 cases cover all four alignments,
lengths0..257 and six fill arguments (including high bits and zero). Observer
checks original extent and expected contents/guards at notification time; tests
also check destination return and LR. Physical mapping/queue integration is not
exercised by this observer. All18tests passed12.02s. Candidate index-validation
SHA256: F0B41D925ADF38DBD9C3F99FD7171354290A89F814588DBD8C3E0DEE2E922546.
No runtime of the combined new copy/fill hooks yet. These notifications expand
writer coverage; scalar/inline writes still prevent blanket comparison removal.

## Combined copy/fill and single-index-validation runtime

Candidate F0B41D92...E922546 ran as owned PID53932 from 04:42:32 through
04:46:25. Artifacts: out/native-bridge-run/binding-validation-20260910-044232-7c67d996/.
Used movement/fire inputs, IntegerPostCenters, fresh settings and no automatic
captures/watch audit. intro.png was black at its capture instant; later inspected
mission.png shows street gameplay, firing/casings and105/120 ammunition, while
after-input.png shows a changed position and100/120 after continued firing.
The black still alone is not evidence of a persistent rendering failure or proof
of correct intro imagery.

All retained logs contain zero error lines and zero all=true write batches.
Final indexed sample:12,016,000 draws,765 builds,12,015,235 hits,426 entries,
no source mismatches or evictions. Published index reuse746/rejections0;
retained vertex reuse0/replacements0. Model-owned payload totals:427 vertices /
40,074,192 bytes and3994 indices /9,040,488 bytes (shared CPU/GPU payload, not
incremental residency). Page fallback notifications affected bounded owner sets
(observed2/16/2), not every buffer. No rebuilding explosion observed versus the
previous765-build movement/fire run, but workloads are not frame-identical.

Exact-path-validated owned process was stopped and waited for. This verifies
combined runtime compatibility, not individual new-writer hits on model storage:
notifications currently lack producer attribution. Hidden presentation provides
no visible FPS claim, and zero mismatches does not establish writer completeness.

## Explicit update invalidates overlapping physical owners

VB/IB unlock completion previously invalidated only the supplied handle. Added
NativeModelBuffers::NotifyUpdateAliases: for a published physical extent, reuse
the exact interval lookup to invalidate every overlapping owner once, including
the source owner. Nonphysical/empty owners retain their own explicit notification;
unknown owners retain the bridge's mesh-handle invalidation fallback. Whole-buffer
extent is conservative because nested unlocks are not transaction certificates.
This closes alias propagation for known explicit updates, not all scalar writers.

Tests cover partial overlap across VB/IB kinds and A/C/E virtual addresses,
adjacent nonoverlap, unknown physical mappings, empty extents, missing owners,
notification counts and retirement/republishing at a different physical address.
All18tests passed12.14s. Candidate index-validation SHA256:
563543CB9BF2B64EDEE60144169EE4C72C38083CC572335FF9ABBB2F7E6BD78E.
No runtime validation of this follow-up yet. Remaining byte comparisons remain
enabled; shared page watches still do not provide a complete concurrent/native
provider ownership guarantee.

## Alias-update candidate: retry-menu runtime

Candidate563543CB...7E6BD78E ran as owned PID40280 04:49:53..04:55:12.
Artifacts: out/native-bridge-run/binding-validation-20260910-044953-536ef792/.
The original retry script's145/150/153s inputs did not reach a prompt: inspected
retry-prompt.png instead showed ordinary gameplay. Updated the script to
210/215/218s and observed its live reload at196530ms. Inspected confirmed-prompt.png:
"Restarting mission. Continue?", No selected. Then the scheduled260s Up/263s A
inputs ran; after-retry.png shows resumed street/player/HUD rendering.

All retained logs contain zero errors. Final indexed counters:16,738,000 draws,
720 builds,16,737,280 hits,380 entries,zero source mismatches/evictions. Published
index reuse700/rejections0; retained vertex reuse1/replacements0. Model payload:
380 vertices/37,630,248 bytes;3995 indices/9,125,976 bytes (shared payload).
Stopped only exact-path-validated owned process and waited for exit.

This supplies compatibility coverage through the retry prompt/return to gameplay,
not proof of a full asset teardown/republication: no later sampled publication
milestone was observed, and the before/after captures alone cannot establish every
gameplay reset. In particular, no direct runtime evidence yet identifies multiple
physical owners affected by an explicit unlock. Hidden run is not an FPS test.

## Whole-recomp store-boundary inventory

Inspected generated/default/edf2017_pch.h rather than assuming SDK store functions:
scalar accesses are locally generated macros; overriding SDK functions would not
intercept them. Added read-only tools/audit-guest-store-boundaries.ps1, with Details
output identifying each enclosing function, opcode, emitted route and source
character offset. Counts independently agree with rg over instruction comments:
150,071 store/zero instructions total;145,902 scalar-macro,4,031 raw-vector,
75 raw-atomic,50 inline bulk-zero and13 MMIO-or-scalar paths. No unclassified
instruction blocks in this source snapshot.

This changes the next implementation requirement: scalar-macro instrumentation
alone cannot establish complete writer coverage. Vector destinations bypass the
macros; conditional atomic writes must notify only on success; dcbz/dcbzl inline
zeroing bypasses the newly hooked memset routine; MMIO stores also have ordinary
memory branches. CPU register-only SIMD operations must not be mistaken for guest
writes. Any generated-store adapter must cover these routes and preserve byte
order, alias offsets, atomic semantics and completion ordering. SDK/native-provider
writes remain a separate boundary even if generated stores are all intercepted.

Counts describe all recompiled stores, including stack/CPU metadata, not150,071
model writers or observed runtime events. The inventory is not a formal decoder,
writer synchronization proof or authorization to remove byte comparisons. No game
binary changed and no runtime launched during this audit.

## Optional generated scalar-store observer

Added guest_scalar_store_observer.h and EDF2027_OBSERVE_SCALAR_STORES (OFF by
default). When enabled, CMake appends the adapter after the generated recomp PCH;
generated source files remain untouched. The four scalar macros preserve volatile
encoded stores and the generated host-alias offset, then report physical-address
stores to the existing completed-write endpoint. Address arguments are evaluated
once. Ordinary low virtual stores do not call the observer. This is an opt-in
diagnostic path: notification/queue overhead is not yet measured.

Tests compile the actual adapter and check8/16/32/64-bit byte order, unaligned
stores, original address/width, single address evaluation, and completed contents
at callback time on the C alias. A low virtual store verifies observer exclusion.
All18tests passed12.10s with the game option OFF. The ON game's full recomp/PCH
integration and live coverage are not yet verified; the unit translation uses the
header directly. E-alias mapping remains delegated to the generated macro and
was not exercised by these new cases.

Vector/raw atomic/inline-zero/MMIO paths and SDK providers remain separate gaps.
The observer is not a concurrency guarantee or a content-ownership certificate;
all per-draw comparisons remain enabled. Next steps are verifying the opt-in
build, expanding emitted-store coverage, and measuring actual writer destinations.

## Scalar observer integration verification in progress

Configured an independent Release build at `out/build/win-native-scalar-audit`
with `EDF2027_OBSERVE_SCALAR_STORES=ON`. The normal `win-native-clean` cache
remains OFF and retains the canvas-metadata candidate. The independent generated
CMake PCH includes the original generated header followed by the observer header.
All 81 recompilation shards compiled; dumpbin symbol inspection of shards 0 and
80 confirms external references to `edf_native_observe_guest_scalar_store`.
This establishes actual compiled instrumentation, not merely header presence.
Full link completed and all 18 tests passed (13.40s), including the expanded
alias tests below. Diagnostic executable SHA256:
`4FE5072229950933DA1DA0FCAE5841B179B2FFD7CC089890FAA1A3250BE1CA05`.

Extended the production-adapter unit test to cover both C and E aliases. E-alias
cases verify encoded bytes at independently calculated address+0x1000 and guard
the unadjusted 4 KiB page, in addition to callback ordering, extent and single
address evaluation. This checks Windows host addressing, not actual shared
physical-page alias coherence. Runtime writer attribution and cost remain
unmeasured; vector/atomic/inline-zero/MMIO/native-provider gaps remain. No
geometry comparison is disabled and the observer is not enabled in normal builds.

After the non-owned game exited, started owned diagnostic PID49720 at 07:36:57,
run `out/native-bridge-run/binding-validation-20260910-073657-fa964526`, using
the scalar-audit executable above, movement/fire input, original internal size,
game-default MSAA and fresh isolated user data. Runtime is still in progress;
revalidate this exact process before stopping it or starting another diagnostic.
Do not treat the instrumented executable as a recommended performance build.

## Scalar observer runtime compatibility

Owned PID49720 completed the scripted movement/fire sequence and was stopped
after exact executable-path validation at approximately 07:40:52. Captures in
the run directory: `loading-check.png` is mission/difficulty selection (not a
loading-screen verification), `mission-check.png` shows street/player/HUD at
120/120, `fire-check.png` shows a changed viewpoint, firing and 115/120, and
`after-input.png` shows another position, ants/effects and 102/120 ammunition.

All rotated logs inspected together: zero error/critical lines and zero all=true
write batches. Timestamp-sorted final indexed sample: 17,459,000 submissions,
766 builds, 17,458,234 hits, 427 entries, zero vertex/index mismatches or evictions.
Published index reuse744/rejections0; retained vertex reuse1/replacements0.
Model storage:427 vertices /40,021,176 bytes;3995 indices /9,114,360 bytes
(shared CPU/GPU payload, not incremental residency). Logged page-overflow batches
affected1/2/16/2 owners rather than every model. No rebuilding explosion is evident
relative to prior movement/fire runs, but these are not frame-identical workloads.

This establishes compatibility of the fully compiled scalar adapter through this
bounded gameplay sequence. It does NOT attribute a particular model write to the
scalar callback: existing batch logs combine bulk, scalar and other notifications.
Next useful diagnostic is producer attribution and actual model-overlap accounting,
alongside closing raw vector/atomic/zero and native-provider gaps. Occluded DXGI
presentation prevents a visible FPS conclusion. Per-draw byte comparisons remain
enabled; zero mismatches is not proof of complete writer ownership. No game source
or normal executable changed during this runtime check.

## MMIO-helper ordinary memory writes

Extended the opt-in store adapter to wrap all four generated `REX_MM_STORE`
helpers. Template bodies expand the original macros before redefining call sites,
retaining their MMIO dispatch and encoded volatile memory stores. Only completed
physical-alias writes notify the existing scalar endpoint. The byte/halfword MMIO
branches retain the full uint32 value passed to CheckStore; the 64-bit branch
retains its two ordered high/low CheckStore calls. No guest/generated file changed.

Focused production-adapter tests passed (3.35s): four widths, aligned/unaligned
addresses, C/E aliases, post-write callback contents, original extent, address
evaluation once, E-alias offset guards, and low virtual memory exclusion. These
initially exercised ordinary-memory branches. Added an isolated real SDK MMIO
handler test with a registered recording callback: byte/halfword values are not
truncated, word value is preserved, doubleword dispatch is high then low at +4,
and no physical observer notification occurs. The expanded focused test passed
in3.70s. The diagnostic full
rebuild completed, including all81 generated shards; all18 diagnostic tests passed
in12.95s. The final real-handler test addition was separately compiled in the
normal test tree, where all18 tests passed in13.02s. Diagnostic game SHA256:
`26EFA1887CE979D0B091ADB85D4ADE98DF1E163E940358A4FC5BEFBCE501AE46`.
No game was launched this turn; previous scalar-audit runtime evidence predates this
extension. Normal build instrumentation remains OFF. Raw vector/atomic/inline-zero
and native-provider gaps still prevent removing per-draw comparisons.

## Conditional atomic-store adapter

Added an opt-in wrapper for the emitted `__sync_bool_compare_and_swap` calls
(inventory:71 stwcx and4 stdcx). The wrapper executes the same compiler atomic
builtin, preserves its boolean success result for the generated CR0 update, and
notifies only after successful exchange. Expected/desired encoded bits are passed
unchanged to the builtin. Raw host destinations are converted back to guest
physical-alias addresses using integer range checks and the inverse Windows
E-alias +0x1000 adjustment, without pointer truncation. Host pointers, low guest
virtual addresses and the E-alias host gap are excluded. Physical heap validation
remains in the existing completion endpoint.

Focused tests passed3.24s for success/failure and post-write encoded contents,
32/64-bit widths and C/E aliases. Additional host-pointer/gap exclusion cases are
included in the completed diagnostic rebuild. No generated files changed; normal
build instrumentation remains OFF. Full diagnostic link and all18 tests passed
(12.16s). Diagnostic SHA256:
`2FD9D4F1FC8C6B5616BB4C96535A9F60A64D9CA710A5E8D0EF2BD67D46A3569C`.
Runtime integration of this extension is unverified. The callback
is after the atomic operation, not part of its atomic transaction; this does not
establish an exclusive producer/consumer ownership protocol.

Rechecked remaining emitted forms by destination:4031 guest vector stores use
`simde_mm_store_si128` to REX_RAW_ADDR; register-only SIMD stores must be excluded.
48 dcbzl forms zero128 bytes, and2 dcbz forms zero32 bytes. Their alignment is
already computed in the generated ea expression. These forms remain unobserved,
as do uncovered native providers. No geometry-source comparison was removed.

## Vector and inline-zero adapters

Added `guest_raw_store_observer.h` as the final opt-in generated PCH include.
It wraps `simde_mm_store_si128` and unqualified `memset` calls after retaining
their original implementations inside inline adapters. Vector byte shuffling
and guest effective-address alignment stay in the generated expressions.
Notifications follow the complete 16-byte vector store or fill. Raw-pointer
range filtering excludes register/host destinations and low virtual memory;
zero-byte fills emit no notification. The existing option name is retained for
compatibility, but its description now refers to generated-store diagnostics.

Tests call the actual replacement macros through small adapters before undefining
them for the remainder of the test translation unit. C/E-alias tests check encoded
vector contents,32/128-byte zeroing,callback-after-write,exact extents and guard
bytes; host vector/fill stores are excluded and memset's returned pointer is
preserved. Focused tests passed3.27s before adding the zero-length case. The full
diagnostic rebuild (including that case) completed; all18 tests passed12.10s.
Diagnostic SHA256:
`CCC88D99DDC87A240C54D938F5B7BCD7EF0A90CFF24E16A9232199EAFEC22966`.
Normal instrumentation
remains OFF and no geometry comparisons were removed.

Together these adapters target every emitted route in the current store inventory,
not every possible writer in the port. Native SDK/provider writes and concurrent
producer/consumer synchronization remain separate requirements. Runtime combined
coverage, attribution and overhead are unverified; this is not a performance build.

Started owned combined-observer PID29708 at07:54:25, exact scalar-audit executable
above, run `out/native-bridge-run/binding-validation-20260910-075425-cbe83439`.
Movement/fire script, original size/game-default MSAA, fresh isolated user data.
Runtime is in progress: revalidate this process before stopping or launching
another diagnostic. The prior scalar-only run cannot validate these newer adapters.

## Native provider follow-up

Inspected the imported RtlFillMemoryUlong provider in the current SDK: it writes
`length >> 2` big-endian uint32 words directly through mapped memory. It bypasses
generated-store adapters. The sole direct generated call is shard10 at return
address82150E80, with destination stack+192, length800, pattern0x80000000. The
import is also present in generated registration/function tables, so indirect
uses are not excluded. This direct stack call is not evidence of a model writer;
no provider hook was added on that basis. A complete provider destination audit
remains necessary; NtReadFile's existing adapter alone is not complete coverage.

## Combined generated-store runtime

Owned PID29708 ran07:54:25..07:58:25 and was stopped after exact-path validation.
Candidate CCC88D99...FEC22966 includes scalar, MMIO-helper, atomic, vector and
inline-fill adapters. In the run directory above, `intro-check.png` shows the
mothership but is heavily overexposed: brightness remains an open fidelity issue,
not a passing visual check. `gameplay-check.png` shows forward movement/firing
with008/120 ammunition; `after-input.png` shows a changed viewpoint, ants, street,
player and HUD at100/120. This verifies bounded gameplay compatibility, not a
complete mission or console-equivalent visuals.

Timestamp-sorted rotated logs:17,925,000 indexed submissions,767 mesh builds,
17,924,233 hits,428 entries,zero vertex/index mismatches or evictions. Zero
error/critical lines and zero all=true write batches. The sampled native index
reuse and retained vertex storage remain active. No rebuilding explosion is
apparent versus prior movement/fire runs, but workloads are not frame-identical.
DXGI occlusion prevents a visible FPS conclusion, and callback cost has not been
isolated. Combined batch logs do not attribute individual writes to each adapter.

Next ownership requirement remains producer attribution/model-overlap evidence
and native-provider/concurrent-access coverage. Passing this run does not justify
disabling comparisons. No normal executable changed during this runtime check;
all owned diagnostic processes from this run have been closed.

## Provenance runtime result

Owned PID22948, candidate2EDCA6D3...25FB929, ran08:02:22..08:06:13 and was
stopped after exact-path validation. Artifacts are in
`binding-validation-20260910-080222-cb766d74`. `gameplay-check.png` shows the
initial street/player/HUD at120/120. `fire-check.png` was captured after the input
sequence: changed position, ants/effects and100/120. Logs verify analog movement
and RT255 at160005/180006/195011ms and release at210006ms; the capture filename
alone is not evidence of active firing at that exact instant.

No generated-write overlap records occurred. The audit queries every retained
sample, not merely the power-of-two logged batches, but retains only the first32
generated extents per drain. Large startup/loading batches omit writes, so this
negative result cannot certify immutable geometry or complete writer coverage.
The last logged audit batch524288 had6 generated calls,0 provider calls,6 samples,
0 omitted and0 overlaps. This is a single batch, not a cumulative write count.
It demonstrates many small generated-write batches unrelated to model ranges.

Final indexed sample17,019,000 submissions,766 builds,17,018,234 hits,427 entries,
zero source mismatches/evictions. Published index reuse744/rejections0; retained
vertex reuse1/replacements0. Model payload427 vertices/40,074,192 bytes and3995
indices/9,114,360 bytes. Zero error/critical lines and zero all=true batches across
rotated logs. DXGI occlusion means no visible FPS claim. Normal build unchanged.

Next candidate is explicit model-range subscriptions to distinguish writes to
live model storage from unrelated physical memory without scanning every draw.
Registration/publication, aliases, retirement and concurrent writer ordering must
be defined and tested before using such a filter to discard notifications. Native
provider coverage remains separate. No comparisons are disabled on this evidence.

## Model page subscriptions (classification only)

NativeModelBuffers can now attach to NativeBufferWrites. Successful publication
updates an owner-keyed physical subscription; replacement removes old page refs,
nonphysical/empty publication removes the subscription, and retirement/destruction
unsubscribe. The bridge attaches its registry to the existing queue. The queue
must outlive an attached registry; copying registries is disabled. Shared pages
use reference counts, so retiring one alias preserves the others. Storage is a
lazy512KiB page-reference table plus the owner map, not guest page protection.

Subscription mutation and writer classification share the queue mutex, with
renderer-registry -> queue ordering. Writers never acquire renderer locks.
Generated writes touching subscribed pages are counted and sampled separately
(first32 matching extents per batch), allowing unrelated writes to stop crowding
out relevant samples. Exact owner-overlap queries at drain now use these matching
samples. Page matches are conservative: same-page nonoverlap is possible, and
owner identity at drain may differ from identity at write completion. Allocation
failure sets sticky unknown classification; fault injection is not yet tested.

No writes are discarded: coverage/invalidation remains unchanged. This is not yet
a production filtering or immutable-version contract, nor synchronization around
the underlying CPU store. Tests cover cross-page extents, shared-page aliases,
replacement, nonphysical publication, destruction, physical-end bounds, unknown
extents, repeated retirement and matching-sample limits. Full diagnostic build
and all18 tests passed12.36s. SHA256:
`84201E327B7525E21B8410A947F90CBEC2F0D4A27751DD019673D596B09B7E1F`.
Runtime subscription verification remains pending; no normal game was rebuilt.

Started owned PID54960 at08:10:31, exact scalar-audit executable above, run
`out/native-bridge-run/binding-validation-20260910-081031-5c634f16`.
Movement/fire input, original scene size and fresh isolated user data. Runtime
is in progress; revalidate this exact process before stopping or launching another.

Added a concurrent queue test: a writer records1000 generated extents while a
second thread repeatedly replaces a subscription and adds/removes a shared alias.
It checks all1000 notifications survive, exact union coverage remains unchanged,
match counts cannot exceed writes, and final unsubscription leaves no page refs.
The native guest-memory suite passed three consecutive runs (20.37s total).
This tests subscription bookkeeping under contention, not ordering of the actual
game CPU store against publication or draw. No game executable changed for this test.

## Subscription runtime result

Owned PID54960 completed movement/fire and was stopped after exact-path validation
at08:14:19. In `binding-validation-20260910-081031-5c634f16`, `fire-check.png`
shows firing/casings at015/120 and `after-input.png` shows changed position,
ants/player/HUD at100/120. No logged errors/critical lines or all=true batches.
Final indexed sample16,166,000 submissions,766 builds,16,165,234 hits,427 entries,
zero source mismatches or evictions. Published index reuse744/rejections0;
retained vertex reuse1/replacements0. Model payload427 vertices/40,074,192 bytes,
3995 indices/9,114,360 bytes. These shared payload counts are not GPU residency.

Subscriptions did identify generated page hits during startup/loading (e.g.
batch2:310 hits,32 retained,278 omitted; batch32768:114 hits,32 retained,82 omitted).
No exact overlap records occurred among retained matching samples. The later
gameplay power-of-two audit batches65536..524288 each report0 page hits and
unknown=false. Those logged per-batch counts do not prove all intervening batches
were empty; large matching batches still omit samples. This supports compatibility
and exposes page false positives without proving complete absence of model writes.

No filtering or comparison removal is justified solely by this result. Next
implementation should establish a publication/write ordering contract and exact
model-range notification versions, while retaining fallback checks for uncovered
providers. Callback overhead and visible FPS remain unmeasured (occluded run).
Normal executable unchanged; this owned runtime is now closed.

## Exact notified-write versions

Subscriptions now own an ObservedVersion(lifetime,revision). Every successful
subscription/republication gets a fresh lifetime, including identical owner/range
reuse. Record advances revisions of every exact overlapping owner, for generated
and bulk/provider notifications alike, before union/page coalescing. Page false
positives do not increment exact versions. Unknown physical extents conservatively
advance all subscribed owners. Drain does not reset versions; retirement removes
the token. Allocation uncertainty or uint64 exhaustion makes Version unavailable.

Queue locking orders notification/version mutation against subscription changes,
not the underlying CPU store. Versions represent only writes routed through
Record; explicit invalidations and uncovered providers are NOT certified by this
token. Publication currently subscribes after native index construction, so a
stable snapshot/publication handshake is still required before cache validation
could rely on versions. No draw uses this token to bypass source comparisons.

Unsampled exact owner-hit totals distinguish generated and other endpoints;
bounded logs include cumulative counts. Provider-only hit batches are included.
Initial implementation scans owner ranges after a page hit; loading overhead is
unmeasured and may require an interval index before production use. Tests cover
same-page misses, aliases, provenance, drain persistence, page-overflow batches,
unknown extents, lifetime reuse, retirement and400 concurrent version increments.
Full diagnostic build/all18 tests passed12.11s. SHA256:
`0CE1BB179D53FD854BBB444ADA57ACC724F658CCE4A2C2118A93F02B6DB091B3`.

Started owned PID16480 at08:18:26, exact scalar-audit executable above, run
`out/native-bridge-run/binding-validation-20260910-081826-0bc0c7e9`.
Movement/fire input, original size/MSAA and fresh user data; LoadTimings enabled
to expose loading overhead (not directly comparable to runs without profiling).
Runtime remains in progress; revalidate this exact process before stopping it
or launching another diagnostic. Normal executable was not rebuilt.

## Exact-version runtime and owned snapshot publication

Owned PID16480 was stopped after exact-path validation at08:22:18. Run
`binding-validation-20260910-081826-0bc0c7e9` used the pre-snapshot candidate
0CE1BB17...6DB091B3. `gameplay-check.png` shows firing/casings at030/120 amid
street/NPC/player/HUD rendering. Final indexed sample8,717,000 submissions,
761 builds,8,716,239 hits,422 entries,zero source mismatches/evictions and no
error/critical lines. Last cumulative exact-write report at batch262144 had
generated0/provider0 owner hits. These unsampled counters cover notified exact
overlaps against subscriptions, not unnotified writers or pre-subscription writes.

Correction on subsequent inspection: the prior search used the wrong log label.
LoadTimings alone DOES enable `Native script timing`; LoadingTrace is unrelated
to that timer. This run recorded nested map.820B4D58 durations32782.4385ms and
32804.0934ms. These are inclusive nested load calls, not a separately timed outer
ScriptLoadMap function. Hidden/occluded presentation, profiling and concurrent
compilation prevent an FPS comparison. The diagnostic path needs optimization.

Found and fixed double-reading of live input during native buffer construction:
index conversion now consumes its owned source_ copy; vertex conversion likewise
consumes source_. Dynamic vertex update first owns one snapshot, converts/uploads
that snapshot, then moves it into retained CPU state after successful Map/Unmap.
Thus GPU conversion and validation bytes come from the same host snapshot. The
initial copy itself still needs a writer/snapshot synchronization contract.

Index publication now registers model metadata/subscription before native snapshot
construction, captures the observed version, then commits retained storage under
the queue lock only if that version/lifetime still matches. A rejected or unknown
physical snapshot stays unretained and the existing validated first-use path is
available. Nonphysical owners retain the prior byte-validated path. No GPU work
runs inside the queue-locked commit. This orders attachment against completed
notifications; it does not serialize unnotified raw stores or certify all providers.

Tests cover same-page unrelated writes, exact-write rejection, old owner lifetime
rejection, retirement and retained-storage state using real native index resources.
Full diagnostic build/all18 tests passed13.29s. New candidate:
`out/build/win-native-scalar-audit/edf2027-native-owned-snapshot.exe`, SHA256
`5578A9D73104545EEE379AE140AAB87E3EDD8864E7FC4B59C2777F19BDC1DE84`.
Runtime validation of this snapshot candidate is pending. Normal executable not
rebuilt; all source comparisons remain enabled. No diagnostic is left running.

## Indexed exact-version lookup

Replaced the per-page-hit scan of every subscribed owner with a sorted interval
index and prefix-maximum end bounds. The index rebuilds lazily after subscription
changes and retains pointers into stable map nodes; retirement/replacement marks
it dirty under the same queue lock before a subsequent query. Allocation failure
keeps it dirty and falls back to the prior exact scan, never stale pointers or
missed notifications. Invalid extents still advance all owners conservatively.

All18 tests passed12.58s. Added a600-step deterministic overlap-oracle test with
publication/replacement/retirement and exact revision/hit-count checks; the updated
guest-memory suite passed6.52s. The candidate includes owned-snapshot conversion
and the observed-index publication handshake from the preceding section.
`edf2027-native-owned-snapshot-indexed.exe` SHA256:
`6A6ADD8CDF857C9CB61128122D6EC80CB2AFAF916E8D641DF053D43A8AC4A18F`.

Started owned PID57368 at08:25:48, exact executable in `out/build/win-native-scalar-audit`,
run `out/native-bridge-run/binding-validation-20260910-082548-f3e5d424`, movement/fire
script, fresh user data and LoadTimings enabled. Runtime and load-cost comparison
remain pending. Revalidate this exact process before stopping or launching another.
Normal executable unchanged; all comparisons still enabled.

### Indexed lookup runtime result

Completed the owned PID57368 run above and stopped only that process after
revalidating its exact executable path. The two nested `map.820B4D58` inclusive
load bodies completed in 5039.5325ms (08:26:57.729) and 4953.2053ms
(08:27:47.176), compared with 32782.4385/32804.0934ms in the preceding
full-owner-scan diagnostic. This is an instrumented loading comparison, not a
normal-build or visible gameplay FPS measurement; the candidate also includes
the owned-snapshot changes.

At 08:31:54.438 the indexed-upload log reports 35,639,000 submitted draws,
776 mesh builds, 437 entries, zero indexed errors, zero source mismatches and
zero cache evictions. Published-index consumption reports 754 reused builds
and zero rejected generations. The last sampled cumulative exact-version log
(08:29:28.904, batch524288) reports zero generated/provider owner hits. That
negative result covers notified writes against live subscriptions only: it
does not establish completeness of native provider coverage or exclude writes
before subscription. No source-comparison bypass is justified by these counts.

Inspected `gameplay-check.png` from the run: the soldier, city, mothership,
radar and HUD are visible, with 120/120 ammunition. This capture does not prove
the scripted firing sequence executed, nor mission completion or lighting
fidelity. The process was allowed to run beyond the input script's end before
being stopped; no second capture of firing was obtained.

Next ownership gap confirmed in source: first-draw retention in the indexed
bridge still uses lifetime-only RetainVertexStorage/RetainIndexStorage, while
the load-time index publication uses CommitObservedIndex. Mesh-cache hits
still compare live vertex/index bytes. Extending the publication handshake
must avoid introducing queue locks on every cache hit and must not treat the
diagnostic store observer as complete native-provider coverage.

Re-ran the current diagnostic CTest suite after collecting this result: all
18 tests passed in 12.08s, including the updated interval-overlap oracle.
This turn changed documentation only; no new gameplay executable was built.

### First-draw observed retention

Subsequent implementation extends the observed-version handshake to vertex
retention and to first-draw indexed mesh construction. NativeMeshCache accepts
a synchronous non-owning two-pointer snapshot observer, called immediately
before construction/source revalidation or dynamic update, never on an unchanged
cache hit. The bridge uses it to capture physical-owner write versions only on
those paths. No std::function allocation or new queue lock is added to ordinary
cache hits. Indexed model meshes use the immutable cache; immediate dynamic
meshes do not install this ownership observer.

Newly returned physical vertex/index storage is attached to its model owner only
through CommitObservedVertex/CommitObservedIndex. Nonphysical storage retains
the existing source-validated lifetime path. Missing/unknown versions reject
physical retention. After a rejected attachment, an unchanged cache hit does not
attempt to retroactively certify that snapshot; reuse remains mesh-local until
a later reconstruction. Queue ordering covers completed notifications, not
concurrent unnotified raw stores. Existing per-draw source comparisons remain.

Built `out/build/win-native-scalar-audit/edf2027-native-first-draw-ownership.exe`
with generated-store observation still ON. All18 tests passed in12.49s. Added
vertex stable/exact-write/republication/retirement rejection checks and observer
cold-build/rebuild/cache-hit counts. An additional integrated test captures a
version, mutates the source and records its completed write inside the observer,
then verifies construction sees the new source but owner attachment is rejected;
the updated quad suite passed0.20s. This is a deterministic interleaving test,
not proof of arbitrary concurrent-store safety. Runtime validation is pending;
normal gameplay executable unchanged and no diagnostic process started.

### First-draw ownership runtime and bulk-word provider follow-up

Validated `edf2027-native-first-draw-ownership.exe` (SHA256
`542B5D6899AE6C35C305318BB6E4477099ABBBBBB721CF02C07ABA05CB16F522`)
in owned PID16164, started08:39:12, isolated run
`out/native-bridge-run/binding-validation-20260910-083912-c019656b`.
Nested map.820B4D58 loads took5021.543/4956.1938ms. The first-draw handshake
therefore did not reproduce the earlier full-owner-scan loading regression in
this diagnostic. This is not a visible FPS benchmark or a normal-build timing.

`firing-check.png` shows firing, shell casings and003/120 ammunition;
`after-input.png` shows the subsequent city/enemy scene with100/120 ammunition.
The indexed log at08:42:52.641 reports14,761,000 submissions,767 builds,
428 entries, zero errors/source mismatches/cache evictions. By08:43:11.646,
published-index reuse is748 with zero rejected generations. The last exact-version
sample (batch524288) has zero cumulative generated/provider owner hits, subject
to the notification-coverage limitations already documented. Stopped only this
owned process after path verification and after the movement/fire script ended.
No complete-mission or brightness-fidelity claim follows from these captures.

Additional provider work: private generated-import redirection now routes
RtlFillMemoryUlong through a native completion adapter, including registered
import references. The SDK original remains responsible for actual writes and
register behavior; the adapter captures r3 destination and `(r4 & ~3u)` extent
before calling it and notifies after return. This matches the inspected SDK's
floor(length/4) word loop and leaves trailing bytes unnotified. Zero-length
notifications remain no-ops in the existing endpoint. No generated sources or
SDK files were edited. The known direct call is still stack-only; this covers
possible physical destinations without claiming an observed model writer.

The extracted actual adapter passed fixture tests for0/1/3/4/5/7/8/31/32-byte
requests, completed-write ordering, volatile argument clobbering, big-endian
pattern bytes, untouched trailing bytes and queued extents (immediate-tail
suite3.61s). The fixture substitutes a controlled provider, not the SDK itself.
Full candidate build `edf2027-native-provider-word-fill.exe` is in progress;
the completed PID16164 run predates and does not validate this provider adapter.

Full provider build subsequently completed, all81 recompiled shards, all18 tests
passed11.86s. SHA256 for `edf2027-native-provider-word-fill.exe`:
`50770900665D1BB697821FA8F5ABBDA7FC7CEDA7D78AC2838A963D0C16FC84CF`.
dumpbin verifies shard10 and init/register object references use
`edf_native_RtlFillMemoryUlong`. Read-only packaged PE audit passes: only the
existing rexruntime.dll and amd_fidelityfx_dx12.dll non-system dependencies,
no staged/imported Xenos DLL. This import audit does not prove absence of dynamic
loading or completion of renderer migration. Runtime validation of the provider
candidate remains pending; no game is left running. Normal executable unchanged.

### Provider runtime / non-observer build

Ran provider-word-fill SHA50770900...16FC84CF as owned PID41584,
08:45:57..08:49:56, isolated artifacts in
`out/native-bridge-run/binding-validation-20260910-084557-80f84f6b`.
Map.820B4D58 inclusive loads4946.6001/5498.9497ms; the second overlapped a
normal-build compilation, so it is not a clean comparative performance sample.
Inspected firing-check.png (008/120 ammunition, firing/casings) and
after-input.png (100/120, city/enemies/effects). Last indexed sample16,783,000
submissions,767 builds,428 entries, zero errors/source mismatches/evictions;
published indices745 reuses, zero rejected generations. Process module inspection
found System32 d3d11.dll and the packaged rexruntime/FidelityFX DLLs, no Xenos-named
module. Stopped only the exact-path-validated owned process after the script.
This validates the candidate workload, not observed physical word-fill calls,
complete provider coverage, all missions or visible FPS.

Built all latest changes with EDF2027_OBSERVE_SCALAR_STORES=OFF as
`out/build/win-native-clean/edf2027-native-resource-ownership.exe`, SHA256
`12377DEF8D7F73522C4D4AEF8E76EAB3979C8942C97341E284AB3B6BA6AB4D80`.
All18 tests passed13.48s; packaged PE audit passed with the same two non-system
DLLs and no staged/imported Xenos DLL. This replaces neither the user's executable
nor config. It includes AA/canvas changes and the newer snapshot/retention/provider
work, without generated-store instrumentation. Byte comparisons remain enabled.
Runtime validation of this non-observer candidate remains pending. No game or
build is left running.

### Non-observer resource-ownership runtime

Validated SHA12377DEF...A6AB4D80 as owned PID50140, started08:51:35,
isolated run `out/native-bridge-run/binding-validation-20260910-085135-17284884`.
Map.820B4D58 inclusive loads3760.6593/3726.27ms. Both completed before compiling
the subsequent worker-initialization change. No visible FPS claim: this was the
hidden diagnostic launcher with load/host timing logs, but without generated
store instrumentation. Inspected firing-check.png (035/120, firing/casings) and
after-input.png (100/120, city/enemy effects). Last indexed sample17,311,000
submissions,767 builds,428 entries, zero errors/source mismatches/evictions;
published index reuse747/rejections0. Stopped only this owned process after
path validation and after the movement/fire script. This validates the
non-observer candidate workload, not a whole mission or pixel-perfect lighting.

## Generated-write provenance sampling

NativeBufferWrites now counts generated-adapter versus other completion-endpoint
calls under its existing queue lock and retains the first32 original generated
extents per batch, before exact-union/page coalescing. `provider_calls` includes
hooked bulk guest helpers as well as native providers; it is not an SDK-only count.
Invalid mapped extents retain the existing all-invalidating sentinel. Drain resets
counts/samples together with coverage, without changing invalidation semantics.

At model publication and indexed consumption, a read-only interval-index query
reports sampled overlaps against currently published owners. Logs include owner,
lifetime generation and kind (first16 overlaps), and batch counts/samples/omissions
(first8 batches then powers of two). This identifies generated writes separately
from bulk/provider notifications, not individual writer PCs or opcodes. Samples
can omit relevant writes, and an owner at drain need not be the owner at write time.
It is diagnostic evidence, not a transaction certificate or completeness proof.

Tests cover original extents versus merged union, bounded samples, independent
counts, drain reset, invalid writes, nested aliases, adjacency, invalid query
ranges, retirement/republication and query non-invalidation. Diagnostic build and
all18 tests passed12.03s. Runtime sampling verification is next; byte comparisons
remain enabled and the normal executable was not rebuilt this turn.

Started owned PID22948 at08:02:22, candidate scalar-audit SHA256
`2EDCA6D34C6067072604A5C1DCAA3CD6F5C8AF9184C736E85E681F10B25FB929`,
run `out/native-bridge-run/binding-validation-20260910-080222-cb766d74`.
Movement/fire script and fresh isolated user data; runtime remains in progress.
Revalidate this exact process before stopping it or launching another diagnostic.
Early provenance logs at08:02:24 confirm actual generated notifications: first
batch7,732,271 generated calls and122,303 other-provider calls,32 retained samples,
7,732,239 omitted; no sampled owner overlaps at that early drain. Later startup
batches also contain generated calls. This proves callback execution/queue routing,
not absence of model writes: heavy sample omission makes a negative result weak.
