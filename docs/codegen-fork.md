# ReXGlue codegen fork (phase 2: an optimizing code generator)

Goal: make the recompiled guest code (14,335 functions, `generated/default`) faster by
changing what the code generator emits, while keeping guest results **exact**. Phase 1
(branch `codegen-overhead`, `docs/codegen-overhead.md` there) found that only ~9-12% of
guest-code time is real work; the rest is the PPCContext register file (24%), guest
memory access (15-18%), CR/XER flags (13-17%), single/double conversions (10-13%), address
arithmetic and the physical-offset check (15%), calls and save/restore (6-8%).

This page covers the fork, the first optimization (guest registers as C++ locals with an
interprocedural call protocol), the in-process exactness audit, the static ABI survey,
and what comes next. Everything is opt-in; the default build is unchanged.

## Where things are

| what | where |
|---|---|
| codegen fork | `D:\roms2\rexglue-sdk-edf`, branch `edf-codegen` (local only, never pushed; `origin` is upstream rexglue-sdk). Base: v0.10.0 `c94f5eb`, the version the prebuilt SDK was built from |
| the pass | `src/codegen/edf_ipa.{h,cpp}` in the fork; hooks in `builders/context.cpp` (token mode), `function_graph.cpp` (capture), `codegen_writer.cpp`, `config.{h,cpp}` |
| game side | this branch (`codegen-fork`, worktree `.claude/worktrees/codegen-fork`): `CMakeLists.txt` (two options), `edf2017_codegen_fork.toml` (fork options, included by the manifest), `src/codegen_fork/` (audit), `tools/codegen-fork/` (smoke run, survey) |

The prebuilt runtime (`rexruntime.dll`, the PPCContext layout, the dispatch table and its
exports) is untouched: only the codegen tool is rebuilt.

### Building the fork

Submodules were initialized from the edf3 SDK checkout's module store (read-only use; no
network). libmspack's git symlinks check out as text files on Windows and were replaced
by copies (a local, uncommitted change in the submodule). Then:

```
call VsDevCmd.bat -arch=amd64 -host_arch=amd64
cd /d D:\roms2\rexglue-sdk-edf
cmake --preset win-amd64 -DREXGLUE_ENABLE_FIDELITYFX=OFF -DREXGLUE_ENABLE_TRACY=OFF
cmake --build out\build\win-amd64 --config Release --target rexglue
```

gives `out\win-amd64\Release\rexglue.exe` (links the whole runtime; ~620 steps the first
time, seconds after that). The unmodified fork regenerates `generated/default`
byte-identically (only `codegen.build.stamp` / the partition file's line endings differ,
and the prebuilt tool writes the same stamp). With every fork option off, the patched fork
is also byte-identical.

The fork adds its own image hash to the codegen fingerprint (`project_recompiler.cpp`), so
a rebuilt tool re-runs codegen instead of trusting the old stamp.

### Game-side switches

- `-DEDF_REXGLUE_CODEGEN_EXE=D:/roms2/rexglue-sdk-edf/out/win-amd64/Release/rexglue.exe`
  (CMake cache, empty by default): the codegen step uses that tool; the game still links
  the prebuilt runtime. The codegen step depends on the exe, so rebuilding the tool re-runs
  codegen.
- `edf2017_codegen_fork.toml` (included by `edf2017_manifest.toml`; the stock tool ignores
  its keys, so the default build regenerates identically):

