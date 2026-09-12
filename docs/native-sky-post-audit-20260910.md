# Sky and post-processing brightness audit

User suspects a sun-like light in the sky. This is a lead, not a confirmed cause.

## Source evidence

Decoded retail shader sources using the existing native shader checker `--source`
against the supplied disc assets (no shader source edits).
`m_tD_Sky.dxsl` samples its diffuse texture multiplied by `m_MaterialDiffuse`.
The pass declares CW culling, no depth writes, and SRCALPHA/INVSRCALPHA blending.
There is no separate directional/sun lighting calculation in this shader.
This does not exclude a separate object or a bright texture/material.

`PS_Downsample_Tone` averages four samples, blends the red value with MiddleGray
according to ToneMap, then interpolates old history toward it by0.025 per frame.
`PS_Bloom` scales scene RGB by MiddleGray/(tone+0.001), applies its white-point
curve, and adds a second sampled texture containing bloom. Thus both exposure
history and additive bloom need verification; globally scaling the output would
not establish correct native rendering.

## Paired live capture

Used geometry-audit executable SHA256
B9C2671D169AB5F689AB457EDE521DBDBF86638C7487B47D55B0FA90D0B79EF0.
Confirmed no game running, launched owned PID26940, normal untiled native path,
hook timings and mesh watch audit disabled. Input script advances to Mission1.
Log: `out/native-bridge-run/native-sky-paired-20260910.log`.
Capture prefix: `out/native-bridge-run/sky-paired-20260910`.
Selected indexed frames600/1200/1800, paired scene color and output, limit3.
Stopped only PID26940 after exact executable-path check and WaitForExit.

Visually inspected both scene-color and output BMPs at frames600 and1200.
Frame600 scene retains blue sky, building/ship detail and contrast; its paired
post-processed output is severely washed out across the sky, ship and buildings.
Frame1200 also shows increased haze/highlight spread after post-processing, but
less severely. These are same-frame pairs, not comparisons between camera poses.
The BMP decoder clamps HDR to[0,1] and does not apply display gamma: this locates
visible amplification in post-processing but does not measure original HDR peaks
or establish that all pre-post scene values are correct.

The observed1x1 sampled target at texture0x40009160 has red values0.2861328,
0.56884766 and0.2536621 at the three captures. Nearby final-pass logs show
MiddleGray0.8 and LuminanceWhite1.5. History is changing, not permanently zero.
These are resource observations; identify the actual bound tone/bloom inputs at
the exact draw before attributing final output to this specific resource.

Next: capture exact PS_Bloom input identities/values and separate exposure-only
from additive-bloom contribution at a selected frame. Preserve the retail formula
and normal presentation until the incorrect input or native operation is isolated.
No brightness fix or additional performance optimization was made in this run.

## Exact-input diagnostic implementation

Added read-only ShaderBindings::ReadTexture, returning an owning snapshot of the
current named view. WARP tests cover absent/unbound names, sampler-only names,
resolved binding publication, clearing, and a retained snapshot after clearing.
The optional paired-capture path now snapshots actual PS_Bloom scene/bloom/tone
inputs immediately before its selected draw, logs dimensions/view format/center
RGBA and exact MiddleGray/white-point constants, and writes input BMPs. It uses
the existing bounded capture policy and attempts only once per candidate frame.
Unsupported views or failed diagnostics are caught separately and do not skip
the real draw. No replacement texture or shader is bound by the diagnostic.
Normal play does not perform these readbacks or fetch the capture prefix string.

Native shader checker confirmed the three resource names in PS_Bloom reflection.
Importantly, sampler slots are scene0/bloom1/tone2 while texture slots are
scene0/tone1/bloom2; the diagnostic resolves texture names rather than assuming
matching texture/sampler slot numbers.

Built separate edf2027-native-post-audit.exe; full18tests passed12.50s. Previous
playtest executables preserved. The exact-input capture implementation still
needs live validation; no brightness correction is included in this binary.

## Live exact-input validation

