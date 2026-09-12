# SGO conversion input ownership

September 11 generated-source follow-up. All 13 emitted direct calls to
8219EBD0 are indexed in geometry-sgo-input-callers.csv. Entries describing a
cache-like branch classify the inspected call setup, not every predecessor or
the complete caller. This is not a geometry immutability certificate.

Complete 8219EBD0 preserves entry r4, passes it to 821D5120, checks the converted
magic, and publishes that same address through entry r3 on success. It neither
allocates nor copies the input. Its sole emitted direct call to 821D5120 is at
8219EBF0. Conversion-before-publication does not make the input newly owned.

## File-loaded inputs

Three inspected setups pass the pointer produced by 821A0EB0 into the wrapper:

| Caller | Loader call PC | Output buffer control | Conversion call PC |
| --- | --- | --- | --- |
| 820A3488 | 820A34D8 | entry r3 | 820A3508 |
| 82199F28 | 82199F74 | entry r3 + 4 | 82199FB0 |
| 821E50C0 | 821E5110 | entry r3 | 821E514C |

Complete 821A0EB0 uses an initially empty temporary file buffer, calls 821A0550,
and on a successful read calls 8219F9E0 with the caller's destination control.
It then releases the temporary raw file buffer. The allocation/copy/decode
chains in geometry-dxm-source-ownership.md also apply here: normal successful
preparation produces CPU-heap-backed storage, not a model physical-pool alias.
These three call setups reload input from that destination control before SGO
conversion. No intervening payload-pointer substitution occurs on those paths.

Failure qualification matters. 82199F28 and 821E50C0 branch away from conversion
when the file loader returns false; 820A3488 does not test that result. Moreover,
821A0EB0 returns true after calling preparation without checking its result.
Therefore even a checked loader return is not proof of successful allocation or
decode. The CPU-heap classification assumes successful preparation, not merely
a true return. Decoder bounds and failed allocation behavior remain as documented
in the DXM note. Nested SGO offsets have not been certified within that allocation.

## Shared cache inputs

Complete 820AB608 returns zero for an empty handle; otherwise it checks the
handle/node relationship and returns node+44. It does not allocate or copy the
payload. 820AFAC0 passes this result to conversion. Six other inspected call
setups load node+44 directly through similar handle checks (see CSV). Their
shared payload allocation and possible later aliases are the next ownership
frontier; classifying the handle itself as local would not classify its payload.

The other inputs are an owner field (821C2090), an entry argument (821A6278),
and a preceding load path not yet fully traced (820D1F50). Indirect calls,
reentrant conversion, malformed records and arbitrary overwrite remain open.
No runtime comparison or rendering behavior changed in this investigation.

## Cache miss and node-construction follow-up

Complete 821B24C0 normalizes the filename through 821B2850, uses the fixed cache
at 8257C0B4 through 821B23D0, then copies the returned handle through 820AC260.
Complete 821B23D0 looks up the key with 820AB498. On a miss it calls 821B20F8
at 821B242C and passes the returned buffer-control address directly to the file
loader 821A0EB0 at 821B243C. A false loader return erases the entry through
821B21F0 and produces an empty handle. A hit skips loading. As above, successful
preparation rather than the loader return alone is the allocation qualification.

Complete 821B20F8 returns node+44. When inserting, it constructs a temporary
key/value with an initially empty byte buffer and calls 821B1D38. Complete
821B1D38 dispatches insertion to 821B17A0 (or the general 821B1B90 fallback).
The allocation portion of 821B17A0 allocates 64 bytes through 820B24A8 at
821B1838 and constructs the node through 821B1728 at 821B185C. The entire
tree-balancing body of 821B17A0 was not reviewed for alias certification here.

Complete 821B1728 initializes node+44/+48/+52 to zero, then calls the previously
traced deep byte-copy helper 82125D28 on value+32. It copies the extra word to
node+56 separately. Thus this constructor does not import the incoming byte
buffer pointer as borrowed backing; its normal successful copy is CPU-heap
storage. On the empty temporary used by the miss path, the file loader fills
that buffer control after insertion. Generic insertion can receive nonempty
values, but this constructor still deep-copies them.

Complete 821B2300 looks up the node and acquires a handle, updating node+56.
Complete 820AB538 and 820AC260 copy the three-word handle and update that same
node counter; they do not substitute node+44 or copy its payload. 820AC260 first
releases its previous handle through 820A7E50, whose effects were not re-reviewed
here. This connects 820AFAC0's normal cache-miss input to the file-preparation
ownership chain and explains why handles subsequently share one payload.

Direct-edge search found one caller each of 821B23D0 (821B24C0), 821B20F8
(821B23D0), and 821B1D38 (821B20F8). This does not enumerate every cache field
writer or certify the generic insertion fallback, cache hit history, destruction,
reentrancy, or all six other cache-like SGO caller setups. Those remain explicit
limits rather than being inferred safe from this miss-path trace.

## Generic insertion and handle release

Follow-up completed 821B1B90: it searches using the supplied key, then either
calls 821B17A0 at 821B1C40/821B1C9C with the same value argument, or returns
an existing iterator with the inserted flag false. It has no alternate payload
allocation or assignment route. This closes the generic insertion dispatch
question left above, without certifying arbitrary input node pointers.

The remainder of 821B17A0 was reviewed after its allocation/constructor portion.
After construction its direct stores affect tree links (node+0/+4/+8), color
(node+60), container count/root links, and the returned iterator. They do not
replace node+44. Complete rotation helpers 820A52E8 and 8219C178 likewise write
only links at node+0/+4/+8. This supersedes the earlier incomplete-body note
for insertion, under the normal valid-tree assumption. Allocation failure is
still not certified: the code continues after a null allocation.

Complete 820A7E50 decrements node+56 for a live handle. When the count becomes
zero, it logs the key and invokes erase 820A74A8 at 820A7F1C, then clears the
handle's third word. Non-final release does not replace or copy the payload.
The inspected erase tail (820A78A0 onward) frees node+44 through 820B25B8 at
820A78B0, clears its buffer-control words, releases the key string if allocated,
and frees the node through 820B2510 at 820A78E8. The preceding erase/tree-repair
body was not fully reviewed here, so this is a release-tail trace rather than
a complete destruction/alias certificate.

