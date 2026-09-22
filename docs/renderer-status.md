# Renderer status

Last updated 2026-09-22 (evening), at commit `b908b96` on
`native-scene-renderer`.

This is the one current statement of the native renderer effort. Where another
`docs/renderer-*.md` or `docs/native-scene-renderer*.md` file disagrees with it,
this file wins. Those files are kept as history (see the last section).

## How to enable

One switch, `--edf_native_renderer=off|world|full` (default `off`), replaces
setting the individual cvars in section 5:

- `world`: `edf_native_host`, `shader_bridge`, `seam_draws`,
  `material_activation`, `scene_queued`, `scene_preload`, the six
  `scene_*_owned` flags the static world pass requires (sources, membership,
  selection, camera, geometry, material), `scene_tree`,
  `scene_tree_published`, `scene_visibility`, `frame_dispatch`,
  `scene_group_order` and `static_world_pass`.
- `full`: `world` plus `model_publication`, `model_pass` (the rigid model
  pass) and `post_finish`.

Not in any preset: every audit, `bucket_dispatch` and `map_effect_list` (their
in-game audit and census have not been run), and the optional
`scene_activation_owned`, `scene_instance_owned`, `scene_pass_owned`,
`scene_view_owned` and the deferred flags. Turn those on individually.

Rules (`src/native_graphics/native_renderer_preset.h`): a flag is on when its
own cvar is on or the preset includes it. An individual cvar can only add a
flag, never remove one the preset turns on; to drop one, use the smaller preset
and add the rest by hand. The preset is read once at startup (restart to change
it; an unknown name stops startup), and one line
`Native renderer: preset=... on=[...] off=[...]` logs the effective flags.

## 1. Goal

"Native renderer" means one concrete thing: during a frame, the guest render
helper `sub_821A5080` and the callbacks it drives do not run, and the frame is
drawn from the scene publication (the data the game publishes at its
simulation cadence) by native code. Until the helper's callbacks stop running,
the helper's CPU cost is still paid, whatever is replaced underneath it.

Earlier work replaced single callees inside the guest traversal, each with a
fallback to the guest. That removed no helper cost: the helper still ran, each
native stage added eligibility, capture and locking work beside the guest work,
and in practice the fallback was the normal case. Removing helper cost needs
whole passes to be skipped at the helper level and drawn from the publication.

## 2. Measurements

The metric is the median FPS reported by `tools/renderer-runtime-gate.py`,
measured from mission entry +10 s to +150 s with the flicker script
(`tools/native-flicker-input.txt`). Mission entry is the first scene draw
after the loading screen. Baseline exe `6e9c94b` = 56.2.

| Build | Flags | Median FPS | Min FPS | Notes |
|---|---|---|---|---|
| 8d9edd1 | default | 56.55 | — | |
| 8d9edd1 | all scene flags, audit off | 6.7 | — | |
| 8d9edd1 | all scene flags, audit on | 6.8 | — | |
| 06e9ae7 | all scene flags | 11.8 | — | |
| d7f0892 | all scene flags | 13.4 | 6.1 | first 3 fully native static groups |

Separate note, not a result: in the window entry +115..190 s, the baseline
measured 27.1, `8d9edd1` default 22.9, and `d7f0892` 57.2. The two runs appear
to be in different mission phases, and image verification is pending. This is
not a speedup and must not be quoted as one.

Reading the table:

- The default path is unchanged in speed. The all-flags path is still about
  four times slower than the baseline.
- `06e9ae7` includes the PopulateGroup skip (`8b1dc21`), change-only tree
  recapture (`0359810`) and owner-bounded retirement (`92f3e8d`).
- `d7f0892` adds the change-driven preload (`665e3e1`), eligibility for
  texture/state lists and blended materials (`cbc5d33`), explicit instance
  resolution (`fbf95c9`), proportional publication (`74bd08e`), the draw-time
  geometry comparison (`70a998e`) and the once-per-walk source generation
  (`d7f0892`).
- Nothing after `d7f0892` has been measured, including the native static world
  pass (`71fc0a2`). `7927bf4` says in its message that it was not yet built.

## 3. Rules

- A change counts only when it passes the runtime gate against the baseline
  (median FPS no worse, same flicker script), plus an image A/B check wherever
  it draws. For native-pass work, also pass `--min-native-groups` so that a run
  which silently falls back does not count, and `--expect-drop` on the phase
  the change is meant to shrink.
