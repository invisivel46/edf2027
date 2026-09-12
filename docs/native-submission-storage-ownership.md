# Submission storage ownership audit

## Finding

The next ownership boundary is a mixed CPU worker command stream, not simply
a vector of GPU submission ranges. Replacing CB30 with a host allocator alone
would leave raw guest-pointer producers and the E640 consumer behind. No
runtime storage replacement is implemented by this audit.

Ghidra was rerun successfully with C868, CB30 and CF60 added to the maintained
seed list. Exports are in `out/ghidra/geometry-writers-inline/`. Assembly and
generated bodies were checked together; the Ghidra XREF list is not complete.

## Distinct storage families

| Storage | Owner/producer | Contract still retained |
| --- | --- | --- |
| Descriptor command ranges | device+13156/+13160/+13164, 82141500 | 72-byte blocks, 8-byte header, descriptor+116 root; converted address/word-count entries |
| Worker command lists | 20-byte list structure, 8213CB30 | root at +0, block at +4, cursor +8, end +12, device +16; 4224-byte blocks with links |
| Deferred resource retirement | device+13144/+13148/+13152, 82141440 | Separate descriptor+112 root and resource tags; not the command-range list |
| Command reservation/refill | device+40/+44/+48, 8213CC20 | Ring, backward-descriptor or callback allocation; pending fence at +13520 |

CB30 seals the preceding block using its current cursor before allocation.
On allocation failure it clears list+0 and uses device+15152 fallback storage.
On success it links both the new block and preceding cursor, then publishes
block+4 as the next cursor and block+4220 as the end. Returning a native pointer
from this interface is not valid while these guest stores remain.

## Direct growth frontier

Scanning every generated default translation unit for `bl 0x8213cb30`, tracking
the enclosing `DEFINE_REX_FUNC`, finds eleven sites in nine enclosing bodies:

- 8213CD78: initializes the 20-byte list and publishes its root after growth.
- 8213CDC0: appends the 0x82 submission-range command in recording mode.
- 8214EC00: initializes three lists and appends a 252-byte 0x80 state record.
- 82140E98: three growth sites; payload/lifetime classification still required.
- 821409A0: payload/lifetime classification still required.
- 821FD428: payload/lifetime classification still required.
- 821FD958: payload/lifetime classification still required.
- 821FDF50: payload/lifetime classification still required.
- 821FE358: payload/lifetime classification still required.

These are enclosing generated bodies, not necessarily every retail entry-point
name within their ranges. This inventory excludes indirect calls, tail calls,
and writers that have enough capacity and do not themselves call growth. It is
not proof of complete writer coverage.

## Busy enqueue and actual submission

C868 reads device+10868. When nonzero, it takes the guest spinlock at +10872
and checks busy again. If still busy, C018 inserts a 16-byte linked record into
the supplied reservation and returns reservation+16. C868 increments busy only
after insertion, releases the lock, and returns without calling C410.
If busy clears under the lock, it releases the lock and follows direct submit.
Direct submit increments busy before C410 and passes a stack descriptor pair.
The native submission scope currently prevents completion dispatch from racing
the enclosing busy increment. That protection must survive storage migration.

C018 is a separate producer even though it does not call CB30. It writes
reservation+4 = words|0x82000000, +8 = command address, +12 = 0xc0000000,
and +0 = converted reservation+16. It updates list block/cursor, flushes the
new record, then publishes the old cursor link. Ignoring this path would lose
busy-worker submissions.

E640 consumes the 0x82 record at 8214E720, masks words to 24 bits, snapshots
address/words onto its stack, and calls C410 at 8214E73C (return 8214E740).
It then advances by eight bytes. Other branches consume state, continuation
and callback commands. Enqueue at C868 is therefore not a GPU completion point.

## Implementation direction

Migrate the shared worker stream as typed host-owned records, with native
producer entry points and a consumer that preserves CPU callbacks, state-copy
commands, continuations and busy accounting. Keep actual D3D completion arming
at submission, not at enqueue. Finish classifying the six unreviewed producer
bodies and non-growth writers before replacing CB30 storage. Test enqueue
while busy, busy clearing under the lock, chained continuation, callback
reentrancy, reset/drain and storage reuse. The descriptor+116 command list and
descriptor+112 retirement list require their own lifecycle migrations.

This audit changes the next implementation target from isolated allocator
replacement to joint producer/consumer ownership. Geometry comparisons remain
enabled; no new executable or performance result is produced by this audit.