| key | default | meaning |
|---|---|---|
| `edf_ipa_locals` | false | the pass below |
| `edf_ipa_abi_boundary` | false | trust the PPC ABI at opaque boundaries (below); not bit-exact for dead volatile values |
| `edf_ipa_vtables` | false | model RTTI virtual calls by slot target sets, guarded (step 2; negative result) |
| `edf_ipa_fast_abi` | false | `__fast_sub_X` register-passing variants (step 3) |
| `edf_ipa_audit` | false | emit both bodies of every replayable function plus a wrapper calling `edf_ipa_audit()`; needs `-DEDF_CODEGEN_FORK_AUDIT=ON` |
| `edf_ipa_only` / `edf_ipa_exclude` | [] | restrict the pass to / keep out of it (the 138 bodies fingerprinted by `tools/extract-native-*.cmake` are excluded, so those scripts keep passing) |
| `edf_hooked` | 293 addresses | functions replaced by native hooks in this build (`sub_X != __imp__sub_X` in the PDB) |
| `edf_external` | 324 addresses | guest functions named anywhere in `src/` (hooks, native callers, `REX_EXTERN`) |
| `edf_ipa_report` | out/codegen-fork/ipa-report.jsonl | one JSON line per function (the survey's input) |

- `-DEDF_CODEGEN_FORK_AUDIT=ON`: links `src/codegen_fork/ipa_audit.cpp` and appends
  `ipa_audit_pch.h` to the recompiled-code target (every scalar guest store is journaled
  while an audit replays a call). Audit builds only.

Example (locals, exact): set `edf_ipa_locals = true`, configure with the codegen exe, build
the `win-amd64-release-nopgo` preset (PGO drops out for changed functions anyway).

## The pass: guest registers as locals, ctx synced by interprocedural summaries

### Idea

Every guest register a function touches becomes a C++ local (`PPCRegister r31 = ctx.r31;`),
so clang keeps it in a host register, deletes dead flag computations and forwards values.
PPCContext stays the architected state **only at boundaries** where someone else can look:

- before a call: write back the dirty locals the callee may read, plus the dirty live ones
  the callee may write (a "may write" callee that does not write leaves ctx's old value,
  so a live dirty register must be in ctx before the call);
- after a call: reload what the callee may have written (dead reloads are dropped by clang);
- at a return or tail call: write back what is dirty and live after the function.

What "may read", "may write" and "live after" mean comes from interprocedural summaries,
so a call to a small leaf costs a store or two instead of a round trip of every register.

### How it is built

1. **Token emission.** Every function is emitted once more in *token mode*: the builders
   print `@R3@`, `@F1@`, `@C6@`, `@CTR@`, `@XER@`, `@LR@`, `@RES@` where they would print
   `ctx.r3` etc. (the link register got an accessor; it was a string literal in four
   builders). The per-instruction text is captured. This is exactly what the normal
   emission prints, modulo spelling, so the analysis sees the code that will run.
2. **Model.** Each instruction's text is parsed into statements with register reads,
   writes, full-width kills (`.u64/.s64/.f64 =`, whole-object assignment, `compare`,
   `setFromMask` at statement level, unconditional), partial writes (read+write),
   calls (direct by name, `REX_CALL_INDIRECT_FUNC`, anything else that is handed `ctx`),
   tail calls (call + `return;`), returns, gotos, traps and switch tables. `ppc_trap` is
   known to read r3/r4 only. Anything unexpected (nested braces, `else`, a conditional
   non-tail call inside a block, setjmp/SEH) keeps the function in the ctx form.
3. **Summaries (bottom-up over call-graph SCCs).** `READS(f)`: registers whose value on
   entry may be observed (by f's instructions, by what f writes back, by callees reading
   ctx). `WRITES(f)`: registers f or its callees may change. Imports, indirect calls,
   hooked functions and unparsed bodies are opaque (read and write everything). Excluded
   (fingerprinted) bodies keep their ctx text but get a computed summary.
4. **LIVEOUT (top-down).** `LIVEOUT(f)` = a runtime base (r1-r4, r13, f1-f4, lr,
   non-volatiles) | the needs after every call of f | what any opaque call site may
   observe (a callback run by an import, an indirect target). Functions named in native
   sources, hooked functions, the entry point and the callees of unparsed or fingerprinted
   bodies keep everything.
5. **Fixpoint.** READS/WRITES/LIVEOUT are only ever enlarged, so at the end every
   function's computed sets are contained in the published ones: the protocol is sound
   for recursion too (58,822 summary iterations in 3 rounds, ~9 s of codegen in total).
6. **Emission.** Locals are loaded at entry (dead loads vanish), a forward may-dirty
   analysis and an architected-liveness analysis give the write-backs:
   `W(call g) = dirty & (READS(g) | (WRITES(g) & live-after))`,
   `W(tail g) = dirty & (READS(g) | LIVEOUT(f))`, `W(exit) = dirty & LIVEOUT(f)`;
   reloads `WRITES(g) & used(f)`. `if (c) return;` / `if (c) ppc_trap(...)` become blocks
   when they need write-backs.

Why it is exact: guest memory operations are the same statements in the same order; every
value a callee, the runtime or a native hook can read from ctx is written back first, and
every value a callee can change is reloaded; registers that are not written back are dead
for every observer the analysis can see. Native code is covered by `edf_external` /
`edf_hooked` (LIVEOUT and READS = everything).

**Exact mode vs ABI mode.** In exact mode an opaque callee may read *every* register, so
anything live across two opaque calls is live everywhere and LIVEOUT stays "everything";
the gain is in function bodies and in calls to analyzable callees. That pessimism is the
price of bit-exact *dead* state (e.g. an APC run inside a kernel wait doing `mfcr` saves
the volatile CR fields' garbage into a stack slot). `edf_ipa_abi_boundary = true` trusts
the PowerPC/Xbox 360 ABI at opaque boundaries: imports, indirect calls and hooks read only
the argument registers, r1/r2/r13, lr, xer and the non-volatiles, and a function entered
from one owes only the ABI results. Guest memory and all *live* registers are still
computed exactly; only values of dead volatile registers at opaque boundaries may differ.
It stays off by default.

Functions left in the ctx form: 138 fingerprinted (excluded), 127 SEH funclets
(`shares-registers`), 32 stubs. **14,038 of 14,335 (97.9%) get the locals form**; no
function fails to parse.

## Exactness gate: in-process differential audit

The cross-run guest-hash trace cannot gate this: phase 1 found two baseline runs differ
from the first step (timer writes, a time seed, thread races, stale stack). The gate is
therefore in-process, per call:

- With `edf_ipa_audit`, every *replayable* function gets three symbols: the locals body
  (`__edfcand_sub_X`), the ctx body exactly as the stock tool emits it (`__edfref_sub_X`)
  and `sub_X`, a wrapper calling `edf_ipa_audit(index, addr, ctx, base, cand, ref, LIVEOUT)`.
  Replayable = nothing in the call closure has an effect outside guest memory and
  registers: no import, hook, indirect call, MMIO, raw or atomic store (dcbz, stvx,
  stwcx.), time base or global lock. 3,760 of the locals functions qualify.
- `src/codegen_fork/ipa_audit.cpp`: sampled calls (first 2 per function, then every
  512th; `--edf_ipa_audit_first/_every`) run the candidate with a store journal
  (`ipa_audit_pch.h` redefines `REX_STORE_U8..U64`), capture the context, roll guest memory,
  the context and the guest-owned MXCSR bits back, run the reference, and compare every
  byte either run wrote and every register in the function's LIVEOUT (plus msr, fpscr,
  vscr_sat, all vector registers). Nested calls run the candidate directly; the
  reference's result is the one that stays.

Results (muted, uncapped, release-nopgo + audit):

| build | scenario | audited calls | functions | mismatches |
|---|---|---|---|---|
| first cut | M1 (benchmark-skipintro, 220 s) | 3.2 M | 1,538 | 72 register mismatches in 11 functions, 0 memory |
| fixed protocol, exact | M1 (200 s) | 2.9 M | 1,537 | **0** |
| fixed protocol, exact | horde-ants-1000 (full script) | 7.67 M | 1,526 | **0** |
| ABI mode | horde-ants-1000 (full script) | 7.76 M | 1,535 | **0** |

The first cut reloaded every register a callee *may* write after the call; when the
callee did not write it on that path, the caller read ctx's stale value (e.g. r10 in
sub_823256F0, cr0 in sub_8232E4B0). The audit found it on the first run; the write-back
rule above fixes it.

