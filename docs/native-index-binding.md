# Native index binding

September 10, 2026. Ghidra seeds `821375C0`, `82141440`, `82141AB8` were added
and exported through the existing headless project. Generated instructions and
Ghidra agree on the setter's CPU effects:

1. Read the previous index resource at device+12164.
2. If nonzero and device+10780 is nonzero, write that fence to old resource+8.
3. Otherwise, when device+10784 intersects the old header flags, append an
   eight-byte retirement record using the queue cursor/end at device+13148/+13152.
   Full queues invoke `82141440`; the record contains old_resource>>2, a preserved
   bit 31, and a second word of `ffffffff`. Advance the cursor by eight.
4. Store the new resource in device+12164, even when it equals the previous one.

The preserved bit is read from original entry SP-48 (SP+80 in the setter's
128-byte frame). Its semantics are not established. The native adapter keeps it
instead of inventing a zero value. Queue allocation executes with the original
guest stack depth and LR, so its nested frames do not consume this caller slot.
The general queue allocator and consumers remain recompiled dependencies.
`82141AB8` is a cache-line flush/sync helper, not a vertex/index payload writer.

`SetNativeIndexResource` now performs these CPU effects directly; the native
bridge no longer calls original `821375C0` when enabled. The host binding map is
updated afterward, retaining the existing submission/state locking boundary.
Malformed queue spans fail before writing a record or the new binding. The
bridge-disabled diagnostic route remains original. This is not removal of guest
retirement storage, binding-slot reads, geometry comparisons, or all graphics
bookkeeping, and it is not a new concurrency guarantee.

The production template tests cover missing old resources, active fence updates,
masked-out retirement, in-capacity/full queue writes, both preserved flag values,
and null/replacement/same-resource binding. They check records, cursors and
callback order. Guest memory and queue allocation are fixtures: real allocator
behavior, native wrapper stack integration, queue-consumer equivalence and live
performance remain unverified for this change. Full native build and 21/21 CTest
passed; this index-setter build has not been live-tested yet.

## Live follow-up

Run `out/native-bridge-run/binding-validation-20260910-202913-43a0504f/` used
the index-setter build SHA256
`8C4ACFFE1B8B564CB952C05C71007223F19CC76C45646671AF119D6E18D24A10`.
The inspected `mission.png` is the loading screen, not gameplay; the subsequent
`gameplay.png` shows the player, NPCs, HUD and AF14 120/120 after loading.
The cleanup counter reached 4096 completed releases at 20:31:05.313. Sampled
cleanup rows were unbound, so they do not prove the bound-unbind branch.
At 20:31:19.723 indexed telemetry showed 2,916,000 submissions, 718 builds,
379 resident entries, zero upload errors and zero vertex/index mismatches.
No error/critical rows were found in the inspected log.

This is a bounded startup/loading/gameplay regression check with no movement,
fire or complete mission, not retirement-branch coverage or an FPS benchmark.
The native index path was enabled, but fence versus deferred-queue branch usage
was not separately counted. Exact owned PID 49736 (start 20:29:13) was stopped;
WaitForExit returned true and process absence was verified. Artifacts remain.

The next setter's audited CPU effects are in
[native-stream-binding.md](native-stream-binding.md).
