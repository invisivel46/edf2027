# First implementation packet: one complete retained static group

Execution update: see [current completion evidence](renderer-static-group-completion-progress.md).
The indexed/cache CPU chain and configured geometry/material effects are now
authored and tested. The packet remains open for integrated CPU/GPU replay,
supported-configuration eligibility, remaining partial-failure/lifetime checks
and milestone validation. Earlier handoff results remain historical evidence.

Machine-readable scope: `out/renderer-replacement/first-task.json`.
Parent: P03 / R04.pass. **Ready to start fixture work; production replacement is
gated on the scoped ownership and parity checks below.** This does not require
finishing all P01/P02 work or classifying the entire function census.

## Outcome

For one declared supported opaque static-group configuration, geometry, material
and world data reach native GPU output from retained inputs, while required CPU
state and resource lifetime effects have explicit native/producer owners.
Legacy setup, material activation and draw-consumer calls for that group reach
zero. Unsupported configurations keep a counted, explicit fallback.

Start with the existing replay's indexed opaque geometry and retained material
updates. Keep selection fixed and exclude mutation callbacks from this first
configuration; their existing tasks remain open. This is a bounded vertical
slice, not a declaration that all static rendering is complete.

## Evidence and starting points

- `src/native_graphics/guest_shader_bridge.cpp:3375`: geometry/material handoff,
  activation, world restoration and the indexed CPU tail. Inspect the second
  indexed handoff at line 3540 as well; preserve both paths.
- `src/native_graphics/native_scene_handoff.h`: pending/install/draw/consume
  behavior. A group with no accepted native draw must not consume dirty state.
- `tests/native_immediate_tail_tests.cpp:1746`: packet-free dirty-bank tests.
  At line 1780, original indexed execution is compared with the real extracted
  CPU helper chain, with explicit command-buffer/cursor/stack exclusions.
- `tests/native_static_pass_replay_tests.cpp`: independently specified output
  images, retained publications, replay order and lifetime checks.
- `out/renderer-inventory/static-pass-contracts.csv`: contracts for
  `sub_821D96D8`, `sub_821B94E8`, `sub_821B8E48` and their remaining obligations.
- The packet contains the exact 19-function direct dependency slice, six native
  boundary cuts and 24 external targets. External ABI helpers and unresolved
  dispatch remain explicit obligations, not additional port assignments.

## Ownership

The implementer owns tests in `native_immediate_tail_tests.cpp`,
`native_static_pass_replay_tests.cpp`, `native_scene_tests.cpp`, and the bounded
handoff helper in `native_scene_handoff.h`. One integrator owns changes in
`guest_shader_bridge.cpp` and `CMakeLists.txt`.

Do not edit generated guest bodies or generated adapter copies. Reuse the
existing extraction and differential-test machinery. Do not build a second
instruction interpreter or restart scalar-function research.

## Ordered work

1. Reproduce the existing indexed differential test and static replay. Read the
   actual device/shader/cache ranges compared by the indexed fixture and its
   explicit exclusions. Record this baseline before changing behavior.
2. Expose the existing static-group handoff orchestration as a small injectable
   helper used by the real bridge and the test. Preserve callback ordering,
   exception behavior and pending-state transitions exactly. Do not create a
   test-only imitation of the bridge path.
3. Connect that helper to a fixture that records geometry installation, material
   activation, world restoration, indexed-tail invocation and native submission.
   Exercise: no accepted draw, one accepted draw, several draws, material change,
   fallback before the first draw, fallback after a draw, and a throwing callback.
   Current baseline calls must be visible in the trace.
4. For the declared supported configuration, use original/extracted execution to
   identify required non-packet writes and their consumers. Assign each write to
   retained publication, native group execution, or explicit producer completion.
   Carry retirement/binding/default ordering from existing tests. An unassigned
   write is a blocking contract question, not permission to delete it.
5. Implement those owned effects for that configuration, then remove the legacy
   consumer calls only on its fully defined path. The same fixture must now fail
   if legacy setup, activation or indexed draw consumption occurs. Do not add a
   flag that claims ownership without implementing the effects behind it.
6. Run the group through retained replay on both WARP backends. Compare independently
   specified pixels and the justified CPU-state projection. Replay the same
   generation twice and out of order; release/reuse source resources and verify
   retained frames remain valid. Keep other modes on their explicit fallback.

## Completion tests

- The fixture calls the production handoff helper and real relevant CPU helper
  chain; a backend-only replay is insufficient evidence about guest ingestion.
- Geometry installation precedes activation; final world state follows activation;
  required draw-state consumption happens exactly when owed and is retry-safe.
- The selected supported group has zero guarded legacy consumer calls, matching
  expected pixels and required non-packet state. Change one expected ordering,
  CPU word and pixel in negative controls; each must fail.
- Retirement, partial failure, retained lifetime and address reuse remain correct.
- Existing unsupported-group fallback remains counted; unexercised feature/mode
  claims remain open.

## Validation commands

In an initialized VS developer shell, using the existing configured build:

```powershell
cmake --build out/build/win-amd64-release --target edf_native_immediate_tail_tests edf_native_scene_tests edf_native_static_pass_replay_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^(edf_native_immediate_tail_tests|edf_native_scene_tests|edf_native_static_pass_replay_tests)$' --output-on-failure
.\validate-renderer-offline.cmd
```

The immediate-tail target, including integrated CPU/GPU group replay, is now in
the default 27-suite gate. The focused command remains useful during iteration.
No mission boot during iteration. A later static-pass integration milestone must
exercise applicable gameplay scenarios before broader feature closure.

## Stop and report precisely

If the selected configuration reaches a new indirect target, requires an
unowned CPU write or lacks a valid resource precondition, record the exact
instruction/site, input case and missing owner contract. Keep its legacy path
intact and emit the smallest failing fixture. Deliver the tests and evidence;
do not report the production replacement complete.

Deliver changed code/tests, a before/after call-and-write trace, exact commands
and outcomes, source/fixture hashes, and Epistemic evidence IDs. Update only the
scoped contract status supported by those results.
