# Native scene renderer

For the current interruption state, known material-loader bug, verification
ledger and next steps, read the [2026-09-18 handoff](native-scene-renderer-handoff.md).

## Objective

The game publishes scene changes at its simulation cadence. A native renderer
owns geometry, materials and object state, and can render between game updates
without executing the original render helper. Camera and object interpolation,
visibility and instance submission belong to that native renderer.

This is an architectural migration, not a cache of recorded guest draw calls.
The current helper still executes for each render iteration. The most recent
prepared-geometry comparison measured about 11 ms in that helper, versus an
8.33 ms total budget for 120 FPS. Faster acquisition did not reduce the helper
total. See [the measurement](prepared-geometry.md).

## Implemented foundation

`NativeIndexedMesh::RetainDraw` creates an independently owned, immutable
geometry range. It retains native vertex/index generations and the backend
that created them, validates the index range once, and owns a copy of the input
layout's semantic names. Destroying a cache entry or reusing a guest resource
identity cannot change that retained geometry. Dynamic vertex allocations are
rejected: a published scene must not silently observe subsequent buffer edits.

Retained geometry supports ordinary and explicit instanced indexed submission.
The caller supplies the compatible pipeline, material and instance stream and
must use a recorder from the owning backend. Resource release follows the
backend's existing serialization requirements; shared ownership alone does not
make the backend safe for concurrent mutation.

Release build and native quad rendering tests pass. Readback checks cover cache
invalidation, cache clearing and guest identity reuse; rejection checks cover
invalid ranges, missing backend ownership and mutable vertices. This API is not
yet wired into gameplay and no frame-rate improvement is claimed.

`native_scene.h/.cpp` now implements native object lifetime tokens, immutable
simulation snapshots, material generations, and native scene submission. A
single producer creates/updates/removes objects, then publishes a strictly
increasing simulation tick. Unchanged instances share their immutable storage;
old snapshots retain deleted objects/resources until their consumers release
them. Clearing a scene does not reuse IDs. Publication is atomic to readers.

The renderer samples previous/current object transforms at the requested
fraction, accepts camera matrices independently for each render, conservatively
culls local bounds in homogeneous clip space, and batches adjacent compatible
objects into direct instanced draws. Material bindings explicitly distinguish
world/view/projection constants and row/column matrix packing. Order is supplied
by the producer and is never changed to improve batching. Unsupported transform
interpolation (shear, reflection, discontinuity) snaps to the current transform.
Missing publication ticks and replaced geometry/material generations also snap.

The renderer draws into caller-owned targets; BeginFrame, clears, GPU completion
and submission remain the frame owner's responsibility. It owns shader bindings
during its pass, so a mixed guest/native caller must invalidate the bridge's
binding cache afterwards. Material descriptions must include every consumed
constant/resource slot, including explicit null textures where required. Scene
resource retirement must stay serialized with backend use.

`edf_native_scene_tests` verifies actual pixel output on D3D11 and D3D12 WARP,
with both row-major and column-major world matrices. Checks cover independent
camera rendering of the same snapshot, transform interpolation, unchanged-object
reuse, old-snapshot rendering after deletion, material ordering, clipping,
publication gaps and a 257-object batch boundary. The release build passes.
Evidence: `out/native-scene-build.log`, `out/native-scene-tests.log`.

The renderer is also connected to retail queued static-world submission through
the opt-in path described below. The independent gameplay frame scheduler and
removal of guest scene traversal remain unimplemented. Standalone checks alone
do not establish retail coverage.

## Native queued submission

Simulation publication also retains material input programs when scene preload
is enabled. Each program owns the reflected register payloads in activation
order, marks local versus global inputs, retains both vertex variants and the
pixel shader, and retains the exact native texture generations. Local and global
texture records contribute their slot, LOD-bias bits, mip/min/mag requests and
texture LOD range. Ordered render-state offset/value records are owned as data;
they are not executed against a guest device.

Material activation is a patch over pass state. Sampler addressing and other
fields can be inherited, and render-state records invoke device setters. The
input publication deliberately does not pretend that the last visible draw's
complete device state belongs to an immutable material. The native pass must
resolve these overrides against its explicit state and create the pipeline and
samplers. `NativeSceneMaterialProgram::Capture` then reconstructs constant and
resource bindings from owned inputs, with no guest reads or activation callback.
Applying the state programs in the gameplay renderer is still incomplete.

