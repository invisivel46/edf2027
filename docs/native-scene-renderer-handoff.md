# Native scene renderer: handoff

Last updated: 2026-09-18. This records the interrupted implementation state, not a declaration of completion. Revalidate the worktree and process state before continuing.

## Goal and current status

The objective is a complete native scene renderer: the game publishes scene state at simulation cadence; native code owns geometry, materials, object state, visibility and LOD; native rendering and interpolation run independently at 120 Hz. Migrate static world first, followed by animated objects, effects, shadows and UI. Remove the original render helper from displayed-frame generation.

**This goal is incomplete.** The original helper still runs every render iteration. Standalone rendering tests, retained draw assets and opt-in preload coverage do not establish an independent gameplay renderer. The user asked for a larger architectural push after successive helper optimizations. Do not reduce the goal to another cache optimization or claim 120 FPS has been reached.

The latest work adds simulation-side material input programs. It builds and passes tests at an earlier revision, but gameplay exposed a register-count validation bug: only **17 of 412** material groups became ready. That bug remains unfixed. Several small edits after the last build also remain unbuilt. The user then requested this handoff; resume from the fix below.

At handoff, the last build had exited successfully and no `edf2027` process was running. The material gameplay process was deliberately stopped before its scheduled screenshot. Check again before building or launching.

## First action: fix global constant extent validation

File: [native_scene_material.h](../src/native_graphics/native_scene_material.h), `ReadNativeSceneMaterialInputs`.

The current code incorrectly applies this check to both local and global parameters:

```cpp
if(bytes%16 || bytes>size_t(parameter.registers)*16)
  throw std::runtime_error("native scene material parameter extent mismatch: "+parameter.name);
```

Gameplay reports extent mismatches for `g_mView` and `g_mViewTranspose`. Xbox shader metadata can declare fewer registers than the compiled native shader requires, while the live global backing vector has sufficient capacity.

Use the existing bridge as the authoritative behavior: `RegisteredShader::ResolveUploads` and `UploadParameters` in [guest_shader_bridge.cpp](../src/native_graphics/guest_shader_bridge.cpp).

- Global groups (`group & 1`): copy native reflection's required bytes. Validate against the live vector's `value.available`, including its existing upper bound. Do not reject against `parameter.registers`.
- Local groups: preserve the ordinary bridge's semantics. It uploads `parameter.registers * 16`; `ShaderBindings::SetGuestFloatRegisters` requires the exact native binding extent. Do not silently truncate or pad a local mismatch.
- Preserve unused-parameter skipping and alignment checks. The preload requirements currently use the maximum of normal/reversed VS requirements; verify compatibility for both variants.
- Add a regression case where the global descriptor register count is smaller than the native requirement but backing capacity is sufficient, plus rejection when backing capacity is insufficient.
- Rebuild, run the native scene rendering tests and hook audit, then rerun gameplay to measure actual material coverage.

Latest sampled failure log:

```text
[2026-09-18 22:52:24.866] Native scene material preload:
groups=412 ready=17 loaded=42619 reused=17 deferred=990660
(owned inputs; pass state still explicit)
```

The counters are cumulative operations, not unique assets. Empty error-level logs do not make this run successful: repeated material deferrals are the failure.

## Verification ledger

All paths below are relative to the repository. Files under `out/` are local artifacts and may not survive a fresh checkout.

| Work | Evidence | Result and limits |
| --- | --- | --- |
| Latest material build | `out/native-scene-material-inputs-build.log` | Exit 0; predates edits listed below. |
| Latest material tests | `out/native-scene-material-inputs-tests.log` | D3D11/D3D12 scene rendering tests passed at that revision. |
| Latest material gameplay | `out/native-scene-material-inputs-process.json` | PID 27128 stopped early; only 17/412 material groups ready. No completed Mission 1 screenshot or FPS result. |
| Geometry preload | `out/native-scene-geometry-preload-{build.log,tests.log,process.json,summary.json,fps.json,bmp}` | 412/412 groups ready, zero deferrals; Mission 1 image visually correct. PID 43988 stopped. 55.15 FPS for game seconds 199–241. |
| Unseen-part population | `out/native-scene-group-assets-final-*` | 44,497 records created before their own first draw across scene lifetimes; examined 146,363, rejected 0; 23,289 live objects last sample. 56.75 FPS. PID 47980 stopped. |
| Membership audit | `out/native-scene-membership-*` | 683,404 lists and 31,619,986 node visits, zero membership mismatches; 11,438,821 culling checks, zero mismatches. Owned processes stopped. |

