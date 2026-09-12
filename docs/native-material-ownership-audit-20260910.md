# Native material ownership: construction, relocation and updates

Read-only generated-code trace, September10,2026. This is implementation evidence,
not an implemented material-plan cache. Research reports in ../edf2027-analysis
list these functions as unclassified; conclusions below come from generated bodies.

## Confirmed boundaries

| Function | Observed behavior | Native ownership consequence |
| --- | --- | --- |
| 821BC230 (shard52) | Builds stage groups at instance+0/+36 through821BBAD8, textures through821BA120, copies pass+8 to instance+96, stores pass at+108. Returns0 on any builder failure,1 after completion. | Publish only after successful completion; partial construction cannot create a valid plan. |
| 821BD5B0 (shard41) | Sole direct caller of821BC230. Resizes owner+16 via821BD298, iterates20-byte pass records and112-byte instances. Does not branch on individual821BC230 return values. | An outer success result alone cannot prove every material was constructed successfully. |
| 821BD298 (shard37) | Allocates count*112+4, initializes elements, copies min(old count,new count) elements via821BD110, destroys old array via820ABF20(r4=3), replaces vector. | Instances can relocate; a constructor-only address cache is incomplete. |
| 821BD110 (shard50) | Copies two stage groups, local/global record vectors, textures, state and pass pointer into another112-byte instance using several helpers. Also called from shard17. | Copy/relocation must publish a destination generation. Do not reuse source data addresses without tracing helper fixups. |
| 820ABF20 (shard41) | With flag bit1 set, reads count at array-4 and traverses112-byte elements backwards, releasing their component vectors. | Retire array element identities before original destruction. Single-element branch and all callers still need examination. |
| 821BCC28 /821B9970 (shards72/12) | Find matching local named records and append record pointers to a temporary result list. | Lookup is not itself a parameter mutation. |
| 821BC430 (shard70) | Iterates result-list record pointers; copies min(requested count,record+8) float4s from entry r5 into data at record+4. Uses inline64-bit stores. | Values remain mutable; tracking memcpy alone misses this update path. |

Setter821BCD58 (shard21) and the scalar-setter body in shard18 call821BCC28
then821BC430, confirming the lookup-list consumer. The scalar callsites return
at821BCE5C/821BCE70.

## Effect on the replacement design

The current UploadParameters/UploadTextures still reconstruct records and resolve
names at activation. The target is native binding plans published at successful
construction AND copy/relocation, retired before destruction, with mutable values
updated separately. Native reflection destinations belong to the shader generation;
reloading a shader must not retain old resolved destinations.

Before implementing the producer hooks, inspect821BD110's stage-copy helper821BCF98
and record-copy helper821BC588: determine whether local data pointers are rebased
and whether source names remain independently owned. Trace the other copy caller,
single-element destruction and texture/global mutations. Until then neither raw
record addresses nor value buffers have a proven immutable lifetime contract.

This trace changes the immediate implementation scope: hooking821BC230 alone is
insufficient, even though it has only one direct caller. No material-value cache or
guest model-buffer comparison bypass has been enabled.

## Copy and update semantics verified (subsequent trace)

821BCF98 (shard45) allocates/resizes the stage's first vector through821BCA50.
Each16-byte entry retains its first word and copies its nested vector at+4 using
8211A2C0 (shard1). That helper resizes via8211A018 and copies16-byte value elements
with inline64-bit loads/stores. Separately,821BC588 (shard5) resizes the named-record
vector via821BC2D8 and copies all four words verbatim, including pointers. There is
no pointer rebasing in that helper or in the inspected821BD110 caller. Consequently,
do not assume a copied named record points into the destination's copied value
storage. The source pointer semantics must be preserved until any later fixup or
shared lifetime is established; this trace does not label the original behavior a bug.

The other821BD110 caller is821CA160 (shard17): resizes through821C9F98, then copies
source vector elements into destination elements in112-byte steps. Copy coverage
must include this path, not only material pass selection/resizing.

820ABF20's non-array branch calls820ABE88 (shard8) before optional scalar deallocation
when flag bit0 is set. The array branch inlines component destruction instead of
calling820ABE88 per element. Hooking only the scalar destructor would miss arrays.

