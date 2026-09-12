# Render queue entry ownership

Scope: queue constructed by `82439AA8` and configured by `82439B48`, reached
through coordinator+0x8c in the inspected small-renderer path. This is not a
whole-program alias or shutdown-order proof.

## Allocation and publication

Queue constructor `82439AA8` installs table `82065600` at `82439AD0`.
Its +0x30 method is `824396C8`, the cleanup used before reconfiguration and
on allocation failure. Configuration `82439B48` obtains two allocations:

- Payload: `8212F420` at `82439BA4`, flags `208C800E`; published at queue+0x2c.
- Entry array: `82432158` at `82439BE8`, flags `208C800D`; count prefix of
  four bytes followed by 60-byte entries, published at queue+0x30. Zero-count
  special case uses `820B24A8(4)` instead.

Both normal nonzero allocations use the previously traced nonphysical heap
route (flag bit 31 clear), not the model physical pool. Count/size overflow,
alignment arguments, later pointer replacement and concurrent access remain
separate concerns.

Each entry is constructed by `824323D8` at `82439C10`; the constructor
installs table **820650F0** at `82432400` and zeroes offsets 0x2c..0x38.
After both allocations succeed, `82439B48` aligns the payload base and
assigns successive slices via `82432180` at `82439CA4`. The latter calls
entry slot +0x30 (`824321C0`), then publishes the slice pointer at entry+0x2c
(`824321B0`) and capacity at +0x30 (`824321B4`). It does not set the owning
flag at +0x38. The queue, not these initialized entries, owns the payload.

This resolves the initial payload source of the 60-byte entry returned by
`824398D8` and retained by the renderer. Entry address and payload address
are different pointers; neither initial allocation is the model VB/IB pool.

## Methods and release

Raw big-endian table words, paired with the constructor's actual vptr store:

| Entry slot | Target | Effect |
| --- | --- | --- |
| +0x08 | 82433978 | Shared release; observed count one invokes slot zero with flags 1, otherwise atomic decrement |
| +0x00 | 82439CD0 | Scalar/array deleting destructor |
| +0x30 | 824321C0 | Frees payload only if +0x38 is nonzero, then clears four entry fields |
| +0x38 | 82432240 | Sets cursor +0x34 to capacity for mode 3, otherwise zero; no payload store |
| +0x48 | 82432398 | Tail-dispatches to entry slot +0x34; target/effects still to inspect |
| +0x50 | 824323B0 | Returns payload + cursor |
| +0x58 | 823F7CC0 | Returns cursor +0x34 |

`82439CD0` calls `82432428` once for scalar flags or backward over the
count-prefixed array when flag bit 1 is set. Flag bit 0 requests deallocation
through `82432168 -> 8212F4B8`, flags `208C800D`. `82432428` restores the
entry vptr, conditionally frees an owned payload, clears entry+0x2c..0x38,
then calls base cleanup `82433940`. Thus release is not unconditionally
metadata-only; its effects depend on ownership and reference lifetime.

Queue cleanup `824396C8` frees queue+0x2c first, then invokes the first
entry's slot zero with flags 3 for a nonempty array; the zero-count case frees
the count prefix via `820B2510`. It finally zeroes queue bookkeeping. This
ordering requires outstanding renderer/worker references to have been drained;
the cleanup body itself does not establish that synchronization.

## Verification and remaining work

Fifteen additional targets exported, 314 total seeds. Generated instructions
were checked for pointer publication and allocator/destructor flags. Ghidra
`82432168` encounters bad instruction data in an inlined free path; rely on
the explicit three-instruction wrapper and generated target code, not its
expanded C, for that boundary. Existing `8243C4E0` incomplete-control-flow
and physical-free import limitations remain. Headless runs exited zero/saved,
which is not equivalent to complete decompilation.

Next: entry slot +0x34 and other payload writers, ownership-flag changes,
worker/coordinator construction linkage, and release/reconfiguration ordering.
No geometry comparisons were removed and no runtime build was produced.

## Data-access method follow-up

Seven further targets were exported (321 seeds). The remaining selected
methods on the installed table `820650F0` are now classified:

| Slot | Target | Inspected effect |
| --- | --- | --- |
| +0x2c | 82432178 | Returns constant 4 |
| +0x34 | 82432220 | Adds low 32 bits of r4 to cursor +0x34 at 82432234; no bounds check or payload store |
| +0x3c | 82432278 | Copies from payload+cursor to entry r4 destination via 821E8320 at 824322E4 |
| +0x40 | 82432308 | Copies entry r4 source into payload+cursor via 821E8320 at 82432374 |
| +0x4c | 82432080 | Returns payload pointer +0x2c |
| +0x54 | 8243DF88 | Returns capacity +0x30 |
| +0x5c | 824323C0 | Returns unsigned capacity <= cursor |

This resolves `82432398`'s +0x48 tail dispatch: for this installed table,
the target is cursor adjustment `82432220`, not a payload-writing method.

The copy methods call +0x5c, select zero bytes if at/past capacity, otherwise
min(requested,capacity-cursor), and store the selected length through entry r6.
After the copy they reload that output length and add it to cursor (stores
`824322F8` / `82432388`). Generated instructions confirm copy direction and
the capacity clamp. Their caller-supplied output/destination pointers are not
automatically non-aliasing: overlapping length output, source, destination or
entry fields and concurrent changes are not certified here. Cursor addition
can wrap; these methods are not a general bounds-safety certificate.

The bridge's existing `sub_821E8320` hook captures destination/length before
the guest routine and calls `NotifyCompletedNativeBufferWrite` afterward.
Thus these two selected payload copies use an already intercepted writer
boundary; no duplicate notification hook was added. The raw-pointer getters
at +0x4c/+0x50 still allow writes outside these methods, and completed-write
notification alone does not prove synchronization with a racing snapshot.

Also fixed the exporter's incomplete-output detection to match `bad instruction`
case-insensitively with `Locale.ROOT`. Previously Ghidra's lowercase warning
in `82432168` was visible in C but missed by the uppercase substring check.
A full rerun now explicitly labels that output INCOMPLETE. Existing incomplete
functions remain incomplete; this change improves reporting, not decompilation.

Remaining ownership-flag changes, raw-pointer consumers, worker/coordinator
linkage and shutdown ordering are unchanged. No runtime comparisons removed.

## Renderer consumption of the queue pointer

The inspected `8243D6A0` worker path calls entry +0x58 (`823F7CC0`) at
`8243D8F0`. Its return is the cursor/count, **not a pointer**. r29 becomes
that count minus eight. The worker resets cursor through entry +0x38,
retains the entry through the renderer setter, and advances cursor by eight
through entry +0x48 at `8243D980`. It calls renderer +0x50 at `8243D9BC`,
passing r29. For table `82064C78`, the callee is `8242EB78`; its inspected
body obtains the actual data pointer independently from its retained entry.

At `8242EC04`, renderer+0x2c's slot +0x50 resolves to `824323B0` for
entry table `820650F0`, returning payload+cursor. The renderer keeps that
pointer in r25 and uses it as the **source** of row copies at `8242EC8C`.
Destination r28 comes from texture lock `8213B5A0` at `8242EC64` (SP+0x54),
advances by destination pitch (SP+0x50), and is unlocked via `82139B90`.
Source advances by renderer plane stride; the outer loop processes three
planes. These copies use the already intercepted `821E8320` boundary.
This closes the previously unknown source getter for this installed-table
path; it does not prove other consumers of returned raw pointers safe.

Configuration `8242E978` stores plane dimensions/strides in renderer+0x14c
through +0x16c, and releases/recreates three groups of three texture handles
through `8213B730`. Its selected body writes renderer fields, not source
queue bytes. It also calls renderer slot +0x64 and `8242D540`; their effects
are separate. Source-size checks against all plane copy extents and failure
paths have not been established.

### Callback distinction

Renderer slot +0x30 is `8242CFF8`. Its mode mapping is:

| Mode | Function field | Context field |
| --- | --- | --- |
| 1 | +0x110 | +0x11c |
| 4 | +0x10c | +0x118 |
| Other (including 3) | +0x108 | +0x114 |

