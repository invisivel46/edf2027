# Dispatching renderer work

The coordinator owns the queue, dependencies, assignments and integration. A
worker owns one bounded slice and proposes a result. A different agent reviews
that exact result. Parent tasks in `renderer-coverage.json` stay open: integrating
a slice never changes parent status or coverage dispositions.

This is a cooperative workflow, not an authentication boundary against an agent
deliberately forging logs or editing the ledger. The validator checks identities,
hashes and result structure. A reviewer must check the source contract, evidence
relevance and independent test oracle. Existing Epistemic IDs are not automatically
proof that a particular requirement has been met.

## Models and concurrency

The first-wave specification is `docs/renderer-dispatch-first-wave.json`:

| Slice | Model / effort | Outcome |
|---|---|---|
| `R01.scope.frame-to-bucket-v1` | `gpt-6-astra` / high | Evidence for one two-function connected path |
| `R10.capture.final-host-three-frame-v1` | `gpt-5.6-terra` / high | Bounded final-host capture implementation and offline cases |
| `dispatch.readiness-v1` | `gpt-5.6-sol` / medium | Independent packet/prerequisite audit |

These are initial routing choices, not benchmark results. Use Astra for ambiguous
contracts, ownership/concurrency and escalations; Terra for implementation with
an established contract; Sol for bounded evidence/report tooling; Luna only for
mechanical tasks with exact machine-checkable output. Evaluate acceptance rate,
review time, rework, elapsed time and available usage data before changing routing.

This session has three worker slots plus the coordinator. Slot availability does
not establish task independence. The dispatcher refuses concurrent write ownership
overlap even across isolated checkouts. Workers do not recursively delegate.
The coordinator serializes GPU tests, inventory generation and milestone boots;
this first version records that policy in packets but does not manage a GPU lock
or launch agents automatically. Pass the packet's model/effort explicitly when
spawning, with a fresh context containing the packet and required shared guidance.

## Initialize and inspect

Run from the repository root. Only the coordinator invokes mutating commands.

```powershell
python tools/dispatch-renderer.py init
python tools/dispatch-renderer.py status
python tools/dispatch-renderer.py packet R01.scope.frame-to-bucket-v1 --output out/renderer-dispatch/drafts/scope.md
```

The default durable ledger is `out/renderer-dispatch/ledger.json`. Preserve it
with task artifacts; it is not committed. `--ledger <path>` before the subcommand
selects another campaign. Initialization refuses to overwrite an existing ledger.
All mutations acquire an exclusive filesystem lock and atomically replace the
ledger. A leftover lock after a crash requires checking the recorded process
before manual removal; time alone does not authorize another writer.

Initialization validates slice IDs, parent IDs, ownership paths, models and
acyclic dependencies. It pins HEAD, a SHA-256 manifest of tracked and nonignored
untracked source files (including dirty contents), all declared input files
including ignored inventories, and parent task context. `knowledge/`, `out/` and
the volatile `generated/default/codegen.build.stamp` are excluded from the source
fingerprint. Evidence is checked separately; build/evidence output must not
invalidate source identity. New source files do invalidate it.

Draft packets explicitly say not to execute. After reviewing the selected
contract prerequisites and coverage audit, the coordinator marks slices ready:

```powershell
python tools/dispatch-renderer.py ready R01.scope.frame-to-bucket-v1
```

The CLI runs the coverage audit before initialization, readiness and baseline
advance, requires exit zero and `passed:true`, and saves the report/hash in the
ledger. Readiness requires unchanged baseline/input hashes, integrated slice
dependencies with unchanged accepted artifacts/evidence, and existence of
explicitly required Epistemic evidence. A broad
parent dependency need not be complete; define the actual required contract as a
slice dependency or explicit evidence IDs. Investigation packets may retain
unknowns. Add substantive contract prerequisites before assigning dependent
implementation; do not bypass a gate by deleting dependencies.

## Isolate and assign

Implementation cannot run in the coordinator checkout. Create an isolated local
checkout from the pinned baseline, preserving current uncommitted and untracked
source as well as declared ignored inputs:

```powershell
python tools/dispatch-renderer.py prepare out/renderer-dispatch/workspaces/capture-v1
python tools/dispatch-renderer.py ready R10.capture.final-host-three-frame-v1
python tools/dispatch-renderer.py assign R10.capture.final-host-three-frame-v1 --owner capture-worker-1 --workspace out/renderer-dispatch/workspaces/capture-v1
python tools/dispatch-renderer.py packet R10.capture.final-host-three-frame-v1 --output out/renderer-dispatch/packets/capture-v1.md
```

`prepare` uses a detached, local shared clone and copies pinned file contents over
it. It does not commit, stash, reset or change the coordinator's index. Do not
prune the parent object store while shared clones are in use. A failed preparation
leaves its directory for diagnosis; choose a new destination for a retry. Source
symlinks escaping the workspace are rejected. Game data and existing CMake build
directories are not copied. Configure a worker's own build directory with the
same SDK when needed; builds must not share a CMake cache across checkouts.

Read-only workers may use the coordinator checkout:

Use that checkout for the first-wave research/audit workers: their full coverage
audit also reads inventory files beyond the explicitly copied packet inputs.
The prepared implementation checkout contains its declared inputs and source,
not the entire ignored research/output tree.

