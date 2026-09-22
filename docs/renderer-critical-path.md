> Historical as of 2026-09-22; current status in [renderer-status.md](renderer-status.md).

# Renderer critical-path replacement backlog

Generated from the source-verified atlas and coverage catalog. Counts are planning reach, not missing-function totals or completion percentages.

Plan `693dcff3140e24bcfadc68685e0e9c7c52d1048d97be388436a74e43757290b5`; atlas `bdb977008aa68f95c03b936aa678ed98ab0f00ed4dbc9fe9902a62fce911fc11`.

## Execution order

| Rank | Package | Wave | Boundary roots | Features downstream | Indirect records / flagged functions |
|---|---|---:|---:|---:|---:|
| 1 | P00 Boundary contracts and acceptance harness | 0 | 5 | 32 | 75 / 107 |
| 2 | P01 Resource, state and retirement contracts | 1 | 96 | 32 | 2380 / 1897 |
| 3 | P02 Producer publication, camera and view ownership | 2 | 82 | 26 | 612 / 1013 |
| 4 | P06 Compatibility and diagnostic mode policy | 2 | 43 | 2 | 2041 / 1690 |
| 5 | P03 Complete retained static rendering | 3 | 11 | 16 | 20 / 13 |
| 6 | P05 Composition, loading, presentation and synchronization | 3 | 10 | 11 | 81 / 118 |
| 7 | P04 Animated models and remaining world families | 4 | 5 | 14 | 36 / 64 |
| 8 | P07 Independent cadence and final performance acceptance | 5 | 1 | 5 | 0 / 0 |

Wave is dependency order, not a day estimate. Feature counts overlap and must not be summed.
P00 starts with scoped contract/fixture gates; it does not require narrating all 9,216 functions before implementation.
The exact site, function, boundary-cut, external-target and shared-helper sets live in `out/renderer-replacement/plan.json`.
A days-scale delivery date remains unsubstantiated until the first end-to-end slice passes.

## P00 — Boundary contracts and acceptance harness

Depends on: none.
Tasks: R01.scope, R01.callbacks, R01.modes, R10.trace, R10.capture, R10.input, R10.scenarios.
Features: F12 Single/multiple views, split-screen, free/motion cameras, F20 D3D12/D3D11 hardware and WARP/direct/recorded modes, F32 All maps, weapons and runtime-only declaration combinations.
Owner files: `src/native_graphics/guest_shader_bridge.cpp`, `src/native_graphics/native_backend_host.cpp`, `src/native_graphics/native_capture_policy.h`, `src/native_graphics/native_frame_dispatch.h`, `src/native_graphics/native_host_surface.cpp`, `src/scripted_input.h`, `src/scripted_input_logic.h`, `tests/native_backend_host_tests.cpp`, `tests/native_capture_policy_tests.cpp`, `tests/native_frame_dispatch_tests.cpp`, `tests/scripted_input_reload_tests.cpp`.
Existing targets: `edf_native_backend_host_tests`, `edf_native_capture_policy_tests`, `edf_native_frame_dispatch_tests`, `edf_scripted_input_tests`.

Observed sites: 70; direct dependency slice: 192 functions; shared with other packages: 184.
Dispositions: {"bounded call-path investigation; no original dependency inferred from name alone": 65, "native-path dependency or extraction; effect contract required": 5}.

