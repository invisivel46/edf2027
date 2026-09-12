# Geometry writer enumeration

SGO input follow-up: geometry-sgo-input-ownership.md and its 13-edge caller CSV
separate successful CPU-heap file preparation from shared cache-node payloads
and raw caller inputs. Three direct file-loader setups are traced; their failure
and nested-record bounds qualifications remain explicit. Cache payload ownership
is not established merely by the local lifetime of its handle.

Current index maintenance: `tools/check-geometry-append-inventory.ps1` verifies
the eight direct `821C8A20` caller/CallPC pairs in geometry-append-callers.csv
against generated source. Its summary and geometry-vector-fill-writers.csv now
reflect the later append/tracker research below; the earlier statement that seven
append callers remain unclassified is superseded. This closes enumeration of
those direct edges, not indirect entries, later aliases or concurrent conversion.

Packed-bit-vector follow-up is in `geometry-bit-vector-ownership.md`: the tail
mask's direct resize chain reaches a local bitset in821BBAD8, and the separate
821B9EA8 set/clear writer has four enumerated direct callers. The three generic
caller chains now resolve to the same local bitset through fill and forward/backward
copy helpers; iterator helpers preserve its owner. Normal direct-path storage is
CPU-heap-backed scratch. Indirect entry and escaping aliases are not certified.
No comparison policy changed.

Four-byte vector-fill follow-up: all generated direct BA368 calls are BAFD0
(call PC 821BB04C) and C88A0 (821C891C). BAFD0's sole direct caller is BB7A8
(821BB838), the local bitset chain above. C88A0's sole direct caller is C8A20
(821C8ABC), whose eight pointer-vector callers are classified in the append CSV.
C8A20 constructs an end iterator from the same vector and preserves the input
value pointer in r6. Complete C88A0 preserves that pointer, requests one element
from BA368, and returns an iterator reconstructed from the same vector after
growth. Complete BAFD0 likewise constructs an end iterator when growing.
Addresses abbreviated in this paragraph have prefix 821. This connects both
normal direct routes to CPU-heap-backed vectors, including retained capacity;
it does not certify arbitrary indirect entry, malformed ranges, escaping aliases
or concurrent tracker replacement. The four fill-store CSV rows now reflect
this provenance rather than leaving their normal direct callers unclassified.

Requested sequence: enumerate writers, establish coverage, then remove per-draw
VB/IB comparisons. This inventory is in progress. Do not interpret a source-store
inventory or a zero-mismatch playtest as proof of complete geometry ownership.

## Reproducible artifacts

Run from the project root:

```powershell
.\tools\audit-guest-store-boundaries.ps1 -CsvPath out/geometry-writer-inventory/generated-store-sites.csv
.\tools\enumerate-geometry-writer-callers.ps1
```

Current generated source yielded 150,071 store instructions, 75,918 direct call
sites and 5,222 unresolved indirect call sites. All captured store instructions
were classified by emitted route (scalar, MMIO/scalar, vector, atomic or bulk zero).
Five source-line samples were checked against the generated files. The inventory
matches store/cache-zero instruction comments; this does not enumerate arbitrary
native SDK writes or prove the emitter has no other memory-writing constructs.

- `generated-store-sites.csv`: function, opcode, route, source line/offset,
  instruction and emitted statements for each captured store.
- `generated-call-edges.csv`: all matched emitted direct calls and indirect sites.
- `known-writer-direct-callers.csv`: callers of currently identified boundaries.
- `imported-provider-call-sites.csv`: SDK/import call sites requiring output-pointer
  classification, including calls outside graphics.
- `source-manifest.csv`: SHA256 of the generated source shards used for call indexing.

These are candidate indexes, not a claim that all sites write model payloads.
Stack stores, unrelated objects, inactive original routines and wrapper-replaced
code must be distinguished from actually reachable geometry writes. Unresolved
indirect targets must remain visible, not be silently counted as covered.

## Identified writer boundaries

| Boundary | Current direct callers | Payload relationship / coverage |
| --- | ---: | --- |
| `821D7530` model VB construction | 1 | Allocates and copies stride × count, then native publication |
| `821D76A8` model IB construction | 1 | Allocates and copies count × 2, then native publication |
| `82134958` / `821349B8` VB lock/unlock | 1 / 1 | `8242D440`: lock 120 bytes, copy from object+144, unlock |
| `82134A78` / `82134AD8` IB lock/unlock | 1 / 1 | `8242D2B0`: six inline uint16 writes, then unlock |
| `821E8320` bulk copy | 863 | Completed destination extent notified; callers still need alias classification |
| `821E8230` checked move | 22 | Four-argument checked wrapper; successful payload move calls tracked `821EA320` |
| `821E8740` forward copy | 27 | Independently implemented copy, completed extent notified |
| `821EA320` move | 43 | Backward inline writes notified; forward path uses 8320 |
| `821E9BA0` fill | 729 | Prefix/block/tail writes, completed extent notified |
| `NtReadFile` import | 5 | Requested extent conservatively notified after current synchronous SDK implementation returns |
| `RtlFillMemoryUlong` import | 1 | Completed floor(length/4) × 4 extent notified |

The two model constructors are called from subset loading (`821B3C98`), as
documented in the analysis project's render note 0330. All figures above count
direct source call sites, not runtime invocations or indirect calls.

## Still open before removing checks

1. Classify aliases of model payload addresses (owner+48 and header+24), including
   saved pointers and physical aliases, across their full allocation lifetime.
2. Resolve which generated scalar/SIMD/atomic/cache-zero stores can reach those
   aliases. The optional global observer is currently off in normal builds and
   its sampled notifications do not constitute exhaustive per-writer coverage.
3. Classify imported/native providers that receive writable output buffers.
   Two intercepted providers are not evidence that every provider is covered.
4. Preserve ordering for writes concurrent with snapshots/submission, nested
   locks, destruction and allocation reuse. A notification after a write alone
   does not protect a snapshot racing an in-progress writer.
5. For each closed resource class, route writes through native ownership and
   test byte changes, alias updates, failed/partial writes and lifetime reuse.
   Only then remove its content comparisons, retaining an opt-in audit.

The draw-failure guard started before this request was withdrawn without being
built or deployed. This work does not change draw behavior or remove checks yet.

## Writer-site attribution follow-up

Generated scalar/MMIO, SIMD, successful atomic and inline-fill adapters now
forward compiler source_location file/line through their nested helper calls.
Only instrumented builds enable those generated adapters; normal builds still
have global store observation off. No generated files are edited.

At notification, NativeBufferWrites captures exact overlapping owner IDs and
their subscription lifetimes with each source site. Per-batch attribution merges
repeated hits of the same site/owner/lifetime, retaining up to 64 distinct keys.
Further unmatched hits increment an explicit omitted counter. Drain logs every
retained key and warns on omission. Invalid extents still invalidate conservatively
but do not invent a precise writer-owner attribution. File names have static
compiler lifetime; no guest payload pointer is retained.

Tests cover source metadata being present in adapter callbacks, exact physical
alias hits, unrelated writes, repeated hits, owner reuse before drain, drain reset
and overflow without losing underlying invalidation counts. All 19 tests passed
in 12.43 seconds. An instrumented gameplay run with this attribution is pending;
this does not identify previously unseen runtime writers yet, and absence of a
site in any single workload cannot close the static inventory.

The store CSV also tags literal-displacement r1 stores as
`stack_relative_immediate`. This is syntactic triage under the PPC stack contract,
not general pointer analysis: it neither excludes pointer values saved to stack
nor classifies indexed/propagated addresses as stack-only.

### Mapping runtime records back to the inventory

`tools/summarize-geometry-writer-sites.ps1 -LogPath <log paths>` groups records
by file/line/owner/lifetime and resolves each source line against the emitted
store's source interval. It retains unresolved locations rather than attributing
them to a nearby instruction, reports omitted hits and deduplicates identical
log lines across supplied files. Use matching generated-source inventory for the
instrumented build. The fixture test (`tools/test-geometry-writer-summary.ps1`)
passed: repeated reports accumulate, duplicate lines do not, reused owner
lifetimes remain separate, an inter-instruction gap stays unresolved and overflow
is reported. This tests the report parser, not gameplay writer coverage.

The separate build tree `out/build/win-native-scalar-audit` is configured with
`EDF2027_OBSERVE_SCALAR_STORES=ON` and executable name
`edf2027-native-writer-sites`. The normal build remains uninstrumented.

### First instrumented run with source attribution

Build SHA256 `DE24406344CFD4A7586019F2D2FC16060DAB56F3CA04EA729280F3B2BCE81612`
passed all 19 tests in 12.42 seconds. Owned PID 21112 ran the movement/fire script
from 16:02:00 in `binding-validation-20260910-160200-0cbf73cd`, with fresh settings.
The captured gameplay shows changed position, enemies and 101/120 ammunition.
At 16:05:49, indexed submissions exceeded 16.7 million, with 767 builds, 428
entries, zero indexed errors and zero source mismatches. The last sampled exact
notification totals at 16:05:42 (batch 524288) were zero generated and zero
provider hits on subscribed model extents. Parsing all retained logs yielded zero
writer-site records and zero attribution-overflow warnings. The process was
identity-checked and stopped after the script.

This workload did not identify a post-publication model writer. It does not prove
all geometry is immutable: pre-publication stores, unregistered/nonphysical
buffers, uncovered providers, indirect paths and other content remain outside
that conclusion. No comparisons were removed from this result.

### Imported provider destination classification

Direct source examination narrowed these specific payload destinations:

- `82150D60 -> RtlFillMemoryUlong`, return LR `82150E80`: destination r1+192,
  length 800. Stack-local fill, not a direct model-payload write under the ABI.
- `821FBB48 -> NtReadFile`, return LR `821FBC24`: r8=r1+176, length 1024.
- `821FBFC8 -> NtReadFile`, return LR `821FC0CC`: r8=r1+208, length 1024.
  Both are stack-local file-read payloads; this says nothing about subsequent
  copies of their contents or the imports' other output parameters.
- `821FB530 -> NtReadFile`, return LR `821FB6EC`: r8=r27, length 65536.
  r27 comes from this function's `ExAllocatePool(65536)` at LR `821FB570`.
  Its pool allocation/lifetime is distinct evidence to inspect, not a model
  pointer supplied directly by the caller.
- Both `821FAC48 -> NtReadFile` calls use r8/r9 derived from entry r4/r5.
  This is the general caller-supplied payload route. Nine emitted direct call
  sites lead into it: `824332C0`, `82432E48`, `82432A80`, `8218B030`,
  `82524150` (two), `821A0550`, `8222E398`, `82412DF8`. These nine destinations
  are the next provider alias-tracing frontier; indirect callers remain open.

### Ghidra targeted first pass

Reused Ghidra 12.1.3 from the EDF2 workspace by copying its installation to
`out/ghidra/installation`. The original installation needed to compile its PPC
language definition, which would write outside this workspace; the local copy
avoids modifying EDF2. Settings/cache/temp also stay under `out/ghidra`.