Ran post-audit SHA256
38E0F1DC7F2F7DD3EF7969E51E713A232BD2B214DAF6717255757C7A46632227
as owned PID47140 after confirming no other game. Captured frames600/1200 with
prefix `out/native-bridge-run/post-inputs-20260910` and log
`out/native-bridge-run/native-post-inputs-20260910.log`. Both input capture groups
completed with matching paired outputs. Stopped only the exact-path-validated
PID47140 and waited for exit. This is diagnostic coverage, not a performance run.

At frame600, actual bound scene is1280x720 RGBA16F, bloom40x22 RGBA8 and tone1x1
RGBA16F. Tone red0.2861328, MiddleGray0.8, white1.5 give exposure2.78617.
Scene center RGB=(0.5019531,0.5419922,0.58203125); bloom center texel
RGB=(0.41568628,0.42745098,0.44313726). Inspected the saved40x22 bloom BMP:
it is nonzero scene-shaped illumination, not an absent/solid fallback texture.
At frame1200, same resource dimensions/formats, tone0.5751953 yields exposure
1.38842; bloom center RGB=(0.26666668,0.26666668,0.2784314).
Center texel readback is not the exact bilinear sample at a final output pixel;
do not use these numbers as an exact reconstruction of the output center.

The capture validates actual final-pass resource selection and associates stronger
exposure plus nontrivial additive bloom with the washed-out frame. It does not
prove that the retail exposure target, luminance reduction, blur sampling, scene
HDR values, or texture conversions match the original GPU. Next trace should
inspect PS_Tone/reduction/blur inputs and sampler state, with reference evidence
before changing any authored exposure or bloom constants.

## Upstream capture extension

Extended the opt-in capture to the fingerprint-identified retail PostEffect chain,
not just PS_Bloom. Selected frames now record up to32 passes with pass numbers,
shader entry/target extent, bound input images (including old tone history), and
actual named native sampler descriptors. Input filenames include pass/shader to
avoid collisions between repeated reduction and horizontal/vertical blur passes.
The pass limit bounds work even if a frame never reaches the final output.
This diagnostic does not replace shaders, change constants, or bind substitutes.

Added ReadSampler owning snapshots, with WARP tests for unknown/unbound and
texture-only names, resolved publication, clearing and retained lifetime.
Full build/all18tests passed12.42s; subsequent log-only edits add pass numbers
to input and constant lines for unambiguous grouping. Full-chain live validation
remains pending. No sampler bug or brightness fix has yet been established.

## Full-chain runtime and research cross-check

Ran SHA256 EB7D6C9D28970839C7EA8DC22AF8E036EF3D1CF15D34AA7C0FC08D9FBD498EE6
as owned PID49752, no concurrent game observed, selected frame600 only.
Log `out/native-bridge-run/native-post-chain-20260910.log`; input/output prefix
`out/native-bridge-run/post-chain-20260910`. All14passes captured. Stopped exact
path-validated PID49752 and waited for exit. No rendering changes in this run.

Observed order: Downsample640x360,320x180,160x90,80x45,40x22;
Downsample_Mono16x16; Downsample8x8,4x4,2x2; Downsample_Tone1x1;
Tone40x22; GaussBlur40x22 twice; Bloom1280x720.
Sampler descriptors: point filtering, U/V clamp, base LOD throughout except
the final bloom-image sampler uses linear filtering. These describe observed
state, not yet proof that sampling coordinates match Xbox rasterization.

Read analysis notes0326,0713,0714. Note0713 is superseded. Note0714's labels
place Tone at the start of the second pyramid and Mono at its end, opposite to
the live trace. Inspected actual820B09B0(shard28): it passes mode1 at second-chain
entry and mode2 at its final iteration.820B01E8(shard75) mode2 specifically binds
both the source and the destination's previous texture via named setters before
drawing; ordinary mode1 does not follow this two-input branch. This supports
history at the final pass and is a reason NOT to reorder using the prose note.
Exact constructor technique-name mapping remains to be independently checked.

Disc DXSL assets carry HLSL source plus entry/profile tables, not Xbox compiled
microcode (note0326 and local ParseEffect/source checker agree). Thus no original
compiled shader comparison has been performed. Native compilation success does
not prove arithmetic equivalence; an original runtime compilation artifact or
another verified reference is needed for that comparison. The shader compilation
hypothesis is neither established nor excluded by this capture.

## Controlled native shader arithmetic check