- **R01.scope**: For each assigned function/site, trace the seed-to-function path and its outgoing calls; label renderer consumer, producer, shared utility or unreachable for a named mode. Expand roots for newly found render targets. Scope labels need instruction/source evidence. Completion: Every census function and indirect site has an evidenced role or an explicit follow-up; no utility is declared renderer work solely from reachability, and exclusions name the mode and proof.
- **R01.callbacks**: Resolve the eight 821A5080 sites and bucket slot 4 at 821A3C50 using registration/removal writers, receiver identity and verified vtables; check player/free/motion/base cameras and listeners. Completion: Registration-to-call table covers all eight sites plus bucket dispatch; runtime target traces map to it; unknown targets fail coverage and get a task.
- **R01.modes**: Inventory startup/loading/gameplay/pause/results/movie, player counts, camera modes, backend choices, ownership toggles and diagnostic branches. Decide support or explicit unsupported behavior for each combination. Completion: Mode matrix includes guards and fallback behavior; every supported branch has a scenario and every unsupported branch has a tested diagnostic.
- **R10.trace**: Add bounded per-scenario counters for callback instruction/target/receiver, content method, pass identity, backend and fallback reason; merge with static ledgers and report new routes. Completion: A scripted run produces per-scenario route IDs and new-target tasks; sampling/overflow and unexercised routes are explicit. Aggregate draw counts alone cannot pass.
- **R10.capture**: Extend the one-shot native_backend_host GPU capture to a bounded timestamped burst with published sequence IDs; retain final composition and label partial scene captures separately. Hidden automated runs have no capturable main window through the existing window script. Completion: Before/during/after gameplay, pause and loading frames are captured with sequence and wall-clock metadata; final UI composition is present and no partial output is mislabeled complete.
- **R10.input**: Replace elapsed-time-only scenario assumptions with a reached-gameplay/camera-control marker; record intended and actual poll time and extend the movement/fire window after cutscene completion. The probe delivered the 180000 ms movement event at 183403 ms. Completion: Each control action has a reached-state precondition, observed input interval and visible/state effect; delayed polling and intro camera motion cannot produce a false movement pass.
- **R10.scenarios**: Exercise the scenario table with deterministic controls, timestamped screenshots, route counters and a matched reference. Include all map/environment and enemy/weapon families identified by the asset/mission census. Completion: Every scenario records reached/observed/compared status and artifacts; failures have tasks; no queued input or screenshot is mistaken for proof of its intended effect.

## P01 — Resource, state and retirement contracts

Depends on: P00.
Tasks: R09.effects, R09.resources, R09.retirement, R09.shader, R09.dynamic, R09.assets.
Features: F06 Projectiles, particles, sparks, smoke and trails, F09 Destruction, debris, removal and respawn, F15 HUD, radar, crosshair, fonts and in-game overlays, F23 Texture, shader and declaration assets/contracts, F24 Shader bindings/default streams and material state, F25 VB/IB/dynamic buffers, locks and physical aliases, F26 Retirement, allocator failure, destruction and reuse, F32 All maps, weapons and runtime-only declaration combinations.
Owner files: `src/native_graphics/guest_shader_bridge.cpp`, `src/native_graphics/native_buffer_writes.h`, `src/native_graphics/native_contract_ledger.h`, `src/native_graphics/native_index_binding.h`, `src/native_graphics/native_model_buffers.h`, `src/native_graphics/native_shader_binding.h`, `src/native_graphics/native_texture_binding.h`, `src/native_graphics/native_upload_ring.h`, `tests/fixtures/geometry-contract-mission1.txt`, `tests/native_effect_tests.cpp`, `tests/native_quad_tests.cpp`, `tests/native_scene_tests.cpp`, `tests/native_shader_binding_tests.cpp`, `tests/native_upload_ring_tests.cpp`.
Existing targets: `edf_native_backend_completion_tests`, `edf_native_effect_tests`, `edf_native_guest_memory_tests`, `edf_native_quad_tests`, `edf_native_scene_tests`, `edf_native_shader_binding_tests`, `edf_native_texture_tests`, `edf_native_upload_ring_tests`.

Observed sites: 102; direct dependency slice: 3205 functions; shared with other packages: 2763.
Dispositions: {"conditional unresolved path; cannot claim bypassed": 13, "native-path dependency or extraction; effect contract required": 25, "producer/interface candidate; do not remove simulation or lifetime effects": 63, "review helper guard; reference alone is not execution": 1}.