Imported the research `guest_image.bin` at 0x82000000 using
`PowerPC:BE:64:64-32addr` into `out/ghidra/project/edf2027-geometry.gpr`.
`tools/ghidra/GeometryWriterTrace.java` successfully exported 17 targeted
functions (disassembly, decompilation, currently known references) to
`out/ghidra/geometry-writers`. Global auto-analysis was disabled; this is a
seeded inspection project, NOT a complete call/reference inventory.

The first inspected exports confirm the constructor allocation/copy/attach
sequence and the six index stores in 8242D2B0 between lock and unlock. They also
expose an analysis prerequisite: stock decompilation treats PPC register-save
helper 821E7F70 as returning the constructor owner, and infers a void return for
the vertex lock wrapper. Those inferred prototypes are not trustworthy. Correct
save/restore helper semantics and lock/provider signatures before relying on
decompiler alias propagation; retain instruction-level/generated-code checks.
Stock Altivec support also does not establish Xbox VMX128 coverage.

No geometry comparisons were removed by this first pass.

Follow-up: inlining the real instructions of eight `__savegprlr` entry points
(verified against `generated/default/edf2017_init.cpp`) corrected the spurious
owner-return inference in the VB constructor and allocation wrapper. The owner
now remains entry r3; allocation receives owner+0x20, and the constructor reads
the resulting payload from owner+0x30. This is an analysis-only change, not a
patch to guest instructions. The reproducible rerun is
`tools/ghidra/trace-geometry.ps1`; 20 targets now export successfully to
`out/ghidra/geometry-writers-inline`.

Added allocator 821D4490, release helper 821D3DC8 and common lock 82134408 to the
trace. The common lock returns the requested payload address, optionally mapped
to a physical alias by flag 0x10, and increments the lock count by 0x100. Its
other stores include resource dirty-range metadata and command-buffer entries;
these must not be mistaken for vertex/index payload writes. The allocator
retains payload addresses in suballocation records and copies one to the
embedded owner allocation record. Those allocator-held aliases and their reuse
remain part of the lifetime trace. Lock wrapper prototypes and atomic p-code
still require instruction-level verification before automated propagation.

The next pass explicitly models 32-bit pointer returns for common lock 82134408
and wrappers 82134958/82134A78. A return-only annotation initially locked empty
parameter lists and erased argument propagation; this was caught by inspecting
the export and corrected with explicit register storage. The common lock uses
r3-r10 plus caller SP+0x54 (stock Ghidra's PPC ABI defaults the next stack argument
to SP+8). The VB wrapper now exports all nine arguments, including 0x03000000,
and returns the pointer from the common lock. The core's r3=r28 instruction at
82134630 and the wrappers' unchanged-r3 epilogues support those return types.

Exports now include high-p-code STORE/CALL/CALLIND sites for 25 selected targets.
These operations expose a tracing frontier, not an automatic alias certificate.
Newly inspected 821D4090 splits/selects free blocks: record+8 is a payload address,
record+12 is extent size, and record+16 is allocation state; on split it advances
the free record's address and reduces its size. 821D4380 grows the pool through
821D3FF0 and inserts bookkeeping nodes. This distinguishes stores *of* payload
addresses from stores *through* them. Continue into backing allocation/free and
record consumers before treating these saved pointers as covered. All 25 exports
completed and the project saved successfully; no runtime checks changed.

### Pool allocation/release frontier

The next Ghidra run exported 29 selected targets. Instruction/generated-source
cross-checks establish the following route, without claiming whole-program
alias coverage:

| Function | Role in the model pool | Payload versus metadata |
| --- | --- | --- |
| 821D3FF0 | Obtains backing storage through 8212FB98; initializes a free-block record | Saves the returned address, does not fill geometry |
| 8212FB98 | Calls MmAllocatePhysicalMemoryEx (LR 8212FBF4) | Native backing allocation boundary |
| 821D3748 | Marks a block free; merges adjacent free records | Allocation-state/size stores, not writes through record+8 payload |
| 821D3698 | Unlinks and frees a bookkeeping node through 820B2510 | List links and node lifetime |
| 821D3B68 | Removes a pool chunk; releases backing storage through 8212FC28 | Backing lifetime boundary plus bookkeeping cleanup |
| 8212FC28 | Forwards entry r3 as r4, sets r3=0, tailcalls 8252CF3C | Import table identifies MmFreePhysicalMemory |

The raw image's 8252CF3C import stub is undecodable by the current Ghidra setup:
8212FC28 reports completed decompilation but emits halt_baddata. Its generated
source and import table provide the boundary evidence instead. The exporter now
explicitly marks this incomplete control-flow condition; do not interpret a
successful export as a successful semantic trace.

Native bridge source already intercepts 821D3DC8 before its guest critical section
and calls RetireAllocation(r4), invalidating the matching native mesh. Model VB/IB
destructors also retire ownership first. Existing tests cover matching/nonmatching
allocation records and repeated retirement. This narrows the normal release/reuse
path; it does not cover arbitrary aliases freeing/writing a live backing chunk.
Generated direct-call counts for this frontier: 821D4490=1, 821D3DC8=2,
821D4700=2, 821D3FF0=1, 8212FC28=6. These are source edges, not exhaustive indirect
call coverage. No comparison removal follows solely from these counts.

### Model draw consumers

The binding-call inventory identifies nine distinct direct callers of
82137410/821375C0 (stream/index binding). Two are the model paths below; the
others are 8242D440, 8242E868, 82139930, 8242EB78, 8242D138 and the two model
destructors. A direct-call inventory does not exclude indirect consumers.

- 821B2C28 iterates 148-byte subsets. At 821B2D14 it binds subset+4 as the
  vertex resource, with stride from subset+60; at 821B2DC0 it binds subset+84
  as the index resource. It selects declaration/material state and submits at
  821B2EE0. Its inspected body has no non-stack stores or loads of the payload
  pointer fields (subset+52 / subset+132). Callee side effects remain separate.
- 821D96D8 obtains the subset from queue+12 and binds the same embedded headers
  at 821D972C/821D9770. It walks queued instances, calls 821D9600, then submits
  at 821D97E4. Its direct non-stack store at 821D9810 clears queue+4, not geometry.
- 821D9600 traverses 12-byte records in instance+[16,20), forwarding each to
  vertex constant setter 82149248 at 821D96B0. It has no direct non-stack stores.
  The bridge documents the setter destination as the device constant bank;
  this is a constants-upload path, not evidence of CPU vertex skinning.
- 821D88C0 supplies the file-relative mesh records to 821B3C98; the latter copies
  source vertex/index data through the two constructors. Publication follows
  each completed constructor. This connects source-file storage to separate
  model-pool storage rather than proving the file buffer itself immutable.

The last Ghidra run saved 33 targeted exports. These selected draw bodies do not
introduce a new direct payload writer. Remaining work includes other consumers,
saved/physical aliases, virtual calls and imported providers; this is not a
whole-program immutability proof. No gameplay code or comparisons changed.

The remaining direct binding callers are now classified in
`docs/geometry-binding-consumers.csv`. `tools/check-geometry-binding-inventory.ps1`
compares the list against generated call edges; it verifies the direct-caller
list, not the semantic classifications or completeness of writer tracking.

82139930 resets device bindings with null resources. 8242E868 and 8242D138
unbind resources and clear released handles; the latter retains unresolved
virtual cleanup slots +0x3c/+0x58. 8242EB78 binds object+0x3c/+0x40 IB/VB
handles, uses stride 20 and submits six indices. Its row-copy destination comes
from texture lock 8213B5A0 for three resource planes, followed by 82139B90 unlock.
Its virtual source getter and callbacks at object+0x108/+0x10c remain unresolved.
Do not equate the presence of memcpy here with a model vertex-buffer write, or
equate its direct texture destination with proof that its callbacks cannot write
geometry. This run saved 37 targeted exports; no runtime comparison changed.

### Static function-pointer candidates

The Ghidra exporter now scans initialized image blocks for aligned big-endian
32-bit words equal to the selected function addresses, recording each location,
target and decoded-instruction versus data/unclassified status in
`function-pointer-candidates.csv`. This also sees undecoded data references that
the seeded direct-reference view misses. It does not find computed, unaligned,
encoded or runtime-created function pointers, and a matching word alone is not
a vtable proof.

The first scan returned 36 candidates. Most lie in alternating function-address
and descriptor records (for example 82089938 contains 82134408 and the following
word is 40008D03). These resemble unwind/function metadata and must not be
reported as actual indirect callers without a consumer. Three candidates sit
in consecutive function-pointer tables: 8206083C -> 82412DF8,
82064CA4 -> 8242E868, and 82064CC8 -> 8242EB78.

The file-read table is independently tied to an object: generated 82412AE0
initializes an object's first word to 82060824, and 82412EA8 restores that same
table during cleanup. Thus 82412DF8 occupies virtual offset 0x18 in that table,
not merely a coincidental pointer match. This exposes a concrete indirect route
into the caller-supplied file-read destination, outside the nine direct calls
to 821FAC48. Next trace the object returned by 82412AE0 and calls to slot 0x18;
the other two table candidates still need receiver/base validation. Runtime
geometry comparisons remain unchanged.

The factory chain is now concrete: 824109F0 allocates an 8-byte provider,
writes vtable 82060798 at 82410A7C, and passes it to 82410830 at 82410AA4.
820607AC contains 82412AE0, establishing the provider's create/open slot +0x14.
82412AE0 has no emitted direct callers in the current call-edge inventory;
that absence is explained by this virtual entry, not by the function being dead.
Its successful path allocates a 28-byte file object, installs 82060824, stores
the opened handle at +4 and file size/remaining count at +8/+12, then publishes
the object through entry r5. Read slot +0x18 remains 82412DF8, which forwards
the caller's payload destination to 821FAC48. The factory itself initializes
objects and filename buffers; it does not directly receive a geometry payload.

824109F0's emitted direct caller is 823F1328. The next receiver trace is the
provider registry at 82410830 and its consumers; this pass has not resolved all
read-slot calls or their destination provenance. The Ghidra project saved 40
targeted exports successfully, with the previously identified physical-free
import stub still explicitly marked incomplete. No comparisons changed.

Registry follow-up: 82410830 registers providers in the map referenced by
8257ED34. 82410068 looks them up; its direct caller 824100F8 invokes provider
slot +0x14 and returns the resulting file/stream object (with an optional wrapper
and cached-provider path). 82410450 and 824105E8 parse names and forward to this
open routine; they are not themselves byte-read destinations. Seven emitted
calls into these two open routines come from five functions: 8242B4B8, 82418620,
82422D90, 823F4288, and 8240CC30. All five are now exported for inspection.

Two inspected consumers narrow the destination frontier:

- 82418620's non-mapped path queries byte count through 8240FB38, allocates that
  count with 823E6270 at 824186C8, then calls 8240FB38 with the fresh allocation
  at 824186F0. The mapped path instead obtains an existing data pointer through
  8240FAE0. The read destination on the allocating path is not an incoming model
  payload; mapped-data ownership and downstream conversion remain separate.
- 8240CC30 consumes this API to parse an image beginning with magic 0x58554942
  (XUIB), version 5. Its non-mapped path uses 8240FC20 to obtain file bytes; the
  mapped path uses 8240FAE0. This identifies an XUI binary consumer without
  asserting that every user of the provider API is UI-only.