## Producer classification and native reachability correction

The six previously unclassified enclosing bodies have now been reviewed at
their growth sites, together with E640/E2C8/E1C8. A follow-up Ghidra run exported
those functions and saved successfully. Ghidra cannot decompile part of 409A0
(constructor error at 82140D18); its generated PPC body and bridge hook provide
the evidence for that path. This is not full function-equivalence verification.

| Producer | List/payload at its growth sites | Current native boundary |
| --- | --- | --- |
| 821409A0 | device+13000, 4-byte 0x87 loop marker | Native tiling-begin hook bypasses original body, checks caller |
| 82140E98 | device+13000: 0x8c mask (8 bytes), 0x8b two secondary-list roots (12 bytes), 0x88 loop-end (4 bytes) | Native tiling-end hook bypasses original body, checks callers |
| 821FD428 | device+12960, 16-byte tile patch record starting with a guest packet pointer | Native-owned immediate allocation returns zero at the specific audited call, taking the earlier no-buffer exit; other scopes remain unproven |
| 821FD958 | device+12960, 16-byte tile patch record | No equivalent ownership bypass established in this audit |
| 821FDF50 | device+12960, 16-byte tile patch record | No equivalent ownership bypass established in this audit |
| 821FE358 | device+12960, 16-byte tile patch record | Successful native indexed draw uses extracted CPU prefix, excluding this producer; original fallback remains for other conditions |

The four draw-family records are **not E640 opcodes**. E1C8 walks the
secondary list in 16-byte records, takes the first word as a destination pointer,
tests packed rectangle bounds against tile rectangles, and patches destination+8
with a two-bit-per-tile mask OR 0x80000000. E2C8 first processes its primary
argument list, then lists referenced by the other argument. E640's 0x8b branch
calls E2C8 only when worker+364 has its high bit set. This is retained Xbox tile
packet patching, not native geometry ownership. Do not recreate that packet
patching in a host-owned queue merely to preserve an unused retail path.

Consequently the earlier nine-body inventory is a **retail frontier**, not nine
proven active native producers. Begin/end tiling and successful native draw
paths already remove several entries. Remaining indirect reachability and
unsupported draw routes still require investigation rather than global deletion.

## E640 consumer record map

| High byte / word | Observed action |
| --- | --- |
| bit31 clear; 0x81; exact 0xc0000000 | Return current cursor to outer worker; does not advance |
| 0x80 | Copy 248 bytes into worker+116, reset index/mask, advance 252; service E488 twice |
| 0x82 | Submit address/24-bit word count through C410; advance 8 |
| 0x83 | Service E488 and re-read same cursor; not an advancing no-op |
| 0x84 / 0x85 / 0x86 | Save or revisit worker+368/+372 and manipulate loop flags |
| 0x87 / 0x88 | Save loop start at +376 or increment index at +44 and revisit start |
| 0x89 / 0x8a | E170 service or E5B8 wait plus KeUnlockL2; advance 4 |
| 0x8b | Conditionally call E2C8 with secondary roots; advance 12 |
| 0x8c | Mask test; save next cursor at worker+80 and divert to worker+96 when set |
| Other high-bit-set word | Treat word as link and continue at word+4 |

After nonterminal commands E488 runs before the next word is read. A host-owned
consumer must retain this servicing order and explicit continuation identities;
snapshotting the whole stream once would miss links published by concurrent
producers and the 0x83 re-read behavior.

Diagnostic command coverage now counts actual visits in the existing
`edf_native_hook_timings` audit. Kinds 1..13 correspond to opcodes 0x80..0x8c;
kind 0 is a low-bit31 word, 14 is exact terminal 0xc0000000, and 15 is another
link. Counts log the first eight and powers of two, across threads. Repeated
visits are not distinct commands or frames. Disabled diagnostics add no reads
or counters. A bounded mission observation cannot prove absence elsewhere.

## Bounded native regression and coverage

Full build and 21/21 tests passed (12.50s). Run
`out/native-bridge-run/binding-validation-20260910-215210-a31f04e1/` used the new
descriptor controller plus command diagnostics, HookTimings and RetirementAudit,
with a fresh user profile. Executable SHA256:
`D61B4F24CECC1E5B8F1BE1D5F410598B7A6F4407C54CBB77BC2E6E81E4307530`.
Inspected `gameplay.png` shows Mission 1 player/HUD and AF14 120/120. At
21:54:40.900 indexed telemetry had 6,279,000 submissions, 717 builds, 378 entries,
zero upload errors and zero VB/IB mismatches. No error/critical/audit-failure
rows were found. These are submissions, not rendered frames or FPS.

