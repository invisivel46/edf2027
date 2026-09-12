# Native resource fence wait controller

September 10, 2026. The native-host 8213C928 hook now calls
`WaitNativeResourceFence` instead of the original recompiled wait body. The
bridge-off path still calls the original. This follows the Mission 1 retirement
trace in native-retirement-queue.md, which exercised fence stamping rather than
descriptor-owned deferred records.

The controller preserves target-zero early return, unsigned 32-bit fence distance
comparisons, the distinct recording early return, optional submission when the
target equals the issued fence, rechecking after submission, wait-record setup,
polling until completion or a false service response, and final accounting.
It reloads the completion pointer and issued value for each check; these are not
captured once for the lifetime of the wait. Recording is not completion.

The wrapper uses a copy of the caller context and the original 144-byte frame
depth. Its retained 24-byte accounting record starts at frame+80. Service call
LRs are 8213C97C (submit), 8213C9A8 (begin), 8213C9B4 (poll), 8213C9E8 (end).
The resulting r3 is returned; caller stack and nonvolatile registers are not
replaced by scratch helper state. Generated guest code was not edited.

Tests compare the native controller's service sequence with the extracted retail
body for 686 issued/target/completed/recording combinations, including zero and
wrap boundaries. Those paired tests use controlled submission and stop-on-first-
poll services. Five additional native-controller scenarios test completion during
submission, completion during begin, three-poll completion, false poll response,
and replacement of the completion pointer. They are not paired full-helper or
PPC wrapper integration tests. Full native build and 21/21 CTest passed (12.55s).

## Remaining ownership boundaries

- Submission still goes through 8213CF60 and its retained CPU bookkeeping.
- 821394D8 initializes a guest wait record with device, kind, completion value,
  thread timing fields and the low timebase word.
- 82139688 still owns progress/error policy. Its native hook polls native worker
  signals and publishes native D3D completion first, then calls the original
  service and sleeps 1ms if it requests another iteration. The original tracks
  completion changes, refreshes its progress timestamp under a thread/flag
  condition and calls 82145620 after a 5000-unit threshold. Units must be proved;
  this is not yet permission to substitute a 5000ms host timeout.
- 82139508 adds elapsed low-timebase ticks to device+20032 for kind 3, otherwise
  device+20024, and optionally invokes device+13068's profiling callback.
- 82145620's error path can invoke diagnostics, set device error flags, write
  issued-2 to guest completion, reset a device field and call 82138010; its other
  route traps. Native error handling must not treat this as successful D3D
  completion or silently fake completion to release resources.

These remaining services are the next migration boundary, not proof that the
entire wait subsystem is native. Resource+8 fence fields, guest storage and
per-draw geometry comparisons remain. No performance improvement is claimed.

## Live regression and helper exports

Headless Ghidra completed with new seeds 821394D8, 82139508, 82139688 and
82145620. Their exports agree with the generated instruction observations above;
unrelated previously documented analysis warnings remain.

Run `out/native-bridge-run/binding-validation-20260910-205601-2e96f7eb/` used
SHA256 `54167450F92C2A1E6A277F3B03A148E480E6A3994C31915F29A0AD9A6565124F`,
a fresh profile and RetirementAudit. Inspected `gameplay.png` shows the player,
NPCs and HUD in Mission 1, AF14 120/120. Cleanup reached its 4096 checkpoint at
20:57:52.710. Both setter fence branches reached 1,048,576 at 20:57:59.227.
At 20:58:26.927 indexed telemetry reports 5,756,000 submissions, 717 builds,
378 resident entries and zero upload errors or VB/IB mismatches. No error,
critical or audit-read-failure rows were found. This is bounded regression
coverage, not proof every wait branch ran, full-mission fidelity or performance.

Owned PID 3128, start 20:56:01, was stopped after exact path/start verification;
WaitForExit returned true and process absence was checked. A subsequent bridge
source edit only corrected stale comments; no behavior changed after this build.

## Native progress-poll follow-up