The next concrete read boundaries are 8240FB38 and 8240FC20, plus the other three
open callers and mapped-data lifetime. Fifty targeted exports saved successfully;
the existing undecodable physical-free stub remains flagged. No runtime changes.

Read-wrapper follow-up (55 exported targets): 8240FB38 uses a null destination
to query virtual slot +0x14, otherwise forwards r3/r4/r5/r6 to read slot +0x18
at 8240FB90. Its seven emitted direct calls are now destination-classified:

| Caller / call instruction | Payload destination |
| --- | --- |
| 8242B4B8 / 8242B5F8 | Null: size query only |
| 8242B4B8 / 8242B634 | Fresh allocation stored at object+68 |
| 82418620 / 824186B8 | Null: size query only |
| 82418620 / 824186F0 | Fresh allocation from 824186C8 |
| 8240FC20 / 8240FD00 | Fresh allocation from 8240FCC0; published through output only after success |
| 82422E50 / 82422F18 | Fresh allocation from 82422EE4, through caller's output slot |
| 82422F90 / 8242300C | Caller-supplied entry r6: unresolved destination provenance |

82422E50 has a separate mapped-pointer-plus-offset branch using 8240FAE0, not a
byte read. 8240FAE0 forwards to slot +0x20; it does not itself copy payload bytes.
823F4288 and the XUIB loader 8240CC30 use read-all 8240FC20 on their non-mapped
paths. 82422D90 initializes a stream descriptor and queries its size, leaving
actual range reads to subsequent calls. Fresh allocation here excludes a directly
supplied published model payload on the inspected path, not downstream aliases
or arbitrary output-slot writes. The unresolved explicit destination route is
now 82422F90, alongside indirect/mapped consumers. All comparisons remain on.

### Explicit range-read destinations

All five emitted callers of 82422F90 were inspected in Ghidra and their call
arguments checked against instruction listings:

| Caller / call instruction | Destination and extent | Remaining question |
| --- | --- | --- |
| 82420CE8 / 82420D30 | Caller stack+0x58, 12 bytes | Subsequent parsing/copies are separate writers |
| 824211C0 / 8242120C | Caller stack+0x50, 4 bytes | Subsequent parsing/copies are separate writers |
| 82420F88 / 82420FD8 | Caller stack+0x60, 0x118 bytes | Subsequent decompression is separate |
| 82423E48 / 82423F94 | Object+0x128 pointer, variable range | Reused allocation; pointer escapes as function return |
| 82423B38 / 82423D40 | Object+0x124 pointer, 0x1000 bytes | Initialization and pointer escape unresolved |

82423E48 grows its reusable buffer via 823E6270 at 82423F5C, stores the pointer
at object+0x128 and capacity at +0x12c, then either reads into it or memcpy's
from already-loaded data. It returns that buffer to its caller. Thus this is a
real repeated payload writer, but not yet identified as a model geometry writer.
82423B38 reads a staging block, hashes it via 8252D43C, and passes it to
8241FDB0 with separate cache-block output storage. The input object+0x124 and
decompression output must not be conflated.

824211C0 dispatches on the four-byte `xttf` signature (0x78747466), tying these
header paths to a font-format reader. This does not by itself establish that
all returned buffers stay disjoint from geometry. Follow the two saved-pointer
fields and returned data; stack header reads are no longer unresolved direct
geometry destinations under the ABI. Added real save-helper entries 821E7F5C
and 821E7F64 (confirmed in the generated import/function table) to prevent
spurious incoming-object replacement in these decompilations. Sixty targeted
exports saved successfully; the physical-free import stub remains flagged.

Font-buffer lifetime follow-up: 82423090 initializes the 0x1a0-byte reader
object, allocates 0x1000 bytes via 823E6270 at 824230C4, and stores that staging
pointer at +0x124 (824230D4). It zeros +0x128/+0x12c. 82423108 frees and clears
both saved pointers (82423130/34 and 82423164/68), confirming their intended
ownership. This rules out interpreting +0x124 as an arbitrary input pointer in
the normal initialized-object path; overwritten fields and other aliases are
not excluded by constructor examination alone.

82423E48's two direct callers were inspected: 824217F0 copies returned bytes into
its own growable allocation at object+0x44, then parses glyph-like headers;
824245F0 forwards the returned pointer to 82424648. Neither passes it directly
to the model constructors in its inspected body. The latter consumer remains
to be followed before declaring the returned pointer contained.

823E6270 is not an unspecified allocator callback: it sets flags to 0x248e0000
and tailcalls 8212F420. Those flags select the non-physical branch, forwarding
to 8212FB50 (not 8212FB98/MmAllocatePhysicalMemoryEx used by the model pool).
8212FB50 obtains the process heap via 82132F38 and invokes its allocation helper.
This establishes distinct allocation routes; backing-heap implementation still
needs checking before asserting physical disjointness for all aliases. The
matching free 823E6298 takes the corresponding non-physical release route.
Sixty-six targets exported and the project saved. No runtime comparison changed.

The remaining direct glyph-data consumer 82424648 reads the supplied compressed
glyph record and decodes coordinate/flag arrays into separate growable storage.
Composite glyphs recursively call 824245F0; the staging pointer is not directly
stored as a model payload in the inspected body. Downstream geometry-building
helpers 82424CC0, 82423570, 824235D8 and 82423378 remain separate consumers.

Heap follow-up: 82132F38 returns the global heap handle at 82582494. The observed
initialization path populates it from 82131498 with flags=2, null supplied base,
and a 4096-byte commit argument (call returning at 8212FAA0). 8212FB50 forwards
to 82131A48. The latter's large-allocation branch calls import 8252CE3C, identified
by the generated import table as NtAllocateVirtualMemory. Its small-block
growth/segment paths and heap creation still need tracing for a complete backing
claim. Ghidra currently lacks that import's writable-output signature and shows
spurious null-derived stores after the call; those are not accepted as real
destinations. Runtime source in xboxkrnl_memory.cpp is available for verifying
the virtual versus physical allocation implementation. Seventy targets exported
and the project saved; no checks were removed on this partial heap evidence.

Heap creation/growth follow-up (73 targeted exports): 82131498's null-base
creation path reserves through NtAllocateVirtualMemory at 821317C8 and commits
at 82131818. 82130EF0 searches the existing 64 segment slots, then, when growth
is allowed, explicitly zeros the requested base at 82130FEC before reserving
through the same import at 8213101C (retry at 82131060). It commits that returned
base at 821310B0 and passes the resulting bounds to 82130D88 at 821310E0.
82130638 splits/inserts free blocks and updates heap metadata; its inspected
body has no allocation calls.

The runtime implementation in rexglue-sdk/src/kernel/xboxkrnl/
xboxkrnl_memory.cpp selects LookupHeapByType(false, page_size) for a null-base
NtAllocateVirtualMemory request, whereas MmAllocatePhysicalMemoryEx selects
LookupHeapByType(true, page_size). Thus the observed fresh font-buffer backing
route is virtual, unlike the model pool's physical backing. A fixed-base commit
uses LookupHeap(address), so the import name alone is not a disjointness proof.
Remaining heap frontier: existing-segment commit 8212FFA0 and segment setup
82130D88 have not yet been inspected here; external pointer replacement and
downstream copies remain distinct alias questions. This narrows the font-buffer
candidate family, not the complete geometry writer set. All steady per-draw
geometry comparisons remain enabled. This was analysis-only; no game build or
performance test was performed.

Existing-segment follow-up (75 targeted exports): 8212FFA0 takes the pending
commit address from a segment range-list node at +4 and commits it through
NtAllocateVirtualMemory at 82130054. There is also a real optional CALLIND at
82130034 through heap+0x584. The process-heap initializer 8212F9F8 zeros all
48 option bytes, then sets only the size field before calling 82131498;
82131498 copies those options and stores option+0x24 to heap+0x584 at 82131A08.
Thus the normal process heap initializes this callback to null. Its null-base
creation branch additionally rejects a non-null callback. This is an
initialization-path conclusion, not proof against later pointer replacement.

82130D88 sets up the segment using the supplied base/end bounds and, if the
initial committed region is too short for metadata, commits more at the
supplied committed end via NtAllocateVirtualMemory at 82130E24. It publishes
the segment in the heap's slot table at 82130EC8, registers its uncommitted
tail via 8212FE80, and inserts free blocks via 82130638. On these inspected
normal paths the backing remains the virtual reservation; neither routine
selects the model's physical allocator. Tail-range bookkeeping in 8212FE80
is not a new backing provider established by this pass and is not claimed
fully audited. Downstream copies must still be classified independently.

Added the exact save-helper entry 821E7F6C (__savegprlr_25, verified against
generated/default/edf2017_init.cpp) and reran the exports successfully. The
known physical-free import decoding warning remains. No geometry checks were
removed. Next priority returns to geometry/header pointer aliases and the
unresolved caller-buffer/virtual-getter file destinations, rather than treating
every font metadata operation as a potential model allocation write.

### Buffered file-reader destination trace

Ghidra exports now include 82432A80, 82432E48, 824332C0, 82432818 and
8243DF20 (80 targets total). The three file-read sites use the buffer object's
vtable byte offset +0x28 (decimal 40), not byte offset +0x40. Their enclosing
stream holds a linked buffer ring at +0x84 and current buffer at +0x80;
buffer links are +4/+8. Reads into a ring buffer and copies out to the caller
are separate writer boundaries. In particular, 82432E48 copies to entry r4
plus the completed-byte count at 82433260; 824332C0 has the corresponding
caller-destination copy. Both use the already intercepted 821E8320, but that
does not settle concurrent snapshot ordering or other writers.

82432818 initializes this ring. With null entry r4 it allocates block-size times
block-count via 8212F420 using flags 0x208c800a (call 82432894). With non-null
r4 it uses caller-provided storage unchanged. It creates 0x60-byte buffer
objects via 8243DE78/8243DF20, links them through slot +0x18, then passes
successive block addresses to slot +0x24 at 82432918. Therefore this family
cannot yet be excluded merely because its default allocation is non-physical.

8243DF20 installs vtable 820657B0, verified against generated instructions and
the raw image. Its +0x24 entry at 820657D4 is 8243E040; its +0x28 entry at
820657D8 is 8243DF88. Generated code confirms 8243DF88 simply returns
object+0x30. 8243E040 stores a non-null caller buffer into that field at
8243E0D4; its null-input alternative allocates with flags 0x208c8002 and stores
the owned pointer separately at +0x2c. This resolves the normal ring's getter
target and identifies a concrete external-pointer ingress, not an immutability
certificate. These two small methods were checked in generated code rather
than added to the current Ghidra export count.

There are no direct generated call edges into 82432818 or the three reader
methods in the current index. Raw image entries place 82432818 at 820651C4
and the readers at 8206518C/820651C8/820651D0, among other pointer candidates.
Next trace: the owning stream's vtable installation and virtual callers of
its ring initializer, particularly non-null entry r4. Runtime-created or
replaced vtables remain outside the raw pointer scan. No comparison was removed
and no new gameplay build was produced by this analysis pass.