Added extracted, unchanged821BC588 and821BC430 to the native-tail fixture. Six
record-copy cases cover counts0/1/3 and controlled resize success/failure, exact
record bytes including retained data pointers, guard bytes and return/nonvolatile
ABI. The resize provider is a fixture, not a test of the retail allocator. Eight
update cases cover empty/three-target lists and requested counts0/1/2/4 against
target extents0/1/2, exact inline value writes, preserved trailing bytes and ABI.
Targeted rebuilt test passed3.23s. These tests establish the original contract;
no native material-plan producer or activation consumer has been enabled yet.
Full existing suite subsequently passed18/18 in14.35s. The initial plain-shell
ctest invocation lacked the Visual Studio tool path; rerunning in the developer
environment completed successfully. Production executable was not rebuilt or changed.

## Native parameter metadata integration

NativeMaterialParameters now owns immutable names/register counts/register indices
for all four local/global vertex/pixel groups. Successful821BC230 construction and
completed821BD110 copying publish fresh destination metadata; old entries retire
before either operation. Scalar820ABE88 and array/scalar820ABF20 retire before
original destruction. Array retirement checks112-byte element alignment and extent
overflow. A failed constructor does not leave an older generation selected.

UploadParameters consumes these native groups instead of ReadNamedParameters.
Live reads still obtain local record+4 data pointers and global record+0 vector
descriptors, including their current data/count. This deliberately preserves the
retail copied-pointer semantics. Values are not immutable, copied or blindly
version-reused. There is no guest-metadata fallback for missing publication.
Texture records, reflection name lookup and active-vertex parameter list assembly
remain per-activation work: this is not yet a fully resolved native material plan.

Additional constructor trace: at821BC0A4..821BC114, local register index is located
in the stage's allocated range list; its data pointer is range.data plus16 times
the register offset. The resulting named record is appended at821BC170..821BC1DC.
The global record append821B94F8 has only the builder's direct callsite. This
establishes the construction metadata source, not arbitrary outside-API writes.

Tests cover four group separation, owned strings versus live local/global value
pointers, republish identity, rejected publication preservation, scalar and aligned
array retirement, retained generations, extent overflow and truncated global data.
First suite run terminated because the new missing-owner test caught runtime_error
but map::at threw out_of_range. Get now emits an explicit runtime_error diagnostic;
rebuilt tests pass18/18 in14.53s. The live executable was built before this
diagnostic-only correction and must be rebuilt after the live process is stopped.

Live PID9612, SHA256B61DA9106F7AA1C90D267D63A657BCCA3524F6E178407A9F4F4AE7B535007FDD,
native-material-parameters-20260910.log: normal early entry, untiled/vsync enabled,
audit/hook timings disabled, frame-time logging enabled, application cap0. Captured
and inspected native-material-parameters-world.png showing Mission1 player, squad,
city, mothership and HUD. Intro capture remains visibly overbright. At01:28:42.377,
2,650,000activations,zero misses/parameter errors/texture binding errors. Final
01:28:42.401:5,348,000indexed submissions,zeroerrors,723builds,379entries,noevictions.
No error/critical matches in this log. FPS t155/160/165s:56.4/56.5/54.7; profiling is
disabled unlike the earlier combined profile, so no attributed performance gain.
Stopped exact-path-validated PID9612,WaitForExit=true. Rebuilt production executable
successfully with diagnostic correction: SHA256
9B315979E1E831D3E275CE7911243A58C52103C52C137851FD093673704613F5.
No new live run of that diagnostic-only rebuild. Original copy/update routines and
native metadata storage have tests; the four new hook adapters have live integration
coverage but not yet extracted paired ABI tests. Whole-game producer coverage and
all shader reload/parameter mutation paths remain unproven.

## Resolved native constant destinations

ShaderBindings exposes opaque FloatRegisterBinding tokens containing a validated
reflected destination, required register extent and a shared shader-generation
identity. Tokens cannot be fabricated with arbitrary offsets. Default and foreign
tokens reject before mutation; optimized-out tokens retain ownership but return
false. ShaderBindings copying is disabled so two mutable buffer owners cannot share
a generation accidentally. Tokens surviving shader destruction cannot become valid
through address reuse. Source sizes remain checked on every upload; immutable
reflected packing validation moves to resolution.