Native-host 82139688 now calls `PollNativeFenceProgress` instead of its original
body. It still progresses worker signals and publishes native completion before
evaluating the record, and keeps the existing 1ms sleep on a continue result.
The four-iteration retail delay loop and 821F9FC8 thread-ID helper are gone on
this path. The latter is replaced by its exact TLS+256, thread+332 reads.
Completion changes preserve the original second completion sample, then refresh
record+12 and record+8 in their original order. Thread exemption and unsigned
5000-unit threshold remain unchanged. Error reporting still calls 82145620 with
the original 128-byte frame depth and LR 82139750, outside the state mutex.

The production-template fixture exercises 80 combinations of device-stop flag,
completion progress, matching thread, exemption flag and elapsed values
0/4999/5000/5001/UINT32_MAX. It verifies continue/stop, error target/count and
record updates, including subtraction wraparound. These are expected-result
unit tests, not a differential execution of the original poll or asynchronous
completion sampling tests. Build and all 21 tests passed in 12.51 seconds.

SDK source inspection found an additional timing limitation: xboxkrnl_threading.cpp
describes thread+0x58 as kernel time, but `include/rex/system/xthread.h` models it
as `uint8_t unk_58[4]`. A named-field search in xthread.cpp did not establish a
writer. This does not prove the clock never advances (raw/bulk writes are still
possible), but it is not evidence of a reliable host deadline. Future migration
must establish the actual clock behavior and choose native error semantics;
passing normal gameplay does not validate a stuck-GPU timeout.

### Live native-poll regression

Run `out/native-bridge-run/binding-validation-20260910-210107-0ae7ac3d/`, SHA256
`097A0B25D6D959C9710E6FFBB84749A1D7C09C4DAF7E3DA5FC8D5D079C1E8788`, used a
fresh profile and RetirementAudit. Inspected `gameplay.png` shows Mission 1's
player/NPCs/HUD under the mothership, AF14 120/120. At 21:03:27.083 telemetry
reports 4,939,000 indexed submissions, 718 builds, 379 entries, zero upload
errors and zero VB/IB mismatches. No error, critical or audit-read-failure rows
were found. This proves startup/intro/gameplay-scene regression coverage only,
not invocation of every poll branch, real timeout behavior, combat or performance.
Owned PID 26248, start 21:01:07, was stopped after exact path/start verification;
WaitForExit returned true and process absence was checked.

## Native wait-record setup and accounting

Native-host 821394D8 and 82139508 now use `BeginNativeFenceRecord` and
`EndNativeFenceRecord`. Their original recompiled bodies only remain on the
non-native fallback. Setup preserves the six-word record and store order, using
the runtime's QueryGuestTickCount (the same source as REX_QUERY_TIMEBASE).
Accounting preserves kind-zero early return, low-32-bit timebase subtraction,
64-bit total wrapping and kind-3 versus other-kind counter selection.

The optional profile callback still executes after the total update. Its native
adapter preserves r3=0, r4=kind, r5 from the caller, the full unsigned 64-bit
thread-clock subtraction in r6, LR 821395B8 and the 96-byte frame depth. It
disables flush mode as the original does and computes two separately rounded
single-precision products for f1. Callback dispatch uses the runtime resolver;
the callback's r3 is returned. This is callback compatibility, not proof that
the callback itself has been migrated.

Forty production-template cases cover five kinds, callback present/absent and
four tick intervals (including wrap), record extent, timebase call order,
counter selection/wrap and callback arguments/order. They do not exercise the
actual indirect callback adapter or prove floating-point edge-case equivalence.
Full build and 21/21 CTest passed (12.41s); a subsequent adapter flush-mode
correction was rebuilt. This candidate has not been live-tested; the earlier
screenshots and logs predate these setup/accounting changes.

The normal native wait controller, setup, polling and accounting bodies are now
native C++, but their compatibility record, guest thread clock and cumulative
counter storage remain. Submission and device-error reporting still have
retained CPU paths. Full native timing/resource lifetime ownership and per-draw
comparison removal remain unfinished. Next validation should cover real
setup/end invocations and callback configuration before replacing record storage.

## Record-use coverage audit