Stream construction follow-up (85 Ghidra targets): 82433800 installs vtable
82065150 and initializes the 0x88-byte stream object. The direct-call index
has four construction sites, all in 8242C7B8. For that vtable, +0x60 is
82432718 (descriptor dispatcher), +0x64 is 82432688 (existing handle),
+0x68 is 82432608 (wide filename), +0x6c is 824324F8 (narrow filename),
and +0x74 is 82432818 (ring setup). These offsets distinguish the stream
setup API from unrelated classes with a similarly numbered virtual slot.

Generated instructions for 82432718 show descriptor+0x20 supplies the staging
pointer in all three branches: it is passed in r6 for filename setup or r7
for handle setup. 824324F8 forwards entry r6 to the ring initializer's r4
at 824325EC; 82432688 forwards entry r7 at 82432708. 82432608 only converts
the filename into a stack buffer and forwards to the narrow-filename slot.

The inspected factory descriptor construction zeros 48 bytes and populates
flags, source, range and size/count fields, leaving descriptor+0x20 zero.
For those factory paths, the ring therefore selects its own allocation, not
an external geometry pointer. This does not cover subsequent reinitialization
of a returned stream through its public virtual methods. Factory switch-case
coverage also needs generated-code cross-checking: Ghidra reports a fragmented
function body and a type-propagation warning. Do not treat the decompiled
switch alone as exhaustive. Added verified __savegprlr_20 entry 821E7F58 to
remove its false call-result alias and reran successfully. No checks removed.

Factory switch cross-check completed against the full generated function:
8242C810..8242C81F is a four-entry absolute jump table, with targets
8242CBCC, 8242C820, 8242CA20 and 8242CAC0 for input kinds 1..4;
out-of-range kinds also take 8242CBCC. Only the 8242C820 and 8242CBCC
families construct the 0x88-byte ring streams. Their descriptor zero loops
are 8242C968, 8242C9F8, 8242CD04 and 8242CD88, each clearing six 8-byte
words starting at stack+0x60. None of the subsequent descriptor field stores
overwrites stack+0x80 (descriptor+0x20) before the setup calls at 8242C9C4,
8242CD54 and the shared 8242CDD8. Thus all four inspected ring-stream factory
setup paths request private backing. Kinds 3/4 construct different classes;
they are not covered by this ring-stream conclusion.

The Ghidra script now explicitly disassembles these four switch arms and sets
the verified function body to 8242C7B8..8242C80F plus 8242C820..8242CE7F,
excluding the inline table. Rerun succeeded; the exported instruction-address
set contains all 430 expected instructions and no table entries. The remaining
type-propagation warning is retained, so generated instructions remain the
authority for descriptor argument flow. This fixes the previous listing gap,
not arbitrary switch handling elsewhere.

Next lifetime boundary: the factory passes both stream objects to 8242C2E8
at 8242CDFC before releasing its local references. Trace their storage and
later use there to determine whether ring reinitialization can occur. The
initial allocation evidence does not by itself certify lifetime immutability.

Stream retention follow-up (88 targeted exports): 8242C2E8 passes its first
stream (entry r5, saved r25) to a 680-byte wrapper through slot +0x44 at
8242C634; the second stream (entry r6, saved r24) goes to a 552-byte wrapper
at 8242C660. Constructors 82430FD0 and 8242FD10 install vtables 82064E20
and 82064CE8, respectively (checked in generated instructions). Raw image
entries 82064E64 and 82064D2C resolve those attachment methods to 82430460
and 8242FC50.

Both methods retain the input through stream slot +4, invoke their wrapper's
old-stream cleanup slot +0x4c, and save the stream at wrapper+0x34. The stores
are 824304A0 and 8242FC94. These are object-reference stores, not payload
copies or ring-buffer pointer replacement. 82430460 then creates a context
via 82437D18 and publishes a wrapper backpointer at context+0x260; 8242FC50
creates a context via 82436420, stores it at wrapper+0x208, and invokes
8242F818. The former also queries stream slots +0x24/+0x28. These observed
stream calls are not its ring initialization slots, but callbacks through the
new contexts remain an open use path. Ghidra's import-output/type inference
removes some blocks from the first attachment's C, so its apparent zero
arguments are not accepted as proof that those callbacks cannot access data.

Next: enumerate uses of the saved wrapper+0x34 stream reference and the context
backpointer, including cleanup and callback read/seek paths. The factory's
local releases do not end the stream lifetime because these wrappers retain it.
All geometry comparisons remain on; this pass changed analysis only.

Wrapper-use follow-up (94 targeted exports): cleanup slots +0x4c resolve to
82430800 and 8242F388. They dispose the context through 82437A80/824350F8,
release the saved stream via its slot +8, then clear wrapper+0x34 at
82430874/8242F400. Context cleanup internals remain a separate boundary.

8242FA60 and 82430C10 are cache-window readers: each rejects requests above
0x10000, preserves an overlapping suffix via copy/move, resets/seeks the
saved stream, then reads through stream slot +0x3c into its cache. Destinations
are wrapper+0x20c's pointer in the former and wrapper+0x2c's pointer in the
latter, plus the preserved-byte offset; read extents are 0x10000 minus that
offset (calls 8242FBC0 and 82430D60). Both return a pointer into the cache
through entry r6, so returned-cache consumers remain relevant writers/aliases.
These are not calls to ring setup (+0x74), nor stores of cache addresses into
the ring's backing-pointer field. Cache allocation/lifetime still needs tracing.

For the known stream vtable, +0x34 maps to 82432DB8, +0x38 to 82432DF8 and
+0x3c to 82432E48. Generated code confirms 82432DF8 with argument 1 resets
position to stream+0x38's starting offset and clears its end flag; it does
not replace the ring. 8242F818 invokes this same reset before context parsing.
8242F310 also resets the stream, then calls its own wrapper's attachment
slot +0x44 with the existing stream. That reattachment retains the stream
before old-context cleanup/releases, as traced above; it is not itself
stream ring reinitialization. Effects reachable through context callbacks
are not excluded by these direct-body observations.

The offset-use scan was a candidate search restricted to 8242/8243 functions
and selected base registers, not a whole-program alias inventory. Next focus:
the two cache pointer allocations/returned aliases, plus context callbacks.
No normal-build hook replacement for these six routines was found in the
searched src/tools C++ headers/sources. No runtime checks or game build changed.

Cache allocation follow-up (98 targeted exports): wrapper initialization slots
+0x2c map to 824302D8 and 8242F188; full teardown slots +0x3c map to
824303D0 and 8242F288. The former initializer allocates exactly 0x10000 bytes
via 8212F420 with flags 0x208c8017 at 8243033C, then stores the result into
wrapper+0x2c at 82430344. The latter allocates the same extent with flags
0x208c801d at 8242F1C4 and stores it at wrapper+0x20c at 8242F1CC.
Neither inspected initializer provides a caller-buffer alternative. Both flags
select the non-physical allocation route already traced through the process
heap. These paths do not substitute a model-pool allocation for the cache.

Full teardown frees those exact fields using matching flags through 8212F4B8
at 824303F8/8242F2B0 and clears them at 82430400/8242F2B8, before wrapper
cleanup callbacks. These are different from stream-only cleanup, which
retains the cache allocation for possible reattachment. Initialization first
invokes full teardown and invokes it again on failure. Pointer users after
teardown and callback effects remain lifetime questions; allocation provenance
alone does not establish safe concurrent access or rule out later corruption.

The direct-call inventory identifies only forwarding entries 82430FC0 and
8242FC40 into the two cache readers; actual callback registration/consumption
still needs tracing. Updated scripts exported all 98 targets successfully,
with the previously known import decoding warnings retained. This was an
analysis-only change: all steady per-draw geometry comparisons remain enabled.

Cache-consumer follow-up (103 targeted exports): 82430FC0 loads its wrapper
from input context+0x260, narrows the file offset to 32 bits and tailcalls
82430C10. The context/backpointer origin matches 82437D18 and 82430460:
allocate/zero a 624-byte context with flags 0x248c8000, then store the wrapper
at +0x260. 8242FC40 instead moves entry r7 into r3 before tailcalling
8242FA60; its cache owner is explicit caller state, not a pointer obtained
from a vtable in that forwarding entry.

The current generated-call index has 63 direct sites across 16 functions into
82430FC0 and one into 8242FC40 (82440698). These are callers of the forwarding
entries, in addition to the previously documented single tailcall into each
reader. The 16-function consumer family remains open; the absence of aligned
raw function-pointer candidates for the forwarding entries is not evidence of
unused code.

82440698 obtains a descriptor from state+0x44 (growing via 82440490 if empty).
At 82440710 it invokes the cache reader with owner=state+0x3c, offset=state+0,
length=state+0x34 and a stack output-pointer slot. If the returned cache pointer
differs from descriptor+0's destination pointer, it copies the returned extent
into that destination via 821E8320 at 82440780. It publishes the descriptor
to its caller and advances the stream offset. Thus this inspected consumer
copies out rather than saving the cache pointer into the descriptor; the
descriptor payload allocation still needs classification. Raw pointer candidate
82098418 references 82440698, but its dispatch ownership is not yet established.
No geometry comparisons were removed by this pass.

Descriptor allocation and parser follow-up (121 targeted exports): 82440490
allocates a 24-byte descriptor, a payload of state+0x34 bytes, and an 8-byte
link through 8243E6D0 at 824404C4/824404E4/82440504. The payload output is
written directly into descriptor+0. 8243E6D0 ignores its nominal provider and
category inputs in the inspected implementation: it allocates entry r5 bytes
through 8212F420 with fixed flags 0x208c8000 and writes the result through
entry r6. Therefore this fresh descriptor payload follows the non-physical
heap route, not a caller-provided model pointer. Free-list reuse and consumers
of published descriptors remain separate lifetime questions.

All 16 direct consumer functions of 82430FC0 are now exported, not all audited.
Inspected 82440B58 and 82440D28 read 24-byte records and decode fields into
their caller-supplied output objects; those output destinations still require
caller classification. They do not directly modify the returned cache bytes.
82440A18 caps a requested string-like payload at 0x200, allocates length+2
using flags 0x248c8000, zeroes it, and copies cached chunks of at most 0x80
bytes into that allocation. It publishes this new pointer, not the cache
pointer, and frees it on failure.

82441F70 allocates a 32-byte metadata record at context+0xcc, reads five
16-bit lengths, then calls 82440A18 for five separately allocated fields.
Its inline byte-swapping stores target those copies. They are real scalar
writers but are not writes into the cache or model pool on this allocation
path. The record and its returned fields still have downstream lifetimes.
Other exported consumers need inspection, particularly the larger parsing
functions; export count is not a writer-coverage certificate.

Added verified register-save entries 821E7F44/48/4C/54 (__savegprlr_15/16/17/19)
and reran after the new parsers exposed another false save-helper result alias.
All 121 targets exported and the project saved; known import warnings remain.
No gameplay code, builds, or steady per-draw comparisons changed in this pass.

Consumer checklist: docs/geometry-cache-consumers.csv now records all 16
82430FC0 direct callers and their 63 call sites, checked against the generated
call index. Ten have partial body reviews and six remain exported/unreviewed;
none is marked fully alias-certified. The count check validates indexing only,
not semantic classifications or indirect-call completeness.