These paths establish deep-copy insertion and reference-counted sharing rather
than a borrowed-pointer insertion mechanism. Full cache-hit history still needs
other mutation paths and caller mapping; zero references do not by themselves
prove that no external raw pointer escaped. Concurrent/reentrant access and SGO
nested-offset bounds remain open. No runtime checks were removed.

## Remaining cache caller mapping and complete erase body

The six additional cache-like callers now map to the same 821B24C0 loader:

| Caller | Loader call PC | Populated handle used by conversion |
| --- | --- | --- |
| 821256A8 | 82125708 | local SP+128 |
| 820E1B60 | 820E1BD0 | r31 |
| 821A6E08 | 821A6E34 | local SP+88 |
| 820F1870 | 820F1B38 | r31 |
| 820ADF60 | 820AE00C | local SP+96 |
| 8212A208 | 8212A348 | r26 |

Inspected loader argument setup and the full emitted interval from each loader
call through SGO conversion. The nonempty-handle branches read node+44 from
the same handle. This is a direct caller-to-provider connection, not an inference
from similar field offsets. The CPU-heap preparation qualification now applies
to these normal paths too. Four callers test loader failure and branch away;
820F1870 and 8212A208 proceed to handle inspection without testing its return.
Empty-handle branches use zero or a saved register instead of node+44 and are
not included in the nonempty-path ownership claim. Some intervals call logging,
string release or output initialization; arbitrary reentrancy is not certified.

Complete 820A74A8 has now been inspected, superseding the partial-erase note.
It retains the original node in r26, advances the returned iterator through
820A3938, and handles the two-child case by moving tree links rather than copying
payload fields between nodes. Direct repair stores affect links and colors;
the original r26 node reaches the previously traced payload/key/node free tail.
Rotation callees are the previously inspected 820A52E8 and 8219C178. There is
no direct payload transfer into a surviving node in the erase body. The iterator
advance helper and invalid-tree exception helpers are not certified here.

This closes the six normal provider mappings and the erase-body payload-transfer
question. It does not establish all writers to cached payloads, external escaped
pointers, valid SGO offsets, concurrency or arbitrary indirect entry. No runtime
comparison policy changed.

## Last three wrapper-input routes

820D1F50 initializes buffer control at entry owner+728/+732/+736 to zero and
calls 821A0EB0 at 820D2084 with that control. Conversion at 820D20BC reads
owner+728 and publishes through owner+740. This adds a fourth direct file-loader
route with CPU-heap storage on successful preparation. Unlike the two checked
loader callers above, a false result calls the error logger and then falls
through to conversion if the logger returns. Failure is not certified safe.

821A6278 has one emitted direct caller, 820B4D58 at 820B51A0. The inspected
setup derives r29 from a record returned by 820B6FA8: payload = record +
word(record+4), then passes r29 in r5. The record lookup takes the relative
field at entry = map + word(map+20) + iteration_offset; its argument is
entry + word(entry+24). Here map is loaded from owner+124 in 820B4D58.
Thus this is a nested map-relative payload, not an unexplained arbitrary
argument, but map allocation, helper return semantics and offset bounds still
require tracing. No ownership certificate follows from relative addressing.

821C2090 obtains its r27 context through global 8257C310, then uses context+68
as the optional SGO input. It separately copies a handle from context+72 when
present. The input cannot be identified as node+44 solely because a handle is
nearby: their relationship needs the context publisher. Searches found multiple
stores to the global around construction calls. One inspected setup publishes
its entry r5 after writing transform/owner fields, allocates the new object,
then invokes its constructor. Publisher enumeration and the source of context+68
remain open. The current ledger now distinguishes this construction-context
route from an unspecified owner field.

These are upstream provenance findings only. No guest writer hook, geometry
comparison policy or executable changed in this follow-up.

## Map-relative input allocation follow-up

820B4D58 calls 821B24C0 at 820B4EE0 with a handle at entry owner+112.
It checks the return and branches away on failure. The nonempty-handle path
loads node+44 into r23, converts through 820B77A0 at 820B4FAC, checks map magic
and version, and publishes the same r23 at owner+124. The later record loop
therefore derives its map from the already-traced file-cache storage, not from
a newly identified physical geometry allocation. This normal path assumes
successful file preparation and valid map records, as do the other cache traces.

Complete 820B6FA8 searches count=word(map+8) entries of 12 bytes starting at
map+word(map+12). Each entry's name is entry+word(entry+0); the comparison
helper is 821E9510. On match it returns the entry address itself; on no match
it returns zero. It has no payload allocation or direct payload store. The
820B4D58 caller computes entry+word(entry+4) and passes that address through
821A6278 to SGO conversion. There is no intervening null-result check before
the word(entry+4) load. The record and payload offsets are not checked against
the cache allocation extent here, so this is not an out-of-bounds safety proof.

The preceding 820ADD28 call is separate from that payload read. Complete
820ADD28 delegates to 820ADC18; complete 820ADC18 finds or retains a cache
handle in its manager's map at manager+24. On a miss it calls 821B24C0 at
820ADCB8 and copies the resulting handle through 820AC260. It does not return
an alternate payload to the record loop. Its manager-map insertion helper was
not fully audited in this follow-up.

The formerly raw SGO argument now has a normal file-cache allocation chain.
Remaining qualifications include conversion/record bounds, no-match handling,
cache-hit mutation history, escaped aliases and concurrency. No runtime
comparison was removed and no build was required for this source investigation.

## Construction-context payload publishers

Complete 821A6278 writes its retained entry-r5 SGO pointer to context+68,
publishes that context through 8257C310, and invokes the selected factory through
an indirect call (LR821A63C0). On normal completion it clears the global. This
connects that publisher to the map-relative cache payload traced above. The
factory target and any nested construction still require their own reachability
and lifetime analysis; publishing the pointer is not a copy or ownership transfer.

The inspected publication interval in 821A6E08 calls 820AB608 on its local
SP+88 cache handle, stores the returned payload at context+68, stores the address
of that handle at context+72, publishes the context, then invokes the selected
factory (LR821A703C). It clears the global and releases the local handle on the
normal completion path. This establishes the payload/handle relationship that
could not previously be inferred from their adjacent offsets. The loader for
this handle was already traced to 821B24C0; arbitrary reentrancy remains open.

The inspected publication interval in 821A5EF0 explicitly zeros context+68
before invoking its factory (LR821A5FA8). Absent intervening changes, 821C2090's
nonnull-input branch therefore does not convert SGO for that publisher.

