# Native rendering thread investigation — 2026-09-10

## Evidence and decision

The user reports that mission 1 reaches a stable 60 FPS after the introductory
radio sequence ends. This is a useful phase boundary, not proof of audio-thread
contention or GPU saturation.

Current indexed and immediate draw hooks execute D3D11 work synchronously on
their calling game thread. Both acquire the recursive submission gate and then
the bridge's immediate-context mutex. The UI thread performs presentation via
`NativeHostSurface::Paint`; `VisitNativePresentationFrame` keeps that same mutex
held through the consumer callback, including `Present`. Therefore a blocking
Present can prevent game draw submission. Its actual contribution remains
unmeasured in the affected visible scenario.

The inspected SDK source defaults `ignore_thread_affinities` and
`ignore_thread_priorities` to true. `XThread::SetActiveCpu` only pins a host thread
when affinity ignoring is disabled and at least six logical processors exist.
These source defaults are not a measurement of a user's live thread affinity.

Do not introduce an asynchronous renderer until measurements justify it and
queued commands have stable native state/resource lifetimes. Merely moving a
draw to another thread and waiting immediately does not create useful overlap.
An eventual renderer should own both immediate-context submission and Present,
consume a bounded native command queue, and preserve resource-update ordering
and completion semantics without deferred reads of mutable guest memory.

## Added diagnostics

`edf_native_hook_timings=true` now also reports:

- `indexed.submission_wait` and `immediate.submission_wait`: time to acquire the
  game-command ordering gate.
- `indexed.context_wait` and `immediate.context_wait`: time to acquire the shared
  immediate-context mutex after the submission gate is acquired.
- `presentation.context_wait`: UI-side context mutex acquisition, excluding
  context-state isolation, compositing and Present.

The existing timing accumulator uses per-thread buckets and reports count,
total wall time and maximum. Lock acquisition includes uncontended overhead and
OS scheduling delays; it does not identify the lock holder. Phases have separate
report windows and cannot be added as exclusive per-frame costs. Instrumentation
and logging themselves perturb timing, so compare with an uninstrumented run.
Existing GPU spans include submission gaps, not exclusively GPU busy time.

The validation launcher accepts `-HookTimings`; it remains off by default.
No lock ordering, synchronization, native ownership or rendering behavior changed.

## Build verification

- Executable: `out/build/win-native-clean/edf2027-native-lock-timings.exe`
- SHA256: `827C92A1CE02F7050AED664B12F204950D1630DAFFDE3B390C2FEF8D4CB4903A`
- Build succeeded; all 18 CTest tests passed in 12.09 seconds.
- Runtime comparison of introduction versus normal gameplay is still pending.
  Tests do not establish a performance improvement or diagnose the slowdown.

The previous source-check-volume diagnostic process (PID 3848) was stopped after
checking its exact executable path. Its last log sample at 15:16:22.142 reported
43,499,234 checks each for vertices and indices, with 866,713,266,596 vertex
candidate bytes and 51,292,439,052 index candidate bytes. These are comparison
lengths, not measured memory traffic or elapsed comparison costs.

## Hidden runtime follow-up

Run `out/native-bridge-run/binding-validation-20260910-151904-0e54994e`
used the executable above, fresh defaults, hook timings enabled and the
movement/fire input script. PID 37004 was identity-checked and stopped after
capturing the mothership introduction and gameplay with 18/120 ammunition.

The host logs explicitly report DXGI occlusion. This run therefore cannot test
normal visible VSync blocking, nor establish the user's FPS or precisely mark
the end of radio chatter. The following windows are selected by report timestamp;
their independent phase buckets include boundary spillover and omit trailing
unreported calls. Values are average microseconds per reported call, not frame
times, measured on game draw thread 15504:

| Phase | Intro, 15:20:20–15:20:50 | Gameplay, 15:21:40–15:22:05 |
| --- | ---: | ---: |
| Indexed command-gate acquisition | 0.0192 | 0.0195 |
| Indexed context-lock acquisition | 0.0204 | 0.0214 |
| Indexed mesh preparation, inclusive | 3.047 | 1.646 |
| Indexed native hook, inclusive | 4.097 | 2.607 |

Context-lock maximums in those windows were 0.5014 and 0.5804 ms. The data does
not show large sustained draw-lock contention under occluded conditions. Mesh
preparation is a stronger CPU investigation candidate here, but these inclusive
times do not isolate source comparisons from other mesh work. Do not infer a
visible-session bottleneck or a render-thread speedup from this experiment.

`tools/summarize-native-hook-timings.ps1` now accepts multiple rotated log paths
and optional `-ByThread` grouping. Its combined versus per-thread call totals
were checked against this run and matched. Normal output stays phase-aggregated.
Visible intro/gameplay profiling remains pending; no renderer-thread rewrite
or synchronization behavior change was made.
