# Native renderer migration

Initial investigation: 2026-09-08. This document records migration history;
early sections describe intermediate states, not the current default runtime.
The Windows build now uses native D3D11 without loading Xenos. See
[native-gpu-boundary.md](native-gpu-boundary.md) for later CPU/GPU separation,
live validation and remaining startup/lifecycle work. Complete replacement,
full-game fidelity and handheld performance are not yet verified.

Confirmed scope: retain recompiled gameplay; target Windows handheld PCs first.
The initial native backend is D3D11. Linux/Android are not acceptance targets for
this first backend.

## Implemented native shader foundation

`src/native_graphics` now provides an SDK-independent SGSL/DXSL reader and a
D3D11 source compiler. It retains shader bytecode, native shader objects, and
reflection for future named-parameter bindings and vertex input layouts. It
does not translate Xbox microcode. Source files are read from the user's dump;
none are embedded or copied into this repository.

The checker compiled and created all **130 shader entries from 44 effects and
90 techniques** on a hardware D3D11 device, with zero failures. Compilation uses
Shader Model 5 with legacy HLSL compatibility. The retail `../Common.fx` include
is explicitly resolved to the compressed `Shader/Common.fx` shipped on disc.
No source-text rewrites were necessary for this compilation test.

Build from a Visual Studio x64 developer shell without a ReXGlue installation:

```powershell
cmake -S . -B out/build/native-graphics -G Ninja -DCMAKE_BUILD_TYPE=Release -DEDF2027_NATIVE_GRAPHICS_ONLY=ON
cmake --build out/build/native-graphics
ctest --test-dir out/build/native-graphics --output-on-failure
out/build/native-graphics/src/native_graphics/edf_native_shader_check.exe C:/path/to/extracted/game
```

The dependency-free parser tests cover record-relative references, separate
blend/depth states, truncated inputs, allocation limits, missing shader entries,
and zero-initialized/overlapping SGSL ring copies.

`ShaderBindings` now creates native constant buffers from reflection, updates
named values with exact layout checks, binds texture/sampler slots for either
shader stage, and clears explicitly unset resources. All 130 disc entries also
pass construction of these bindings. Resource arrays and unknown resource types
currently produce explicit errors rather than guessed bindings.

`edf_native_draw_test` executes a native D3D11 draw and reads every pixel back.
It verifies vertex/pixel constants at non-default slots, texture/sampler slots,
two successive constant updates, and clearing stale bindings. This deterministic
test uses Windows WARP; the disc shader checker uses a hardware D3D11 device.
Neither is yet a replay of a guest draw or a gameplay rendering test.

`ParseGuestEffect` handles the endian-converted buffer consumed by the guest
shader loader. `sub_821B6C10` calls `sub_821D90C8` before `sub_821B6880`; that
conversion swaps numeric records while retaining byte-string source/name data.
Passing the guest buffer through the disc parser would therefore be incorrect.
The separate parser is tested against a converted synthetic effect.

## Native texture import

`CreateNativeDdsTexture` imports classic DDS resources directly into D3D11:
BC1/BC2/BC3 remain compressed, with all supplied mip levels and all six cube
faces retained. Packed RGB formats are decoded by their channel masks to RGBA8.
Dimensions, payload bounds, masks and cube completeness are validated. DX10 DDS,
volume textures and unknown formats are rejected explicitly. Raw top-level row
padding follows DDSD_PITCH; subsequent raw mips currently assume tight packing.

`edf_native_texture_tests` reads back every face/mip of synthetic BC1/BC2/BC3
cubes byte-for-byte and checks malformed inputs. The draw test samples a native
texture imported from a synthetic RGBA DDS. All three native tests pass.

The development bridge originally snapshotted DDS bytes at `sub_821B43D8` and
`sub_821B4210`, associating native resources with successful guest handles. The
bounded startup capture `out/native-bridge-run/textures.log` imported 119 textures
without reported import errors, while recording 20,000 shader activations and
32,060 named parameter uploads with no handle misses or parameter errors.
This is resource-import coverage, not proof of equivalent rendered frames.

Remaining texture work includes native sampler state, complete resource coverage,
resource release, dynamic/render-target/movie textures, and the extended image
loader's requested resize, format, filtering and mip-generation semantics.
The current bridge imports the original DDS contents; it does not yet reproduce
those extended transformations. It remains opt-in and calls the reference
loader, so these native resources do not yet replace guest texture rendering.

### Material texture bindings

The bridge now reads the local 28-byte records at instance+72 and global 8-byte
records at instance+84 during `sub_821B8E48`, setting native pixel-shader resource
views by name. `sub_821BA120` constructs these vectors from the guest pixel
shader constant table. The global lookup `sub_821A2178` returns tree node+40;
`sub_821A1F78` establishes the string key at node+12 with the engine's inline/
heap string layout. Global values supply the current texture handle at +28.
This path clears previous native material textures before processing a new set.
Absent native resources are explicitly unbound and counted, never replaced with
an unrelated texture. Unsupported name/layout matches are reported separately.

`edf_native_shader_check <game directory> --bindings` exposes the actual native
reflection. For example, `DiffuseNormalCubeLighting:PS_Main` puts its diffuse0
sampler at s0 but diffuse0 texture at t1, and normal sampler at s2 but normal
texture at t0. Guest indices cannot be reused for these native resource slots.
The draw test now also verifies legacy combined sampler names resolve to both
native resource types and that bulk material texture clearing unbinds resources.

The initial live binding capture reveals startup texture handles not created
through the two DDS loader hooks; these are counted as missing native resources
in `out/native-bridge-run/texture-bindings.log`. Closing those creation paths is
required before native output can replace the reference renderer. Sampler state
is not yet uploaded by this bridge; texture binding alone is insufficient for
native sampled draws. Global name decoding also needs broader gameplay coverage.
At 10,000 activations the bounded capture reported 2,962 resolved-or-null texture
bindings, 9,047 missing-resource uses (not unique textures), zero texture-binding
errors, and 15,474 constant uploads with zero parameter errors. It advanced past
60 seconds and was stopped after capture. No native frame-equivalence or handheld
performance claim follows from this diagnostic run.

### Shared loader, lifetime, and missing-resource classification

The current snapshot boundary is the shared image loader `sub_82201458`, which
both engine wrappers reach. Its source pointer/length are r4/r5; the output
pointer is at entry SP+148 (SP+1668 after its 1520-byte frame). It returns an
HRESULT, not the engine wrappers' Boolean. Moving the hook avoids duplicate
imports while covering direct D3DX callers. Original image transformations remain
unimplemented in the native importer, as noted above.

`sub_821347C0` decrements the resource reference count and calls `sub_82134220`
at zero. The latter frees storage; the bridge now erases native mappings before
that function executes. The bounded `shared-loader.log` capture verified imports
and observed four imported texture destructions without reported import errors.
Shader bindings can retain COM references until their next material update,
matching ordinary D3D11 binding ownership rather than leaving reusable guest
addresses registered indefinitely.

`sub_8213B730` now records allocation dimensions, mip count, usage, format and
caller against each returned handle, removing any previous native image on
address reuse. These records are diagnostic CPU metadata, not GPU emulation or
replacement image contents. The `allocations.log` capture identifies the first
missing resources as the actual post-processing targets: 1280x720 from
`sub_8219E3B8`, then 640x360, 320x180, 160x90, 80x45 and 40x22 from
`sub_821B8C30`, format `0x1a22ab60`. The following 16x16 target uses
`0x2da2ab5e`. This agrees with research note 0714's two reduction pyramids.
Thus broadening DDS imports alone cannot close the missing bindings: native
render-target allocation, content-producing passes and resolves must replace
these resources. Allocating empty images would only conceal the missing work.

## Native render-target path

`NativeRenderTarget` now owns a D3D11 render surface and a separate sampled
texture. The explicit resolve copies single-sampled surface contents; changing
the surface alone does not overwrite the previous resolved texture. HDR tests
read every pixel of RGBA16F outputs, including values above 1 and negative values,
verify retained contents before the next resolve, and check unwritten targets
remain invalid. Native formats currently accepted by this host helper are
RGBA16F, R16F and RGBA8; this is not a blanket guest-format mapping.

The live bridge connects the engine constructor `sub_821B8C30` and begin/end
boundaries `sub_821B8828`/`sub_821B88B0`. The constructor's texture handle is at
owner+4, surface handle at +12, texture/surface formats at +16/+20 and dimensions
at +24/+28. The verified scene-reduction pair `0x1a22ab60`/`0x1a2201bf` currently
gets a host RGBA16F target. Other format pairs remain unsupported pending their
component swizzle and resolve-conversion handling; guest eDRAM precision and
conversion equivalence are not established by choosing this host format.

Begin/end tracking binds native targets, sets their viewport, restores the
previous tracked target and performs the explicit resolve only when contents
have been produced natively. Resource destruction drops target registrations.
Texture binding checks content validity as well as allocation, preventing an
empty native target from counting as a resolved material resource.

The bounded `out/native-bridge-run/render-targets.log` capture created all five
scene-reduction targets (640x360 through 40x22) and reached 3,000 target scopes
without reported begin/end errors. It correctly reported zero valid native
resolves and 1,155 unwritten supported targets at that checkpoint. **Guest draw
submission is not connected yet**, so this is verified resource/scope integration,
not a native post-processing frame. The reference renderer still supplies output.

Next execution boundary: `sub_820B01E8` begins a target, sets named parameters,
selects the shader with `sub_821B94E8`, calls `sub_821A79B8`, then ends the target.
The latter sets the guest vertex declaration and calls `sub_821FD8F8` with stride
16; the post pass uses primitive 13 (quad list), one quad. Native vertex input,
quad topology conversion, sampler states and producing the source scene remain
necessary. The full-screen color-clear wrapper is `sub_821340D0`, which converts
packed color and calls `sub_82133D20`; it is not yet forwarded to native targets.

## Native immediate quad submission

`QuadStream` now converts the immediate post quad's big-endian float stream to
native vertices, expands each quad to triangles (0,1,2 and 0,2,3), and submits a
D3D11 draw. Input layouts are cached with the owning native vertex shader, and
dynamic vertex buffers are reused/grown. This adapter deliberately supports only
the recovered POSITION0 float2 / TEXCOORD0 float2, 16-byte stream; it is not a
general world-geometry implementation.

The live declaration at `sub_821FD8F8` was recovered from device+11536. Its element
count is declaration+24 and its copied 12-byte element records begin at +52
(`sub_82149AB0` establishes those offsets). The observed post declaration is
`[0,0x2c23a5,0]`, `[8,0x2c23a5,0x50000]`. The bridge checks those complete records,
primitive 13, stride 16 and bounded vertex count before submitting; incompatible
draws produce explicit errors rather than interpreting arbitrary geometry as quads.

`edf_native_quad_tests` reads every pixel after guest-vertex conversion, draw and
resolve. It verifies full quad coverage, interpolated UV values, changed data on
buffer reuse, buffer growth for multiple quads, and rejected truncated input.
An initially mismatched synthetic VS/PS interface produced incorrect interpolants
despite successful shader compilation. The test now uses a matching varying
structure, and `ValidateNativeShaderLink` explicitly checks used PS inputs against
VS output semantic/index, register, type and component mask. A negative test
preserves that regression. Live shader activation checks each selected pair before
allowing quad submission; compilation success alone is not treated as linkage proof.