RegisteredShader owns separate normal/reversed-depth parameter-plan caches. Keys
are weak identities of the native material metadata generation, not guest addresses.
Each group resolves once; failed resolution does not publish a partly ready group.
Expired material keys are pruned on a new-key miss; shader retirement/reload destroys
that shader's caches. UploadParameters now uses cached tokens for required global
extents and full constant uploads, eliminating those per-parameter name lookups.
Live values, byte dirty checks, partial instance patches, texture name bindings and
model vertex/index comparisons are unchanged. Cache hits still perform a native
material-identity lookup: this is not elimination of all comparisons.

Full build/all18tests passed12.52s. WARP packing/readback for scalar/vector arrays
and both matrix layouts now exercises resolved tokens. Foreign and retired shader
generations reject. Subsequent removal of the redundant per-upload immutable extent
check and default-token/wrong-size tests passed the rebuilt draw test0.18s; final
production relink/full-suite check still pending while the earlier live build runs.

Live resolved-constant candidate PID49760, SHA256
A8467E7D859D1CA82DE66B6099ADA8656F6358652314C9B9810AF8FB415AF07D,
native-resolved-constants-20260910.log. Early-entry script, untiled/vsync enabled,
hook timings/audit disabled, frame logging enabled,cap0. Inspected screenshot
native-resolved-constants-world.png shows Mission1 player/city/squad/mothership/HUD.
Final01:39:39.500:34,853,000indexed submissions,zeroerrors,723builds,379entries,
noevictions;11,610,000material activations,zero misses/parameter/texture errors.
Stopped exact-path-validated PID49760,WaitForExit=true.

Final relink failed twice with permission denied. Revalidation found another game
process PID54204 at the same executable path, started01:34:08; it was not launched
by this validation work and was left untouched. Its overlap also invalidates any
controlled performance inference from this run (observed t160..175s55.8..56.9FPS).
The disk executable remains the live-tested candidate; the removal of the redundant
immutable extent check is tested in the rebuilt draw-test target but not yet linked
into the game. Close the other instance before completing the production relink.

User subsequently closed the other instance. Revalidated PID54204 absent and no
edf2027 process, then full build/relink succeeded and18/18tests passed12.34s.
Final executable SHA256
2DA873B21003DF88FF71D3F77AF429A6C6C417CB8C8F3DB199AE5EE822ECFBBF.
This includes the resolved binding cache and removal of the redundant immutable
packing extent check. Live evidence above is for the preceding candidate with that
extra check still present. No further game was launched; no controlled FPS claim.

## Native texture-name ownership

Material generations now include two texture metadata groups. Publication copies
validated local/global names using the existing parser; activation uses those native
strings rather than reconstructing texture-record vectors/names. Local record+4/+8
handle/slot and global record node/value+28 handle plus slot remain live reads.
Sampler words still come from the device after original activation. Texture views,
validity and sampler state are not frozen. The same construction/copy/destruction
hooks govern both parameter and texture metadata; publication is transactional.

Inspected821BC8C0 (shard62): each local texture record copies all seven words without
rebasing. Builder821BA120 (shard48) appends local/global records through821B9590 and
821B9628. Native metadata retains destination record addresses after copying.
Tests cover local/global native names, changing live handles and slots, invalid
texture-count replacement preserving the old generation, node underflow and live
value truncation. All18tests passed12.08s. No live integration run for this change.

Another user game process PID260 (start01:43:33) locked the default executable.
Left it running. Added EDF2027_EXECUTABLE_NAME, a validated basename CMake cache
option defaulting toedf2027, and configured this build directory with
EDF2027_EXECUTABLE_NAME=edf2027-native-texture-metadata. Full build succeeded with
the candidate beside the unchanged default executable. This cache selection persists
until reconfigured; restore-DEDF2027_EXECUTABLE_NAME=edf2027 to update the default
after playtesting ends. No concurrent candidate run or FPS claim was made.

## Resolved texture and sampler destinations

ShaderBindings::ResourceBinding holds private reflected texture/sampler slots and
the same generation identity used by constant tokens. Resolution permits separate
texture-only/sampler-only names, combined legacy names and optimized-out resources.
Token setters reject default/foreign/retired generations before changing any state.
Named setters remain unchanged for other consumers; they do not acquire additional
lookups. Token setters still update owning ComPtrs and slot arrays, and Bind emits
the actual D3D state as before.

