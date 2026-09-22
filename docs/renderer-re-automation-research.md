# Automating renderer reverse engineering

Research date: 2026-09-22. Epistemic session: `session-20260922161343-da2e649e`.

The recommended direction is a reusable analysis and validation pipeline: extract facts once, derive bounded contracts with explicit rules, compose effects across calls, and send agents only unsupported cases and counterexamples. This is a proposed architecture, not a measured whole-project speedup. No surveyed source establishes turnkey complete reverse engineering of this Xenon renderer.

This survey covers public primary documentation and research through the research date, including 2026 preprints. Benchmark results below are author-reported, not independently replicated here. Tool support claims must be separated from compatibility with our precise PPC language, VMX instructions, ABI and guest memory model. No external analyzer was installed or benchmarked on EDF during this survey.

## What the project measurements actually show

Reproduce with `python out/re-automation-survey/profile.py`; inputs and hashes are in `out/re-automation-survey/profile.json`. The SQLite database was opened read-only and its hash checked against the existing census baseline. The final profiling command took approximately 1.50 seconds according to the command tool; this is classification of existing export shapes, not semantic analysis or validation throughput.

| Observation | Measured count | Consequence |
|---|---:|---|
| Census functions with database instruction rows | 9,216 / 9,216 | Bulk access already exists; agents need not retrieve bodies individually. |
| Instruction rows | 755,720 | A bounded, machine-readable input corpus. |
| Distinct complete instruction-word sequences | 9,208 | Exact duplicate elimination alone barely reduces this corpus. |
| Distinct mnemonic-only sequences | 7,162 | Even coarse syntax grouping leaves substantial diversity. |
| Functions in repeated mnemonic groups | 2,688 | Candidate families, not established equivalent behavior. |
| Singleton mnemonic groups | 6,528 | Similarity alone cannot solve the workload. |
| Functions containing indirect sites | 1,326 | Receiver analysis is concentrated in a subset. |
| Indirect sites | 3,332 | Preserve every unresolved site. |
| Vtable-load-pattern sites | 2,697 | Worth building a reusable receiver-provenance analysis. |
| Constant-offset / register-pointer sites | 404 / 231 | Separate patterns with unresolved object/target meanings. |
| Functions without recorded direct or indirect sites | 1,563 | Useful triage candidates; incomplete CFGs and local effects remain possible. |
| Scalar dispatch sample | 170 functions / 1,443 instructions | A concrete automation benchmark already available. |
| Scalar mnemonic patterns | 40 | Parameterized semantic rules may amortize review. |
| Largest two scalar patterns | 40 masked-load getters; 19 load/return shapes | First candidate pilot; validate operands and full control flow. |

Identical instruction words are not a semantic proof: PC-relative targets and environment can differ. Mnemonic grouping deliberately ignores operands. No tally status was advanced by this experiment. Evidence: `ev-20260922161857-00813869`.

## Reuse before introducing new infrastructure

Existing code already does part of the extraction:

- `tools/ghidra/RendererInventory.java` exports call edges, indirect sites and raw p-code store presence.
- `tools/ghidra/GeometryWriterTrace.java` exports selected high-p-code store/call frontiers and rejects misleading truncated decompilation. It explicitly does not prove alias coverage.
- `tools/inventory-scalar-dispatch.py` validates raw scalar instructions against the image and emits syntactic classifications.
- `tools/inventory-small-retained.py` contains a large manually authored effects dictionary. Generating guarded, structured effects would remove repeated manual narration here.
- `tools/resolve-renderer-inventory-sites.py` uses limited instruction-comment lookback for load patterns. Replace this progressively with definition/use and memory provenance, retaining current instruction locators as witnesses.
- The Ghidra export language is `PowerPC:BE:64:64-32addr`. An ordinary PPC32 preset is not an interchangeable execution model.
- `generated/default/edf2017_pch.h` includes byte-order conversion, physical aliases and MMIO routes. Generic C++ pointer analysis does not automatically recover guest address ownership from these macros.
- The existing static-pass replay is explicitly synthetic and does not execute original guest hooks. It remains useful, but is not the differential guest-function runner proposed below.

These observations are recorded in `ev-20260922161938-585a7467`.

## Tool and technique decisions