No `Native worker command coverage` rows appeared. Sampled C868 calls through
8192 visits came from CDC0 with busy=0, increment=0 and recording=0. This supports
prioritizing the direct submission path for this scene; it does not establish
that worker/tiling paths are unreachable in other missions or under pressure.
No deferred-list, full-mission, combat, fidelity or performance claim follows.
Owned PID 47032, start 21:52:10, was stopped after exact path/start verification;
WaitForExit returned true and process absence was checked.

## Native submission observer controller

C410 now snapshots each guest descriptor once into host-owned
`NativeSubmissionDescriptor` values. `SubmitNativeObservers` consumes that
snapshot directly instead of invoking the extracted PPC ring-submit tail.
Signal/completion arming uses the same snapshot after observer processing, under
the existing submission ordering/scope locks. The original producer C868 still
creates its temporary descriptor on the guest stack; that input boundary remains.

The native controller preserves normal and flag-bit2 observer paths, per-range
virtual method24 and external observers, method28 batch notifications, and
cursor10820 updates with three individual mask applications per range. Observer
configuration is re-read at each original callback boundary. Virtual method28
does not overwrite volatile argument registers beyond its object argument.
Observer address subtraction retains the full 64-bit PPC result, including
negative results and positive results above INT32_MAX. No ring-base13476 read,
GPU capacity wait, ring packet construction, or MMIO doorbell occurs here.

Ownership is deliberately immutable for a submitted batch: observer mutation
of the original guest descriptor array no longer changes later descriptors in
that batch. Previously completion arming already used the pre-callback snapshot
while the extracted tail could re-read mutated descriptors. CPU cursor and
observer registration remain guest ABI state, not fully native device ownership.

96 expected-result cases span both modes, both observer kinds enabled/disabled,
zero/one/three descriptors and four masks including a non-contiguous mask.
They check callback order, zero-word handling, observer extents, cursor wrap and
publication-before-final-notification. Explicit address tests cover full-width
subtraction. These are template tests, not differential PPC adapter/callback
mutation or real observer coverage. Full target build succeeded; this change
postdates the preceding Mission 1 smoke test and needs live regression.

## Direct producer no longer allocates a guest-stack descriptor

Native C868 now calls `DispatchNativeSubmission`. Its direct branch passes one
host-owned descriptor to `SubmitOwnedNativeDescriptors`, the same observer and
completion service used by the C410 compatibility boundary. There is no guest
descriptor write/read round trip in this branch and no extra range vector.
C410 still snapshots arrays supplied by remaining guest callers. Both snapshot
and processing preserve the nested submission scope, blocking completion
dispatch until enclosing busy-counter changes finish.

The busy branch preserves acquire/recheck/C018 insertion/increment/release
ordering, including falling through to direct submission when busy clears
under the lock. It retains the original guest spinlock and linked storage;
these are not advertised as native-owned. A thrown insertion now releases
the lock before propagating failure. Direct submission increments busy before
callbacks, with no counter rollback or artificial completion on failure.
Cursor returns and lock tokens retain all 64 bits. The adapter retains the
176-byte service-frame depth and service return addresses from the Ghidra C868
assembly. Non-native mode still calls the original C868 body.

18 new controlled-service cases cover initially idle, queued and busy-cleared
branches, increments 0/1/UINT32_MAX, and submission/insertion exceptions. They
check host descriptor values, full-width cursor/token retention, busy counter
wrap, callback order and release-before-rethrow. These are not concurrency,
real guest lock or PPC adapter differential tests. The combined observer and
dispatcher changes passed 21/21 tests (14.31s) before the final restoration of
the C410 snapshot scope; the final build/test result is recorded below. Neither
change has had live regression yet. Guest completion cursors, worker lists,
refill/allocation and geometry comparisons remain.

Final snapshot-scope build succeeded and all 21 tests passed again (12.17s).
No live game process was launched for this change.

## Ring-copy cursor arithmetic

BD90's remaining native extracted tail did not read packet source bytes or
write GPU ring words, but still loaded ring base13476 and iterated once per
word to advance cursor10820. The bridge now computes that CPU effect directly
using `AdvanceNativeRingCursor`, without the packet pointer, ring base, stack
frame or per-word loop. It separately returns `(old_cursor+words)&mask`, the
omitted-reservation result previously returned by the extracted native tail.
For non-contiguous masks that return need not equal the repeatedly masked
cursor; zero words likewise retain the old stored cursor.