RegisteredShader's weak-material cache now includes two texture-plan groups.
UploadTextures resolves names on first use of a material/shader generation and
then uses tokens, preserving the combined-sampler mismatch error, optimized-out
handling, current texture handle/validity, current slot and sampler-word reads.
This removes material resource-name lookups, not all native map searches, data
dirty comparisons, instance parameter lookups, or model geometry comparisons.

WARP draw/pixel tests now bind separate texture/sampler names through tokens and
verify foreign/default/retired rejection without clearing live inputs. Legacy
combined bindings and optimized-out resources use the resolved path too. Full
build/all18tests passed13.37s. Candidateedf2027-native-material-bindings.exe SHA256
F0DC43E42D66996179B2CDDCDFF1644BE5E94C5721703F064E64569E691D4712.
Build-cache output basename is nowedf2027-native-material-bindings. User process
PID47872 was observed running the preceding texture-metadata candidate and left
untouched. No concurrent game launch; both texture-name and resource-plan changes
still lack assistant-verified live gameplay coverage. No performance gain claimed.

## User Mission1 playtest feedback

User reports gameplay is okay through Mission1: opening moments approximately
50..55FPS, then a solid fixed60FPS through the end of the mission. This is user
playtest evidence, not a frame-time capture or proof of all-game/handheld completion.
Last observed user process was PID47872 running the texture-metadata candidate;
no game process was present when this feedback was checked. The user did not name
the executable in the report, so do not attribute the result to the newer resolved
resource-binding candidate without confirmation.

Next performance comparison should isolate the opening-heavy interval from the
later sustained60FPS interval. Distinguish cold resource work from scene workload
and CPU submission versus GPU/presentation time; a startup-only slowdown does not
by itself prove loading, eDRAM overhead, or the engine's30FPS fallback. Geometry
content comparisons remain enabled, and brightness remains separately unresolved.

User confirmed that the completed Mission1 playtest was texture-metadata. Started
a hook-timed material-bindings profile as PID44112 with log
native-material-opening-profile-20260910.log (first01:51:47.713). Revalidation found
user PID41040 running the same candidate concurrently. Stopped only owned PID44112,
after exact-path validation,WaitForExit=true. Left PID41040 running. Abandoned this
profile before gameplay validation; overlapping timing samples are not a benchmark.

Added existing-cache vertex/index mismatch counters to NativeMeshCache and indexed
submission logging. They reuse existing comparison results: no second byte scan,
no changed invalidation or reuse policy. Vertex mismatch includes a changed source
extent; index mismatch includes changed index data/extent only after layout matches.
Generated native-index identity changes, layout changes and cold misses are not
classified as index byte mismatches. Short-circuit behavior means an index mismatch
does not also check/count vertex changes on the same draw. Counters are lifetime
cumulative, like builds/hits, and do not reset on Clear. Producer invalidation can
remove an entry before comparison, so zero mismatches is NOT proof of immutability.

Extended WARP mesh tests establish cold/hit exclusion, independent vertex/index
mutation classification and exclusion of index-width layout changes. Rebuilt quad
test passed0.22s. No diagnostic game binary relink or live run yet; the playtest
candidate remains untouched. These counters prepare the geometry ownership audit,
not removal of the comparisons themselves.

## Geometry diagnostic build and allocation trace

User subsequently tested material-bindings: Mission1 opening56..58FPS, then fixed60.
This is encouraging user feedback, not a controlled A/B timing result.
Built a separate edf2027-native-geometry-audit.exe, preserving both playtest binaries.
All18tests passed12.38s. SHA256:
B9C2671D169AB5F689AB457EDE521DBDBF86638C7487B47D55B0FA90D0B79EF0.
No user game was running before starting owned diagnostic PID31988 with hook timings,
normal untiled path, memory-watch audit disabled, and native-geometry-opening-20260910.log.

