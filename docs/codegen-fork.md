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

## Problems found

1. The may-write reload bug (fixed; found by the audit, see above).
2. Opaque boundaries dominate precision: in exact mode LIVEOUT is "everything" for every
   function, and 9.6 k functions read "everything". Without trusting something at imports
   and indirect calls, interprocedural CR/XER liveness only helps inside bodies and at
   calls into analyzable subtrees.
3. The 138 fingerprinted bodies (e.g. 821B0258, 7.8% of M1) must stay byte-identical;
   they keep the ctx form. Re-auditing their `extract-native-*` fingerprints would let them
   join.
4. Host register pressure: a body with 30+ live guest registers spills to the host stack.
   Spills are cheaper than ctx traffic only when they are fewer.
5. MXCSR sticky flags differ between the two forms (clang drops dead FP operations); no
   guest reads them, and the audit compares only the guest-owned MXCSR bits.
6. Timing was not measured in this phase (on request).

## Next steps

1. **Time it** (when timing is allowed again): `ipa-exact` and `ipa-abi` against the
   nopgo base with `--edf_deterministic_steps --edf_step_timing`, M1 and horde, 3 runs
   (phase-1 `run-timed.ps1`). Built binaries: `out/exp/{base,ipa-exact,ipa-abi}`.
2. **Precision without giving up exactness**, most valuable first:
   - vtable slot target sets for `lwz r11,0(r3); lwz r11,N(r11); mtctr; bctrl` call sites
     (union of the summaries of every function in slot N of every vtable; falls back to
     opaque when a slot holds a hook or an unknown); this is also backlog item 2;
   - per-import summaries from the SDK's export shims (argument count from the
     `ArgTranslator` signature; exports that can run guest code, like alertable waits
     delivering APCs, stay opaque);
   - must-write summaries (a callee that writes r3 on every path makes r3's old value dead
     at the call).
3. **Fast register-passing ABI** on top of the summaries: `sub_X_fast(ctx, base, <P>)`
   with P = READS(X) & {r1, r3-r10, f1-f13}, the caller passing its locals instead of
   writing them back, the callee treating P as dirty on entry, and returning r3 in rax or
   f1 in xmm0; `sub_X` becomes a ctx wrapper (indirect calls, hooks, the runtime and
   native code keep working). Evidence for the host convention (clang, x86_64-windows):
   `__attribute__((regcall))` passes ctx/base/r1/r3-r7 in rcx, rdx, rsi, rdi, r8-r11 and
   doubles in xmm0-xmm2 (the MS x64 default has only four argument registers); a
   `{u64, double}` pair is returned through memory, so return only one register and write
   the rare second one back. Keep `base` an argument (a global costs a load per function).
   r13 is the KPCR address the runtime sets per thread (`thread_state.cpp:36`) and that
   exports read back (`kernel_state.cpp:1250`). The only guest write is `ld r13,152(r7)`
   in sub_82520FC0 (a context-restore routine, which writes everything), so r13 is a
   read-only local loaded once elsewhere, never an argument.
4. **Stack accesses without volatile and without the phys-offset check** (r1-relative
   D-form loads/stores; phase 1's STACK_FAST, exact because stacks are thread-private and
   below 0xE0000000), then constant bases (`lis 0x82xx`) - backlog item 3.
5. **Single-precision tracking** (keep lfs/fadds/fmuls/... results as float where double
   rounding is provably identical: + - * / sqrt, not fmadds), and **inlining** of small
   non-hooked callees (drop weak/noinline on the `__imp__` bodies the analysis owns).

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
- The audit: `edf_ipa_audit = true` + `-DEDF_CODEGEN_FORK_AUDIT=ON`; the log gets
  `IPA audit: audited=... mismatches=...` every 10 s and `IPA audit MISMATCH sub_X ...`
  with the differing registers and bytes (first 3 per function).