- **R09.effects**: For each assigned explicit/macro/adapter/store boundary, finish local effects if pending and trace transitive writes, readers, aliasing, exceptions and callbacks. Assign simulation, producer lifetime, native consumer or compatibility ownership. Completion: Every assigned source site has a complete effects/owner/ordering table with instruction evidence and differential tests; partial local review is not a completed status.
- **R09.resources**: Review texture/declaration/shader/VB/IB create/import/lock/unlock/destruction and physical/virtual alias writers, including ImportTexture function reference; retain versioned identity through frame completion. Completion: Create/failure/update/free/reallocate-same-address and alias tests reject stale handles and preserve in-flight frames, with no unreported guest writes.
- **R09.retirement**: Trace 82141440 queue growth/consumption, immediate fence tags, allocator providers, reference transitions and resource destruction. Preserve callback ordering and failure semantics. Completion: Queue exhaustion, allocation failure, same-resource rebind, delayed GPU completion and address reuse have explicit expected results and passing lifetime tests.
- **R09.shader**: After the fixed retirement-before-default-read ordering, investigate payload aliasing during incremental constant writes, concurrent mutation and malformed count/extent behavior; either preserve reachable behavior or prove input preconditions. Completion: Differential fixtures cover pixel/vertex, null/no-default, payload self-alias and failure-after-partial-write; document excluded malformed streams with provider evidence.
- **R09.dynamic**: Enumerate immediate/nonindexed/quads, wire/trail/debris and transient ring producers; snapshot mutable bytes before render and retain through GPU consumption. Completion: Wraparound, append, concurrent publication, removal and large effect bursts never overwrite in-flight data; geometry contracts record all encountered formats.
- **R09.assets**: Re-run disc effect/texture coverage, enumerate technique render-state combinations, merge runtime declaration catalogs and add offline replay for immediate/font/movie/XUI/utility contracts. Completion: Zero missing assets, rejected contracts and omitted records across the declared content corpus; catalog growth is reported, not treated as proof of exhaustion.

## P02 — Producer publication, camera and view ownership

Depends on: P00, P01.
Tasks: R02.publication, R02.camera, R05.pass, R05.post.
Features: F05 Vehicles, mounted weapons and vehicle cameras, F11 Camera, projection, zoom and interpolation, F12 Single/multiple views, split-screen, free/motion cameras, F13 Targets, viewport, scissor, clear, resolve and offscreen passes, F14 Post-processing, environment/fog, tone mapping and gamma, F22 Render size, aspect, MSAA and sampler quality, F30 Independent frame scheduling and performance.
Owner files: `src/native_graphics/effect.cpp`, `src/native_graphics/guest_shader_bridge.cpp`, `src/native_graphics/native_camera_history.h`, `src/native_graphics/native_model_pose_history.h`, `src/native_graphics/native_scene_adapter.h`, `src/native_graphics/native_scene_pass_inputs.h`, `src/native_graphics/native_scene_sources.h`, `tests/native_display_gamma_tests.cpp`, `tests/native_scene_tests.cpp`.
Existing targets: `edf_native_backend_compositor_tests`, `edf_native_display_gamma_tests`, `edf_native_effect_tests`, `edf_native_scene_tests`.

Observed sites: 84; direct dependency slice: 2139 functions; shared with other packages: 290.
Dispositions: {"native-path dependency or extraction; effect contract required": 11, "non-renderer producer candidate; preserve at explicit interface": 2, "producer/interface candidate; do not remove simulation or lifetime effects": 71}.

- **R02.publication**: Split 820B4250 counter/time mutations and 821A4DE8 publication from render repeats. Publish camera, transforms, animation, effect clocks and resource generation together. Completion: Render a retained generation repeatedly: simulation counters, authoritative poses and resource lifetimes remain unchanged; one producer tick advances once.
- **R02.camera**: Replace live camera inputs around 821CDDF8 and 821BE8D0 with retained per-view projection/view/FOV/viewport values; preserve matrix rounding and source-pose restoration. Completion: Locked/unlocked movement, zoom, camera changes and dimension edge cases match reference matrices; simultaneous views never share camera state accidentally.
- **R05.pass**: Specify begin/end, inherited target, clear, viewport, scissor, format, resolve and post-pass contracts, including 821BE8D0/821BE9D8 and tiled-to-untiled compatibility branches. Completion: Nested/offscreen/multiple-view tests verify explicit target and state restoration; loading direct-scene output and normal post output both present.
- **R05.post**: Enumerate techniques that implement tone mapping, exposure/bloom-like passes, fog/environment and output gamma; document which are actually present, with exact pass identities. Completion: Named pass captures compare scene color and final output across authored presets, MSAA modes and 720p/1080p; no unnamed technique or rejected state remains.