Rendering checks reconstruct a material equal to the original binding capture
and render it after the producer bindings and source lifetime change, on D3D11
and D3D12. Source checks cover local/global indirection, unused payloads, register
capacity, resource slots, sampler fields, state ordering and immutable values.
Unchanged material programs share storage; shader/texture replacement, changed
values and source-group lifetimes cause new publications or retirement.

`--edf_native_scene_preload=true`, together with the queued scene option, loads
registered static-group geometry at the simulation publication boundary. It
decodes the group descriptor directly: embedded vertex resource at +4, embedded
index resource at +84, selected declaration node +28, stride +60, and triangle
count +140. It resolves the material's vertex shader without activating the
material. No guest device setter, material callback, instance upload or draw
runs during this loading path.

Only registered physical buffers are accepted. Both buffer snapshots use the
existing writer exclusion and sampled comparison policy; publishing their GPU
storage requires a successful joint commit. Group membership, descriptor inputs,
buffer generations/revisions, declaration identity and shader bytecode identity
participate in reuse. Failed refreshes remove the current group asset and retry
on a later simulation publication. Older immutable publications keep their own
geometry. Source retirement/replacement removes stale current group records.

Scene publications now include geometry groups independently of renderable
instances. The geometry pool is shared with subsequent draw-based material
capture. Rendering checks cover publishing with no visible objects/materials,
mesh/source retirement, group-address reuse, and drawing retained geometry later
on D3D11 and D3D12. Descriptor checks verify resource roles, triangle rounding,
rejection of malformed inputs and absence of guest writes.

This option is off by default while the full pass is incomplete. Materials still
depend on guest activation, including sampler settings and render-state callbacks;
preloaded geometry alone does not make an unseen group independently renderable.
The original helper still generates gameplay frames.

The gameplay run's first geometry publication had **412/412 groups ready**;
the last sampled publication also had 412/412 ready. Across scene lifetimes it
recorded 14,293 successful loads/refreshes and 2,694,607 unchanged reuses, with
zero deferred loads, unreported geometry writes, native direct retries, queue
fallback batches or logged errors. Loads/refreshes are operations, not distinct
group counts. The Mission 1 image was inspected and the owned game was stopped.
The 199-241 second window measured **55.15 FPS**, which establishes no speedup
over the preceding 56.75 FPS group-population run. Geometry loading is now
independent of visible draws; independent material loading and frame generation
remain incomplete.

Evidence: `out/native-scene-geometry-preload-build.log`,
`out/native-scene-geometry-preload-tests.log`,
`out/native-scene-geometry-preload-process.json`,
`out/native-scene-geometry-preload-summary.json`,
`out/native-scene-geometry-preload-fps.json` and
`out/native-scene-geometry-preload.bmp`. The original group setup and material
shader-selection bodies are source-fingerprint gated. Generated code is unchanged.

Registered static parts now have an event-maintained material-group index. Once
a queued group has retained geometry and a material, the adapter populates its
eligible registered parts, including parts which have never been selected or
drawn. Each receives its own event-published world transform and native lifetime
identity. This does not append unseen parts to the current draw queue.

Population requires a complete vertex world-matrix override with matching
registered storage. Any drawn instance with other overrides excludes the group;
unseen parts with unsupported overrides are individually rejected. The bridge
does not run their draw callbacks or modify their guest constants. Membership
revisions and retained asset identities avoid repeating unchanged population;
model replacement invalidates affected groups even when guest addresses are
unchanged. Retiring an unrelated owner leaves populated groups intact.
Group hints hold weak asset references, while published scenes own their assets.

Rendering checks cover a never-observed part, rejection of unsupported parts,
publication after source retirement, same-address model replacement and owner
address reuse on D3D11 and D3D12. Build and checks pass in
`out/native-scene-group-assets-final-build.log` and
`out/native-scene-group-assets-final-tests.log`.

This closes the per-object first-visible-draw dependency for supported members
of a captured group. Entirely unseen groups still lack geometry/material capture;
the group capture still depends on the helper's setup callbacks. The published
catalog is not yet an independently selected gameplay pass. No helper removal
or performance improvement is established by these checks.

The final gameplay run populated **44,497** native part records before their own
first draw across the run's scene lifetimes. At the last sample it had examined
146,363 parts, rejected none, and recorded no deferred population failures,
native direct retries, queue fallback batches or logged errors. The live native
catalog contained 23,289 objects at the last direct-instance sample. These are
coverage counters, not proof that all groups or all passes have native assets.
The Mission 1 image was inspected and the owned game process was stopped.