| Technique | Proposed use | Decision and limit |
|---|---|---|
| Ghidra batch raw/high p-code export | Canonical CFG, operand, use/definition and effect facts | Primary starting point: existing project investment. Export failures and unsupported operations explicitly. |
| Strict parameterized instruction/IR rules | Getters, bitfield setters, wrappers, copies, reference operations | First implementation target. Every accepted member must satisfy the rule; reviewing one representative is insufficient. |
| Reaching definitions and abstract interpretation | Track argument/global/stack/allocation-relative addresses and callbacks | Build a bounded domain on existing facts; prototype angr where it reduces work. Unknown joins and aliases remain unknown. |
| BSim, Function ID, learned similarity | Retrieve candidate families and known library matches | Assistance only. Similarity cannot establish equivalence or ownership. PPC-specific library references are needed for FID. |
| Datalog-style rules | Query allocation → constructor → field write → receiver → callsite witnesses | Begin with the existing Python/SQLite environment; add Souffle only if measured rule/scale needs justify it. DDisasm and OOAnalyzer supply design precedents, not compatible drop-in analyzers. |
| Function effect summaries | Avoid repeatedly exploring the same callees | Include preconditions, mode guards, alias assumptions, outputs, ordered events and unresolved effects. Recursion needs a fixed-point process. |
| Differential execution and fuzzing | Compare isolated original guest behavior with proposed native behavior | Highest-value validation addition; environment setup/reset and observable-effect definition are real work. |
| Bounded symbolic checks | Prove a small getter/setter rule or produce a counterexample | Restrict instruction set, memory assumptions and solver budget. A timeout is inconclusive; a proof applies only to its model. |
| Retypd / data-layout inference | Recover candidate fields and structure constraints | Useful later where receiver analysis is blocked; inferred structure does not establish object lifetime. |
| rev.ng | Learn incremental invalidation and data-layout design | Do not migrate now: the inspected current architecture enum omits PowerPC. |
| Macaw/Crucible | Alternative PPC symbolic prototype | Explicit PPC32/64 support, but Xenon details remain unverified; integration is not yet justified. |
| ReVa / MCP automation | Improve exceptional-case access to Ghidra | Optional convenience; the current batch extractors already avoid many tool round trips. |
| LLM decompilation and agent frameworks | Suggest rules, adapters, tests and explanations | Their output remains a hypothesis until checked. Readability and function naming are separate from executable correctness. |

Primary-source evidence and applicability caveats for every row are indexed below. The architecture recommendation is `claim-20260922161938-7d034e42`, intentionally a hypothesis pending a project pilot.

## Proposed pipeline

1. **Versioned fact store.** Key facts by image hash, guest address, byte range, processor specification, extractor version and relevant configuration. Export raw instructions plus high-p-code SSA/CFG data when available. Keep raw and decompiler-derived facts distinct. Do not rerun Ghidra or reread unchanged bodies for each worker.
2. **Machine-checkable local contracts.** Normalize register temporaries while preserving widths, signedness, address-space semantics, masks, offsets, branch guards and call targets. Classify regions as stack, argument-relative, global, allocation-relative, MMIO or unknown. A rule emits its exact member list, parameter values, preconditions and instruction witnesses.
3. **Composed summaries.** Process call-graph components bottom-up; substitute arguments into callee summaries. Iterate recursive components to a conservative fixed point. Unknown callees propagate unknown effects rather than being treated as harmless. Cache callers against the summaries they used.
4. **Demand-driven receiver and ownership analysis.** Start from the renderer boundaries and resource mutations blocking the next implementation. Trace relevant field writers, aliases, vtable installations, registration and teardown. Do not declare a slot's compatible targets to be its receiver population. Preserve a path for every unresolved mode or writer.
5. **Offline original/native comparison.** Run isolated generated guest functions on reset register/memory state; compare with native candidates using return/register contracts, writes to externally relevant memory, and ordered callback/allocation/retirement/MMIO events. Model permitted dependencies explicitly. Unknown external calls fail the case. Retain minimized failures as fixtures.
6. **Incremental exception queue.** Agents receive the failing rule, evidence slice, counterexample and unresolved question. A successful assignment contributes an analyzer rule, a verified library summary, or an executable fixture reusable across functions. Routine reports and per-function evidence links are generated.
7. **Milestone integration.** Use gameplay captures for new subsystems, mode coverage and replay seeds at agreed milestones. They complement static and offline checks; one observed trace never closes every possible receiver or concurrency path.

Effect ordering matters: comparing only final bytes misses release-before-use, callback mutation, reference-count transitions and synchronization errors. Memory comparisons need explicit observability and alias rules, rather than excluding every stack write or comparing arbitrary padding. Floating-point tolerances must be operation-specific; byte equality is preferable when the original contract requires it. The generated guest and Ghidra may share errors or assumptions, so cross-check supported instruction semantics and use independently constructed negative controls.