- Offline suites (`validate-renderer-offline.cmd`, the differential fixtures,
  the scalar pipelines) are necessary but not sufficient: they prove narrow
  contracts, not that the game got faster or draws the same image.
- The gate's default `--tolerance` is 0.05; treat any drop as a failure unless
  it is explained.

## 4. Plan and status per pass

Times are ms per frame from the 2026-09-18 hook timings. The hook phase is the
`--expect-drop`/`--max-phase` name in the gate. Hook buckets are inclusive and
overlap, so the times must not be summed.

| Pass | ms/frame | Hook phase | State | Next step |
|---|---|---|---|---|
| Static world (`821C3BB8(owner+240)`) | ~7.0 | `render.queued` | native opt-in | Build `b908b96`, gate with all scene flags plus `edf_native_static_world_pass` and `--expect-drop render.queued`, A/B images with `edf_native_ab_alternate` |
| Octree walk (`821C61D8`, `820B4038`) | ~1.8 | `render.children`, `render.gather` | native opt-in | Measure `43e1c3e` (one bridge lock per walk) against `d7f0892` |
| Models (`821C9C20`) | ~2.65 | `render.model` | data published | Check `edf_native_model_publication_audit` in game, then draw models from the published layouts and poses |
| Finish/post (`820B0B80`) | ~1.13 | `render.finish` | not started | Break down what the finish phase draws before choosing a boundary |
| Map effects (`820B35A0`) | ~0.73 | `render.list` | native opt-in | Run the census; the list walk is native but each object still goes through the guest `sub_821C0C00` |
| Buckets (`821A3BA0`, `821C0C00` insert) | <0.1 | `render.buckets` | native opt-in | Run `edf_native_bucket_dispatch_audit` in game; low priority by cost |

"Native opt-in" means code exists behind a default-off cvar. No pass has
reached "measured" in the sense of section 3: the only measured native runs are
the all-flags rows in section 2, which predate the world pass.

What each state rests on:

- **Static world.** `06e9ae7` publishes each owner's group walk order at the
  simulation step. `71fc0a2` replaces `821C3BB8(owner+240)` at the end of
  `820B4310` and draws groups in that published order, resolving every
  instance from publication and explicit pass inputs. Groups it cannot draw
  run the guest group callback (see section 7). Eligibility was widened in
  `cbc5d33` and tightened in `c468f10` (image constants and anisotropy entry
  in the alias preflight; shader defaults on any traced pass mirror rejected).
- **Octree walk.** Native tree traversal and visibility existed before this
  branch. `d7f0892` resolves the source generation once per walk; `43e1c3e`
  holds the bridge lock once per walk between guest calls and reads the view
  once until a guest call.
- **Models.** `b908b96` is data only: layouts are captured at first sight per
  (instance, generation) and retired when `820B2510` frees the instance, its
  model node or its pose storage; pose snapshots are published per tick in a
  copy-on-write map beside the scene. Draws are unchanged.
- **Map effects.** `79f1e50` adds the census (by vtable, mode and slot-4
  method, over the `clMapEffectManager` list at manager+48) and the native list
  walk, which keeps the guest's read points.
- **Buckets.** `037acbf` ports the mode-1/2 depth key of `sub_821C0C00` and the
  `sub_821A3B80` bucket head insert.

After the static world pass, the remaining passes are taken one at a time in
order of cost, each skipped at the helper level and drawn from the
publication. The helper can be dropped only when every pass is covered.

## 5. Cvars added on this branch

All are in the `EDF2027` category and were added between `6e9c94b` and
`b908b96`. All booleans default to `false`. "Requires" lists what must also be
on for the cvar to have an effect.

**Static world pass**

