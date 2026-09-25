# Handoff: m1-intro-perf (paused 2026-09-24)

Branch `m1-intro-perf` (worktree `.claude/worktrees/m1-intro-perf`), off `native-scene-renderer` 9bd8522.
A second, throwaway worktree `.claude/worktrees/m1-intro-perf-horde` (detached: horde tip 017149e +
the frame-trace commit cherry-picked as bfd5149) is built but was never run. Remove it with
`git worktree remove` when it is no longer needed.

## Goal

The user reports that on a ROG Ally (Z1 Extreme, 25 W) Mission 1 runs at about 57 FPS but drops to
about 40 FPS during the opening (radio chatter, mothership flyover, soldiers, the first ants).
The FSR mode barely changes either number. The task: find what makes that segment more expensive,
measured on the desktop and projected to the Ally, and fix it only where the fix is exact.

## Done

1. **18203ed: frame-trace columns (committed, verified in 7 runs).**
   `--edf_native_frame_trace=<csv>` now also writes these columns per swap:
   - `steps`
   - `step_dispatch_ms` (821A4BA0, engine thread)
   - `render_helper_ms` (821A5080, render thread)
   - `frame_transition_ms` (821A4DE8)
   - `geometry_draws`
   - `game_tick`

   The new columns are documented in docs/native-backend-migration.md.
2. **Ticker fix (verified in game on branch `presenter-60hz`).** `src/native_graphics/ui_ticker.h`
   gains `NextUiTickerDeadline`: the presenter's 60 Hz ticker now keeps an absolute grid. The old
   rule was `now + period` after each dispatch, which added every wake's latency to the period.
   - Unit test `TestTickerDeadlineGrid` is in tests/input_latency_tests.cpp, and
     `edf_input_latency_tests` passes.
   - Benchmark route, locked, VSync off, 300 s, trace from t=60 s (desktop, 2026-09-24):
     baseline 57.2 FPS, host pacing 17.48 ms, 4.84% of frames ran two steps (1.049 steps/frame),
     `shared_slot_wait_ms` 13.1; with the fix 60.0 FPS, 16.668 ms, 0.03% (1.0004),
     0.01 ms. Unlocked at a 120 cap: 120.0 FPS and 8.335 ms in both builds.
     A/B image gate (VSync off) passed. The VSync-on runs could not be judged: the desktop's
     display was asleep (DWM advanced 1 refresh in 2 s) and both builds ran at 38.6-38.8 FPS
     there.

## Key finding 1: locked mode is capped at 57.2 FPS by the presenter ticker, not by the GPU or CPU

This is almost certainly the user's "~57 FPS".

- In locked mode (`edf_native_unlock_framerate=false`, the default), frame-ready notifications do not
  wake the host (native_backend_host.cpp:38 only calls `ready()` when the unlock is active).
  Presentation is therefore paced by NativeUiTicker's 16.67 ms timer.
- That timer computed `deadline = now + period` after each dispatch, so every wake's latency
  accumulated.
- Measured in run `ecore8-cap60` (locked, VSync off):
  - host pacing `interval_avg_ms=17.47`;
  - FPS 57.1-57.2 in menus, intro and gameplay alike;
  - frame-trace `shared_slot_wait_ms` = 12.7 ms per frame: the engine thread waits in the scene
    image slot `Reserve` for the presenter;
  - 1.05 simulation steps per frame: 47 of 952 frames ran two steps, which is periodic judder.
- The fix should give exactly 60.0 and one step per frame. **Next step: verify** with the runs
  below.

## Key finding 2: the opening is the peak of live, animated, colliding characters, not audio or streaming

Console `stats all` (every 10 s, via `stats.cfg`):
- 44 `clGiantAnt` alive from mission start;
- 10 `clFriendSoldier`;
- up to 23 `clFriendPeople` (civilians) for the first ~50 s;
- the ants die over the next ~80 s, then a second wave of 44 arrives (the fire-hold part of the
  route is expensive again).

Desktop (i7-14700 + 4070 Ti SUPER, 1280x720, native renderer, unlocked, cap 0). Opening =
22:05:35-22:06:06 of run `prof-unmuted`; later = 22:07:30-22:08:00 of the same run.

| per frame / step | opening | later | intro cinematic (pre-mission) |
|---|---|---|---|
| sim step (821A4BA0 per step) | 2.2-2.44 ms | 1.0-1.35 ms | 0.4-0.8 ms |
| render helper per frame | 3.5-4.15 ms | 2.4-3.3 ms | 1.2-2.5 ms |
| frame transition 821A4DE8 | 0.52-0.65 ms | 0.25-0.41 ms | 0.15-0.26 ms |
| draws per frame | 850-866 | 640-720 | 130-650 |
| GPU frame / models pass | 1.33 / 0.62 ms | 0.9-1.1 / 0.14-0.35 ms | 0.8 / 0.1 ms |
| guest audio thread | 4.75% of a core | 2.7% | |
| audio muted vs unmuted | no difference (0.05 ms) | | |

In locked mode on 8 E-cores (`ecore8-cap60`), the loop's work (interval minus waits) was 5.1 ms per
frame in the opening against 4.5-4.6 later. The opening's sim step is 2.2 ms, against 1.3-1.5 later.

What grows on the engine thread (xperf samples, opening vs later, 30 s each):
- skeletal (NativeSkeletalMultiply/Evaluate): +847 samples;
- the sphere-vs-mesh collision family 821AFE20: 821AEE50 went from 75 to 547 samples, plus
  821AF100, 821AF7A0, 821AF328 and 821B0258;