Added `edf_native_shader_check <game directory> --post-arithmetic` with implementation
in tools/native_post_arithmetic.h. It compiles the actual fingerprint-checked disc
PostEffect source through the production native compiler/binding path, draws the
retail VS with each of PS_Bloom, PS_Tone and PS_Downsample_Tone, and reads back RGBA16F.
Independent CPU equations preserve the source's sequential assignments (including
the updated C in the denominator), full-vector tone-history interpolation and
bloom-generation alpha. Inputs are uniform1x1 float textures, point/clamp samplers,
zero downsample offsets, with history0.01/0.2861328/0.5751953/1 and an HDR blue1.25.
Each RGBA channel must be finite and within0.002+0.002*abs(expected), allowing
half-float output quantization and arithmetic rounding. Missing required inputs
or changed disc-source fingerprint fail the checker.

Built the checker and ran it against the supplied x360 game directory:12/12 GPU
cases passed. No game executable or rendering policy changed for this check.
This checks native compiler arithmetic and bindings against audited HLSL semantics,
not Xbox compiler equivalence. Uniform samples intentionally do not test pixel
centers, blur/downsample offsets, filtering across texels, or live material values.
Next discriminating checks should target those spatial inputs or an original
runtime-compiled shader reference, rather than assuming a generic compiler error.

## Spatial negative control: pixel-center mismatch reproduced

Extended the same checker with a nonuniform4x4 float texture reduced to2x2 using
the actual retail PS_Downsample, four explicit offsets, point/clamp sampling,
and the half-source-texel UV origin authored by820B01E8. Expected disjoint2x2
averages are0.15625,0.28125,0.65625,0.78125. With the unshifted D3D11 viewport the
outputs differ (required negative control); adding0.5 to viewport X/Y restores
all four expected values within0.001. The12 uniform arithmetic cases still pass.
Only the standalone checker changed, not the game executable.

The local SDK graphics/util/draw.cpp applies+0.5 screen-coordinate offsets for
PA_SU_VTX_CNTL.pix_center==kD3DZero. Native QuadStream currently copies authored
XY/UV with no pixel-center adjustment. This is a concrete conversion risk, but
the actual retail pixel-center mode must be traced before enabling a production
shift. Test edge coverage and small targets as well; do not equate the controlled
sampling discrepancy with proof of the full live brightness root cause.

## Bounded pixel-center experiment

Added default-off `edf_native_post_integer_centers`. When enabled, it shifts
native viewport X/Y by+0.5 only for a pass accepted by CanInitializeReductionTarget
(audited source/entries, full-screen geometry, expected render state, complete
inputs and matching target extent). Restores the original viewport after the draw
and on draw exceptions. Does not shift indexed world geometry, font/movie or
other unverified immediate draws. Default presentation remains unchanged.

Expanded spatial oracle coverage to all pixels/channels in1x1,2x2,3x3 and8x8
targets, explicitly cleared to zero before drawing a positive uniform sentinel.
All edge-coverage checks, the spatial negative control, and12 arithmetic cases
passed on hardware. Full build/all18CTest suites passed12.09s.
Experimental executable: edf2027-native-post-centers.exe, SHA256
127D1F147EEB5B04C699B501FAD7961466E73A1442ED3A804C2951D9107573D9.
No live enabled run yet; enable explicitly with
`--edf_native_post_integer_centers=true` for a controlled capture.
Actual retail pix_center setting remains unproven; SDK reference identifies
register0x2302 bit0 (zero is integer center). Do not promote the experiment to
default or claim a brightness fix until live/reference evidence supports it.

## Enabled pixel-center live experiment

Ran post-centers SHA127D1F...573D9 with the switch explicitly true as owned
PID50648 after checking no game running. Selected indexed frame600, same startup
input script; log `out/native-bridge-run/native-post-centers-20260910.log`, prefix
`out/native-bridge-run/post-centers-20260910`. Capture completed, then stopped only
the exact-path-validated PID50648 and waited for exit.