| Cvar | Default | Purpose | Requires |
|---|---|---|---|
| `edf_native_static_world_pass` | false | Draw the static opaque world pass natively in published group order; unsupported groups run their guest group callback | `edf_native_frame_dispatch`, `edf_native_scene_tree`, `edf_native_scene_tree_published`, `edf_native_scene_queued`, `edf_native_scene_visibility`, `edf_native_scene_sources_owned`, `edf_native_scene_membership_owned`, `edf_native_scene_selection_owned`, `edf_native_scene_camera_owned`, `edf_native_scene_geometry_owned`, `edf_native_scene_material_owned`, `edf_native_scene_preload`, `edf_native_scene_group_order` (logs the first missing one) |
| `edf_native_frame_dispatch` | false | Own the outer render phase dispatch in native code; remaining phase callbacks are kept | `edf_native_shader_bridge` |
| `edf_native_scene_material_owned` | false | Build queued rigid scene materials from published programs and native pass inputs | native scene path |
| `edf_native_scene_activation_owned` | false | Activate queued scene shader bindings from published material inputs | `edf_native_scene_material_owned`, audit off |
| `edf_native_scene_geometry_owned` | false | Use published geometry for the first native group draw, keeping the indexed CPU tail | `edf_native_scene_material_owned`, a publication, audit off |
| `edf_native_scene_instance_owned` | false | Use lifecycle-owned world-only instance register metadata | a published source with a world |
| `edf_native_scene_pass_owned` | false | Carry explicit render and sampler pass state between native groups | native scene path |
| `edf_native_scene_view_owned` | false | Carry viewport, scissor and target selection across native material groups | native scene path |
| `edf_native_scene_camera_owned` | false | Consume immutable producer camera matrices at native render entry | native scene path |
| `edf_native_scene_geometry_deferred` | false | Submit eligible native groups before installing compatibility geometry bindings | `edf_native_scene_activation_owned`, `edf_native_scene_material_owned`, `edf_native_scene_material_audit` off |
| `edf_native_scene_material_deferred` | false | Submit eligible native groups before CPU material activation and restore state at handoff | a pending geometry handoff (so `edf_native_scene_geometry_deferred`) |
| `edf_native_material_activation` | false | Run material activation from a native operation list with native sampler resolution | `edf_native_shader_bridge`; the material state/sampler audits off |

**Publication and preload**

| Cvar | Default | Purpose | Requires |
|---|---|---|---|
| `edf_native_scene_sources_owned` | false | Select static sources, LOD, visibility and world from one scene publication | a publication with sources |
| `edf_native_scene_membership_owned` | false | Take spatial lists and hierarchy from the same publication as sources and assets | a publication with membership |
| `edf_native_scene_selection_owned` | false | Resolve published static instances without mutating the current scene database | a publication |
| `edf_native_scene_tree_published` | false | Read the immutable spatial hierarchy published by the world producer | `edf_native_scene_tree` |
| `edf_native_scene_group_order` | false | Publish each world owner's static group walk order (owner+240) at the simulation step | none |

The preload itself (`edf_native_scene_preload`) predates this branch; its
change-driven behaviour (`665e3e1`) has no cvar.

**Eligibility**

| Cvar | Default | Purpose | Requires |
|---|---|---|---|
| `edf_native_scene_reject_compatibility` | false | Diagnostic: reject counted static-group compatibility boundaries before calling them | native queued scene path |

The eligibility rules (`cbc5d33`, `c468f10`) have no cvar of their own; they
apply whenever the native queued scene path runs.

**Walk, buckets and map effects**

| Cvar | Default | Purpose | Requires |
|---|---|---|---|
| `edf_native_scene_tree` | false | Native spatial tree traversal and culling; leaf callbacks stay explicit | `edf_native_shader_bridge` |
| `edf_native_bucket_dispatch` | false | Insert sort-mode 1/2 objects into the guest depth buckets natively instead of `sub_821C0C00` | none |
| `edf_native_map_effect_list` | false | Walk the map-effect list (`sub_820B35A0`) natively; each object still goes through the hooked `sub_821C0C00` | none |

**Models**

| Cvar | Default | Purpose | Requires |
|---|---|---|---|
| `edf_native_model_publication` | false | Capture model draw layouts at first sight and publish per-tick pose snapshots; draws unchanged | none |

**Post**

No cvar was added for the finish/post pass on this branch.

**A/B**

| Cvar | Default | Purpose | Requires |
|---|---|---|---|
| `edf_native_ab_alternate` | 0 (int, 0..1000) | Alternate guest and native passes in runs of N indexed output frames from the capture start frame; odd runs are native, 0 is off. Also holds the static world pass on the guest side of alternate frames (`c8493c8`) | scene capture settings for the paired captures (see `compare-renderer-ab-captures.py`) |

**Audits (diagnostic, all default false)**