The opt-in RetirementAudit now also samples record setup (phase 0), polling
(phase 1, before native progress publication) and completed accounting (phase 2).
Each phase logs its first eight calls and power-of-two checkpoints, with record,
device, kind, start/current thread-clock fields, profile callback and caller LR.
Disabled mode does not read these diagnostic fields or increment counters.
The candidate rebuilt and all 21 tests passed (12.26s).

A full generated direct-call search found four record owners: 8213C928,
8213BC48, 8213BCE0 and 8214E5B8. Added the latter three to Ghidra and completed
headless analysis; checked their exports against generated instructions:

- 8213BC48 uses kind 2 and waits on writeback+4's packed pointer/generation,
  comparing the low two generation bits and the aligned pointer.
- 8213BCE0 uses kind 1 and waits on writeback+60 versus the masked circular
  allocation range; it returns the masked range end.
- 8214E5B8 visits six 56-byte worker records starting at device+11208, uses
  kind 0 for active slots, and waits for a pointed-to signal to equal a packed
  address/generation target.
- 8213C928 is the already-native issued/completed resource-fence controller.

Each owner passes a stack-local record at frame+80 to begin/poll/end; the three
newly examined owner bodies do not directly read its six fields. This is direct
caller evidence, not a whole-program indirect/alias proof. These non-resource-
fence wait conditions must be preserved in a host-record migration; they cannot
all be replaced by waiting for the same scalar GPU fence.

### Live setup/accounting coverage

Run `out/native-bridge-run/binding-validation-20260910-210958-bebf894e/`, SHA256
`03F36B5747CE84253AE9AD67D31D7E24C183D7A989F4E2141A73F2D5B7EF0DA2`, used
a fresh profile and RetirementAudit. Actual setup and accounting each reached
checkpoint 32, polling reached 64. At 21:11:55.197-.199 the sampled kind-13
wait on thread 40648 completed through callers 8213C9A8/8213C9B4/8213C9E8;
earlier samples include kind 14 on thread 21116. Sampled kernel start/current
fields were zero and callback pointers null. This proves real setup/poll/end
invocation, but not the profile callback adapter, timeout advancement or all
record users. Sampling by phase can omit different kinds/callers between
checkpoints; absence of their sampled rows is not evidence they never ran.

Inspected `gameplay.png` shows Mission 1 player/NPCs/HUD, AF14 120/120. At
21:12:10.623 indexed telemetry reports 3,796,000 submissions, 717 builds,
378 entries, zero upload errors and zero VB/IB mismatches. No error, critical
or audit-failure rows were found. This is bounded startup/intro/gameplay-scene
coverage, not combat, whole-mission fidelity or an FPS benchmark.
Owned PID 31032, start 21:09:58, was stopped after exact path/start verification;
WaitForExit returned true and process absence was checked.

Next storage migration must retain separate caller wait predicates, account for
record reuse/nesting/thread ownership, and validate configured profile callbacks.
The observed zero kernel-clock fields reinforce the need for explicit host
progress timing rather than assuming this compatibility clock is a deadline.

## Native-owned record storage

Setup/poll/accounting now use a thread-local `NativeFenceRecords` registry keyed
by guest TLS and the old record address. That address identifies a record; the
six fields are held in a native `std::array`, with no guest-memory mirror.
`NativeFenceRecordAccess` routes only its six aligned words to native storage;
device, timing and counter accesses still go to the checked guest reader.
Unsupported byte/doubleword accesses overlapping the token's extent are rejected.
The existing template control flow and the four distinct caller predicates remain.

Setup builds a local record and publishes it after success. Duplicate active
keys are rejected instead of silently overwriting another wait. Polling requires
an active record in the current host-thread registry and guest TLS. Accounting
takes a detached copy and removes the key before its work/callback, permitting
reentrant address reuse without dangling references or an active completed record
if accounting throws. Guest-thread migration between host threads during a wait
is not supported by this ownership model; the audited callers use local stacks.
Unwinding out of a poll before its owner calls end can still leave a record until
thread teardown; native error/unwind ownership remains follow-up work.