What the audit does not cover: functions whose closure has an opaque call (their bodies
use the same machinery, and they boot and run, but nothing replays them), and whether the
ABI-mode assumption holds at opaque boundaries (by construction it cannot see them).
Exception paths that unwind through a locals-form frame into a guest SEH scope (the
generated `SEH_CATCH_ALL`) would see stale ctx registers; the scenarios logged no
`SEH exception caught`.

Smoke runs (non-audit builds, `tools/codegen-fork/smoke.ps1`): exact and ABI mode both
reach the M1 street and play (200 s) and run the whole 1000-ant horde script to its
`quit`, with no faults in the log.

## Survey: how many functions follow the ABI (static analysis)

`tools/codegen-fork/survey.py out/codegen-fork/ipa-report.jsonl`. "ABI inputs" = argument
registers r3-r10/f1-f13, r1/r2/r13, lr, xer, non-volatiles (r14-r31, f14-f31, cr2-cr4);
"ABI results" = r3/r4, f1-f4 plus those.

| | exact mode | ABI mode |
|---|---|---|
| functions with a computed summary | 13,895 | 13,909 |
| READS within the ABI inputs | 3,246 (23.4%) | 6,780 (48.7%) |
| LIVEOUT within the ABI results | 0 | 4,936 (35.5%) |
| both | 0 | 2,843 (20.4%) |
| READS of more than 64 registers | 9,536 | 7,605 |