Exact failed material log:
`out/native-bridge-run/binding-validation-20260918-224925-ec212133/game.log`.

Geometry preload's first publication was `ready=412 loaded=412 reused=0 deferred=0`; last sampled publication was `ready=412 loaded=14293 reused=2694607 deferred=0`. Last sample had 24,381 native objects and 45,603 group-created records before draw. There were no reported untracked writes, direct retries, queue fallbacks or logged errors in that verified run.

Do not claim an improvement from 56.75 to 55.15 FPS or compare unmatched builds/scenes as a controlled benchmark. Earlier default-path measurements were around 80 FPS; opt-in scene paths have mostly been around 50–62 FPS. None demonstrate the final target.

### Edits after the latest successful material build/tests

These require rebuilding; the existing binary does not validate the current source:

- `global` tags on owned constant/texture inputs, their population, and test assertions.
- Backend identity check when reusing a material program.
- Preload cvar description updated to mention material inputs.
- Comments and documentation.
- Texture binder `8213BA98` fingerprint added to the hook audit; rerun that audit.

## Architecture map

| File under `src/native_graphics/` | Responsibility |
| --- | --- |
| `native_scene.h/.cpp` | Lifetime-safe database, immutable scene snapshots, renderer, interpolation, culling and instancing. |
| `native_scene_bindings.h/.cpp` | Reflected material capture; separates world/camera matrices from immutable material bytes. |
| `native_scene_adapter.h/.cpp` | Owner/generation/LOD/part mapping, asset interning, source transforms and publication. |
| `native_scene_sources.h` | Constructor/model/destructor identities, LOD parts, worlds, bounds and group membership/revisions. |
| `native_scene_geometry.h` | Static group geometry descriptor reading. |
| `native_scene_material.h` | Owned material input decoding and independent binding reconstruction. |
| `native_queued_scene.h` | Frame-local native queues and fallback restoration of guest links. |
| `native_scene_membership.h` | Event-owned spatial-list membership and immutable ordered snapshots. |
| `native_scene_visibility.h`, `native_scene_cpu_window.h` | Audited visibility behavior and short-lived page-validation reuse. |
| `guest_shader_bridge.cpp` | Gameplay hooks, preloading, native/guest integration. |

Tests live in [native_scene_tests.cpp](../tests/native_scene_tests.cpp). The longer implementation history is in [native-scene-renderer.md](native-scene-renderer.md); early sections describe historical intermediate states, not necessarily the current integration.

### Geometry and object population already implemented

Simulation publication after `821A4DE8` loads all registered static groups without invoking guest setters, material activation, instance callbacks or draw callbacks. Only registered physical `NativeModelBuffers` resources are accepted. Guarded `NativeBufferWrites` snapshots and `CommitObservedGeometry` retain immutable native generations; there is no unguarded payload fallback. Geometry is interned in the same pool used by later visible draw capture.

Reuse validates descriptor fields, group revision, shader bytecode, declaration identity, buffer generations and lifetime/revision. Cache hits do not depend on the global writer epoch; scheduled comparisons use `CopyObservedSet`. `NativeSceneCpuWindow` caches permission validation briefly, never guest payload contents.

`NativeScenePublication::group_geometry` owns group assets independently of the instance snapshot. Failed loads remove the current asset and retry later; old publications remain immutable.