New body observations: 82442810 reads a length, allocates that length through
8212F420 with flags 0x248c8000, stores the pointer at context+0xe0 and copies
cached chunks into it. Instructions at 8244295C/8244299C confirm the copy
destinations and r5 extent even though Ghidra omits the copy's third argument.
82442338 and 824429C0 build separately allocated record tables at context+0xd0
and +0xe8, respectively. Their value buffers come from 82440A18 or fresh
8212F420 allocations, followed by copy/byte-swap operations on those buffers.
Neither inspected path publishes the cache pointer as a value buffer. Their
fragmented switch bodies still need instruction-level coverage verification.

82443408 decodes a stack-local header, calls 82440D28 with stack-local output
objects, then dispatches to 824429C0/82442F18 and advances the context cursor.
This classifies one specific output-pointer caller of 82440D28, not every caller.
All downstream lifetimes remain explicit in the checklist. No runtime comparison
was removed and no new performance result follows from this analysis.

Remaining cache consumer body review: all 16 checklist entries now have partial
reviews (63 direct read sites); the earlier ten-reviewed/six-unreviewed count is
superseded. None is globally alias-certified. The six additional bodies inspected
are 82443BF0, 82444210, 82438B38, 82441350, 82442F18 and 82441C38.

Instruction-level checks strengthen three local classifications. In 82443BF0,
r30 retains entry context and r31 is context+0x1a8 (82443C00/82443C2C).
Explicit non-stack stores use r31, within context+0x1a8..0x1ef. In 82444210,
r31 retains entry context, r29=context+0x1a8, r30=context+0x1f0
(82444220/82444240/82444254). Its explicit non-stack stores use these
context bases, including the 8-byte timestamp at +0x258 (8244464C) and
cursor at +0x1ac (824447DC). Neither inspected body stores through its returned
cache pointer. This describes direct body stores, not the transitive effects of
the cache reader or a proof of every caller's context identity.

82441C38 copies into context+0x76, +0x9c and +0xac at
82441D30/82441DE8/82441EA0. Before each copy the instruction stream places
the returned read count in r5, requires equality with the requested count,
then rejects counts above 0x20, 0x10 and 0x20 respectively. These bounds
confirm copy extents omitted by the decompiler's copy signature.

82441350 decodes format metadata; its fragmented switch needs full instruction
coverage. 82442F18 fills inline context descriptor slots and delegates to
82441350; count updates and array bounds remain an explicit review boundary.

82438B38 is an unresolved alias frontier: it passes its caller's output-pointer
slot directly to 82430FC0. Several normal paths copy into the buffer referenced
by context+0x264 and replace the output with that buffer. Some intermediate or
error paths can leave the cache pointer in the output slot. A special path also
invokes 8244C670 on the context+0x264 buffer. Do not classify this as an
unconditionally copying consumer. Next trace the buffer's allocation/overwrite
sites, caller handling of the returned pointer and errors, and the transform.
Its fragmented switch still requires instruction-level coverage verification.

These are analysis/checklist changes only. Steady per-draw vertex/index
comparisons remain enabled; no build or performance improvement is claimed.

Cache-output follow-up (127 targeted exports): 82437DD8 allocates the buffer
referenced by context+0x264 through 8212F420 at 824386C4 and publishes it at
824386CC. Its size is either 256 or context+0x18; the flags are 0x248c8000.
This establishes a fresh non-physical allocation on this setup path, not every
possible later overwrite of the pointer. 8244C670 is an in-place XOR transform
over its entry r4 buffer, using up to 16 bytes per iteration and a stack-local
block generated by 8246DE58. The inspected 82438B38 call supplies context+0x264,
not the cache pointer. The block generator's transitive effects remain unreviewed.

The generated source contains one direct 82438B38 caller, 824391C0. At
82439284 it supplies a stack-local output slot (SP+0x64) and count (SP+0x60).
Negative errors normally return without consuming that output, but 0x80040005
is explicitly forwarded with an error flag. Nonnegative returns are forwarded
too, with return code 3 setting a separate flag. At 824392E8 it passes the
pointer/count to 824454D0, using context+0x248 as the processing object.
Thus the previous error-path alias concern cannot be dismissed as unused output.

Repaired Ghidra's 824391C0 body using the raw image switch table at
82439248..82439257 (targets 8243934C, 82439258, 82439308, 8243934C),
cross-checked against the generated implementation. The export now contains
all 106 instructions in 824391C0..82439377 excluding the 16-byte table.
This verifies this dispatcher's instruction coverage, not all indirect callees.

824454D0 writes the supplied pointer/count into a zero-initialized 40-byte
stack request at SP+0x50/+0x54 (8244557C/82445580). Its indirect call at
824455B8 invokes processing-object+0x2c8 with that request. These instruction
stores are essential: the decompiler omits the request's first two fields in
its rendered C. Setup 8244BCB8 contains a matching setter selecting 82445618;
generated instructions construct that address and store it at object+0x2c8.
This setup body is fragmented and other configuration paths are not certified.

82445618 reads the request and forwards its pointer/count and flags through
824604C0 on the request+0x24==0 path; the alternative delegates to 824604C8.
The inspected 824454D0 construction leaves that field zero. The remaining
alias frontier is therefore inside this processing chain, especially 824604C0,
rather than an unexplained arbitrary output pointer at 82438B38. State reset
and downstream retention still need review. No global writer exclusion is
claimed from these local observations.

Headless runs completed with project saves; the existing physical-free import
decode warning at 8212FC28 remains. No gameplay source or geometry comparison
policy changed in this pass.

Retained-input follow-up (131 targeted exports): 824604C0 sets r10=0 and
tail-branches to 82460238. 824604C8 transforms r10 with XOR 0xab7638de
before entering the same function. Both wrappers were checked against generated
instructions; 82460238 is now exported independently.

82460238 retains entry r4 at reader-state+0x1c (82460288) and the positive
input count at +0x20 (8246028C). Depending on flags, it also retains the original
pointer/count at +0x14/+0x18 (824602BC/824602C0). This is an actual retained
alias, not merely a stack-local forwarding request. In the previously inspected
82445618 path, reader state is processing-object+0xe0. The normal 824604C0
entry chooses default byte callback 8252B718 at state+0x54. The image contains
4E 80 00 20 at 8252B718 (blr), matching the generated no-op return: it preserves
the input byte in r3 and has no memory stores.

82460238's inspected payload accesses are byte loads before byte-callback calls
at 82460370/82460388/824603A0/824603B8, not payload stores. It then calls
8245FC30 at 824604AC. That routine fills bit accumulators by reading bytes
through state+0x1c, advancing the pointer and reducing the remaining count.
Instruction loads at 8245FC94/8245FD08 consume the input byte; all explicit
non-stack stores in this body are reader-state-relative (+0x1c, +0x20, +0x24,
+0x28, +0x2c, +0x30). Its two indirect calls pass the loaded byte, using the
same state+0x54 callback. Thus this immediate consumption path does not write
the input payload when using the inspected default callback.

This does not close every lifetime/alias path: other readers of retained
state+0x14/+0x1c, later callback/pointer replacement, and the producer's
fragmented switch remain to be classified. No arbitrary non-default callback
is certified. The finding narrows this branch to retained read-side input in
the immediate path, not a newly discovered geometry writer. All headless runs
saved successfully; known import warnings persist. Geometry comparisons are
unchanged.

Producer coverage and reader neighborhood (142 targeted exports): repaired the
82438B38 body after checking its four raw switch entries at 82438DF8..82438E07
against generated/default/edf2017_recomp.0.cpp. Targets are 82438E2C, 82438ECC,
82438EEC and 82438F84. The export now contains all 398 instructions between
82438B38 and 8243917F excluding the 16-byte table. This closes the missing
instruction coverage item for this producer, not its transitive alias review.
The recovered copy path still targets context+0x264; it does not invalidate
the previously recorded partial-read output escape.

Added docs/geometry-bit-reader-consumers.csv for eleven nearby retained-input
users. This is a bounded neighborhood review, not an exhaustive alias search:
candidate selection scanned generated functions starting 8245F/82460 for direct
field accesses, so rebased pointers and functions elsewhere remain outside it.
Inspected all eleven exported C bodies. 82460098 adjusts counts/cursor;
8245FE78 rewinds to original input and reads into accumulator state;
8245FD78 adjusts the available extent and primes accumulators; 8245FFD0 and
8245FF80 query state without stores. 824601A8 initializes null input pointers
and the default 8252B718 byte callback. 8245FBD8 resets counters and callback,
but only clears retained input pointers when owner+0x2c0 is nonzero; do not
assume unconditional pointer retirement.

82460668 reads up to 24 bits and stores the decoded result through entry r5.
824607D8 accumulates a decoded count through entry r4. 82460958 consumes bits
without such an output. Their inspected byte-consumption loops load through
the retained pointer, update reader fields, and invoke the byte callback; no
direct payload mutation was found in these bodies. Caller-provided output
destinations still require classification. 824604D8 can refill through the
function at reader-state+0 with context reader-state+4 and a stack request,
then invoke 82445618 with reader-state+8. This callback ownership is now an
explicit frontier, not presumed equivalent to the already classified byte
callback at +0x54. The checklist keeps all eleven reviews partial.

The headless run saved successfully. These analysis changes do not remove
geometry comparisons or constitute a game build/performance result.

Decoded-output preparation inventory (148 targeted exports): added
tools/enumerate-bit-reader-outputs.ps1, producing
out/geometry-bit-reader-outputs.csv. It scans generated direct calls to 82460668
(output r5) and 824607D8 (output r4), retaining caller, call address, source line
and immediate output-register preparation. It stops at labels, branches and
calls instead of assuming a register's earlier value survives those boundaries.
The 146 unique call addresses match an independent rg direct-call count:
132 explicitly prepare a stack-relative address, seven remain unresolved at
a control-flow boundary and seven prepare from another register. These are
syntactic classifications only. A stack result can subsequently be copied to
another destination; neither downstream copies nor indirect callers are covered.

The fourteen non-stack-certified sites lie in 82460B28, 82462C38, 82463258,
824668E8, 8246AD20 and 8246C0D8; these six functions are now exported.
In 8246C0D8 the inspected decompilation forwards its own entry r3 output to
the bit reader for several states, and uses entry r7+0x34 for an intermediate
field. Its switch body remains fragmented: this is an output-alias frontier,
not certification. Generated instructions for the sole direct 824607D8 caller
at 82466C1C prepare r4=owner r31+0xc8 and r3=owner r31+0xe0. That specific
output is a field adjacent to reader state, not a stack slot; owner identity
still requires tracing. The remaining large caller bodies are not fully audited.

Refill setup was narrowed using generated 8244BCB8 instructions: at 8244C3F8
it initializes reader state at processing-object+0xe0 through 824601A8. If its
optional entry r8 configuration is non-null, it copies configuration+4 into
reader-state+0 and configuration+8 into reader-state+4. This accounts for
where the refill callback and its context enter the reader; it does not prove
the callback target or lifetime of every configuration. Unlike the fixed
byte callback, this refill callback cannot be presumed harmless from its slot.

Headless export completed and saved. Inventory count and duplicate-address
checks passed; these checks validate indexing, not alias semantics. No geometry
comparison was removed in this pass.