Why functions fall outside the ABI (exact mode): 9,599 read "everything" because an
opaque callee sits in their closure (1,970 contain an indirect call themselves, 404 call
an import or CRT function, 293 hooks, the fingerprinted bodies); 700 read volatile
scratch registers on entry (r11/r12 fragments of a parent function, the `mfcr` pattern);
350 read only volatile CR fields (the `mfcr` that saves the whole CR to keep cr2-cr4).
In ABI mode most of what remains is argument-register pessimism: an import or an
indirect call is assumed to read all of r3-r10 and f1-f13, so arguments nobody set are
still "live" across calls.

Hot functions (engine-thread share from the phase-1 profile):

| function | horde | M1 | form | READS exact / ABI | LIVEOUT (ABI) | ABI inputs (ABI mode) | replayable |
|---|---|---|---|---|---|---|---|
| sub_821AEE50 | 20.1% | 5.4% | locals | 70 / 56 | 57 | yes | no (indirect via 821CE4C0) |
| sub_821C38F0 | 8.0% | 2.7% | locals | 3 / 3 | 65 | yes | yes |
| sub_8211CE10 | 5.2% | | locals | 67 / 67 | 49 | no | no |
| sub_821AF100 | 4.3% | | locals | 70 / 56 | 57 | yes | no |
| sub_821AF7A0 | 3.4% | | locals | 65 / 55 | 57 | yes | no |
| sub_821AF328 | 2.7% | | locals | 70 / 57 | 49 | yes | no |
| sub_821B0258 | 2.5% | 7.8% | ctx (fingerprinted) | 3 / 3 | 76 | yes | yes |
| sub_820D6868 | 2.2% | | locals | 68 / 61 | 49 | yes | no |
| sub_821C58F8 | 2.1% | 2.1% | locals | 69 / 61 | 49 | yes | no |
| sub_8211CFF0 | 2.1% | | locals | 7 / 6 | 49 | yes | yes |
| sub_821C9688 | | 7.0% | locals | 22 / 22 | 73 | yes | yes |
| sub_821C9478 | | 3.0% | locals | 76 / 76 | 76 | no | no |

The collision cluster (821AEE50/AF100/AF328/AF7A0) is poisoned by indirect calls two
levels down (821CE4C0 -> 821CE3A0, four `bctrl`, and 821C1628 -> 821C1370): virtual
shape/callback calls. The octree (821C38F0), 821C9688 and 821B0258 are clean.

### Static instruction mix (not a timing)

Phase-1's `static_stats.py` over the three binaries (guest functions only):

| | base | locals, exact | locals, ABI mode |
|---|---|---|---|
| instructions | 6.36 M | 8.27 M (+30%) | 7.22 M (+14%) |
| PPCContext accesses | 1.68 M | 1.13 M (-33%) | 1.00 M (-40%) |
| of which CR fields | 385 k | 57 k (-85%) | 17 k (-96%) |
| phys-offset sequences | 1.10 M | 0.95 M | 0.93 M |