The 199-241 second window measured **56.75 FPS**. The initial version, which
invalidated all population hints on every retirement, measured 53.45 FPS and
examined over two million parts. The final version invalidates only groups
containing the retired owner. These runs do not establish an improvement over
the earlier 61.725 FPS publication run. The queued scene remains opt-in and
the guest helper still executes; native ownership currently adds work before
the independent scene pass can replace that helper work.

Evidence: `out/native-scene-group-assets-final-process.json`,
`out/native-scene-group-assets-final-summary.json`,
`out/native-scene-group-assets-final-fps.json` and
`out/native-scene-group-assets-final.bmp`. The source-boundary fingerprint audit
also passes; no generated guest routine was edited.

Spatial-list membership now has an event-owned native index. Node construction
(`821C4EB8`) and copy construction (`821C5D28`) establish list generations;
intrusive insertion (`821A1628`) preserves predecessor order and detaches old
membership, and removal (`821A1678`) removes members or retires list headers.
Destruction of a spatial-node range uses that same header-removal routine.
Changed lists publish immutable ordered arrays at the simulation transition;
unchanged arrays retain their storage. A late event can materialize a new array
from the native index without walking guest links.
Publication maintains a changed-list queue, so it does not scan the entire
spatial-list registry each simulation update. Normal rendering also checks the
already-read head/end against the publication and falls back on disagreement.

Native leaf visibility consumes these arrays until it needs an unported guest
dispatcher. It then drops the array and follows the live post-callback link for
the remainder of that leaf, preserving membership mutation semantics. Lists
without audited spatial-node lifetimes retain guest traversal. The visibility
audit also compares the complete ordered native list with the live guest list.
These bridge membership records still identify guest nodes/owners; they do not
yet own the geometry and dispatch state needed to remove all guest callbacks.

The gameplay audit checked **683,404** ordered lists with **zero mismatches**.
Native arrays supplied **31,619,986** node visits across **85,192** membership
events. The registry contained 37,448 spatial lists and 15,684 live member nodes
at the last sample. Culling comparisons also remained at zero mismatches across
11,438,821 checks; there were no queue fallback batches or logged errors. The
Mission 1 capture was inspected, and the owned game process was stopped.
This diagnostic run is not a normal-path performance measurement. The native
visibility route remains explicitly opt-in.

Evidence: `out/native-scene-membership-audit-summary.json`,
`out/native-scene-membership-audit.bmp`,
`out/native-scene-membership-final-build.log` and
`out/native-scene-membership-final-tests.log`. Checks cover predecessor order,
cross-list moves, removal, moves to untracked lists, unchanged publication reuse,
immutable old publications and list-address reuse, plus D3D11/D3D12 rendering.
The final build additionally limits publication to changed lists and adds the
normal-path head/end guard; these do not change the audited culling math.

Native leaf visibility (`820B4038`) now transforms centers, applies the distance
cutoff, classifies spheres and oriented boxes, and directly selects static LOD
parts for audited direct-dispatch objects. The pure visibility code accepts
native camera and bound values without a guest reader. PPC single-precision
operation order and inclusive boundary comparisons are preserved. Large boxes
enclosing the frustum remain visible even if none of their corners is inside.

Construction, model replacement, dirty update, the bound writer `821BEF10`, and
distance setter `821C0B88` retain immutable visibility metadata for static owners.
This metadata exists before geometry is first drawn. Other object types still
read their bounds from guest state. The mixed leaf keeps guest list membership,
duplicate-generation marks, context-center writes, hidden flags, sorting modes,
and unknown virtual callbacks. It reads the next list node after callbacks,
matching the guest's mutation semantics. Native queue selection preserves group
ordering and verifies no unknown producer has changed a group's guest head.
Header accesses and ordinary generation/center stores use validated CPU windows;
the center write window is reacquired after any remaining guest dispatcher.
No interlocked publication is required for these fields. The next node is reused
only if the iteration ran pure native work; a guest callback forces a fresh read.
`NativeSceneCpuWindow` amortizes committed-page validation across adjacent CPU
objects during that pure traversal. It caches access, never contents, admits
writable pages separately, and invalidates every page before a guest dispatcher.
Cross-page accesses use the backing validator. These borrowed windows never
become persistent scene assets or GPU resources.

`--edf_native_scene_visibility=true` enables the native leaf implementation
inside the opt-in queued scene path. It defaults off because the mixed path's
remaining guest-memory accesses still make it slower than the original leaf.
`--edf_native_scene_visibility_audit=true` compares retained metadata, transformed
centers, sphere classifications and box classifications with live state and the
original routines. Mismatches are errors and use the original result during the
audit. Normal operation does not run those diagnostic guest routines.