The worker installs `8243B120` with mode 1 at `8243D774`; this is **not**
either callback invoked directly by `8242EB78`. Those calls are renderer
+0x108 at `8242ECF8` and +0x10c at `8242EF58`, populated from worker
+0x34/+0x38 with contexts +0x44/+0x48 at `8243D79C`/`8243D7C4`.
Their providers remain unresolved. `8243B120` itself is a tail-dispatch
through worker slot +0x88; `8243B110` dispatches through +0x84. Neither
stub's body has a direct payload store; their virtual targets remain open.

Four additional targets exported (325 seeds); both runs saved with exit zero,
retaining the documented incomplete-control-flow warnings. No runtime changes.

## Worker callback registration

`8243B1F0` installs worker table `82065678` at `8243B214`, then calls
`82439DE0`. That initializer clears function fields +0x34/+0x38 and contexts
+0x44/+0x48, along with the other callback fields. This proves the initial
state only; derived construction or later registration may replace it.

For this installed table, slot +0x38 is `8243A000`. It calls worker slot
+0x0c before registration and +0x14 afterward, saving incoming r5/r6 across
those calls, and publishes the following pairs:

| Registration mode | Worker function/context | Store sites | Render-worker use |
| --- | --- | --- | --- |
| 1 | +0x3c/+0x4c | 8243A06C/8243A070 | Separate worker callback |
| 2 | +0x40/+0x50 | 8243A078/8243A07C | Separate worker callback |
| 4 | +0x38/+0x48 | 8243A060/8243A064 | Forwarded to renderer mode 4 |
| Default, including 3 | +0x34/+0x44 | 8243A084/8243A088 | Forwarded to renderer mode 3 |

Thus the two callbacks called by `8242EB78` are caller-provided registration
values, not fixed `8243B120`/`8243B110` stubs. The generated direct-call index
contains no direct calls to `8243A000`; this does not exclude virtual calls.
Callers of the registration slot and any derived vptr installation still need
resolution before assigning concrete providers to these fields.

Ghidra initially inferred a body missing the three switch arms even though
its C displayed the cases. Verified the raw four-word table at 8243A050..5F
against generated/default/edf2017_recomp.68.cpp, then repaired the local
function body to A000..A04F plus A060..A0AB, excluding only table data.
The rerun's instruction listing now includes all eight pair-publication stores.

The installed base table also maps +0x84/+0x88 to `8243D018`/`8243B688`,
which are candidate targets for the two fixed forwarding stubs; their effects
and possible derived overrides are not certified by reading the base table.
The additionally exported `8243A168` is a state-transition method with virtual
calls, not a callback-field publisher; its callees remain separate.

Four new targets (329 seeds total) exported and saved successfully. This pass
narrows callback provenance and repairs analysis coverage, not runtime behavior.

## Derived worker and coordinator publication

Derived constructor `82431E68` calls base `8243B1F0` at `82431E7C`, then
installs table `82064F60` at `82431E8C`. Raw guest-image words confirm that
this table preserves callback registration slot +0x38 (`8243A000`) and
forwarding slots +0x84/+0x88 (`8243D018`/`8243B688`). The derived-table
question for these particular slots is resolved; registration callers and
the forwarded targets' effects are still open.

Factory `8242C2E8` puts its coordinator r23 into SP+0xb4 at `8242C5D4`,
then passes SP+0xb0 as the worker setup configuration at `8242C608`.
Derived table +0x24 is `8243BCF8`. On its successful initialization path,
that method passes config+4 to worker slot +0x44 at `8243BFB4`.
The raw derived table resolves +0x44 to `8243A0B0`.

`8243A0B0` retains the incoming non-null coordinator through its slot +4
at `8243A0E4`, releases any previous worker+0x2c through slot +8 at
`8243A100`, clears the old field at `8243A108`, and publishes the incoming
pointer at `8243A110`. This closes the normal factory-to-worker+0x2c link
used by the coordinator getters in `8243D6A0`. It does not establish
shutdown synchronization or exclude later replacement through this setter.

The setup body also initializes worker-local lists and obtains four handles
through `821FC410`, passing entry points `8243B130`, `8243B160`,
`8243B190`, and `8243B1C0`. These are distinct from the user-supplied
callback pairs in worker+0x34..0x50. Their lifecycle and scheduling effects
are not certified by this pointer-publication trace. Setup cleanup slot
+0x34 (`8243BFE8`) and device slot +0x40 (`8243C290`) remain separate.