An emitted-store search for the -15600 displacement also finds multiple typed
factory wrappers publishing their caller-supplied context (usually entry r5),
plus 821E49D0 publishing a retained context pointer. Those paths do not establish
the provenance of context+68 by themselves. The displacement search is a
candidate enumeration, not exhaustive global-address/alias analysis. Normal
global clearing also does not establish exception safety or concurrent/nested
construction safety. No comparison-removal policy changed.

## Typed factory counterexample to a single-publisher assumption

Searching direct calls to the typed context-publishing wrappers finds additional
callers, not just the three generic publishers above. They cannot be collapsed
into the generic publisher trace without inspecting their argument setup.

One complete additional route is 821E4B30 -> 821E49D0 at call PC821E4BF0.
Complete 821E4B30 supplies a local context at SP+144 and explicitly zeroes
SP+212 (context+68) and SP+216 (context+72). There are no intervening calls
before the factory call. Complete 821E49D0 retains entry r5, writes context+64,
copies exactly 64 transform bytes to context+0, publishes it through 8257C310,
and invokes constructor 821E5658 after allocating 432 bytes. Neither its own
stores nor the 64-byte transform copy change context+68/+72 before construction.
Thus this normal direct route supplies no SGO payload or borrowed cache handle.

821E49D0 has only the one emitted direct caller above. This closes its direct
input initialization question; it does not certify every nested constructor,
indirect factory entry, allocation failure or concurrent replacement of the
global context. Other typed wrapper callers remain to be classified. No runtime
comparison policy or executable changed.

## Reproducible typed-factory caller index

Run tools/enumerate-construction-context-callers.ps1 to generate
out/geometry-writer-inventory/construction-context-callers.csv. Current source
matches 86 direct call sites across 26 typed context publishers. The six largest
groups are 820E4260 (18), 820E5760 (13), 820FC458 (9), 820E7CA0 (5),
8210EBD8 (5), and 820F9930 (4). This gives a concrete classification queue,
not 86 proven geometry writers. Each row starts unclassified.

The index discovers the reviewed r30-base/global-displacement publication form,
records caller/call-PC/factory, and captures the last textual r5 assignment.
That assignment is only a navigation aid: intervening branches, callbacks and
register clobbers are not resolved. Duplicate function or call identities and
missing adjacent return addresses cause failure. Other global store forms and
indirect calls are outside this index. The manually inspected 821E4B30 context
at SP+144 and 820B26D0 call at 820B2FCC with SP+80 matched the generated rows.

The initial shared-initializer hypothesis was not established: the inspected
820B26D0 caller initializes context fields inline. Complete 820B29F8 is a string
cleanup at context+84, not a common SGO/context initializer. Do not classify
callers from its presence. The index is intended to support grouped source
review of actual context+68/+72 writes rather than repeated blind searching.

## Largest typed group: shared embedded initializer found

The 18 direct calls to factory 820E4260 divide into 17 call sites whose caller
bodies invoke 820E4D20, and one object-resident context path in 820F9FB0.
This is a candidate grouping, not yet 17 complete reaching-definition proofs.
Complete 820E4D20 first calls base initializer 820E4758, then constructs an
embedded context at argument+176. After the base call it explicitly zeros that
context's +64/+68/+72 and sets its +76 byte to one. No further call occurs
before return. The earlier failure to find one universal initializer does not
exclude this shared initializer for a particular resource class.

Complete caller 82195B80 initializes at SP+192 (call PC82195BCC), then supplies
SP+368 to factory 820E4260 at 82195D54. The 176-byte relationship matches the
embedded context. Its direct scalar setup does not populate context+68/+72;
intervening member helper effects and aliases remain a separate qualification.
The other 16 call sites need their initializer arguments and intervening writes
checked before being classified as null-SGO paths.

Complete 820E4260 preserves input context+68/+72 in its own body: it writes
manager at +64, copies 64 transform bytes to +0, publishes the context and
constructs 8211D4D0. Its normal factory setup does not itself introduce an SGO
payload pointer. Nested construction and invalid allocation remain outside this
claim.

Complete 820F9FB0 demonstrates the lexical ledger's limitation. At 820FA08C
the normal branch passes entry owner+1904, while a null-subobject branch passes
r30=0. The last textual r5 assignment is the null fallback, not the normal
argument. Earlier in the same function 820F9930 receives owner+1648 on its
normal branch. These contexts require the owning object's initialization trace;
they are not stack-local default contexts. Do not use the ledger's assignment
column as a path-independent value or ownership certificate.

This grouping narrows the next work to embedded-context initialization and
member updates, rather than 18 unrelated payload allocators. No comparison
policy or executable changed.

## Effect member-copy boundary

Complete 820E4758 calls 8210AB08 at argument+8 before initializing scalar
parameters. Complete 8210AB08 zeros four words at its argument+0/+4/+8/+12.
For the inspected 82195B80 local object at SP+192, this zeros SP+200..215,
including the handle whose copy destination is SP+204. These fields precede
the construction context at SP+368; context SGO/handle fields are SP+436/+440.

Complete 820E61A8 first releases the destination through 820E60B8, copies
three words at destination+0/+4/+8, and for a nonempty source increments the
referenced node's counter at node+48. It neither copies a whole parameter
object nor directly writes the construction context. Complete 820E60B8 tests
destination+8 first and returns without release calls when zero. Thus the
initialized-empty destination on this path avoids the old-node destruction
branch, assuming no intervening alias overwrite. A nonempty destination can
instead decrement a node counter and call 820E5C60 when it reaches zero;
that general release branch is not classified by the empty-path reasoning.

The source-node increment is an external write, not a stack-only operation.
Source handle validity/aliases still matter. This narrows the helper effects
between context initialization and factory submission without claiming all
intervening callbacks or all 17 caller paths safe. The helper's node+48 counter
is also distinct from the file-cache node+56 counter traced earlier; similarly
shaped handles must not be assumed to share a container type.

## Resident contexts: constructor and reference-list boundary

Read complete generated bodies of 820FA600, 820D27A0, 820FA358,
82109580, 821094B8, 820D2658, 82125B30, 821A1628 and 821A1678.
820FA600 constructs the two members matching 820F9FB0's normal arguments:

| Constructor call PC | Member offset | Embedded context | SGO / handle fields |
| --- | --- | --- | --- |
| 820FA6A8 -> 820D27A0 | owner+1424 | member+224 = owner+1648 | owner+1716 / +1720 |
| 820FA6B0 -> 820E4D20 | owner+1728 | member+176 = owner+1904 | owner+1972 / +1976 |

820D27A0 calls its base initializer first, then initializes the embedded matrix,
zeros context+64/+68/+72 and sets byte+76 to one, with no subsequent call.
The previously reviewed 820E4D20 does the equivalent at member+176. Thus both
resident contexts have explicit null SGO/handle initialization. This establishes
the matching layout, not a complete constructor-to-factory reaching definition.

820FA600's suffix calls 82109580 at 820FA8A4 with destination owner+1436
and source word(saved construction context+84), then 820FA358 at 820FA8B0.
82109580 is not a whole-member/context copy: 821094B8 first releases the
destination array via 820D2658, allocates 12*count+4 bytes via 820B2550,
and stores the new array pointer/capacity/count in destination+0/+4/+8.
Its copy loop links individual 12-byte reference records and writes record+8.
Normal successful allocation therefore places those records in separate heap
storage, not over the embedded context. Count overflow/invalid pointers and
arbitrary linked-list aliases are not certified by this normal-path finding.

Complete 820FA358 writes parameter fields before the embedded contexts. Its
resource-handle copy at 820FA4C4 targets owner+1740 through 820E61A8; it does
not directly populate owner+1972/+1976. No direct scalar store in that body
populates either resident context's SGO/handle fields. Resource lookup, member
helper aliases and other later callers still need their own ownership bounds.

The constructor also calls 82125B30 for members owner+2000 and owner+2080.
That helper attaches member+16 to the owner's reference list at owner+4 and
stores the owner pointer at member+24. Complete 821A1628/821A1678 only splice
the two link words (including neighboring nodes); they do not copy a parameter
object or call an arbitrary callback. These indirect neighbor stores must be
classified as reference-list writes, not dismissed as stack-only stores.
821C0AE0 additionally forwards owner and its field+32 to 821A6920 during
construction; that registration path and later object updates remain open.

This closes the missing initializer identification for the resident branch and
rules out a whole-context copy in the inspected array helper. It does not prove
that every later alias preserves the null fields. Runtime comparisons remain
enabled; this source investigation required no executable rebuild.

## Registration branch and resident-context update caller

Complete 821A6920 resolves the registration forwarded by 821C0AE0. It searches
the manager's list (fields +44/+56) by name through 820A2BF8. On no match it
returns zero without registering the owner. On a match, 821C0AF0 writes the
selected entry to owner+28; that helper has exactly one store and no calls.
It then allocates a 20-byte list node through 820B24A8, embeds an owner reference
at node+8 (owner pointer at node+16), splices it into the manager's list at +60,
and removes the temporary stack reference. The normal path updates reference
links and manager count, not resident context+68/+72. It publishes a reference
to the owner, so later consumers remain a separate question.

Complete 820A2BF8 and 820A26A8 establish that the normal name comparison reads
16-bit characters and does not write the supplied strings or owner. The range
error branch is not reached for this caller's zero starting offset. Complete
821D39A0 normally increments the list count at its argument+8 (manager+68);
its overflow/error branch remains outside this normal-path classification.
Failed allocation and corrupt/aliased neighbor pointers are not certified.

The sole emitted direct call to 820FA600 is in complete 820A42D0: it allocates
2160 bytes via 820B24A8 at 820A42E0, checks for null, and constructs the returned
allocation at 820A42EC. This supplies a concrete normal heap allocation covering
both contexts rather than merely inferring an owning object from nearby offsets.
Indirect constructor entries and the virtual dispatch identity remain separate.

Complete 820FA108 is the sole emitted direct caller of 820F9FB0 (820FA314).
For mode one it updates vectors at owner+1360/+1376/+1392, then invokes
820F9FB0 when the low four bits of word(word(owner+1340)+8) are zero. Its
direct writes do not populate either resident context's SGO/handle fields.
The mode-zero branch supplies owner+1376 to vector helpers and returns;
other modes return without the factory call. Vector helpers 821B0718/821B0320
and state transition 820FD350 remain qualified until their effects are bounded.
This identifies the actual update entry and call condition, not every indirect
method or escaped owner alias. No runtime comparison policy changed.

## Resident descriptor: resolved callback cycle and vector destinations

Read complete 820FD350/821E2A00 and connected the previously documented generic
dispatch mechanism in geometry-render-queue-ownership.md to this actual owner.
820FA600 initializes descriptor owner+1328 with receiver owner at +8 and record
owner+1312 at +12, initially zero. Its packed target is 820F9F50 with zero
receiver adjustment. Therefore the initial 820FD350 call has no old handler
absent intervening alias writes; its new-handler dispatch reaches 820F9F50
with the owner as receiver and mode zero. Complete 820F9F50 does nothing for
mode zero or two. This bounds the constructor's newly installed callback,
not just the generic wrapper's own direct stores.

Complete 820F9F50, 820F9BD8 and previously read 820FA108 resolve this cycle:

- 820F9F50, mode one, when owner+1220 byte is nonzero: installs 820F9BD8.
- 820F9BD8, mode zero: initializes direction using owner+1392 and +1360;
  mode one installs 820FA108; mode two returns without work.
- 820FA108, mode one: invokes the resident factories under its low-four-bit
  condition, and after the counter exceeds 300 installs 820F9F50 again.
  Mode two returns without work.

Each transition uses owner+1328 with zero receiver adjustment. For this cycle,
old-handler mode two therefore does not introduce additional callback work.
This does not exclude an unrelated later writer replacing the descriptor.

Complete 821B0320 is call-free vector normalization/scaling with writes only
argument+0/+4/+8. In 820FA108 those arguments are owner+1376 or stack vectors;
in 820F9BD8 it receives a stack vector. Complete 821B0718 directly writes only
its three output floats and uses local matrices with 821C7B20/821C7E30.
Those two complete matrix bodies write their supplied local matrices and call
821E9558/821E9630 for floating-point math. Complete 8219F020 additionally
updates its fixed global RNG word; it is not a stack-only helper.