The arithmetic handles arbitrary masks, not only 2^n-1. After one increment
and mask, bits above the first zero mask bit cannot change under later unit
increments; only the contiguous low one-bits cycle. All-ones masks use normal
uint32 wrap. 801,792 cases compare directly against the per-word loop across
low-byte masks and selected high-bit masks, plus 15 maximum-count composition
checks. The targeted contract test passed. This change was made while the
preceding producer/consumer executable was running, so that run cannot validate
the new BD90 adapter. Full integration results follow separately.

A literal-offset scan also found +10820 accesses in BD90, C410, reset D298,
and enclosing generated bodies 82363E48/82363B60/8236B9C8. The latter offsets
are not established aliases of the graphics device; do not treat matching
numbers as a proven writer list. Native cursor ownership still requires reset
and retained consumer migration, not deletion based on this scan alone.

### Producer/consumer live regression (before BD90 arithmetic)

Run `out/native-bridge-run/binding-validation-20260910-220251-bd1169a9/`,
SHA256 `0686FD41A12FF702D51CDFA481150EB6D84F2CC346EDE4C98925B1F851DEA6CF`,
used a fresh profile, HookTimings and RetirementAudit. Inspected `gameplay.png`
shows Mission 1 player/HUD, AF14 120/120. At 22:05:29.002 telemetry reports
7,060,000 indexed submissions, 718 builds, 379 entries, zero upload errors and
zero VB/IB mismatches. No error/critical/audit-failure rows were found. This
validates bounded startup/intro/gameplay-scene regression for the combined
host descriptor producer and observer consumer, not combat, full mission or FPS.

No worker-command coverage rows appeared; sampled C868 direct calls have
busy=0, increment=0 and recording=0. The busy list branch and actual registered
observer callbacks are not established as live-covered. Owned PID 52452, start
22:02:51, was stopped after exact path/start verification; WaitForExit returned
true and process absence was checked. The subsequently edited BD90 adapter is
not included in this executable hash or live result.

The final build including native BD90 arithmetic succeeded; all 21 tests passed
(12.24s). Live BD90 adapter coverage and any performance gain remain unverified.

## Dedicated GPU-ring storage removed

The hash-guarded native D298 extraction now omits its separate GPU-ring
allocation (default 32768 bytes) and ignores an optional borrowed-ring pointer.
The native ring value is zero, the obsolete nonzero-ring allocation check is
gone, and `MmGetPhysicalAddress` is no longer called for that ring. Successful
initialization publishes zero at device+13476; owned-ring field13416 remains
zero after the existing cleanup. Existing old-owned storage is still released
by the original reset cleanup, after native completion tracking retirement.

The distinct command-storage allocation (default 2 MiB) and 96/32-byte CPU
writeback allocations remain. Ring-size-derived cursor mask13480 remains a CPU
compatibility contract. This change does not remove guest command reservations,
writeback storage, worker queues, geometry comparisons or all reset extraction.

The remaining literal ring-base reader was native worker initialization EE50,
which copied it into worker+4 / device+10816. Native initialization now publishes
zero for that obsolete alias without reading13476. Audited E8E0/E640/E328 worker
control bodies do not read worker+4 as ring storage; C410 and BD90 already have
native consumers that do not read it. Matching numeric offsets in unrelated
structures are not treated as aliases. Unreviewed indirect callback behavior
is not proved safe by this literal scan; broader runtime coverage remains needed.

Reset tests compare the original using a borrowed ring against native ignoring
both absent and provided ring inputs. This deliberately aligns failure ordinals
for the remaining allocations, not for the removed ring allocation. Tests check
zero native owner/base, normalize only the differing borrowed-ring input and
ring publication, and retain original drain/free order, command/writeback
effects, return codes and nonvolatile/stack checks. Worker initializer tests
explicitly verify zero alias and normalize that one removed field before full
memory comparison. Full build and all 21 tests passed (12.60s). This is bounded
fixture evidence, not all device-reset or callback lifecycle coverage.

### Ring-free initialization live regression