This removes leaf culling and eligible static LOD dispatch from guest execution;
spatial hierarchy traversal and pass membership still come from the guest.
Independent native membership and eager geometry/material capture remain
necessary before the entire world pass can bypass the helper.

The remaining hierarchy boundary is concrete: `821C61D8` iterates 144-byte root
nodes and invokes `821C5FC8`. A node has an occupancy count at +116, eight child
pointers at +84, a center at +32, half extents at +48, radius at +64 and a leaf
list at +120. `821C5FC8` performs node sphere/AABB culling; `821C56C0` visits
accepted descendants and calls the native leaf. A root flag at +108 also enables
a virtual callback, and root +100/+104 contain traversal counters. Those effects
and membership mutation must be accounted for when replacing this boundary.
There are additional direct leaf lists at world+372 and overlay-object+48, so
the registered static-owner catalog alone is not an authoritative pass list.

*Corrected 2026-09-22: see [renderer-status.md](renderer-status.md).*

The initial gameplay audit compared 11,025,360 sphere/box results with zero
mismatches; it also checked retained metadata and center transforms. It consumed
23,574,735 retained bounds and directly selected 4,497,829 static objects. Queue
fallback batches and logged errors were zero, and the Mission 1 capture was
inspected. Evidence: `out/native-scene-visibility-audit-summary.json` and
`out/native-scene-visibility-audit.bmp`. The audit process was stopped.

The first normal implementation regressed to 38.65 FPS in the established
199-241 second window. Batching ordinary CPU writes/accesses recovered 46.125
FPS, still below the preceding publication path's 61.725 FPS. A 2,000-sample
helper profile then found 251 samples in the native leaf, 439 in the runtime DLL
and 485 in ntdll, motivating the short-lived page windows. The symbol-map copy's
`.text` hash matched the sampled executable. Evidence is retained in
`out/native-scene-visibility-fps.json`,
`out/native-scene-visibility-batched-fps.json` and
`out/native-scene-visibility-batched-profile.json`; none establishes a speedup
over the earlier guest leaf. The corresponding owned processes were stopped.

With page windows, the same gameplay window measured **50.375 FPS**. The final
counter recorded 55,000,025 candidates, 44,904,792 retained bounds, 8,894,206
direct native static selections, and zero queue fallback batches or logged
errors. The capture was inspected and the process stopped. This remains below
the earlier 61.725 FPS publication run, so native leaf visibility defaults off;
the native implementation remains available for the independent scene migration.
This is not a matched same-build baseline comparison or an all-content claim.

Evidence: `out/native-scene-visibility-window-fps.json`,
`out/native-scene-visibility-window-summary.json`,
`out/native-scene-visibility-window.bmp`,
`out/native-scene-visibility-window-tests.log` and
`out/native-scene-visibility-final-build.log`. Checks cover culling boundaries,
frustum-enclosing boxes, LOD thresholds, source retirement/address reuse, live
CPU contents, writable-page validation, invalidation after a protection change,
and cross-page range validation, in addition to the D3D11/D3D12 rendering checks.

The adapter now publishes immutable scene records at `821A4DE8` after dirty
updates complete, advancing its tick only for simulation steps (or locked
operation). Static update `820B2DF8` applies the event-owned world registers to
existing native objects under the audited owner generation. Publication includes
retirement, including an empty scene, and retains unchanged instance records.
An immutable ID index allows render selections to locate published records
without rebuilding a lookup per displayed frame.

Each `821A5080` invocation acquires one publication. Selected objects use that
record when it matches current retained state; newly captured or changed assets
use the current record. Nested helpers restore their previous publication, and
resource references are released under backend submission serialization.
Counters distinguish published selections from current-state selections.

This moves transform updates and publication to simulation boundaries, but does
not establish complete scene coverage: geometry and materials are still first
captured through drawing, and guest visibility still selects parts and LODs.
The next ownership migration must capture unseen assets and pass membership,
then replace visibility traversal. Reusing a publication alone cannot remove
the guest helper. Gameplay interpolation remains disabled on this mixed path.

The publication gameplay run selected 12,989,670 published records and 10,332
current records at its final counter (over 99.9% published). It recorded zero
queue fallback batches and no logged errors. The Mission 1 capture was inspected
and the owned game process stopped. The same 199-241 second diagnostic window
measured 61.725 FPS versus the preceding 61.3 FPS run; this is not evidence of a
meaningful speedup. This change establishes a simulation publication consumer.
It does not yet remove guest scene selection or first-draw asset capture.

