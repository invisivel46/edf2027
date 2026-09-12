# Packed-bit-vector writer frontier

Generated-source review, September 11. This is a bounded direct-call trace, not
proof that generic bit-vector routines can never receive geometry aliases.

## Resize path

`821BAE98` (shard56) publishes the requested bit count at owner+0, erases excess
backing words through `821B98D8`, then masks a partial last word at `821BAFC4`.
Its backing vector control begins at owner+4: begin/end are owner+8/+12.

The emitted direct chain is:

| Caller | Call PC | Callee | Argument relationship |
| --- | --- | --- | --- |
| 821BBAD8 | 821BBCFC | 821BBA28 | Local bitset at SP+96; requested count r30 |
| 821BBA28 | 821BBA68 | 821BB8F8 | Growth branch: requested minus old count |
| 821BB8F8 | 821BB940 | 821BB7A8 | Growth/insertion helper |
| 821BBA28 | 821BBACC | 821BB668 | Shrink branch, iterator range |
| 821BB668 | 821BB754 | 821BAE98 | Same owner, new bit count from remaining iterator |

Each listed callee has the single emitted direct caller shown, except BBA28's
two outgoing branches. Source checked in shards22/34/44/60/56. Follow-up review
completed BB7A8 in shard13 and BB668 in shard60, including iterator construction,
word-vector resize and the forward/backward copy dispatch described below.

In `821BBAD8`, label821BBC5C initializes the local count and begin/end/capacity
to zero. Before the resize call it compares cached count r28 with requested r30 and skips
resize if r28>=r30. After resize and the bit-marking loop it reloads count from
SP+96. The direct call thus requests growth according to that cached count,
not an intentional shrink. This narrows the normal direct route to the tail-mask
writer; it does not certify all intervening helper effects, malformed ranges,
indirect entry, or whole-function memory safety. A local control block alone
would not establish backing ownership without initialization/allocation evidence.

## Separate bit assignment writer

Complete `821B9EA8` (shard66) sets or clears one bit according to the low byte
of r4. The iterator at r3 supplies owner at+0, word pointer at+4 and bit offset
at+8. It checks owner/pointer and the computed bit position against owner count,
then performs a four-byte read/modify/write at that word pointer. Set stores at
821B9F1C; clear stores at821B9F88. It does not allocate backing storage.

| Caller | Call PC | Established use / remaining question |
| --- | --- | --- |
| 821BBAD8 | 821BBD78 | Marks bits through an iterator built from the local bitset |
| 821BA648 | 821BA6A0 | Fills the newly inserted range of the same local bitset |
| 821BA740 | 821BA820 | Forward bit copy for the same owner's shrink path |
| 821BA968 | 821BAB10 | Backward bit copy for the same owner's insertion path |

The four direct edges were re-enumerated from all generated shards. Follow-up
review resolves the three generic caller provenance questions on these direct
paths; the chain does not introduce a separate destination owner:

| Operation | Direct chain after BBA28 |
| --- | --- |
| Fill | BB8F8 -> BA648 -> B9EA8 |
| Backward shift | BB8F8 -> BB7A8 -> BB4C0 -> BB200 -> BA968 -> B9EA8 |
| Forward shift | BB668 -> BB400 -> BB108 -> BA740 -> B9EA8 |

Addresses in this table have prefix 821. Each intermediate callee has exactly
one emitted direct caller, rechecked across all generated shards. BB400/BB4C0
copy and forward the supplied iterator triples. BB108/BB200 compute a result
iterator and forward the source/destination triples to BA740/BA968. Those loops
copy individual bits through B9EA8 while advancing or retreating both iterators.
BA648 copies its current iterator to the stack and passes the fill byte to B9EA8.

Complete iterator helpers BA008 and BA098 copy owner/word/bit and call B9DA0
with positive or negated displacement. Complete B9DA0 changes only iterator+4
and iterator+8; it neither changes owner nor allocates payload storage. B9F90
constructs an end iterator from owner+8 and owner+0 through the same helper.
These helpers do not introduce an independent backing address.

BB7A8 grows the word vector at owner+4 through BAFD0 before reconstructing its
iterators. Complete BAFD0 inserts through BA368, or shrinks through B98D8.
The existing allocation trace in geometry-writer-enumeration.md establishes
BA368 -> B8DD0 -> 820B24A8 as process-heap allocation; capacity reuse retains
the vector's backing. Together with the root's empty initialization, this
classifies normal direct-path bit storage as CPU-heap-backed local scratch,
not a newly identified physical model VB/IB writer.

Scope: this closes the direct helper-owner questions, not arbitrary indirect
entry, malformed iterators/ranges, escaping aliases or whole-function memory
safety. Bounds checks alone are not ownership proof. No runtime comparisons
were removed, and no new runtime geometry corruption was observed.