The instruction count grows because of the call-boundary write-backs/reloads (static
totals: 637 k write-back and 1.29 M reload statements before clang's dead-code
elimination, for 82.7 k call sites) and host-stack spills (30+ live locals). The hot
bodies moved both ways: 8211CE10 903 -> 701 (-22%), 821C9688 1010 -> 780 (-23%),
821AEE50 1252 -> 1251, 821C58F8 474 -> 724 (+53%), 821AF7A0 289 -> 457 (+58%). Whether
the trade pays is a timing question (below).

## Timing (engine step dispatch ms per simulation step)

`tools/codegen-fork/time-series.ps1` runs the variants interleaved (round-robin, 3 rounds)
through `run-timed.ps1` (phase 1's script: waits for a quiet machine - no other EDF game,
no clang/ninja/lld - runs uncapped and muted with `--edf_step_timing`) and `tim.py`
summarizes: Mission 1 = benchmark route, `MISSION.CAM` +20..+80 s; horde = 1000 ants,
`hold-begin`..`hold-end`. All nopgo builds. Median of 3 runs (range):

| variant | options | M1 ms/step | vs base | horde ms/step | vs base | horde steps/s |
|---|---|---|---|---|---|---|
| base | stock codegen | 2.034 (1.978-2.189), n=6 | | 24.18 (21.84-25.29), n=6 | | 35.1 |
| ipa-exact | `edf_ipa_locals` | 1.615 (1.587-1.719), n=6 | **-20.6%** | 20.57 (17.97-22.22), n=6 | **-14.9%** | 40.4 |
| ipa-abi | + `edf_ipa_abi_boundary` | 1.529 (1.439-1.537) | -24.9% | 18.73 (17.90-19.20) | -22.5% | 46.7 |
| vt-exact | + `edf_ipa_vtables` | 1.690 (1.603-1.741) | -17.0% | 21.04 (20.49-21.65) | -13.0% | 41.3 |
| fast-exact | + `edf_ipa_fast_abi` | 1.535 (1.513-1.627) | **-24.6%** | 19.93 (19.31-20.55) | **-17.5%** | 43.0 |
| fast-abi | fast + ABI boundary | 1.562 (1.558-1.651) | -23.2% | 19.59 (19.36-19.95) | -19.0% | 43.4 |

Rounds 1-3 (base, ipa-exact, ipa-abi) and 4-6 (base, ipa-exact, vt-exact, fast-exact,
fast-abi) were separate series; base and ipa-exact ran in both and agree on M1 (base 2.041 /
2.027, ipa-exact 1.609 / 1.620). The horde base moved ~7% between series (22.9 / 24.5), so
compare the horde within a series: rounds 4-6 give ipa-exact -12.9%, fast-exact -18.6%,
fast-abi -20.0%. Raw logs: `out/codegen-fork/runs`; summaries
`out/codegen-fork/timing-{step1,step23,all}.txt`. The timing runs doubled as smoke tests:
every run of every variant finished its scenario (the horde ran to `quit`) with no errors,
except vt-exact's guard-miss log lines (below). `--edf_deterministic_steps` was dropped
for timing: with it, the benchmark's input script (which runs on the game clock) had not
reached `MISSION.CAM` after 8 minutes.

**Exact mode is faster, not slower**, despite +30% static instructions: most of the
call-boundary syncs are dead and clang removes them, and the bodies lose most of their
ctx traffic. The hot bodies that grew statically (821C58F8 +53%, 821AF7A0 +58%) call a
poisoned subtree (16+ non-volatiles written back before each call) with 30+ live locals
(host spills); the fast ABI and precise summaries target exactly that.

## Step 2: vtable slot target sets (negative result)

`edf_ipa_vtables` models `lwz vt,0(obj); lwz rY,N(vt); mtctr rY; bctrl` (one straight
path, no label or call in between, registers unchanged) by the union of the summaries of
slot N/4 of every RTTI vtable (the SDK's `VTableScanner`: 297 vtables, 1,798 slots in
0x82000988-0x82020678). The guard `EDF_VT_OK(vt, slot)` (a byte table holding the slot
count at each vtable start) is evaluated before the slot load. A miss calls
`edf_vt_miss()` (logged, counted) and takes a write-back-everything/reload-everything
path. `edf_vt_verify()` compares every vtable word in guest memory with the image (the
audit log prints it).

