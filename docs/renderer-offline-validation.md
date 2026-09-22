# Offline renderer validation

From the repository root in PowerShell:

```powershell
.\validate-renderer-offline.cmd
```

This is the default regression gate for renderer changes. It initializes the
Visual Studio compiler environment, reconfigures the **existing** CMake cache,
incrementally builds 33 test targets, and runs them serially. It preserves the
configured SDK. It does not build or launch `edf2027`, open a game window, read
disc data, or require a save game. D3D11/D3D12 WARP and the Windows graphics
development environment are required for the offscreen tests. Missing graphics
support is a failure, not a skipped pass.

The default build directory is `out/build/win-amd64-release`. For another
configured build, pass `--build-dir <path>`. Set `VSDEVCMD` if Visual Studio is
installed somewhere other than the location used by `build.cmd`. In an already
initialized developer shell, `python tools/validate-renderer-offline.py` runs the
same gate directly. A fresh checkout needs the normal SDK configuration first:

```powershell
cmake --preset win-amd64-release -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=<your-rexglue-sdk>
```

Every selected target is rebuilt before testing. Exit zero requires every
selected CTest name to exist and every result to pass; missing, disabled,
skipped, timed-out or failing tests cannot yield success. Tests have a default
120-second timeout. The runner stops on configuration/build failures. Reports,
command logs, fixture SHA-256, revision/worktree status, timings and JUnit results
are saved under `out/renderer-offline/<UTC-run>/`. The printed `report.json` path
is the evidence to attach to a task. Build failures also produce a failed report.

## Existing suites wrapped

`--suite cpu` runs 26 suites: scalar effect rules, three-way scalar execution,
scalar analysis rules, original scalar getter contracts,
setter analysis rules, original scalar setter contracts,
frame dispatch, shader binding, material render
state, material samplers, capture policy, pacing, scripted input reload, backend
abstraction, upload ring, decode workers, effects, display gamma, worker callback
audit, model constructor orchestration, bucket dispatch, map effects, A/B
alternation, and the Python runtime-gate, image-compare and A/B-capture tool
tests (CTest wrappers over synthetic logs/images). Some use extracted production code
with synthetic/stub dependencies; they do not execute retail callbacks.

`--suite render` runs seven suites: native scene, backend completion, backend UI,
backend compositor, backend conformance, static-pass replay, and the original-vs-native
CPU differential with integrated retained-group GPU replay. The last suite uses
the actual geometry/material/indexed CPU helpers and submits the same fixture's
guest-derived inputs on both WARP backends. It adds about 45 seconds. These
use offscreen graphics resources. The default `--suite all` runs both groups.
Focused groups are useful while iterating; their reports explicitly identify the
reduced scope. The allowlist lives in `tools/renderer_offline_suites.py`, shared
with dispatch acceptance. Reports bind source content before and after validation;
the gate fails if source changes during its run.

For iteration, `--quick --since <git-ref>` runs only the suites whose mapped
files (`SOURCES` in `tools/renderer_offline_suites.py`: each suite's test source
and the tool or fixture it alone exercises) changed between the ref and the
working tree, untracked files included. Changes under `docs/`, `knowledge/` or to
`.md` files select nothing; any other unmapped change (shared `src/`, CMake,
generated code, shared fixtures) selects every suite. The report records
`suite: "quick"` and the changed files, so dispatch acceptance still rejects it;
run the full gate before submitting.

Window/presentation tests, SDK-host integration, guest-memory and extracted
immediate/present-tail suites, asset checkers and other legacy render tests are
not included in this initial gate. Continue running applicable task-specific
tests. Adding a new suite requires adding its build target/CTest name to the
allowlist after confirming that it is bounded and needs no game boot.

## Static-pass replay v1

`tests/fixtures/static-pass-replay-v1.txt` is a synthetic, versioned event stream.
It runs end to end **from the native scene input boundary to GPU readback**:

1. Decode big-endian indexed quad geometry through `NativeIndexedMesh`, retain
   the draw through `NativeSceneAdapter`, and discard the input bytes/mesh wrapper.
2. Observe two overlapping colored objects and publish their initial scene.
3. Update one object's transform/material, then retire it and reuse its owner
   address with a new generation. Check stable IDs on update and distinct IDs
   across lifetimes. Publish the changed scenes and an empty scene.
4. Destroy the producer and local geometry reference. Replay all four retained
   publications out of order, including the initial one twice, on D3D11 and
   D3D12 WARP. Each pass clears, binds, draws, submits and reads back a 64x32 target.
5. Compare every RGBA byte against independently specified integer rectangles.
   Check visible/draw counts and backend validation messages. Release all
   publications and require geometry/material weak references to expire.

The oracle does not call renderer matrix/raster code or derive expected output
from another backend. Both backends must match the same expected images. An
internal negative control changes one byte and verifies that it is rejected.
On image failure, actual, expected and difference PPMs are written to
`<build-dir>/static-pass-replay-artifacts/`, named by backend and publication.
The failure reports its fixture line and differing pixel count. JUnit captures
the successful frame/count output as well.

The fixture exercises opaque static geometry, world/camera constant binding,
material updates, draw order and retained publication lifetimes. It is not a
retail capture and does not prove guest-hook ingestion, texture lifetimes,
skeletal animation, effects, shadows, presentation, or full gameplay parity.
The existing suites cover additional bounded contracts, not every reachable
game mode. Future fixtures should add independently justified expected output;
do not regenerate expected images automatically from a changed renderer.

## Per-change and milestone policy

Use this command plus new cases for the changed contract on ordinary changes.
Build the game when integration/linkage needs checking, without launching it.
Reserve game boots for milestones that integrate a renderer family, change the
guest-to-native boundary, or change presentation/loading/resource teardown.
At those milestones run the applicable scenarios in
`renderer-coverage-runtime.md`, checking paths absent from offline fixtures.
A green offline report closes only its tested contract; runtime-only acceptance
cases and unknown boundaries remain open in the coverage audit and Epistemic.
