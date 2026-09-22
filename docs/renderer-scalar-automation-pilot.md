# Scalar automation pilot

Implemented 2026-09-22: one batch extracts facts for 170 scalar functions and
classifies 59 with two conservative getter rules. The other 111 remain explicit
unsupported cases. This implements the bounded AUTO-01/02 pilot and the original
execution portion of AUTO-03 from the RE automation survey.

## Reproduce

The normal regression command includes both new suites:

```powershell
.\validate-renderer-offline.cmd
```

It compiles unchanged generated guest function bodies against the actual SDK
PPCContext and guest-memory macros. It needs the configured SDK/build environment,
but neither Ghidra nor the original image at test time. It does not boot the game.

To refresh independent facts and the evidence report:

```powershell
.\tools\export-renderer-scalar-facts.ps1
python tools/analyze-renderer-scalars.py
```

The exporter needs the existing Ghidra installation/project, inventory and Java
paths documented by its parameters. It uses a read-only project with transient
function definitions. `-AllCensus` selects 9,216 functions; that full export has
not been exercised. The analyzer needs `out/renderer-tally/tally.json` and writes
`out/renderer-automation-pilot/contracts.json`. The frozen fixture is
`tests/fixtures/renderer-scalar-pilot.json`; do not regenerate it to hide drift.
Changed original bodies or accepted Ghidra boundaries fail validation.

## Observed coverage

| Item | Result |
|---|---:|
| Frozen scalar functions / image-checked instructions | 170 / 1,443 |
| Natural Ghidra instructions / high-p-code operations | 1,492 / 2,817 |
| Incomplete decompilations | 0 |
| Masked load getters / plain load getters | 40 / 19 |
| Unsupported functions | 111 |
| Accepted functions in reserved partition | 13 |
| Guest harness invocations | 35,046 |
| Negative controls | 354 |

The unchanged Ghidra export reproduced SHA-256
`ce138cc07dafcc2e1e71337f73f218d311e4fce0ec97d28b720eab4433a4841f`.
The partition is deterministic, but was established alongside rule development;
it is not a blinded generalization benchmark. No comparable manual baseline,
agent-token total, peak-memory measurement or net speedup has been established.

The masked rule requires exactly `lwz r11,D(r3); rlwinm r3,r11,...,Rc=0; blr`;
the plain rule requires `lwz r3,D(r3); blr`. Return encoding, registers, instruction
contiguity, widths and alignment are checked. Contracts describe big-endian
loads, signed displacements, rotate/mask fields, zero extension, register
clobbers, unchanged memory and explicit ordinary-memory preconditions.

The harness invokes actual `__imp__` bodies and compares the entire reset context
and memory with a separate bit-by-bit contract evaluator. Fixed patterns,
one-hot/one-zero values, seeded random values and three receiver addresses form
the corpus. Controls exercise wrong returns, undeclared register changes,
memory writes, endian mismatch, unaligned and unmapped inputs. Python mutations
also reject alternate widths, condition updates, broken dataflow, return variants,
extra instructions, missing bytes, stale bodies and accepted-boundary changes.
These are bounded executable checks, not a proof over every input.

## Accounting and follow-ups

The report maps all 170 records to existing function and scalar boundary IDs.
Its proposal scope is the 59 getter function IDs and `task:R09.effects`.
Boundary IDs are evidence references; the report does not mutate the tally or
mark a function, boundary or parent task complete. A subsequent coordinator
reconciliation in `docs/renderer-getter-acceptance.json` imports the reviewed 59
getter contracts as partial knowledge, with immutable evidence and body freshness
checks. See `docs/renderer-tally.md` for the reconciled counts. Independent Sol review is in
`out/renderer-automation-pilot/review/scalar-getter-design-review.md`.

| Remaining work | Dependency | Completion test |
|---|---|---|
| Resolve `sub_82137978` shared-tail body under R09.effects / R01.scope | Existing frozen facts and natural Ghidra body | Explain branch at 82137984 to 821370E0 and all 49 additional instructions; reconcile the inventory with preserved provenance before accepting any contract |
| Establish receiver validity, ownership, lifetime, modes and concurrency under R09.effects | Caller/receiver provenance for each family | Evidence accounts for all admitted receivers and states explicit excluded populations; local getter contracts alone cannot close this |
| Add native comparison to AUTO-03 when a replacement exists | Native getter adapter and accepted receiver contract | Execute original and native implementations on identical reset inputs; compare declared registers, memory and effects with mutation controls |
| Expand to scalar setters (AUTO-04) | Separate frozen setter corpus and write/alias model | Exact write-set, byte width, ordering and negative controls pass; unsupported calls or aliases remain exceptions |
| Measure scaling before census-wide deployment | Equivalent manual baseline and instrumented batch | Report setup, rerun, review, token and memory cost per accepted obligation; do not infer speedup from these simple getters |

Ghidra follows a shared tail for unsupported `sub_82137978`, producing 49 extra
instructions. All frozen 1,443 address/word pairs match, and all accepted 59
getters have exact independent boundaries. A temporary attempt to normalize away
the extra range was rejected and reverted; the discrepancy is retained in the
facts, report, tests and Epistemic.

No native scalar getter adapter exists. This pilot does not establish native
replacement equivalence, renderer completion or gameplay coverage. Milestone
gameplay remains necessary for integration paths beyond these contracts.