Complete 821B07D8, used by 820F9BD8 with output owner+1360, also directly writes
only three output floats. It supplies stack matrices to 821C7B20/821C7E30 and
821C7EE0, and calls RNG helpers 8219F068/8219F020. Remaining unbounded helper
effects in this branch are 821C7EE0, 8219F068 and the math callees, alongside
other object methods/descriptor aliases. No direct geometry-input-field write
was found in the resolved cycle. No runtime comparison was removed or build
produced by this source investigation.

## Vector-helper effect closure for the resolved resident cycle

The outstanding math/helper bodies above are now fully read, including their
direct callees: 821C7EE0, 8219F068, 821E9558, 821E9630, 8219EEC0,
821C7D80 and 821EC5E0. This closes that specific helper-effect queue:

| Helper | Established guest writes, excluding ordinary stack frames |
| --- | --- |
| 821E9558 / 821E9630 / 821EC5E0 | Only stack scratch; no calls or pointer-output stores |
| 8219EEC0 | Only stack frame; sole math callee 821EC5E0 has stack scratch only |
| 8219F068 | Fixed RNG word at base -2108358656 +24288, plus stack scratch; no calls |
| 821C7D80 | Six floats at output+0/+8/+16/+24/+32/+40; math callees above |
| 821C7EE0 | No direct output stores; forwards original output to 821C7B20/821C7D80, reads original input vector |

Together with the earlier complete bodies, 821B0718 and 821B07D8 have bounded
effects: their three output floats, local matrix/scratch storage, and the fixed
RNG word shared by 8219F020/8219F068. No hidden pointer-writing callback or
additional payload allocation remains in these helper chains. On the inspected
resident-cycle calls, their output is owner+1376 or owner+1360, respectively;
their matrix destinations are local stack buffers. These ranges do not include
either resident context's SGO/handle fields. This classifies these actual calls,
not every possible caller of a general vector helper.

The resident-context investigation can now move past the vector/math branch.
Still open are other methods and escaped aliases of the owning object, the
remaining constructor helpers, and nested factory effects. Global RNG mutation
is explicitly retained in the effect set; it is not misclassified as pure math.
No runtime comparison policy or executable changed.

## Remaining constructor helpers: child virtual-call boundary

Read complete 8210C6F0, 8210C5A0, 820D79D8, 8210B898, 821C1120,
820BDD50, 821D1880, 82125B88 and 820DEDF8.

820BDD50 initializes the member passed at owner+2016 or owner+2096. Its
821D1880 call zeros the two reference links before 821A1678 unlinks them,
so no old neighbor is visited absent an intervening alias write (none is
directly introduced between these calls). The remaining direct stores end at
member+49. These stores do not overlap either resident context.

82125B88 resolves a named entry through 82124BD8. On success it writes the
destination member's +0/+4 and calls 820DEDF8 with destination member+16.
Complete 820DEDF8 splices its reference links, copies the referenced owner
pointer at +8 and scalar/transform fields at +16..+49. Its direct destination
extent therefore ends at outer member+65; neighbor-link effects retain the
reference-list ownership qualification. 82124BD8 itself remains to be bounded.

820FA600 calls 8210C6F0 with count zero. Complete 8210C5A0 allocates a
four-byte count header for that request; both its new-element construction and
old-element copy loops are skipped. It still releases the old array through
820D79D8 before publishing the new empty control at owner+1116/+1120/+1124.
Therefore zero requested count alone does not eliminate old-element destruction.
820D79D8 skips destruction only when the prior pointer is null (or its stored
allocation count is zero); otherwise it calls 821CFFA8 for each 104-byte record.
The base constructor's prior array state remains relevant. Successful zero-count
replacement makes 8210C6F0 skip its later element and SGO lookup loops; failed
allocation leaves the prior control unchanged and is not covered by that claim.

8210B898 is not just scalar setup. With a valid type-four SGO entry it builds a
temporary name/member table, passes the owner to 821AF920, copies the result to
owner+1140 through 820DE968, then calls 821C1120 at 8210B9B8. Complete
821C1120 walks the owner's child list from fields +144/+156. For each node it
loads child=node+8, reads child's vtable slot+4, and invokes that method at
821C11C0 (return address 821C11C4), before reading child bounds at +48.
Its own object stores are bounds/count fields +160..+192 and +208, but the
child virtual method effects are unresolved. A null or empty child list skips
that dispatch; the current constructor trace has not established either case.

This replaces the generic remaining-helper question with concrete allocation,
old-array destruction and child-method boundaries. Next relevant providers are
the base constructor's owner+1116 state and the child types/list assembled before
821C1120, including 821AF920. Geometry comparisons remain enabled.

## Base-array initialization and child-converter input connected

Complete 820FC6F0 calls 8210CA18 on the same owner before its own scalar
setup. Complete 8210CA18 explicitly zeros owner+1116/+1120/+1124 after
820D6000 returns. Its remaining direct stores do not repopulate that control;
the separately allocated record array in its suffix is owner+1128, not +1116.
However, numerous intervening helper calls remain between those zeros and the
derived constructor's 8210C6F0 call. This establishes the initializer but does
not yet establish an empty old array at the later release boundary.

Read complete 821AF920, 821AE980 and 821D7110. The previously unresolved
8210B898 -> 821AF920 call passes the type-four SGO entry's relative payload
as r6. 821AF920 retains it in r27 and passes it directly to 821D7110 at
821AF960 before checking the converted magic/version. There is no intermediate
allocation or input copy. This connects the existing 821D7110 tracker entry in
geometry-swap-tracker-lifetimes.csv to a concrete parent-SGO input path.

821D7110 is an in-place conversion, not a read-only validator. On recognized
byte-reversed magic it writes magic/version; for version 258 it initializes
tracker 8257C33C, swaps header words +8/+12/+16/+20, visits 16-byte records
through 821D6D30 and 12-byte records through 821D7038, then clears the tracker.
Those record addresses derive from offsets inside the same supplied payload.
Parent SGO allocation/offset bounds and nested record aliases remain relevant;
the already-converted fast path does not justify excluding the conversion path.

For a matching name, 821AF920 allocates a 192-byte child at 821AFA68 and
constructs it with 821AE980 at 821AFA74. It links child+28 into the parent's
list rooted at owner+144 at 821AFAB4, before configuring the child through
821AF6C0 at 821AFAC8. Thus the later 821C1120 list is not assumed empty.
Successful configuration appends the child pointer to a separately allocated
temporary output vector; the list link already exists if configuration fails.
The complete child constructor installs vtable base -2113798144 -29484
after 821D19B0, then initializes transform and trailing control fields.
Its slot+4 target and subsequent configuration effects still need resolution.