Read actual generated allocator bodies:821D4090(shard5) searches free list nodes,
aligns requests to16bytes, marks a fitting node occupied, or splits its free range
when more than request+256bytes remain. Split updates free-node address/size and
inserts an allocated descriptor through821D3EB0(shard1); it does not copy the
existing occupied geometry payload in this body.821D4380(shard15) creates a pool
list entry and calls821D3FF0(shard27), which obtains a new backing allocation via
8212FB98 and initializes a free-range descriptor. This narrows the allocation
trace; it does not establish that all other callers/writers preserve contents,
nor justify disabling guest model byte comparisons.

Implemented resolved partial-register patch tokens for per-instance constants.
The active vertex ranges carry normal/reversed generation-checked tokens copied
from the material plan; the instance loop no longer performs named reflection
lookups or recomputes type layouts. Source bytes/range validation and constant
dirty comparisons remain. Token copies still carry names during activation;
this is not removal of all activation allocations or geometry comparisons.
Named patch API delegates to token resolution. WARP tests cover resolved matrix,
vector/scalar-array patches, empty/end patches, optimized-out names and rejection
of default/foreign tokens plus overflowing ranges before visible mutation.
Full build/all18tests passed12.62s; candidateedf2027-native-instance-bindings.exe.
No live validation or FPS claim. Work paused for the user's20FPS report on the
separate post-centers candidate; do not attribute that report to this new binary.

While awaiting a visible-window performance log, extended resolved instance-patch
tests to reject retired shader generations, empty patches beyond the end (including
SIZE_MAX), and15-byte malformed payloads. The existing subsequent draw verifies
failed patches leave the constants unchanged. Signed-zero/NaN-payload/infinity
bit-preservation now exercises the resolved overload directly. Rebuilt only the
draw test: passed0.20s. No game executable changed or game launched; visible-run
log and active game process were absent at the initial check.

Diagnostic run reached visually verified mothership intro and street gameplay
(native-geometry-opening-20260910.png and native-geometry-gameplay-20260910.png).
Stopped only owned PID31988 after exact path validation and WaitForExit.
Final indexed sample:13,053,000submitted,0errors,723builds,379entries,
13,052,277hits,0vertex/index mismatches,0entry/budget evictions.
This observes stable cached bytes in this scripted run, not coverage of all writes
or active combat. Timing samples near195/200seconds were55.7/57.3FPS; later samples
dropped to14..18FPS around/after the capture/readback interval. Cause not diagnosed;
do not characterize this run as a clean steady-state performance benchmark.
User identifies a possible sky/sun light source for overbrightness. Intro screenshot
is washed out while street capture is darker. Sky contribution, bloom and exposure
remain hypotheses to isolate, not grounds for a global brightness multiplier.

## Cached active vertex binding ranges

Material/shader plans now own a shared immutable vertex-range vector containing
both normal and reversed binding tokens. Repeated activation shares this vector
instead of clearing/rebuilding it and copying names/tokens. Construction validates
all ranges before publishing. Activation resets its old reference first and only
publishes the new active reference after successful uploads/binds. Weak material
cache keys still allow retirement; an active reference keeps its list alive, while
shader-generation token checks reject stale bindings after shader replacement.
Live constant reads and dirty-byte comparisons remain unchanged.

Rebuilt edf2027-native-instance-bindings.exe; all 18 tests passed in 11.93 seconds.
SHA256: 95CAE4D4A9286040429435122AC75EB66F432533F0307132EA1DE498670506A9.
Existing tests exercise token ownership and partial patches, not this bridge-local
cache lifecycle directly. Gameplay validation remains pending; no FPS improvement
is claimed. The user-tested post-centers executable was not replaced.

Follow-up reload audit found that RegisterShaders replaced records without
invalidating the linked handle pair. Identical reused handles could skip link
validation and retain an active range list from the previous shader generation.
Replacement now invalidates active VS, linked VS/PS, and the shared range list
before registry mutation, including cross-owner handle reuse. Fresh unrelated
registrations preserve active state. ForgetOwner and failed instance patching
also release the inactive range list. Mesh Acquire already compares owned
bytecode identity, with reload coverage in native_quad_tests, so no additional
blanket mesh flush was added to registration.

Rebuilt instance-bindings candidate; all 18 tests passed in 12.01 seconds.
Current SHA256: 8DD952646C5A765566957AFA793E310A4A57371725DE067343D0333390493FE7.
The suite covers mesh reload and token rejection, but not a live effect reload
through RegisterShaders. No game launched and no performance claim made.