## P06 — Compatibility and diagnostic mode policy

Depends on: P00, P01.
Tasks: R09.compat.
Features: F20 D3D12/D3D11 hardware and WARP/direct/recorded modes, F28 Bridge-disabled, ownership fallback and audit modes.
Owner files: `src/native_graphics/guest_shader_bridge.cpp`.
Existing targets: `edf_native_frame_dispatch_tests`.

Observed sites: 43; direct dependency slice: 2798 functions; shared with other packages: 2763.
Dispositions: {"conditional compatibility; mode policy must decide": 40, "conditional unresolved path; cannot claim bypassed": 2, "native-path dependency or extraction; effect contract required": 1}.

- **R09.compat**: For every disabled-bridge/host/activation/ownership fallback and diagnostic branch, retain an explicit supported owner or reject the mode; verify macros are forwarding, not replacements. Completion: Toggle matrix exercises all declared paths; no branch is marked native solely because a hook exists and no removed mode silently falls back.

## P03 — Complete retained static rendering

Depends on: P00, P01, P02.
Tasks: R03.selection, R03.mutation, R04.pass.
Features: F01 Static world geometry/material/world matrices, F02 Visibility, hierarchy, LOD, hidden and duplicate state.
Owner files: `src/native_graphics/guest_shader_bridge.cpp`, `src/native_graphics/native_scene_handoff.h`, `src/native_graphics/native_scene_material.h`, `src/native_graphics/native_scene_membership.h`, `src/native_graphics/native_scene_pass_inputs.h`, `src/native_graphics/native_scene_sources.h`, `src/native_graphics/native_scene_tree_publication.h`, `src/native_graphics/native_scene_visibility.h`, `tests/native_scene_tests.cpp`.
Existing targets: `edf_native_immediate_tail_tests`, `edf_native_scene_tests`.

Observed sites: 19; direct dependency slice: 19 functions; shared with other packages: 14.
Dispositions: {"audit-only reference; retain validation oracle": 5, "conditional unresolved path; cannot claim bypassed": 8, "native-path dependency or extraction; effect contract required": 6}.

- **R03.selection**: Publish hidden/mode flags, duplicate marks, LOD inputs, hierarchy and ordered membership together at 821C61D8/820B4038/821BEE68/821C3BB8; eliminate mixed live-generation selection. Completion: Visibility, LOD, duplicate/order, removal, reinsert and address-reuse fixtures match the reference and audited gameplay without live selection reads.
- **R03.mutation**: Enumerate 821C0C00 callback implementations and mutation writers; specify republish/defer behavior for every unsupported or mid-selection mutation route. Completion: Adversarial callback/removal tests preserve order and retained lifetimes; fallback counts are attributed to a declared route and no silent live-list restart remains.
- **R04.pass**: Move required CPU setup/activation/indexed-tail effects at 821D96D8, 821B94E8 and 821B8E48 to explicit producer or native owners; submit geometry/material/world from retained inputs. Completion: Static pass succeeds with legacy setup, activation and draw submission disabled; mixed-content ordering and retirement remain correct on D3D11/D3D12.

## P05 — Composition, loading, presentation and synchronization