`PopulateGroup` can create eligible unseen parts using a group's captured geometry/material and simulation-published worlds. It requires one vertex-stage world matrix and exact world-only overrides. It does not add parts to the current visible queue or write guest constants. Its cache keys include group revision and geometry/material identity. Owner retirement invalidates only affected groups; do not restore broad invalidation, which previously caused millions of unnecessary examinations. Entirely unseen groups still need material resolution before they can become fully renderable.

### Material programs currently implemented

`NativeSceneMaterialInputs` owns shader identities, named constant byte streams, texture handles/slots/settings, optional LOD range and ordered state override records. Constant bytes remain in guest big-endian float4 format. Global/local tags identify their origin.

`NativeSceneMaterialProgram` owns the backend, shader prototypes/bytecode/reflection and exact native texture generations. Its `Capture` method creates independent CPU `ShaderBindings`, validates linking and reconstructs constants/resources.

**Capture does not resolve or execute state overrides or inherited sampler state.** Its caller must supply an already resolved pipeline, blend factor and samplers based on explicit pass state. A material program is not yet a complete gameplay material/pipeline.

The adapter publishes `NativeSceneGroupMaterial { group, revision, program }` through `group_materials`; it supports publish/lookup/retire. `PruneGroupGeometry` now prunes material programs too despite its name.

`PreloadStaticSceneMaterialsLocked` runs after geometry preload at simulation publication. It uses the loaded group's material address, published parameter schema and native shader reflection. Textures must be registered, content-valid and have a native handle. Reuse compares owned inputs, texture generations, shader bytecode identities and now backend identity. Failure retires the current program and retries next publication. Logging occurs every 120 ticks.

Tests already cover local/global indirection, unused unreadable parameters, settings/order/LOD/state copying, source immutability, retained old snapshots, capacity/slot/state-count rejection, and reconstruction/rendering after original bindings/resources change. They did not cover the descriptor-count/global-capacity distinction that failed in gameplay.

## Remaining architectural work

`821A5080` still calls `__imp__sub_821A5080` every displayed iteration. Scene queues/publication are scoped around that helper. `821D96D8` still performs guest stream/declaration/index setup and material activation. The first visible draw captures the resolved material/pipeline; subsequent eligible world-only instances reuse native assets/transforms. Selection can still mix a publication with current adapter records.

There is no complete independent static-world pass or independent frame scheduler. Do not render the entire database snapshot blindly: it includes different LOD parts and potentially different passes. Preserve pass membership/order, camera state, hidden flags, duplicate marks, LOD and render state.

Recommended next milestones after repairing material coverage:

1. Resolve material state overrides and samplers over explicit pass state. Avoid treating the device state left by a previous draw as the material definition.
2. Build resolved native group materials/pipelines and compare them with normal visible capture.
3. Publish complete owner/LOD/pass membership and assets in a selectable scene snapshot.
4. Render one complete static-world pass while bypassing its original traversal, setup and submission.
5. Establish independent frame scheduling/interpolation, then migrate animated objects, effects, shadows and UI.

`820B4250` (RenderWorld) increments object+364 and advances float+356 before effect parameters. The whole helper is not side-effect-free; move/preserve simulation effects before bypassing it.

## Audited data layouts and traps

### Static group geometry

- Descriptor = `Word(group + 12)`.
- Vertex resource is embedded at descriptor+4; index resource at descriptor+84.
- Declaration container = `Word(descriptor+72)`; selected node = `Word(descriptor+76)`. Reject missing values or node equal to `Word(container+4)`; declaration = `Word(node+28)`.
- Stride = `Word(descriptor+60)`; signed index count = `Word(descriptor+140)`, positive and truncated to complete triangles.
- Material = `Word(Word(descriptor)+16)`; pass = `Word(material+108)`; vertex shader = `Word(Word(pass))`.
- **`82149A90` is the declaration setter; `821375C0` is the index setter; `82137410` is the stream setter.** Earlier notes mixed the first two roles.

### Material activation and inherited state

Audit `821B8E48` in `generated/default/edf2017_recomp.38.cpp` (around line 8303; use `rg`, line numbers can move).