Viewed enabled output600 against prior post-chain output600: substantially less
washout, with visible sky/ship/building detail. Actual final tone red0.48095703
gives exposure~1.66 at MiddleGray0.8 versus prior~2.79. Bound bloom center RGB is
(0.14509805,0.15294118,0.16470589), lower than the preceding unshifted capture.
Scene center differs too, and pedestrians have advanced differently between runs:
these are comparable scripted intro captures, not an identical frozen-scene A/B.
No FPS claim, full-game validation, or definitive retail-mode proof follows.

This supports pixel-center alignment as a contributor to the observed brightness
defect and justifies focused playtesting. Keep the switch default-off pending
retail-mode confirmation and wider gameplay/edge checks. Candidate can be tested
with `edf2027-native-post-centers.exe --edf_native_post_integer_centers=true`.

## Rectangular sampling checks

Extended the standalone checker with nonuniform6x4->3x2,4x6->2x3,2x8->1x4,
8x2->4x1 and the actual80x45->40x22 reduction dimensions. Checks every output
pixel/channel against independent integer-center point footprints; output is
cleared to a negative sentinel to catch missing coverage. The unshifted4x4
negative control and12 arithmetic cases remain mandatory.

Initial strict ideal-coordinate oracle failed at80x45 output row11: ideal
source Y=23 lies exactly on a point-sampling boundary, while interpolated float
UVs select the lower adjacent footprint. Refined only exact mathematical
boundary cases to permit either adjacent coherent footprint (all RGBA channels
must match the same footprint); non-boundary pixels retain one expected result.
This does not establish Xbox tie-breaking equivalence. All five rectangular
cases then passed, as did previous coverage/arithmetic/negative-control checks.
Only the checker changed; the user playtest candidate and default-off switch
remain unchanged. No game was running during the check.

## User feedback: performance priority

User tested the experiment: brightness better, not perfect; original appearance
not remembered; performance about20FPS. No user game was running when checked.
Own earlier enabled intro capture logged59..60FPS through115s, but did not cover
the user's gameplay interval and cannot refute their report. No evidence yet
attributes20FPS to the shift, capture overhead, shader compilation, or pacing.
The inspected roaming edf2027.toml showed no matching capture/timing/audit/video
entries; this does not establish the user's complete command line/effective config.
Next priority is same-binary gameplay comparison with the pixel-center switch
off/on, diagnostics explicitly disabled. Keep the switch experimental.

## Same-binary performance comparison

Used unchanged post-centers SHA127D1F...573D9 for sequential runs, identical
scripted navigation, game/user/cache paths, vsync true, FPS cap0. Explicitly empty
scene capture prefix, output capture limit0, paired captures false, mesh watch
audit false, hook timings false. FPS logging remained enabled in both. No concurrent
game observed at launches or process checks; no builds or GPU checker runs during
measurement. Screenshot readback only after measurement, followed by exact-path
validated stop and WaitForExit. Scripts do not move/fire, so this is stationary
Mission1 opening coverage, not the user's whole playtest.

Off: owned PID39132, log native-centers-off-perf-20260910.log (and rotated sibling).
On: owned PID51016, log native-centers-on-perf-20260910.log (and rotated sibling).
Both reached visually verified street gameplay; screenshots centers-off/on-perf-
world-20260910.png. Both stopped. In elapsed150..211s,13five-second samples each:
off56.6..60.0FPS, arithmetic sample mean58.1385; on55.5..59.1, mean57.7231.
These are presentation-hook counts, not scanout or independent simulation ticks.

The enabled run had a real transient slowdown earlier:36.8 at95s,27.1 at101s,
38.4 at106s,26.1 at111s,27.6 at116s,30.7 at121s,35.1 at126s. Later recovered to
58..60FPS, including59.1 at251s. This reproduces a substantial low-FPS interval
with image captures disabled, but NOT sustained20FPS gameplay. Sequential runs
are not synchronized scene A/B and the slow intro shifts subsequent scene timing.
No causal claim about half-pixel GPU cost, machine load, or pacing is established.
User's20FPS report remains unresolved; next capture should target the slow interval
with CPU/GPU/host timing and repeat baseline to distinguish repeatable switch cost
from transient behavior. Keep experiment default-off.

## Host visibility limitation discovered

