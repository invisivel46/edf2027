# Renderer task execution guide

Generated from the authored playbooks. Read [the common workflow](renderer-task-workflow.md) before executing a task. The audit status and task IDs are preserved; detailed instructions are not new implementation evidence.

For a self-contained packet containing only one task and the common workflow:

```powershell
python tools/show-renderer-task.py R10.capture --output out/R10.capture-task.md
```

`python tools/show-renderer-task.py --list` lists task IDs. Add `--rows 5` and optional `--match 821A5158` to include a small matching boundary excerpt; samples do not limit task scope. Research inputs under `out/` must already exist or be rebuilt from the inventory workflow.

A useful standalone implementation starting point is R10.capture. R01.scope starts the bounded research path; follow its evidence into R01.callbacks/R09.effects. The full dependency graph remains in the catalog.

| Task | Kind | First bounded slice |
|---|---|---|
| [R01.scope](#r01-scope) | investigation | Classify one root-to-callee path starting at sub_821A5080. Select at most five functions from that path, not the first five unrelated CSV rows. |
| [R01.callbacks](#r01-callbacks) | investigation | Resolve the scene-begin callback at 821A5158 (manager+132, slot1) from registration through removal before tackling the other eight sites. |
| [R01.modes](#r01-modes) | investigation | Build the backend x recorded/direct-draw matrix first: D3D12, D3D11, WARP variants and empty backend. |
| [R02.publication](#r02-publication) | implementation | Document and test the owner+364 counter and owner+356 time writes in820B4250 before moving any operation out of rendering. |
| [R02.camera](#r02-camera) | implementation | Capture one player-view camera record and compare its view/projection bytes with the current path at a locked simulation tick. |
| [R03.selection](#r03-selection) | implementation | Select one static leaf containing two ordered objects and publish all inputs used to accept/reject those objects. |
| [R03.mutation](#r03-mutation) | implementation | Reproduce one callback that removes or changes the next object during leaf traversal. |
| [R04.pass](#r04-pass) | implementation | Render one retained static group including geometry, material and world transforms while counting every legacy setup/activation/tail call it makes. |
| [R05.pass](#r05-pass) | implementation | Describe one ordinary gameplay begin/end pair and the loading direct-scene end path before changing target inheritance. |
| [R05.post](#r05-post) | investigation-and-implementation | Identify one actual post technique and capture its input scene-color and output for the same frame. |
| [R06.pose](#r06-pose) | implementation | Build one rigid-model record and one two-bone skinned record; enumerate their original traversal/upload calls. |
| [R06.families](#r06-families) | investigation-and-implementation | Choose the soldier/player method sub_820DEA08 and one supported concrete receiver. Finish its route before selecting another family. |
| [R07.effects](#r07-effects) | investigation-and-implementation | Trace one muzzle-flash or particle method to geometry, sort key, material parameters and lifetime update. |
| [R07.environment](#r07-environment) | investigation-and-implementation | Choose one special method, preferably sky sub_820BB270 or broken-piece sub_82120168, and record how it differs from a static group. |
| [R07.shadows](#r07-shadows) | investigation-and-implementation | Identify one real shadow-producing technique/pass and its caster/receiver route; the initial slice is investigation only. |
| [R07.special](#r07-special) | investigation | Select one concrete class table pointing to8252B718; follow its constructor and other render-related slots. |
| [R08.ui](#r08-ui) | implementation | Publish and replay one HUD/text layer at a fixed camera/tick, recording its position in final composition. |
| [R08.loading](#r08-loading) | implementation | Reproduce the no-active-output loading branch and link its eligibility counter to an actually published and presented sequence. |
| [R08.movie](#r08-movie) | implementation | Trace one decoded movie frame from plane production through native conversion to the host frame. |
| [R08.present](#r08-present) | implementation | Trace one published frame through acquisition, GPU copy, Present and surface release, including its sequence IDs. |
| [R09.effects](#r09-effects) | investigation | Choose one explicit retained call required by R04.pass; review its complete connected CPU effect contract before selecting another callee. |
| [R09.resources](#r09-resources) | investigation-and-implementation | Choose one VB/IB owner and trace create, publish, update, retire and same-address replacement before reviewing other resource kinds. |
| [R09.retirement](#r09-retirement) | investigation-and-implementation | Follow one texture/shader unbind through immediate-fence tagging or the eight-byte retirement queue, including queue exhaustion. |
| [R09.shader](#r09-shader) | investigation-and-implementation | Add a fixture where an early constant write aliases a later defaults payload word; compare incremental original reads against the current collected-word parser. |
| [R09.dynamic](#r09-dynamic) | implementation | Trace one immediate quad or effect vertex buffer from producer write through upload-ring allocation and GPU completion. |
| [R09.sync](#r09-sync) | investigation-and-implementation | Choose one worker registration mode and follow its callback/context pair from writer to invocation and shutdown. |
| [R09.assets](#r09-assets) | investigation-and-implementation | Replay the existing Mission1 declaration fixture with the documented effect catalog, recording the exact corpus identity and accepted/rejected totals. |
| [R09.compat](#r09-compat) | investigation-and-implementation | Choose one ownership-predicate fallback and exercise both sides using a valid fixture for its exact guard. |
| [R10.trace](#r10-trace) | implementation | Instrument the outer callback at821A5158 with instruction/target/receiver and scenario identity, using a bounded collector. |
| [R10.capture](#r10-capture) | implementation | Extend the final-host capture policy to request exactly three images with distinct timestamps/sequence IDs; keep the existing one-shot behavior as the default. |
| [R10.input](#r10-input) | implementation | Add one wait-for-gameplay-ready action followed by a short movement event, while preserving the existing time-only script format. |
| [R10.scenarios](#r10-scenarios) | runtime-validation | Repeat S03/S04 using readiness, route attribution and complete-frame burst artifacts before selecting an unvisited scenario. |
| [R10.cadence](#r10-cadence) | implementation | Use a deterministic fixture with a60Hz producer and120Hz consumer rendering one complete retained publication without guest render callbacks. |
| [R10.performance](#r10-performance) | runtime-validation | Measure one fixed gameplay route on D3D12 with default settings and diagnostics/capture disabled; establish a reproducible baseline before changing one variable. |

<a id="r01-scope"></a>

## R01.scope: Classify the exported closure

**Kind:** investigation. **Audit status:** open.

**Goal:** For each assigned function/site, trace the seed-to-function path and its outgoing calls; label renderer consumer, producer, shared utility or unreachable for a named mode. Expand roots for newly found render targets. Scope labels need instruction/source evidence.

**Scope dependencies:** None.

**Prerequisite evidence for this slice:** No implementation prerequisite. The coverage audit must be current, and selected generated-code and Ghidra locators must exist.

**Start with:** Classify one root-to-callee path starting at sub_821A5080. Select at most five functions from that path, not the first five unrelated CSV rows.

**Read these files:**

- `out/renderer-inventory/complete-function-inventory.csv`
- `out/renderer-inventory/root-manifest.csv`
- `out/renderer-inventory/generated-call-edges.csv`
- `out/renderer-inventory/indirect-site-ledger.csv`

**Find the relevant code/evidence:**

```powershell
rg -n 'sub_821A5080|sub_820B4310|sub_820B4250' out/renderer-inventory/complete-function-inventory.csv out/renderer-inventory/root-manifest.csv out/renderer-inventory/generated-call-edges.csv out/renderer-inventory/indirect-site-ledger.csv
```

**Steps:**

1. Find the selected function in the function inventory; record root_reasons and incoming/outgoing direct edges in contract.csv.
2. Read the native hook and generated PPC body for each selected function. Use the existing Ghidra body as an aid, and check disputed control flow against emitted instructions.
3. For each branch, list render submission, simulation writes, resource operations and indirect targets. Assign renderer-consumer, producer, shared-utility or unresolved; a single function may have multiple roles.
4. Record the exact mode/guard for any exclusion. Add a scoped follow-up for unknown callees/targets, then move to the next connected slice. Leave the package open until all assigned rows have evidence.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- A shared allocator reached from a renderer is classified as a utility with its caller obligations retained.
- An indirect call with a plausible vtable candidate stays unresolved without receiver/registration proof.
- Every selected function has both an incoming-path witness and a documented outgoing-boundary disposition.

**Verification:** inspect the evidence rows against source and the cases above; run the coverage validator. No unrelated game build is required for a read-only result. Runtime-validation tasks additionally require their scenario/measurement artifacts.

**Task completion gate:** Every census function and indirect site has an evidenced role or an explicit follow-up; no utility is declared renderer work solely from reachability, and exclusions name the mode and proof.

**Do not:**

- Do not port the 9,216-function closure wholesale.
- Do not use class names, hook presence or absence of an __imp__ call as proof of ownership.

**If blocked or uncertain:** If a body is truncated or a target cannot be identified, save the last proven instruction/argument flow and the next precise lookup. Do not classify the path as unreachable.


<a id="r01-callbacks"></a>

## R01.callbacks: Close frame receiver populations

**Kind:** investigation. **Audit status:** open.

**Goal:** Resolve the eight 821A5080 sites and bucket slot 4 at 821A3C50 using registration/removal writers, receiver identity and verified vtables; check player/free/motion/base cameras and listeners.

**Scope dependencies:** R01.scope.

**Prerequisite evidence for this slice:** R01.scope must identify the selected callback as renderer-facing; full broad-closure triage is not required to investigate this site.

**Start with:** Resolve the scene-begin callback at 821A5158 (manager+132, slot1) from registration through removal before tackling the other eight sites.

**Read these files:**

- `src/native_graphics/native_frame_dispatch.h`
- `out/renderer-inventory/core-callback-candidates.csv`
- `out/renderer-inventory/startup-listener-registration.csv`
- `out/renderer-inventory/world-camera-factory-registration.csv`

**Find the relevant code/evidence:**

```powershell
rg -n '821A5158|821A51D8|821A5264|821A5290|821A52A8|821A52E4|821A52F8|821A5368|821A3C50' src/native_graphics/native_frame_dispatch.h out/renderer-inventory/core-callback-candidates.csv out/renderer-inventory/startup-listener-registration.csv out/renderer-inventory/world-camera-factory-registration.csv
```

**Steps:**

1. Record instruction, receiver expression, slot byte offset, argument values and candidate targets. Keep pointer verification separate from receiver membership.
2. Trace all discovered writers of the receiver/list field and constructor vtable installs. Add registration, mutation and removal locators to the same row.
3. Follow each candidate implementation through its render/producer boundary; retain mode guards for player, free, motion and base cameras and listeners.
4. Compare with observed targets when R10.trace data exists. An unobserved but reachable candidate remains in scope. Deliver a nine-site table and explicit unresolved receiver questions.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- All eight outer sites and bucket site821A3C50 have rows.
- Removal/re-registration and a changed receiver table cannot escape the candidate population silently.
- An unknown runtime target creates a new task row instead of being discarded.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_frame_dispatch_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_frame_dispatch_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Registration-to-call table covers all eight sites plus bucket dispatch; runtime target traces map to it; unknown targets fail coverage and get a task.

**Do not:**

- Do not replace the callback with a guessed class-specific target.
- Do not treat one Mission1 trace as proof that other modes cannot register another receiver.

**If blocked or uncertain:** If receiver registration is missing, retain original dispatch for that site and hand off the exact receiver field and candidate writer search.


<a id="r01-modes"></a>

## R01.modes: Declare supported and compatibility modes

**Kind:** investigation. **Audit status:** open.

**Goal:** Inventory startup/loading/gameplay/pause/results/movie, player counts, camera modes, backend choices, ownership toggles and diagnostic branches. Decide support or explicit unsupported behavior for each combination.

**Scope dependencies:** R01.callbacks.

**Prerequisite evidence for this slice:** Use current setting definitions and actual consumer guards. R01.callbacks supplies known camera/listener modes; leave unknown mode populations explicit.

**Start with:** Build the backend x recorded/direct-draw matrix first: D3D12, D3D11, WARP variants and empty backend.

**Read these files:**

- `out/renderer-coverage/configuration-modes.csv`
- `src/native_graphics/guest_shader_bridge.cpp`
- `README.md`
- `docs/renderer-coverage-runtime.md`

**Find the relevant code/evidence:**

```powershell
rg -n 'edf_native_scene_backend|edf_native_seam_draws|edf_native_frame_dispatch' out/renderer-coverage/configuration-modes.csv src/native_graphics/guest_shader_bridge.cpp README.md docs/renderer-coverage-runtime.md
```

**Steps:**

1. For each selected setting, read its definition, accepted range, restart requirement and every consumer guard; record literal default separately from accepted values.
2. List compatible and incompatible combinations using the source guards, starting with backend and seam_draws. Record the existing rejection/migration behavior.
3. Map supported combinations to scenario IDs and specific captures/tests. Mark unexecuted combinations untested, not unsupported.
4. Repeat for cadence, workers, credits, MSAA, resolution/canvas, camera/player modes and ownership/debug switches. Propose any support-policy change separately from observed behavior.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- D3D12 plus direct D3D11-only draws has an explicit disposition backed by its guard.
- Every one of the98 captured definitions has a value/guard/restart/test row.
- Unsupported is justified by source or an explicit product decision; an absent test is recorded as untested.

**Verification:** inspect the evidence rows against source and the cases above; run the coverage validator. No unrelated game build is required for a read-only result. Runtime-validation tasks additionally require their scenario/measurement artifacts.

**Task completion gate:** Mode matrix includes guards and fallback behavior; every supported branch has a scenario and every unsupported branch has a tested diagnostic.

**Do not:**

- Do not silently change defaults or remove compatibility modes while cataloging them.
- Do not enumerate the Cartesian product blindly; document guard-equivalent groups and pairwise choices.

**If blocked or uncertain:** If a requested support policy is not determined by source or existing project decisions, document the alternatives and impact; do not invent a supported/unsupported label.


<a id="r02-publication"></a>

## R02.publication: Separate simulation mutations from rendering

**Kind:** implementation. **Audit status:** open.

**Goal:** Split 820B4250 counter/time mutations and 821A4DE8 publication from render repeats. Publish camera, transforms, animation, effect clocks and resource generation together.

**Scope dependencies:** R01.callbacks, R09.effects.

**Prerequisite evidence for this slice:** R01.callbacks identifies the selected producer/render route; R09.effects supplies its writes, readers and ordering. Do not move a write whose consumers are unknown.

**Start with:** Document and test the owner+364 counter and owner+356 time writes in820B4250 before moving any operation out of rendering.

**Read these files:**

- `src/native_graphics/guest_shader_bridge.cpp`
- `src/native_graphics/native_scene_adapter.h`
- `src/native_graphics/native_scene_sources.h`
- `src/native_graphics/native_model_pose_history.h`
- `tests/native_scene_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n '820B4250|821A4DE8|Publish|generation' src/native_graphics/guest_shader_bridge.cpp src/native_graphics/native_scene_adapter.h src/native_graphics/native_scene_sources.h src/native_graphics/native_model_pose_history.h tests/native_scene_tests.cpp
```

**Steps:**

1. Record before/after simulation fields and the publication point after821A4DE8. Identify which thread owns each mutation and which resource generation the data refers to.
2. Define one immutable publication record with generation/tick ID and retained handles for the selected state. Specify how the consumer acquires it without combining generations.
3. Move only proven simulation updates to the producer tick; make repeated rendering consume the same retained record without advancing those updates.
4. Add repeat-render/new-tick tests before expanding the record to camera, animation and effects. Include a delayed consumer retaining the previous generation.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Rendering one generation10 times leaves selected counters/time and authoritative poses byte-identical.
- One new simulation tick applies the update once and publishes a new generation.
- A producer update while an old frame is pending does not invalidate that frame or mix its inputs.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_scene_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_scene_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Render a retained generation repeatedly: simulation counters, authoritative poses and resource lifetimes remain unchanged; one producer tick advances once.

**Do not:**

- Do not copy all live guest memory into a snapshot.
- Do not move updates based only on a function name such as RenderWorld.

**If blocked or uncertain:** If simulation versus rendering ownership cannot be distinguished, keep that update in its current owner and return an evidence-backed contract question.


<a id="r02-camera"></a>

## R02.camera: Own camera derivation and interpolation inputs

**Kind:** implementation. **Audit status:** open.

**Goal:** Replace live camera inputs around 821CDDF8 and 821BE8D0 with retained per-view projection/view/FOV/viewport values; preserve matrix rounding and source-pose restoration.

**Scope dependencies:** R02.publication.

**Prerequisite evidence for this slice:** R02.publication must provide a generation and retention contract for this view. Resolve dimensions/FOV and matrix operation order before replacing derivation.

**Start with:** Capture one player-view camera record and compare its view/projection bytes with the current path at a locked simulation tick.

**Read these files:**

- `src/native_graphics/guest_shader_bridge.cpp`
- `src/native_graphics/native_camera_history.h`
- `src/native_graphics/native_scene_pass_inputs.h`
- `tests/native_scene_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n '821CDDF8|821BE8D0|NativeCamera|projection' src/native_graphics/guest_shader_bridge.cpp src/native_graphics/native_camera_history.h src/native_graphics/native_scene_pass_inputs.h tests/native_scene_tests.cpp
```

**Steps:**

1. Trace view/projection/FOV/viewport producers and distinguish authoritative source pose from derived matrices.
2. Store the selected per-view camera inputs and derived values in the publication; use a view identity so a second view cannot overwrite the first.
3. Preserve audited floating-point operation order and scoped source-pose restoration. Add interpolation as a consumer operation over two retained generations.
4. Compare locked/unlocked, zoom and view changes against the reference path, recording any intentional numeric tolerance and its reason.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Repeated rendering does not alter authoritative camera pose/FOV.
- Two views with different projection/viewport retain separate values.
- Zoom, dimension changes and invalid dimensions follow an explicit policy and do not introduce silent NaNs or changed rounding.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_scene_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_scene_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Locked/unlocked movement, zoom, camera changes and dimension edge cases match reference matrices; simultaneous views never share camera state accidentally.

**Do not:**

- Do not recompute projection with an algebraically equivalent formula without checking float rounding.
- Do not import a live viewport after selecting an older camera generation.

**If blocked or uncertain:** If a camera mode lacks a known producer or identity, preserve its current path and leave that mode open.


<a id="r03-selection"></a>

## R03.selection: Finish immutable static selection

**Kind:** implementation. **Audit status:** open.

**Goal:** Publish hidden/mode flags, duplicate marks, LOD inputs, hierarchy and ordered membership together at 821C61D8/820B4038/821BEE68/821C3BB8; eliminate mixed live-generation selection.

**Scope dependencies:** R02.publication, R09.effects.

**Prerequisite evidence for this slice:** R02.publication supplies a retained generation; R09.effects identifies writers of hidden/mode/duplicate/LOD inputs for this leaf.

**Start with:** Select one static leaf containing two ordered objects and publish all inputs used to accept/reject those objects.

**Read these files:**

- `src/native_graphics/native_scene_tree_publication.h`
- `src/native_graphics/native_scene_membership.h`
- `src/native_graphics/native_scene_visibility.h`
- `src/native_graphics/native_scene_sources.h`
- `tests/native_scene_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n '821C61D8|820B4038|821BEE68|821C3BB8|hidden|duplicate' src/native_graphics/native_scene_tree_publication.h src/native_graphics/native_scene_membership.h src/native_graphics/native_scene_visibility.h src/native_graphics/native_scene_sources.h tests/native_scene_tests.cpp
```

**Steps:**

1. Read the current selector and list every guest-memory field it reads, including inherited flags and duplicate bookkeeping.
2. Trace writers for missing fields; add fields to the same publication as hierarchy and ordered membership, rather than fetching them later.
3. Run selection against only that retained generation; defer live mutations to a new generation through the established producer contract.
4. Add exact selected-ID/order comparisons and a guard that detects guest selection reads. Expand to hierarchy and LOD boundaries after the leaf fixture passes.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Hidden/visible, LOD threshold and duplicate cases return the expected IDs in the expected order.
- Remove/reinsert and same-address new lifetime do not select stale objects.
- Changing live guest fields after publication does not change selection of that publication.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_scene_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_scene_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Visibility, LOD, duplicate/order, removal, reinsert and address-reuse fixtures match the reference and audited gameplay without live selection reads.

**Do not:**

- Do not mix retained membership with current adapter records.
- Do not treat an empty scene or zero audit checks as a passing comparison.

**If blocked or uncertain:** If a required field has no known writer, keep that selection route explicit and request a writer investigation instead of reading it live inside the native consumer.


<a id="r03-mutation"></a>

## R03.mutation: Resolve mutation and unknown-object fallback

**Kind:** implementation. **Audit status:** open.

**Goal:** Enumerate 821C0C00 callback implementations and mutation writers; specify republish/defer behavior for every unsupported or mid-selection mutation route.

**Scope dependencies:** R01.callbacks, R03.selection.

**Prerequisite evidence for this slice:** R01.callbacks identifies the chosen target and mutation contract; R03.selection gives the immutable traversal behavior to preserve.

**Start with:** Reproduce one callback that removes or changes the next object during leaf traversal.

**Read these files:**

- `src/native_graphics/guest_shader_bridge.cpp`
- `src/native_graphics/native_scene_membership.h`
- `src/native_graphics/native_scene_tree_publication.h`
- `out/renderer-inventory/indirect-site-ledger.csv`
- `tests/native_scene_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n '821C0C00|Invalidate|mutation|fallback' src/native_graphics/guest_shader_bridge.cpp src/native_graphics/native_scene_membership.h src/native_graphics/native_scene_tree_publication.h out/renderer-inventory/indirect-site-ledger.csv tests/native_scene_tests.cpp
```

**Steps:**

1. Find the821C0C00 fallback and record when it invalidates, calls original code and refreshes live state.
2. Build a fixture for mutation during traversal: next removal, current removal, insertion or target replacement. Write the required order/visibility result before changing behavior.
3. Choose an explicit contract consistent with producers: retain the current generation, or abort/defer and republish. Keep guest-pointer validation boundaries around callbacks.
4. Replace only the proven live fallback route and expose a reason counter for any remaining unsupported route. Check reuse after removal.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Removing the next node neither dereferences freed memory nor skips an unrelated retained node.
- New insertions appear only in the defined generation/order.
- Unknown callbacks cannot silently restart traversal over mutable live links.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_scene_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_scene_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Adversarial callback/removal tests preserve order and retained lifetimes; fallback counts are attributed to a declared route and no silent live-list restart remains.

**Do not:**

- Do not continue with cached live pointers after a guest callback.
- Do not swallow an unsupported callback and count the object as rendered.

**If blocked or uncertain:** If callback mutation semantics differ by mode, retain separate explicit mode branches until each has evidence.


<a id="r04-pass"></a>

## R04.pass: Submit a complete retained static pass

**Kind:** implementation. **Audit status:** open.

**Goal:** Move required CPU setup/activation/indexed-tail effects at 821D96D8, 821B94E8 and 821B8E48 to explicit producer or native owners; submit geometry/material/world from retained inputs.

**Scope dependencies:** R03.mutation, R05.pass, R09.effects, R09.retirement.

**Prerequisite evidence for this slice:** Selected group has R03.mutation selection semantics, R05.pass inputs, R09.effects CPU-effect ownership and R09.retirement lifetime/queue contracts.

**Start with:** Render one retained static group including geometry, material and world transforms while counting every legacy setup/activation/tail call it makes.

**Read these files:**

- `src/native_graphics/guest_shader_bridge.cpp`
- `src/native_graphics/native_scene_handoff.h`
- `src/native_graphics/native_scene_material.h`
- `src/native_graphics/native_scene_pass_inputs.h`
- `out/renderer-inventory/static-pass-contracts.csv`
- `tests/native_scene_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n '821D96D8|821B94E8|821B8E48|indexed_cpu_tail' src/native_graphics/guest_shader_bridge.cpp src/native_graphics/native_scene_handoff.h src/native_graphics/native_scene_material.h src/native_graphics/native_scene_pass_inputs.h out/renderer-inventory/static-pass-contracts.csv tests/native_scene_tests.cpp
```

**Steps:**

1. Make a before/after table of every retained call for the group. Separate required CPU writes from packet emission and actual native submission.
2. Move required writes to their documented producer/native owner; keep ordering and retirement operations even when packet emission is removed.
3. Submit the group from retained handles and explicit pass inputs. Add a fixture guard that fails if legacy setup/activation/draw submission runs for the selected supported group.
4. Compare output and state with the reference, then expand to multiple materials and mixed static/animated ordering. Keep unsupported groups counted and assigned.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Nonempty supported static pass renders while guarded legacy consumer calls remain zero.
- A material boundary preserves inherited state needed by the following draw.
- Retiring a resource after publication does not break the in-flight frame; address reuse does not revive it.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_scene_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_scene_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_immediate_tail_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_immediate_tail_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Static pass succeeds with legacy setup, activation and draw submission disabled; mixed-content ordering and retirement remain correct on D3D11/D3D12.

**Do not:**

- Do not equate removing a draw call with removing its CPU side effects.
- Do not add a new ownership flag without implementing the complete selected boundary.

**If blocked or uncertain:** If a retained CPU write lacks an owner/reader contract, stop removing that call and return the exact missing effect to R09.effects.


<a id="r05-pass"></a>

## R05.pass: Own view, target and pass transitions

**Kind:** implementation. **Audit status:** open.

**Goal:** Specify begin/end, inherited target, clear, viewport, scissor, format, resolve and post-pass contracts, including 821BE8D0/821BE9D8 and tiled-to-untiled compatibility branches.

**Scope dependencies:** R02.camera, R09.resources.

**Prerequisite evidence for this slice:** R02.camera provides retained per-view inputs and R09.resources defines target identity/retention. Existing pass-contract rows remain partial until their remaining obligations are resolved.

**Start with:** Describe one ordinary gameplay begin/end pair and the loading direct-scene end path before changing target inheritance.

**Read these files:**

- `src/native_graphics/native_scene_pass_inputs.h`
- `src/native_graphics/guest_shader_bridge.cpp`
- `out/renderer-inventory/pass-format-contracts.csv`
- `out/renderer-inventory/untiled-boundary-contracts.csv`
- `tests/native_scene_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n '821BE8D0|821BE9D8|8219C5A8|8219C840|viewport|resolve' src/native_graphics/native_scene_pass_inputs.h src/native_graphics/guest_shader_bridge.cpp out/renderer-inventory/pass-format-contracts.csv out/renderer-inventory/untiled-boundary-contracts.csv tests/native_scene_tests.cpp
```

**Steps:**

1. Record target color/depth handles, formats, sample count, viewport, scissor, clear values, resolve source/destination and entry/exit CPU writes.
2. Create an explicit pass record that contains every currently inherited input and a defined output-state handoff to the next pass.
3. Apply begin/draw/end/resolve using that record. Preserve required producer dirty-state effects separately from removed rendering packets.
4. Add nested/offscreen and loading fixtures, then test resize/recreation with old pass records still retained.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Nested pass B returns to the required A target/state without reading unrelated current guest state.
- Loading with no active output still produces an eligible correctly owned direct-scene frame.
- MSAA resolve and resource recreation preserve matching extent/format/lifetime.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_scene_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_scene_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_backend_compositor_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_backend_compositor_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Nested/offscreen/multiple-view tests verify explicit target and state restoration; loading direct-scene output and normal post output both present.

**Do not:**

- Do not import the most recently bound target as the owner of an older pass.
- Do not restore Xbox tile replay as a compatibility shortcut.

**If blocked or uncertain:** If begin/end ownership differs between direct-scene and normal output, document both routes and leave the unproven route open.


<a id="r05-post"></a>

## R05.post: Validate post-processing and output transforms

**Kind:** investigation-and-implementation. **Audit status:** open.

**Goal:** Enumerate techniques that implement tone mapping, exposure/bloom-like passes, fog/environment and output gamma; document which are actually present, with exact pass identities.

**Scope dependencies:** R05.pass, R09.assets.

**Prerequisite evidence for this slice:** R05.pass gives target order/lifetime; R09.assets gives the selected effect identity and reflected parameters. A descriptive label such as bloom is not an identity.

**Start with:** Identify one actual post technique and capture its input scene-color and output for the same frame.

**Read these files:**

- `src/native_graphics/effect.cpp`
- `src/native_graphics/guest_shader_bridge.cpp`
- `docs/native-renderer-migration.md`
- `tests/native_display_gamma_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n 'Tone|MiddleGray|Luminance|post|bloom|gamma' src/native_graphics/effect.cpp src/native_graphics/guest_shader_bridge.cpp docs/native-renderer-migration.md tests/native_display_gamma_tests.cpp
```

**Steps:**

1. Record effect/technique/pass identifiers, shader fingerprints, samplers, constants and target formats from source/assets.
2. Trace authored environment values through upload to the native bindings. Distinguish tone-map/gamma/display transforms and their order.
3. Capture paired inputs/outputs and compare with a matched reference at720p before changing arithmetic or viewport offsets.
4. Fix only demonstrated differences, add focused parameter/output tests, then repeat for1080p and relevant MSAA/preset variants.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Paired captures use the same frame and correctly labeled linear/HDR/display spaces.
- Authored preset transitions change only the intended parameters.
- Resolution/MSAA changes preserve sampling alignment, output extent and gamma order.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_effect_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_effect_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_display_gamma_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_display_gamma_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Named pass captures compare scene color and final output across authored presets, MSAA modes and 720p/1080p; no unnamed technique or rejected state remains.

**Do not:**

- Do not tune authored tone constants by eye to hide a binding/order defect.
- Do not claim HDR fidelity from a clamped BMP.

**If blocked or uncertain:** If no matched reference or actual technique identity is available, deliver the technique/parameter table and keep visual equivalence untested.


<a id="r06-pose"></a>

## R06.pose: Replace model traversal and pose uploads

**Kind:** implementation. **Audit status:** open.

**Goal:** Migrate 821C9478/821C9C20/821B2C28 and 821A1738/821A17D8 into retained rigid/skinned draw records with owned matrix arrays and upload dirty-state effects.

**Scope dependencies:** R02.publication, R09.effects, R09.resources.

**Prerequisite evidence for this slice:** R02.publication supplies a tick/generation; R09.effects supplies upload dirty-state and traversal contracts; R09.resources supplies retained mesh ownership.

**Start with:** Build one rigid-model record and one two-bone skinned record; enumerate their original traversal/upload calls.

**Read these files:**

- `src/native_graphics/native_model_pose_history.h`
- `src/native_graphics/guest_shader_bridge.cpp`
- `out/renderer-inventory/content-boundary-contracts.csv`
- `tests/native_scene_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n '821C9478|821C9C20|821B2C28|821A1738|821A17D8' src/native_graphics/native_model_pose_history.h src/native_graphics/guest_shader_bridge.cpp out/renderer-inventory/content-boundary-contracts.csv tests/native_scene_tests.cpp
```

**Steps:**

1. Record model-record layout, matrix-index selection, skinning branch, palette size and draw order from the original traversal.
2. Publish immutable model draw records and copied/retained matrix arrays with explicit lifetime. Separate authoritative bones from interpolated render poses.
3. Implement native traversal/upload for the two fixtures and guard their original model-render calls. Preserve any required CPU upload mirrors in their declared owner.
4. Extend to attachments and mixed skinned/unskinned records, then validate interpolation across two ticks with source bones unchanged.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Rigid records choose the correct matrix; skinned records upload the expected palette and draw order.
- Repeated interpolated frames leave source bones byte-identical.
- Palette replacement/removal while an older frame is pending never uses released storage.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_scene_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_scene_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Rigid and skinned fixtures, attachments, interpolation and LODs render with original model traversal disabled and unchanged authoritative bones.

**Do not:**

- Do not call original model traversal and label pose interpolation as full ownership.
- Do not retain a pointer to a mutable guest palette without a lifetime contract.

**If blocked or uncertain:** If a record flag or matrix upload side effect is unknown, keep that branch open and request its exact local-effect contract.


<a id="r06-families"></a>

## R06.families: Close model-family render routes

**Kind:** investigation-and-implementation. **Audit status:** open.

**Goal:** For each assigned renderable method, follow direct and indirect routes to submission, classify per-instance/attachment overrides and migrate the remaining native consumer; keep external class labels provisional.

**Scope dependencies:** R01.callbacks, R06.pose.

**Prerequisite evidence for this slice:** R01.callbacks proves that receiver can reach the method; R06.pose provides the retained rigid/skinned consumer used by its draws.

**Start with:** Choose the soldier/player method sub_820DEA08 and one supported concrete receiver. Finish its route before selecting another family.

**Read these files:**

- `out/renderer-inventory/renderable-method-review.csv`
- `out/renderer-inventory/renderable-content-routes.csv`
- `out/renderer-inventory/renderable-helper-routes.csv`
- `src/native_graphics/guest_shader_bridge.cpp`
- `tests/native_scene_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n '820DEA08|8210E6C0|8219A2D0|821E2250|821E5810' out/renderer-inventory/renderable-method-review.csv out/renderer-inventory/renderable-content-routes.csv out/renderer-inventory/renderable-helper-routes.csv src/native_graphics/guest_shader_bridge.cpp tests/native_scene_tests.cpp
```

**Steps:**

1. Find the method row and enumerate its direct/helper/indirect route to draw submission; note class labels as provisional until receiver evidence exists.
2. Record family-specific visibility, attachment, material, pose, topology and pass overrides; compare them with the shared model contract.
3. Implement only the missing overrides around the shared native consumer and create a fixture for each distinct branch.
4. Choose a mission/scenario that actually instantiates the receiver and collect route plus image evidence. Repeat per method, including multipart enemies and vehicles.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- The selected receiver reaches the expected native consumer without original render traversal.
- Attachments retain parent/child pose and ordering across LOD/removal.
- Each method/class row is tested or retains a precise unresolved/unvisited disposition.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_scene_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_scene_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Soldier/civilian/enemy/large multipart/vehicle/attachment rows each have a targeted scene or evidenced unreachable disposition; no original render callback needed.

**Do not:**

- Do not assume all classes sharing a method share all runtime overrides.
- Do not mark an entire family complete from one visible soldier.

**If blocked or uncertain:** If no reproducible scenario or registration proof exists for a family, deliver its static contract and leave runtime acceptance open.


<a id="r07-effects"></a>

## R07.effects: Own dynamic effects and transparency

**Kind:** investigation-and-implementation. **Audit status:** open.

**Goal:** For each particle, spark, smoke, projectile, muzzle, shell and glass method, retain geometry/material/lifetime/sort inputs and remove per-render simulation changes.

**Scope dependencies:** R02.publication, R05.pass, R09.dynamic.

**Prerequisite evidence for this slice:** R02.publication owns effect time; R05.pass owns blend/depth/pass context; R09.dynamic retains transient geometry bytes.

**Start with:** Trace one muzzle-flash or particle method to geometry, sort key, material parameters and lifetime update.

**Read these files:**

- `out/renderer-inventory/renderable-method-review.csv`
- `out/renderer-inventory/renderable-content-routes.csv`
- `src/native_graphics/guest_shader_bridge.cpp`
- `src/native_graphics/native_scene.h`
- `tests/native_scene_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n '8211D250|82121848|821897A8|8217D6E0' out/renderer-inventory/renderable-method-review.csv out/renderer-inventory/renderable-content-routes.csv src/native_graphics/guest_shader_bridge.cpp src/native_graphics/native_scene.h tests/native_scene_tests.cpp
```

**Steps:**

1. Separate effect simulation (spawn/age/expiry) from geometry preparation and rendering; document exact input fields and writers.
2. Publish geometry, material, transform, depth/sort order and lifetime identity at the producer tick.
3. Render retained records in the original required transparent/additive order without advancing age; retain buffers until completion.
4. Compare controlled firing and overlapping effects before expanding to trails, smoke, sparks, glass and projectile families.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Ten renders of one tick do not age or expire an effect.
- Two overlapping alpha effects preserve the required draw order; additive paths keep their own state.
- Expiry/removal and buffer reuse do not leak old effects into later frames.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_scene_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_scene_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_quad_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_quad_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Controlled firing, explosions, trails, overlapping alpha/additive effects and expiry match reference ordering; render repeats do not age effects.

**Do not:**

- Do not assume all transparency can be sorted by distance.
- Do not infer a specific particle method from smoke pixels alone.

**If blocked or uncertain:** If sort key, spawn owner or lifetime writer is unknown, preserve the route and record that exact prerequisite.


<a id="r07-environment"></a>

## R07.environment: Own special world and destruction families

**Kind:** investigation-and-implementation. **Audit status:** open.

**Goal:** Follow sky, wire, rock, tree, grass, broken-object and broken-piece routes; implement their topology, material and lifecycle deviations from the static group path.

**Scope dependencies:** R03.selection, R04.pass, R09.dynamic.

**Prerequisite evidence for this slice:** R03.selection and R04.pass define retained static behavior; R09.dynamic covers any mutable topology or transient vertex storage.

**Start with:** Choose one special method, preferably sky sub_820BB270 or broken-piece sub_82120168, and record how it differs from a static group.

**Read these files:**

- `out/renderer-inventory/renderable-method-review.csv`
- `out/renderer-inventory/renderable-helper-routes.csv`
- `src/native_graphics/native_scene_geometry.h`
- `src/native_graphics/native_scene_membership.h`
- `tests/native_scene_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n '820B8D28|820BB270|820BBA48|82172698|8211FAA8|82120168' out/renderer-inventory/renderable-method-review.csv out/renderer-inventory/renderable-helper-routes.csv src/native_graphics/native_scene_geometry.h src/native_graphics/native_scene_membership.h tests/native_scene_tests.cpp
```

**Steps:**

1. Trace the selected method through helpers to topology/material/pass setup and submission; record any object removal or destruction side effects.
2. Express its deviations as explicit retained inputs rather than extra live guest reads in the static pass.
3. Implement the special draw path and lifecycle transition, with fixtures for visibility and retirement.
4. Repeat for wire, rocks, trees, grass and destruction families; use actual environment/mission evidence to exercise each.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Sky/vegetation state does not leak into the following ordinary world draw.
- Destruction removes the intact draw and retains debris for its documented lifetime.
- Same-address replacement and LOD/cull changes do not show stale parts.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_scene_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_scene_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Outdoor/environment and destruction scenes exercise each family; broken/removing objects leave no stale retained draws or resource aliases.

**Do not:**

- Do not force line/ribbon/debris topology through a triangle-only static contract.
- Do not close all environment families from a city capture.

**If blocked or uncertain:** If a special family uses an unidentified helper or geometry writer, add that bounded investigation before changing its rendering.


<a id="r07-shadows"></a>

## R07.shadows: Identify and migrate shadow passes

**Kind:** investigation-and-implementation. **Audit status:** open.

**Goal:** Trace shadow-related effect techniques, render-target/depth writes and caster/receiver callbacks from source/assets; associate observed pass identities with static, animated and effect casters. Do not infer shadows from a dark patch.

**Scope dependencies:** R01.callbacks, R05.pass, R06.pose.

**Prerequisite evidence for this slice:** R01.callbacks supplies renderer routes and R05.pass supplies pass identities. R06.pose is needed only when migrating animated casters.

**Start with:** Identify one real shadow-producing technique/pass and its caster/receiver route; the initial slice is investigation only.

**Read these files:**

- `src/native_graphics/guest_shader_bridge.cpp`
- `src/native_graphics/effect.cpp`
- `docs/native-renderer-migration.md`
- `out/renderer-inventory/renderable-content-routes.csv`

**Find the relevant code/evidence:**

```powershell
rg -n 'shadow|Shadow|depth|stencil' src/native_graphics/guest_shader_bridge.cpp src/native_graphics/effect.cpp docs/native-renderer-migration.md out/renderer-inventory/renderable-content-routes.csv
```

**Steps:**

1. Search effect techniques, reflected parameters, depth/stencil/target setup and caster-related routes. Record positive and negative search results.
2. Trace a candidate from producer through draw to the sampled/receiver pass; distinguish actual shadows from baked texture shading or other darkening.
3. Capture a controlled stationary/moving caster and receiver pair with candidate pass counters. If no technique is found, bound the searched modes/assets instead of claiming global absence.
4. Only after identification, publish the caster/receiver inputs and migrate the complete pass; add occlusion and animated-caster tests.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- The pass identity is linked to target writes and receiver use, not just a symbol name.
- Moving a caster changes the expected receiver shadow in a matched reference.
- Absence claims state an exact asset/mode search scope and retained unknowns.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_scene_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_scene_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_effect_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_effect_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Document the actual shadow technique and target contract (or evidence of absence per mode), then compare static/moving caster and receiver occlusion in controlled captures.

**Do not:**

- Do not invent a shadow-map implementation before identifying the game technique.
- Do not use a dark screenshot region as sole proof of shadow coverage.

**If blocked or uncertain:** If the actual shadow technique cannot be identified, stop at a reproducible search report and a precise next producer/pass query.


<a id="r07-special"></a>

## R07.special: Resolve base/no-op/test renderable methods

**Kind:** investigation. **Audit status:** open.

**Goal:** For shared 8252B718 and test/base-class rows, verify concrete dispatch and overrides; determine whether each family renders elsewhere, is producer-only, or is genuinely unreachable.

**Scope dependencies:** R01.scope, R01.callbacks.

**Prerequisite evidence for this slice:** R01.scope identifies the table/function slice; R01.callbacks provides known receiver-registration routes.

**Start with:** Select one concrete class table pointing to8252B718; follow its constructor and other render-related slots.

**Read these files:**

- `out/renderer-inventory/renderable-method-review.csv`
- `out/renderer-inventory/verified-vtable-pointers.csv`
- `out/renderer-inventory/renderable-table-installations.csv`
- `out/renderer-inventory/renderable-constructor-review.csv`

**Find the relevant code/evidence:**

```powershell
rg -n '8252B718|8210E220|821E5558' out/renderer-inventory/renderable-method-review.csv out/renderer-inventory/verified-vtable-pointers.csv out/renderer-inventory/renderable-table-installations.csv out/renderer-inventory/renderable-constructor-review.csv
```

**Steps:**

1. Confirm the slot pointer against the verified table row and local body; record the distinction between a no-op method and a no-render object.
2. Find constructor installations, concrete subclasses and other render/producer callbacks that may perform the visible work.
3. Trace registration/removal and mode activation. For debug/test objects, determine whether the audited build can construct/register them.
4. Assign each selected class no-op-slot, alternate-render-route, producer-only or unresolved with evidence; repeat for all shared/base/test rows.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- A no-op slot with another rendering slot is not classified as a non-rendering family.
- A claimed unreachable debug route has constructor/registration/mode evidence.
- Every class sharing the method receives its own disposition or a justified shared contract.

**Verification:** inspect the evidence rows against source and the cases above; run the coverage validator. No unrelated game build is required for a read-only result. Runtime-validation tasks additionally require their scenario/measurement artifacts.

**Task completion gate:** Each class/table row has registration and slot evidence; no-op local body alone never closes a whole content family.

**Do not:**

- Do not delete classes or callbacks based on a no-op local body.
- Do not import external port-complete labels as evidence.

**If blocked or uncertain:** If concrete construction or registration is unknown, keep the class unresolved with the exact table/slot and missing writer.


<a id="r08-ui"></a>

## R08.ui: Own HUD, XUI, fonts and overlays

**Kind:** implementation. **Audit status:** open.

**Goal:** Migrate player overlay 820D3FD0, listener callbacks and UI composition to retained draw records; cover text, radar, menus, pause/results and SDK settings overlay.

**Scope dependencies:** R01.callbacks, R05.pass, R09.assets.

**Prerequisite evidence for this slice:** R01.callbacks identifies overlay/listener receivers; R05.pass provides target/clip state; R09.assets covers the selected font/XUI material contract.

**Start with:** Publish and replay one HUD/text layer at a fixed camera/tick, recording its position in final composition.

**Read these files:**

- `src/native_graphics/guest_shader_bridge.cpp`
- `src/native_graphics/native_xui_bindings.h`
- `src/native_graphics/font_effect.cpp`
- `src/native_graphics/native_canvas_constants.h`
- `tests/native_xui_tests.cpp`
- `tests/native_ui_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n '820D3FD0|RenderOverlay|RenderUiListener|Xui|font' src/native_graphics/guest_shader_bridge.cpp src/native_graphics/native_xui_bindings.h src/native_graphics/font_effect.cpp src/native_graphics/native_canvas_constants.h tests/native_xui_tests.cpp tests/native_ui_tests.cpp
```

**Steps:**

1. Trace UI production separately from rendering. Record texture/font handles, authored canvas coordinates, clip/scissor, layer order and parameters.
2. Build retained draw records for the selected layer, preserving update cadence and references until the frame finishes.
3. Render the layer through native UI submission with original consumer calls guarded; compare final composed output, not only intermediate scene color.
4. Expand to radar/crosshair, menus/pause/results and SDK overlay, then verify canvas transforms at720p/1080p and alternate aspect.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Text, radar and crosshair stay in the expected layer and clip region.
- Pause and SDK overlays cover the intended game content without corrupting its target.
- Authored1280x720 coordinates scale consistently at native1080p with no double scaling.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_xui_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_xui_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_ui_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_ui_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_backend_sdk_ui_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_backend_sdk_ui_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Startup/menu/gameplay/pause/results show correct order, text, clip/scissor and authored canvas at 720p/1080p and alternate aspect ratios; original UI callbacks disabled for consumer.

**Do not:**

- Do not move menu/gameplay state updates into repeated render execution.
- Do not accept partial output captures as final UI composition proof.

**If blocked or uncertain:** If font/texture lifetime or listener ordering is unknown, preserve that layer and document the missing contract.


<a id="r08-loading"></a>

## R08.loading: Close loading and mission transitions

**Kind:** implementation. **Audit status:** open.

**Goal:** Specify no-active-output/direct-scene publication, concurrent loader submissions and menu-to-map/retry/exit handoff; avoid holding submission locks across pacing waits.

**Scope dependencies:** R05.pass, R08.ui, R09.retirement.

**Prerequisite evidence for this slice:** R05.pass and R08.ui supply direct-scene/UI composition; R09.retirement supplies old-generation drain rules. Readiness alone is not publication success.

**Start with:** Reproduce the no-active-output loading branch and link its eligibility counter to an actually published and presented sequence.

**Read these files:**

- `src/native_graphics/guest_shader_bridge.cpp`
- `src/native_graphics/native_backend_frame_queue.h`
- `tools/start-native-binding-validation.ps1`
- `docs/renderer-coverage-runtime.md`

**Find the relevant code/evidence:**

```powershell
rg -n '8219C840|direct_scene|loading|submissions' src/native_graphics/guest_shader_bridge.cpp src/native_graphics/native_backend_frame_queue.h tools/start-native-binding-validation.ps1 docs/renderer-coverage-runtime.md
```

**Steps:**

1. Record menu-to-load-to-gameplay states, active target/output identities, thread ownership and submission-lock intervals.
2. Add or use sequence evidence for eligibility, publication, acquisition and presentation; identify any dropped/blocked stage.
3. Fix the selected output/locking/lifetime defect while preserving loader producer work; do not hold the shared submission gate across pacing sleep or unrelated completion waits.
4. Exercise cold/warm load, retry, menu return and another mission; retain queue peaks and old/new resource generation evidence.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Loading frames actually present while no ordinary output exists.
- Queue/upload memory remains bounded during a slow load.
- Retry or next mission does not consume resources from the retired map generation.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_backend_compositor_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_backend_compositor_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_host_lifetime_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_host_lifetime_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Cold and warm load, retry, return to menu and another mission show changing loading frames with bounded queues; old-generation resources drain before reuse.

**Do not:**

- Do not interpret cumulative XUI/font counts as per-frame visible progress.
- Do not change scene ownership merely to suppress a black-frame log.

**If blocked or uncertain:** If the scenario cannot reach loading deterministically, fix/record R10.input readiness first; do not mark the transition passed.


<a id="r08-movie"></a>

## R08.movie: Own movie composition and lifetime

**Kind:** implementation. **Audit status:** open.

**Goal:** Trace movie decode producers and retained original calls; retain YUV/RGB textures and conversion parameters until consumption, including skip and end-of-movie.

**Scope dependencies:** R05.pass, R09.resources.

**Prerequisite evidence for this slice:** R05.pass identifies the movie composition target; R09.resources defines plane ownership and upload lifetime.

**Start with:** Trace one decoded movie frame from plane production through native conversion to the host frame.

**Read these files:**

- `src/native_graphics/native_movie_bindings.h`
- `src/native_graphics/movie_effect.cpp`
- `src/native_graphics/guest_shader_bridge.cpp`
- `tests/native_movie_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n 'movie|Movie|YUV|plane' src/native_graphics/native_movie_bindings.h src/native_graphics/movie_effect.cpp src/native_graphics/guest_shader_bridge.cpp tests/native_movie_tests.cpp
```

**Steps:**

1. List format, dimensions, pitch/stride, YUV/RGB conversion constants, producer thread and plane reuse point.
2. Retain or copy the planes and parameters as one frame identity until the consumer/GPU finishes; keep decoder production separate.
3. Implement/verify native conversion and composition with correct topology and color range; compare a known decoded input and matched output.
4. Exercise skip/end while frames are queued, then repeat for supported SD/HD variants and menu transition.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- All planes belong to the same decoded frame and retain correct pitch/extent.
- Known color samples agree with the documented conversion/range.
- Skip/end drains or cancels pending frames without presenting a stale movie frame over the menu.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_movie_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_movie_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_backend_compositor_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_backend_compositor_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Supported SD/HD movie paths play, skip and transition with correct color/topology and no stale frame or premature plane retirement.

**Do not:**

- Do not overwrite decoded plane memory still referenced by a queued draw.
- Do not claim SD/HD coverage from a cumulative movie counter.

**If blocked or uncertain:** If a movie variant or decoded reference is unavailable, leave that variant untested and deliver the known format/lifetime contract.


<a id="r08-present"></a>

## R08.present: Close host presentation and device lifecycle

**Kind:** implementation. **Audit status:** open.

**Goal:** Review host files outside native_graphics, 820B0B80 finish callbacks, surface ring ownership, resize/fullscreen/minimize/restore and device recreation policy.

**Scope dependencies:** R05.pass, R09.sync, R09.retirement.

**Prerequisite evidence for this slice:** R05.pass owns final output, R09.sync defines completion waits and R09.retirement defines release timing. Include host integration outside native_graphics when following call sites.

**Start with:** Trace one published frame through acquisition, GPU copy, Present and surface release, including its sequence IDs.

**Read these files:**

- `src/native_graphics/native_backend_host.cpp`
- `src/native_graphics/native_host_surface.cpp`
- `src/native_graphics/native_backend_frame_queue.h`
- `src/native_graphics/native_backend_present.cpp`
- `tests/native_host_lifetime_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n '820B0B80|Present|resize|sequence|completion' src/native_graphics/native_backend_host.cpp src/native_graphics/native_host_surface.cpp src/native_graphics/native_backend_frame_queue.h src/native_graphics/native_backend_present.cpp tests/native_host_lifetime_tests.cpp
```

**Steps:**

1. Write the surface-ring state transitions and ownership at each stage. Identify original finish callbacks and required CPU effects.
2. Remove only render-consumer dependence already covered by those contracts; retain surface references through copy/presentation completion.
3. Handle resize/minimize/restore/shutdown by draining or invalidating the correct generation; make device-loss recovery or explicit failure policy visible.
4. Compare submitted/acquired/presented sequence IDs and complete-frame captures during transitions. Investigate repeats/drops by stage.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- No surface is reused before the consuming GPU operation completes.
- Resize/restore presents the new extent without dangling old targets.
- Shutdown releases only owned resources and cannot deadlock on an unpublished or canceled frame.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_backend_host_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_backend_host_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_host_lifetime_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_host_lifetime_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_present_tail_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_present_tail_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Resize and mode changes preserve frame order and resource completion; host tests pass and captured unique frame IDs match submissions with no unexplained repeats/drops.

**Do not:**

- Do not equate calling Present with a new rendered image.
- Do not remove finish callbacks until their CPU/lifetime effects are assigned.

**If blocked or uncertain:** If device-loss behavior is not implemented or reproducible, record the actual failure policy and leave recovery acceptance open.


<a id="r09-effects"></a>

## R09.effects: Classify all retained CPU effects

**Kind:** investigation. **Audit status:** open.

**Goal:** For each assigned explicit/macro/adapter/store boundary, finish local effects if pending and trace transitive writes, readers, aliasing, exceptions and callbacks. Assign simulation, producer lifetime, native consumer or compatibility ownership.

**Scope dependencies:** R01.scope.

**Prerequisite evidence for this slice:** R01.scope proves the selected call matters to a renderer route. Reuse existing partial local-effect evidence instead of restarting decompilation.

**Start with:** Choose one explicit retained call required by R04.pass; review its complete connected CPU effect contract before selecting another callee.

**Read these files:**

- `out/renderer-inventory/retained-call-effect-reviews.csv`
- `out/renderer-inventory/macro-original-dependencies.csv`
- `out/renderer-inventory/retained-adapter-terminal-edges.csv`
- `out/renderer-inventory/state-write-ledger.csv`

**Find the relevant code/evidence:**

```powershell
rg -n '821D96D8|821BE9D8|82141440|82149608' out/renderer-inventory/retained-call-effect-reviews.csv out/renderer-inventory/macro-original-dependencies.csv out/renderer-inventory/retained-adapter-terminal-edges.csv out/renderer-inventory/state-write-ledger.csv
```

**Steps:**

1. Record branch guard, arguments, return use and already reviewed local writes. Mark local versus transitive evidence separately.
2. Follow outgoing calls and pointer aliases far enough to assign every relevant write, callback, allocation, release, lock and failure effect; include vector/atomic/zero-fill operations.
3. For each destination, identify reader, lifetime and required ordering; assign simulation producer, resource producer, native consumer or compatibility-only owner.
4. Deliver a contract and a differential fixture specification; implement a fixture when needed to resolve disputed behavior. Repeat by selected callee, not by every raw store row independently.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- No required CPU write disappears merely because packet emission is unnecessary.
- Aliased memory, callback writes and failure-after-partial-write are represented in the contract.
- Stack-only disposition has alias/escape evidence; unknown transitive effects keep the call open.

**Verification:** inspect the evidence rows against source and the cases above; run the coverage validator. No unrelated game build is required for a read-only result. Runtime-validation tasks additionally require their scenario/measurement artifacts.

**Task completion gate:** Every assigned source site has a complete effects/owner/ordering table with instruction evidence and differential tests; partial local review is not a completed status.

**Do not:**

- Do not count87,579 store candidates as87,579 implementation tasks.
- Do not upgrade a partial local review to complete callee semantics.

**If blocked or uncertain:** If a helper/provider is unresolved, record exact callsite, arguments and consumer obligation; do not delete or bypass the retained call.


<a id="r09-resources"></a>

## R09.resources: Close resource creation and identity contracts

**Kind:** investigation-and-implementation. **Audit status:** open.

**Goal:** Review texture/declaration/shader/VB/IB create/import/lock/unlock/destruction and physical/virtual alias writers, including ImportTexture function reference; retain versioned identity through frame completion.

**Scope dependencies:** R09.effects.

**Prerequisite evidence for this slice:** R09.effects supplies the selected allocator/lock/unlock/destructor effects and the physical/virtual alias relationship.

**Start with:** Choose one VB/IB owner and trace create, publish, update, retire and same-address replacement before reviewing other resource kinds.

**Read these files:**

- `src/native_graphics/native_model_buffers.h`
- `src/native_graphics/native_buffer_writes.h`
- `src/native_graphics/guest_shader_bridge.cpp`
- `out/renderer-inventory/native-original-references.csv`
- `tests/native_scene_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n 'ImportTexture|82201458|NotifyUpdateAliases|ObservedVersion|CommitObserved' src/native_graphics/native_model_buffers.h src/native_graphics/native_buffer_writes.h src/native_graphics/guest_shader_bridge.cpp out/renderer-inventory/native-original-references.csv tests/native_scene_tests.cpp
```

**Steps:**

1. Write a lifecycle table with resource identity, lifetime ID, revision, aliases, producer and retained native handle.
2. Trace every known mutation route including import callback82201458, unlock, guest writes and free/reallocation; distinguish identity change from content revision.
3. Fix or specify the observed-version handshake so a snapshot commits only if the owner/lifetime/revision still match; retain old handles for pending consumers.
4. Add create/update/failure/reuse tests and repeat for textures, declarations and shaders. Attribute untracked writers to a named follow-up.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Same address with a new lifetime never reuses stale native bytes or metadata.
- An alias update invalidates all overlapping published owners it is required to affect.
- Failed allocation/import does not publish a partially initialized resource or leak a retained reference.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_scene_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_scene_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_guest_memory_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_guest_memory_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Create/failure/update/free/reallocate-same-address and alias tests reject stale handles and preserve in-flight frames, with no unreported guest writes.

**Do not:**

- Do not replace lifetime identity with an address-only cache key.
- Do not assume CPU-only heap placement without verifying the relevant allocation route.

**If blocked or uncertain:** If a write route bypasses revision tracking, keep that resource on the explicit validated path until its writer is incorporated.


<a id="r09-retirement"></a>

## R09.retirement: Close retirement queue and allocator ownership

**Kind:** investigation-and-implementation. **Audit status:** open.

**Goal:** Trace 82141440 queue growth/consumption, immediate fence tags, allocator providers, reference transitions and resource destruction. Preserve callback ordering and failure semantics.

**Scope dependencies:** R09.effects.

**Prerequisite evidence for this slice:** R09.effects must provide the queue writer and allocator-call contracts. Queue consumption and GPU completion are separate facts to establish.

**Start with:** Follow one texture/shader unbind through immediate-fence tagging or the eight-byte retirement queue, including queue exhaustion.

**Read these files:**

- `src/native_graphics/native_index_binding.h`
- `src/native_graphics/native_texture_binding.h`
- `src/native_graphics/native_shader_binding.h`
- `out/renderer-inventory/retirement-owner-provenance.csv`
- `out/renderer-inventory/small-retained-contracts.csv`

**Find the relevant code/evidence:**

```powershell
rg -n '82141440|RetireNativeBoundResource|10780|10784|13148' src/native_graphics/native_index_binding.h src/native_graphics/native_texture_binding.h src/native_graphics/native_shader_binding.h out/renderer-inventory/retirement-owner-provenance.csv out/renderer-inventory/small-retained-contracts.csv
```

**Steps:**

1. Record queue record layout, old-resource predicate, preserved tag inputs, cursor/capacity and allocation-growth behavior.
2. Find the queue consumer and the event/fence that makes release safe; trace reference ownership through failure and same-pointer rebind.
3. Create differential fixtures for both retirement branches and allocator exhaustion before changing ownership code.
4. Preserve observable ordering, alias writes and failure effects; test delayed completion and same-address reuse across pending frames.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Queue-full handling preserves every record and the observed tag bits.
- Immediate retirement and deferred retirement follow the correct predicates.
- Rebinding the same pointer or retiring aliased memory does not change the original required write order.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_shader_binding_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_shader_binding_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_backend_completion_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_backend_completion_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Queue exhaustion, allocation failure, same-resource rebind, delayed GPU completion and address reuse have explicit expected results and passing lifetime tests.

**Do not:**

- Do not zero an apparently uninitialized tag input that the original preserves.
- Do not infer the queue is unused because one gameplay run did not fill it.

**If blocked or uncertain:** If the queue consumer or allocator ownership cannot be proved, keep retirement behavior and report the precise missing release condition.


<a id="r09-shader"></a>

## R09.shader: Finish shader-default aliasing and failure parity

**Kind:** investigation-and-implementation. **Audit status:** open.

**Goal:** After the fixed retirement-before-default-read ordering, investigate payload aliasing during incremental constant writes, concurrent mutation and malformed count/extent behavior; either preserve reachable behavior or prove input preconditions.

**Scope dependencies:** R09.retirement.

**Prerequisite evidence for this slice:** R09.retirement provides retirement ordering and alias semantics. The earlier retirement-before-default-read fix is baseline behavior to preserve.

**Start with:** Add a fixture where an early constant write aliases a later defaults payload word; compare incremental original reads against the current collected-word parser.

**Read these files:**

- `src/native_graphics/native_shader_binding.h`
- `tests/native_shader_binding_tests.cpp`
- `out/renderer-inventory/small-retained-contracts.csv`
- `src/native_graphics/guest_shader_bridge.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n 'ReadNativeShaderDefaults|SetNativeShaderResource|82149608|821498C8' src/native_graphics/native_shader_binding.h tests/native_shader_binding_tests.cpp out/renderer-inventory/small-retained-contracts.csv src/native_graphics/guest_shader_bridge.cpp
```

**Steps:**

1. Read the pixel/vertex original contracts and existing tests. List the three stream phases and exact read/write/failure sequence.
2. Construct the smallest valid aliased stream that distinguishes incremental reads from precollection; record expected memory mutations at each step.
3. Determine whether reachable providers permit the alias or malformed input. Fix reachable parity differences, or document a proven provider precondition; never infer safety from absence in a sample.
4. Extend tests for null shader, absent defaults, pixel/vertex dirty stores and failure after partial application. Keep the previous retirement alias regression passing.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Retirement still precedes binding/default sampling in the required order.
- Payload self-alias behavior matches the justified contract.
- Malformed count/extent behavior has an explicit reachable-input policy and tests, including already-applied writes when required.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_shader_binding_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_shader_binding_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Differential fixtures cover pixel/vertex, null/no-default, payload self-alias and failure-after-partial-write; document excluded malformed streams with provider evidence.

**Do not:**

- Do not combine the two pixel dirty stores without an equivalence argument.
- Do not simply remove bounds validation to mimic malformed loops.

**If blocked or uncertain:** If malformed or aliasing inputs have no established provider contract, deliver the failing differential case and precise reachability question before choosing behavior.


<a id="r09-dynamic"></a>

## R09.dynamic: Own dynamic geometry and scratch buffers

**Kind:** implementation. **Audit status:** open.

**Goal:** Enumerate immediate/nonindexed/quads, wire/trail/debris and transient ring producers; snapshot mutable bytes before render and retain through GPU consumption.

**Scope dependencies:** R09.resources.

**Prerequisite evidence for this slice:** R09.resources supplies identity/revision and release rules for the selected transient owner.

**Start with:** Trace one immediate quad or effect vertex buffer from producer write through upload-ring allocation and GPU completion.

**Read these files:**

- `src/native_graphics/native_upload_ring.h`
- `src/native_graphics/native_buffer_writes.h`
- `src/native_graphics/guest_shader_bridge.cpp`
- `tests/native_upload_ring_tests.cpp`
- `tests/native_quad_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n 'immediate|transient|append|ring|quad' src/native_graphics/native_upload_ring.h src/native_graphics/native_buffer_writes.h src/native_graphics/guest_shader_bridge.cpp tests/native_upload_ring_tests.cpp tests/native_quad_tests.cpp
```

**Steps:**

1. Record byte extent, stride/topology, index use, producer writer and all consumers of the mutable buffer.
2. Snapshot or retain immutable bytes before the producer can reuse them; attach the snapshot to the publication/frame identity.
3. Reserve ring space with explicit completion ownership and implement wrap/split behavior without overwriting pending uploads.
4. Stress append, wraparound, concurrent publication and large effect bursts; feed encountered formats into the contract catalog.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- A producer rewrite after publication cannot change the older queued draw.
- Ring wrap waits/splits or allocates according to policy instead of overwriting pending data.
- Large bursts report bounds/rejections explicitly and preserve draw order.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_upload_ring_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_upload_ring_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_quad_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_quad_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_scene_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_scene_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Wraparound, append, concurrent publication, removal and large effect bursts never overwrite in-flight data; geometry contracts record all encountered formats.

**Do not:**

- Do not retain a naked pointer to scratch memory across a producer update.
- Do not treat a successful allocation as proof of safe GPU reuse.

**If blocked or uncertain:** If completion ownership for a transient block is unknown, keep its existing synchronized path and resolve that owner first.


<a id="r09-sync"></a>

## R09.sync: Close worker, signal and synchronization callbacks

**Kind:** investigation-and-implementation. **Audit status:** open.

**Goal:** Resolve worker registration modes, 8213C9F0 alternate signals, access10/12/14 and other lock callers, monitor/profiling callbacks, vblank/event delivery and SDK imports.

**Scope dependencies:** R01.scope, R09.effects.

**Prerequisite evidence for this slice:** R01.scope distinguishes renderer-relevant callbacks; R09.effects provides local lock, signal and event mutations.

**Start with:** Choose one worker registration mode and follow its callback/context pair from writer to invocation and shutdown.

**Read these files:**

- `out/renderer-inventory/retained-callback-review.csv`
- `out/renderer-inventory/worker-registration-review.json`
- `out/renderer-inventory/sdk-monitor-provenance.json`
- `src/native_graphics/guest_shader_bridge.cpp`
- `tests/native_worker_callback_audit_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n '8243A000|8213C9F0|82134408|8214EBA0|KfAcquireSpinLock' out/renderer-inventory/retained-callback-review.csv out/renderer-inventory/worker-registration-review.json out/renderer-inventory/sdk-monitor-provenance.json src/native_graphics/guest_shader_bridge.cpp tests/native_worker_callback_audit_tests.cpp
```

**Steps:**

1. Record registration mode, callback/context fields, writers, invocation guard, thread and lock held at each stage.
2. Resolve alternate targets and external monitor/profiling callbacks separately; enumerate lock access/caller combinations instead of generalizing one audited caller.
3. Draw a lock-order and signal/wait table, including exception/cancellation/shutdown paths. Implement only the selected proven owner transition.
4. Add contention, missing-callback, alternate-mode and shutdown fixtures. Use bounded waits so a deadlock produces a recorded failed test.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Callback/context publication cannot mix two registrations under the documented synchronization.
- Every selected signal has a corresponding wait/completion contract, including failure paths.
- Shutdown and contended execution complete within the fixture deadline without leaked locks.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_worker_callback_audit_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_worker_callback_audit_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_backend_completion_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_backend_completion_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Each callback population, activation guard and lock/event owner is evidenced; contention, failure and shutdown tests terminate without deadlock or lifetime races.

**Do not:**

- Do not remove a lock because sampled single-threaded gameplay worked.
- Do not classify SDK synchronization imports as renderer packet tails.

**If blocked or uncertain:** If an external callback population is unavailable, preserve it and record provider/registration requirements; do not substitute a no-op.


<a id="r09-assets"></a>

## R09.assets: Close shader/texture/declaration/render-state coverage

**Kind:** investigation-and-implementation. **Audit status:** open.

**Goal:** Re-run disc effect/texture coverage, enumerate technique render-state combinations, merge runtime declaration catalogs and add offline replay for immediate/font/movie/XUI/utility contracts.

**Scope dependencies:** R01.modes.

**Prerequisite evidence for this slice:** R01.modes defines which backend/format combinations are in scope. Inspect the documented checker invocation before running it; do not invent asset paths or flags.

**Start with:** Replay the existing Mission1 declaration fixture with the documented effect catalog, recording the exact corpus identity and accepted/rejected totals.

**Read these files:**

- `docs/native-coverage.md`
- `src/native_graphics/native_contract_ledger.h`
- `tools/native_texture_check.cpp`
- `tests/native_effect_tests.cpp`
- `tests/fixtures/geometry-contract-mission1.txt`

**Find the relevant code/evidence:**

```powershell
rg -n 'coverage|contract|rejected|declaration|technique' docs/native-coverage.md src/native_graphics/native_contract_ledger.h tools/native_texture_check.cpp tests/native_effect_tests.cpp tests/fixtures/geometry-contract-mission1.txt
```

**Steps:**

1. Read native-coverage.md and the checker/test argument parser. Record asset root, corpus hashes, command, omissions and failure counters.
2. Enumerate technique render-state combinations and compare with native decoding/binding support, not merely shader compilation.
3. Implement additive catalog merge with stable contract identity and provenance; retain rejected records and report newly seen combinations.
4. Add offline replay for one uncovered path (immediate/font/movie/XUI/utility) at a time and associate runtime captures with mission/weapon/menu scope.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Merging two catalogs preserves their union and reports new/rejected contracts without duplication.
- Intentionally unsupported declaration/state is reported as a rejection, not silently omitted.
- Zero failures identifies the exact closed asset set; runtime declaration exhaustion is not inferred.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_effect_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_effect_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_texture_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_texture_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Zero missing assets, rejected contracts and omitted records across the declared content corpus; catalog growth is reported, not treated as proof of exhaustion.

**Do not:**

- Do not erase rejected contracts to obtain a clean summary.
- Do not call Mission1 the complete runtime declaration corpus.

**If blocked or uncertain:** If assets/checker binaries or a variant are missing, record the exact prerequisite and keep that coverage axis untested.


<a id="r09-compat"></a>

## R09.compat: Make compatibility and diagnostic routes explicit

**Kind:** investigation-and-implementation. **Audit status:** open.

**Goal:** For every disabled-bridge/host/activation/ownership fallback and diagnostic branch, retain an explicit supported owner or reject the mode; verify macros are forwarding, not replacements.

**Scope dependencies:** R01.modes, R09.effects.

**Prerequisite evidence for this slice:** R01.modes supplies supported-mode intent; R09.effects supplies the retained original call effects. Unproven policy changes are proposals, not defaults to change.

**Start with:** Choose one ownership-predicate fallback and exercise both sides using a valid fixture for its exact guard.

**Read these files:**

- `out/renderer-inventory/native-dependency-classification.csv`
- `out/renderer-inventory/macro-original-dependencies.csv`
- `out/renderer-coverage/configuration-modes.csv`
- `src/native_graphics/guest_shader_bridge.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n 'ownership|fallback|EDF_RENDER_PHASE|EDF_RENDER_STATE_SETTER' out/renderer-inventory/native-dependency-classification.csv out/renderer-inventory/macro-original-dependencies.csv out/renderer-coverage/configuration-modes.csv src/native_graphics/guest_shader_bridge.cpp
```

**Steps:**

1. Record branch predicate, effective settings, selected original/native route and required post-state for the chosen callsite.
2. Use fixtures to enter each guard outcome; confirm failure/unsupported cases remain visible and correctly owned.
3. If replacing a supported route, preserve CPU effects and add route attribution. If rejecting an unsupported route, use the agreed policy and an explicit diagnostic.
4. Repeat for host/bridge/activation-disabled and diagnostic paths, preserving macro forwarding classifications.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Both native and retained branches produce the documented required CPU state.
- Unsupported configuration cannot silently enter an unowned renderer.
- Diagnostic-only branches are labeled and do not falsely count toward native completion.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_frame_dispatch_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_frame_dispatch_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Toggle matrix exercises all declared paths; no branch is marked native solely because a hook exists and no removed mode silently falls back.

**Do not:**

- Do not remove compatibility routes simply to make original-call counts zero.
- Do not change unrelated defaults to force a fixture through the desired path.

**If blocked or uncertain:** If support policy or callee effects are absent, leave the fallback intact and return that exact prerequisite.


<a id="r10-trace"></a>

## R10.trace: Make runtime route coverage attributable

**Kind:** implementation. **Audit status:** open.

**Goal:** Add bounded per-scenario counters for callback instruction/target/receiver, content method, pass identity, backend and fallback reason; merge with static ledgers and report new routes.

**Scope dependencies:** R01.callbacks.

**Prerequisite evidence for this slice:** R01.callbacks supplies the selected site/receiver contract; instrumentation of an unresolved target is allowed, but must label it unresolved rather than assuming a class.

**Start with:** Instrument the outer callback at821A5158 with instruction/target/receiver and scenario identity, using a bounded collector.

**Read these files:**

- `src/native_graphics/native_frame_dispatch.h`
- `src/native_graphics/guest_shader_bridge.cpp`
- `tools/summarize-renderer-coverage-run.py`
- `out/renderer-inventory/indirect-site-ledger.csv`
- `tests/native_frame_dispatch_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n 'ResolveIndirectFunction|HookTiming|RenderOverlay|RenderPose' src/native_graphics/native_frame_dispatch.h src/native_graphics/guest_shader_bridge.cpp tools/summarize-renderer-coverage-run.py out/renderer-inventory/indirect-site-ledger.csv tests/native_frame_dispatch_tests.cpp
```

**Steps:**

1. Define a trace row with schema version, run/scenario ID, timestamp, frame/publication ID, instruction, target, receiver/vtable, backend/pass and fallback reason.
2. Record at the actual dispatch/branch, not only an enclosing timing scope. Bound memory/events and expose dropped/sampled counts.
3. Join observed sites/targets to static ledgers; retain unknowns with their raw locators and emit a new-work list.
4. Test known/unknown target and collector overflow, then capture one scenario interval and verify attribution before broadening to family/pass events.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- A changed target at the same callsite is preserved as a distinct observation.
- Overflow/sampling is reported and cannot masquerade as complete target coverage.
- Every event joins to a known row or an explicit new-target investigation.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_frame_dispatch_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_frame_dispatch_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** A scripted run produces per-scenario route IDs and new-target tasks; sampling/overflow and unexercised routes are explicit. Aggregate draw counts alone cannot pass.

**Do not:**

- Do not log every draw without a bound and then use the run for performance measurement.
- Do not treat timing phase names as exact callback receiver proof.

**If blocked or uncertain:** If a site lacks a stable frame/scenario identifier, record the missing correlation and do not claim scenario attribution.


<a id="r10-capture"></a>

## R10.capture: Capture complete frames in a timed burst

**Kind:** implementation. **Audit status:** open.

**Goal:** Extend the one-shot native_backend_host GPU capture to a bounded timestamped burst with published sequence IDs; retain final composition and label partial scene captures separately. Hidden automated runs have no capturable main window through the existing window script.

**Scope dependencies:** None.

**Prerequisite evidence for this slice:** No other task must be complete. Read both backend-host and D3D11-host capture paths and retain their final-composition semantics.

**Start with:** Extend the final-host capture policy to request exactly three images with distinct timestamps/sequence IDs; keep the existing one-shot behavior as the default.

**Read these files:**

- `src/native_graphics/native_backend_host.cpp`
- `src/native_graphics/native_host_surface.cpp`
- `src/native_graphics/native_capture_policy.h`
- `tests/native_backend_host_tests.cpp`
- `tests/native_capture_policy_tests.cpp`

**Find the relevant code/evidence:**

```powershell
rg -n 'captured_|edf_native_host_capture|Capture|sequence_' src/native_graphics/native_backend_host.cpp src/native_graphics/native_host_surface.cpp src/native_graphics/native_capture_policy.h tests/native_backend_host_tests.cpp tests/native_capture_policy_tests.cpp
```

**Steps:**

1. Locate when each host path captures relative to UI composition and presentation; identify the published sequence and resource lifetime at that point.
2. Implement a bounded capture schedule/count and unique output naming. Define handling for repeated sequence IDs, missing images, write errors and overwrite attempts.
3. Write a manifest containing capture index, wall/monotonic time, sequence, extent/backend and complete-versus-partial kind. Avoid changing normal behavior when capture is off.
4. Add policy/host tests and collect before/during/after gameplay and pause/loading examples; verify actual UI composition visually.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Exactly the configured bounded number of files is produced; existing files are not silently overwritten.
- Manifest sequence/time matches the captured final host frame, including repeated images.
- Disabled capture causes no readback/file activity; old one-shot configuration still works.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_capture_policy_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_capture_policy_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_backend_host_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_backend_host_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Before/during/after gameplay, pause and loading frames are captured with sequence and wall-clock metadata; final UI composition is present and no partial output is mislabeled complete.

**Do not:**

- Do not repurpose partial scene-output BMPs and label them complete.
- Do not require a visible OS main window for automated GPU capture.

**If blocked or uncertain:** If a backend cannot expose final composition or sequence identity, report that limitation explicitly and leave its acceptance open.


<a id="r10-input"></a>

## R10.input: Synchronize scenario controls with gameplay state

**Kind:** implementation. **Audit status:** open.

**Goal:** Replace elapsed-time-only scenario assumptions with a reached-gameplay/camera-control marker; record intended and actual poll time and extend the movement/fire window after cutscene completion. The probe delivered the 180000 ms movement event at 183403 ms.

**Scope dependencies:** R10.trace.

**Prerequisite evidence for this slice:** R10.trace must provide or identify a trustworthy gameplay/control-ready event. Merely seeing a rendered scene is insufficient because the intro renders before control is available.

**Start with:** Add one wait-for-gameplay-ready action followed by a short movement event, while preserving the existing time-only script format.

**Read these files:**

- `src/scripted_input.h`
- `src/scripted_input_logic.h`
- `tools/renderer-coverage-input.txt`
- `tests/scripted_input_reload_tests.cpp`
- `docs/renderer-coverage-runtime.md`

**Find the relevant code/evidence:**

```powershell
rg -n 'GetDeviceState|ReloadInputEvents|t0_|ButtonsAt|AnalogAt' src/scripted_input.h src/scripted_input_logic.h tools/renderer-coverage-input.txt tests/scripted_input_reload_tests.cpp docs/renderer-coverage-runtime.md
```

**Steps:**

1. Define readiness from an audited game/control state and record its provenance. Keep waiting bounded with a visible timeout result.
2. Schedule dependent actions relative to the actual readiness event; record intended time, actual poll time, scenario and release time.
3. Preserve old scripts, live reload clock semantics, neutral expiry and real-pad merging; do not replay expired events after a reload.
4. Add delayed-ready/no-ready/reload tests, then verify a movement or fire action by resulting state/captures, not just delivered buttons.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- A late readiness event shifts dependent controls instead of losing them during a cutscene.
- No readiness before deadline reports not-reached and leaves controls neutral.
- Reload and expiry do not latch buttons/axes or restart the scenario clock.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_scripted_input_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_scripted_input_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** Each control action has a reached-state precondition, observed input interval and visible/state effect; delayed polling and intro camera motion cannot produce a false movement pass.

**Do not:**

- Do not change the player control scheme to make one script work.
- Do not use a fixed180-second delay as proof of gameplay readiness.

**If blocked or uncertain:** If no validated control-ready marker is available, deliver the candidate field/producer investigation instead of inventing a timer heuristic.


<a id="r10-scenarios"></a>

## R10.scenarios: Run the declared content and lifecycle matrix

**Kind:** runtime-validation. **Audit status:** open.

**Goal:** Exercise the scenario table with deterministic controls, timestamped screenshots, route counters and a matched reference. Include all map/environment and enemy/weapon families identified by the asset/mission census.

**Scope dependencies:** R10.trace, R10.capture, R10.input.

**Prerequisite evidence for this slice:** R10.trace, R10.capture and R10.input must provide their selected-scenario evidence. A matched reference is required for parity; otherwise report execution only.

**Start with:** Repeat S03/S04 using readiness, route attribution and complete-frame burst artifacts before selecting an unvisited scenario.

**Read these files:**

- `docs/renderer-coverage-scenario-results.json`
- `docs/renderer-coverage-runtime.md`
- `tools/start-native-binding-validation.ps1`
- `tools/renderer-coverage-input.txt`
- `tools/summarize-renderer-coverage-run.py`

**Find the relevant code/evidence:**

```powershell
rg -n 'S01|S02|S03|S04|S05|S06|S07|S08|S09|S10|S11|S12|S13' docs/renderer-coverage-scenario-results.json docs/renderer-coverage-runtime.md tools/start-native-binding-validation.ps1 tools/renderer-coverage-input.txt tools/summarize-renderer-coverage-run.py
```

**Steps:**

1. Select one scenario and list its required mode, map/content, controls, observation markers and pass/fail conditions before launching.
2. Use isolated userdata and a new output directory. Record executable/source hashes, exact arguments, input file and machine/backend information.
3. Run bounded controls and collect reached-state, route, complete-frame and resource evidence. Stop only the owned process after verifying its actual executable path.
4. Compare with the reference, assign reached/observed/compared status separately, add new gaps, and update only the supported scenario conclusions. Repeat across the content census.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Every claimed action has an actual reached marker and observed effect.
- Missing families/modes or unavailable references remain explicit untested cases.
- Each failure/new target has a task and artifacts sufficient to reproduce it.

**Verification:** inspect the evidence rows against source and the cases above; run the coverage validator. No unrelated game build is required for a read-only result. Runtime-validation tasks additionally require their scenario/measurement artifacts.

**Task completion gate:** Every scenario records reached/observed/compared status and artifacts; failures have tasks; no queued input or screenshot is mistaken for proof of its intended effect.

**Do not:**

- Do not infer all-map coverage from Mission1.
- Do not kill an existing user game process or reuse output paths that would overwrite evidence.

**If blocked or uncertain:** If assets, readiness, input or reference prerequisites fail, record not-reached/not-compared and resolve the named prerequisite before treating the scenario as accepted.


<a id="r10-cadence"></a>

## R10.cadence: Integrate complete frames at independent cadence

**Kind:** implementation. **Audit status:** open.

**Goal:** Consume retained full-frame publications independently of the simulation clock, interpolate render inputs and retire generations by consumer completion.

**Scope dependencies:** R04.pass, R06.families, R07.effects, R07.environment, R07.shadows, R08.ui, R08.loading, R08.movie, R08.present.

**Prerequisite evidence for this slice:** For the selected mode, R04.pass/R06.families/R07.effects/R07.environment/R07.shadows/R08.ui/R08.loading/R08.movie/R08.present must provide complete consumer and lifetime contracts. Do not integrate partial content and call full-frame independence complete.

**Start with:** Use a deterministic fixture with a60Hz producer and120Hz consumer rendering one complete retained publication without guest render callbacks.

**Read these files:**

- `src/native_graphics/native_pacing.h`
- `src/native_graphics/native_frame_flight.h`
- `src/native_graphics/native_queued_scene.h`
- `src/native_graphics/guest_shader_bridge.cpp`
- `tools/report-native-host-cadence.py`

**Find the relevant code/evidence:**

```powershell
rg -n 'simulation|generation|interpol|FrameExtraSimulationSteps|render_helper' src/native_graphics/native_pacing.h src/native_graphics/native_frame_flight.h src/native_graphics/native_queued_scene.h src/native_graphics/guest_shader_bridge.cpp tools/report-native-host-cadence.py
```

**Steps:**

1. Write the producer-publication-consumer-retirement state machine, including missed ticks, interpolation endpoints, pause and loading transitions.
2. Schedule render consumption independently while preserving exactly one authoritative simulation update per tick; interpolate only approved render inputs.
3. Guard all original consumer entries for the selected supported mode and retain input generations until every consuming GPU frame finishes.
4. Test delayed producer/consumer and transitions, then measure produced publication IDs, rendered unique images and presented sequences in gameplay.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- A60Hz simulation remains60Hz when render cadence changes to120Hz.
- Extra frames show measured interpolation/image changes where expected, rather than repeated Present of one image.
- Pause/load/consumer lag preserves simulation authority and resource lifetime without original consumer reentry.

**Existing regression targets (run after the relevant changes):**

```powershell
cmake --build out/build/win-amd64-release --target edf_native_pacing_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_pacing_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_scene_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_scene_tests$' --output-on-failure --no-tests=error
cmake --build out/build/win-amd64-release --target edf_native_host_lifetime_tests --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^edf_native_host_lifetime_tests$' --output-on-failure --no-tests=error
```

Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.

**Task completion gate:** 60 Hz producer / 120 Hz consumer produces measured unique frames with stable simulation over movement/combat/load and no original consumer helper invocation.

**Do not:**

- Do not increase simulation speed to reach a rendering target.
- Do not claim120Hz from an FPS cap or presentation count alone.

**If blocked or uncertain:** If any required content still needs an original consumer callback, leave full-frame integration incomplete and identify that specific upstream task.


<a id="r10-performance"></a>

## R10.performance: Measure non-audit performance and backend parity

**Kind:** runtime-validation. **Audit status:** open.

**Goal:** Run D3D12 hardware and declared D3D11 fallback, worker counts 0/1/4, MSAA 1/2/4, native resolutions and frame credits; use a smaller pairwise matrix with rationale for combinations.

**Scope dependencies:** R10.cadence, R10.scenarios.

**Prerequisite evidence for this slice:** R10.cadence provides the mode being assessed, R10.scenarios provides a repeatable route and accepted content. Record the actual machine/display configuration.

**Start with:** Measure one fixed gameplay route on D3D12 with default settings and diagnostics/capture disabled; establish a reproducible baseline before changing one variable.

**Read these files:**

- `tools/report-native-host-cadence.py`
- `tools/report-native-frame-trace.py`
- `tools/report-native-geometry-runs.py`
- `tools/start-native-binding-validation.ps1`
- `docs/native-backend-migration.md`

**Find the relevant code/evidence:**

```powershell
rg -n 'edf_native_geometry_workers|edf_native_frame_latency|edf_native_msaa|edf_native_vsync' tools/report-native-host-cadence.py tools/report-native-frame-trace.py tools/report-native-geometry-runs.py tools/start-native-binding-validation.ps1 docs/native-backend-migration.md
```

**Steps:**

1. Define warmup and measurement intervals, deterministic route, power/display settings and required metrics before running.
2. Collect CPU/GPU and frame-time distribution, unique image cadence, memory peak and failures with intrusive audit tracing/readback disabled.
3. Compare backend, worker0/1/4, sample count, native resolution and credits using a documented pairwise matrix; change one factor for causal comparisons.
4. Report medians and tail percentiles, durations/sample counts, visual checks and limitations. Separate regression from a target the implementation never met.

**Required outputs:**

- contract.csv: selected source locators, branch/receiver, inputs, writes/outputs, owner, lifetime, evidence and unresolved question.
- results.md: exact slice attempted, prerequisite evidence, changes/findings, commands with exit codes, acceptance cases passed/failed/not-run, and remaining scope.
- Epistemic evidence IDs and any confirmed claim IDs; negative results and unresolved assumptions must be included.

**Cases to add or demonstrate:**

- Reported intervals exclude startup/warmup and identify instrumentation overhead.
- 120Hz claims include unique-image cadence and preserved simulation, not only presentation rate.
- Each comparison names backend/settings, content route and sample size; unsupported combinations are explicit.

**Verification:** inspect the evidence rows against source and the cases above; run the coverage validator. No unrelated game build is required for a read-only result. Runtime-validation tasks additionally require their scenario/measurement artifacts.

**Task completion gate:** Report CPU/GPU/frame-time distributions, unique image cadence, visual comparisons and memory peaks with audit instrumentation off; 120 Hz is measured rather than inferred from a cap.

**Do not:**

- Do not use the35.6FPS capture-heavy audit run as a performance baseline.
- Do not average away stalls or silently discard failed runs.

**If blocked or uncertain:** If the route cannot repeat or timing instrumentation changes behavior materially, report the measurement limitation and fix it before drawing a performance conclusion.