Manual follow-up to the fourteen unresolved output preparations: recorded
per-address evidence in docs/geometry-bit-reader-output-review.csv, keeping the
automatic inventory conservative and unchanged. Seven unresolved control-flow
cases prepare r5=SP+0x50. The branch targets 82460F20, 8246424C and 8246AE78
each have only the inspected incoming generated branch; those branches preserve
the prepared output register. The 82462DD8 arithmetic/trap path also preserves
its stack output. These observations concern generated control flow, not
arbitrary computed entry into a function's middle or downstream result copies.

Four sites in 8246C0D8 forward its entry r3, retained in r29. Its two direct
callers, 8246C3C0 at 8246C520 and 8246C7F8 at 8246CAF0, both supply SP+0x50.
Their entry r7 argument is their enclosing owner+0x204; therefore the fifth
bit-reader output in 8246C0D8 (8246C244) targets enclosing owner+0x238 on
these call paths. Both callers are added to Ghidra exports; fragmented switches
and indirect callers remain outside this local proof.

The other two non-stack destinations are 82462EF0 ([entry context+0]+0x260)
and 82466C1C (owner+0xc8, with reader at owner+0xe0). Thus the 146 direct
sites divide into 139 explicit/manually traced stack outputs, four forwarded
outputs whose two direct callers supply stack, and three owner-field outputs.
This is a destination classification, not certification that all later uses of
decoded results are non-geometry writes. Owner allocation identity and indirect
call paths remain required before excluding those fields globally.

Owner allocation follow-up (153 targeted exports): 82437DD8 calls 82444C10
with r3=r4=0 at 824382AC and stores its result at parser+0x248 (824382B4).
82444C10 allocates 0x2d0 bytes through 8245A228 at 82444C44, zeroes the
allocation, and initializes it via 82444B80. The generated 8245A228 wrapper
sets allocation flags to 0x248c8000 and tail-calls 8212F420, establishing the
already traced non-physical heap route for this fresh processing object.
82444B80 zeroes 0x2d0 bytes and initializes processing-state fields; its
82444938 prerequisite is not fully reviewed.

8244BCB8 calls 8245C420 and stores the result at processing-object+0
(8244BD80/8244BD8C). 8245C420 allocates 0x338 bytes through the same
8245A228 wrapper at 8245C434, zeroes it and initializes codec fields.
Consequently owner+0x260 is within this fresh codec allocation and processing
object+0xc8/+0x238 are within the fresh 0x2d0-byte allocation on these creation
paths. This is constructor provenance, not proof that every observed reader
caller has the same object instance or that later pointer replacement is absent.

Important narrowing of the refill frontier: 82437DD8 explicitly sets r8=0
at 82438564 before its 8244BCB8 call at 82438590. This is the optional
configuration consumed by 8244BCB8. Thus this particular parser creation
does not install a refill callback after 824601A8 initializes the reader's
callback/context to zero. Arbitrary refill callbacks are a concern for other
configuration paths or later mutations, not something this path installs.

Generated caller evidence links 824668E8's owner r4 to 82466E88's entry r3;
the latter is passed r29 by 82445B98 at 82445D34. The direct 82462C38 call
at 8244607C in that same function also receives r29. Full 82445B98 owner
provenance and the caller chain for the +0x238 field are still open. No broad
alias exclusion or comparison removal is claimed. The Ghidra run completed
with a successful project save; no game build was performed.

Processing-loop owner linkage (155 targeted exports): the parser dispatcher
824391C0 passes parser+0x248 to 8244B3B8 at 82439318. Generated 8244B3B8
retains entry r3 in r31 and passes it unchanged to 82445B98 at 8244B52C.
82445B98 retains entry r3 in r29, which supplies 82462C38 at 8244607C
and 82466E88 at 82445D34. The latter retains entry r3 in r27 and supplies
that as r4 to 824668E8 at 82466FB0. 824668E8 retains r4 in r31; its
82466C1C unary-bit output is therefore processing-object+0xc8.

For this linked path, 82462C38 loads codec=[processing-object+0] and supplies
codec+0x260 to the reader at 82462EF0. Together with the previously traced
constructors, these two destination fields lie in non-physical processing/codec
allocations on the inspected creation and dispatch path. Remaining qualifications
are later pointer replacement, other entry paths, and downstream result use;
this is not a global lifetime certificate. Updated the manual review ledger
to replace the missing direct owner-chain item with those actual boundaries.

The +0x238 output path remains indirect: raw pointer candidates for 8246C3C0
and 8246C7F8 appear at 82098C58 and 82098C70, respectively. The direct
8246C7F8 -> 8246C3C0 call at 8246CCAC forwards the same enclosing owner,
but the dispatch selecting these functions still needs classification. Raw
function-pointer presence alone is not proof of the owner passed by dispatch.
The two new exports completed and the Ghidra project saved; geometry checks
and gameplay code are unchanged.

Third-field dispatch follow-up (159 targeted exports): the raw records at
82098C58/82098C70 are only address candidates, not evidence of a vtable or
dispatch. The surrounding image alternates function starts with packed words
such as 0x40007b03 and 0x40015f03. Do not use those records to infer call
arguments. The actual selecting instruction sequence was found in 8246CD78:
it constructs 8246C7F8, stores it at codec+0x1e4, and invokes it through CTR
at 8246CE70. Ghidra independently resolves this constant-target indirect call.
It forwards its entry processing pointer and secondary argument unchanged.
8246C7F8 then directly forwards that processing pointer to 8246C3C0 at
8246CCAC. This establishes the local +0x238 owner-preserving call chain.

The preceding codec+0x1e4 dispatch is used by 82448370, 82448878 and
82448A28 (now exported). Their callbacks receive entry r4 as their first
argument, while the callback slot belongs to entry r3. Generated 82448C08
sets codec=[entry processing+0], passes codec as r3 and processing as r4,
and chooses those three routines at 82448EF0/82448EB4/82448EBC. The
8246CD78 callback is installed by the previously inspected 8244BCB8 codec
configuration path and can be restored by 8246CE80, which resets processing
state+0x204 and writes codec+0x1e4. The dispatcher therefore preserves the
processing-object identity through this local chain; how 82448C08 itself is
selected and later callback/pointer replacement remain explicit boundaries.

These new exports completed with a project save. No global writer coverage,
geometry comparison removal or game-performance improvement is claimed.

Outer dispatcher selection (161 targeted exports): 8244BCB8 passes its
processing object to 82449648 at 8244C014. That routine stores 82448C08
at processing+0x1f8 (82449678) when codec+0x118 is nonzero and codec+0x28
is zero; otherwise it selects 82447AA0. The 82445B98 branch at 82445D20
prepares r3=processing and, for codec+0xb0 != 1, branches to the indirect call
through processing+0x1f8 at 82445D7C. 82448C08 retains that processing
pointer and obtains codec=[processing+0], matching the argument split at its
three downstream dispatch calls. This closes the previously missing selection
and incoming-owner link for the inspected 82448C08 path.

Added docs/geometry-decoder-owner-chain.csv as a compact index of the linked
sites; it is documentation of inspected flow, not a machine-checked alias proof.
The three exceptional owner-field outputs now have local creation/dispatch
provenance. Updated the +0x238 review row accordingly. None of this certifies
the lifetime after subsequent pointer or callback replacement, the alternative
82447AA0 dispatch path, every codec mode or arbitrary indirect callers.

82449648 also assigns per-channel pointers at record+0x34/+0x94 using either
codec+0x1b4 plus a channel stride or an existing record+4 pointer. These are
separate downstream data destinations whose storage ownership is not established
by the three metadata-field classifications. Do not interpret the closed local
metadata chain as exclusion of all decoder writes. The Ghidra run completed
and saved. Runtime comparison policy remains unchanged.

Downstream allocation follow-up (164 targeted exports): 8244BCB8 allocates
codec.channel_count * 0x6f0 bytes through 8245A228 at 8244BF8C and stores
the fresh array at processing+4 (8244BF94). It zeroes the array, initializes
records through helpers including 8245AE18, then publishes processing+4 to
codec+0x140 at 8244C010. This establishes the channel-record array's fresh
non-physical heap provenance on this setup path.

8245A948 has a separate output allocation at 8245ADF4, published to codec+0x1b4
at 8245ADFC. It allocates channel_count * codec+0x100 * 4 bytes through
8245A228, but only when codec+0x118==0 and codec+0x1b4 is null. A non-null
pointer is retained, not replaced. The fresh constructor zeroes this field;
later reuse/configuration must not be certified solely from the allocator.
The earlier 82449648 assignment of channel+0x34/+0x94 is based on this
shared buffer when codec+0x118==0, otherwise on channel+4. That latter
per-channel pointer still requires allocation tracing.

8245A948 also allocates numerous scratch arrays, some through aligned helper
8245A248; the whole scratch graph is not certified by inspecting the shared
output allocation. 8245A320 releases and clears codec+0x1b4 and other
owned arrays, and ultimately releases the codec. Its free helpers and all
external aliases remain separate lifetime questions.

8245AE18 directly initializes channel fields but also stores through the
nested pointer at [channel+0x1a8]+8. This is a real indirect write whose
destination is not explained by the record-array allocation alone; retain
that boundary explicitly. All three new functions exported and the project
saved. No geometry checks were removed or game build produced in this pass.

Nested channel-write provenance (166 targeted exports): 82444C90 allocates
an 8-byte-per-channel array at processing+8, then a fresh 28-byte descriptor
for each channel via 8245A228 at 82444CF0, publishing it at channel+0x1a8
(82444CF8). It allocates a separate buffer sized 2*codec+0xe4+7 (guest-width
arithmetic) at 82444D24, stores it at descriptor+4 (82444D2C), and zeroes it.
At 82444D68 it sets descriptor+8=buffer+2; descriptor+0xc and +0x10 are
further interior pointers. The earlier 8245AE18 nested write through
[channel+0x1a8]+8 therefore targets this fresh non-physical buffer on the
inspected setup path. 8244BCB8 calls 82444C90 before 8245AE18, using the
same processing/channel-array arguments. Later descriptor replacement and
general size/bounds correctness are not certified by this allocation trace.

8245A898 initializes four 0x38-byte subobjects starting at channel+0xc8,
calling 8246F698 and 8246FDB8 for each. Their internal allocations remain
unreviewed; these subobjects must not be confused with channel+4, the separate
per-channel output pointer still requiring provenance. Generated 8245D058
invokes 8245A948 at 8245D0DC, after setup helpers 8245C758/8245CF70 and
before 8245AF18. Those are candidate points for tracing the remaining buffer
initialization, not yet ownership evidence.

Both new exports completed and the project saved. The nested-write allocation
question is narrowed to its subsequent lifetime, not left as an unknown initial
destination. Runtime geometry comparison policy remains unchanged.

Per-channel pointer follow-up (169 targeted exports): 82445138 loads
codec+0x14c, adds channel_index*0x70 and publishes the result at channel+4
(824451B8..824451C4). 8245A948 freshly allocates channel_count*0x70
bytes through 8245A228 and publishes codec+0x14c at 8245ABFC. The same
initialization assigns channel+8 from the separate codec+0x150 allocation.
This explains the channel+4 source used by 82449648/82445138 when
codec+0x118 is nonzero. On this initial setup path it is an owned-array slice,
not an external output-buffer argument. Later replacement, all consuming
extents and lifetime remain separate checks. Added a compact buffer-provenance
CSV to distinguish each destination and its remaining qualification.