Results:
- 2,362 of 5,202 indirect call sites match the pattern, but **no summary improves**: a
  slot's target set is every class's slot N, and most targets themselves read everything
  (slot 0, the destructors: 161 of 185 targets read >64 registers, because they call
  operator delete, an import; slot 5, which 821CE3A0 uses: 27 of 30). The collision
  cluster stays poisoned (821AEE50 READS 70). In ABI mode the unions do worse than
  treating the call as an ABI-respecting opaque call (READS 56 -> 70), because a slot that
  holds a hooked or unparsed function becomes "everything".
- **The guard misses constantly**: up to 1.3e8 misses per run (sites 0x82240860,
  0x82240D28, 0x8224171C, 0x824158EC, ...). Many objects' vtables are not RTTI vtables
  (classes compiled without RTTI, COM-style interfaces), so "the vptr is a known vtable"
  is not a usable assumption here, and a miss path cannot be exact (the unknown target may
  read a register the caller's caller never wrote back). The audit (M1 2.8 M calls, horde
  7.7 M) still found 0 mismatches and `vtable_words_changed` stayed 0, but it covers only
  replayable functions, which contain no guarded site whose target misses.
- vt-exact times slightly *slower* than ipa-exact (M1 1.690 vs 1.620): the guards and miss
  logging cost something and buy no precision. Keep it off.

What would un-poison the cluster: precise per-site types (not available statically), or
making the opaque boundaries precise *and exact*. That means per-import summaries from the
SDK export shims (every export reads its ArgTranslator arguments, r1 for stack arguments
and r13; the exports that can run guest code on the calling thread, i.e. alertable waits
delivering APCs via `FunctionDispatcher::ExecuteTrap` (`xboxkrnl_threading.cpp:380-942`),
stay opaque), plus a verified "ABI class" for indirect targets: a generated per-function
class byte and a guard at each indirect site checking `class(ctr)` respects the ABI, with
misses logged. ABI mode's timing (-22.5% horde vs -14.9% exact) is the upper bound of
that work.

## Step 3: fast register-passing ABI

`edf_ipa_fast_abi` builds on the locals pass. Every locals-form, non-hooked function whose
READS contain at most 7 GPR and 8 FPR argument registers (r1, r3-r10, f1-f13) gets
`extern "C" EDF_FAST_CC void __fast_sub_X(ctx, base, a_r1, a_r3, ..., a_f1, ...)` with
`EDF_FAST_CC = __attribute__((regcall))`. Evidence for regcall: on x86_64-windows it
passes up to ~11 integer and 16 vector arguments in registers; the MS default passes four.
`sub_X` becomes the ctx wrapper (`__fast_sub_X(ctx, base, ctx.r3.u64, ...)`) for indirect
calls, the runtime, native code and hooks.

A direct call to a fast callee passes the callee's params from the caller's locals
instead of writing them back. The callee treats them as dirty from entry, so at its exits
it writes back exactly what its LIVEOUT needs. The caller keeps a passed register dirty
unless the callee may write it. Functions that read every argument register (poisoned)
stay ctx-only. 3,388 functions qualify in exact mode (3,495 in ABI mode). The
declarations go into the per-shard headers, so the fingerprinted bodies' extraction
boundaries are unchanged.

Not done yet: returning r3 in rax / f1 in xmm0 (the callee still writes r3 back and the
caller reloads it); tuning the argument limits by call-site count; r1 is still passed as
an argument (nearly everything reads it).

Audit: fast entries are audited too. In audit builds `__fast_sub_X` stores its arguments
to ctx and calls the audited wrapper, and the candidate's own fast body is renamed.
Result: M1 2.84 M calls in 1,536 functions, horde 7.71 M calls in 1,537 functions,
**0 mismatches**.

Timing: fast-exact is -24.6% on M1 and -17.5% on the horde vs base (ipa-exact: -20.6% /
-14.9%).

## Problems found