Release build, D3D11/D3D12 scene checks and original-body fingerprint checks pass.
New checks update a transform without another draw observation, reject a stale
owner generation, render the resulting publication, preserve unchanged records,
and retain older publications after updates and retirement. Evidence is in
`out/native-scene-publication-build.log`, `out/native-scene-publication-tests.log`,
`out/native-scene-publication-fps.json`, `out/native-scene-publication-summary.json`
and `out/native-scene-publication.bmp`.

`--edf_native_scene_queued=true` routes supported queued static-world draws through
`NativeSceneAdapter` and `NativeSceneRenderer`. The adapter keeps native objects
under audited owner-generation/LOD/part identities, retains geometry across mesh
cache eviction, and interns equivalent material images. Backend ownership is
shared so retained resources cannot outlive their device owner.

Guest visibility still selects LODs. `821BEE68` now selects the event-registered
native LOD parts into frame-local native queues, rather than reading the guest
instance vector and writing temporary links for each part. Each queue is consumed
in reverse insertion order, matching the original group heads. The native group
retains the four material/geometry setup callbacks, with their original guest
return addresses, and establishes retained assets through the first indexed
draw. Subsequent instances reuse those assets directly: they no longer invoke
`821D9600` or the indexed draw hook. Native code updates their bindings, selects
immutable native objects and renders the group through the native renderer.
Each selected object is frozen immediately so later updates cannot change a
pending draw. Selections remain owned until backend submission completes. Native
rendering invalidates the bridge's binding cache; unsupported draws flush pending
native work and restore guest bindings before falling back, preserving order.

World/view/projection and transposed-view matrices are packed from explicit
native state. A group reuses its last immutable material when all non-matrix
constant bytes, textures, samplers, pipeline and blend factor still match.
Only object/camera matrices are refreshed in that case; no reflection scan or
full material-image copy is needed per instance. Weak asset indexes are pruned
periodically and on retirement rather than retaining old material generations.

The first capture of a group can now reuse the material retained by a previous
frame as well. The adapter keeps a bounded weak hint indexed by group address;
`CaptureNativeSceneMaterial` still checks the current backend, pipeline, blend
factor, non-matrix constant bytes, textures and samplers before accepting it.
Camera/object matrices are read afresh. An expired hint returns no material,
and address reuse cannot bypass live binding validation. This avoids repeated
reflection and material-image construction for unchanged groups, while retaining
the current per-group guest activation and first-draw resource checks.

The cross-frame material-reuse run measured **61.3 FPS** in the 199-241 second
diagnostic window, versus 53.275 for the preceding native-queue run. This is a
single development comparison, not a statistically established gain against the
shipping renderer. Its last material counter reused 2,137,442 of 2,140,158
captures (about 99.9%). The queue counter reached 14,000,015 selections with
zero fallback batches; there were no queued draw fallbacks or logged errors.
The gameplay capture was inspected. The path remains opt-in and still depends
on guest visibility and group activation.

Evidence: `out/native-scene-retained-material-build.log`,
`out/native-scene-retained-material-tests.log`,
`out/native-scene-retained-material-process.json`,
`out/native-scene-retained-material-fps.json`,
`out/native-scene-retained-material-summary.json` and
`out/native-scene-retained-material.bmp`. Checks cover weak hint expiry and
replacement as well as the existing changed-binding capture checks. The owned
game was stopped. An earlier short launch used the preceding build after a new
test failed to compile; it was stopped before gameplay and contributes no result.

The feature stays **off by default**. This is a renderer migration stage, not
yet a performance win: guest preparation remains and the adapter currently adds
work. The initial partially supported run measured 35.425 FPS and the first
fully supported run (before material refresh reuse) measured 27.675 FPS over
game seconds 199-241. These are development diagnostics, not a matched comparison
against the shipping path. Both logged zero errors. The first full route reported
zero queued fallbacks and its Mission 1 capture was visually inspected.

The material-refresh version reached **48.8 FPS** in the same diagnostic window.
Its last counter reported 11,889,508 native instances submitted in 1,860,820
native draws, with zero queued fallbacks. The run logged no errors or backend
validation messages, and the Mission 1 capture was visually inspected. This is
still slower than the earlier shipping-path measurements; the feature remains
opt-in while guest preparation is removed. No 120 FPS result is claimed.