## Instance-bindings runtime smoke test

Ran current candidate SHA256
46E326A5044C3E867E2DED47B10BD68E9E0C9981EBEE4B8088D087F298109A3C
as owned PID45284, 03:10:22 through 03:13:11. Log:
out/native-bridge-run/native-instance-validation-20260910-031022.log.
Enabled the post-center experiment, used scripted Mission 1 confirmations with
no movement/firing. Inspected instance-validation-intro-031022.png and
instance-validation-gameplay-031022.png: mothership and street gameplay rendered.
Per-instance g_mWorld patches executed. Last indexed sample: 4,910,000 submitted,
zero indexed errors, 721 builds, 4,909,279 hits, zero VB/IB mismatches. Last
activation sample: 2,490,000 activations, zero misses/parameter/texture errors.
This covers routine activation, not a live same-address shader reload or combat.
Stopped only the exact-path-validated owned PID and waited for exit.

Important diagnostic flaw: the command's empty --edf_native_scene_capture=
consumed the following --edf_native_output_capture_limit=0 as its filename
prefix. Correspondingly named BMP files exist in the executable directory and
the log reports existing capture-path collisions for movie/font/output captures.
Do not label this run capture-free or error-free overall. Native host also
reported DXGI occlusion; no visible performance conclusion is supported.
Prior off/on automated commands with the same empty-option sequence must not
be treated as capture-disabled baselines, even if intended that way. This does
not establish the cause of the user's separate 20 FPS report.

Added tools/start-native-binding-validation.ps1 with a unique fresh user/log
directory and no empty capture option, preserving the empty default. It restores
input environment variables after launch and refuses a competing EDF process.
PowerShell syntax validation passed; corrected-launch runtime verification is
still pending. No capture artifacts were deleted or overwritten by this work.

## Corrected-launch verification

Executed start-native-binding-validation.ps1 -IntegerPostCenters unchanged.
Owned PID62844 ran 03:14:33 through 03:17:38. Artifacts are under
out/native-bridge-run/binding-validation-20260910-031433-fdea5399/.
Inspected gameplay.png: street gameplay, character, HUD and world render.
Across the run's log files there are zero [error] lines. No BMP in the executable
directory was created/modified after launch; the sole deliberate window capture
is gameplay.png in the isolated run directory. The empty-capture-argument fix
has now passed runtime smoke verification.

Final indexed sample: 7,235,000 submitted, zero errors, 723 mesh builds,
7,234,277 hits, 379 entries, zero evictions and zero vertex/index mismatches.
Final material sample: 3,280,000 activations, zero misses/parameter/texture errors.
The g_mWorld per-instance token path executed. Stopped only exact-path-validated
PID62844 and waited for exit. Candidate binary was unchanged from the preceding
run (46E326A5...8109A3C).

Scope remains scripted startup/opening and stationary gameplay, not active
combat, full mission completion or shader reload. Hidden-host DXGI occlusion
still prevents a visible FPS claim. Stable bytes here do not justify removing
remaining geometry comparisons. No additional GPU dependency was removed by
this validation-only step.

## Native font binding ownership

Added NativeFontBindings: six constant tokens and the atlas/sampler tokens are
resolved once with the font shader pair, rather than resolving names/type layouts
on each glyph draw. Live register blocks, atlas and sampler state are still read
per draw. Exact 64/32-byte block validation and both shader-generation checks
precede constant mutation. Resource setters retain missing-binding errors.
The bridge publishes the plan alongside the newly created shader pair.

native_xui_tests now runs its recovered-font RGBA readbacks through this actual
plan, including nonzero unused register lanes, alpha threshold and three mask
values. Added stale pixel-generation and malformed vertex-block rejection tests
that verify existing vertex constants remain unchanged.

Full build and all 18 tests passed in 11.88 seconds. Current instance-bindings
candidate SHA256: 4EB90DA7F4F4B522BF6BBA58D24AFFFF87B7E6436904E843954805A9642EE34D.
The prior clean runtime test predates this font change; no new gameplay validation
or FPS improvement is claimed. Constant dirty-byte checks and geometry source
comparisons remain; this removes name resolution, not those comparisons.