Depends on: P00, P01, P02.
Tasks: R08.ui, R08.loading, R08.movie, R08.present, R09.sync.
Features: F15 HUD, radar, crosshair, fonts and in-game overlays, F16 Title, menus, pause, mission results and SDK settings, F17 Loading, direct-scene output and concurrent loader, F18 Movies, conversion, skipping and movie-to-menu transition, F19 Presentation, VSync/VRR, pacing and surface ring, F21 Geometry workers, frame credits and submission budgets, F22 Render size, aspect, MSAA and sampler quality, F27 Worker signals, events, locks, vblank and profiling, F29 Resize, fullscreen, minimize/restore and shutdown.
Owner files: `src/native_graphics/font_effect.cpp`, `src/native_graphics/guest_shader_bridge.cpp`, `src/native_graphics/movie_effect.cpp`, `src/native_graphics/native_backend_frame_queue.h`, `src/native_graphics/native_backend_host.cpp`, `src/native_graphics/native_backend_present.cpp`, `src/native_graphics/native_canvas_constants.h`, `src/native_graphics/native_host_surface.cpp`, `src/native_graphics/native_movie_bindings.h`, `src/native_graphics/native_xui_bindings.h`, `tests/native_host_lifetime_tests.cpp`, `tests/native_movie_tests.cpp`, `tests/native_ui_tests.cpp`, `tests/native_worker_callback_audit_tests.cpp`, `tests/native_xui_tests.cpp`.
Existing targets: `edf_native_backend_completion_tests`, `edf_native_backend_compositor_tests`, `edf_native_backend_host_tests`, `edf_native_backend_sdk_ui_tests`, `edf_native_host_lifetime_tests`, `edf_native_movie_tests`, `edf_native_present_tail_tests`, `edf_native_ui_tests`, `edf_native_worker_callback_audit_tests`, `edf_native_xui_tests`.

Observed sites: 13; direct dependency slice: 209 functions; shared with other packages: 184.
Dispositions: {"conditional unresolved path; cannot claim bypassed": 4, "native-path dependency or extraction; effect contract required": 3, "platform contract; not generated renderer code": 2, "producer/interface candidate; do not remove simulation or lifetime effects": 4}.

- **R08.ui**: Migrate player overlay 820D3FD0, listener callbacks and UI composition to retained draw records; cover text, radar, menus, pause/results and SDK settings overlay. Completion: Startup/menu/gameplay/pause/results show correct order, text, clip/scissor and authored canvas at 720p/1080p and alternate aspect ratios; original UI callbacks disabled for consumer.
- **R08.loading**: Specify no-active-output/direct-scene publication, concurrent loader submissions and menu-to-map/retry/exit handoff; avoid holding submission locks across pacing waits. Completion: Cold and warm load, retry, return to menu and another mission show changing loading frames with bounded queues; old-generation resources drain before reuse.
- **R08.movie**: Trace movie decode producers and retained original calls; retain YUV/RGB textures and conversion parameters until consumption, including skip and end-of-movie. Completion: Supported SD/HD movie paths play, skip and transition with correct color/topology and no stale frame or premature plane retirement.
- **R08.present**: Review host files outside native_graphics, 820B0B80 finish callbacks, surface ring ownership, resize/fullscreen/minimize/restore and device recreation policy. Completion: Resize and mode changes preserve frame order and resource completion; host tests pass and captured unique frame IDs match submissions with no unexplained repeats/drops.
- **R09.sync**: Resolve worker registration modes, 8213C9F0 alternate signals, access10/12/14 and other lock callers, monitor/profiling callbacks, vblank/event delivery and SDK imports. Completion: Each callback population, activation guard and lock/event owner is evidenced; contention, failure and shutdown tests terminate without deadlock or lifetime races.

## P04 — Animated models and remaining world families

Depends on: P00, P01, P02, P03.
Tasks: R06.pose, R06.families, R07.effects, R07.environment, R07.shadows, R07.special.
Features: F03 Rigid models, skinning, animation and attachments, F04 Soldiers, civilians, insects, bosses and multipart enemies, F05 Vehicles, mounted weapons and vehicle cameras, F06 Projectiles, particles, sparks, smoke and trails, F07 Muzzle flash, shells, transparent glass and additive effects, F08 Sky, terrain decorations, wires, trees and grass, F09 Destruction, debris, removal and respawn, F10 Shadows and caster/receiver passes, F31 Base/shared no-op and debug/test object families.
Owner files: `src/native_graphics/effect.cpp`, `src/native_graphics/guest_shader_bridge.cpp`, `src/native_graphics/native_model_pose_history.h`, `src/native_graphics/native_scene.h`, `src/native_graphics/native_scene_geometry.h`, `src/native_graphics/native_scene_membership.h`, `tests/native_scene_tests.cpp`.
Existing targets: `edf_native_effect_tests`, `edf_native_quad_tests`, `edf_native_scene_tests`.

