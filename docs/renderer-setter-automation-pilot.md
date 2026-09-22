> Historical as of 2026-09-22; current status in [renderer-status.md](renderer-status.md).

# Scalar setter automation pilot

This batch extends the scalar getter pipeline with bounded integer setters. Run
the regression gate without starting the game:

```powershell
.\validate-renderer-offline.cmd
```

Refresh the setter report with `python tools/renderer_setter_analysis.py` after
the scalar Ghidra facts exist. The new frozen selection references the existing
170-function, image-checked scalar corpus rather than collecting fresh functions
opportunistically while developing rules.

Use `--ghidra-facts out/renderer-automation-pilot/ghidra/facts.json` to revalidate
the full independent export. Normal offline compilation uses a reviewed frozen
boundary attestation, checks its accepted-body digest and source-manifest hash,
and verifies current generated bodies. It does not rerun Ghidra. The attestation
is trusted fixture evidence, not a new independent decoder result on each run;
changes to its corpus or acceptance fields require review and full-facts validation.

The selection contains 87 functions with surviving high-p-code STORE operations.
The full scalar corpus has 96 raw store-bearing functions: nine more have stores
removed by high-level optimization, including floating-point stack conversions.
Those nine remain explicitly outside this selection; the other 15 getter-
unsupported functions have no raw store. The 59 accepted getters are separate.

The three rules cover 24 functions: 15 masked word stores with dirty flags,
seven byte stores with dirty flags and two direct word stores. The remaining
63 candidates stay unsupported. Six accepted functions belong to the reserved
partition. None of the 24 actual functions has overlapping value/dirty offsets;
the synthetic alias checks are additional oracle tests only.

Validation on 2026-09-22 passed all 24 offline suites. The setter harness ran
14,256 actual guest invocations, 214 negative controls and 22 synthetic alias
oracle checks in 0.428223 seconds. Full-facts analysis/report generation took
0.9927 seconds including Python startup. These are rerun measurements, not
development-cost or manual-comparison benchmarks. The authoritative run is
`out/renderer-offline/20260922T165934.590088Z/report.json`.

The first run caught a negative-control setup error: repeated dirty-flag bytes
could be unchanged by byte reversal. The repaired control initializes distinct
bytes, verifies its baseline, and rejects the reversed result. The failed run
is retained at `out/renderer-offline/20260922T165902.016651Z/report.json`.

The harness extracts unchanged generated bodies and compiles them against the
real SDK. Instrumented store wrappers record address, width, value and order,
then execute the original generated-header volatile store macros. A separate
byte/bit evaluator checks the entire context, memory and ordered write trace.
Ordinary aligned guest memory is a precondition; MMIO, physical aliases and
concurrency are outside this pilot.

Masked setters preserve the high half of the merge destination register. Their
value store occurs before the 64-bit dirty-flag load and store. The evaluator
therefore reads dirty flags from the memory resulting from the first write.
Synthetic overlapping-offset checks exercise that oracle behavior separately
from the fixed offsets present in the actual guest functions. They do not imply
that runtime receiver alias populations have been established.

Negative controls corrupt output registers, bytes, write widths, missing writes,
write order and byte order after a valid baseline. They test the comparison
mechanism; they are not compiled mutations of guest instruction bodies. Static
rule tests separately mutate instruction words and reject unsupported variants.

The deterministic reserved partition is not a blinded benchmark. Reported batch
execution time excludes development and review; no net speedup over manual
analysis is established. Native replacement equivalence remains untested.

## Bounded remaining work

| Work | Dependency | Completion test |
|---|---|---|
| Unsupported setter families | Frozen selection and per-function rejection reason | Add an exact family rule plus independent executable oracle and mutation controls; leave unmatched members unsupported |
| Floating-point and stack-store effects | Raw instructions as well as optimized high-p-code | Account for stores removed by decompiler optimization; do not equate absent high-p-code STORE with absent machine stores |
| Shared-tail `sub_82137978` | Entry and shared body evidence | Keep the thunk transitive/unsupported until tail `sub_821370E0`, its call and receiver bounds have accepted contracts |
| Receiver, ownership, lifetime and concurrent writes under R09.effects | Caller population and field provenance | Bound admitted objects/modes and writer synchronization; setter-local state agreement does not close this |
| Native differential execution | A native replacement and accepted preconditions | Compare original/native state, memory and ordered effects on identical reset inputs |

The shared-tail discrepancy is understood as a representation difference: the
four-instruction entry branches at 82137984 to 821370E0. Generated C++ represents
this as a call to another generated function; Ghidra includes the 49-instruction
tail in the natural body. The tail performs further loads, conditional clamps,
stores and a call to sub_82134BD8. It must not be normalized away or treated as
a complete four-instruction local setter contract.
