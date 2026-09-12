# Model subset wrapper-copy frontier

September 10, 2026. Ghidra exports were expanded and checked against generated
instructions. This is an alias/lifetime finding, not proof that live model
payloads are mutated by these paths during gameplay.

## Concrete wrapper layout and copies

821B3C98 stores subsets in 148-byte (0x94) records. It constructs the model VB
at record+4 and IB at record+0x54. The layout relevant to ownership is:

| Record member | Extent | Payload pointer within record |
| --- | --- | --- |
| Material/reference word | +0, 4 bytes | Not a geometry payload pointer |
| VB wrapper | +4, 68 bytes | +0x34 (VB owner+48) |
| Referenced declaration/object bookkeeping | +0x48, 12 bytes | Separate reference bookkeeping |
| IB wrapper | +0x54, 60 bytes | +0x84 (IB owner+48) |
| Final float | +0x90, 4 bytes | Not a geometry payload pointer |

821B3250 copies the VB wrapper using 821E8320 at 821B3280 (return 821B3284),
length 68, then the IB wrapper at 821B32F4 (return 821B32F8), length 60. It
copies the middle reference object separately and conditionally increments that
object's reference count. No model constructor or payload allocation is called
for either geometry wrapper. These are shallow wrapper copies: headers,
allocation records, initialized flags and payload pointers are copied together.
The body does not write through the copied payload pointers.

Three assignment helpers repeat the same two wrapper spans:

| Helper | Direction | VB/IB copy instructions |
| --- | --- | --- |
| 821B3320 | Forward range assignment | 821B3360 / 821B33CC |
| 821B3400 | Backward range assignment | 821B3450 / 821B34BC |
| 821B34E0 | Fill assignment from one record | 821B3528 / 821B3590 |

The explicit release/increment in these helpers belongs to the middle +0x48
reference, not to a new independent VB/IB allocation. Do not infer deep geometry
copying from that reference-count activity.

## Container paths

821B3B08 is the subset resize helper called directly by 821B3C98. Growth goes
through 821B3788, which can allocate a larger record array, copy the existing
prefix/suffix with 821B3250, insert records through 821B3698, then destroy and
free the old array. In-capacity insertion can use 821B3628 -> 821B3400 and
821B34E0. The new array's begin/end/capacity pointers are published at
821B39D0/39E4/39E0 respectively.

Shrinking goes through 821B36F8 -> 821B35B8 -> 821B3320, then range destruction
via 820AB448. That destructor walks records in 0x94-byte steps and calls
821B3210, which performs IB cleanup at record+0x54, middle-reference release at
+0x48, and VB cleanup at +4. The model cleanup/pool-release hooks already cover
those original owner addresses; they do not by themselves publish copied owners.

821B3C98 creates an empty stack template with 821D7420/821D75B8 before resize.
Those helpers clear the initialized flag and metadata and initialize the
allocation record. Thus insertion from this template is not evidence that a
live buffer was copied. However, the resize implementation also contains
existing-element relocation paths; inspecting only the empty template cannot
exclude those paths for an already populated container.

## Additional callers prevent declaring the frontier closed

The generated direct-call inventory, cross-checked against the current source,
also finds 821CA378 calling 821B3250 twice and 821B35B8 twice. 821CA310 calls
821B36F8; it is called by 821CA378. Calls into 821CA378 come from 821D8770 and
821CA7A8. These callers have not been classified in this audit. They are the
next targeted Ghidra work, especially whether their copied records can contain
already published model buffers.

## Consequence for native ownership

The current registry publishes model ownership through 821D7530/821D76A8.
It must not treat absence of later payload notifications as proof that all
wrappers naming the allocation were registered. Wrapper-copy destinations can
be ordinary CPU memory: the physical bulk-write notifier then correctly reports
no geometry payload write, but that says nothing about the newly copied pointer.

If live copies are reachable, migration needs explicit copied-owner/alias
registration and a proven allocation-lifetime policy across assignment and old
record destruction. Blindly calling Publish for a copied wrapper would not
establish who can release the shared allocation. No alias registration or
comparison bypass was implemented on this evidence alone.

Expanded the maintained Ghidra seeds with the resize/copy/assignment/destruction
helpers and saved exports under out/ghidra/geometry-writers-inline. All three
headless runs exited zero and saved successfully; known unrelated incomplete
control-flow warnings remain. No game/build was launched by this audit. The
finding narrows the next writer/alias investigation; it does not establish a
runtime bug, immutable geometry, full alias coverage, or completion.

## Caller classification: normal cache insertion copies an empty model

Follow-up Ghidra work resolves the additional direct callers, with generated
instructions used to check arguments omitted by the decompiler:

- 821CA378 is general subset-array assignment. It can assign an existing prefix,
  append copies within capacity, or allocate/copy a replacement array. Its body
  alone does not restrict the source to uninitialized buffers.
- 821D8770 resizes the outer array of 52-byte model records. It copies up to
  min(old count,new count) records using 821CA378 at 821D8840, then copies their
  scalar fields, destroys the old array and publishes the replacement.
- 821CA7A8 copies an outer model array, allocating through 821CA1C8 and applying
  821CA378 to each source record. 821CA850 calls it as part of constructing a
  copied model collection.

The direct cache-miss path places stronger constraints on these generic bodies:

1. 821CB550 looks up the cache key. On a miss it calls 821CB328 at 821CB5B0;
   only after that returns does it invoke the model loader 821D88C0 at 821CB5C4.
   The cache-hit branch obtains the existing reference without loading/copying
   the model again.
2. 821CB328 constructs a stack model at SP+0xB0 via 821D8740. That initializer
   zeroes the three array descriptors (words 0 through 32) and byte 36.
   It then copies this empty model into the key/value temporary at SP+0x80 via
   821CA850 (call 821CB3A4).
3. The temporary pair begins at SP+0x60. 821CB010 receives that pair pointer as
   entry r6 and preserves it in r31. All seven insertion calls to 821CA950 pass
   this same value as r7; its fallback 821CAD10 likewise forwards the same pair
   at both insertion sites. Tree comparisons choose a position, not a different
   model value. There is no model-load call in these insertion bodies.
4. 821CA950 constructs the node model at node+0x2C from pair+0x20 using 821CA850
   at 821CAA1C. Thus this insertion source still has zero model-record count.
   821CA7A8 consequently does not iterate/copy any subset wrappers here.
5. After insertion returns the empty cache model, 821D88C0 allocates its outer
   model array through 821D8770 before calling 821B3C98 for each model record.
   On this fresh path the old outer count is zero, so the relocation loop in
   821D8770 also copies no existing live model. Each new subset array is then
   sized from the previously identified uninitialized template before the
   VB/IB payload constructors run.

Therefore the inspected successful fresh-cache path does NOT create live VB/IB
aliases through these wrapper-copy routines. This is a useful exclusion: do not
add copied-owner publication hooks solely because the generic template bodies
contain shallow copies. It does not prove that arbitrary indirect entry,
reentrant cache mutation, malformed input, reused loader objects, or other
unclassified aliases cannot reach live-copy branches. It is also not evidence
that no later payload writer exists. The standalone node/model counters outside
the three array descriptors are not certified by the empty-count argument.

Added twelve targets across this follow-up's three headless runs. Each run
exited zero and saved its project; known unrelated incomplete-flow warnings
remain. No executable changed, no comparisons were removed, and no runtime
performance or complete-writer-coverage claim follows from this static result.