Tests cover nesting, identical tokens under different TLS values, separate
registries, duplicate begin/missing poll/double end rejection, map rehash
reference stability and detach/reentrant reuse. Forty setup/accounting and eighty
poll-policy cases now run through native record access. Accounting tests verify
all 24 old guest-record bytes remain untouched. These are fixture checks, not
cross-thread runtime migration or whole-program alias coverage. Full native
build and 21/21 CTest passed (12.28s).

### Native-storage live regression

Run `out/native-bridge-run/binding-validation-20260910-211552-13dba29e/`, SHA256
`DAC25B955B4CDAE63AA240BC9D6F23B071E3D25C6BBB4223DB59ADD53D49719B`, used
a fresh profile and RetirementAudit. Trace confirms native record begin/poll/end
on threads 30060 and 54916, including kinds 14 and 13; setup/end reached their
16 checkpoint and poll reached 32. Sampled kernel clocks remained zero and
profile callbacks null. Inspected `gameplay.png` shows Mission 1's player/HUD,
AF14 120/120. At 21:18:24.641 telemetry reports 6,496,000 indexed submissions,
717 builds, 378 entries, zero upload errors and zero VB/IB mismatches. No error,
critical or audit-failure rows were found. No configured callback, exception
unwind, all-owner, combat or full-mission coverage is claimed.

Owned PID 35616, start 21:15:52, was stopped after exact path/start verification;
WaitForExit returned true and process absence was checked. Only a stale source
comment was corrected after this executable was built. Record fields are now
native-owned; guest-clock/accounting-counter dependencies and retained submission
and error services still prevent complete native lifetime ownership. Per-draw
geometry comparisons remain enabled; this run is not a performance benchmark.

## Host-clock progress and fail-closed waits

Each native record now also owns a steady-clock progress timestamp. Production
polling uses `PollHostFenceProgress`: native completion changes refresh that
timestamp, as does the retained matching-thread/exemption flag condition. It no
longer reads thread+88 to detect stalls. `edf_native_wait_stall_ms` defaults to
5000 with accepted range 1..600000 milliseconds. This is an explicit native
no-progress policy, not a conversion of the original 5000 unknown clock units.
It is not a total-wait deadline: observed progress/exemption can extend it.
Caller-specific fence/ring/worker completion predicates are unchanged.

A device-failure flag or expired host deadline now logs and throws, rather than
calling 82145620 (whose retail error behavior can write issued-2 into completion).
The native poll wrapper discards the active record on any escaping exception,
including native progress-publication failures, before propagating it. No error
path here fabricates guest completion or returns success so the caller can reuse
unfinished resources. This is failure propagation, not graceful device recovery.
Other callers of the original device-error handler remain outside this boundary.

Forty synthetic-time cases cover device failure, completion progress, thread
exemption and 0/4999/5000/5001/60000ms intervals. A reader that throws on thread+88
proves these tests do not consult the guest kernel clock; tests also verify no
completion mutation or guest-record writes. Registry tests cover idempotent
exception discard and subsequent address reuse. Full build and 21/21 CTest
passed (12.27s). Tests use an injected error callback, not an actual GPU hang.

Setup/end still retain guest-clock fields for profiling callback compatibility,
and cumulative tick counters remain in guest device storage. Thus only progress
timing has become host-owned in this step. Suspend/resume and debugger pauses
need explicit testing/handling before treating this policy as handheld-ready;
host elapsed time can include a long scheduling pause. Configured callback and
device recovery behavior also remain unverified.

### Host-progress live regression

Run `out/native-bridge-run/binding-validation-20260910-212158-3b521c78/`, SHA256
`ABD250AD384B77014CC26EE531F1B647105DBA556F76D8C2BCCD13ABE2178E2F`, used
a fresh profile and RetirementAudit with the default 5000ms native stall limit.
Trace shows setup/end checkpoint 16 and poll checkpoint 32 through the native
resource-wait controller. No error, critical, stall or audit-failure rows were
found. Inspected `gameplay.png` shows Mission 1 player/HUD, AF14 120/120. At
21:24:34.300 indexed telemetry reports 7,311,000 submissions, 717 builds,
378 entries and zero upload errors or VB/IB mismatches. This confirms bounded
normal-run regression, not real timeout, suspend/resume or recovery coverage.
Owned PID 58124, start 21:21:58, was stopped after exact path/start verification;
WaitForExit returned true and process absence was checked. No FPS improvement
or per-draw geometry comparison removal is claimed.