Evidence: `out/native-scene-queued-process.json`,
`out/native-scene-queued-full-process.json`, their `*-fps.json` reports, and
`out/native-scene-queued-enabled.bmp` / `out/native-scene-queued-full.bmp`.
Build and checks for material refresh reuse are recorded in
`out/native-scene-queued-refresh-build.log` and
`out/native-scene-queued-refresh-tests.log`. The adapter checks cover independent
equivalent material captures, retained geometry sharing, repeated object identity,
selected-snapshot isolation and rendering after owner retirement on both APIs.
Material-refresh runtime evidence: `out/native-scene-queued-refresh-process.json`,
`out/native-scene-queued-refresh-fps.json`, `out/native-scene-queued-summary.json`
and `out/native-scene-queued-refresh.bmp`. All owned runs were stopped.

The first direct group loop still refreshed bindings/materials per instance and
measured **48.35 FPS**, with no demonstrated improvement over 48.8. Removing
that refresh for complete rigid-world overrides raised the diagnostic result
to **56.35 FPS** (eight intervals, 40 seconds, the same 199-241 window). This is
one development run, not a statistically established gain or a comparison
against the shipping renderer. The last direct counter reported **10,700,000**
instances, all taking the matrix-only path, with zero retries. The queued path
reported zero fallbacks, no errors were logged, and the Mission 1 screenshot
was visually inspected. The feature remains opt-in.

Release build: `out/native-scene-direct-world-build.log`. D3D11/D3D12 checks:
`out/native-scene-direct-world-tests.log`; these additionally cover ordered
overlapping constant writes, dirty-state rejection, invalid/aliased sources,
queue callback/link order, both matrix packing layouts and retained asset use
after the mesh owner is destroyed. The original-body fingerprint audit passes.
Runtime evidence: `out/native-scene-direct-world-process.json`,
`out/native-scene-direct-world-fps.json`, `out/native-scene-direct-summary.json`
and `out/native-scene-direct-world.bmp`. Both direct-path diagnostic processes
were stopped after capture.

Static source registration is now event-driven (details below). Its gameplay
run reached **10,200,000** direct world instances with zero retries, zero queued
fallbacks and no logged errors. The final sampled catalog event had 9,518 live
owners and 26,870 parts after 19,456 registrations. Model-replacement behavior
is also covered by the focused checks. The Mission 1 capture was inspected. The same diagnostic
window measured **55.4 FPS** versus the preceding 56.35: this does not establish
a performance improvement. The change removes render-time source discovery and
correctly retires parts on model replacement. At that stage, transform reads
and visibility selection still came from the guest.

Evidence: `out/native-scene-source-events-build.log`,
`out/native-scene-source-events-tests.log`,
`out/native-scene-source-events-process.json`,
`out/native-scene-source-events-fps.json`,
`out/native-scene-source-events-summary.json` and
`out/native-scene-source-events.bmp`. The owned process was stopped. Checks
cover all active LODs, relocated/removed parts, empty models, invalid model
ranges and owner retirement; construction/model-builder fingerprints are gated.

## Remaining integration

1. Establish audited object creation/destruction identities and publish immutable
   scene snapshots. Preserve previous/current transforms and resource generations.
   A source matrix pointer or a per-frame draw index is not a stable object ID.
2. Retain material state, textures and compatible pipelines. Separate camera,
   world, lighting and animation constants from immutable material constants.
   Preserve shader matrix layout and transparent draw ordering.
3. Migrate static world geometry first. Perform native visibility selection and
   direct instance submission from the published scene. Compare the native output
   with the existing path before making it the gameplay default.
4. Move skinned objects, shadows, effects and overlays into explicit native
   passes. Audit render-helper callbacks for simulation or lifetime side effects
   before removing their execution.
5. Render independently between simulation publications, with camera/object
   interpolation and bounded GPU frame ownership. Retire guest scene traversal
   once migrated content no longer depends on it.

Success requires correct gameplay rendering with the original scene helper
absent from native frame generation, including object removal and level changes.
Repeated presentation of a completed image does not meet that requirement.
Measure CPU and GPU frame time and helper invocation count under the same
gameplay workload; 120 FPS remains a target, not an established result.

## Adapter audit findings

The queued group routine `821D96D8` iterates temporary list nodes at group+4,
reads the instance-parameter pointer at node+0, advances through node+4, and
clears group+4 after submission. These are render-queue observations, not audited
persistent scene identities. The adapter must trace their producers and source
object lifetimes before retaining them across publications.

