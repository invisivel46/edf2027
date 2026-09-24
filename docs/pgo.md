# Profile-guided optimization

The release build (`win-amd64-release`) is compiled with clang IR PGO against a
profile committed to the repo, `pgo/edf2027.profdata`. This page covers what the
profile is, how it is trained, when to retrain it, and what it costs and gains.

## How the build uses it

- `CMakePresets.json`: `win-amd64-release` sets `EDF_PGO=use` and
  `EDF_PGO_PROFILE=${sourceDir}/pgo/edf2027.profdata`. `build.cmd`,
  `package_release` and every measurement in `out/build/win-amd64-release` get PGO
  with no extra step.
- `win-amd64-release-nopgo` is the same build without the profile, for A/B
  measurements. `win-amd64-pgo-train` is the instrumented build
  (`-fprofile-generate`) that `tools/pgo-train.ps1` builds and runs.
- `cmake/edf_optimization.cmake` does the work:
  - Configure copies the profile into the build tree as
    `pgo/use-<md5>.profdata` and compiles against the copy. Ninja tracks compile
    lines, not file contents, so a retrained profile changes the line and every
    object is rebuilt against it. The profile is also a configure dependency, so
    replacing it re-runs configure on the next build.
  - If the profile is missing, configure prints a warning and builds without PGO.
    The game is still correct, only about 10% slower. `-DEDF_PGO=` silences it.
  - Both the instrumented and the optimized build pass
    `-mllvm -static-func-full-module-prefix=false`. The profile then names
    internal-linkage functions (`static`, anonymous namespaces, lambdas) by
    source file name, not by full path. Otherwise a profile trained in one
    checkout or worktree would not match those functions in another.
  - Stale-profile warnings are off (`-Wno-profile-instr-out-of-date`,
    `-unprofiled`, `-missing`). `-DEDF_PGO_STALE_WARNINGS=ON` turns the
    out-of-date one back on. Each TU then reports how many of its functions no
    longer match the profile, which is a quick staleness count.
- PGO changes only inlining, block layout, register allocation and similar
  decisions. It doesn't change what the code computes. The v3 profile's
  `-ffp-contract=off` still applies, so no float operation is fused or reordered.
  The A/B image gate below checks this in game.

## Why it stays valid

The guest code (`generated/`, 81 TUs, the bulk of the hot code) is generated
from the XEX and practically never changes. Its profile stays valid for as long
as the recompiler's output stays the same. A host change loses the profile only
for the functions it changes: their CFG hash no longer matches, and clang
compiles them as it would without PGO. Everything else keeps its profile. So a
profile that is a few host commits old costs only the difference on the changed
functions.

## When to retrain

- **Before each release.** Run `tools/pgo-train.ps1`, rebuild, run the A/B image
  gate, and commit the new profile together with `pgo/training.json`.
- **After a codegen change** (a new ReXGlue version, new overrides, a different
  partition): the whole guest profile goes stale.
- **After a large host change on a hot path** (the full-frame renderer, the
  render registry, the hooks), if it has to be measured at full speed before the
  next release.
- **After a compiler upgrade.** A profile from another LLVM version may not load
  and is at least less accurate. Clang reports a profile it can't read as an
  error.

## Training: `tools/pgo-train.ps1`

```
powershell -ExecutionPolicy Bypass -File tools/pgo-train.ps1
```

1. Configures and builds `win-amd64-pgo-train` (`out/build/win-amd64-pgo-train`)
   at idle priority with `BUILD_JOBS` jobs (default 2), and records its wall
   time and its compilers' peak memory.
2. Runs the training scenarios from `tools/renderer-scenarios.json`, one after
   another, through `tools/run-renderer-scenario.ps1`. Each run gets its seeded
   save (`tools/make-edf-save.py`) and its input script, and uses the native
   renderer unlocked and uncapped (`--edf_fps_cap=0`, VSync off, frame times on):
   - `benchmark`: Mission 1 (M202) from a fresh profile, with the intro, the
     loading screen and the street fight;
   - `ufo-swarm`: Mission 6 (M212), the gunship swarm;
   - `cave`: Mission 11 (M301), 110 ants underground;
   - `vehicle`: Mission 9 (M211), the tank, building collapses and tank fire.

   The title screen, menus and loading screens are part of every run. It waits
   for any other game to exit before each launch. Each run writes its own raw
   profile to `out/pgo-train/<scenario>.profraw`. The instrumented game rewrites
   it every 10 s (`EDF_PGO_DUMP_SECONDS`) through `src/pgo_profile_writer.cpp`,
   writing to a temporary file first and then renaming it. A run is stopped with
   `TerminateProcess`, which skips the runtime's exit-time write, so it never
   leaves a truncated profile.