Three targets added since the 329-seed pass (332 total); headless runs saved
successfully. Cross-checked the new methods against generated PPC C++ and
the exported instruction listings. Existing incomplete-body warnings remain
for other targets. No runtime comparison, executable, or performance change.

## Fixed completion callbacks

For derived worker table `82064F60`, the two fixed forwarding stubs resolve
to the following bodies (not the caller-provided renderer pre/post callbacks):

- `8243B120` -> slot +0x88 -> `8243B688`: atomically decrements worker+0xf8
  at `8243B6B0`, calls coordinator slot +0x60 at `8243B6CC`, and signals
  through worker slots +0xc8/+0xd0 at `8243B6E0`/`8243B6F4`.
- `8243B110` -> slot +0x84 -> `8243D018`: obtains the first embedded queue
  through coordinator +0x44 with r4=SP+0x50, selects an entry using
  `82439978(queue, worker+0x100)`, resets its cursor, and reads eight bytes
  from entry slot +0x50's returned pointer at `8243D094`. It releases the
  entry and queue references, atomically decrements worker+0xf4 at
  `8243D0D8`, advances/wraps worker+0x100, passes the eight-byte value to
  coordinator +0x5c at `8243D110`, then signals worker +0xc4/+0xcc.

`82439978` returns queue+0x30 plus index*60, retaining that entry through
slot +4 at `824399A4`; it has no index bounds check. Entry cursor reset and
getter resolve to the previously inspected `82432240`/`824323B0` for the
normal installed entry table. The payload operation above is a read, not a
geometry write. Outstanding-reference and queue-shutdown conditions remain.

Raw coordinator table `82064EE0` maps **+0x5c to `82431C08` and +0x60 to
`82431CB8`**. `82431C08` increments coordinator+0x13c, stores the incoming
64-bit value plus the unsigned +0x14c field at +0x130, and every fifth count
calls +0x78 (`82431D80`), between +0x0c/+0x14 calls. `82431D80` copies
+0x130 to +0x120 and stores its +0x74 virtual return at +0x128. The +0x74
callee remains separate. `82431CB8` only increments coordinator+0x140.
The additionally inspected `82431BF0` increments +0x144; it is not the
target of either of these two callback dispatches.

Raw derived worker slots +0xc4/+0xc8/+0xcc/+0xd0 resolve respectively to
`8243B2F0`/`8243B300`/`8243B310`/`8243B320`. Generated instruction bodies
pass worker+0x54/+0x64/+0x74/+0x84 respectively to `KeSetEvent`, with r4=1,
r5=0. Ghidra follows their tail branches into the undecodable import at
`8252D17C`, so all four exports are explicitly INCOMPLETE; the generated
import bindings establish the event call, not whole-import alias safety.

These inspected bodies narrow the fixed callbacks to queue/control-state
operations and event signaling, with no direct VB/IB payload store. This is
not closure of arbitrary callbacks, nested kernel effects, or concurrency.
Eleven new seeds (343 total); no runtime comparisons disabled.

## Registration wrapper and caller-search coverage

`82439FE8` is a second registration entry point: it leaves r3/r4/r5 intact,
sets r6=r3 (worker as callback context), and tail-dispatches through +0x38.
Raw tables `82064F60` and `82065678` both install this wrapper at +0x3c,
and `8243A000` at +0x38. Its callers can therefore register a callback
without explicitly supplying a context argument. The generated direct-call
index has no direct caller of this wrapper; virtual callers remain unresolved.
Ghidra export confirms all five instructions (344 seeds, saved successfully).

Added `tools/enumerate-geometry-registration-candidates.ps1` to make the
next caller search reproducible. It exports 162 syntactic load-to-CTR dispatch
candidates and a source SHA256 manifest under
`out/geometry-registration-candidates/`: 106 for displacement 0x38 (including
seven tail dispatches), 56 for 0x3c (including three tail dispatches).
Checked the known wrapper is present exactly once and every exported dispatch
offset points to a generated bctr/bctrl comment.

