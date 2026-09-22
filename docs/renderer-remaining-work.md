# Renderer remaining-work ledger

2026-09-22. Package index derived from [the evidence inventory](renderer-completion-inventory.md)
and [implementation handoff](native-scene-renderer-handoff.md). The concrete
[coverage audit](renderer-coverage-audit.md) now supplies the feature/mode/family
join, task dependencies, completion tests and source-pinned boundary assignments.
See [runtime findings](renderer-coverage-runtime.md) for the two post-fix runs.
For execution, use the [step-by-step task guide](renderer-task-guide.md).
Each of the34 tasks now names starting files/symbols, a bounded first slice,
prerequisite evidence, ordered steps, deliverables, regression cases, existing
test commands, and stopping conditions. Export a single complete assignment with
`python tools/show-renderer-task.py R10.capture --output out/R10.capture-task.md`.
Planning coverage is checked for the declared census; complete runtime coverage
and independent-renderer implementation remain unproven.
Function counts measure research coverage, not missing implementations or percent complete.

The target is a renderer that submits complete frames from owned inputs at an
independent cadence while preserving simulation, resource lifetimes and visible
behavior. Existing original-code dependencies may remain on the producer side
only where their role and synchronization are explicit.

| ID | Work package | Evidence / existing capability | Remaining deliverable | Acceptance evidence |
|---|---|---|---|---|
| R01 | Frame callbacks and modes | Native outer/bucket orchestration; eight indirect sites in emitted outer helper | Classify every renderer-facing callback target and registration by mode; assign native consumer or producer-only role | Target/registration ledger with no unassigned displayed-frame route; runtime scenarios exercise each declared mode |
| R02 | Simulation and camera publication | Camera snapshots, source events and interpolation; original world callback changes counters/time | Publish camera, animation and other render inputs at an explicit producer boundary; remove simulation advances from repeated rendering | Repeated rendering of one generation leaves authoritative simulation and lifetimes unchanged; new generations advance correctly |
| R03 | Static selection | Native hierarchy, LOD, membership and retained source selection | Own hidden/mode/duplicate state and handle unknown/mutating callbacks without live selection reads | Retained-generation selection tests cover visibility, order, LOD, removal, reuse and mutations; audited runtime comparison |
| R04 | Static pass execution | Retained geometry/material/world draws and group handoffs | Classify and relocate compatibility setup, activation, indexed-tail and retirement side effects; submit complete pass from retained inputs | Complete static pass runs with legacy setup/activation/draw submission disabled; lifetime and mixed-content tests |
| R05 | View/pass boundaries | Published camera and target/viewport state; scene begin reviewed | Own clear, viewport, target inheritance, resolve and pass exit across all supported views | Multi-view and nested/offscreen pass ordering and output comparison; no implicit inherited guest pass state |
| R06 | Animated models and skinning | Model traversal and matrix upload paths reviewed; interpolation scope preserves source bones | Enumerate model families and migrate their remaining selection, pose upload and submission dependencies | Soldiers, vehicles, attachments and relevant LODs verified; retained pose and lifecycle tests |
| R07 | Effects, shadows and remaining world families | Renderable-family census and focused route evidence | Assign a concrete migration task and fallback policy to every remaining family and special pass | Family-to-task coverage join; targeted scenes and image comparisons for effects, transparency, shadows and special modes |
| R08 | UI, movies, loading and presentation | Native components and existing tests; original callback dependencies remain | Close frame composition, loading/menu/movie/overlay ordering and presentation ownership | Startup, loading, gameplay, menus and transitions render correctly on supported backends; resize/device lifecycle tests |
| R09 | Resource and compatibility contracts | Texture/shader/declaration/allocator/retirement reviews; shader ordering regression fixed | Close callback populations and queue consumers; finish retained-call effect classification; address remaining shader payload aliasing/validation differences | Lifetime/reuse/failure tests and explicit producer/consumer ownership for retained paths; no unexplained resource loss |
| R10 | Independent cadence and end-to-end acceptance | Pacing infrastructure and sampled gameplay validation | Integrate complete-frame submission independently of simulation; establish scenario/backend/performance acceptance matrix | Matched-frame comparisons, lifetime stress, cadence tests and non-audit performance measurements across declared scenarios |

## Coverage audit disposition

The requested coverage audit is recorded in `renderer-coverage-audit.md` and
`renderer-coverage.json`: 32 feature surfaces, all 47 renderable-method rows,
98 renderer/cadence setting definitions, and concrete R01–R10 subtasks. Each
subtask names a change or bounded investigation, dependencies and a completion
test. Every exported unresolved boundary is assigned; no partial review is
silently promoted to completed status. `tools/audit-renderer-coverage.py`
verifies the exact join and reruns the upstream source-freshness audit.

Two post-fix runs exercised startup/loading, Mission 1, weapon/fire/reload and
pause/resume. Movement inputs overlapped the intro transition, so isolated
movement acceptance remains open. Runtime findings added R10.input and
R10.capture; unvisited scenarios remain explicit tasks. No claim of exhaustive
runtime population or visual equivalence is made.

The original checklist below is retained as the ongoing maintenance gate for
new discoveries, rather than outstanding work to create the initial task map.

1. Enumerate supported frame modes, content families and lifecycle scenarios.
   Use the existing renderable and callback ledgers; do not equate the broad
   9,216-function closure with renderer scope.
2. Join every renderer-facing hook, retained call, callback and family to one
   work package or a justified already-complete/producer-only disposition.
   Current explicit coverage still has21 sites awaiting local effects; macro
   coverage has49. Partial local reviews also retain transitive obligations.
3. Split each work package into concrete code changes and bounded research
   questions, with dependencies and an executable or inspectable acceptance
   check. Unknown callback targets must name their registration/receiver search;
   "research more" is not a sufficient task.
4. Exercise representative scenarios to discover routes absent from static
   assumptions. Record which modes/families were actually exercised, not just
   aggregate successful draw counts. Add newly observed work to the same ledger.
5. Audit coverage: every declared feature, mode and lifecycle has a disposition;
   every unresolved boundary has a specific task; every implementation task has
   a dependency and completion test. A planning inventory can then be complete
   while its implementation and bounded research tasks remain unfinished.

## Current implementation evidence

The shader retirement/default ordering fix has a failing-before/passing-after
alias regression and passes targeted binding tests. The full executable
recompiled its bridge and linked successfully afterward
(`out/renderer-inventory/renderer-shader-order-build.log`).

The preceding baseline gameplay run reached440,000 published-geometry bypasses
and37million audited instance reads, with412/412 ready preload groups and no
logged error/critical or sampled nonzero mismatch counters. The inspected image
shows city, player, NPCs and HUD. This is one scene and the pre-fix executable;
it does not establish complete content coverage or validate the new shader fix
in gameplay. The owned process was stopped after path verification.

The coverage join is now available. Next priority is the complete static
producer/selection/pass boundary (R02–R05), resolving R01/R09 questions needed
by that boundary. Continue targeted reverse engineering alongside implementation.