## Bounded engineering tasks, not another open-ended research campaign

These task cards originated as proposals. The [scalar automation pilot](renderer-scalar-automation-pilot.md)
now implements the bounded AUTO-01/02 work and original-execution portion of
AUTO-03. Native comparison and broader automation remain open; no parent
renderer task was completed by this pilot.

| ID | Deliverable and scope | Dependencies | Completion test |
|---|---|---|---|
| AUTO-01 | Export stable, typed raw/high-p-code facts and quality flags for the 170 scalar functions; prepare a whole-census batch mode. Reuse current Java exporters. | Existing image and inventory hashes | All 170 accounted for; instruction coverage agrees with raw export; undecodable operations and CFG gaps are explicit; repeat output is deterministic. |
| AUTO-02 | Implement one parameterized rule for the 40-member masked-load getter shape, then the 19 load/return candidates. Emit exact operands, return expression and memory-read preconditions. | AUTO-01 | Each accepted member passes full-body/control-flow matching; offset, mask, width, base-register, conditional-return and added-store mutations are rejected or change the summary. Unmatched candidates stay open. |
| AUTO-03 | Build a resettable guest-function runner for this scalar subset with mapped guest memory and explicit ABI state. Add a separate reference expression evaluator and native adapters where available. | AUTO-01; AUTO-02 for expected contracts | Original guest, contract evaluator and available native adapter agree on held-out cases; dirty globals, aliasing, endian errors and unsupported calls trigger failures; no game launch. |
| AUTO-04 | Add guarded setter summaries including dirty-state writes and ordered events; test dependency substitution on simple wrappers. | AUTO-02, AUTO-03 | All supported effects reproduced; unsupported members reported; unknown callees cannot disappear; at least one deliberately wrong mask/store/order is caught by the oracle. |
| AUTO-05 | Cache summaries and propagate dependency invalidation; generate tally proposals for explicit obligations. | AUTO-01, AUTO-02 | Callee or instruction change invalidates dependent summaries; unrelated change preserves them; stale evidence and missing members reject acceptance. No broad task/function closure from a local effect alone. |
| AUTO-06 | Analyze one receiver field end-to-end, starting with manager+132 / scene-begin site821A5158; generalize queries only after recovering accepted pilot witnesses. | AUTO-01, AUTO-05; existing two pilot contracts | Recover known startup/setter/teardown path; planted alternate writer creates an unresolved edge; compatible vtables never become exhaustive population proof. |
| AUTO-07 | Rank remaining boundaries by blocked implementation, reuse/fan-in and analysis uncertainty; dispatch bounded exceptions. | AUTO-05, AUTO-06 | Every dispatched exception has tally IDs, source witnesses, a bounded question, an oracle and a stop condition; no unresolved row is dropped by grouping. |

Start AUTO-01 and the narrow getter rule before building a general symbolic engine or adopting another large framework. AUTO-03 can be developed against the same frozen facts once the ABI/observables are specified. This is the first useful small parallel wave; ownership should separate extractor, rule checker and harness files, with a coordinator reviewing their shared schema.

A local contract can close a local-effect obligation while lifetime or runtime-mode obligations remain open. Preserve that granularity in the tally. The 108,048 rows are coverage records, not 108,048 requests for prose.

## Pilot measurement and acceptance

Before rule development, freeze the 170-member corpus, known contracts and a holdout split; retain unseen operand combinations and adversarial mutants. Do not call a random sample review a proof for the rest of a family. Machine checks run on every accepted member; independent review validates the rule, assumptions and oracle.

Report: extraction coverage, accepted contracts by rule, rejected/unsupported members, disagreement count, mutation detection, held-out failures, analysis runtime, peak memory, agent tokens, coordinator review time and setup cost. Compare equivalent accepted obligations against a measured manual baseline on the same family. The two earlier pilots covered much harder ownership questions, so their timings are not a fair getter benchmark.

Use total setup + processing + review cost divided by accepted obligations to estimate the break-even point. Measure incremental rerun cost separately. Set a target speedup before scaling, but do not advertise a multiplier or a percentage of all 9,216 functions until this pilot demonstrates it. A low-coverage conservative analyzer can still be useful; a fast unsound classifier cannot.

## Sources and Epistemic evidence

The following are source-reported capabilities or findings, with applicability judgments kept distinct from measured EDF results. Research abstracts were used to establish benchmark scope/results, not to claim that full experimental methods were reproduced.

