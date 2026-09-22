# Renderer status

Last updated 2026-09-22, at commit `8b1dc21` on `native-scene-renderer`.

This is the one current statement of the native renderer effort. Where another
`docs/renderer-*.md` or `docs/native-scene-renderer*.md` file disagrees with it,
this file wins. Those files are kept as history (see the last section).

## 1. Goal

"Native renderer" means one concrete thing: during a frame, the guest render
helper `sub_821A5080` and the callbacks it drives do not run, and the frame is
drawn from the scene publication (the data the game publishes at its
simulation cadence) by native code. Until the helper's callbacks stop running,
the helper's CPU cost is still paid, whatever is replaced underneath it.

The approach used so far was per-function replacement with a fallback to the
guest. Individual callees (leaf culling, static LOD dispatch, the shader cache,
the indexed draw tail, material activation, scalar getters and setters) got
native versions behind flags, each able to fall back to the original guest code
when an input was not supported. This did not remove helper cost, for three
reasons:

- The helper still ran every frame. The native pieces were called from inside
  the guest traversal, so the guest's hierarchy walk, pass membership, callback
  dispatch and CPU setup mirrors stayed on the frame.
- Every native stage added work beside the guest work it was meant to replace:
  eligibility checks, capture, publication, interning and locking. When the
  stage fell back, the frame paid for both.
- In practice the fallback was the normal case. The eligibility check rejects
  every real material (section 3), so no static group ran fully native. The
  flags added cost and removed none.

Removing helper cost therefore needs whole passes to be skipped at the helper
level and drawn from the publication, not more leaf replacements.

## 2. Measured state (measured 2026-09-22)

All numbers in this section were measured on 2026-09-22.

- The gate tool `tools/renderer-runtime-gate.py` measures the window from
  mission entry +10 s to +150 s, with the flicker script
  (`tools/native-flicker-input.txt`). Mission entry is the first scene draw
  after the loading screen. That window mixes phases: after entry comes a
  pre-mission scene, then the mission load (`MISSION\M202\MISSION.CAM`) and a
  second loading screen (~70 s at 57 FPS), then gameplay, at different times
  in each run. The gate's `--phase all` (the default when both logs show that
  loading screen) gates intro, loading and gameplay FPS separately and reports
  them under `phases_fps`; `--phase entry` is the numbers below.
- Baseline exe `6e9c94b`: median 56.2 FPS.
- Commit `8d9edd1` with default flags: 56.55 FPS.
- Milestone run with all native scene flags on: median 6.3 FPS, and 0 of all
  nonempty static groups fully native.
- The docs' history says every opt-in native stage so far measured slower than
  the ~80 FPS default path when frames are unlocked.

So the default path is unchanged in speed, and turning the native path on makes
the game roughly nine times slower while drawing nothing natively end to end.

## 3. Known causes (found 2026-09-22)

| Cause | State |
|---|---|
| PopulateGroup never skipped unchanged groups | Fixed in `8b1dc21`, not yet measured |
| Per-tick full rescans | Open, in review |
| Static-group eligibility rejects every real material | Open, in review |
| Loading slowdown | Open, in review; not a renderer cause |

**PopulateGroup (fixed in `8b1dc21`, not yet measured).** PopulateGroup never
skipped unchanged groups, because it compared material identity. Callers
resolve a fresh material capture each frame, so the identity never matched and
every group was re-populated every frame. Materials are now interned before the
comparison. The fix has not been built or run; its effect on FPS is unknown
until it passes the gate.

**Per-tick full rescans (open, in review).** Every tick does work proportional
to the whole scene, not to what changed:

- all 412 groups go through geometry/material preload;
- a full tree capture runs for every owner;
- `NativeSceneAdapter::Publish` copies and sorts about 23k objects;
- Retire/Prune runs in O(groups) on the loader thread.

**Eligibility rejects every real material (open, in review).** In
`src/native_graphics/native_static_group_eligibility.h`, the PassState /
opaque-blend check at line 35 fired at least 262,144 times, and the
texture/state list check (lines 36-37) fired at least 16,384 times. No real
material got past both. Native texture/state code exists but is gated off, so
these groups always take the compatibility path.

**Loading slowdown (open, in review).** The slow loading is not caused by the
renderer. The movie-pacing change of 2026-09-18 keeps 60 Hz pacing until the
first 3D frame, which paces the loading screen as well.