These are **not 162 confirmed registration calls**: a displacement can be an
object field rather than a vtable slot, and different object types reuse the
same slot. The bounded syntactic scan does not resolve register copies,
indexed loads, branches, pointer aliases, or argument provenance; it cannot
prove absence of other callers. Immediate-mode filtering alone did not identify
the missing providers. The CSV retains preceding instructions so register-
supplied modes and callback addresses can be traced without repeating the
whole source search. No runtime behavior changed.

### Candidate review and scan correction

The first inventory allowed an intervening instruction to overwrite the
loaded target before `mtctr`. Corrected that association and added synthetic
regression fixtures for direct/tail dispatch, target clobber, CTR replacement,
branch/function boundaries, and target changes after CTR is already loaded.
`tools/test-geometry-registration-candidates.ps1` passes. Regenerated inventory:
157 candidates (0x38: 96 calls + 7 tails; 0x3c: 51 calls + 3 tails), five
fewer than the initial scan. It remains a bounded syntactic search, not proof
of complete coverage or receiver identity.

Rechecked these tempting matches against raw installed tables and the Ghidra
instruction exports; they are not worker callback registration on these paths:

| Site | Receiver/table | Actual method |
| --- | --- | --- |
| 8242C6E4 | Factory coordinator r23 / 82064EE0 | +0x38 -> 82431480, configuration |
| 8243D968 | Retained renderer / 82064C78 | +0x38 -> 8242D048, queue-entry reference setter |
| 8242D088 | Renderer / 82064C78 | +0x3c -> 8242D0B0, release retained entry |
| 8243D06C | Selected queue entry / 820650F0 | +0x38 -> 82432240, cursor reset |

The generic tail-dispatch candidates `820DB210` (+0x38) and `820DB1D0`
(+0x3c) have no generated direct callers in the current direct-call index.
This does not exclude indirect use. Resolving their receiver types or their
address references is still needed before including/excluding them as worker
registration routes. No callback provider or runtime check was removed by
this inventory correction.

### Generic wrapper address references

Ghidra exports confirm `820DB210` and `820DB1D0` are four-instruction tail
dispatches through incoming object's +0x38 and +0x3c respectively. Neither
body has a direct store. The aligned raw-pointer scan found no literal word
for either target; this did not mean they were unreferenced: generated code
constructs both addresses with `lis -32242` plus signed low-half `addi`.

Selected verified references pack `(function address, zero adjustment)` into
SP+0x50..0x57, load it as a 64-bit r4, and call `820FD350` with r3 pointing
to an enclosing object's +0x4d0 callback descriptor and r5=0:

- `820DBCC0`: function `820DB210` (an earlier call at `820DBC94` passes the
  same descriptor to `820DB4E8` for a separate test).
- `820DBEC0` and `820DC184`: function `820DB1D0`.
- `820DBCE0` separately compares the stored callback identity against
  `820DB210`; this is not a call through the wrapper.

`820FD350` first invokes the old descriptor handler with mode 2 if present,
clears its three-word record, stores the incoming packed function/adjustment
at its own +0 (`820FD3BC`), and publishes `821E2A00` as the new handler at
`820FD3C8`. It then calls that handler at `820FD3D4` with mode 0. Generated
`821E2A00` loads the function from descriptor+0 and computes the actual
receiver as descriptor+8's value plus descriptor+4's adjustment, dispatching
at `821E2A34`. The receiver is therefore **not the descriptor address itself**.
Its +8 initialization remains to be traced before proving the concrete type.

This identifies descriptor registration/dispatch, not the worker's callback
setter. No blanket exclusion of these generic wrappers is justified yet.
Three new Ghidra seeds (347 total), all saved; `821E2A00` above was inspected
in generated/default/edf2017_recomp.10.cpp, not exported in this pass.
Existing unrelated incomplete-import warnings remain. No runtime changes.

### A concrete descriptor receiver initializer

`82103A60` supplies a concrete initializer for the +0x4d0 descriptor layout.
It saves entry r3 in r30, calls base `8210CA18`, and installs vptr `820065A4`
at `82103AC4`. At `82103D34` it stores that same enclosing object pointer
into descriptor+8 (enclosing object+0x4d8). It clears descriptor+0..7,
links descriptor+0xc to enclosing object+0x4c0 at `82103D44`, and registers
packed function `82100C80` with zero adjustment through `820FD350` at
`82103D48`. Thus this constructor's descriptor receiver is the enclosing
object, not a worker or a separately allocated renderer.

