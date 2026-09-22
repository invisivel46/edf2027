# Native shader-cache completion

Follow-up: [native indexed completion](renderer-native-indexed-completion.md)
replaces the enclosing packet-free indexed orchestration.

`src/native_graphics/native_shader_cache.cpp` replaces the extracted PPC cache
bookkeeping body in the native draw chain. CMake compiles the authored C++ into
both the game and the differential fixture. The existing
`edf_native_shader_cache_cpu_tail` ABI name is retained for its callers; its body
no longer executes generated EB68 code or calls the retail shader patcher.
The old extractor remains available as historical reference and is not a build
input. Retail `sub_8213EB68` remains available for unsupported rendering.

## Ownership and scope

Native CPU completion now owns these effects on its packet-free path:

- Compare the declaration ID and two masked 64-bit signatures.
- Reject mutation while the record's last-use fence is still busy, using unsigned
  32-bit modular distance comparisons. Rejection writes no cache metadata.
- Allocate a declaration ID on a refresh with a zero ID, sharing the guest's
  atomic counter and global critical region. Skip zero and `UINT32_MAX`, including
  wraparound; preserve big-endian memory representation.
- Publish the declaration ID and signatures into the program variant record on
  refresh. Update record+64 from the current guest fence and device+11552/+11560
  from the current signatures on every successful completion, including hits.

The native implementation deliberately shares guest metadata with fallback.
It creates no host cache keyed by a reusable guest address. Existing resource
producers still own allocation, retirement, and reinitialization. Reused-address
tests initialize a new declaration ID and signatures at the same addresses; they
do not claim that untracked destruction or concurrent mutation is safe.

Fence ownership is unchanged: this helper reads current/completed guest fence
values and records use. It does **not** advance completion or substitute a host
GPU fence. The existing submission/completion producer remains responsible for
those values. The native helper therefore removes the extracted consumer while
preserving the protocol with unsupported draws.

Read/store order is retained where later reads may see earlier metadata stores.
Xbox microcode patching and its destination-address calculations are absent on
this already packet-free path. The wrapper preserves stack, link register and
nonvolatile registers, and returns the success bit in r3. Volatile register
scratch and guest stack scratch contents are not outputs of this native ABI.

## Validation

`native_immediate_tail_tests` compares the new implementation with original
EB68 execution using the existing controlled packet-patch boundary. Existing
tests cover atomic ID allocation at zero, ordinary values and wraparound, and the
full main-state matrix through real cache hit/refresh/busy paths. The static-group
fixture now reaches the authored implementation through the real indexed chain.

Added 64 direct lifecycle comparisons cover both program variants, masked and
unmasked signature changes, fence equality, wraparound, busy rejection, repeated
native calls, a retail fallback call after native completion, and reinitialized
metadata at reused addresses. Each step compares bytes `[0,0xe0000)` and return/
nonvolatile ABI state. Atomic tests also compare the global counter. Guest stack
scratch is outside this projection. All 192 deliberate changes to record+64 or
either cached signature must be rejected by the comparison.

Validation commands (VS initialized by the wrappers):

```text
cmd /c out\static-group-focused.cmd
cmd /c out\static-group-integration.cmd
```

Exact results and source hashes: `out/shader-cache-result.json`; detailed logs:
`out/shader-cache-focused.log`, `out/shader-cache-test-details.log`, and
`out/shader-cache-integration.log`. No game boot is needed for these comparisons.
Epistemic session: `session-20260922191641-4f4d4aca`.

## Remaining static-group work

The previously observed shader-cache completion effects now have an authored
native owner. This is one native-chain consumer replacement, not closure of the
entire static group or the original EB68 function's microcode behavior. Geometry
setup, material activation, and the remaining indexed/main-state CPU consumers
still require their own ownership and zero-call checks. Continue using the shared
group differential and retained WARP replay for those changes. Source-pinned atlas
and planning snapshots remain historical until refreshed; do not automatically
promote their old review results to this revision.
