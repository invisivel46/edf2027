# Renderer work accounting

Run from the repository root, without starting the game:

```powershell
python tools/audit-renderer-coverage.py
python tools/tally-renderer.py
python tools/tally-renderer.py --check
python -m unittest discover -s tests -p test_renderer_tally.py
```

The authoritative generated snapshot is `out/renderer-tally/tally.json`. `--check`
recomputes it and rejects differences, missing evidence, changed inventory hashes,
unaccepted pilots, changed accepted artifacts, or unmapped coverage items. Retain
snapshots used by campaigns; create a new output path for the next baseline.

The current reconciliation counts 9,216 functions: 468 with partial local evidence
and 8,748 untriaged. No whole-function contract or exclusion is accepted by this
reconciliation. This is a conservative evidence tally, not a claim that existing
native code does nothing. The imported review tables and exact accepted pilot
scopes are listed in `renderer-tally-reconciliation.json`.

Call-path reviews credit their hook function, not an unreviewed callee. Evidence
freshness is labeled per source: `source_hash_verified` where a supporting body
hash exists, otherwise `catalog_only`. Catalog-only evidence remains partial.

The reconciliation also records deferred evidence: macro-call reviews contain 36
net-new candidate functions, but lack catalog membership and reviewed source
hashes. They remain untriaged in this tally. `R09.effects` owns the explicit
source-recheck, cataloging and acceptance steps recorded in the reconciliation.

The accepted `scalar-getters-v1` set contributes partial local knowledge for an
exact 59-function selection. Eleven were already partial, so the net change is 48.
The acceptance pins the archived pre-tally report, frozen manifest, rule source,
Ghidra facts, actual-body harness source and result, independent review, pilot
documentation, final 22-suite offline report, and evidence records. The tally
rechecks all selected generated bodies against the frozen manifest on every build.
It does not close a function, boundary, task, receiver population, lifetime, mode,
native-adapter equivalence, or gameplay obligation.

The accepted `scalar-setters-v1` set contributes partial local knowledge for an
exact 24-function selection, all net-new to the partial tally. Its immutable
archive preserves the accepted report, analyzer source, generated-body harness
source, 87-candidate selection fixture, natural facts, independent review, pilot
documentation, 14,256-invocation harness report with 214 negative controls, and
the final 24-suite offline report. The tally also pins the unchanged scalar
manifest and real runtime PCH, then re-hashes every selected generated function
body against that manifest. This historical acceptance does not depend on the
current setter analyzer or harness remaining at their accepted version. It does
not close a whole function, boundary, parent task, receiver population, lifetime,
mode, native-setter equivalence, or gameplay obligation.

There are 108,048 open inventory boundary obligations. Many are individual store
instructions or candidate edges; this is not an estimate of 108,048 coding tasks.
Functions, boundary rows, parent tasks, features and scenarios overlap. Never add
those counts or use them as a percentage of rendering correctness.

Both reviewed pilots count as resolved **bounded research obligations**. They
advance seven distinct functions to partial knowledge, with overlap against the
older local-contract tables. Their receiver-population, mode, transitive-effect
and synchronization questions remain open. Thus initial obligation accounting is
108,048 starting + 2 bounded obligations registered - 2 accepted resolutions =
108,048 unresolved inventory obligations. Partial progress does not burn down a
whole-function or whole-population obligation.

The snapshot separately reports 16 implementation parent tasks, 5 investigation
tasks, 11 mixed investigation/implementation tasks and 2 runtime-validation tasks.
All remain open. Of 13 gameplay scenarios, 6 are partially exercised and 7 are
unexercised; neither status implies successful validation. All 32 features and 98
configuration entries must reference existing parent tasks.

## Worker handoff

New dispatch specs pin `tally: {"path": "out/renderer-tally/tally.json", "sha256":
"<file SHA-256>"}` and give each slice explicit `tally_ids`. Select connected,
bounded records, using `function:sub_821A5080`, `task:R01.callbacks`, or the exact
boundary IDs from the snapshot. Include relevant source inputs and completion
tests in the slice as before. Pin an archived snapshot when work spans tally updates.

Worker results report each assigned ID as unchanged, partial or resolved, with
specific details and evidence IDs. Newly discovered obligations are reported
separately. These are proposals: integration does not automatically change this
tally. The coordinator checks the independent review, remaining boundaries and
completion test, then adds the accepted bounded slice to the reconciliation and
rebuilds. Whole-function closure and exclusions require a separate reviewed
reconciliation mechanism; this first version deliberately cannot infer them.

Boundary IDs hash the source table, task and complete source record. Moving a row
does not change identity; changing its contents does. Such a change requires
explicit reconciliation with the older snapshot, not silent reuse of its status.
The snapshot pins all input hashes and accepted evidence artifacts. `--check`
detects stale output; the coverage audit checks the upstream census.

The old pilot ledgers remain historical evidence. Pending assignments pinned to
the old source fingerprint are stale after these tooling changes. Start a new
campaign with the current source and tally rather than rewriting accepted results.
