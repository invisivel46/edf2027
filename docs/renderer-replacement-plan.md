> Historical as of 2026-09-22; current status in [renderer-status.md](renderer-status.md).

# Using the replacement plan

The [critical-path backlog](renderer-critical-path.md) groups all 34 coverage
tasks and 32 features into eight coordinated packages. Start the
[first implementation packet](renderer-first-replacement-task.md) as a bounded
static-group slice; do not dispatch a whole multi-feature package to one agent.

Rebuild and audit the plan without booting or compiling the game:

```powershell
python tools/renderer_replacement_plan.py
python -m unittest tests.test_renderer_replacement_plan
```

`out/renderer-replacement/plan.json` contains exact source sites, instruction-level
atlas provenance, complete observed direct slices, native boundary cuts, external
targets, adapter terminal obligations, shared-helper memberships, test targets,
feature joins and completion criteria. `audit.json` checks exact task, feature,
site and native-boundary coverage plus source hashes. The generator rejects stale
source reviews instead of silently assigning their old meaning to changed code.

The census has 237 native boundary routes. The site ledger verifies 186 explicit
calls, 87 macro forwards and one original-function argument. A separate 65-site
set includes hookable entries, resolver/helper calls and imports; these sites
are assigned bounded classification work rather than treated as proven original
renderer execution. Four native routes without named original calls retain their
helper/observer obligations. Root counts overlap between packages.

The source scanner masks comments and strings, verifies exact call locators and
reviewed source hashes, and checks both macro invocations and their forwarding
definitions. It retains source-reviewed guards. This is stronger than the atlas's
lexical native mentions, but still does not prove which branches execute at runtime.

Traversal follows observed atlas edges and stops at other native hooks, exposing
them as contract cuts. It also follows recorded extracted-adapter terminal edges.
It does not manufacture targets for unresolved indirect calls. The resulting
5,240-function union and 2,864 shared helpers are **dependency context, not a port
queue**. Producer, audio, compatibility, audit and platform calls retain separate
dispositions. No shared utility is automatically assigned for replacement.

Ranking is transparent: dependency wave, downstream feature breadth, number of
boundary roots, then unresolved indirect risk. Wave is not elapsed time. The
static package has a 19-function direct slice and existing original-vs-native
CPU-tail and GPU replay tests, making it a useful first vertical slice while
large resource and producer packages are handled through scoped prerequisites.

Only that first integrated slice can establish credible throughput for a
days-scale schedule. This planning pass does not alter task completion, the
renderer implementation, the scalar acceptance policy, or the original atlas.