The bounded `native-quads.log` capture submitted 3,000 native quad draws without
reported submission errors. The subsequent `linked-quads.log` capture includes
the linkage guard. These are diagnostic native submissions, **not valid game
frames**: their sampled source images, sampler states and render states are not
all native yet. Accordingly the bridge leaves their contents invalid and does
not expose them as successful resolves. Game presentation still uses Xenos.

## Live guest shader bridge

The Windows executable now includes `guest_shader_bridge.cpp`, enabled with
`--edf_native_shader_bridge true` for reference runs. It snapshots HLSL and
entry metadata at `sub_821B6880`, then associates compiled native shader/binding
objects with the guest handles in the loader's 72-byte result records. Reads
check address arithmetic and mapped readable memory; input strings are bounded.
Shader activation at `sub_821B8E48` resolves those same handles through the native
registry. Reload replaces the owner, and the shader-array teardown boundary
`sub_820AB2F8` removes that owner's native resources.

The first bounded startup run registered 22 live shaders and resolved 20,000
observed activations with zero missing shader handles. Log:
`out/native-bridge-run/bridge.log`. The run advanced frame statistics through
30 seconds and was stopped after the capture. This was a live recompiled-game
reference run, not the standalone shader checker.

The bridge currently constructs and tracks native resources while calling the
original guest functions. It does not yet replace the render output. Textures,
draw submission, and synchronization still use the reference path.

### Live parameter uploads

`sub_821BBAD8` builds a 36-byte parameter group per shader stage. The instance's
vertex group begins at +0 and pixel group at +36. Each group retains named
local records at +12 and named global records at +24 (both 12-byte vector
headers, 16-byte records). Local records are `[name, data, register_count,
register_index]`; globals are `[value_vector, name, register_count,
register_index]`. `sub_821B8E48` obtains current global data through
`value_vector+0`. These retained names allow the bridge to read live values
without treating guest register indices as native buffer offsets.

The bridge now uploads both groups to native constant buffers and binds native
vertex/pixel shaders. `SetGuestFloatRegisters` reverses each float's bytes and
repacks scalar/vector arrays and row/column-major matrices using native shader
reflection. Unknown optimized-out names are counted separately; register-count
and type mismatches are reported rather than truncated or silently padded.

The bounded run in `out/native-bridge-run/parameters.log` reached 20,000 shader
activations and 32,017 native named uploads with zero handle misses, optimized-out
names, or parameter errors. It remained on the reference rendering path. That
development run dropped to about 12 FPS while doing both paths; it is not a
handheld performance result. Its process was stopped after capture.

The native draw/readback test now feeds big-endian guest registers and verifies
scalar/vector values, padded scalar/vector arrays, and nonsquare matrices in
both row- and column-major layout. This tests the conversion against explicit
pixel values, not merely successful D3D calls. Full guest frame equivalence and
coverage outside the startup route remain unverified.

This validates source ingestion and native shader creation only. It does not
validate guest shader parameter bindings, guest geometry/pixels, frame timing,
handheld performance, or gameplay without Xenos. The game executable still uses
its existing graphics path. The next integration work is the guest resource and
named-parameter adapter, native draw submission, and presentation/synchronization.

## Current port

The executable runs the recompiled PowerPC game in `generated/default`.
`CMakeLists.txt` selects `GPU_PLUGINS xenos` and packages `rexgpu-xenos`;
`src/edf2017_app.h::OnPreSetup` also unconditionally selects `xenos`.
`src/input_hooks.cpp` already wraps `sub_82151460` for frame pacing and statistics.
Any graphics migration must preserve that hook or explicitly integrate with it.

Removing those build/startup selections alone does not provide native rendering.
The guest still creates graphics resources and writes Xbox GPU commands. The
existing GPU implementation also supplies completion and vblank services.

## What the research provides

The sibling `../edf2027-analysis/edf2017-native` contains a separate native game
implementation. Its `src/platform/window.cpp` implements D3D11 presentation;
`src/game/mission_renderer.cpp` prepares world batches, while `src/ui/renderer.cpp`
implements UI composition. Asset readers live under `src/assets` and `src/ui`.

These are useful source implementations, but they do not currently accept this
port's live guest-memory objects. `MissionRenderer` uses the native rewrite's
`MissionWorld`, class registry, resources, and object lifetimes. Importing that
whole application would also replace gameplay, scripting, input, and audio.
It would be a different migration from replacing this port's GPU backend.

Research references, relative to `../edf2027-analysis`:

| Contract | Evidence | Reusable implementation |
| --- | --- | --- |
| Mesh subset declarations, vertex/index spans | `notes/render/0331-the-declaration-located-instead-of-guessed.md` | `edf2017-native/src/assets/dxm.cpp` |
| Material textures and selected effects | `notes/render/0702-dxm-materials-carry-exact-texture-bindings.md` | `edf2017-native/src/assets/dxm.cpp`, `src/game/mission_renderer.cpp` |
| Independent blend, cull, and depth-write state | `notes/render/0703-dxsl-owns-blend-depth-and-cull-state.md` | `edf2017-native/src/assets/dxsl.cpp`, `src/platform/window.cpp` |
| Animated world geometry | `notes/render/0715-live-objects-now-reach-the-recovered-envelope-ve.md` | `edf2017-native/src/game/mission_renderer.cpp`, `src/platform/window.cpp` |
| Post-processing resource graph | `notes/render/0714-the-post-chain-has-two-reduction-pyramids.md` | `edf2017-native/src/platform/window.cpp` |
| Hardware XUI rendering | `notes/render/0520-the-10-fps-renderer-was-an-offline-compositor-in.md` | `edf2017-native/src/ui/renderer.cpp`, `src/platform/window.cpp` |

Use the detailed notes and current source together. Some overview documents are
older than the implementation. In particular, note 0714 supersedes 0713: the
post-processing graph has two reduction pyramids, not just the five scene
reductions. `reports/subsystems/render.csv` records per-function evidence and
remaining partial implementations; a verified native behavior does not by itself
verify a guest-memory adapter.

## Recommended boundary if retaining recompiled gameplay

Intercept the game's graphics layer before it emits Xbox GPU commands, preserve
guest object identities with host resource handles, and submit native draw calls.
Keep simulation, scripts, transforms, animation updates, and resource ownership
in the recompiled game. Host rendering consumes the state produced by those
systems.

The following addresses are investigation anchors, not verified replacement
signatures. Recover parameters, return values, guest writes, and destruction
paths before overriding them.

| Guest boundary | Evidence / responsibility |
| --- | --- |
| `sub_821B3C98`, `sub_821B2FA8` | Mesh loader and per-subset declaration conversion; note 0331 |
| `sub_821B4210`, `sub_821B43D8`, `sub_821B5888`, `sub_821B59F0` | Texture initialization names in `names.tsv`; lifecycle and argument contracts still need tracing |
| `sub_821D7C18` | Material selection and `_Blend` technique suffix; note 0703 |
| `sub_821B62B0` | Selected DXSL pass state records; note 0703 |
| `sub_8219CE18`, `sub_8219CF20` | Render/sampler state table application; note 0196 |
| `sub_821A7270`, `sub_821A78B8`, `sub_821A73F8` | 2D begin/primitive/end path; note 0196, with later texture research needed |
| `sub_820B1028`, `sub_820B09B0`, `sub_820B04B8`, `sub_820B0B80` | Post-processing allocation, reductions, blur, and frame pass order; notes 0713/0714 |
| `sub_82151460` | Existing port frame-pacing hook around the guest swap wrapper |

The adapter must cover other draw producers too, including XUI, movie frames,
particles, HUD, and render-to-texture passes. A working map preview is insufficient
to switch the executable over.

## Native sampler state

`DecodeNativeSampler` converts a CPU-side sampler snapshot into a D3D11
descriptor. It supports wrap, mirror, clamp-to-edge and mirror-once,
point/linear min/mag/mip filtering, base-only mip selection, anisotropy through
16x, mip range and signed 5-fractional-bit LOD bias. Halfway/border addressing,
unresolved filtering modes and special gradient/filter adjustments are rejected
explicitly. The cache key excludes resource address and non-sampler fields.

The bridge observes `sub_821B8E48` **after** the original activation now, retaining
its instance/device arguments. This matters because the activation writes the
named filters and `sub_8213BA98` applies the bound texture's mip limits. The
snapshot comes from CPU D3D9 device state at +1024+24*guest_slot, words 0/3/4/5;
inline engine address-mode writes are therefore included. This transitional
adapter reads only state fields and creates native samplers; it does not submit
or interpret GPU commands. It still depends on original CPU setters to maintain
this state, which must be retained/replaced appropriately when bypassing the
original device and command-generation path.

Local/global material records supply the guest sampler slot, but native texture
and sampler slots are independently resolved by shader name. Sampler objects
are cached, rebound on material activation and cleared alongside textures on
incomplete binding sets. Texture addresses are not sampler identity.

Native draw tests sample a two-level DDS and read pixels to verify wrap, clamp,
mirror, mirror-once, linear filtering, authored mip selection and maximum-LOD
clamping. Descriptor checks cover eight basic filter combinations, signed bias,
anisotropy, unsupported address modes and cache masks. Bulk sampler clearing is
checked against bound D3D11 state.

The bounded `out/native-bridge-run/samplers.log` capture created six sampler
states (point/linear, clamp U/V, wrap W, differing mip limits), reached 10,000
shader activations with zero reported texture/sampler binding errors, and passed
3,000 native quad submissions. Source scene and render states remain incomplete;
diagnostic target contents stay invalid and displayed frames still use Xenos.

## Native blend/depth/raster state

`NativeRenderState` now creates and binds cached D3D11 blend, depth/stencil and
rasterizer objects for the immediate quad path. The bridge snapshots the applied
CPU device values at +10424 (blend), +10420 (depth), +10440 (raster) and +10428
(alpha control) immediately before a draw. Guest setters `sub_82135078`,
`sub_82135108`, `sub_821355A8`, `sub_82134EB8` and related functions establish
these fields. The native library does not include Xenos headers or process
command buffers.

This state slice handles separate RGB/alpha blend factors and operations,
depth enable/write/compare, face orientation and front/back/no culling. It
rejects alpha testing/coverage, enabled stencil, constant blend factors and
unsupported polygon/offset modes rather than silently claiming their support.
At that initial checkpoint RT0 wrote all channels and scissoring was disabled;
the following section records their replacement. Depth attachments, MRT,
alpha/stencil handling and remaining raster options still require integration. Consequently
native draw contents remain diagnostic and invalid for downstream sampling.

The native draw/readback test verifies source-over RGB/alpha behavior on actual
pixels, plus descriptor checks for additive blending, depth writes/compare,
culling and explicit rejection of unsupported alpha testing. All four native
tests pass. The bounded `out/native-bridge-run/render-state.log` capture applied
the observed post state (blend `0x10001`, depth `0x700760`, raster `0x18000`,
alpha control `0x87000006`) and submitted native quads without reported state
conversion errors. This is not full render-state or native-frame equivalence.

### Applied channel masks, viewport and scissor