The static-world path is now traced through a live dispatch sample and the
generated routines. Vtable `82002760` selects `820B2670`; its deleting destructor
is `820B28C0`, which calls `820B2870`. The base constructor `820B33B0` initializes
three 44-byte LOD records at object+408; the destructor destroys those records.
`820B2670` selects a LOD using object+404 and distance thresholds, then tail-calls
`821BEE68`. The latter links **existing** 28-byte instance records into their
material groups. The nodes are not newly allocated per render: their queue
links are temporary, while the enclosing static object establishes lifetime.

`NativeSceneSources` tracks these constructor/destructor generations and maps
the LOD instance-vector records to owner/LOD/part identities. Registration now
runs after `820B33B0` construction and `820B2AC0` model replacement; the
`820B2670` render-time vector scan has been removed. The base constructor calls
the model builder internally, before its lifetime is published, so that nested
load is ignored and the complete initial model is registered after construction.
Any old address generation is retired before entering a reused constructor.
Model replacement re-registers every active LOD and retires previous native
parts, including removed LODs. Empty models retain their owner lifetime with no
parts. Submitted native snapshots keep their old assets until submission ends.
It handles vector relocation, refuses observation without a constructor, and
rejects conflicting owners. The hooks run under `--edf_native_scene_adapter_audit=true`;
the `Native scene ownership` counter reports coverage of queued instance draws.
The audit flag alone does not change drawing. The queued-renderer flag additionally
uses those identities to retain and render native objects.

`CaptureNativeSceneMaterial` captures the bridge's reflected constant images,
retains bound texture generations and backend-owned samplers, and separates
world/view/projection matrices into explicit native bindings. It supports a
combined view-projection matrix when that is all a retail shader exposes.
Unsupported matrix shapes/names (including skinning palettes) fail explicitly.
The scene rendering checks now use this adapter, clear/mutate the original
bindings, and verify that retained materials still draw correctly on both APIs.
`NativeSceneDatabase` is named separately from the existing bridge's render-target
scene structure so the two can coexist during migration.

Evidence for dispatch discovery: `out/native-scene-adapter-audit-process.json`
and its game log; generated source for the named routines and the live vtable
words. Build and adapter checks: `out/native-scene-adapter-build.log`,
`out/native-scene-adapter-test-build.log`, `out/native-scene-adapter-tests.log`.

The ownership run reached Mission 1 and recorded 11,200,000 mapped queued
instance submissions with zero unmapped submissions and zero logged errors.
Live owner counts decreased from 9,938 to 9,844 and registered part counts also
changed, exercising retirement during the run. This establishes coverage for
this observed workload, not all missions or a complete level-unload audit.
Evidence: `out/native-scene-ownership-process.json` and
`out/native-scene-ownership-summary.json`. The owned diagnostic process was
stopped after collecting the result. No performance gain is claimed for these
opt-in ownership hooks.

The native group now replaces the old per-instance loop. Native selections own
their frame-local order and skip guest self/next-link reads. The fallback path
captures the initial head before setup and reads each next link after its draw;
both routes clear the group head at the original boundary. An unsupported LOD
producer first materializes all pending native selections back into guest links,
then disables native queuing for the remainder of that helper call. That preserves
mixed-group order before the unknown producer runs. Untracked producers changing
a native-owned head, or selections left at helper return, fail explicitly.
A pending geometry write or
unsupported instance returns to the full bridge path before reusing assets.
The native shortcut requires all five guest device dirty masks to be zero.
It copies the ordered overrides into the guest vertex constant bank and applies
native bindings. This is the CPU-visible endpoint of the original constant
setter plus packet-free indexed CPU tail: transient vertex dirty bits are
consumed, and other state stays unchanged. Aliasing, empty register ranges,
unaligned devices and nonzero masks retain the original routines. The original
queue, instance setter and register copier have source fingerprint gates.

For a single complete world-matrix override, the group now decodes that matrix
directly into the native object. It does not patch shader bindings, refresh a
material or copy the guest constant bank for each instance. The matrix packing
comes from the captured shader; overlapping parameter mappings and pixel-stage
world matrices exclude this shortcut. The latest encoded matrix is owned by the
group and synchronized to guest/native bindings once before any fallback guest
callback or at group completion. No intermediate guest callback can observe the
omitted stores. General overrides retain the ordered native upload path.

The first instance of each group still uses the full bridge for resource
validation/setup. Native simulation publication, visibility/LOD selection and
the remaining passes must replace these remaining dependencies before frames
can be generated without the guest helper.