- transition pose 821C9688/821C9478;
- octree 821C38F0/821C58F8.

All of these are already covered by the unmerged **horde branch** (`worktree-agent-ac11824caadfb1183`:
fbb35f7 collision, 259a3c5 broadphase + pose, 592db3a grid) and by the skeletal work (4988fa6, merged
into native-scene-renderer after this branch was cut). So they were not duplicated here.

The render thread's growth is `NativeFullFrameModels::Build` (skinned per-draw material/palette
capture) and `NativeRenderPoseBlender::Decompose`. Decompose already runs only once per pose per
tick. That is the GPU-skinned agent's area (a4c8090).

Other findings:
- **Mission start hitches (one-off, not sustained).** A 61-70 ms step dispatch at tick ~6305, and
  16 ms transitions and a 22 ms helper in the next few frames.
- **Constant cost on every frame.** The rexruntime `TimerQueue::TimerThreadMain` (SDK,
  disruptorplus spin wait) spins about 14% of a core in every phase. It lives in the SDK, not this
  repo.
- **E-core runs were barely slower than P-core runs.** An 8-E-core affinity run
  (`ecore8`, uncapped) was barely slower than all cores. Other agents' builds were running at the
  time (`BUILDS=` in threads.csv), so treat the E-core numbers as noisy.

## Projection to the Ally

- Normal gameplay at 57 FPS is the ticker cap, so the Ally's normal frame cost is at most
  17.5 ms.
- 40 FPS in the opening means about 25 ms per frame. In locked mode, once a frame exceeds a tick,
  frames start running two steps, and the opening's sim step is about 1.7x the later one. So the
  frames most likely become simulation-bound with 1.5 steps per frame on average.
- The fixes that would help are the horde branch's collision/octree/pose natives, skeletal
  batching, and a smaller render-thread cost per skinned draw.
- The ticker fix raises the normal-gameplay cap to 60 and removes the periodic two-step frames. It
  does not by itself fix the 40 FPS.

## Next steps, in order

1. Verify the ticker fix in game. Compare run `ecore8-cap60` (before) with
   `run.ps1 -Name fix-ecore8-cap60 -Exe out/build/win-amd64-release/edf2027-tickerfix.exe -Affinity 0xFF0000
   -Extra '--audio_mute=false','--edf_native_thread_qos=0','--edf_native_unlock_framerate=false'`.
   Expect:
   - FPS 60.0;
   - `steps` = 1 on ~every frame;
   - host pacing `interval_avg_ms` about 16.67.

   Also run locked with VSync on (`--edf_native_vsync=true`) with the fix and with an old exe, and
   check that nothing regresses (movie playback, menus, occluded window). Then amend the WIP into a
   proper commit.
2. Measure the horde build on the same route. Its exe is at
   `.claude/worktrees/m1-intro-perf-horde/out/build/win-amd64-release/edf2027.exe` and it has the
   trace columns. Run `run.ps1 -Name horde-u0 -Exe <that>` and compare sim ms per step in the opening
   against `u0-unmuted`. This quantifies how much of the opening's extra cost the horde branch removes.
3. For the user on the Ally, have them run, from the game directory:
   `edf2027.exe --edf_frametime_log=true --edf_native_frame_trace=C:/temp/ally-trace.csv`
   (plus `--edf_native_gpu_timings=true`).
   - Play Mission 1 through the opening and send `ally-trace.csv` and the log.
   - Check `steps` per frame, `step_dispatch_ms/steps`, `render_helper_ms`, `frame_transition_ms`
     and `shared_slot_wait_ms`.
   - A value of 57.2 with a large `shared_slot_wait_ms` confirms the ticker cap.
   - F1 shows whether they are on the locked default.

## Gotchas

- Other agents race for the single-game slot. `start-native-binding-validation.ps1` throws when a
  game is running, so run.ps1 and runprof.ps1 retry in a loop. Runs can wait 10-30 minutes.
- threads.csv logs `OTHER_GAMES=` and `BUILDS=` marker rows (tid 0). Check them before trusting a
  run.
- Uncapped runs make `Native frame spike` lines meaningless; use the trace CSV instead.
- The runner scripts `run.ps1` and `runprof.ps1` (xperf needs admin; the sessions had it) are
  copies in docs/handoff/m1-intro-perf-tools/. They reference the old session scratchpad path in
  `$s`; edit it before use. The same folder holds the per-run analysis scripts:
  - `analyze.py <run dir> [window s]`
  - `diff.py <dumpA> <dumpB> <tidA> <tidB>`
  - `dumpagg.py`
  - `gpu.py`

  To make a dump: `xperf -i w0.etl -symbols -o w0-dump.txt -a dumper`, with `_NT_SYMBOL_PATH` set
  to the build dir.
- Run data (trace.csv, game.log, ETL dumps) is in the session scratchpad
  `%USERPROFILE%\AppData\Local\Temp\claude\D--roms2-edf2027\c0bb9598-13c3-4be9-9d79-1b99d8f65ac7\scratchpad\runs\`
  (u0-unmuted, u0-muted, prof-partial, prof-unmuted, ecore8, ecore8-cap60). It may be cleared.
  Epistemic evidence: ev-20260924234933-4445988e and ev-20260925011242-3dcd077d.
- `generated/default/codegen.*` are modified by every build. Do not commit them.