```powershell
python tools/dispatch-renderer.py assign R01.scope.frame-to-bucket-v1 --owner scope-worker-1 --workspace .
```

Assignment rechecks the baseline, source inputs, ownership and owner availability.
It creates a unique attempt ID and a unique output directory. The packet contains
scope, steps, expected cases, source hashes, allowed files, checks and stop rules.
Send this assigned packet to the agent. Epistemic sessions must be unique; keep
the returned session ID and pass it on every write. Use the central project
knowledge service even when implementation lives in an isolated checkout.

## Results and review

Have the coordinator produce a result skeleton for the assigned worker:

```powershell
python tools/dispatch-renderer.py result-template R10.capture.final-host-three-frame-v1 --output out/renderer-dispatch/capture-result-template.json
```

Generate/update the source fingerprint after the final edit with
`python tools/dispatch-renderer.py fingerprint --workspace <worker-checkout>`.
Fill the skeleton; it intentionally starts incomplete. Set `disposition` to
`slice_complete` only when all offline cases pass. Leave `parent_complete:false`.
List actual changed files, session/evidence IDs, and artifacts under the assigned
output directory. Each artifact reference has the form:

```json
{"path": "out/renderer-dispatch/results/<attempt>/results.md", "sha256": "<file SHA-256>"}
```

Acceptance cases must exactly match the packet's IDs, name an artifact and contain
specific details. Offline cases require `passed`; milestone-only runtime cases
require `not_run` with the remaining work explained. Supplemental checks require
the exact argument array, exit code zero and a hashed log. Run them in the worker
checkout; logs alone are worker assertions subject to independent review.

Implementation also requires the full offline report (`suite:all`). The offline
runner records source fingerprints before and after building/testing and fails
if they differ. The dispatcher requires those hashes to match the submitted
checkout. Older reports without fingerprints cannot satisfy this gate.

```powershell
python tools/dispatch-renderer.py validate-result R10.capture.final-host-three-frame-v1 --result <result.json>
python tools/dispatch-renderer.py submit R10.capture.final-host-three-frame-v1 --result <result.json>
```

Submission copies the validated result into the ledger and records its canonical
SHA-256 as `result_id`. A separate reviewer reads source, artifacts and oracle,
then writes a review in the worker checkout:

```json
{
  "reviewer": "capture-reviewer-1",
  "packet_id": "<assigned packet ID>",
  "result_id": "<submitted result ID from ledger>",
  "decision": "accept",
  "contract_checked": true,
  "oracle_checked": true,
  "details": "Specific contract checks, evidence examined, oracle independence and remaining runtime limits."
}
```

```powershell
python tools/dispatch-renderer.py review R10.capture.final-host-three-frame-v1 --reviewer capture-reviewer-1 --record out/renderer-dispatch/results/<attempt>/review.json
```

Self-review is rejected. Review and integration revalidate submitted source and
artifact hashes. Changed artifacts or source invalidate acceptance. Worker chat
messages and a successful packet render are never completion proof.

## Integration and retries

The coordinator applies the reviewed file changes, checks integration conflicts,
and runs the full offline gate on the resulting coordinator source. The tool does
not apply patches or merge branches. Then record integration:

```powershell
python tools/dispatch-renderer.py integrate R10.capture.final-host-three-frame-v1 --offline-report out/renderer-offline/<run>/report.json
```

Changed file hashes must match the reviewed worker result, and the integration
report must match the complete current coordinator source. Read-only integration
does not need a GPU report. Neither route changes coverage task status.

If a worker is blocked or review finds a defect:

```powershell
python tools/dispatch-renderer.py block R10.capture.final-host-three-frame-v1 --reason "Concrete missing contract or failed case; artifact path and next bounded action"
```

Before retrying, stop/reconcile the previous worker and preserve its handoff.
Re-readying and assignment create a new attempt; old attempts remain in the
ledger. A changed source baseline requires a **new campaign ledger**, preserving
the old one, if the changes are outside integrated patches. For a normal completed
wave, first finish or explicitly block all active attempts, review dependency
evidence for applicability, then advance the same campaign:

```powershell
python tools/dispatch-renderer.py advance --reason "Reviewed integrated contracts remain applicable; ready for the next wave"
```

Advance refuses in-flight work or source changes outside accepted integrated
patches. It archives the old baseline, pins current source/inputs and retains
integrated results with their artifact/evidence hashes. Previously ready slices
become pending and must pass readiness again. A downstream slice can now depend
on the accepted contract while receiving a checkout containing the integrated
implementation. There is no automatic semantic rebase: the coordinator must
explain why accepted contracts still apply. Investigate invalidated evidence
instead of transferring completion to unrelated source.

The normal lifecycle is pending -> ready -> assigned -> submitted -> reviewed ->
integrated, with blocked for incomplete handoffs. Only verified integration releases
slice dependencies. Full parent closure remains a separate coverage/evidence review.

## Verify the tooling

```powershell
python -m unittest discover -s tests -p test_renderer_dispatch.py
python tools/audit-renderer-coverage.py
```

The tests use temporary repositories and synthetic evidence. They exercise
acceptance and rejection paths, not the renderer or the truth of research claims.