The immediate path now includes RT0's applied write mask at device+10332 (low
nibble) in its native blend state. `sub_82137F98` updates this mask when targets
change, combining requested masks with attachment presence. Scissor enable at
+11584 is established by `sub_82137978`; `sub_821370E0` stores the requested
rectangle at +12400..+12412 and intersects it with viewport x/y/width/height at
+12376..+12388. Min/max depth follow at +12392/+12396. The native adapter reads
these CPU values for every supported draw and applies a D3D11 viewport and
scissor rectangle. It does not use a full-target viewport as a substitute for
the game viewport anymore. Nonfinite/out-of-range viewport depth and overflowing
coordinates are rejected. Empty intersections become an empty native scissor.

Pixel-readback tests now verify red/alpha-only and zero-channel write masks,
partial scissor coverage, viewport intersection, empty intersections and turning
scissoring off without retaining the prior clip. Viewport overflow and inverted
depth ranges are rejected in tests. The bounded `out/native-bridge-run/clipping.log`
capture exercises this integration with game data. Full frame equivalence remains
unproven; source-scene rendering and remaining state/resource coverage are open.

## Runtime integration

The installed ReXGlue SDK exposes `RuntimeConfig::graphics` as an injected
`std::unique_ptr<rex::system::IGraphicsSystem>`. `ReXApp::SetupPresentation` loads
`gpu_plugin` only if this pointer is empty. A custom implementation can therefore
bypass the Xenos plugin loader without changing recompiled gameplay.

There are two distinct presentation integration paths in the SDK source:

* An injected graphics system supplies a provider/presenter, used by ReXApp for
  the window and ImGui overlays.
* With no graphics system, detached presentation uses `OnCreateImmediateDrawer`
  and an application-owned paint loop. This branch is not selected merely by
  injecting a graphics system whose presenter is null.

The research D3D11 backend owns its own window. It must be adapted to this port's
window and overlay lifecycle rather than creating an unrelated second window.

The SDK's `src/kernel/xboxkrnl/xboxkrnl_video.cpp::VdSwap_entry` still reads a
Xenos texture-fetch descriptor and writes command packets. Its
`src/graphics/graphics_system.cpp` installs the command processor and supplies
vblank callbacks. The native path must replace or bypass the corresponding
guest producers and waits with verified host presentation/completion behavior.
Successful no-op `IGraphicsSystem` methods do not establish that contract.

Removing the plugin also differs from removing every Xenos reference from the
runtime's source: the installed runtime retains Xenos types in its video exports.
For execution without GPU emulation, ensure the native path does not execute
those command-generation/processing paths. A source-level removal from the SDK
is a separate build concern after the executable works through native graphics.

## Migration and acceptance gates

### Indexed geometry boundary (2026-09-09)

`NativeIndexedMesh` creates native immutable vertex/index buffers and input
layouts for the verified float1/2/3/4 declaration codes. It preserves declared
offsets and stride, endian-converts attributes and 16/32-bit indices, rejects
overlapping/unsupported attributes, and checks indexed subsets including signed
base vertices. The WARP quad test now also reads back indexed meshes with padded
POSITION/NORMAL/TEXCOORD declarations, both index widths and two draw subsets.
All four standalone tests pass. Packed bone indices and skinning are not covered.

Tracing the actual model producer `821B2C28` established this CPU API boundary:

- `82137410(device, stream, vertexBuffer, byteOffset, stride, dirtyMask)` binds
  vertex data. A bridge hook retains its unencoded arguments.
- `82149A90` stores the declaration at device +11536.
- `821375C0` stores the index buffer at device +12164.
- `821FE358(device, primitive, baseVertex, firstIndex, indexCount)` submits
  indexed geometry. Model callers use primitive 4 (triangle list).

`821D7530` and `821D76A8` copy the model's vertex and index data into CPU-backed
resources. `822D01C0` sets their data pointer at +24. The vertex resource's +28
contains the encoded byte length; the index resource's +28 is its byte length,
and header bit 31 selects 32-bit indices. The model loader requests 16-bit indices.

The bridge now intercepts the first 20 indexed calls for bounded native upload
and draw-range validation. This is diagnostic, not cached live scene submission:
it does not draw meshes into an unrelated post-processing target. The startup
smoke run `out/native-bridge-run/indexed.log` did not reach this indexed helper;
live resource decoding therefore remains unverified. Next exercise a controlled
mission and connect the full-resolution scene color/depth target before drawing.

### Remaining acceptance gates

Native depth foundation added on 2026-09-09: single-sampled D24S8, D32 and
D32S8 targets, selective depth/stencil clears, separate initialized-channel
tracking and input validation. WARP pixel tests prove nearer indexed geometry
occludes farther geometry and exercise read-only/disabled depth independently.
D24S8 byte readback proves clearing either channel preserves the other. These
are host API tests, not evidence of live guest depth equivalence.

The scene setup function is `8219E3B8` (`8219E5A0` was a return address inside
it, not a function entry). It creates the scene sampled texture via `8213B730`
with format `0x1a22ab60`, stored through the resource wrapper at owner +100.
It creates surfaces via `8213B850`: format `0x18280186` through owner +108 and
format `0x1a220197` through owner +116. The latter's low six format bits are 23,
the SDK's floating 24-bit depth/stencil format, not UNORM D24S8. D32S8 supplies
host floating depth and stencil storage, but viewport depth range/precision and
clear/resolve semantics still need verification before choosing its guest mapping.
Do not silently substitute fixed-point D24S8. Owner +84/+88 supply dimensions;
the setup chooses height from display mode, so do not hard-code 720.

The development bridge now intercepts `8213B850` surface allocation and mirrors
single-sampled `0x1a220197` depth into D32S8. Final resource release removes the
native registration. `821340D0` mirrors only whole-resource depth/stencil clears
whose viewport covers the resource and which have no scissor/explicit rectangles.
Unsupported partial clears invalidate the affected native channel. The lower
clear implementation `821334E8` proves flags 0x10 depth and 0x20 stencil; low
four bits select color targets. Device +12184 holds the current depth surface.

Live startup `out/native-bridge-run/depth.log` confirms a 1280x720 single-sampled
native depth allocation, plus a **640x736 MSAA=1** depth surface which is explicitly
unsupported. The latter comes from the tiled scene setup `8219C258`, paired with
the owner +132 HDR surface and stored at owner +136. This is not a 640x736 final
frame: the tiled rendering boundaries must be recovered before assigning a full
native scene target. No live native depth clear or scene-depth draw is proven by
this run. The native depth registry is not yet attached to world rendering.

Full-frame native scene scope is now hooked at `8219C7A8`, after its original
calls `8219C5A8` (BeginTiling) and sets the full viewport. It allocates HDR color
and floating depth/stencil at owner +84/+88 dimensions, clears from the game's
actual BeginTiling values, and binds those native attachments. Post-process
scopes retain precedence; an unsupported nested scope cannot fall through and
write the scene. `8219C930` closes the scope at intermediate resolve, `8219C840`
closes a direct end-frame route, and `8219E140` releases native scene ownership.
No native tile replay is involved. Single-sampling remains a development limit.

`out/native-bridge-run/full-scene.log` verifies 1280x720 allocation and five
consecutive begin/end pairs (resolve mode 1), clear color zero and **clear depth
zero**. The scene viewport producer loads minZ from 0x820008CC and maxZ from
0x820009A4; the latter is the observed zero clear constant. This reversed-depth
path needs a native viewport/depth comparison test before enabling mesh draws.
The current native scene viewport is provisional 0..1 and no scene draw or
resolve is published; contents stay invalid. Thus this proves scope plumbing,
not native scene imagery. The smoke process was stopped after inspection.

Reversed-depth validation: WARP rejects a viewport with MinDepth=1, MaxDepth=0
(RSGetViewports retains the previous range). The compiler now offers an explicit
native vertex variant: preprocess the retail HLSL, wrap its typed entry, and
transform returned clip position with `z = w - z`. This preserves all other
varyings and permits a legal sorted native viewport range. Unsupported entry
signatures fail explicitly; the wrapper does not rewrite arbitrary returns.
This is host HLSL compilation, not Xbox shader translation or a geometry pass.

The disc checker now compiles reversed-depth variants as well as original shader
entries and constructs their independent reflected bindings. The native quad
test verifies near/far occlusion with the reversed variant, zero clear and
GREATER comparison on D24S8, D32 and D32S8. All four tests pass. The variants
are not yet selected by the live bridge: that still needs per-draw viewport
selection and the variant's own constant bindings/input layout. Do not bind a
variant with unverified original reflection slots. Original viewport validation
continues rejecting reversed ranges until the caller selects the transform.

Live depth-variant selection is now implemented: each registered vertex shader
owns original and reversed `ShaderBindings`, receives named parameters into both
independent layouts at material activation, and validates both links against the
pixel stage. Draw-time viewport decoding normalizes the native range and selects
the corresponding shader and input layout. Quad streams are cached separately
per variant. `MakeNativeDrawViewport` exposes the required reversal explicitly;
the old strict viewport helper still rejects reversed ranges. Tests cover both
1..0 and .75...25 reversed ranges. Scene begin now reads the actual guest viewport
rather than installing the provisional 0..1 assumption.

The indexed hook now calls native `DrawIndexed` for supported float-only meshes
inside the active full-frame scene scope, applying actual blend/depth/raster,
mask, viewport and selected vertex bindings. Other scopes retain the bounded
upload diagnostic. Buffer construction is currently per draw (not suitable for
handheld performance yet); resource caching/update tracking is still required.
Scene contents remain invalid and unpublished because scene producer coverage,
packed/skinned attributes and frame fidelity are incomplete. This implementation
does not prove a live mesh draw was exercised; a controlled mission is still needed.
The bounded `depth-variants.log` startup run reached 1,000 native immediate
submissions with zero reported draw errors and no reported parameter errors.
It did not reach the indexed hook. The owned smoke process was stopped.

Controlled input investigation: the built-in default scripted pad presses Start
at 7 seconds, before this slow dual-renderer run reaches the title prompt. A
window capture confirmed it remained at "Press START". The checked-in
`tools/native-mission-input.txt` delays Start to 45 seconds and spaces A presses
for slow transitions. Set `EDF_INPUT_SCRIPT` to its absolute path. Use isolated
`out/native-mission-run` user data, not existing gameplay saves.

`mission-delayed.log` and `mission-delayed-screen.png` reached Mission Selection,
Mission 1 Arrival, with the difficulty menu on Normal. The first script ended at
125 seconds, before the next confirmation was consumed; it did not reach mesh
draws. Later 145/165/185-second confirmations have now been added for the next
run. The test reached 20,000 material activations / 49,512 named uploads without
reported parameter errors and 4,000 native quads without reported draw errors.
This is still reference-rendered menu navigation, not native mission validation.
The bounded process was stopped. `tools/capture-game-window.ps1` captures the
largest window owned by the exact process ID (MainWindowHandle selected a tiny
auxiliary window), without activating other applications or sending OS input.

Extended run `mission-extended.log` reaches indexed rendering at approximately
137 seconds after input polling begins. It records six actual native indexed
submissions with reversed depth, but also 3,667 rejected attempts by that point.
The first mesh has four vertices (176 bytes / stride 44), six 16-bit indices,
and four declaration elements; it fails index bounds validation. Other calls
use stride 96 and 32,004 indices. An independent material failure reports
`g_mView guest=3 native=4`; subsequent missing-active-shader failures follow it.
Do not pad the matrix or flip index endianness speculatively: inspect producer
layout and actual resource bytes. Diagnostics now include the offending index,
vertex count and initial index-resource words for the next run.