This pass identifies actual producers for both the old-array and child-list
questions, and connects an existing conversion writer to its input owner.
No runtime checks were disabled; no build or gameplay run was required.

## Child vtable resolved and retained input fields classified

Read the big-endian words of ../edf2027-analysis/guest_image.bin at image
offset 0x18CD4 (guest base 0x82000000). The constructor's table 82018CD4
contains 821AECC8 at +0, 821AEA30 at +4 and 821B0110 at +8. Thus the
821C1120 dispatch resolves to 821AEA30 for children retaining the vtable
installed by 821AE980; this is image evidence, not an inferred adjacent method.
An initial diagnostic seek used PowerShell's signed hexadecimal base literal
and failed the exact-read check; the successful read used decimal 2181038080
for the image base. No image bytes were changed.

Complete 821AF6C0 preserves the child and payload arguments, calls 821D7110
again on that same payload at 821AF6E4, then validates magic/version and the
signed record-index bound. On success its stores are child+176 (selected
12-byte record), child+180 (record-relative data) and child+184 (the incoming
name-table member pointer). It can also grow global control 8257C078 through
8217A180 based on record+8. It does not replace the child's vtable. These
retained payload pointers are not copied native-owned data, and their later
read/alias lifetime must not be conflated with the child's heap allocation.

Complete 821AEA30 reads those child fields, calls 821C7FB0 with output
child+112 and input child+184, and updates child transform/bounds fields.
Its other calls are 821B0258 (in-place output child+48, matrix child+184)
and 821C39F0 (child+48, matrix child+184). Direct stores are contained in
child+48..+80 and child+160..+175; no direct parent-context write occurs.
Those three helper effects and the global growth helper remain to be bounded.
Other child types already present on the parent's list and vtable replacement
remain outside this specific constructor/configuration path.

This resolves one formerly unknown virtual target and identifies its actual
input aliases. Geometry comparisons and the executable remain unchanged.

## Resolved child callback: complete write set

Complete 821C7FB0, 821B0258 and 821C39F0 are call-free. They respectively
write nine floats at output offsets 0/4/8/16/20/24/32/36/40, three floats at
output+0/+4/+8, and three floats at output+16/+20/+24. Their matrix/input
pointers are only read. Combined with complete 821AEA30, the callback's entire
non-stack write set is within the child: +48..+59, +64..+75, +80..+83,
+112..+123, +128..+139, +144..+155 and +160..+175. Its calls introduce no
allocation, callback, global store or write through the retained payload pointer.
This closes the helper-effects question for that resolved child type; it does
not classify other types in the parent's list or invalid/overlapping objects.

Complete 8217A180 also resolves configuration's global growth operation. It
frees the prior control+0 allocation through 820B25B8, clears its three control
words when that pointer was nonnull, allocates 4*requested_count bytes through
820B2550 (oversize requests become -1), and publishes the resulting pointer.
Only successful allocation writes requested count/capacity. There is no copy of
old payload and no initialization of the new allocation's contents in this
helper. For 821AF6C0 the control is fixed at 8257C078; this is a separate heap
scratch allocation, not the child+180 SGO record data. Old-backing ownership,
other users and concurrent growth still require qualification. Failed allocation
must not be interpreted as initialized usable storage.

The immediate child bounds callback is now bounded to its own object. The
remaining work in this branch concerns converter input ownership, global scratch
users and other owner/child aliases, not these three matrix helpers. Runtime
geometry comparisons remain enabled; no executable changed.

## Scratch control 8257C078: direct references and escaping local alias

Searching emitted -16264 displacements and checking the 82580000 base found
seven enclosing functions: 821AF6C0, 821AEE00, 8252C660, 821AEE50,
821AF100, 821AF328 and 8212DFD0. This is an index of that addressing form,
not exhaustive derived-pointer or raw-address coverage. Nearby -16248 uses
refer to the distinct 64-bit value at 8257C088, not this pointer control.

Complete 821AEE00 and 8252C660 free a nonnull control+0 through 820B25B8
and clear all three control words. Complete 821AEE50 appends qualifying
20-byte input-record pointers to the scratch array using child+188 as the
index/count. Complete 821AF100 reads the current list, resets child+188,
and compacts retained pointers back into the same array. Complete 821AF328
reads the list without directly writing its slots. These functions inspect
record-relative triangle coordinates and can pass computed contact information
to 821CE4C0; that helper's side effects are not classified by this review.
The list is therefore candidate-record pointer scratch, not a VB/IB byte copy.
Capacity/record bounds and concurrent reuse remain separate obligations.

The inspected 8212DFD0 interval loads the global backing pointer and stores it
at SP+200, alongside SP+192/+196 owner/context values and zero count SP+204,
before calling 8212D980 at 8212E2F8. This is an explicit derived-pointer path
outside a search for global-address loads. Full 8212DFD0 and that helper chain
still need argument propagation review before classifying their scratch writes.
Do not exclude them based solely on the seven direct-reference functions.

This narrows remaining scratch users to concrete producer/filter and derived
context paths. No runtime geometry comparison policy changed.

## Derived scratch writer reached through 8212D980

Read complete 8212DFD0, 8212D980 and 8212D110. The caller passes SP+144
as r6 to 8212D980 at 8212E2F8. Thus its earlier SP+200 backing pointer is
context+56 and SP+204 count is context+60. Context+48/+52 retain the caller's
entry r3 and child pointer. 8212D980 preserves r6 across its recursive
bounding-box traversal and passes it unchanged to leaf helper 8212D110 at
8212DA2C. It directly writes only its stack frame.

8212D110 saves that r6 as r21. For qualifying 20-byte input records it writes
the record pointer to word(context+56)[word(context+60)] and increments
context+60. This establishes an actual scratch writer through the derived
alias without any global-address reference in the writer body. The record's
vertices/plane data are read via relative offsets; those reads are not VB/IB
uploads. Capacity enforcement is not present at this append site.