Research note `../edf2027-analysis/notes/render/0730-mothership-body-is-144-sgo-owned-child-objects.md`
identifies `82103A60` as the mothership constructor and describes its child
construction. Independently verified the installed table in the guest image:
`820065A4` +0x38 is `82104718`, +0x3c is `820825FC`, unlike either worker
registration slot. This establishes one gameplay-object descriptor path;
it does **not** establish that all `820DBBE8`/`820DBD80`/`820DC0E8`
receivers were constructed here, nor exclude other initializers or later
receiver replacement. The initial function on this path is `82100C80`,
not either generic wrapper being investigated.

Also exported `821E2A00` and verified its instruction listing: the dispatch
receiver is the zero-extended 32-bit descriptor+8 value plus the zero-extended
32-bit adjustment from descriptor+4; target is descriptor+0. It preserves
incoming r4/r5 in the emitted sequence. Ghidra's inferred prototype/call omits
some live arguments, so the instruction listing is authoritative here.
Two new seeds (349 total), headless save succeeded. This narrows receiver
provenance without certifying the mothership's entire callback effect graph.
No runtime comparisons removed.

## Opt-in live registration evidence

The native bridge now hooks `8243A000` observationally. Enable
`--edf_native_worker_callback_audit=true` to log worker pointer, raw mode,
function pointer, context pointer and caller return address for registrations
whose original calls return. The default is false. The disabled path calls
the original directly; the enabled path saves input registers in host locals,
calls the original exactly once, and logs those saved inputs afterward.
It adds no guest-memory reads, changes no guest arguments/fields, and makes
no callback-ownership or comparison-bypass decision.

Logging is limited to 256 completed registration observations per process,
followed by one truncation warning. Concurrent sequence numbers describe
observation order after return, not the exact guest publication order.
Addresses may be reused across lifetimes. A tail wrapper preserves an outer
return address, so the logged return address is not necessarily the setter's
immediate caller. No logged rows is not proof that callbacks cannot occur.

Built `out/build/win-native-clean/edf2027-native-scene-only.exe` successfully
with this diagnostic. All 19 existing CTest tests passed; they are regression
coverage, not a live test of callback-registration logging. The game was not
launched in this pass and no live registration sample has been collected.
This diagnostic is intended to identify concrete providers for the next static
audit; it does not replace static coverage or remove runtime comparisons.

Focused adapter verification now exists as `edf_native_worker_callback_audit_tests`.
The build extracts the production `8243A000` hook body, substituting only a
minimal context, cvar access, log sink and mocked original setter. The test
passed after compiling in the native build. It checks exactly-once forwarding,
preservation of all modeled callee output fields, pre-call argument snapshots
despite callee clobbering, log emission after the original returns, disabled
mode, null callback input, 32-bit return-address capture, and the 256-row/
one-warning cap. It does not execute the retail setter, prove concurrency
ordering, or demonstrate a live registration workload. The previous 19-test
regression result predates this separately passing new test.

Live probe: `out/native-bridge-run/binding-validation-20260910-193339-804d6894/`
contains the fresh-user hidden run's `game.log`, `state.png` (difficulty menu)
and `mission.png` (Mission 1 mothership introduction). Launched the diagnostic
executable with `-WorkerCallbackAudit` via the validation launcher; that switch
now forwards `--edf_native_worker_callback_audit=true`. The original input
sequence stopped at difficulty selection, so the reloadable script gained
late confirmations at 150/155 seconds. A logged confirmation at 150012 ms and
the subsequent mission capture establish that this sample reached the intro.

There were zero `Native worker callback registration:` rows. This does not
prove universal non-use, hook reachability, or absence of callbacks registered
through other paths. No callback target was learned from this sample. Treat
this callback branch as unobserved in the captured workload, not as a reason
to disable geometry validation. Indexed rendering continued with zero reported
vertex/index mismatches in the inspected tail; no whole-game claim follows.
The hidden run is not a performance benchmark or a visual-fidelity pass.

Owned PID 54420 was stopped after validating its executable path and exact
start time (2026-09-10 19:33:39); waiting on that process confirmed exit.
Logs, screenshots and isolated user data were retained. No geometry checks
were removed in this probe.