The reference screenshot during this transition was black then white, not a
verified playable mission frame. The run establishes live indexed interception,
not native frame correctness or successful gameplay. It was stopped after
recording the failures. All existing unit tests and the 130-original/66-reversed
shader compilation check passed before this run; the updated diagnostic build
also succeeds. Next resolve index bytes and optimized matrix register uploads.

Root causes recovered in the follow-up `mesh-bytes.log` run:

- GuestReader incorrectly assumed every guest address translated as base+address.
  A live index resource pointed at 0xF2A2FA40. Reading host 0x1F2A2FA40 returned
  float-like words and index 15606 for a four-vertex mesh. Read-only process
  inspection found the correct physical address 0x212A30A40 containing exactly
  `00 00 00 01 00 02 00 00 00 02 00 03`. The SDK's `Memory::TranslateVirtual`
  applies each heap's host offset; the bridge now uses that API and rejects
  spans whose translated endpoints are not contiguous. It retains committed-page
  validation. No index byte-order change was made.
- `821A1CA8` stores the global value vector's allocated logical float4 count at
  +8, rounded to four registers; material records instead store the Xbox
  compiler's consumed footprint. `821A17D8`/`821C8000` write full transposed
  matrices to the global data. Native uploads now request their reflected
  register size and require it to fit that owned global vector. They read actual
  data, not invented padding. Local packed constants retain their strict checks.

The shader-binding API now exposes tested register-size requirements for row/
column matrices, scalar/vector arrays and optimized-out names. Four native tests
pass and the Windows executable builds. The diagnostic run was stopped before
rebuilding; the two fixes still need the next live mission validation.

Live validation `mesh-translated.log` now reaches **2,000 native indexed
submissions** with reversed depth. The previous index-range and `g_mView` errors
do not recur in this run. 501 rejected draws remain at that checkpoint, with
the reported failures now unsupported packed/non-float attributes. The process
was stopped after collecting this evidence; no playable native frame is claimed.

`NativeIndexedMesh` now additionally expands declaration types `0x001A2286`
(UBYTE4, unnormalized) and `0x00182886` (D3DCOLOR, normalized) to native
inputs. Their 8-in-32 endian conversion and respective
0/1/2/3 versus 2/1/0/3 channel orders are explicit. Native attributes are repacked
into a compact host stride; guest offsets/stride still govern source reads and
overlap validation. Expanded allocation size is capped. The pixel test exercises
all four bone-index components and distinguishes red/blue color order. All four
tests and the Windows build pass. Live skinned-mesh transformation, bone palette
uploads and frame fidelity still need verification; this is not full skinning
acceptance merely because the packed input adapter exists.

Follow-up `packed-mission.log` reached 3,000 indexed submissions with ten
reported indexed failures, including shader input-layout mismatches. It also
reported missing `g_tShadowTexture_Sampler` resources. Inspection of retail
envelope shaders revealed `int4 BLENDINDICES`, not float4. UBYTE4 expansion now
selects FLOAT/SINT/UINT from the native shader's reflected input signature;
integer inputs retain integer bits rather than float bit patterns. D3DCOLOR
remains normalized float4. Pixel-readback tests now exercise both int4 and uint4
indices, four unequal weights, a 32-entry float4x3 bone palette uploaded from
guest registers, and both ordinary/reversed-depth vertex shaders. All four
native tests pass. The prior live process was stopped before rebuilding; live
acceptance of this integer-input correction is still pending. Neither these
tests nor successful submissions prove a complete native game frame.

Shadow investigation corrected the interpretation of the earlier error:
`TrySetTexture` returned false because the *compiled binding name* was absent,
not because the texture handle lacked a resource. The source diagnostic now
also prints the decompressed `Common.fx`. That source guards shadow sampler
declarations and shadow lighting with `__DX__`; native compilation previously
omitted this define and silently compiled lighting without shadows. Both
normal compilation and reversed-depth preprocessing now define `__DX__=1`.
All 130 retail entries and 66 reversed-depth variants compile successfully,
and reflection now includes `g_tShadowTexture_Sampler`. A regression guard
requires this define for the synthetic mesh vertex/pixel and reversed entries.
Actual shadow render-target contents and frame fidelity remain unverified.

Separately, legitimately optimized-out legacy combined samplers no longer
abort a whole material upload and clear its other textures. A record absent
from both texture and sampler reflection is skipped; sampler-only inconsistent
bindings still fail. Tests verify a dead sampled expression is absent in both
maps while a live combined sampler retains its resource bindings. This is not
a substitute for enabling the retail shadow code above.

Live `integer-mission.log` reached 1,000 native indexed submissions with two
indexed rejections at that checkpoint; eight input-layout mismatches had been
reported by shutdown. Thus integer-index pixel tests do not establish complete
live mesh coverage, and the remaining declarations/signatures need tracing.
This run used the integer correction but predates the `__DX__`/sampler changes.
The exact process was stopped before rebuilding with those changes.

Input-layout failures now include the vertex entry, HRESULT, guest stride,
supplied semantic/format/offset/type, and reflected required signature. A
negative test verifies a missing UV produces these diagnostics. The standalone
shader checker also validates all 90 retail pass links in both ordinary and
reversed-depth modes; all pass with `__DX__` enabled (196 shader compilations).

The first shadow-enabled diagnostic run (`shadow-layout.log`) missed the
45-second Start input. A captured window at about 170 seconds still showed
“Press START to continue”; later A inputs did not enter gameplay. This is not
mission validation. The process was stopped, and a separate slow-start input
script delays Start to 90 seconds with a three-second hold for the next run.

`shadow-layout-delayed.log` successfully passed Start (captured player-selection
menu) and reached indexed rendering. The remaining layout failures are now
specific: stride-48 declarations contain POSITION0/NORMAL0/TEXCOORD0/1/2;
stride-52 declarations add D3DCOLOR at offset24. The selected VS_Main requires
POSITION0/NORMAL0/TANGENT0/TEXCOORD0/1. TANGENT0 is missing from both live
declarations. Do not synthesize a tangent or rename a UV without recovering the
guest's shader/declaration pairing and intended fetch/default behavior.
No texture-binding or parameter-bridge error was reported through the sampled
300-second checkpoint after enabling `__DX__`. This does not prove shadow
resource contents are available: pending scene/reduction resources remain.
Two DDS imports reported unsupported pixel formats; their errors now include
flags, bit count and channel masks (or FourCC) for the next live trace.

The standalone `edf_native_texture_check <game directory>` now recursively
decodes SGSL-wrapped loose `.dds` assets and imports each into a native D3D11
device without booting the game. Initial scan: 83 assets, 34 failures, all
Map01..Map05/Shadow*.dds with flags=2, bits=8, RGB masks=0 and alpha mask=255.
The importer now supports this verified A8 format as RGBA8 `(0,0,0,A)`;
Common.fx samples the alpha for shadow lighting. Readback tests cover four
different alpha values, padded top-level rows, a separate mip, and malformed
alpha masks/truncated payloads. All four native tests pass and all 83 loose DDS
assets now import successfully. This census excludes DDS embedded in archives
and does not establish live shadow/frame correctness. It identifies a concrete
missing map-shadow format rather than treating the sampler-name fix as enough.

Shader-pairing investigation verified `821498C8` writes the vertex shader
handle to device+12420 and `82149608` writes the pixel shader to device+12416.
The indexed bridge now checks these authoritative CPU bindings against its
last activated native pair before submission. A mismatch is rejected with
both pairs logged, rather than rendering with a potentially stale material.
Layout errors also include shader handles and the guest caller address.
This distinguishes an unobserved shader change from a genuinely absent tangent
in the next mission trace; it does not yet resolve the tangent failures.

The declaration binder `8213E070` supplies the missing-semantic contract. It
searches 12-byte declaration entries by usage byte+9 and index byte+10. On
failure, branch `8213E140` selects fetch slot95 and builds the constant swizzle
from `0x9250`. The BE16 table at guest `0x82009964` starts
`e000,1c00,0380,0070,0008,000a,0008,000e`: its X/Y/Z/W masks select
`4,4,4,5`, the zero/zero/zero/one selectors. Thus missing tangents are not a
request to generate a tangent basis: the guest explicitly defaults them.

The native mesh adapter now appends `(0,0,0,1)` inputs for missing known guest
semantics, using float/int/uint storage according to reflection. It retains
strict validation of malformed supplied declarations and unknown semantics.
Pixel-readback tests exercise all four default components and ordinary/reversed
shaders for all three scalar types. The prior negative missing-UV test was
replaced because the recovered guest contract permits missing semantics.
All four native tests pass. Live validation of the default adapter is pending;
the running shader-pairing trace predates this change.

`shader-pairing.log` reproduced the missing-tangent errors after the draw-time
CPU shader-pair check passed. The failing stride52 cases used guest
VS=0x40c39580 / PS=0x40c388c0 and caller=0x821d97e8 (grouped renderer).
Thus these captured failures are not stale native shader selection. This run
also includes A8 texture support; no DDS-import failure was reported in the
collected error trace. The process was stopped before rebuilding with the
new default-semantic adapter. Full native frames remain unverified.

Indexed meshes now have a native buffer cache keyed by guest VB, IB,
declaration, shader handle, and depth variant. Reuse requires identical shader
bytecode identity, stride/index width, and exact declaration/vertex/index bytes.
This avoids stale data before every guest update producer has an explicit hook;
it still pays CPU comparisons and is not the final upload strategy. Guest final
resource destruction invalidates matching entries; shader-owner teardown clears
the cache. Failed data replacements erase old entries before construction.
The cache limits retained geometry payload to 64 MiB and 256 entries with LRU
eviction; larger meshes remain usable but uncached. The payload accounting covers
source snapshots, expanded buffers, and CPU indices, not driver allocations or
in-flight references. Counters report builds, hits, and retained payload bytes.

Tests verify reused buffers, changed vertices via pixel readback, changed index
validation, invalid declaration replacement, shader reload, resource invalidation,
budget eviction and oversized uncached meshes. All four native tests pass.
Live performance is not yet measured; `default-inputs.log` predates the cache.

Live `default-inputs.log` reached 2,000 native indexed submissions with zero
indexed errors after implementing missing-semantic defaults. The prior
missing-tangent/layout failures did not recur in this sampled run, and the
collected log contained no error entries before shutdown. This is submission
coverage, not proof of complete or correct native frames. The uncached run
remained extremely slow (one reference frame over a 103-second interval during
mission rendering). The exact process was stopped before building the cache
integration; no handheld performance claim is made.

Optional `--edf_native_scene_capture <output-prefix>` captures up to three
partial native HDR scenes after indexed work, as `<prefix>.1.bmp` through
`.3.bmp`. It reads the native D3D11 surface itself, not the reference window,
and never sets scene/resolve content validity. Existing paths are rejected.
The BMP is a top-down, padded BGR24 diagnostic with linear HDR clamped to 0..1;
nonfinite RGB is magenta. This intentionally does not claim retail tone mapping
or complete native presentation. Tests cover channel order, row orientation,
padding, negative/overbright/subnormal/nonfinite values, and validity isolation.
All four native tests pass; live capture still needs the rebuilt application.