3. Converts each raw profile with `llvm-profdata` (the one bundled with VS 18,
   `VC\Tools\Llvm\x64\bin`, LLVM 20, which is the compiler's own). It then merges
   them with `--weighted-input`. The weights are normalized by each scenario's
   total block count, so each scenario contributes its `-Share` of the merged
   counts however long or fast it ran. The default shares are Mission 1 = 2 and
   each combat scenario = 1. Writes `pgo/edf2027.profdata` and
   `pgo/training.json` (commit, date, LLVM version, per-run times, block counts
   and weights).

`-SkipBuild` reuses the instrumented build. `-SkipRuns` re-merges the raw
profiles of an earlier run, for example with other `-Share` values.
`-Scenarios` and `-Seconds` change the training set.

## Results

Measured on 2026-09-24 against the profile trained on `43f3292`, on the same machine (28 threads)
that the other agents were building on and playing. Every run waited until no other game and no
clang/lld was running. The table notes the ones where a build started during the run anyway.
Mission 1 benchmark, native renderer, unlocked, uncapped (`--edf_fps_cap=0`), VSync off. The
gate's gameplay window is gameplay start +10 s to +150 s. p1 is 1000 / p99 frame time from the
frame-time histograms, which have 0.25 ms buckets.

| Pair | Non-PGO median / p1 FPS | PGO median / p1 FPS | Median | First 45 s median | Quietness |
|---|---|---|---|---|---|
| a | 235.7 / 142.9 | 241.6 / 137.9 | +2.5% | 212.5 → 221.4 (+4.2%) | a clang build ran during the PGO run |
| b | 231.6 / 142.9 | 270.8 / 190.5 | +16.9% | 210.2 → 257.9 (+22.7%) | quiet |
| c | (512.5, invalid) | 263.1 / 181.8 | n/a | 189.7 → 254.2 (+34%) | quiet. The non-PGO run left the scripted route 57 s into gameplay (a steady 510+ FPS view); only its first 45 s count. |
| hook timings | 201.3 / 142.9 | 220.2 / 153.8 | +9.4% | 187.9 → 206.6 (+10%) | quiet |

- **PGO was faster in every pair.** The size of the gain varies with the machine's state,
  from +4% to +34% on the first 45 s. The one run with a clang build beside it gave the smallest
  gain. Across all valid runs, the first-45-s medians are 188-213 FPS without PGO and 207-258
  FPS with PGO.
- **Simulation step, from the hook-timing pair.** `engine.simulation_dispatch` over the gameplay
  window, per 60 Hz step, was 2.72 ms without PGO and 1.93 ms with PGO (-29%). Per call, it was
  0.82 → 0.37 ms. The hook-timing code itself is cold in the profile, so this pair understates
  PGO slightly.
- **UFO swarm (M212) is inconclusive.**
  - The first non-PGO run left the route and held 571 FPS; it is invalid.
  - The second pair gave 252.9 without PGO and 255.3 with PGO (+1%). A clang-cl build overlapped
    the non-PGO run.
  - The two PGO runs themselves differ: 204.5 vs 255.3. The scenario's run-to-run variance is
    larger than the effect.
- **The scripted routes are not deterministic.** The route was left in 1 of 8 benchmark runs, for
  good (non-PGO c), and for 10 s in the PGO hook-timing run. It was also left in 1 of 4 UFO runs.
  Both builds did this. Check the FPS timeline before trusting a median.

**Output is unchanged.**

- A/B image gate, locked, Mission 1 (`run-renderer-scenario.ps1 -Variant ab`, then
  `compare-renderer-ab-captures.py`): **passed** for both the non-PGO and the PGO build, with no
  failing frames and no persistent tiles. The same single edge frame (4063) was not judged in
  either.
- Same output frames (4000-4063) compared across runs with `compare-renderer-images.py`:

  | Comparison | Pixels differing (mean) | PSNR (mean) |
  |---|---|---|
  | non-PGO vs non-PGO | 5.33% | 30.9 dB |
  | non-PGO vs PGO (run 1) | 5.15% | 30.2 dB |
  | non-PGO vs PGO (run 2) | 5.81% | 30.1 dB |

  PGO against non-PGO differs as much as two non-PGO runs differ from each other. The
  difference is on animated things (the player, civilians, the mothership, sparks), not on
  static geometry or shading. That is run-to-run timing, not code generation. PGO doesn't touch
  float semantics, and `-ffp-contract=off` stays.
- ctest on the PGO tree: 82/82 passed.

**Cost.**

| | Wall time (2 jobs, idle priority) | Peak compiler working set |
|---|---|---|
| Instrumented build (`win-amd64-pgo-train`) | 5.4 min (ninja log) | not captured |
| Non-PGO release build | 4.4 min | 1.8 GB total, 1.0 GB in one clang |
| PGO release build | 5.0 min | 1.8 GB total, 1.1 GB in one clang |
| Training runs (4 scenarios) | 42 min of runs, 47 min end to end | the game, about 1.7 GB |

PGO adds about 0.1 GB per clang process and about 15% to the build time. At 2 jobs it stays far
below the memory where clang has run out at high parallelism. The PGO exe is 37.2 MB, 0.9 MB
bigger than the non-PGO one: hot paths get more inlining.

The committed profile is 9.7 MB. It covers 27,323 functions, including all the hot guest code.
`pgo/training.json` records its training.