## Native allocator wait controllers

Native-host 8213BC48 and 8213BCE0 now call native C++ controllers instead of their
original bodies. Generation waiting preserves `(generation-completed)&3` and
the aligned-pointer condition; ring-range waiting preserves masked end addition,
wrapped/nonwrapped intervals and inclusive end comparison. Both preserve initial
predicate testing, begin, poll-before-recheck, and final accounting. Every
iteration reloads the writeback pointer. BCE0 returns the masked range end.

The bridge adapter retains the 144-byte caller frame and opaque frame+80 token,
kind 2 for generation and kind 1 for range waits, and original service call LRs.
Begin/poll/end resolve to the native record/progress/accounting implementation.
The non-native fallback still executes original bodies. Existing extracted
native ring-submission tails that omit BCE0 capacity waits remain unchanged;
this replacement does not reintroduce those omitted calls.

Added hash-guarded original bodies to the presentation test fixture. 343
generation cases and 1029 range/mask cases compare service sequences against
the original (poll is controlled to stop), and range cases compare returned
positions. Values include zero, boundaries, wraparound, and masks 0/31/all-ones.
The fixture also checks original stack/nonvolatile preservation. Two additional
native-controller scenarios complete after three polls while switching the
writeback pointer. These are not full PPC adapter or real allocation-pressure
tests. Full native build and 21/21 tests passed; this candidate has not been
live-tested. The preceding host-progress smoke run predates these controllers.

Remaining in this immediate family: the 8214E5B8 worker wait controller, guest
profiling clock/counter consumers, submission bookkeeping, host suspend/resume
policy and broader error recovery. Native resource allocation and geometry
writer/comparison removal are still unfinished.

## Native worker-slot wait controller

Native-host 8214E5B8 now calls `WaitNativeWorkerSlots`; the non-native fallback
retains its original body. It visits the six 56-byte entries at device+11208,
skips inactive slots without reading their completion pointers, captures each
packed address/generation target before begin, polls before checking completion,
reloads the signal pointer after each successful poll, and ends each active
slot before visiting the next. A false poll ends only that slot's wait; thrown
native failures still unwind. The adapter preserves global device lookup,
144-byte frame depth, opaque frame+80 token, kind 0 and original service LRs.

512 fixture scenarios cover all active masks, all low-two-bit generation tags
and stop versus three-poll completion. They check active-slot traversal order,
inactive invalid-pointer avoidance, captured-target stability when source fields
change, changed completion pointers, and poll/end counts. These are native
expected-result tests, not a paired original-worker execution or real scheduling
proof. Full native build and 21/21 CTest passed (12.20s).

All four directly identified wait-record owner controllers now have native-host
implementations. This does not complete submission ownership: 8213CF60 and
8213CDC0 still run original CPU bodies, and the native C410/BD90 extracted tails
still preserve CPU cursor/callback behavior. Their already-removed GPU ring
writes and BCE0 capacity waits were not reintroduced by these new hooks.

### Combined allocator/worker-controller regression

Run `out/native-bridge-run/binding-validation-20260910-213040-09e113d2/`, SHA256
`028411AA2B9B0F533BDDF6FE4E0F48BB8B921F1ECEA154354C82AE002E045573`, used
a fresh profile and RetirementAudit. Inspected `gameplay.png` shows Mission 1
player/HUD, AF14 120/120. At 21:33:12.140 indexed telemetry reports 6,588,000
submissions, 718 builds, 379 entries, zero upload errors and zero VB/IB mismatches.
No error, critical or audit-failure rows were found. Sampled record calls include
the resource-fence controller, but do not prove the new allocator/worker hooks
were exercised. This validates bounded startup/intro/gameplay-scene regression
for the combined candidate, not all-owner runtime coverage or performance.
Owned PID 55612, start 21:30:40, was stopped after exact path/start verification;
WaitForExit returned true and process absence was checked. Geometry comparisons
remain enabled and the overall Xenos replacement remains incomplete.

## Native submission-flush controller