82444880, called by 82444938 at 82444A80 with processing+4 as the channel
array, frees descriptor+4 buffers and channel+0x1a8 descriptors and clears
those owner slots. It also releases processing+8. This identifies the nested
allocation's cleanup path but does not invalidate every external alias by proof.

Discarded a misleading candidate: 8245EA18 allocates 0x98-byte records and
per-record arrays, called by 8244BCB8 with processing+0x78. It is not the
0x6f0-byte channel array and its record+4 allocation must not be used as
evidence for channel+4. The targeted run exported all three additions and
saved. No gameplay code, geometry checks or performance behavior changed.

Shared-pointer replacement candidates (171 targeted exports): separated the
three object types sharing offset 0x1b4 in
docs/geometry-decoder-shared-pointer-candidates.csv. The bounded decoder-range
direct-store scan found constructor zeroing (8245C420), conditional allocation
publication (8245A948), and cleanup clearing (8245A320) for codec+0x1b4.
82462C38 and 8246BBA0 instead write processing+0x1b4, used as a parser
state-machine value. 82463258, 8245AE18 and 8246BE40 write channel+0x1b4,
a scalar decoded/initialized value. In particular, 8246BE40 constructs its
base from codec+0x140 plus channel_index*0x6f0, then stores decoded_value+1
at 8246BF40. Those sites must not be counted as replacements of the codec's
shared-output pointer merely because the displacement matches.

This scan is not whole-program alias coverage: computed addresses, rebased
stores, copies of larger structures, functions outside the searched decoder
range and external writes are not ruled out. The initial shared buffer's
non-null reuse path remains conditional on lifetime provenance.

8246BBA0's inspected body also copies, clears and writes float values through
processing+0x1c0/+0x1d0 matrix pointers. These are distinct from the shared
output pointer and remain separate allocation/extent questions; the fragmented
switch is not fully instruction-certified. The two new exports completed and
saved. Geometry comparison policy remains unchanged.

Matrix allocation/cleanup follow-up (172 targeted exports): 8244BCB8 zeroes
processing+0x1c0/+0x1d0/+0x1d4, then for codec+0x3c>2 allocates three
channel_count squared * 4 buffers through 8245A228 at
8244C1E0/8244C1FC/8244C218. They are published at
8244C1E8/8244C204/8244C220. These initial extents match the matrix copy
and clear extents in the inspected 8246BBA0 body. This is not a proof of every
matrix consumer, integer bound or later pointer replacement.

Exported 82444938 confirms release and clearing of all three matrix slots,
in addition to the already traced nested-descriptor cleanup and codec teardown.
Generated instructions at 82444AE0/82444AF4/82444B08 call the free wrapper
for the matrix buffers. Consolidated established paths and remaining limits in
docs/geometry-decoder-branch-status.md to avoid repeatedly treating initial
allocation questions as unresolved or silently broadening partial findings.
Headless export completed and saved. No runtime comparison was removed.

Separate generic file-reader branch (178 targeted exports): 8218B030 opens
the path at 825549F4 and invokes 821FAC48 with its entry r4 destination,
length 0x1f20 and a stack-local byte-count result. Its sole generated direct
caller is 8218B2A8 at 8218B52C, which forwards entry r8 on the read state.
Two direct callers of 8218B2A8 were found: 8218BF90 at 8218C0C8 and
8218C440 at 8218C57C. Their generated instructions load r8 from owner+0x3c;
the vector-like control object begins at owner+0x38. Ghidra's inferred caller
rendering omits this sixth argument, so the register evidence is required.

Both callers have initialization paths invoking 8218BE40(vector,0x1f20,0)
(8218C064/8218C470); subsequent state-machine calls can reuse existing
storage. 8218BE40 is a resize operation, not an unconditional allocator.
It compares end-begin against the requested length, grows via 8218BBA0 or
shrinks via 8218B868. 8218BBA0 compares capacity to required size: its
growth path calls 820A3BD0(new_capacity,0), copies existing ranges, fills
inserted bytes, releases the old buffer via 820B2510 and publishes new
begin/end/capacity. Its in-capacity path writes into the retained allocation.

This narrows the file destination to an owner-held byte vector. It does not yet
classify 820A3BD0's allocation domain, every constructor/overwrite of the vector,
or downstream consumers of the bytes. The state dispatcher still has fragmented
Ghidra coverage, and copy-call signatures lose arguments in decompiled C;
use generated instruction evidence for those boundaries. Six new targets were
exported across the completed runs and the project saved. Decoder branch limits
remain recorded separately; steady geometry comparisons are unchanged.

Vector allocator/clear follow-up (183 targeted exports): Ghidra traces
820A3BD0 -> 820B24A8 -> 821E8C20. Generated instructions in
edf2017_recomp.15.cpp confirm that 821E8C20 obtains the process heap via
82132F38 and invokes 82131A48(heap,0,size) at 821E8C7C. This joins the
previously traced nonphysical process-heap allocation path, rather than the
model physical-pool allocator. Allocation-failure handlers and arbitrary
later pointer replacement are not certified by this observation.

Correction to the earlier loose description of 8218B8F8 as cleanup: it is
vector clear, not allocation release. It passes begin/end packed iterators
to 8218B868, which erases a range and updates vector+8 (end). For the
whole-vector clear, the trailing copy length is zero and end becomes begin;
begin and capacity remain intact. General partial erase can copy a trailing
range via 821E8230 at 8218B8E0. Retained storage must therefore remain in
the lifetime model after this operation. Other caller operations may still
release storage; this finding concerns 8218B8F8 itself.

820B2510, used when growth replaces old storage, decrements the allocation
counter and tail-calls 821E8CE8 for non-null pointers; that path frees through
82132330 on the process heap. Its null case instead reaches a diagnostic
callback, not a payload writer. Five new Ghidra targets exported successfully
and the project saved. The known incomplete physical-free import export
(8212FC28 -> 8252CF3C) remains; export success is not full coverage proof.
No runtime checks were removed and no new game build was produced.

Checked-move boundary follow-up (184 targeted exports): 821E8230 is
(destination, capacity, source, bytes) in r3/r4/r5/r6. Zero bytes return success
without a payload access. Nonzero copies require non-null destination/source
and capacity >= bytes; 821E82CC/821E82D0 move bytes/source into r5/r4 and
821E82D4 calls 821EA320. Invalid arguments write error state through
821EFC18 and call 821EFAE0; those error-handler side effects are not classified
here. No independent inline payload copy exists in this wrapper's inspected body.

Added this signature explicitly to GeometryWriterTrace.java using the low
32-bit argument registers, cross-checked against generated shard 47. Re-export
now preserves all four call operands at 8218B8E0 and all five move sites in
8218BBA0. The successful payload path reaches the existing native move hook,
whose backward copy notifies directly and forward path uses the hooked
821E8320; another wrapper notification would duplicate existing coverage.
This establishes a boundary route, not caller aliasing or concurrency safety.

Added the wrapper to the reproducible known-writer caller index: 22 generated
direct sites, independently matched by rg; total direct/indirect site counts
remain 75,918/5,222. Ghidra completed and saved, with the known incomplete
physical-free import warning still present. Native cache inspection confirms
live source comparisons remain necessary for unnotified raw writes. No runtime
policy was changed, and no build or performance claim follows from this audit.

Checked-move caller families (196 targeted exports): inspected five callers
near the model routines without assuming address proximity establishes object
identity. 821B7948 inserts/fills 2-byte vector elements; 821BA368 does the
same for 4-byte elements. Their payloads are not copies of entire model
resource headers on these inspected paths. Each has four inline fill store
sites, recorded separately in docs/geometry-vector-fill-writers.csv; tracking
their checked-copy callees alone does not track these inline fills.

Their fresh allocations route through 820A0420(count*2) and
821B8DD0(count*4), respectively, both calling the previously traced
820B24A8 process-heap allocator. Each can instead reuse existing capacity;
fresh allocation provenance does not establish the history of retained pointers
or caller-supplied insertion iterators. 821B7000 and 821B98D8 erase ranges
of 2-byte and 4-byte vectors via the checked move. 821B6FB0 and 820BBB78
move the respective element ranges to end-relative destinations.

The inspected direct caller chain includes resize 821B7E88 -> 821B7948 /
821B7000, resize 821BAFD0 -> 821BA368 / 821B98D8, clear 821BAE98 ->
821B98D8, and one-element insert 821C88A0 -> 821BA368. Ghidra omits the
last fill-pointer argument in 821C88A0's rendering; instructions preserve
entry r6 through the call at 821C891C, so this is not a zero-argument fill.
Higher owners of those vectors remain to trace; these are candidate writers,
not eight confirmed model-payload writers. Twelve new exports completed and
the project saved. Runtime comparisons remain unchanged.

Vector owner follow-up (202 targeted exports): the sole generated direct
caller of 821B7E88 is 821B8090. At 821B80B4/BC/C0 it zeroes the
begin/end/capacity fields of a stack-local vector at SP+0x50, then calls
resize(length+1,0) at 821B80C4. Thus the normal direct path into 821B7948
starts empty rather than inheriting a caller's buffer. 821D9430 consumes
bytes and emits 16-bit mapped characters plus a terminator into that allocation;
821B8090 then constructs its output string and frees the temporary buffer.
This agrees with analysis note script/0225's string-conversion identification.
The four fill-site rows now distinguish this established direct path from
unproven alternative entries; no claim of global indirect-call exclusion follows.

The 4-byte family has two distinct uses. 821BB7A8 grows packed-bit storage
through 821BAFD0 with ceil(bit_count/32) elements and a zero fill. Correction:
821BAE98 is not merely clear; it shrinks backing words as needed, publishes
bit count, and masks the last word at indexed store 821BAFC4 when bit count
is not a multiple of 32. 821BB668 uses it after bit-range movement.

821C8A20 is generic 4-byte push-back with an additional inline payload store
at 821C8A7C when capacity is available. One of its eight distinct direct
callers is DXM node initialization 821D79D8: at 821D7A04 it saves incoming
node pointer r5 to SP+0xe4 and passes that slot as the value at 821D7B34,
with incoming r4 vector as destination at 821D7B3C, then appends at
821D7B74. Loader 821D88C0 initializes that vector empty at SP+0x50 and
passes it at 821D8A2C; recursion preserves it. This copies a node pointer
value into a loader vector, not node contents or a geometry header. Other
push-back callers and subsequent use of the node list remain separate.
Cross-checked model context against analysis note assets/0329 and instruction
exports. Two additional store candidates join the CSV. Six new targets
exported and saved; known physical-free import warning remains. No runtime
comparison was removed and no game build was produced.

Append caller classification (210 targeted exports): all eight generated
direct callers of 821C8A20 are now listed in docs/geometry-append-callers.csv.
Seven lead to vectors initialized empty on the stack (including the previously
traced DXM loader). Initialization was checked against instruction stores,
not inferred from stack location alone. Loop appends can reuse capacity, but
their initial backing allocation follows the already traced CPU-heap path.
The appended values are pointer values; a pointer-valued element does not mean
the append writes the pointee. Later callees using those lists remain separate.

