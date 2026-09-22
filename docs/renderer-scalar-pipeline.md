# Automated scalar pipeline

Run from the repository root:

```powershell
.\run-renderer-scalars.cmd
```

The command analyzes all 170 frozen scalar functions, incrementally builds the
offline harness, executes changed eligible cases, archives evidence, and updates
the renderer tally under the reviewed policy. It never boots or builds the game.
It needs the existing SDK-configured CMake build and Visual Studio environment;
the wrapper initializes that environment. `--no-accept` permits development
without changing the tally. `--force-tests` reexecutes every eligible case.

## Automated work

The effect engine decodes instructions into ordered register and memory effects.
It derives contracts by instruction semantics, without per-function allowlists
or manually written family fixtures. Exact body hashes and independently exported
Ghidra boundaries bind the frozen corpus. A boundary mismatch stays unsupported.

For every eligible function, the harness compares the unchanged generated body,
the derived contract, and a separately implemented raw-instruction interpreter.
It checks the entire context, final memory, and ordered writes over 402 inputs per
function. Negative controls check that altered state and traces are rejected.
Independent decoder checks reject unsupported condition-register updates.

The current partition is 127 local contracts and 43 unsupported functions.
All 127 passed 51,054 invocations and 484 per-function negative controls. Adding
byte and halfword loads (`lbz`, `lhz`) admitted 13 additional functions. Known-answer
tests cover zero extension, big-endian reads, signed offsets, address wraparound,
alignment and mapped-memory bounds. Every
unsupported function retains its reason and belongs to a ranked exception bundle
under `R09.effects` in `out/renderer-scalar-pipeline/analysis.json`. Bundles include
a bounded action and completion test; extending one missing semantic operation
can unlock multiple functions at once.

## Cache and acceptance

Analysis caches include the function record, independent boundary, engine, and
analysis helper. Execution caches include the contract, original body, oracle,
engine, actual compiler binary/command, and Ninja-discovered headers. Changes to
shared semantics or runtime dependencies conservatively invalidate all affected
execution results. Unrelated function changes do not invalidate other function
results. Cache corruption is an error. A repeated development run reused all 170
analyses and 114 execution results in 2.76 seconds, executing zero cases and
producing the same evidence identity. The accepted rerun, including full tally
reconciliation, took 7.10 seconds with zero executions for the initial 114 cases.
The byte/halfword extension adds another 13 partial functions: 511 partial,
8,705 untriaged, and zero whole-function contracts
out of 9,216 functions; all 108,048 broad boundary obligations remain open.

`docs/renderer-scalar-policy.json` pins reviewed inputs. Changing those inputs
requires reviewing the change and explicitly renewing the policy; the command
cannot authorize its own new semantics. Acceptance also rechecks current bodies,
compiled contract/body identities, runtime dependencies, complete corpus
accounting, and minimum execution/control counts. Only partial evidence flows
into the tally. Whole-function contracts, broad boundary obligations, and parent
tasks remain open. Hashes protect local provenance and staleness, not against a
malicious writer who can replace both artifacts and their pins.

Content-addressed bundles live under `out/renderer-scalar-pipeline/bundles/`.
`latest-run.json` records timing and cache statistics separately, so reruns do not
change evidence identity. Preserve these bundles with the reconciliation config.
The command archives local evidence; Epistemic findings are recorded separately
by the coordinating agent.

## Limits and extension workflow

### Automatic blocker planning

Every scalar pipeline run also writes `out/renderer-scalar-pipeline/planner.json`.
For planning alone, without compiling or updating acceptance, run:

```powershell
python tools/renderer_scalar_planner.py
```

The planner scans past the first rejection and records every identifiable missing
operation, condition update, control-flow requirement, boundary disagreement,
memory-base/alignment constraint and known receiver mutation. Instruction sites
and raw words accompany each blocker. Extended instruction forms are distinguished.
Candidate bundles include dependencies, specific validation requirements and all
functions whose observed operation sets they cover. Ranking minimizes distinct
missing effects, then maximizes potential functions; it is not an effort estimate.
Functions with unresolved structural gates are listed as investigations rather
than counted as immediate candidates. No planner output can grant acceptance.

Current result: 43 rejected functions, 27 requiring structural investigation and
16 available for semantics planning. The smallest observed batch is
`sub_821358F0`, requiring `subfic` and opcode31/XO136 (`subfe`) with XER carry
semantics. Twelve apparent `lfs`/`stfs` candidates also require non-r3 memory-base
support, so adding those two opcodes alone is insufficient. Unknown instruction
effects or newly modeled paths can expose further blockers; all candidate counts
remain estimates until exact-boundary analysis and execution pass.

Planner regressions run with `python -m unittest tests.test_renderer_scalar_planner`.
The report pins its engine/planner and normalized input hashes. It is regenerated
deterministically, separately from accepted execution evidence.

These are finite local-effect tests on ordinary mapped memory. They do not prove
receiver ownership, MMIO behavior, native renderer equivalence, all input values,
or gameplay parity. The independent boundary fixture preserves the known shared
tail disagreement. Unsupported calls, floating-point operations, control flow,
and unreviewed rotate/insert variants are rejected.

Extend the highest-ranked exception bundle by adding semantics, independent raw
oracle behavior, and meaningful rejection/known-answer tests. Run the command with
`--no-accept --force-tests`, review the resulting changes and exact boundaries,
then renew policy pins and run the default command. Run
`validate-renderer-offline.cmd` for the full 26-suite regression gate. Reserve
gameplay boots for renderer integration milestones.