Native-host 8213CF60 now calls `FlushNativeSubmission` rather than its original
body. It keeps recording/special-mode cache-collection gates, always submits
through CDC0, then re-reads special/global/error gates for the optional wait.
The wait target is issued-2 read after submission. Its post-wait flag update
re-reads the flag byte, and the final return reads the current device cursor.

Native C5F0's CF60 path already returns zero packet words while retaining its
atomic dirty-range CPU effect. The new wrapper checks that zero-word contract
and has no cache-packet allocation/C868 enqueue branch. Unexpected nonzero
output throws rather than silently dropping work or invoking a GPU fallback.
The retained C5F0 CPU service still uses caller stack output words; those are
not the native-owned wait-record fields. The wrapper preserves 112-byte frame
depth and LRs CF9C/CFE0/D020 for collection/submission/wait, respectively.

32 paired cases execute the extracted original CF60 with the existing native
cache service and controlled CDC0/C928 services, then execute the native
controller with the same dependencies. They compare collection/exchange counts,
no packet allocations, submission/wait counts, target/order, flags and cursor.
These cases span recording, special mode, optional-wait enable, preexisting wait
flag and dirty range. They are not full-retail GPU equivalence or real completion
tests. Full native build and 21/21 CTest passed (12.30s).

CDC0 remains a retained CPU dependency, as do cache-range CPU extraction and
shared allocator/descriptor bookkeeping. No native completion is published early
by this change. Native draw resources and guest geometry writer coverage still
need work before per-draw comparisons can be removed.

### Native flush live regression

Run `out/native-bridge-run/binding-validation-20260910-213636-2b56a9ae/`, SHA256
`82F07407C83F18C05B21317E78E1936A80E8ECD0691CED71EB109883DC380469`, used
a fresh profile and RetirementAudit. Inspected `gameplay.png` shows Mission 1
player/HUD, AF14 120/120. At 21:39:16.766 indexed telemetry reports 7,891,000
submissions, 717 builds, 378 entries, zero upload errors and zero VB/IB mismatches.
No error, critical or audit-failure rows were found. This is bounded regression
coverage, not all flush branches, full-mission gameplay or an FPS comparison.
Owned PID 57464, start 21:36:36, was stopped after exact path/start verification;
WaitForExit returned true and process absence was checked. CDC0 descriptor
submission remains the next retained boundary; overall replacement is incomplete.

## Native descriptor-submission controller

Native-host 8213CDC0 now dispatches through `SubmitNativeDescriptor` instead of
the original CPU body. The implementation was checked against the Ghidra
assembly export `out/ghidra/geometry-writers-inline/8213cdc0.txt`, including
service return addresses, signed word counts, both recorded-list formats,
allocation growth conditions and the final cursor/page-alignment operations.
Direct submission still captures through C788 before calling native C868; this
change does not publish completion early. The native adapter preserves a
128-byte service frame. The non-native route retains its original fallback.

288 controlled-service scenarios cover six dispatch modes, empty/nonempty
ranges, growth/no growth, refill/no refill, three backward cursors and the
page-alignment flag. They check callback order, captured submission extents,
list words/cursors, inactive-list preservation and cursor state before refill.
Two fixed address-conversion checks include an upper-address boundary. These
are expected-result template tests, not differential execution of the original
CDC0, full PPC adapter coverage or actual allocation-pressure tests.

Full native target build and 21/21 CTest passed (12.86s). No live game run has
been performed with this descriptor-controller change; the preceding Mission 1
smoke result predates it. No performance improvement is claimed.

Remaining dependencies include descriptor-list growth 82141500, worker-list
growth 8213CB30, refill 8213CC20 and their guest allocation/cursor ownership.
Replacing this controller does not make those lists host-owned or remove the
unresolved deferred resource-retirement consumer. Geometry source comparisons
remain enabled, and complete Xenos replacement remains unfinished.

The subsequent descriptor-controller Mission 1 regression passed bounded
startup/intro/gameplay-scene coverage. Run/hash/limitations are recorded in
[native-submission-storage-ownership.md](native-submission-storage-ownership.md).
It observed direct C868 submission but did not exercise recorded-list growth.
