# Vertex-stream setter replacement boundary

September 10, 2026. Added `82137410` to the local Ghidra trace and exported its
decompilation/pcode/instructions. Checked the generated body in recomp shard 80.
This is an implementation boundary, not a completed replacement.

Inputs: r3 device, r4 stream, r5 resource, r6 byte offset, r7 full byte stride,
r8 64-bit dirty mask. The native bridge already retains resource/offset/full
stride per device/stream but still calls the original setter first.

For a non-null new resource:

- Read header words at resource+24 and +28. Add offset to the first, subtract
  it from the second using uint32 arithmetic. The second word includes encoded
  control bits; it is not merely a raw byte length.
- Store an address word at device+1784-8*stream and the adjusted second word at
  device+1788-8*stream. Address conversion is `(address & 1fffffff) +
  (((address >> 20) + 512) & 1000)` for the adjusted first header word.
- OR the full input r8 into the 64-bit dirty state at device+16.

The old resource comes from device+12188+4*stream. Its fence/deferred-retirement
logic matches the index setter: old+8 receives device+10780 when nonzero;
otherwise intersecting device+10784/header flags append the eight-byte record,
growing through `82141440` when necessary. The preserved record tag bit comes
from entry SP-64 here, not the index setter's SP-48 (144 versus 128 byte frame).

Finally publish the new pointer to the stream slot. Store the low byte of
`stride >> 2` at device+12256+stream, including null-resource calls. If the full
shifted stride is nonzero and differs from the byte at device+11552+stream,
OR bit 51 into device+16. Comparing only truncated byte strides would change
behavior for large strides. Null-resource calls do not rewrite fetch descriptors
or OR input r8, but still process old-resource lifetime and stride state.

Replacement must preserve this order and keep full stride in native ownership.
Share the verified old-resource lifetime helper with index binding rather than
duplicating it. The retirement queue and remaining fetch/dirty-state consumers
are separate migration work; removing their writes without migrating consumers
would not establish a complete native interface. No checks are removed by this
audit, and no performance improvement is claimed.

## Implementation follow-up

`SetNativeStreamResource` now implements these effects directly. The native
`82137410` bridge no longer calls the original setter; it invokes the template,
then publishes the full resource/offset/stride into the native stream map.
Index and stream binding share `RetireNativeBoundResource` for old-resource
fence/queue effects. Queue growth still calls `82141440` with the original
144-byte guest frame depth and LR; the legacy tag uses entry SP-64.

`GuestReader::StoreByte` checks the containing aligned word for writable access
and writes only the requested byte, supporting non-word-aligned stream stride
slots without weakening the existing checked-write alignment contract. Invalid
stream indices >=16 fail before any descriptor or resource mutation.

The production stream template tests cover slots 0, 1 and 15; null/non-null
resources; zero, matching, mismatching and 1024/2048-byte strides; exact descriptor
words; a high-bit 64-bit input dirty mask; first-use, active-fence and growing
deferred queues; retirement-before-binding order; and preservation of all adjacent
stride bytes. Existing index tests exercise the shared lifetime helper as well.
The full native build and 21/21 tests passed (12.24 seconds).

This build has not been live-tested. Tests substitute guest memory/queue services,
so the native wrapper's actual stack integration and real queue growth still need
runtime/differential coverage. Guest fetch/dirty fields, queue storage/consumers,
payload pools and per-draw geometry checks remain. No new synchronization or
measured performance benefit is claimed.

## Live follow-up

Run `out/native-bridge-run/binding-validation-20260910-203527-fed0b2da/` used
SHA256 `15E6A7F4D8C894F454EB0D9CF8D876FB8CDE3E24829EAA942579FEC226C9E80B`
and a fresh profile. Inspected `gameplay.png` shows the player/HUD with AF14
120/120 under the mothership. At 20:37:28.190 indexed telemetry reports
2,290,000 submissions, 678 builds, 339 resident entries and zero upload errors
or vertex/index mismatches. No error/critical rows were found. This is a bounded
startup/mission-scene regression check, not full mission, combat, all stream
indices, queue growth or performance coverage. Brightness fidelity is not proved.
Owned PID 56008 (start 20:35:27) was stopped after exact identity verification;
WaitForExit returned true and process absence was checked. Artifacts remain.

The next queue-lifecycle boundary is recorded in
[native-retirement-queue.md](native-retirement-queue.md).