The eighth path, 821C8AD0, searches the vector loaded from owner+4 and
appends entry r4 only when absent. Its sole generated direct caller is
821C8B90, which uses that result to avoid converting the same address twice:
on a first visit it byte-swaps nonzero 16-bit units in-place at 821C8BE0,
advancing the supplied pointer until a zero unit. This is another payload
writer, distinct from the visited-address vector. Its 21 direct call sites
in 16 functions still need destination/owner classification; neither the
visited-vector allocation nor the converted input is globally certified.

The seven initially-empty-vector paths narrow the append-store provenance;
they do not prove whole-function memory safety, indirect reachability, or all
subsequent aliases. Eight new exports completed and the project saved; known
physical-free import warning remains. No runtime comparison policy changed.

Byte-swap direct frontier (226 targeted exports): all 21 emitted direct calls
to 821C8B90 in 16 functions were matched to Ghidra instruction addresses and
listed in docs/geometry-byte-swap-callers.csv. Each inspected call supplies
a destination constructed from a record-relative offset, including offsets
just converted in-place. SGO 821D5120 iterates name-index entries; DXB
821D9F38 processes record strings, consistent with analysis asset note 0390.
The other rows deliberately describe pointer construction rather than invent
asset types from neighboring addresses. No allocation-bound proof is implied.

The calls use seven visited-tracker objects: 8257C334, 8257C33C, 8257C344,
8257C34C, 8257C354, 8257867C and 82578684. 821C8AD0 loads the actual
visited vector through tracker+4; these constants are tracker addresses, not
payload bases. Their construction/reset/lifetime and the input record owners
are now the bounded remaining questions for this branch. The observed record
conversion pattern does not establish that all writes occur before native
publication or that malformed offsets cannot escape their input allocation.
Sixteen new targeted exports completed and saved, retaining the known
physical-free import warning. Geometry source comparisons remain enabled.

Visited-tracker lifetime follow-up (237 targeted exports): 821C8980 releases
the previous reference-counted owner, clears both tracker words, allocates a
new 16-byte vector control through 820B24A8, and zeroes its begin/end/capacity.
821C8808 publishes that control at tracker+4 and allocates a separate 4-byte
reference count at tracker+0. 821C87A0 decrements the count and clears both
tracker words; on final release 820B7AA8 frees vector backing storage and the
control object, followed by reference-count release. Allocation-failure behavior
is not certified (the original code can dereference a failed count allocation).

All seven generated conversion-entry reset/release pairs were matched to the
seven tracker constants in docs/geometry-swap-tracker-lifetimes.csv. Normal
conversion starts with a fresh empty CPU-heap vector, not a retained allocation
from a preceding asset. This resolves the initial visited-list allocation
question left by the eighth append caller. It does not establish synchronization
of global trackers under reentrant/concurrent conversions or rule out arbitrary
later tracker overwrite. Nor does it classify the records being byte-swapped.

8219EBD0 calls SGO conversion 821D5120 on its caller-supplied input, then
publishes that same pointer to its output owner only when the magic matches.
This establishes conversion-before-that-publication, not before every native
geometry publication; the input allocation remains a caller question. Eleven
new exports completed and saved, with the known physical-free import warning
unchanged. No runtime comparisons were removed and no game build was produced.

DXM conversion/publication ordering (241 targeted exports): 821D88C0 calls
821DB008(input,0) at 821D88EC before material/node initialization and before
the mesh-construction call 821B3C98 at 821D8AB4. That constructor invokes
the previously traced VB/IB constructors, which allocate separate model-pool
payloads, copy the file data, then publish native ownership. Thus this normal
conversion path precedes those new native geometry lifetimes; it is not itself
evidence of a steady per-draw writer to the published payloads.

821CB550 invokes model initialization only on its cache-miss branch at
821CB5C4; the inspected cache-hit branch returns through 821CB480 without
821D88C0. Input is descriptor[0]. 821CB6A0 builds that descriptor on the
stack from entry r6 and a name string, then calls 821CB550 with model cache
8257C314. Its sole generated direct caller is 821AB708 (call at 821ABC10),
confirmed in the generated call index; do not infer input allocation ownership
from the descriptor being local. The input allocation
remains upstream. 821C9BD0 cleans up only descriptor+4's name string; it
does not free descriptor[0]'s file buffer.

821DA8A0 is a fixed seven-word in-place byte-order conversion (offsets
0/4/8/c/10/14/18), used by the inspected node/mesh record converters, not a
geometry allocator. Its effects belong to the same input-record ownership
question. Correction to the tracker-pair description: 821DB008's invalid
version branch at 821DB0AC jumps to 821DB284, bypassing release at
821DB280. Reset/release pairs describe normal conversion, not every error
path. The next reset releases the previous tracker reference, but reentrancy
and allocation failure remain unproven. Four exports completed and saved;
runtime comparison policy remains unchanged.

Retained-reference follow-up (283 targeted seeds): renderer table 82064C78
slot +0x38 resolves to 8242D048, which retains its incoming object, releases
the old reference, then publishes renderer+0x2c at 8242D090. The coordinator
constructor 82431840 installs table 82064EE0. Its setup slots +0x24/+0x34
resolve to 82431150/82431390, and the latter stores the renderer at
coordinator+0x3c (8243144C). This is the opposite ownership direction; it
does not establish the renderer's cleanup-reference type. Recorded the
argument chain, shared release helper 82433978's actual count-one branch,
and coordinator destructor wrapper in geometry-small-buffer-callbacks.md.
Six new exports saved with exit zero; remaining setter callers and nested
release effects stay open. No runtime checks were removed.

DXM input provenance follow-up (249 targeted exports): 821AB708 obtains a
DXB archive object at SP+0x100, initialized by 821D63D0 and loaded through
821D6448. The latter calls 821A0EB0 on archive+0x10, converts the loaded
DXB header through 821DA0B8, and creates an 8-byte-entry index of pointers
to the archive's 32-byte member records. 821D6AE0 selects records by name,
constructs 64-byte descriptors via 821D5CB0, and appends them via 821D6A08.

821D5CB0 copies two names but does not extract/copy/decompress the member
payload: at 821D5D50 it publishes descriptor+0x38 = record + record[0x14],
and at 821D5D40 descriptor+0x3c = record[0x10]. It also marks the index
entry used. 821ABBF4 loads descriptor+0x38 into r6, passed to 821CB6A0
at 821ABC10 and onward to the model loader. This identifies the DXM input
as a member-data alias of the loaded archive, not a model-pool pointer from
a resource header. Descriptor vector insertion/copy and input bounds remain
separate verification questions; the initial descriptor construction is proven.

821A0EB0 starts with a zeroed local three-word buffer, calls the previously
exported file reader 821A0550, then invokes 8219F9E0 to transfer/assign it
to the archive buffer object. 821A0550 invokes 8219F940 before reading into
buffer[0]+progress. Those two buffer helpers remain the next allocation and
transfer boundaries to inspect. 821D5B50 releases archive+0x10 via 820B25B8
and the record index via 820B2510; member descriptors are not by themselves
owners of member payload storage. Eight targets exported and saved. No source
comparison removal or new runtime build follows from this partial chain.

Archive allocation branches (258 targeted exports): 820B2550 calls
821E8C20 at 820B2578, joining the traced process-heap path. 8219F940
allocates new storage, copies min(old size,new capacity) inline at 8219F998,
then releases old storage and publishes the new buffer. 820B25B8's non-null
release path reaches process-heap free 821E8CE8.

Correction to the provisional transfer description: 8219F9E0 dispatches on
SGSL detection. Uncompressed input calls 82125D28, which allocates through
82125CA8/820B2550 and copies bytes at 82125D6C without clearing the source
owner. Compressed input obtains decoded size, allocates via 8219F940, then
calls 821D5548; its two output stores are 821D55C8 and 821D5628, with
other stores confined to its stack dictionary on the inspected body.

The normal DXM input chain now reaches a separate CPU-heap archive allocation
for either input branch. Consolidated established source ownership, construction
ordering and remaining bounds/alias limits in geometry-dxm-source-ownership.md.
The SGSL decoder has no explicit output-capacity guard, and its caller still
invokes it after a failed destination allocation; these are not closed safety
paths. Nine new targets exported and saved, retaining the known physical-free
import warning. No runtime comparison was removed or game build produced.

Descriptor propagation follow-up (268 targeted seeds): normal append via
821D6A08 now has both its spare-capacity and reallocation paths inspected.
821D6328 copies descriptor pointer/size at 821D63A4/63AC. Growth through
821D6928 -> 821D6658 first copies the incoming descriptor via 821D5BC0
(stores 821D5C20/5C28), allocates a 64-byte-entry array through 821D5A40,
copies ranges via 821D6148 (stores 821D61C4/61CC), and fills the inserted
entry via 821D65F0 -> 821D6328. All these preserve offsets 0x38/0x3c;
none copies payload through the member pointer. 821AA130 frees descriptor
names only, followed by separate old descriptor-array release.

The generic assignment fill 821D60D0 likewise preserves pointer/size at
821D6124/612C. The generic insertion backward-copy wrapper 821D63F0
calls 821D5C38, whose implementation is not yet reviewed; do not extend the
normal append conclusion to every insertion path. Updated the consolidated
DXM ownership note with this distinction. Ten additional targets exported;
headless run exited zero and saved, with the existing physical-free import
warning still present. No runtime checks removed, build, or performance claim.

Backward descriptor assignment (269 targeted seeds): 821D5C38 exported and
instruction-checked against generated/default/edf2017_recomp.24.cpp. The
loop walks source/destination backward in 64-byte steps and copies pointer/size
at 821D5C94/5C9C after the two name assignments. It does not store through
the member pointer. This closes the previously unreviewed helper body, not
arbitrary aliases or every caller's bounds. The Ghidra process exited zero
and saved; the known physical-free import warning remains.

Returned to the small-buffer callback frontier: 8242D2B0 publishes newly
created IB/VB handles at object+0x3c/+0x40, then calls object vtable slot
0x60 at 8242D420. Its direct caller 8242DA08 supplies the device at
object+0x38 before calling it at 8242DA48. This establishes the handle
construction order, but does not resolve the final callback target.
Raw candidate locations 82097BF8/7C00/7C08 contain 8242D138/8242D2B0/
8242D440 respectively, alternating with words 40005D05/40006305/40003105.
These are not evidence of a contiguous callable vtable. In particular, do
not assign slot 0x60 to 8242D440 by subtracting 0x60 from its raw candidate
location. The next required evidence is the actual object vptr construction
or a runtime-resolved target with its object lifetime. No runtime changes.

Small-buffer vptr follow-up (277 targeted seeds): the actual constructor
8242E7E0 installs table 82064C78 at 8242E804. Its slots +0x3c/+0x58/+0x60
resolve to 8242D0B0/8242E678/8242EF88. The last is the buffer-creation
callback for this installed table, not 8242D440. Its three non-stack stores
publish handles returned from calls taking fixed image addresses, with no
direct VB/IB payload access. Cleanup +0x58 resets object fields and inline
source data; +0x3c introduces another virtual call on referenced object+0x2c.
Recorded constructor/call-site evidence and remaining limits in
geometry-small-buffer-callbacks.md. Eight new exports saved successfully;
runtime comparison policy remains unchanged.