Observed sites: 7; direct dependency slice: 129 functions; shared with other packages: 117.
Dispositions: {"conditional unresolved path; cannot claim bypassed": 2, "native-path dependency or extraction; effect contract required": 5}.

- **R06.pose**: Migrate 821C9478/821C9C20/821B2C28 and 821A1738/821A17D8 into retained rigid/skinned draw records with owned matrix arrays and upload dirty-state effects. Completion: Rigid and skinned fixtures, attachments, interpolation and LODs render with original model traversal disabled and unchanged authoritative bones.
- **R06.families**: For each assigned renderable method, follow direct and indirect routes to submission, classify per-instance/attachment overrides and migrate the remaining native consumer; keep external class labels provisional. Completion: Soldier/civilian/enemy/large multipart/vehicle/attachment rows each have a targeted scene or evidenced unreachable disposition; no original render callback needed.
- **R07.effects**: For each particle, spark, smoke, projectile, muzzle, shell and glass method, retain geometry/material/lifetime/sort inputs and remove per-render simulation changes. Completion: Controlled firing, explosions, trails, overlapping alpha/additive effects and expiry match reference ordering; render repeats do not age effects.
- **R07.environment**: Follow sky, wire, rock, tree, grass, broken-object and broken-piece routes; implement their topology, material and lifecycle deviations from the static group path. Completion: Outdoor/environment and destruction scenes exercise each family; broken/removing objects leave no stale retained draws or resource aliases.
- **R07.shadows**: Trace shadow-related effect techniques, render-target/depth writes and caster/receiver callbacks from source/assets; associate observed pass identities with static, animated and effect casters. Do not infer shadows from a dark patch. Completion: Document the actual shadow technique and target contract (or evidence of absence per mode), then compare static/moving caster and receiver occlusion in controlled captures.
- **R07.special**: For shared 8252B718 and test/base-class rows, verify concrete dispatch and overrides; determine whether each family renders elsewhere, is producer-only, or is genuinely unreachable. Completion: Each class/table row has registration and slot evidence; no-op local body alone never closes a whole content family.

## P07 — Independent cadence and final performance acceptance

Depends on: P00, P03, P04, P05.
Tasks: R10.cadence, R10.performance.
Features: F19 Presentation, VSync/VRR, pacing and surface ring, F20 D3D12/D3D11 hardware and WARP/direct/recorded modes, F21 Geometry workers, frame credits and submission budgets, F22 Render size, aspect, MSAA and sampler quality, F30 Independent frame scheduling and performance.
Owner files: `src/native_graphics/guest_shader_bridge.cpp`, `src/native_graphics/native_frame_flight.h`, `src/native_graphics/native_pacing.h`, `src/native_graphics/native_queued_scene.h`.
Existing targets: `edf_native_host_lifetime_tests`, `edf_native_pacing_tests`, `edf_native_scene_tests`.

Observed sites: 1; direct dependency slice: 1 functions; shared with other packages: 1.
Dispositions: {"native-path dependency or extraction; effect contract required": 1}.

- **R10.cadence**: Consume retained full-frame publications independently of the simulation clock, interpolate render inputs and retire generations by consumer completion. Completion: 60 Hz producer / 120 Hz consumer produces measured unique frames with stable simulation over movement/combat/load and no original consumer helper invocation.
- **R10.performance**: Run D3D12 hardware and declared D3D11 fallback, worker counts 0/1/4, MSAA 1/2/4, native resolutions and frame credits; use a smaller pairwise matrix with rationale for combinations. Completion: Report CPU/GPU/frame-time distributions, unique image cadence, visual comparisons and memory peaks with audit instrumentation off; 120 Hz is measured rather than inferred from a cap.

## Coordination

One integrator owns guest_shader_bridge.cpp, shared resource/retirement interfaces and acceptance. Parallel workers own bounded files/tests; shared helpers receive one contract owner before edits. No agent gets an unbounded whole-package rewrite.
Run the focused target while iterating and `validate-renderer-offline.cmd` at integration. Use game boots only for declared feature/lifecycle milestones. Record failures and provenance in Epistemic.
No fallback, producer, shared utility or audit call is removed merely to reduce the counts. Compatibility deletion requires a declared supported-mode decision.