| Primary source | Finding and boundary | Epistemic evidence |
|---|---|---|
| [Ghidra BSim retrieves structural similarity, not equivalence](https://github.com/NationalSecurityAgency/ghidra/blob/master/GhidraDocs/GhidraClass/BSim/README.md) | Official BSim documentation describes decompiler-derived function similarity across architectures, compilers and small source changes. Applicable as family/candidate retrieval; no EDF benchmark was run. | `ev-20260922161517-da415eb3` |
| [Perfect BSim similarity does not make datatype transfer safe](https://github.com/NationalSecurityAgency/ghidra/blob/master/GhidraDocs/GhidraClass/BSim/BSimTutorial_Evaluating_Matches.md) | Official evaluation tutorial warns that datatype changes can cause incorrect imported types even with similarity1.0. Similarity-based grouping cannot by itself authorize contract closure. | `ev-20260922161517-567028c5` |
| [Ghidra emulation requires explicit instruction and environment fidelity](https://ghidra.re/ghidra_docs/GhidraClass/Debugger/B2-Emulation.html) | Official documentation says p-code specifications primarily target decompilation; some require unimplemented user operations for emulation. An offline EDF runner must reject unsupported semantics rather than silently stub them. | `ev-20260922161517-0fbeaf2c` |
| [OOAnalyzer offers object recovery rules but its documented target excludes Xenon](https://github.com/cmu-sei/pharos/blob/master/tools/ooanalyzer/ooanalyzer.pod) | Official manual describes fact extraction plus Prolog rules for classes, layouts, constructors/destructors and virtual calls, and explicitly limits OOAnalyzer to32-bit x86 MSVC executables. Technique is relevant; tool is not a drop-in PowerPC solution. | `ev-20260922161517-f54fcf46` |
| [DecompileBench separates readability from executable correctness](https://arxiv.org/abs/2505.11340) | 2025paper evaluates23400functions from130programs across six conventional and six LLM approaches using runtime-aware checks and readability evaluation. Authors report LLM readability gains alongside worse functional correctness. These are source-reported benchmark findings, not project measurements. | `ev-20260922161517-a29399ec` |
| [August2026 realistic RE benchmark still reports incomplete agent success](https://arxiv.org/abs/2608.11469) | SRE-Bench preprint reports19private programs,262binary instances and1572graded tasks. Best evaluated model scores61.4% per-instance and completely solves31.5% of instances. This does not measure EDF, GPU behavior or our task distribution. | `ev-20260922161517-ca1c72fe` |
| [Whole-program reconstruction remains a distinct difficult benchmark](https://arxiv.org/abs/2605.03546) | May2026ProgramBench paper describes200reference-executable reconstruction tasks with behavioral tests. Its evaluated models fully solve none; best reaches95%test passing on3%of tasks. The black-box/documentation setting differs materially from our available generated source and Ghidra facts. | `ev-20260922161518-645456e8` |
| [Ground-truth alignment must itself be measured](https://arxiv.org/abs/2607.07738) | July2026REFORGE studies decompiled function naming with provenance-tracked source/binary alignment and an eight-gate confidence funnel. Its reported alignment losses across optimization levels caution against comparing unmatched function sets or treating names as behavioral contracts. | `ev-20260922161518-6fd74da9` |
| [rev.ng combines data-layout recovery with incremental model invalidation](https://docs.rev.ng/what-is-revng/) | Official docs describe QEMU lifting to LLVM, automated data-layout inference from pointer use, model-driven invalidation and Python/TypeScript automation. Xbox360/Xenon compatibility and benefit over our existing lifted code were not verified. | `ev-20260922161518-e1603cfc` |
| [Retypd exposes a frontend-independent constraint interface](https://github.com/GrammaTech/retypd) | Official repository describes binary type recovery by supplying disassembly-derived constraints to a solver. A custom facts frontend is possible in principle; adoption cost and EDF accuracy remain unmeasured. | `ev-20260922161518-4a560a72` |
| [ReVa automates Ghidra access in GUI and headless modes](https://github.com/cyberkaida/reverse-engineering-assistant) | Maintainer README documents an MCP interface and headless/PyGhidra automation. This improves tool access, not an independent semantic-verification oracle; no installation or project evaluation was performed. | `ev-20260922161518-d5ae7db6` |
| [SVF operates on LLVM-based source representations](https://github.com/SVF-tools/SVF) | Official repository describes pointer/value-flow analysis for LLVM-based languages. Applying it to this project's flat guest-memory/register representation would require frontend/model work; native source pointer precision cannot be assumed for guest addresses. | `ev-20260922161518-e058fe72` |
| [Function ID needs processor-appropriate reference libraries](https://raw.githubusercontent.com/NationalSecurityAgency/ghidra/master/Ghidra/Features/FunctionID/src/main/help/help/topics/FunctionID/FunctionID.html) | Official help describes body-hash library identification, relocation tolerance, and processor-specific databases; bundled references are MSVC x86. Xenon identification needs appropriate reference artifacts, not assumed bundled coverage. | `ev-20260922161857-9c4f64c6` |
| [angr provides CFG recovery and contextual analysis controls](https://docs.angr.io/en/latest/analyses/cfg.html) | Official CFG documentation describes static lifting and edge recovery, attempted indirect-jump resolution, symbolic CFG alternatives and context-sensitivity cost. These provide targeted-analysis building blocks; complete runtime callback population is not promised. | `ev-20260922161857-27fc1af9` |
| [angr exposes machine-readable definition/use observations](https://docs.angr.io/en/stable/api/angr.analyses.reaching_definitions.reaching_definitions.html) | Official API exposes reaching definitions, uses, dependency graph and per-instruction observation points. It is a candidate for extracting argument/effect provenance after a PPC semantic compatibility test; no EDF run performed. | `ev-20260922161857-fb686ab2` |
| [DDisasm illustrates scalable declarative fact processing](https://github.com/GrammaTech/ddisasm) | Maintainer README describes Souffle Datalog rules over decoded binary facts, producing GTIRB and reassemblable output. Applicable architectural lesson is fact extraction plus auditable rules. No claim that DDisasm supports this Xenon image. | `ev-20260922161857-6147fe0b` |
| [Macaw explicitly documents PPC32/PPC64 symbolic-analysis support](https://github.com/GaloisInc/macaw) | Official repository lists PowerPC32/64 and lifting to Crucible symbolic execution. It does not establish our Xenon VMX128/ABI/memory-model compatibility. Candidate alternative for a bounded proof prototype, not a wholesale migration recommendation. | `ev-20260922161857-72fb3b1d` |
| [pypcode supplies Python access to SLEIGH lifting](https://github.com/angr/pypcode) | Official repository describes Python disassembly and p-code translation bindings built for use with angr. Using the same SLEIGH semantics as Ghidra is not an independent check of lifting correctness. | `ev-20260922161857-dfc125a5` |
| [Repeated offline fuzzing needs fast deterministic resettable harnesses](https://llvm.org/docs/LibFuzzer.html) | Official libFuzzer documentation specifies repeated in-process target execution, determinism and narrow targets; documents coverage and structured mutators. Proposal is guest/native differential execution with reset memory and explicit boundary models, not gameplay fuzzing on every change. | `ev-20260922161857-b450d9f9` |
| [LLVM libc documents differential fuzz tests against a reference implementation](https://libc.llvm.org/dev/fuzzing.html) | Official LLVM libc documentation describes differential tests comparing results with a system-libc reference. This is precedent for oracle-based function testing; it does not prove any particular EDF reference implementation correct. | `ev-20260922161857-a9c0e85a` |
| [Current rev.ng model reference omits PowerPC](https://docs.rev.ng/references/model/) | Architecture enum in inspected current reference lists x86,x86_64,arm,aarch64,mips,mipsel,systemz, no PowerPC. This materially weakens rev.ng as a direct EDF tool despite useful data-layout/incremental-design ideas. | `ev-20260922161857-43d6a418` |
| [Data-dependence slices can improve similarity representation](https://arxiv.org/abs/2411.12454) | StrTune2024paper proposes backward data-dependence slices plus learned similarity to address syntax/reordering variation. It remains a retrieval method; no equivalence proof or EDF performance established. | `ev-20260922161857-0ae8add0` |
| [CrackMeBench supports bounded oracle-scored agent evaluation](https://arxiv.org/abs/2605.10597) | May2026preprint uses deterministic validation tasks, executable oracles, limited submissions and measured time/tokens. Small educational tasks do not establish scalability to renderer lifetimes, concurrency or GPU state. | `ev-20260922161857-35c8d048` |
| [LLM4Decompile evaluates reconstruction using re-executability](https://github.com/albertan017/LLM4Decompile) | Author repository describes assembly-to-source and Ghidra-refinement models with tests of reconstructed functions. Relevant as a proposal generator; passing provided tests is not complete behavioral equivalence, and our PPC input distribution is unvalidated. | `ev-20260922161857-7b5f7b67` |

The survey changed documentation and research artifacts only. It did not install tools, change renderer code, close research obligations, or boot the game.