Live `mesh-cache.log` reached 1,000 indexed submissions with zero errors:
166 mesh builds, 834 cache hits, 24,807,212 retained payload bytes. This confirms
reuse on actual mission geometry. A later reference FPS interval recorded
222 frames over 115.4 seconds; its startup/loading boundaries are not matched
to earlier runs, so it is not a controlled speedup or handheld benchmark.
The process was stopped and the capture-enabled Windows application rebuilt
successfully. The next run must inspect actual native pixels.

`native-capture.log` produced the first inspected native surface images:
`out/native-bridge-run/native-scene.1.bmp` (1,634 indexed draws) and `.2.bmp`
(1,651 draws). Both show recognizable sky, trees and the mothership, with its
position/appearance changing between frames. They also show a large flat pale
region where scene content is absent or incorrectly shaded. These are genuine
native D3D11 pixels but visibly incomplete, not playable native presentation.
The reference window capture during this run was white, so it cannot provide a
matched fidelity baseline. At 3,000 native submissions the log reported zero
indexed errors, 495 mesh builds and 2,505 cache hits.

The reference backend's polygon culling maps face=0 to counterclockwise front
and uses the same cull-front/back bits as the native adapter; no speculative
culling inversion was applied. Native captures now also log D32/D32S8 depth
coverage relative to clear0, split into top/middle/bottom bands, plus depth range
and nonfinite counts. This will distinguish a lack of rasterized geometry from
incorrect shading/fog in the flat image area. Both supported depth layouts have
readback tests, and all four native tests pass. Live depth evidence is pending.

Scene initialization and scene completeness are now separate. A full native
color clear initializes every pixel, so the surface's `content_valid` becomes
true; a separate `frame_complete=false` continues to describe missing producers
and unverified fidelity. This does not enable native presentation. Previously
forcing initialized scene data invalid prevented any downstream native pass
from sampling the real scene and hid that part of the pipeline from validation.

The verified `8219C930` mode1 boundary now explicitly resolves the native HDR
surface and registers its sampled view at owner+104. Destination allocation
format (`0x1a22ab60`) and full-frame dimensions must match. Other modes retain
the previous sampled contents, and the separate backbuffer/end-frame path is
not invented. Tests sample an HDR scene in a downstream native shader, verify
old contents persist before resolve, and verify updated pixels after resolve.
All four native tests pass. This bridge integration still needs live validation;
`native-depth.log` was launched before this change.

Live depth evidence from `native-depth-scene.1.bmp` after 1,634 indexed draws:
112,406 pixels differ from clear0, distributed top/middle/bottom as
112,066 / 340 / 0. Depth range is 0..0.0060027307, with zero nonfinite pixels.
The entire bottom third retains clear depth. This makes absent/clipped/culled
geometry (or draws without depth writes) the next investigation, rather than
assuming the flat region is merely wrong lighting. Unhandled helpers
821FDF50 and 821FDE98 have direct callers 8242EB78 and 8241E2E0 respectively;
those middleware-region callers do not yet establish a missing terrain path.
Do not infer complete draw coverage from the lack of indexed errors.

Capture-enabled runs now bracket up to 2,048 native indexed draws per captured
scene with occlusion queries, grouped in logs by shader pair/raster/depth state.
After the surface readback completes prior GPU work, results are polled once
without flushing: unavailable results remain explicitly unavailable, not zero.
The existing three-capture limit bounds this instrumentation. Ordinary runs
without a capture prefix do not allocate queries. Tests verify full-quad sample
counts and an empty query after readback. Indexed calls outside the native scene
scope are now counted and their first ten callers logged, instead of silently
disappearing from coverage evidence. All four tests and the Windows build pass.

Early live `scene-resolve-visibility.log` confirms the new HDR route: mode1
resolves into guest texture0x40007c80 with initialized=true/frame_complete=false,
and the first downstream material reports a native texture binding with no
missing resource. Later reduction-chain resources still remain missing; only
the first scene-to-texture edge is established here. The run remains active
for mission visibility data and must not be counted as frame completeness.

The visibility run completed three native scene captures: 1,634 / 1,651 / 1,651
indexed draws, all queries available, and zero indexed calls outside the native
scene scope. Most shader groups produced zero passing samples, including all
359 draws of the first frame's VS0x400dcc60 group. This rules out the indexed
scene-scope gate as the cause for those missing pixels, not transformation,
clipping, culling, depth testing, or other missing producer classes.

Packed ARGB color clears now reach registered native surfaces by the actual
four device target handles at +12168..12180 (82133D20), with the per-target
flag bits from 821340D0. r7 is the packed value, never a pointer. Only bounded
full-surface, non-tiled clears are supported; skipped partial/tiled writes
invalidate the surface rather than clearing too much. This does not implicitly
resolve or mark the scene complete. Pixel tests cover RGBA channel ordering,
zero alpha, 128/255 normalization, old sampled data before explicit resolve,
and invalid targets. All four tests and the Windows executable build pass.
Immediate draws also check the actual device shader pair and log the first
five post passes' shader entries, viewport/depth range, and quad coordinates.
The new post-clear.log run is for live validation; post-quad output validity
remains deliberately unresolved until complete target coverage is established.

Early live post-clear.log now identifies all five reduction passes as VS_Main /
PS_Downsample with full-target viewports 640x360, 320x180, 160x90, 80x45, 40x22.
Each uses exactly four clip-space corners (-1,+1), (+1,+1), (+1,-1), (-1,-1),
and the expected source half-texel UV offsets. The actual guest shader-pair
checks pass. Retail PostEffect source confirms this VS copies XY and sets
Z=0/W=1; PS_Downsample averages four source samples without discard. Together
with no blending/culling/depth testing and all-channel writes in the logged
state, this supplies the evidence for a checked full-overwrite path next.
No native color-clear call was observed in this early reduction sequence;
implementing clears alone therefore does not initialize these draw outputs.

The checked reduction overwrite route is now implemented and live-verified in
reduction-chain.log. Native shaders retain decoded-source byte length and a
stable FNV-1a asset fingerprint (not a security hash). The inspected 6,292-byte
PostEffect source (6b7926f9747c6933, no includes) identifies VS_Main and
PS_Downsample; same-named shaders from different/modified sources do not qualify.
The rule additionally requires the four exact clip corners with finite UVs,
full viewport, normal 0..1 depth range, no cull/blend/depth/stencil/alpha-test/
scissor, full RGBA writes, and every reflected texture and sampler bound.
Exceptions and unsupported draws invalidate the target. Surface initialization
still does not imply complete gameplay rendering or native presentation.

All five live reductions now report contents_valid=true and resolve into the
next pass. Missing inputs move downstream to the 16x16..1x1 luminance targets
(texture format 0x2da2ab5e), old-tone history, and a 40x22 target (0x1a22ab5d).
Those format/initialization contracts remain to be recovered. Unit tests cover
source changes, unknown stage identity, missing resources/samplers, partial
geometry/viewport, and conditional/masked draw state. All four tests, the
Windows build, and 196 native shader variants / 90 linked passes pass.

Luminance render textures are now registered using the actual constructor pair
0x2da2ab5e / 0x2da2aba4. The texture/surface format codes select R16F and R32F;
8213AF70 assembles the source format's channel selections from bits18..29 into
the texture header swizzle. This pair selects R111. Native storage retains the
R32F render surface and converts explicitly into RGBA16F sampled storage whose
GBA are one. A native compute resolve performs half rounding only at that
boundary; it preserves the compute slot0 and render-target bindings. No guest
GPU commands or emulator headers are needed. Pixel tests verify an odd 9x3
surface, positive/negative/HDR values, half rounding, R111 mapping, restored
output binding, and old sampled pixels persisting before another resolve.

Live luminance-chain.log confirms the constructor clears the 16x16 surface with
ARGB0x80808080, and the native clear/resolve succeeds. The five color reductions
plus 16x16 Mono, 8x8, 4x4, and 2x2 luminance passes now initialize successfully.
The 1x1 PS_Downsample_Tone still lacks initialized m_OldTone input: the observed
startup clear targets the largest luminance surface, not this history texture.
No history seed has been invented. Its initial-data contract and the remaining
40x22 conversion/blur targets are next. All four tests and the Windows build
pass; the live mission diagnostic remains active.

Tone history initialization is now recovered from actual initial allocation
data, not a guessed clear value. 8213B730 writes the allocation address into
the texture header at +32; 8212F420 does not request an explicit zero memset
for this allocation. At creation only, the bridge reads the 1x1 R16F texture's
entire first 4KiB backing page via checked guest virtual translation. Only a
uniformly zero page is accepted: a 1x1 base-level texture fits in that page,
and zero is independent of tiled ordering and byte swapping. Nonzero, unreadable,
truncated, non-1x1, or already-initialized cases are not imported. The upload
initializes only the sampled R111 pixel, not the separate R32F render surface.
There are no later CPU reads pretending to recover GPU-written history.

Live tone-history.log verifies texture0x40009160's initial page0xfe182000 is
uniform zero. Its first 1x1 PS_Downsample_Tone draw now reports initialized=true;
the m_OldTone/m_Tone missing-resource messages disappear. All ten reduction/
luminance/history passes are connected. The remaining missing textures are
the two 40x22 conversion/blur targets. Tests verify accepted zero input, rejected
nonuniform/truncated input, no overwrite of existing history, untouched render
surface validity, and exact half R111 readback. All four tests and the Windows
build pass. Full scene fidelity and native presentation remain incomplete.

Both 40x22 post targets now have native storage and explicit resolve semantics.
8213FAF8 maps texture format29 (16_16_16_16_EXPAND) to format32 (RGBA16F)
at 8213FD6C, consistent with the reference backend's half-float texture mapping;
this is not a signed-normalized fixed-point texture. The 0x1a22ab5d/0x1a2201bf
pair therefore uses the existing RGBA16F surface/sample copy. The other pair,
0x18280186/0x1a2201bf, retains HDR rendering and quantizes to RGBA8 at resolve.
8213FAF8 derives red/blue swap from the destination texture's first channel
selection and inserts it into copy-destination bit24; the texture's ZYXW
sampling reverses that swap. Native logical RGBA storage avoids both physical
swaps while retaining the same sampled colors. No gamma conversion is added.

The conversion helper is shared with luminance, with explicit conversion-kind
identity so an RGBA8 target cannot receive an R16F history import. Tests cover
negative/overbright clamp, 8-bit quantization, distinct RGBA channels, odd 9x3
dispatch bounds, and old sampled contents before resolve. The inspected retail
source also establishes unconditional full-screen coverage for Tone, GaussBlur,
GaussBlur_Illuminance, Base, and Bloom under the same checked state/input gates.

Live blur-chain.log confirms 13 initialized native passes per early frame:
five reductions, five luminance/history passes, Tone, and two GaussBlur draws.
Both 40x22 resource-missing messages are gone; 1,000 native immediate draws have
zero errors. The final PS_Bloom draw is outside these RenderTexture scopes and
still needs the ordinary scene/backbuffer route. This is not yet a complete
native frame or native presentation. The Windows build and all four tests pass.