- Pass = `Word(material+108)`. VS = `Word(Word(pass))`; PS = `Word(Word(pass+4)+4)`.
- Schema groups: VS locals, VS globals, PS locals, PS globals.
- Local constant data starts at record+4; available count is metadata `registers`.
- Global constant storage follows `Word(record)` to a vector; data = `Word(vector)`, capacity = `Word(vector+8)`.
- Local textures are 28-byte records: name, handle, slot, bias bits, mip, min, mag.
- Global textures are `[node, slot]`; node+28 is handle, +32/+36/+40/+44 are bias/mip/min/mag.
- Texture LOD range is header+44 masked by `0x3fc`; null binding preserves inherited range.
- Material state vector: base `Word(material+96)`, count `Word(material+104)`, ordered 8-byte `[offset,value]` records. Guest activation looks up callback `Word(device+56+offset)`. Retained records must be decoded into native semantics, not executed as guest callbacks.

Sampler words are at device+1024+slot*24, offsets 0,12,16,20. Canonical masks in `native_sampler_decode.cpp`: `0x7fc00` addressing; `0xfff80000` filter/aniso; `0xfffffffc` LOD/bias; `0x1ff` fourth word.

Texture binder `8213BA98` (file 36, around 4545) preserves addressing and other inherited fields, inserts texture LOD bits and clamps against device byte+11678 per slot. Mag setter `82136888` (file 4, around 4389) and min setter `82136700` (file 33, around 4509) have alias/aniso behavior involving byte11652+slot and a lookup table; this is not fully decoded. Mip inserts `(mip<<23)&0x1800000` in sampler word+12. Bias setter `82136C20` (file 35, around 4434) scales/truncates and inserts 10 bits at word16 bits12–21; verify its scale constant rather than assuming 32.

`NativeRenderStateProducerFields` in `native_render_state_snapshot.h` identifies audited setters for the six native packed render fields.

### Visibility/membership work and remaining tree migration

Native membership is event-owned, preserves list order/endpoints and publishes changed lists after simulation. Hooks: ctor `821C4EB8`, copy `821C5D28`, insertion `821A1628`, removal `821A1678`. Native leaf `820B4038` uses snapshots until an unknown dispatcher; then it discards the snapshot and follows live links. Invalidate permission windows before guest callbacks.

Visibility preserves audited PPC rounding, sphere/OBB behavior, LOD, bounds, hidden and duplicate semantics, but guest spatial-tree traversal and unknown callbacks remain. Source lifecycle hooks include `820B33B0`, `820B2AC0`, `820B2870`, `820B2DF8`, `821BEF10`, `821C0B88`, and publication `821A4DE8`.

For the next tree audit: manager+48 level vector, begin+52/end+56, 32-byte records. Nodes are 144 bytes: center+32, half-extents+48, radius+64, eight children+84..112, occupancy+116, list header+120/end+132. Relevant functions: root walk `821C61D8`, recursive cull `821C5FC8`, accepted subtree `821C56C0`, manager construction/rebuild `821C7740`, spatial update `821C5730`, bounds recompute `821C5488`, hierarchy initializer `821C4F80`, AABB classifier `821C3178` using transform `821B0258`. Preserve debug manager+108 behavior calling vtable+24 or fall back.

**`821C75D0` is a level-vector resize helper, not a manager destructor.** Actual manager lifetimes/writers still need auditing. Nonhierarchy lists at world+372 and overlay+48 are not all tracked.

## Build, test and gameplay workflow

Workspace: `D:\roms2\edf2027`, Windows PowerShell. Preserve the extensive dirty worktree and untracked source files. Do not reset, clean or overwrite unrelated work. Generated guest code must not be edited. No applicable `AGENTS.md` was found in this session; recheck when resuming. This session had restricted writes and approval policy `never`; do not request sandbox escalation. Do not spawn agents unless user or applicable instructions authorize them.

Do not relink the executable while a game is running. Build helper `out/build-native-scene.cmd` initializes Visual Studio 18 Community and builds `edf2027` and `edf_native_scene_tests` with parallelism 2:

```powershell
cmd /c out\build-native-scene.cmd > out/UNIQUE-build.log 2>&1
```

Wait for exit 0 before testing or launching; otherwise stale binaries can mislead. If the local helper is missing, initialize `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat` in a cmd environment, then run:

```text
cmake --build out/build/win-amd64-release --target edf2027 edf_native_scene_tests --parallel 2
```

```powershell
& out/build/win-amd64-release/src/native_graphics/edf_native_scene_tests.exe > out/UNIQUE-tests.log 2>&1
$sceneExit = $LASTEXITCODE
Get-Content out/UNIQUE-tests.log -Tail 3
exit $sceneExit
```

Run the hook audit and targeted whitespace checks. `git diff --check` does not cover untracked files.

```powershell
cmake -DSOURCE=D:/roms2/edf2027/src/native_graphics/guest_shader_bridge.cpp -P tools/audit-native-hook-boundaries.cmake
```

Gameplay launch:

```powershell
$run = & tools/start-native-binding-validation.ps1 `
  -InputScript out/renderer-performance-input.txt `
  -ExtraArgs @('--edf_native_unlock_framerate=true','--edf_fps_cap=120',
    '--edf_native_scene_queued=true','--edf_native_scene_preload=true',
    '--edf_native_host_capture=D:/roms2/edf2027/out/UNIQUE.bmp',
    '--edf_native_host_capture_after_ms=230000')
$run | ConvertTo-Json | Set-Content out/UNIQUE-process.json
```

Use a unique artifact prefix. The script creates a fresh userdata/run folder. Assets are at `D:/roms2/edf3-translation-project/work/x360`. Typical timing: startup 130–145 seconds, menu selection 150–190, Mission 1 from about 199. Inspect the 230-second image, then stop the owned game around 250 seconds. Keep individual waits short enough to report progress. Logs rotate: collect `game*.log`, extract strings and sort timestamps before choosing the last sample.

To stop an owned run safely, read its JSON, look up the actual PID, verify executable path, then kill/wait. Saved metadata alone does not prove a process is alive:

```powershell
$ownedRun = Get-Content out/UNIQUE-process.json -Raw | ConvertFrom-Json
$ownedProcess = Get-Process -Id $ownedRun.Id -ErrorAction SilentlyContinue
if ($ownedProcess) {
  if ($ownedProcess.Path -ne $ownedRun.Executable) { throw 'Process identity mismatch' }
  $ownedProcess.Kill()
  $ownedProcess.WaitForExit()
}
```

```powershell
python tools/report-native-geometry-runs.py $run.Log --start 199 --end 241 --output out/UNIQUE-fps.json
```

Do not use audit-heavy or incomplete runs for performance conclusions. `edf_native_scene_queued`, `edf_native_scene_preload`, `edf_native_scene_visibility` and `edf_native_scene_visibility_audit` default to false. Visibility remains opt-in because the mixed path was slower; do not enable it in a preload comparison without explicitly accounting for that change.

Hook fingerprints normalize CRLF to LF, slice from `DEFINE_REX_FUNC(sub_ADDRESS) {` to before the next `\nDEFINE_REX_FUNC(`, then SHA256. Relevant entries:

- `821B8E48`, file 38: `1382e9cfc357408d69ec2b59776db047cc256215fc6977d8d371b6e8cd1ebf36`.
- `8213BA98`, file 36: `8f11248594563e3bca0be3d3ca72618b4b2997b6b6265ceda6a574684a1fcb74` (added after last material build/audit).

## Resume checklist

1. Read this handoff and inspect current diffs; preserve all existing work.
2. Recheck process state; fix global constant capacity handling and add its regression case.
3. Build the current source, run scene tests and hook audit.
4. Run gameplay and report material coverage honestly, including deferrals and screenshots.
5. Continue toward an independently submitted static-world pass, then independent frame cadence. Asset readiness is a prerequisite, not completion.