Additional source audit: static update `820B2DF8` visits the object's LOD records
and calls `821BEDF0` with the world matrix at object+224. `821BEDF0` passes the
LOD's parameter-storage pointer at +28 to `821BED40`, which transposes the 4x4
matrix into the parameter register stream. This is a candidate simulation-side
transform publication boundary; its cadence and all callers still need auditing
before it replaces per-render observation.

The dirty-update producer is also traced: `821C0D70(object,1)` uses `821A4160`
to link object+120 into its manager's list at +100. `821A4DE8` traverses that
list and calls virtual slot +8, matching static update `820B2DF8`. The existing
`821A4DE8` hook already publishes the model-motion budget after this transition.
This provides a concrete staging/publication boundary to audit next; it does
not yet establish runtime cadence or eager asset coverage for unseen objects.

Static world-register values are now retained at construction/model replacement
and after `820B2DF8` updates. Each active LOD records the exact parameter-storage
address written by `821BEDF0`/`821BED40`. The native source registry publishes an
immutable, transposed register image per owner, shares unchanged images and
rejects references from retired owner generations. Complete world-only native
instances can use that image without reading the guest matrix again; other
parameter addresses retain the previous guest read path. Final guest binding
synchronization still uses owned encoded bytes at the group boundary.

`--edf_native_scene_transform_audit=true` compares retained matrices byte-for-byte
with each selected guest matrix and reports mismatches explicitly (using the
guest value for that audited draw). Normal operation leaves that diagnostic
read disabled. This is event-driven transform ownership, not yet a whole-scene
simulation snapshot or independent gameplay rendering: instance-list selection,
initial group material setup and the remaining passes still come from the guest.

The native queues are scoped to `821A5080` and are restored across nested calls.
Their guest addresses identify only selections within that helper invocation;
persistent object identity continues to use audited owner generations. Group
addresses and LOD membership are captured at construction/model replacement.
`821C07B8` establishes each instance's self pointer and group reference;
`821C3BB8` visits the group list regardless of whether its guest head is empty.
Those bodies and the old link producer are source-fingerprint gated.

The native-queue gameplay run consumed **12,000,017** selections in its last
sampled counter, with zero fallback batches. The retained-transform counter
reached 10,900,000 matrices across 6,478 update callbacks, with both normal guest
matrix reads and diagnostic reads at zero. There were no direct retries, queued
draw fallbacks or logged errors, and the Mission 1 screenshot was inspected.
The 199-241 second diagnostic window measured **53.275 FPS**, which does not
establish a performance improvement over the preceding 55.4 FPS run. The route
remains opt-in; guest visibility and per-frame group setup are still present.

Evidence: `out/native-scene-queues-build.log`, `out/native-scene-queues-tests.log`,
`out/native-scene-queues-process.json`, `out/native-scene-queues-fps.json`,
`out/native-scene-queues-summary.json` and `out/native-scene-queues.bmp`.
The owned game was stopped. Checks cover native queue ordering, independent
groups, no guest-link writes on the native route, exact fallback link restoration,
LOD registration and retirement. The original-body fingerprint audit passes.

The transform audit exercised **5,979** static update callbacks and compared
**10,700,000** retained register images against their guest counterparts, with
**zero mismatches**, zero fallback matrix reads, zero native retries and zero
queued fallbacks. No errors were logged and the Mission 1 capture was visually
inspected. This run intentionally enabled diagnostic matrix reads; it is not a
measurement of the normal path's performance. The default diagnostic flag is
off, and that branch uses the event-published matrix without the comparison read.
The original update and matrix-writer bodies have fingerprint gates.

Evidence: `out/native-scene-transform-events-build.log`,
`out/native-scene-transform-events-tests.log`,
`out/native-scene-transform-events-process.json`,
`out/native-scene-transform-events-summary.json` and
`out/native-scene-transform-events.bmp`. The owned game process was stopped.
Checks cover the exact matrix transpose, storage-address matching, unchanged
value sharing, immutable older values, owner retirement and address reuse.

`820B4250`, currently labeled RenderWorld, also increments object+364 and
advances a float at object+356 before setting effect parameters. Bypassing the
helper wholesale would drop those updates. Their ownership/cadence must be
resolved as part of migration rather than treating every render callback as
side-effect-free. `820B2510` is a deallocation wrapper (counter decrement then
tail call to `821E8CE8`), not proof of a specific scene-object destructor.

*Corrected 2026-09-22: see [renderer-status.md](renderer-status.md).*