The C930 ordinary-output boundary now records the surface at renderer+112 after
the original function binds it. All surface allocations retain their actual
width/height/format/MSAA arguments. A matching single-sample full-frame RGBA8
surface can own a native output target; unknown contracts remain rejected.
Only PS_Bloom enters this new immediate-draw route, with the existing verified
source/input/coverage checks and an additional check against the device's
actual RT0 handle. Nested RenderTexture scopes restore the native ordinary
output. New scenes, end-frame, surface destruction and renderer teardown close
or invalidate the output scope. UI draws are not silently claimed as covered.

Capture-enabled end-frame now writes up to three mission output BMPs with
'.output.N.bmp' suffix, separate from the pre-post HDR scene captures. RGBA8
readback retains the already tone-mapped byte values; it does not apply another
linear HDR clamp. Tests verify exact channels, top-down rows, padding and no
validity side effects. All four tests and the Windows build pass. The new
final-bloom.log run is checking the live surface contract and composite; this
is not a native swap chain or a claim of complete scene/UI coverage.

Early final-bloom.log confirms the exact ordinary surface contract:
renderer0x40001cd0, surface0x40007cf0, 1280x720, format0x18280186, MSAA0.
The native PS_Bloom draw passes its actual bound-surface check and full-target
initialization rule. All 14 early post-processing draws now report initialized
outputs, including the final composite. Mission output image capture remains
pending; the running game window still presents through Xenos.

The longer first output run exposed stale material selection: subsequent UI
immediate draws inherited the bridge's last PS_Bloom record and were incorrectly
sent to the post-quad adapter. Selection now additionally requires the actual
device VS/PS pair and quad-list/16-byte-stride producer. Unhandled ordinary
output draws are counted/logged separately without erasing the initialized
native bloom pixels. Scene/UI fidelity remains explicitly incomplete. The
Windows rebuild succeeds; final-bloom-selection.log validates this correction.
The corrected live run identifies the skipped producer as caller0x8241e2cc,
primitive5, stride16, with its own device shader pair. It is not the engine's
quad-list PS_Bloom producer. Bloom still initializes normally and the former
declaration errors do not recur in the observed startup interval. This caller
is a concrete next lead for the ordinary-output UI/movie coverage investigation.

The first and third mission output captures from final-bloom-selection were
visually inspected: both are entirely white. Their pre-post HDR scene captures
still contain sky, trees and the ship with the lower region flat. Thus the
connected 14-pass route is not evidence of correct final pixels. Whether the
white result is exposure/history, other post state, or a shared guest behavior
needs isolation. Capture-enabled output diagnostics now read the actual center
RGBA of small post resources without clamping HDR, and early Bloom draws log
their native MiddleGray/LuminanceWhite constant values. Readback tests cover
HDR sign/range, RGBA8 normalization, coordinates, and bounds; scalar/vector
constant diagnostics are tested without changing their bound values.

Research note0607 identifies the XUI device and its two embedded vertex blobs
at 820608B0 / 82060B70, plus the viewport projection upload at82415330. The
8241E180 and8241E1D0 device wrappers call the guest VS/PS creators and directly
store their handles through r5. Hooks now record those handles' source addresses
and correlate them with unsupported draws. This is identity evidence only,
not shader emulation or an implementation of those embedded programs.
Native float2-position/float2-UV triangle strips are implemented and pixel-tested
alongside quad lists, including malformed-span rejection and topology switching.
They are not yet routed into XUI until its shaders/constants are recovered.
All four tests and the Windows build pass. The ui-identity-post-pixels.log run
remains active for shader identification and post-resource pixel evidence.
Early live evidence: Bloom's native MiddleGray is0.5 and LuminanceWhite is1.5.
The unsupported startup strip uses VS source82060B70 and PS source82064428;
the latter is not one of the ten basic brush shaders created at82415018.
Its owning creation path is the next specific UI/movie investigation target.

### Native movie shader validation (September 9)

Offline inspection of the program descriptors in `guest_image.bin` identifies
the startup strip's 82060B70 VS and 82064428 PS as the fixed XUI movie path.
The PS samples Y/U/V independently, subtracts 0.0625/0.5/0.5, applies the
embedded conversion coefficients, multiplies RGB by ColorFactor.rgb and writes
ColorFactor.a. `movie_effect.cpp` implements these equations as native HLSL;
there is no runtime microcode translator in this effect. The VS uses the
uploaded transform/projection rows, including homogeneous W and Params.x.

The SDK-free native build now has five passing CTest suites. The movie suite
checks neutral black, colored YUV, tint/alpha, translated projection and a
4x2 spatial pattern with independent Y/U/V planes at every output pixel.
This proves the native shader's tested behavior, not live movie coverage:
live constant binding and draw routing remain unconnected. The game executable
still uses Xenos.

The live creation caller for PS82064428 is8242AF0C inside8242AE38. That
constructor creates six linear 8-bit textures (format28000002): two full-size
Y planes and two half-width/half-height planes for each chroma channel. It
selects the other embedded PS at820641F0 for heights at most576, so the current
movie shader must not be applied indiscriminately to that variant.

8242AF30 locks three planes through8213B5A0, calls decoder8242C078, unlocks,
and publishes its new buffer index at owner+52 only on S_OK. This bypasses
the XUI LockRect wrapper. A thread-local decode scope now captures these exact
locks; after success the bridge checks each handle against owner+20/+28/+36
plus buffer*4, checks the allocation format/dimensions, and uploads the pitched
byte planes to native R8_UNORM textures. End-of-stream/nonzero statuses do not
upload; failed native validation invalidates the affected native copies.
Resource destruction and address reuse use the existing texture registry.

`out/native-bridge-run/native-movie-upload.log` verifies three successive real
decoded frames (buffer0, buffer1, buffer0) copied to native textures. Y is
1280x720 with pitch1280; U/V are640x360 with pitch768, demonstrating why width
cannot be substituted for source pitch. The Windows executable builds and all
five native suites still pass. This establishes live plane uploads; the
subsequent draw integration is described below.

### Movie draw integration and topology correction

The earlier description of startup primitive5 as a strip was incorrect:
it is a triangle fan (6 is a strip). 8242B988 supplies TL,TR,BR,BL, and the
native four-corner quad adapter expands the same fan triangles 012/023.
The movie test now uses that actual corner order, including its spatial YUV
pattern. Generic strip tests remain valid but do not describe this movie draw.

The wrapper vtable at820621A8 establishes SetTexture at slot32, VS constants
at36 and PS constants at40. Their implementations8213BA98/82149248/82149358
retain raw texture handles at device+12272+slot*4, VS float4 rows at1792 and
PS rows at5888. Native movie draws now read those values directly, with the
existing native sampler decoder and actual viewport/render state. Only the
verified embedded shader pair82060B70/82064428 and four-vertex fan on the
known ordinary-output surface are accepted; unsupported depth/reversed-depth
contracts remain explicit errors. Draws do not turn an uninitialized target
into a claimed complete frame.

`native-movie-draw-verified.log` reports three successful native movie draws,
and their captured BMPs exist. The third was inspected and is nearly black
during the opening fade, so this alone does not prove visible video fidelity.
Capture selection now also includes draws30,60,120 for recognizable imagery.
Native post draws explicitly rebind their PS after movie rendering, preventing
the movie shader from leaking into a later engine draw without an activation.

The subsequent `native-movie-visible.log` run successfully submitted draws30
and60. `native-movie-visible.movie.60.bmp` was visually inspected: it contains
the blue/cyan D3 Publisher animation and readable publisher lettering, using
the real decoder planes and native YUV shader. This establishes recognizable
live native movie output, not matched-frame fidelity: visible block patterns
in the image have not yet been attributed to source video, decoding, or
rendering by a reference comparison. Original GPU draws and Xenos presentation
still run in parallel, and complete UI/movie variant coverage remains open.

### SD movie conversion

Offline descriptor/disassembly inspection of820641F0 finds the same texture
fetches and arithmetic as82064428, but four different embedded chroma
coefficients. The exact bit patterns (R-from-V, B-from-U, G-from-V, G-from-U)
are3FCC4AA0/40010A0C/BF503A5E/BEC960C5 for SD and
3FE575A2/400731DB/BF0872F2/BE5A5B23 for HD. Both share luminance multiplier
3F950A7F and input offsets0.0625/0.5/0.5. `PS_MovieSD` now implements the
second fixed native program. The bridge selects the native PS by the actual
bound embedded source address, not output size, and retains separate bindings
for each variant so changing video types cannot reuse the wrong coefficients.

Both entries pass black/chroma/tint/alpha/transform and spatial-pattern pixel
tests; all five native CTest suites pass and the updated game bridge object
compiles. The live41884 run still uses the preceding HD-only executable and
is retained to reach mission diagnostics. No live SD movie or final link of
the updated game executable has been claimed by these tests.

### Bounded post-vertex-shader world diagnostics

`NativeIndexedMesh::CaptureClipPositions` now replays at most96 indexed
vertices with stream output and no rasterized stream, capturing actual native
SV_POSITION values before clipping/depth. It requires the requested VS already
bound and empty GS/SO state, and restores empty GS/SO state even on failure.
The bridge probes at most24 distinct VS/PS/raster/depth combinations, nine
vertices each, only when scene capture is enabled. Probe errors do not suppress
the actual draw. Indexed draws now explicitly bind their native PS as well.
The mechanism follows Microsoft's documented use of prior-stage bytecode for
[stream output without a geometry-shader program](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-creategeometryshaderwithstreamoutput).

Tests verify16/32-bit indices, negative base vertex, index subsets, the bound
reversed VS, mismatched-VS rejection, unchanged framebuffer and subsequent
ordinary rendering. All five suites pass. The full Windows executable now
links successfully with both SD/HD movie support and the clip diagnostic.

The preceding41884 run did not reach gameplay: its window capture shows the
Normal difficulty confirmation still open after scripted input ended. It was
deliberately stopped to deploy the updated executable. The new
`native-mission-clip-input.txt` retains startup inputs and adds later five-second
confirm presses at270/310/350/390 seconds. `native-world-clip.log` is the new
runtime trace; world clip values remain pending until actual mission draws.

### First live world clip evidence

The44328 run using the later confirmations reached mission drawing at04:05:42.
`native-world-clip.log` contains actual post-VS positions. For the first sampled
VS_Blend draw, all nine sampled vertices have positive W and Y/W from2.832 to
3.114, beyond the top clip plane. The next sampled VS_Reverse draw spans
Y/W3.212..3.230, and a sampled static VS_Main/PS_PixRefrectSpecular pair spans
2.866..2.921. Both static and skinned samples can therefore disappear before
depth testing. These are only the first triangles of one draw per sampled
state combination, not bounds for whole meshes or proof of a shared matrix bug.
No culling, depth or matrix-order change follows from this evidence alone.

The first native pre-post scene BMP (`native-world-clip.1.bmp`) was inspected:
trees and the ship occupy the upper region, while most world geometry remains
absent. The same run's game-window capture after its first presented mission
frame (`world-clip-window-first-frame.png`) is entirely white. Thus the white
final output also occurs in the Xenos-backed presentation; it is not evidence
of a native-only bloom error. This is a temporal observation, not an exact
matched-frame pixel comparison or proof that exposure is correct.

Source inspection confirms821C8000 transposes all16 matrix elements into the
global value buffer. The next useful comparison is the actual bound world,
view and projection matrices plus input positions for an invisible draw,
against its captured native clip positions. The running process is retained
for subsequent frames; no speculative transpose or exposure fix was applied.