1. The may-write reload bug (fixed; found by the audit, see above).
2. Opaque boundaries dominate precision: in exact mode LIVEOUT is "everything" for every
   function, and 9.6 k functions read "everything". Imports (operator new/delete, critical
   sections) and indirect calls are the poison; vtable slot sets do not fix it (step 2).
3. The 138 fingerprinted bodies (e.g. 821B0258, 7.8% of M1) must stay byte-identical, so
   they keep the ctx form. Re-auditing their `extract-native-*` fingerprints would let them
   join.
4. Host register pressure: a body with 30+ live guest registers spills to the host stack.
5. MXCSR sticky flags differ between the two forms (clang drops dead FP operations). No
   guest reads them, and the audit compares only the guest-owned MXCSR bits.
6. The RTTI-vtable guard misses ~1e8 times per run: non-RTTI vtables are common.

## Next steps

1. **Exact import summaries** from an audit of the SDK export shims (reads: ArgTranslator
   arguments, r1, r13; writes: r3 / f1 only), with APC-delivering and guest-executing
   exports kept opaque. Verify in audit builds by wrapping import calls and comparing ctx
   before and after against the claimed WRITES. Upper bound: ABI mode's extra -5..-8%.
2. **Verified ABI class for indirect targets** (per-function class byte + guard + logged
   misses): the only realistic way to make virtual calls precise here.
3. **Fast ABI returns** (r3 in rax / f1 in xmm0, no reload) and must-write summaries.
4. **Stack accesses without volatile and without the phys-offset check** (r1-relative
   D-form loads/stores; phase 1's STACK_FAST, exact because stacks are thread-private and
   below 0xE0000000), then constant bases (`lis 0x82xx`) - backlog item 3.
5. **Single-precision tracking** (keep lfs/fadds/fmuls/... results as float where double
   rounding is provably identical: + - * / sqrt, not fmadds), and **inlining** of small
   non-hooked callees.

r13 is the KPCR address the runtime sets per thread (`thread_state.cpp:36`) and exports
read it back (`kernel_state.cpp:1250`). The only guest write is `ld r13,152(r7)` in
sub_82520FC0 (a context-restore routine, which writes everything), so r13 stays a
read-only local, never an argument.

### Backlog (from the main session; each behind its own option, each audited)

1. fold read-only-data constants (`lis/lfs` from a read-only XEX section, after proving
   the section is never written);
2. vtable shadow tables (host `PPCFunc*` arrays per guest vtable, guarded by an rdata check
   of the object's vtable pointer);
3. pointer-provenance to drop `REX_PHYS_HOST_OFFSET` (r1-derived, image constants and their
   derivatives only; dropping it everywhere crashes);
4. flush-mode state per basic block (switch only at scalar/VMX transitions; removing it is
   wrong, +252%);
5. audit and improve the VMX128 translations (vmsum3/4fp, vpermwi, vrlimi, vpkd3d/vupkd3d);
6. dcbt/dcbtst to `_mm_prefetch`, check dcbz/dcbz128;
7. 32-bit narrowing where upper halves are provably unused;
8. stack-slot promotion for non-escaping r1+const slots (dead stack bytes then leave the
   audit);
9. idiom folding where clang does not already fold.

Priority from the main session: 1, 3, 2, 4, then 5 and 6.

## Tools

- `tools/codegen-fork/smoke.ps1 -Exe out/exp/<dir> -Scenario benchmark-skipintro|horde-ants-1000 -Seconds N -Tag t`:
  waits for a free machine, runs muted and uncapped, reports markers and log faults.
- `tools/codegen-fork/survey.py [report.jsonl]`: the survey above.
- `tools/codegen-fork/time-series.ps1 -Variants a,b,c -Rounds 3 -Start N` (variants are `out/exp/<name>`), `run-timed.ps1`, `tim.py out/codegen-fork/runs`: the timing above.
- The audit: `edf_ipa_audit = true` + `-DEDF_CODEGEN_FORK_AUDIT=ON`; the log gets
  `IPA audit: audited=... mismatches=...` every 10 s and `IPA audit MISMATCH sub_X ...`
  with the differing registers and bytes (first 3 per function).