The full caller also reveals another temporary escape: it publishes a function
address at its entry-r4 object's +208 and SP+144 at +212 before traversal,
then clears both on normal completion. Leaf contact reporting through
821CE4C0 receives that same entry-r4 object. Therefore callback effects and
abnormal-return cleanup must still be checked rather than assuming the stack
context cannot escape. After traversal the caller sends SP+144 to 8212D3F8
and, if its count remains nonzero, 8212D660. These are the next concrete
filter/consumer edges; their scratch effects are not classified here.

No runtime comparison policy changed. The inventory now includes this derived
writer in addition to the global-address-reference list.

## Derived-list consumers and contact-reporting boundary

Complete 8212D3F8 receives the same context in r5, resets context+60, and
compacts retained record pointers into word(context+56) while incrementing
that count. Complete 8212D660 reads those slots without direct list/count
stores. Both can report contacts through 821CE4C0, with stack-local point
and direction arguments. Thus the derived list has a concrete append writer
(8212D110), compaction writer (8212D3F8), and read consumer (8212D660).

Complete 821CE4C0 writes contact/result fields within its entry object, but
is not a leaf: depending on object+140/+153 it invokes vtable+4 before
publishing the result, or calls 821CE3A0 afterward. Complete 821CE3A0 can
invoke object+144's callback, virtual methods on contact participants, and
object vtable+8. None of those dispatches directly loads object+208/+212.
Consequently the callback installed by 8212DFD0 cannot yet be assumed to be
the only callback reached by this contact-reporting path.

The packed address installed at +208 resolves arithmetically to 8212D018.
Its complete body takes the context in r4, reads context+48/+52, and applies
a displacement to two three-float regions (+16/+20/+24 and +32/+36/+40)
of each 64-byte record in word(word(context+48)+112). It neither reads nor
writes context+56's scratch slots. This identifies the target's effects, but
does not establish which intervening method invokes +208 or every possible
contact callback. The backing owner of those 64-byte records remains to trace.

No production comparison was removed. This closes the two immediate list
consumer bodies and separates their scratch writes from contact callbacks.

## 64-byte callback records: normal allocation and append provider

Complete 8212DD40 allocates a 128-byte object through 820B24A8, calls
821D19B0, installs its table, and zeros object+112/+116/+120. The inspected
caller interval at 8212E59C immediately passes that returned object, unchanged
in r3, to 8212DE40 at 8212E5B0. There is no null check between those calls;
the failed-allocation path is not certified.

Complete 8212DE40 builds 64-byte records in local storage and appends them
to the object's +112 control. If full, it calls complete 8212D840 with that
control and a doubled requested capacity. 8212D840 allocates 64*capacity
through 820B2550, initializes vector fields in each slot, copies the retained
old records in eight-word loops, frees old storage through 820B25B8, and
publishes pointer/count/capacity. Allocation failure returns false without
replacing the old control. Successful append copies eight 64-bit words from
the local record to the selected slot. No borrowed input pointer becomes the
array backing on this normal allocation path.

Complete 8212DDC8 frees object+112 through 820B25B8 before base destruction
and optional object free. Thus a concrete create/append/grow/destroy family
exists for separately heap-owned 64-byte records. This is not yet a proof that
every receiver of 8212DFD0/8212D018 belongs to that family: the direct dispatch
wrapper 8212E350 selects its entry-r4 field+32 child by a type marker and keeps
entry r3 unchanged. Its receiver construction/indirect dispatch link remains
to connect. Matching record stride and field offsets alone are insufficient.

This classifies the normal storage provider without claiming all callback
receivers or later aliases proven. Geometry comparisons remain enabled.

## 64-byte owner vtable connects the normal dispatch family

Read guest-image words at 82008808, the table installed by 8212DD40:
slot+0 is 8212DDC8, +4 is 8212CA28, and +8 is 8212E3B0. The following
word is zero, followed by string data; these are not additional method slots.
Complete 8212E3B0 preserves entry r3 and dispatches by entry-r4 field+132:
zero -> 8212CC98, one -> 8212CE28, two -> 8212E350. The previously
inspected 8212E350 branch preserves that receiver into 8212DFD0 when the
other child's type marker matches. This connects the normal allocated object
family to the callback-record path through an actual vtable, rather than
matching field offsets alone. Alternate receivers/table replacement remain
outside the normal-family claim.

Complete 8212CA28, the bounds method in the same table, copies each owned
64-byte record's initial vector to record+16..+31, transforms its first three
floats through call-free 821B0258 using the matrix pointer at record+48,
and updates object bounds at +48..+79. No other callee occurs. This adds a
concrete writer to those separately allocated records; the matrix input is
read, not used as a write destination. Together with creation/growth and
8212D018, the inspected normal paths operate on the owner's heap array,
not directly on the retained SGO triangle data or GPU VB/IB payload.

The family connection is now established for normal virtual dispatch. Remaining
limits are escaped/replaced backing pointers, the other dispatch modes and
contact callbacks; this evidence does not authorize blanket geometry-check
removal. No runtime source or executable changed.

## Other record-owner dispatch modes bounded up to contact reporting

Complete 8212CC98 (mode zero) and 8212CE28 (mode one) read the owner's
64-byte array and perform geometric tests against the supplied query. Their
direct stores are stack-only. Their only callees are 821B02D8, 821B03C8
and 821CE4C0. Complete 821B02D8 writes three floats to its input vector;
all calls here supply local stack vectors. Complete 821B03C8 is call-free
and has no stores. Consequently these modes introduce no direct array or
scratch writer before the already identified contact-reporting boundary.
821CE4C0 can still invoke query/participant callbacks, so neither entire
dispatch mode is classified as read-only.

All three immediate dispatch branches for table 82008808 are now inspected.
Remaining expansion should focus on callback receiver ownership and backing
aliases, not re-open the mode-zero/mode-one arithmetic as unknown writers.
Geometry comparisons remain enabled; this was source analysis only.

## Temporary callback adapter identified

Complete 821CE380 is the adapter for query+208/+212: it loads the function
from entry-r3+208, moves incoming r4 to r5, loads the saved context from
entry-r3+212 into r4, and tail-dispatches if the function is nonnull. It has
no stores. For the 8212DFD0 installation this supplies exactly the context
and displacement argument layout consumed by 8212D018.

No emitted direct call to 821CE380 was found. An aligned big-endian pointer
scan of the guest image found its address at 82019D64. Correction after full
constructor inspection: this is the start of a separate table installed by
821CE2E0, not slot+0x18 of the neighboring 82019D4C table. The adapter is at
query slot zero. The earlier slot+0x18 interpretation was incorrect; see the
constructor and caller evidence below. Its contact/participant dispatch edge
still needs resolution.