### Matrix/input comparison capture

`ShaderBindings::ReadFloat4x4` exposes logical row-major matrix values from
either reflected row-major or column-major constant storage, without changing
the upload or GPU binding. Tests use a non-symmetric matrix to distinguish
the two layouts and verify missing-name and wrong-type handling. Existing clip
probes now also log g_mWorld/g_mView/g_mProjection/g_mViewProjection when present,
plus POSITION0 float3 inputs at the exact captured index/base/stride offsets.
These diagnostics permit an explicit input * world * view * projection
comparison with native SV_POSITION for shader entries with that transform.
They do not assume every shader uses that equation or change matrix order.

The44328 run completed all three native scene/output captures (latest04:09:23)
before it was deliberately stopped. The complete updated Windows executable
builds, all five suites pass, and process49936 now runs with the same extended
mission input script and `native-world-matrices.log` capture prefix. Its new
matrix/input evidence remains pending until it reaches mission draws.

### Reloadable diagnostic input

The49936 attempt remained at the title prompt: its early fixed Start press
was missed, so it produced no mission matrix evidence. To avoid restarting
for every timing correction, `EDF_INPUT_SCRIPT_RELOAD=1` now opts the synthetic
pad into checking its named input file once per second. Changed readable files
replace the event list without resetting the first-poll clock; past events
are not replayed. Missing files retain the prior finite schedule, and an empty
file releases scripted buttons through the existing keystroke transitions.
The normal/default input route is unchanged when reload is not enabled.

The six-suite SDK-free test run passes, including reload timing, unchanged and
missing files, empty-list release, and button-up events. The Windows executable
also builds. New process19260 uses `tools/native-live-input.txt`, initially
empty, and logs to `native-world-live-input.log`. After the title prompt was
visually confirmed, adding Start at130000ms was reloaded at123559ms; the log
records press at130041ms and release at135170ms. The window then advanced to
the next dialog without restarting. Further inputs should be queued only
after inspecting that live window; matrix capture remains pending.

`tools/compare-native-world-matrices.ps1` recomputes the candidate row-vector
WVP transform from logged logical matrices and indexed input positions, with
an explicit `-ReverseDepth` option. A translated-point fixture gives zero error
with reversal and65 without it. The report is a numerical diagnostic, not a
claim that all shader entries implement this equation.

### Live mission matrix evidence (September 9, process 19260)

The reloadable input run reached Mission 1 on Normal without restarting.
The first indexed clip probe arrived at 04:32:39 in
`out/native-bridge-run/native-world-live-input.log`. With `-ReverseDepth`,
the first thirteen static VS_Main/VS_Reverse probes match candidate CPU WVP
within 0.000281 maximum absolute clip-coordinate error (6 or 9 vertices per
probe). This supports correct application of the uploaded logical matrices
for these samples, not correctness of the matrices selected for each draw.
Blend/SingleBlend results are not comparable with this unskinned equation;
their unused g_mWorld is zero and bone transforms require separate validation.

Many static probes share world translation (879.25214, 683.2, 200.38069),
including different materials. Investigate shared-object geometry versus stale
per-draw constants before modifying transpose, culling, or depth conventions.
The current bridge reads named parameters at effect activation; the next audit
must check constant changes between activation and draw against the guest path.
This evidence does not establish complete native world rendering or presentation.

The follow-up source audit found a missing native update path: the loop at
821D97C4 calls 821D9600 immediately before each 821FE358 indexed draw. Its
12-byte records are `[data, first_vertex_register, register_count]`; it invokes
82149248 independently of material activation. The bridge now snapshots the
active material's named register ranges and applies these per-instance overrides
to both native vertex shader variants after the original upload. Partial updates
preserve untouched matrix/array slots rather than zero-padding them. Invalid
overrides invalidate the active native draw instead of submitting partial state.
Six native test suites pass, including partial row/column matrix updates and
out-of-range rejection; the full bridge translation unit compiles. Live visual
verification of the new path remains pending deployment.

The full Windows executable subsequently linked successfully. Process19260 was
stopped deliberately after retaining pre-fix captures, and process48936 started
at 04:38:18 with log/capture prefix `native-instance-update`. Its reloadable
input file was cleared for a new first-poll clock. Mission verification remains
pending; do not infer a visual fix from the successful build or unit tests.

Process48936 reached the instanced path at 04:47:33 but the adapter rejected
its list before applying updates. Inspection of the complete 821D9600 prologue
showed `r30 = r3 + 12`: vector begin/end are at instance+16/+20, not +4/+8.
The adapter offset is corrected; this run is not evidence of a visual fix.
The bone-palette test now also patches and restores one live float4x3 array
element before a GPU draw, verifying that neighboring transforms survive.
All six native suites pass with that additional test.

After the offset correction the full Windows build passed. Process48936 was
stopped deliberately to deploy it. Process36924 started at 04:48:53 with
`native-instance-offset` log/capture prefix and the just-observed input schedule
retained (last A at500000ms). Revalidate its screen and adjust the reloadable
file if timings differ; the first-poll clock belongs to this new process.

The embedded-list parser is now factored into `guest_instance_parameters.h`
and exercised with a fixture whose old +4/+8 offsets are deliberately invalid.
Tests cover the actual +16/+20 range, two 12-byte records, the last valid vertex
register, overflow, partial/reversed/oversized lists, and empty lists. Six suites
pass and the refactored bridge object compiles. Process36924 retains the prior
equivalent inline offset fix; no restart was performed for this refactor.

Native backend integration audit: SDK `RuntimeConfig::graphics` accepts an
owned `IGraphicsSystem`; `gpu_plugin` is only used when that pointer is empty.
Implement the interface directly, not the Xenos `GraphicsSystem` base (which
owns RegisterFile and CommandProcessor). The interface separates presentation
from SetupGuestGpu and allows null overlay provider/presenter, but this is only
an integration seam: guest initialization/completion/waits still need real
native replacements. Do not treat a no-op implementation as a working port.

### Verified native city geometry after per-instance updates

Process36924 reached Mission 1 and logged successful g_mWorld overrides
(register0, count4) starting at 04:58:01, with no instance-parameter errors.
Its first native scene capture, `native-instance-offset.1.bmp` at 04:59:19,
was visually inspected: roads, buildings, sidewalks, railings and street
furniture are now visible, replacing the pre-fix flat lower image. This confirms
that missing per-instance constants caused a substantial part of the missing
world geometry. Scene depth now changes916836 pixels, including all307200
pixels in each middle/bottom third, versus the earlier top-heavy coverage.
All1634 first-frame indexed queries completed; indexed uploads report no errors.

Remaining visible defects: player and HUD absent; some pre-post surfaces are
magenta, which the HDR capture encoder uses for nonfinite RGB. Final native
output remains severely overexposed with rectangular discontinuities. The
Xenos-backed window is still white in `offset-mission-window.png`, so that
window is not proof of native presentation or a clean visual reference.
The second history sample rises from0.014701843 to0.025131226, but exposure
correctness and sustained gameplay remain unverified. Process36924 remains
running for further evidence; the refactored parser object is compiled but
has not been relinked into this running executable.

### Invalid-RGB origin trace

The first restored city BMP has magenta pixels at x152..156,y100 (street pole).
The HDR encoder emits this color when any RGB component is nonfinite. To locate
the producing draw, `edf_native_probe_x/y` opt into one-pixel readback after
indexed scene draws during the first captured frame. It stops on the first
nonfinite sample (or4096 draws/error), logs VS/PS entry names and original effect
source fingerprints, and does not modify render output. Defaults are -1/off.
This identifies the first observed invalid indexed result, not proof that the
named shader alone is faulty; input constants, geometry and textures still matter.

All three pre-probe city captures were retained. Process36924 was stopped for
deployment; the full executable linked successfully (including the refactored
instance reader), and all six native suites pass. The next run uses prefix
`native-invalid-rgb`, probe154,100 and the retained input sequence. Live probe
results remain pending; do not clamp invalid shader math without tracing its
inputs and intended source behavior.

While process45020 (started05:04:47, first poll about05:04:53) advances on the
retained schedule, an offline compiler probe at `out/inspect_shader_math.cpp`
compiled the same dynamic pow(x,y) expression with ps_3_0 and ps_5_0 using the
native compiler flags. Both emit log/mul/exp. Microsoft documents legacy pixel
shader log as ignoring the source sign and returning -FLT_MAX at zero, while
modern log returns NaN for negative finite input and -infinity at zero:
[legacy log](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/log---ps),
[modern log](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/log--sm4---asm-).
The disc sources DiffuseNormalCubeLighting and NormalMap contain unguarded
pow(dot(Normal,S),power). This is a candidate compatibility difference, not an
identified cause: Xbox shader instructions/source modifiers must be checked,
and the pending pixel trace must identify the actual material. No shader math
has been changed based on this experiment. The SDK's Xenos log/logc lowering
does not itself add an absolute modifier, so Direct3D9 semantics cannot simply
be assumed for Xbox code.

The live probe completed at05:15:03 in process45020: pixel154,100 first became
nonfinite on indexed draw1377. VS_Main (handle40d8e550) and PS_PixRefrectSpecular
(40ba7030) share source fingerprint1c3ea79f8ddc952e, matching `m_tDNC.dxsl`
(5024 decoded bytes). RGB was NaN and alpha0.9995117. This effect has no direct
unguarded pow call; the earlier DiffuseNormalCubeLighting/NormalMap hypothesis
does not identify this draw. Its path normalizes vertex normals/tangents, then
normal-mapped normals and reflected eye vectors, and calls Common.fx Lighting
(nested normalize plus lit). Next inspect actual attributes/constants and
intermediate values for this material rather than altering unrelated effects.
The first scene capture completed at05:15:23 with all1634 indexed queries;
process45020 remains live and should not be restarted solely for waiting.

The invalid-pixel trace now additionally logs the implicated draw's bound
material/light/fog vectors, world/view/projection/view-transpose matrices,
vertex declaration, and zero/nonfinite NORMAL0/TANGENT0 float3 statistics
(up to65536 indexed samples, with three examples). These are diagnostics only,
invoked once after the first invalid result; absent semantics are visible in
the declaration log. Non-float3 attribute formats are reported but not decoded
by this diagnostic. The original draw and output are unchanged.

The full build and six native suites pass. Process45020 was stopped deliberately
for deployment after preserving its captures. Process40964 started05:19:06,
with prefix `native-invalid-inputs`, pixel154,100, and the retained menu input
sequence. The new input evidence is pending that run reaching Mission1.

### Zero-tangent normalization correction

At05:29:17 process40964 reproduced draw1377. Its414 indexed normal samples
were finite/nonzero; declaration stride52 contains POSITION0, NORMAL0, COLOR0
and TEXCOORD0/1/2, but no TANGENT0. The established missing-input default is
(0,0,0,1), giving the shader a zero float3 tangent. Material power120,
bump height0.8 and reflection rate0.3 were finite. This rules out a zero-power
explanation for this draw and identifies an actual zero normalization input.

