# Retained static group: handoff fixture and cutover blocker

Follow-up: [native shader-cache completion](renderer-native-shader-cache.md)
implements the cache effects identified below. The report below records the
earlier handoff baseline; full static-group cutover remains open.

The first packet now has a production handoff helper, a CPU differential fixture
and retained GPU replay through that helper. **Zero-legacy cutover remains open.**
The compatibility calls remain necessary until their CPU/cache completion has an
owner. This result does not close P03 or count any whole function as replaced.

## Implemented and exercised

`FinishNativeSceneGroupHandoff` in `native_scene_handoff.h` is called by the real
`sub_821D96D8` bridge boundary and both test fixtures. It preserves synchronization,
native flush, geometry installation, material/world restoration, indexed CPU
completion and invalidation order. The immediate nondeferred indexed path remains
unchanged. Existing pending flags and callback exception behavior are preserved.

The immediate-tail suite executes 24 combinations: zero/one/three accepted draws,
unchanged/changed material, group end/unsupported fallback, and successful/throwing
tail (the throw is applicable only when a draw was accepted). Geometry installation
uses the original declaration setter. Completion uses the real extracted indexed
CPU chain, compared with original indexed execution over bytes `[0,0xe0000)`;
only command cursor `[device+40,device+44)`, packet storage `[0x40000,0x40800)` and
the stack above that projection are excluded. Submission and material activation
are controlled callbacks, not whole guest material ingestion. Repeated finishing
must not repeat installation, activation or successful completion.

Trace for an accepted draw with changed material:
`synchronize -> native submit -> declaration install -> activation -> world restore
-> indexed CPU tail -> invalidate`. With no accepted draw, the tail is absent.
A tail failure retries synchronization/submission and the tail, without repeating
successful installation or activation. The submission callback models an already
drained queue. Fallback is recorded after handoff; the unsupported guest draw itself
is not executed by this fixture. Deliberately changed order and CPU words must be
rejected. GPU replay separately rejects a changed pixel.

Both WARP backends render retained generations through the helper, including
repetition, out-of-order replay, retirement, address reuse and producer release.
Its CPU callbacks trace ordering; CPU parity is established by the separate
indexed fixture. Neither fixture claims end-to-end guest material retirement.

## Concrete blocker: shader-cache completion

For `device=0x1000`, program `0x60000`, declaration `0x65000`, stride byte 7,
dirty bank 0 = 1, and one indexed draw, the observed 4-byte words changed by the
real indexed CPU chain are:

| Address | Effect | Current owner |
| --- | --- | --- |
| `0x1004` | Low half of dirty bank 0 cleared | Extracted indexed completion |
| `0x1010` | Declaration-induced bit in dirty bank 16 cleared | Extracted indexed completion |
| `0x3d20` | Device+11552 cached signature copied from device+12256 | Extracted shader-cache completion |
| `0x601e0` | Program variant 1 record+64 receives device+10780 (200) | Extracted shader-cache completion |

`sub_8213EB68`, reached from the main-state CPU chain, performs the record+64
store at `0x8213EC94`, followed by device+11552/+11560 stores at
`0x8213ECA0`/`0x8213ECA4`. Its miss path at `0x8213EBD0` reads record+64 and
compares against current/completed fence values before allowing cache mutation.
Thus the retained GPU submission is insufficient evidence for deleting these
writes. The fixture records the actual before-tail snapshot and tests omitted
dirty completion as a negative control. The full non-packet comparison also
includes the cache record and signature words.

Next bounded task: assign shader-cache completion to an explicit native owner.
Start at `sub_8213EB68` and its existing cache hit/miss/busy fixtures in
`native_immediate_tail_tests.cpp`. Define what record+64 means across native
submission, unsupported fallback, retirement and address reuse before replacing
it. Dependencies: retained resource generation identity and completion/fence
ownership (P01/P02). Completion test: original/native parity for cache hit, miss,
busy fence, repeated group, unsupported fallback and reused owner; negative
controls on record+64 and device+11552; then the existing group differential and
WARP replay with guarded zero legacy consumers. Do not replace these writes by
blindly clearing dirty banks or claiming native ownership with a flag.

## Validation and provenance

The first baseline failed to compile: the old fixture extractor had not tracked
new declaration/shader hook dependencies. It now explicitly selects compatibility
publication branches, uses a controlled reader and rejects unexpected native
activation. Native shader binding semantics remain covered by their separate suite.
After repair, all three focused suites passed. The new fixture initially failed
its stride precondition; initializing the existing stride contract fixed that
test setup. Final focused run passed all three suites in 41.10 seconds.

Commands: `cmd /c out\static-group-focused.cmd` and
`cmd /c out\static-group-integration.cmd` (VS initialization, game build, then
`validate-renderer-offline.cmd`). No game was booted.
Exact outputs, final integration status and source hashes are retained under
`out/static-group-*`; consult `out/static-group-result.json` for final status.
Epistemic session: `session-20260922190233-30ccc999`.

The atlas and replacement plan are historical source-pinned snapshots. This
bridge edit invalidates their current-source parity; their previous audit success
must not be presented as an audit of this revision. Regenerate source-derived
snapshots before the next dispatch, preserving prior evidence and review scope.