Started owned PID50416 with the same post-centers executable, switch enabled,
captures disabled, hook and host timings enabled; log native-centers-timed-20260910.log.
Host reports `Native host pacing unavailable: DXGI occluded` repeatedly (counts64,
128,256 through02:50:33), not valid visible presentation cadence. Stopped exact-
path-validated PID50416 and waited for exit rather than treating it as a visible
performance benchmark. This run establishes an important measurement limitation.

Earlier off/on FPS averages remain observations of presentation-hook invocation
only. Host visibility was not instrumented in those runs; screenshots taken after
measurement do not prove visibility during it. Do NOT use those averages to
contradict the user's visible20FPS report or claim delivered58..60FPS.
Next performance measurement needs a visible, non-occluded game window and host
timings alongside FPS. No user game was closed. Brightness captures remain useful
for rendering content, independently of this presentation-performance limitation.

## User-visible follow-up

The user's native-visible-perf.log terminates normally at 02:55:19.105. They
reported that the run is fine. Late FPS samples at t=140/145/150/155 seconds are
60.0/60.0/59.8/60.0. Matching host samples average approximately 16.67 ms with
Present CPU time about 0.40 ms. They still report 25-38 repeated images and
23-39 skipped sequences per roughly five seconds: a pacing issue to investigate,
not proof of a particular visible stutter or scanout rate. Earlier loading/static
image repetitions should not be interpreted as the same problem. This new run
does not explain the earlier reported 20 FPS episode.

No user game was closed. Keep the pixel-center experiment default-off: the user
accepts this candidate, but original-console brightness equivalence and full-game
coverage remain unproven. Native resource ownership work resumes separately.

## Retail pixel-centre mode resolved; experiment promoted (2026-09-12)

The switch was default-off only because the actual `PA_SU_VTX_CNTL.pix_center`
setting was unproven. It is now proven twice, statically and at runtime.

Static: register 0x2302 is shadowed at device+10560. `sub_8214EFF8` - the same
initializer the native device defaults are extracted from - stores **4** there
(`li r9,4` then `stw r9,10560(r31)`, recomp.19), and `sub_82132F48` emits the
same `0x2302 = 4` in the device's default register packet (recomp.67: the stream
is header/value pairs 0x2200=0, 0x2203=0, 0x2208=4, 0x2104=0, 0x2280=0x00080008,
0x2302=4, then a 3-register block at 0x2080). `sub_82136440` is the only later
writer: `rlwimi r4,r11,0,0,30` keeps the stored word's upper 31 bits and takes
**only bit 0** from its argument, then raises dirty bit 35, which the 821330C8
flush maps to index 2 above base 0x2300. The recompilation contains no direct
caller of 82136440, so it is reachable only indirectly if at all.

4 decodes as pix_center `kD3DZero`, round_mode `kRoundToEven`, 1/16th vertex
quantization: the ordinary Direct3D 9 configuration, and exactly the case for
which the SDK's own GPU path adds +0.5 to screen coordinates.

Runtime: the bridge now reads that word per draw instead of assuming it.
`out/native-bridge-run/pixel-centers-on-20260912-172323/game.log:286` logs
`PA_SU_VTX_CNTL=0x4, integer_centers=true, rounding=2, quantization=0` once -
the line is emitted only on change, and it never changed during the run.

`edf_native_post_integer_centers` is therefore replaced by
`edf_native_pixel_centers` (default **true**), and the shift is applied from the
live guest word rather than unconditionally: a guest that selected `kOGLHalf`
would get no offset. Scope is unchanged - only passes accepted by
`CanInitializeReductionTarget`. Indexed world geometry, font, movie and the
other immediate draws still need their own validation before the same offset is
extended to them, even though the register is global on real hardware.

`tools/start-native-binding-validation.ps1` now takes `-PixelCenters`, and
omitting it exercises the new default.

Not resolved: the post chain is still visibly over-bright. Comparing
`frame.scene-color.600.bmp` (well exposed, full contrast) with
`frame.output.600.bmp` (highlights blown, low contrast) from the run above shows
the washout is introduced by the exposure/bloom chain, not by the scene render.
`g_PostEffect_MiddleGray` 0.8 and `g_PostEffect_LuminanceWhite` 1.5 match the
retail constants, so the next trace should target the luminance reduction chain
that produces the 1x1 tone value, not the final pass.