## 4. Corrections to older docs

These replace statements in `native-scene-renderer.md` and
`native-scene-renderer-handoff.md`. The affected lines carry a
"Corrected 2026-09-22" note pointing here.

- `820B4250` is a per-simulation-step update of `clMapObjectManager`. It is not
  part of the render helper, and it is not "RenderWorld". Its +364 and +356
  writes are the `g_SignalBrightness` step counter and the `m_WaterTime` clock,
  used only as shader constants. Skipping the helper does not drop them.
- `world+372` holds map objects that are not in the octree.
- The "overlay+48" list is a game-object manager list, not an overlay leaf
  list.
- The static world is drawn by the final call of `820B4310`,
  `821C3BB8(owner+240)`. That call is the boundary for a native static world
  pass.

## 5. Plan

**First: a native static opaque world pass.**

1. Skip `821C3BB8(owner+240)`, the final call of `820B4310`, when the pass is
   enabled.
2. Build the draw list from the scene publication, in the published group
   order. Do not render the whole database snapshot blindly; keep pass
   membership, LOD selection and hidden flags as published.
3. At the end of the pass, hand off device state so that the guest passes that
   still follow see the state they expect.
4. Put the pass behind a new cvar, `edf_native_static_world_pass`, so the same
   build can be run with and without it for A/B comparison.

This pass depends on the section 3 fixes: the per-tick rescans must become
incremental, and eligibility must accept real materials (with the native
texture/state code enabled), or the pass has nothing to draw.

**Then: the remaining passes, one at a time** (animated objects, effects,
shadows, UI and presentation), each skipped at the helper level and drawn from
the publication in the same way. The helper can be dropped only when every one
of its passes is covered.

**Acceptance rule.** Every change must pass `tools/renderer-runtime-gate.py`
against the baseline (FPS no worse) before it counts:

```powershell
python tools/renderer-runtime-gate.py <candidate game.log> --baseline <baseline game.log>
```

Both logs must come from the same flicker script. For native-pass work, also
pass `--min-native-groups` so that a run which silently falls back does not
count. The tool's default `--tolerance` is 0.05; treat any drop as a failure
unless it is explained. Offline suites (`validate-renderer-offline.cmd`,
the differential fixtures and scalar pipelines) are necessary but not
sufficient: they prove narrow contracts, not that the game got faster.

## 6. Older docs

Nothing has been deleted. The files below are historical as of 2026-09-22.
Planning and inventory docs carry a one-line banner saying so.

Planning, inventory and progress reports (bannered, historical):

- `renderer-completion-inventory.md`, `renderer-coverage-audit.md`,
  `renderer-coverage-runtime.md`, `renderer-critical-path.md`,
  `renderer-remaining-work.md`, `renderer-replacement-plan.md`
- `renderer-first-replacement-task.md`,
  `renderer-static-group-completion-progress.md`,
  `renderer-static-group-handoff-result.md`
- `renderer-dispatch.md`, `renderer-task-guide.md`,
  `renderer-task-workflow.md`
- `renderer-re-automation-research.md`, `renderer-scalar-automation-pilot.md`,
  `renderer-setter-automation-pilot.md`, `renderer-setter-family-backlog.md`,
  `renderer-setter-family-expansion.md`
- JSON companions of these (`renderer-coverage.json`,
  `renderer-coverage-scenario-results.json`,
  `renderer-dispatch-first-wave.json`, `renderer-getter-acceptance.json`,
  `renderer-setter-acceptance.json`, `renderer-scalar-policy.json`,
  `renderer-tally-reconciliation.json`) are historical data; no banner.

Implementation notes for single changes (accurate for that change, not a
status; no banner): `renderer-constant-performance.md`,
`renderer-native-indexed-completion.md`, `renderer-native-shader-cache.md`.

Tool references (still describe how to run existing tools; no banner):
`renderer-atlas.md`, `renderer-offline-validation.md`,
`renderer-scalar-pipeline.md`, `renderer-tally.md`.

`native-scene-renderer.md` and `native-scene-renderer-handoff.md` remain the
detailed engineering record (data layouts, audits, evidence paths). They are
not status; the lines contradicted by section 4 are marked in place.
