# Native model-buffer construction

September 10, 2026. Scope: model subset constructors `821D7530` (VB) and
`821D76A8` (uint16 IB), reached from `821B3C98`. This does not certify later
payload immutability or all geometry writers.

## Ghidra evidence

Expanded `tools/ghidra/GeometryWriterTrace.java` with `822CFDC0`, `822CFE58`,
and `82134190`, then reran the local headless project successfully. Exports
under `out/ghidra/geometry-writers-inline/` include decompilation, pcode and
instructions. Existing unrelated incomplete-control-flow warnings remain.
Generated functions were also inspected for exact argument and store semantics.

Both model constructors perform cleanup, pool allocation at owner+32, and a
payload copy into the pointer at owner+48. Only afterward do they build the
32-byte resource header and relocate its data-address field. Header writers do
not store through that data pointer on these constructor paths.

- VB calls `822CFDC0(bytes,0,0,0,owner)`: words 0/4 become 1; word 20 is
  `ffff0000`; word 24 starts at 3; word 28 is `(bytes & 03fffffc)|10000002`.
- IB calls `822CFE58(bytes,0,1,0,0,owner)`: word 0 is `20000002`, word 4 is 1,
  word 20 is `ffff0000`, word 24 starts at zero, and word 28 is the byte count.
- `82134190` returns the low type nibble for these headers (1 or 2).
  `822D01C0` therefore only adds the payload address at word 24, preserving
  the VB low two bits. Its texture branches are not reached by these headers.
- Remaining constructor stores set owner+56/+60/+64 for VB stride/count/bytes;
  IB sets owner+56 count. Both set byte owner+52 to 1 and return 1.

## Replacement

The native bridge now orchestrates these two model constructors directly.
It retains cleanup hooks, the engine pool allocator, the observed bulk copy,
and publication into the native model registry. The fixed constructor header
contract is emitted by `NativeModelHeader`; no original `822CFDC0`, `822CFE58`
or `822D01C0` call remains in the native model-construction path. General
resource/texture callers of those routines are not replaced by this change.

Guest helper calls use a local PPC context, preserving caller nonvolatile state
and LR. Their diagnostic LR values match the original constructor callsites.
The engine still owns a CPU-visible pool allocation: removing that allocation
requires migrating its remaining consumers/writers, not just native D3D11
storage. Native publication and live geometry checks are unchanged. The existing
bridge-disabled diagnostic route still calls the original constructors.

Tests cover exact fixed-contract VB/IB header words over several sizes and
invalid address/extent rejection. The full native build and 20/20 tests passed
(12.94 seconds). These tests do not execute the full retail constructor for
differential equivalence, allocator failure, aliasing or concurrent lifetimes.

The first live candidate exited during startup. A bounded first-eight constructor
trace localized this to after payload copying. The one-byte constructed-flag
write had requested alignment 1 from `GuestReader::WritableBytes`, whose contract
only accepts 4 or 8. Corrected the request to alignment 4 at the already aligned
owner+52 address; the write itself is still exactly one byte. This was a runtime
integration failure not covered by the header-value tests. The corrected run
passed that point and reached XUI/menu rendering. Full retail-constructor and
checked-write adapter tests remain necessary beyond these arithmetic tests.

Corrected candidate SHA256:
`7EBE1AA7B3AFFB8056193997AA5CD9DE2C022D7635EE8C04934D538CE37973A7`.
Run `out/native-bridge-run/binding-validation-20260910-201053-7e03f240/`
reached the Mission 1 mothership introduction (`mission.png`, inspected). The
last sample at 20:12:28.141 reports 1,481,000 indexed submissions, 332 builds /
resident entries, zero upload errors and zero vertex/index mismatches. No
error/critical rows were found. Overbrightness remains visible. This validates
a bounded constructor/loading/rendering path, not combat, all writers, complete
lifetimes, or performance. The 20-test suite also passed again (12.20 seconds).
PID 49088 was stopped after exact path/start-time verification (20:10:53);
WaitForExit returned true and the process was absent. Artifacts were retained.

