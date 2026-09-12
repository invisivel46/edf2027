# DXM source allocation and construction order

This records the inspected normal direct loader path, not a whole-program
geometry immutability certificate. Addresses are guest instruction addresses.

## Established ownership

`821AB708` creates an empty archive object and calls `821D6448`.
The file reader `821A0550`, reached through `821A0EB0`, allocates its raw
buffer through `8219F940 -> 820B2550 -> 821E8C20`. The last helper uses
the previously traced process heap (`82132F38 -> 82131A48`), not the model
physical pool. `8219F940` always attempts a new allocation, optionally copies
the old bytes at `8219F998`, then frees the old allocation on success.

`8219F9E0` prepares the archive buffer by one of two paths:

| Input | Destination allocation | Payload writer | Source lifetime |
| --- | --- | --- | --- |
| Uncompressed | `82125D28 -> 82125CA8 -> 820B2550` | byte loop at `82125D6C` | Source remains owned by the temporary file buffer |
| SGSL compressed | `8219F940 -> 820B2550` using decoded size | decoder `821D5548`, output stores `821D55C8` and `821D5628` | Source remains owned by the temporary file buffer |

The uncompressed path is a deep byte copy, **not pointer transfer**. The
temporary raw buffer is released by `821A0EB0` after preparation. Both normal
destination allocation paths use CPU heap storage. The compressed output is
not an alias of the raw input.

After DXB conversion, `821D6448` indexes 32-byte records within that archive.
`821D5CB0` creates a 64-byte member descriptor with data pointer
`record + record[0x14]` and size `record[0x10]`. Member extraction here only
copies names and publishes the pointer/size; it does not allocate member data.
`821AB708` forwards the descriptor data pointer through `821CB6A0` and
`821CB550` to `821D88C0` on the model-cache miss path.

`821D88C0` calls DXM conversion at `821D88EC` before mesh construction at
`821D8AB4`. `821B3C98` then invokes the VB/IB constructors, which allocate
separate model-pool payloads, copy source bytes, and publish native ownership.
The source archive and those new geometry allocations are distinct on this path.
`821D5B50` later releases the archive buffer and its record index.

## Descriptor append and growth

The normal `821D6AE0 -> 821D6A08` append preserves the member alias:

| Operation | Helper | Pointer/size stores |
| --- | --- | --- |
| In-capacity append/fill construction | `821D6328` | `821D63A4`, `821D63AC` |
| Temporary descriptor copy before growth | `821D5BC0` | `821D5C20`, `821D5C28` |
| Existing descriptor range copy | `821D6148` | `821D61C4`, `821D61CC` |
| Assignment fill (generic insertion branch) | `821D60D0` | `821D6124`, `821D612C` |
| Backward assignment (generic insertion branch) | `821D5C38` | `821D5C94`, `821D5C9C` |

Each listed pair copies source offsets `0x38/0x3c` to the same destination
offsets. The two name strings are copied separately through `820A0AA0`;
these helpers do not dereference the member-data pointer to copy mesh bytes.

When append has no capacity, `821D6928` calls `821D6658` with count one.
The latter copies the incoming descriptor to a stack temporary, allocates a
larger descriptor array through `821D5A40 -> 820B24A8` (64 bytes per entry),
copies the prefix/suffix through `821D6148`, and constructs the inserted
entry through `821D65F0 -> 821D6328`. `821AA130` destroys the old names,
not the member payload; the old descriptor array is then freed separately.
Thus normal append/reallocation does not change the archive ownership of
the model input. This closes descriptor propagation on this direct path.

The generic in-capacity insertion branch also calls `821D63F0 -> 821D5C38`.
The backward assignment implementation decrements source and destination by
64 bytes per iteration, copies both names, then preserves pointer/size at
the stores listed above. Ghidra instructions and generated source agree.
It introduces no direct store through the member pointer. Other callers and
container aliases are not certified by reviewing this helper's body.

## What this does not establish

- The SGSL decoder takes input length but no output capacity. It has no explicit
  output bound at the two output stores. Claimed decoded size versus actual
  emitted size and truncated token handling remain unverified.
- `8219F9E0` still calls the decoder if destination allocation fails; the observed
  branch only conditions the size-field update. Failure behavior is not certified.
- Archive-relative member offsets and DXM nested offsets have not been proven
  bounded for every asset. Later aliases and other descriptor-container uses
  remain separate from the normal append propagation documented here.
- This does not exclude unrelated writers, indirect entries, reentrant conversion,
  or concurrent mutation of live model-pool storage.

Geometry byte comparisons remain enabled. This evidence does not authorize
removing all geometry checks or claim a runtime performance improvement.