## Native XUI vertex binding ownership

Added NativeXuiVertexBindings for the six fixed vertex constants. Normal and
reversed-depth shader generations have separate plans published before their
shader becomes active. Per-draw code reads the live 192-byte register block and
16-byte arithmetic bias, then uses resolved tokens without named reflection.
Both block extents and generation identity are checked before mutation.

The native XUI test now exercises this production plan for both depth variants
and all three existing depth-state cases, retaining pixel visibility checks.
Malformed blocks and a foreign shader are rejected. Full build/all 18 tests
passed in 11.99 seconds. Current candidate SHA256:
6163508BEAE9F78DFFA58D5DB6C3B93BE0AE5D55FD656A154CA397079F921577.
No gameplay run of this change yet. XUI pixel constants/resources and movie
bindings still use names; neither geometry comparisons nor FPS changed by claim.

## Native XUI pixel binding ownership

Textured, solid and alpha-mask variants now each publish a NativeXuiPixelBindings
plan with their shader generation. ColorFactor, the solid BrushColor, and the
textured variants' BrushTexture/BrushSampler use resolved tokens per draw. Solid
draws do not read a texture or supply an unused brush block to textured shaders.
Register extents and shader identity are validated before constant mutation;
solid plans reject texture/sampler setters rather than silently accepting them.

Pixel readback tests now cover all three variants through this production plan.
Tests also reject malformed register sizes, missing solid brush data, foreign
shader constants/resources and solid texture/sampler calls. All 18 tests passed
in 12.43 seconds after a full build. Current instance-bindings SHA256:
D333F1F1861936DBFCBA3AB90A234FF2411B0869E8CBA6D280A827ADB88C6F1F.
No game run of the recent font/XUI changes yet. Movie bindings still use names;
constant dirty-byte checks and geometry comparisons remain. No FPS gain claimed.

## Native movie binding ownership

Both HD and SD variants now own NativeMovieBindings plans, selected by the
actual bound guest shader as before. Four vertex constants, the pixel color
factor, and all three plane/sampler pairs use resolved tokens. The live decoded
texture lookup, format validation, sampler state, and per-draw resource clearing
remain unchanged. Exact 160/16-byte blocks and both shader generations are checked
before constant mutation; out-of-range planes and stale resources are rejected.

Movie tests exercise this production plan for both coefficient variants, neutral
black, chroma/tint/alpha, transform changes and spatial YUV patterns. Added block,
generation and plane-index rejection tests. Initial test run failed because a
new assertion incorrectly used the scalar/vector diagnostic getter on a float4
array; changed the assertion to the Params vector and a changed Params input.
The rebuilt full suite then passed all 18 tests in 12.03 seconds.
Current candidate SHA256:
0BBCB04B875510B2258F8EFFC5B637D61C55960D94930938C0EA451D883996C5.
The recent font, UI and movie changes still need a combined gameplay smoke run.
No FPS improvement or geometry comparison removal is claimed.

## Combined font/UI/movie runtime verification

Current candidate 0BBCB04B...883996C5 ran as owned PID33600 from 03:29:17
through 03:32:27, using the corrected launcher with IntegerPostCenters.
Artifacts: out/native-bridge-run/binding-validation-20260910-032917-a939bece/.
Inspected menu.png (mission/difficulty selection and readable text) and
gameplay.png (street, character, HUD). The log records HD PS_Movie draws,
font draws, XUI draws and per-instance gameplay constants without errors.
SD conversion remains unit-tested only, not observed in this startup movie.

All run log files have zero [error] lines, and no automatic BMP captures were
created/modified in the executable directory. Final indexed sample: 7,915,000
submitted, zero errors, 723 builds, 7,914,277 hits, 379 entries, no evictions,
zero VB/IB mismatches. Final material sample: 3,510,000 activations, zero
misses/parameter/texture errors. Stopped only exact-path-validated PID33600
and waited for exit. No binary changed during this validation.

This supplies combined runtime smoke coverage for the recent fixed binding
plans. It does not cover active combat, full missions, all UI states, same-address
shader reload, original-console brightness equivalence, or visible performance.
DXGI occlusion was reported. Remaining geometry comparisons are still necessary
until ownership of all relevant writes is established.