The SDK reference `dxbc_translator_alu.cpp` explicitly restores legacy
zero-times-anything to positive zero in vector MUL/MAD, unlike ordinary SM5
0*infinity. A new GPU regression reproducing the absent tangent failed before
the correction with NaN half-float color words. Native compilation now prefixes
normalize overloads that return zero for zero squared length and otherwise call
the original intrinsic. This also covers Common.fx includes, without GPU
instruction emulation or changing nonzero normalization intentionally. NaN
inputs are not explicitly sanitized. The regression passes for both missing
and supplied nonzero tangents; all six suites and all196 shader variants pass.

The Windows executable linked successfully. Process40964 was deliberately
stopped after preserving two input-trace scene captures, and process39416
started05:32:50 with prefix `native-zero-normalize`, probe154,100 and the same
recorded menu inputs. Live visual verification of the correction is pending.

Live verification completed at05:43:37 in process39416. The first native scene
capture `native-zero-normalize.1.bmp` contains all1634 indexed draws, with
1634 available visibility queries and no invalid-RGB probe report at154,100.
Visual inspection confirms the formerly magenta poles now render finite colors.
A full BMP pixel scan found3495 exact magenta markers in the preceding
`native-invalid-inputs.1.bmp` and zero in the corrected capture; pixel154,100
changed from255,0,255 to193,225,225. This verifies the observed nonfinite-color
defect, not overall frame fidelity. The player/HUD are still absent, and the
first post-output capture is almost entirely white. Xenos remains enabled.

The normalization GPU tests additionally cover scalar/float2/float3/float4 in
the pixel stage: signed zero becomes positive zero, positive and negative
nonzero vectors retain expected normalization, and NaN inputs remain NaN.
The native build was up to date, all six suites passed again, and the disc
shader check passed44 effects/196 variants/90 linked passes with zero failures.
Process39416 remains active for subsequent scene/post captures.

The offline math probe also compares `mul(input.xyz,View)` with
`mul(float4(input.xyz,1),View)` under ps_3_0/ps_5_0. Both profiles emit dp3 for
the former and dp4 for the latter, so this experiment does not support a
legacy-versus-modern implicit-position-w difference. Do not insert translation
into skinned source expressions on that assumption; actual bone data, shader
identity and intended first-frame visibility still need verification.

The third live post-resource readback has history R=0.06854248, following
approximately 0.024 and 0.048 in the first two captures, with scene luminance
near 0.909. This supports gradual exposure adaptation but does not establish
correct final output. The same diagnostic run takes roughly 86-101 seconds
per mission frame and cannot establish handheld performance.

### Post-processing and hook timing investigation

The corrected run's first two frames take approximately122 seconds each.
The disc PostEffect source blends old and new tone history by0.025 per frame;
PS_Tone and PS_Bloom divide MiddleGray by history+0.001. With first-frame
history0.014701843 and MiddleGray0.5, the exposure multiplier is about31.84.
PS_Tone also supplies the additive blurred illumination used by PS_Bloom.
These equations support startup exposure as a candidate for the white output;
they do not prove the binding, history initialization or final output correct.
Do not replace them with a visually convenient exposure constant.

An opt-in `--edf_native_hook_timings true` diagnostic now separates original
and native CPU wall time for effect activation, instance overrides and indexed
draw hooks. Each thread reports256-call buckets with total/max milliseconds.
Times include nested work and lock waits and are not exclusive GPU/frame costs.
The diagnostic defaults off and avoids clock reads when disabled. The updated
bridge object compiles; runtime timing evidence awaits deployment after the
current capture run is preserved.

All three corrected scene/output captures were preserved before process39416
was deliberately stopped for deployment. The full Windows executable linked,
and all six native suites passed again. Process47224 started05:48:35 with prefix
`native-hook-timings`, the retained input schedule, hook timings enabled and
the one-pixel probe disabled. Its first256-call activation.original bucket is
0.7519ms total (0.0046ms max), confirming the diagnostic runs; startup timings
do not identify the mission bottleneck. The process remains live.

### Batched named-parameter validation

Startup native activation timing buckets repeatedly cost1.3-2.0 seconds per256
calls while the corresponding original calls total less than1 millisecond.
A read-only VirtualQueryEx probe of process47224 measured1000 queries at
host182060000 in13.145ms and at140009160 in171.546ms. Both are committed ranges;
the latter reports a2A17000-byte region. This external query benchmark is not
an exclusive profile of the bridge, but establishes that repeated memory-region
queries can be materially expensive in this process.

The named-parameter path previously validated each table and then revalidated
individual words within it, repeating the whole vertex read for reversed depth.
`guest_parameter_records.h` now validates each vector/table/global-value block
once per call and decodes its big-endian fields directly. Names, live global
data pointers and available lengths are reread every activation, not cached.
Normal/reversed vertex bindings share the validated metadata/data read but each
keeps its own reflected size check. Local zero-sized records still reach the
binding validator, and optimized-out globals retain their skip behavior.

New tests cover exact local/global fields, read counts, same-address global
pointer/length/name updates, empty pixel tables, invalid stage, oversized counts
and registers, and truncated tables/value vectors. All six suites pass; the
Windows bridge object compiles. The executable has not been relinked/deployed:
process47224 is still gathering the pre-optimization baseline. No runtime
speedup or visual equivalence is claimed yet.

The baseline completed its first mission frame at05:59:18 (117.4 seconds).
Its native city capture was visually inspected and retained with the output
capture as `native-hook-timings.1.bmp` / `.output.1.bmp`. Timing report buckets
emitted from05:57:20 through05:59:19 contain512 native activations averaging
122.5279ms,1536 native indexed hooks averaging25.3395ms, and1280 native instance
updates averaging4.8151ms. Corresponding original-call averages are0.00449,
0.00277 and0.000711ms. These are inclusive CPU timings and incomplete boundary
buckets, not an exclusive frame/GPU profile.

`tools/summarize-native-hook-timings.ps1` reports per-phase call counts,
total/average/max milliseconds with optional Since/Until report-time filters.
The future-time empty filter was checked; filtering cannot split boundary
buckets or recover unreported trailing calls.

After preserving that baseline, process47224 was deliberately stopped for
deployment. The full Windows build succeeded. Process57228 started06:00:26
with prefix `native-batched-parameters`, the same retained input schedule,
hook timings enabled and pixel probing disabled. This run contains the batched
parameter optimization; live performance and visual verification are pending.

### Batched texture and sampler validation

The same repeated-query pattern exists in UploadTextures: each table was
validated and then each record word was independently queried again. The new
ReadTextureParameters helper validates vector/table blocks once per call and
decodes local28-byte and global8-byte records. Global string metadata is read
as one28-byte block; inline/heap spelling, declared length and live handle
validation remain intact. ReadSamplerWords validates one24-byte block instead
of four independent words. No guest values or mappings are cached across calls.
Unused native texture records still skip device sampler access, so their unused
slot values are not rejected. Used slots still require0..15.

Tests cover local strides, inline/heap global names, changed handle/key pointers,
length/capacity errors, underflow, oversized/truncated/empty tables, all four
sampler word offsets, live sampler changes, invalid slots and address bounds.
All six suites pass and the bridge object compiles. These resource-binding
changes are not yet linked into the executable: process57228 remains the
parameter-only comparison run and must not be described as testing them.

Viewport and render-state reads now use bounded block decoding as well:
ReadViewportWords validates the contiguous40-byte viewport/scissor fields and
the separate enable word (two queries instead of eleven); ReadRenderStateWords
validates the24-byte state group and separate mask/enable words (three queries
instead of six). Indexed, movie and post draws consume the same decoded values
as before. No full-device allocation size or persistent mapping cache is assumed.
New tests cover every field offset, unchanged raw signed/float bits, mask/boolean
normalization, live changes, overflow and truncated blocks. All six suites pass
and the bridge object compiles. These draw-state changes are also not deployed
into process57228; the current executable still isolates named parameters.

Inspection of the SDK's BaseHeap access APIs rules out replacing VirtualQuery
with QueryRangeAccess alone: xmemory.cpp's QueryRangeAccess checks only
current_protect, while Decommit clears the commit state without clearing those
protection bits. Allocations can also retain protection bits while only reserved.
QueryRegionInfo exposes commitment and protection but scans to the region's
attribute boundary; any future metadata-based validator must additionally
account for host page reconciliation, physical aliases and partial ranges.
No reader guard was weakened on this hypothesis.

The full batched-state run completed at06:21:28.371, with all1634 indexed draws
and visibility queries available. Both scene/output BMP hashes match the
parameter-only and baseline hashes recorded below exactly. The span from first
indexed input06:20:25.074 to scene capture is63.297 seconds, versus75.235 seconds
for parameter-only. These remain diagnostic captures, not handheld frame rates.
After preservation, process53996 was deliberately stopped and the full Windows
build succeeded. Process48488 started06:22:23 with prefix
`native-completion-probe`; it retains the batching changes and additionally
enables the sideband native event-query probe described in
`docs/native-gpu-boundary.md`. The original guest counters and Xenos backend
remain authoritative until native synchronization is fully implemented.

### Parameter batching live verification

Process57228 completed the first scene at06:10:24.647, with all1634 draws and
1634 available visibility queries. Its scene BMP is byte-identical to the
baseline: SHA256314B6720C52E43ADFB613AC115BF1B9DAE57F7A03E0F9D0A9C135CA97E592D5A.
The post-output BMP also matches exactly, SHA256
A177D96DA845E94F6B13C6B1F4EDC42BC880DE18C2FE16A1916AA3651E1C2DAB.
This verifies the optimization for these captured pixels, not missing gameplay
coverage, unclamped HDR equivalence or general correctness across missions.
The scene still lacks player/HUD and post output remains white.

The first indexed input was logged06:09:09.412, about75.24 seconds before scene
capture, versus roughly117 seconds in the baseline. Reported native activation
buckets average35.66ms over768 calls in the selected mission interval, compared
with122.53ms over512 baseline calls; boundary buckets and differing counts mean
this is not an exact per-material speedup. Indexed timings remain about25.86ms
per call, making the pending draw-state batching relevant.

After preserving these captures, process57228 was deliberately stopped for
deployment. The Windows build linked successfully. Process53996 started06:11:46
with prefix `native-batched-state`, the retained input schedule and the same
timing/capture flags. It contains parameter, texture/sampler and draw-state
batching. Its live verification is pending; Xenos remains enabled.

1. Establish bounded reference captures with the current executable: startup,
   animated title, menus, and Mission 1. Record inputs and frame boundaries.
   Resolve resource/draw arguments and synchronization side effects against this
   executable, using the research notes to guide tracing.
2. Add native presentation and a guest resource registry. Verify device creation,
   resource creation/update/destruction, guest address reuse, window resizing,
   and host overlays. Keep fallback selection explicit while coverage is partial.
3. Connect UI and movies, then world geometry/materials, skinning, effects, HUD,
   and post-processing. Preserve guest draw order and live state. Compare matched
   frames and draw/resource records with the reference path.
4. Close guest GPU initialization, swap, completion, interrupt, and wait paths.
   Verify sustained gameplay and mission transitions with no command processor,
   no Xbox GPU register/MMIO handling, and no Xenos plugin loaded.
5. Switch startup and build defaults, remove Xenos from packaging, and update
   graphics settings to the native backend's actual capabilities. Verify a fresh
   output directory runs without `rexgpu-xenos.dll`; an old build directory can
   conceal an accidental dependency.

Completion requires the normal game route to render and run without GPU
emulation, not just successful configuration, linking, or an empty window.