| Cvar | Compares |
|---|---|
| `edf_native_material_sampler_audit` | Native material sampler programs with original activation |
| `edf_native_material_state_audit` | Native material render-state programs with original activation |
| `edf_native_scene_material_audit` | Published programs with explicit pass-time constants and visible group setup; turns off the owned activation, geometry and deferred paths |
| `edf_native_scene_group_order_audit` | Published static group order with a live walk at world-pass entry |
| `edf_native_bucket_dispatch_audit` | Native mode-1/2 bucket key and insert with `sub_821C0C00` |
| `edf_native_model_publication_audit` | Published model layouts and poses with live memory at model draw entry |
| `edf_native_map_effect_census` | Tallies map-effect objects by (vtable, mode, slot-4 method) and logs the top classes every 600 frames |

## 6. Tools

- `tools/renderer-runtime-gate.py` compares a candidate `game.log` with a
  baseline log from the same script: median FPS in the window (`--start`,
  `--end`, default 10..150 s after mission entry), `--tolerance`, and
  `--min-native-groups`. When both logs have `Native hook timing` lines
  (`--edf_native_hook_timings=true`) it adds a `phases` report (ms per call and
  ms per `engine.render_helper` frame). `--max-phase PHASE=MS` fails when the
  candidate phase costs more than MS; `--expect-drop PHASE` fails unless the
  candidate phase costs less than the baseline. Both are repeatable.
  `python tools/renderer-runtime-gate.py cand/game.log --baseline base/game.log --min-native-groups 40 --expect-drop render.queued`
- `tools/run-renderer-ab.ps1` runs baseline and candidate exes interleaved on
  the same script, gates each pair and writes
  `out/renderer-ab/<timestamp>-<Name>/summary.json`. Only one game may run at
  a time; `-Seconds` (default 360) must cover loading plus 150 s.
  `powershell -File tools/run-renderer-ab.ps1 -Baseline edf2027-baseline-6e9c94b -Candidate win-amd64-release -Repeat 2 -HookTimings -GateArgs '--expect-drop','render.queued'`
- `tools/compare-renderer-images.py` compares one guest image with one native
  image (BMP/PNG): share of pixels over `--threshold`, max difference, PSNR,
  optional `--mask`; exits nonzero above `--limit`.
  `python tools/compare-renderer-images.py guest.bmp native.bmp --mask diff.png`
- `tools/compare-renderer-ab-captures.py` pairs captures from an
  `edf_native_ab_alternate` run by the `ab_alternate frame=F native=0|1` log
  lines and judges each guest/native pair against a guest-vs-guest baseline
  plus `--margin`. Exits 0 on pass, 1 on a failing pair, 2 on unusable input.
  `python tools/compare-renderer-ab-captures.py out/captures game.log --prefix ab`

## 7. Known gaps

- **Loading time.** The movie-pacing fix (`cdf0c66`, pacing released after
  eight swaps with no new movie draw) has not been measured. Whether loading
  is back to baseline speed is unknown.
- **Static world pass handoff** (from `71fc0a2`):
  - Guest-queued groups, scissor groups, ineligible groups and groups that
    decline are not drawn natively. They run the hooked guest group callback
    `821D96D8` after the owed device state is handed off.
  - After such a guest group, the pass is re-imported from the device mirrors
    rather than carried natively.
  - The handoff replays only the last group's activation and each sampler
    slot's last binder, then writes the combined render words, dirty masks and
    sampler words and publishes the snapshots. Whether this is everything the
    later guest stages read is not yet shown by an image A/B check.
  - The pass has not been built, gated or image-checked.
- **Unmeasured commits.** Everything after `d7f0892` (section 2).
- **Earlier milestone figure.** The 6.3 FPS milestone quoted in `8b1dc21` came
  from a separate run; the section 2 table is the current reference.

## 8. Corrections to older docs

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

## 9. Older docs

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
not status; the lines contradicted by section 8 are marked in place.

## Measurement phases (added 2026-09-22)

The entry+10..150 s window mixes phases. After mission entry comes a
pre-mission scene, then the mission load (`MISSION\M202\MISSION.CAM`) and a
second loading screen (~70 s at 57 FPS), then gameplay, at different times in
each run. `tools/renderer-runtime-gate.py --phase all` (the default when both
logs show that loading screen) gates intro, loading and gameplay FPS
separately under `phases_fps`. By phase:

| Phase | 6e9c94b baseline | 8d9edd1 default flags | d7f0892 all scene flags |
|---|---|---|---|
| intro | 53.6 | 54.7 | 12.4 |
| loading | 57.3 | 57.1 | 57.2 |
| gameplay | 27.1 | 22.9 (single run, needs repeating) | not reached |

The d7f0892 "57 FPS" late window was the loading screen, not gameplay.