The same numeric +208/+212 offsets also occur in complete 821A4270 and
821A42C8, where radius-related fields +260/+264 are updated and a different
callback argument setup follows 821A3A48. Those bodies are not assigned to
this query type merely because the offsets match. Receiver provenance remains
necessary. No geometry comparison policy changed.

## Slot-offset search qualification and explicit scratch teardown caller

A scoped emitted-code search for slot+24 immediately loaded into CTR found
multiple receiver families, not a unique query callback caller. Complete
821C5FC8 dispatches that slot on entry r3 with bounds-test arguments after
checking receiver+108; complete 821A4FC0 dispatches on list-node+12; complete
821A6508 dispatches on its service pointer at +132. None of those inspections
established the adapter's receiver. They must not be recorded as calls to
821CE380 solely from the slot offset. The contact-to-adapter link remains open.

Complete 820A4F90 and 820A5020 instead use slot+24 for replacing global
service objects, with r4=1 before fresh 84-/76-byte allocations. Complete
820A9348 includes corresponding service teardown, and explicitly calls the
known scratch teardown 821AEE00 at 820A9440. This provides a direct lifecycle
caller for freeing 8257C078's backing, rather than leaving the free helper
unconnected. It does not prove teardown cannot overlap an active contact
query; its surrounding service/participant lifetimes still need qualification.

This search produced a concrete scratch-lifecycle edge but did not resolve the
desired virtual receiver. No runtime geometry checks or executable changed.

## Corrected table boundary and stack-owned query construction

Full 821CE168 initializes matrices from owner+32, copies a 64-byte transform
to owner+416, obtains parameters at owner+480, and calls 821CDDF8. Full
821CDDF8 builds further matrices and projection parameters. Its sole emitted
direct constructor caller 820D39A8 replaces table 82019D4C with 82003E50.
This is not evidence that the adjacent callback table belongs to that object.

The raw image at 82019D60 contains 82085514; 82019D64 contains 821CE380,
followed by two 8252B718 entries. The decisive boundary evidence is full
821CE2E0: it calls 821A3D50 on the unchanged receiver, then installs exactly
82019D64 at receiver+0, zeros callback/context fields +208/+212, stores its
incoming r7 at +16, and copies 48 bytes from incoming-r7+160 to receiver+160.
Full 821A3D50 initializes the contact fields +48 onward, mode +132, reporting
controls +140/+144/+148 and flags +152/+153. This establishes the query
layout independently of neighboring table addresses or matching offsets.

The sole emitted direct caller, complete 821A4730, constructs this query at
SP+80 (call PC 821A4764), with mode 2 and initial reporting control zero.
It iterates manager+44/+56 and passes the same stack query as r4 to each
list-node+8 participant's virtual slot+20 (call PC 821A47A4), then returns
query+152. The query storage in this normal path is stack-owned, not a VB/IB
allocation. Participant callbacks can still reference external storage;
stack ownership of the query does not certify their transitive writes.

Next tracing must follow query slot zero through those participant methods,
not resume the unrelated slot+24 search. Neither a later vtable overwrite nor
the complete participant-to-8212DFD0 chain is proved here. The existing
8212D018 record-array write classification remains conditional on the tracked
receiver/installation. No comparison policy or executable changed.

## Callback helper closure and remaining dispatch limits

Full 821B0130, the sole callee of 8212D018, is call-free and writes exactly
three floats at output+0/+4/+8. In 8212D018 that output is SP+80; its matrix
input is child+112. Thus the complete callback's non-stack writes are exactly
the six previously identified floats per 64-byte collision record, at offsets
16/20/24 and 32/36/40, with owner=word(context+48), backing=word(owner+112),
and count=word(owner+120).
It neither writes the shared scratch pointer list nor invokes another callback.
This closes the callback body's transitive write set for valid inputs, without
asserting all possible record-array aliases are excluded from render resources.

Full 8212E350 also has a second type branch: a child marker matching global
825787B8 tail-dispatches to 8212DA40; the 8257C088 marker branch tail-dispatches
to 8212DFD0. If neither marker matches, it returns. Do not describe mode two
as exclusively the 8212DFD0 path.

Rechecking full 821CE3A0 confirms it dispatches participant slot+20 and query
slot+8, not query slot zero. Complete 8210D440, 821A4BA0 and 821CCF68 contain
slot-zero calls but do not establish the saved incoming-r4 query as receiver:
their examined receivers are respectively constructed/self objects, manager
list entries, and the incoming-r3 owner. They are not adapter call edges.
The participant-to-query-slot-zero edge remains unresolved; these inspections
do not authorize removal of guest geometry comparisons.

## Second mode-two branch: bounded record displacement callback

Complete 8212DA40 receives the unchanged record owner in r3, query in r4,
and child in r5 from 8212E350. It publishes callback 8212D7D8 at query+208
and the record owner itself at query+212. It traverses the owner's 64-byte
records (pointer +112, count +120) against the child's 128-byte records
(pointer +112, count +120). Its own non-stack stores are only the two query
callback/context fields, set on entry and cleared on normal exit. Contact
coordinates and normals are local stack values. Its only callee is 821CE4C0
at call PC 8212DD00; participant callback effects remain a separate boundary.

Complete installed callback 8212D7D8 is call-free. Given r4=record owner and
r5=displacement, it adds the displacement's three floats to record offsets
16/20/24 for each 64-byte record in word(owner+112), count word(owner+120).
Unlike 8212D018, it does not update offsets 32/36/40 or transform a displacement.
It does not allocate, write child records, change array control words, or touch
the shared scratch list. The corrected 821CE380 adapter supplies precisely
these r4/r5 arguments when this installation is active, but the participant
invocation edge is still not established merely by matching the signature.

Both marker branches of 8212E350 are now inspected, and both installed
displacement callback bodies have complete bounded write sets for valid inputs.
This closes the immediate mode-two branch/body inspection, not all contact
participant methods, record-array aliases, or concurrency. Normal allocation
provenance for the record owner remains the previously traced 8212DD40 /
8212DE40 / 8212D840 CPU-heap family. Neither branch is newly established as
a render VB/IB writer. No geometry comparison policy changed.