## Constructor orchestration regression test

`edf_native_model_constructor_tests` now extracts the actual production
`ConstructNativeModelBuffer` body at build time. Fixture guest calls verify
cleanup/allocation/copy/publication order, arguments, and diagnostic LR values.
They clobber their local context to verify the caller's arguments, LR and a
nonvolatile sentinel remain unchanged (except successful return r3=1).
The checked-write fixture mirrors the 4/8 alignment contract that exposed the
startup bug, and checks the constructed flag does not overwrite its three
adjacent bytes. Publication asserts payload/header/metadata are complete.

Both VB and IB exercise zero/nonzero counts, injected allocation/copy/publication
exceptions, and invalid owner/stride/overflow inputs rejected before cleanup.
The fixture tests orchestration, not the SDK's real memory-permission checks,
the retail allocator's behavior, full PPC ABI preservation or concurrent writes.
No rollback guarantee is claimed for exceptions after allocation. Full retail
differential testing remains outstanding. All 21 CTest tests passed after adding
this coverage (12.27 seconds); game implementation is unchanged in this follow-up.

## Native cleanup follow-up

Added Ghidra seeds `82134840`, `82134AE8`, `821395C8`, `821D3EA0` and reran
headless analysis. Generated instructions confirm the model-specific branches:
the device getter follows `*(*82000720)` and increments device+52; VB binding
checks scan 16 words starting device+12188, while IB checks device+12164.
VB cleanup calls SetStreamSource for stream zero if any of those slots match,
not for the matching slot. The replacement preserves this unusual retail behavior.

`CleanupNativeModelResource` now implements constructed-flag handling, the
device-reference increment, model-specific binding checks, unbind-before-release,
and metadata clearing. The bridge retires native cache/registry ownership first,
then calls it with the existing stream/index setter hooks and CPU pool release.
It no longer executes original model cleanup, device getter, generic bound-resource
query, or reference-increment helpers on the native path. Setter internals and
the pool allocator remain recompiled, and the binding check still reads guest
CPU slots. Unexpected constructed-resource types or a null device fail explicitly
rather than attempting to treat another resource class as a model buffer.

The production cleanup template is exercised with constructed/unconstructed,
bound/unbound VB and IB fixtures, first/last stream slots, reference-count wrap,
callback ordering, and flag-byte/metadata extent checks. Guest services are
fixtures; real setter/allocator lifetime and concurrent cleanup are not proved.
Full game build and 21/21 tests pass (12.27 seconds). This cleanup build has not
yet been live-tested; the earlier Mission 1 capture predates this change.

### Live constructed-resource cleanup

Added bounded completed-cleanup telemetry (first eight releases, then powers of
two), logging only after pool release and metadata clearing return. Rebuilt the
full target and passed all 21 tests again (12.30 seconds).

Run `out/native-bridge-run/binding-validation-20260910-202123-15cd8883/`, SHA256
`31B54A77E214BF394B4FF25C1EA3E468A281D179CA7037EB7304F0C9D232D636`,
used a fresh profile. At 20:23:14.684 the log records completed constructed IB
and VB releases; by 20:23:14.702 it reaches the 4096-release checkpoint. All
sampled rows have `unbound=false`, so this is not live coverage of the bound
unbind branch. The inspected `mission.png` shows third-person gameplay, HUD,
AF14 120/120 ammunition and NPCs after the introduction, not just a menu or
cutscene. No scripted movement/fire or full-mission completion was tested.

At 20:23:56.731, indexed telemetry reports 6,852,000 submissions, 718 builds,
379 resident entries and zero upload errors or vertex/index mismatches. No
error/critical rows were found in the inspected log. The image has a dark
gameplay scene; it is not a matched-reference fidelity assessment or evidence
that the separate overbright intro issue is resolved. No FPS claim follows from
this hidden diagnostic. The exact owned PID 51240 (start 20:21:23) was stopped,
WaitForExit returned true, and process absence was verified. Artifacts remain.