Run `out/native-bridge-run/binding-validation-20260910-221017-0486923d/`, SHA256
`EB4719A635F3228F492A413912F9439AF3283D505B5D1FD088FC0799DB054F60`, used a fresh
profile with HookTimings and RetirementAudit. This executable includes native
BD90 arithmetic, producer/consumer ownership and the ring allocation removal.
Inspected `gameplay.png` shows Mission 1 player/HUD and AF14 120/120. At
22:12:34.908 telemetry reports 4,323,000 indexed submissions, 718 builds, 379
entries, zero upload errors and zero VB/IB mismatches. No error/critical or
audit-failure rows were found. Owned PID 33420, start 22:10:17, was stopped after
exact path/start verification; WaitForExit returned true and absence was checked.

This is startup/intro/gameplay-scene regression, not a full mission, all worker
paths, repeated live reset, suspend/resume or an FPS comparison. It supports the
ring-free initialization route in this scene without establishing complete GPU
replacement. Command-storage and CPU writeback allocations remain deliberately.

## Native-authoritative submission cursors

`NativeSubmissionCursors` now owns cursor10820 and its mask per device. A
successful configured D298 reset initializes cursor zero and imports the newly
configured mask once. The existing post-drain/pre-free reset boundary retires
the old entry; teardown or failed initialization does not create a new one.
Submission before initialization fails explicitly instead of silently importing
guest state. Each initialization gets a new generation, preventing a callback
that resets the device from publishing a stale cursor into its replacement.

BD90 reads and advances this native state under submission/state locks. The
submission observer controller obtains its cursor/mask through a snapshot
adapter, without reading the guest cursor or mask on each submission. Before
final observer notifications, it commits the native cursor and writes a guest
compatibility mirror. That mirror is not read back or compared on the native
submission path. Other observer/configuration reads and remaining allocator
waits still use guest ABI state. This is not removal of all guest device fields.

The state mutex is not held through observer callbacks. The existing recursive
submission gate serializes native submissions; within-generation reentrant
publication retains the original controller's outer-cursor overwrite behavior.
Cross-reset publication rejects rather than corrupting the new generation.
Broader reentrant observer/device-reset behavior still needs integration tests.

New tests run the production observer template with a deliberately incorrect
guest cursor/mask and require the native result and compatibility publication.
They also check separate devices, reset generation rejection and retired-device
lookup failure. Full native build succeeded before the framerate discussion;
the resumed validation passed all 21 tests (12.23s). No framerate-clock change,
interpolation or unlock is part of this ownership change.

### Native cursor live regression

Run `out/native-bridge-run/binding-validation-20260910-221638-56d944ea/`, SHA256
`15BF113B009CD51760523A63B630DB7FFB5E47EDA14737EFCDA06FED99953907`, used a fresh
profile, HookTimings and RetirementAudit. Inspected `gameplay.png` shows Mission
1 player/HUD and AF14 120/120. At 22:19:07.105 indexed telemetry reports
5,888,000 submissions, 717 builds, 378 entries, zero upload errors and zero
VB/IB mismatches. No error/critical/audit-failure rows were found. This covers
bounded startup/intro/gameplay-scene regression, not full mission, live repeated
reset, callback reentrancy, suspend/resume or performance improvement.
Owned PID 43804, start 22:16:38, was stopped after exact path/start verification;
WaitForExit returned true and process absence was checked. Geometry comparisons
remain enabled; overall Xenos replacement is incomplete.

### Reject observer submissions that cross a device reset

Inspection found that cursor publication alone did not close the generation
boundary: the final normal observer runs after publication, and special-mode
observers do not publish a cursor. Because the submission gate is recursive,
callbacks can reenter reset on the same thread. Without another check, old
descriptors could reach completion queues belonging to a replacement device.

`NativeSubmissionCursors::Validate` now checks lifetime without changing cursor
state. Production calls it after each guest observer returns, before proceeding
to another observer/ABI read, and again under the state mutex immediately before
signal/completion queue lookup. Existing publication validation remains. No
state mutex is held across guest callbacks. A crossed lifetime throws rather
than arming old ranges; this is rejection, not recovery or rollback of callbacks.

Eight fixture cases cover normal/special mode, retire-only/reinitialize, and
reset/no-reset controls. They reset from slot 28 (after normal cursor publication),
assert no subsequent callback or simulated completion arming after rejection,
and verify the replacement cursor stays untouched. The fixture exercises the
controller/registry protocol, not actual guest reset callbacks or live queues.
Full native executable rebuilt; all 21 tests passed in 12.26s. No live game run
was performed for this change; the preceding live result predates these guards.
This closes a native lifetime validation gap, not geometry writer coverage or
the remaining guest command-list/resource ownership work.
