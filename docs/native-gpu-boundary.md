# Native GPU replacement boundaries

Living audit, September 9, 2026. The normal Windows route now uses native D3D11
without loading Xenos. Recompiled gameplay, native submission/completion and
several CPU-only draw/state tails are implemented; full replacement is not yet
verified. The initial audit sections below describe the original dependencies.
Later dated sections record replacements, tests and their deployment status.

## Current default: native render-state ownership (September 10)

### Native-only scene lifecycle follow-up

The scene begin/end hooks no longer contain calls to original Xbox tiling
routines 821409A0/82140E98. Previously `edf_native_untiled_scene=false` could
select those original paths even in the native-only port. The setting is now
deprecated and inert: startup logs and normalizes a legacy false value to true.
Viewport routing no longer consults that setting. Native begin/end caller and
pairing checks remain; unknown callers fail rather than silently record tiles.
Other CPU graphics helpers and generated original code still exist; removing
these two fallback call edges does not establish full replacement.

Built `edf2027-native-scene-only.exe`, SHA256
`C2BE8C94C616DC072558C6079EEF7E03A54170839B2DDDF831F17CFE104DB6B7`.
All 18 tests passed in 12.19 seconds. The validation launcher supports an explicit
`-UntiledScene:$false` compatibility probe; omission leaves the executable default.

The packaged PE audit passed (no Xenos import/staged DLL). Compatibility run
`binding-validation-20260910-154006-f20053e9`, PID 23336, explicitly passed false.
Startup logged its migration at 15:40:07.042 and native untiled viewport setup at
15:40:08.553 and subsequently. The run reached the mission-1 mothership sequence;
`mission-intro.png` shows the scene and HUD with 120/120 ammunition. The process
was stopped after exact executable-path verification, before scripted movement
and firing. This verifies startup compatibility and a bounded scene-lifecycle
sample, not complete mission coverage or a performance/brightness improvement.

The default is now `edf_native_owned_render_state=true`, audit OFF. This enables
the native packed render words, scissor-enable and blend-factor consumers below
without a launch flag. Explicit false remains available for regression diagnosis.
The validation launcher now omits the ownership argument unless explicitly set,
so fresh-profile runs test the executable default rather than a forced value.
This promotion follows scripted gameplay in native and audited modes, a 4x MSAA
gameplay sample, GPU blend-factor readback tests and actual retail-setter fixtures.
It is not a claim of all-content writer coverage or full Xenos replacement.
Geometry comparisons, other guest reads and broader fidelity/performance work
remain. Earlier OFF-by-default statements below describe the rollout chronology.

Default build: `edf2027-native-state-defaults.exe`, SHA256
`9C778D64E343BD3E9A645335E01FEB8C81047D75A7A743F97D90CC2862CA88FE`.
All 18 tests passed in 12.56 seconds; its packaged PE audit passed with no Xenos
import or staged DLL. The first validation run (43460, directory
`binding-validation-20260910-143232-28d9f6a1`) ended near startup during an
interrupted turn; it does not establish gameplay validation. Its handle was
confirmed absent before starting replacement run 23948 in
`binding-validation-20260910-144826-8153c0b1`.

The replacement run used a fresh profile and no ownership override. Startup
confirmed owned=true, audit=false. It reached gameplay and completed scripted
movement/fire; captures show a changed position and ammo 120/120 then 100/120.
No incomplete ownership or publication failures were logged. Nested map bodies
took 3712.7375 and 3735.7656 ms. The process loaded System32 D3D11 with no
Xenos-named module in the inspected list, and was stopped by exact owned-path
check after validation. This is a bounded regression test, not full-game or
handheld performance certification.

## September10 follow-up: native render-state ownership target

At the start of this audit, draw consumers called ReadRenderStateWords in guest_draw_state.h:
blend=device+10424, depth=+10420, raster=+10440, alpha=+10428,
write-mask=(+10332)&15, scissor-enable=(+11584)!=0. XUI's combined reader also
reads these fields. They are CPU graphics state, not an active Xenos command
processor, but transferring them to setter-owned native state would remove
remaining per-draw guest reads and validation calls at this boundary.

A direct literal-offset generated-store inventory finds39 function bodies and
43 stores across these six fields. Examples include82135108(blend),
82135630(depth),82134EE8(raster),821364C8(alpha),82135BB0(mask),
82137978(scissor), plus8214EFF8 and82137CB8 helper writes. 82137978 stores
the incoming enable then tail-calls821370E0 with device+12400; a replacement
must preserve that viewport/scissor update, not skip the tail call.

This is a starting inventory, not complete writer coverage: indexed/computed
stores, bulk initialization/reset and alias writes are outside the search.
Before substituting a native snapshot, audit these paths and device retirement,
preserve every setter's CPU return/dirty-mask behavior, and test all affected
indexed/movie/UI/immediate/post consumers. No render-state reader was bypassed
by this audit. The latest non-observer candidate and validation provenance are
in native-geometry-writer-audit-20260910.md.

### Render-state snapshot integration (diagnostic, not read bypass)

The bridge now publishes six-word device snapshots after the inventoried
setters, the native device-default initializer and the dispatch-table initializer.
The depth-target producer shares its existing timing hook. Indexed, movie,
XUI/font and immediate consumers compare their existing live reads when
`edf_native_render_state_audit=true`; normal runs leave this diagnostic off.
Missing or mismatched consumer state never seeds or repairs a snapshot.
The original snapshot integration retired state on command reset; the later
field-specific follow-up below corrects this overly broad lifetime boundary.

The snapshot tests cover missing ownership, independent devices, all six changed
fields, repeated mismatches without repair, retirement/reuse and invalid owners.
These tests do not establish complete generated-writer or device-lifetime
coverage. Computed/indirect writers and concurrency still need verification
before replacing the live readers. The validation launcher exposes
`-RenderStateAudit` explicitly; this is not a performance configuration.

Validation: `edf2027-native-render-state-audit.exe`, SHA256
`DA293585073DE0045579661D7EB6E976B035025D25067F650C9315A9D5E0BAF1`,
compiled with scalar-store observation OFF. All 18 tests passed (12.56 seconds);
the packaged PE dependency audit passed with no Xenos import or staged DLL.
Owned runtime PID 34620 used audit ON, run directory
`out/native-bridge-run/binding-validation-20260910-134616-455a0dd1`.
It reached the intro, gameplay and scripted movement/fire; the later capture
shows a changed position and ammunition 100/120 (not an instantaneous muzzle
flash capture). The 16,777,216-check checkpoint reported zero missing snapshots
and mismatches; no mismatch or publication-failure lines were found afterward.
System32 D3D11 was loaded, with no Xenos-named module in the inspected module
list. Nested map.820B4D58 bodies took 3879.2133 and 3752.845 ms. This hidden
diagnostic is not an FPS benchmark. The intro capture remains visibly
overexposed. The exact owned process was stopped after validation.

Full-snapshot publication can absorb a prior unhooked field write when another
setter subsequently runs. Consequently, even zero runtime mismatches cannot
prove every field writer was intercepted. Field-specific producer coverage,
device retirement and concurrency remain prerequisites for removing live reads.

### Field-specific ownership follow-up

Replaced whole-state setter publication with field-specific updates and per-field
producer provenance. An independent literal-store inventory matched all 43 stores
in 39 generated function bodies to the catalog. The table-initializer catch-all
publication was removed; only its actual nested setters publish. Tests now verify
partial validity, reuse without inherited valid fields, and that publishing depth
cannot absorb an untracked blend mutation. The first field-only build passed all
18 tests in 13.15 seconds.

This stricter audit exposed a previously hidden initialization gap: owned run
45724 in `binding-validation-20260910-135314-0996c03e` reported 20,863 early
draw checks with valid mask 0x1f (scissor-enable missing), then no further missing
checks through the 1,048,576 checkpoint. That run was stopped after confirming
the gap, not claimed as full gameplay validation.

Generated 82139A40 calls 82139638 at return address 82139A70 with size 20480
and alignment 128. 82139638 zero-fills successful allocations before returning.
A hook at this exact allocation contract now publishes known zeros before either
device initializer runs, without sampling guest state. Final Release 82139760
(entry reference count at device+52 equals 1) retires ownership before destruction;
non-final Release preserves it. The allocation-hook candidate passed all 18 tests
in 11.91 seconds. Owned startup run 25692 in
`binding-validation-20260910-135548-fb80ae10` confirmed zero-publication executed,
but still reported 20,865 missing checks: command-reset tracking immediately
discarded the allocation snapshot. D298 replaces ring/completion storage, not
these six device fields. Removed that erroneous retirement; allocation/final
Release now delimit snapshot lifetime. Run 25692 was stopped after this diagnosis.
The corrected command-reset candidate passed all 18 tests in 12.16 seconds.
Runtime validation used `edf2027-native-render-field-audit.exe`, SHA256
`0BE1D9663A5CDA0766A56711DD20E77F01FFE3FB117B53EF5381BEB2CAADD2F8`,
scalar-store observation OFF, render-state audit ON. Owned PID 6212 ran from
13:58:15 through the scripted movement/fire segment, with logs/captures under
`out/native-bridge-run/binding-validation-20260910-135815-2333c7c0`.
Startup, intro and gameplay had zero missing fields and mismatches at recorded
checkpoints. Gameplay capture shows 120/120 ammunition; firing capture shows
107/120, muzzle flash/casings, changed camera/position and an enemy particle
effect. Nested map.820B4D58 bodies measured 3789.8529 and 3706.9559 ms.
The inspected process loaded System32 D3D11 and no Xenos-named module. The
exact owned process was stopped after validation. This is a bounded runtime
correctness sample, not an FPS benchmark, all-mission test, or proof of every
final-release/failure branch. Live draw-state reads have not been removed.
Field-specific publication still reads the complete packed word owned by a setter;
it does not yet prove bit-specific or indirect writer coverage. Live draw reads
remain authoritative.

### Opt-in native render-state consumption

`edf_native_owned_render_state=true` now selects setter-owned words for the
indexed, movie, UI/font and immediate render-state keys. It defaults OFF pending
broader validation. The validation launcher exposes `-OwnedRenderState`.
With audit OFF, the resolver returns the owned array without invoking its lazy
guest reader. Missing/incomplete ownership throws; there is no automatic fallback.
With audit ON, it independently reads and compares live state, logs provenance
and rejects a mismatch instead of selecting the live array.

The owned XUI reader takes render/scissor-enable from that array and reads only
the later surface/texture/viewport block at device+12168..12415. Caller read-window
validation and other viewport/resource readers still exist; this is not removal
of every guest-memory validation in a draw. Setters still publish their packed
field after the original CPU setter; geometry byte comparisons are unchanged.

All 18 tests passed in 12.13 seconds. New negative-control tests prove that the
owned non-audit resolver never invokes the live reader, missing ownership never
falls back, and audit disagreement throws. An XUI reader fixture forbids all
access before device+12168: the owned overload succeeds with deliberately
different native render/scissor values while the legacy reader is rejected.
Runtime validation used `edf2027-native-owned-render-state.exe`, SHA256
`AEDE153B93E0889244D1F18AE0F92E393D2FBBC14477A9AEA2815F3056647277`,
with `owned=true, audit=false` confirmed in startup logs. Owned PID 42244 ran
14:05:23..14:09:23, directory
`out/native-bridge-run/binding-validation-20260910-140523-80dee89f`.
The intro, gameplay, movement and firing rendered. Gameplay capture shows
67/120 ammunition and after-input capture 102/120 with a changed camera/position.
The final indexed checkpoint reported 19,276,000 submissions, 766 mesh builds,
427 entries and zero errors, source mismatches or evictions. No incomplete
ownership, audit-mismatch or publication-failure errors were found. This does
not claim zero render-state mismatches: that comparison was deliberately OFF.
Nested map.820B4D58 bodies measured 3722.7242 and 3651.8554 ms. The inspected
process loaded System32 D3D11 and no Xenos-named module. The exact owned process
was stopped afterward. This is not a visible FPS benchmark or full-game test.
The intro still appears overexposed; default consumption remains unchanged until
broader validation.

The same executable was also run with owned+audit both ON, owned PID 38084,
directory `binding-validation-20260910-141008-2eb0dd35`, 14:10:08..14:13:41.
It reached gameplay and scripted movement/fire (later capture shows 100/120
ammunition, a changed position and enemy particle effects). The 16,777,216-check
checkpoint reported zero missing fields and mismatches. No ownership-incomplete,
audit-mismatch or publication-failure errors were found. The exact owned process
was stopped. Compilation overlapped this run, so its timings are not a performance
comparison. This validates the combined mode on the scripted path, not all content.

### Duplicate viewport scissor read removal

The draw viewport helper still read scissor-enable at device+11584 independently
of the owned render key. The opt-in path now passes owned scissor-enable to a
viewport reader that reads only the ten viewport/scissor-coordinate words at
device+12376. Scene binding, indexed, movie and immediate draw callers use it;
setter/clear diagnostics retain their separate original CPU reads. A reader test
forbids access below device+12376: the owned overload succeeds and the original
fails, proving that the extra scissor-enable read is omitted. All 18 tests passed
in 12.19 seconds. The native state option remains OFF by default.

Runtime: `edf2027-native-owned-scissor.exe`, SHA256
`5BBA450D11C6479A6720ADA9DA741430647175B76C9354F76B9CAAA59CECD69D`,
owned state ON, audit OFF, MSAA 4. Owned PID 37528 ran 14:14:37..14:18:19,
directory `binding-validation-20260910-141437-ca552479`. Logs confirm the native
1280x720 scene allocated with samples=4. Gameplay and movement/fire captures
show a changed camera/position and ammunition dropping from 120/120 to 100/120.
The final indexed checkpoint had 16,608,000 submissions, 765 builds, 426 entries,
zero errors/source mismatches/evictions. No incomplete ownership or publication
failure was logged. Nested map bodies measured 3722.3293 and 3658.4356 ms.
The exact owned process was stopped. This extends 4x coverage beyond the earlier
title-screen test, but does not prove full-mission completion, visual equivalence,
handheld performance or interactive settings persistence.

### Blend-factor ownership

The separate float RGBA block at device+10336..10351 was still read whenever
a native blend state required a constant factor. Literal generated-store search
found its four stfs writes in 82135418 only. The original setter unpacks packed
color channels, converts/scales them using the retail float constant and updates
the device dirty mask. Its behavior remains unchanged; a hook now publishes the
completed four word bit patterns once per setter, not once per draw.

Blend ownership shares the device allocation/final-release lifetime but has
independent validity, producer and audit counters. Zero-filled device allocation
publishes zero blend words. Updating packed render fields does not replace blend
ownership, and vice versa. The common BindGuestRenderState helper used by indexed,
movie, UI/font and immediate paths consumes the native blend words when owned
state is enabled; audit mode independently compares the original word bits.
Missing ownership or an owned-mode audit mismatch throws without fallback.

Tests cover exact words including signed zero/NaN payloads, four channel changes
without audit repair, device isolation and retirement/reuse. All 18 tests passed
in 11.94 seconds. The option remains OFF by default. Computed/bulk writers,
concurrency and broader content remain audit gaps; this direct-store inventory
does not establish their absence.

The candidate SHA256 is
`FF52E0ABCEEC66D3CCA9301D719C13F26C8A45F8FA14F15549748CBC0ABD1188`.
Owned PID 36084 ran with owned+audit ON, directory
`binding-validation-20260910-142308-7d05b7e5`, through startup, intro, movement
and firing. Gameplay capture shows 14/120 ammunition and a changed position.
The packed-state audit reached 33,554,432 checks with zero missing/mismatches,
but **no constant-blend audit calls were logged**. Therefore this run is regression
evidence for the common paths, not execution coverage of the blend-factor consumer.
The exact owned process was stopped after the script.

Added a focused WARP D3D11 pixel-readback test: publish owned RGBA bit patterns,
bind native constant-color and constant-alpha blend modes, draw, and check output
channels against the expected factors. Two factor updates per mode verify that
the renderer does not keep a prior value. The focused test passed (0.23 seconds),
then all 18 tests passed (13.10 seconds). This proves owned-factor storage and
D3D11 binding/output for those cases, not that the game exercised that branch or
that every retail packed-color setter input was differentially tested.

Follow-up: the fixture now extracts actual generated 82135418. 4,096 cases cover
each channel's 256 byte values in a mixed packed color across four controlled
scale values (0, 0.5, 1, 1/255). Expected channel conversion is independently
computed; the entire 20,480-byte device is checked for exactly the four float
stores and the dirty-mask OR at +24. Tests also check the four stack scratch
values, input/stack/LR preservation and one nonvolatile register, then publish
the completed words and verify native ownership against the expected result.
The focused immediate-tail suite passed in 3.46 seconds. This is actual setter
coverage, but not exhaustive over all 2^32 packed colors, every floating-point
environment, or the bridge hook's locking/dispatch. The fixture controls the
scale at 0x8200964c; it does not claim to verify the shipped image's constant.

### Worker-initialization packet removal

Following the initialization callers exposed an actual remaining Xbox packet
producer: sub_821477A8 calls sub_8214EE50 before sub_8214EFF8. EE50 initializes
worker records, emits a four-word packet beginning0xC0025800 for each selected
worker, and creates/prioritizes its CPU thread. It is not the render-state default
table initializer; sub_8213C0B8 likewise only updates command cursor/capacity
and a byte flag, not the six render-state fields. The render-state writer audit
therefore remains incomplete and no native render-state snapshot was substituted.

Added a fingerprint-pinned extraction of EE50. Native host mode now uses that
CPU-only initializer: no command cursor/capacity read, rollover call, four packet
stores or cursor publication. All worker records, arithmetic, thread arguments,
priority calls, partial-failure return paths and ABI epilogue remain from the
original generated body. Existing native worker command/signalling paths remain
responsible for worker progress. No packet decoder was introduced. The legacy
non-native-host branch retains the original function.

96 paired differential cases compare actual original and extracted bodies using
controlled thread/priority providers: six masks, four failure positions, with
and without overflow, with and without the skip flag. Checks cover expected
worker counts/failure returns, exact thread/priority arguments, all CPU arena
bytes outside the removed packet effects, stack/LR and nonvolatile registers.
Native packet memory/cursor stay untouched and native rollover count is zero;
the retail negative control writes packets and performs expected rollovers.
The fixture does not start real threads or validate live native worker progress.

All18 tests passed11.99s. Added explicit mask/failure-count assertions afterward;
the rebuilt immediate-tail suite passed3.42s. Final rebuild only generalized the
packet log wording and corrected the generated-store endpoint comment.
Candidate `out/build/win-native-clean/edf2027-native-worker-init.exe`, generated
store observer OFF. Runtime validation remains pending; no game is left running.

### Worker initializer runtime / default-state packet tail

Validated worker-init SHA690260AE...A6B95FD9 as owned PID58372,
09:01:04..09:05:07, run
`out/native-bridge-run/binding-validation-20260910-090104-0f25bc63`.
The native initializer log executes and returns1. Both nested map loads finish
(3802.0318/3745.272ms); firing-check.png actually captures the reload animation,
and after-input.png shows100/120 ammunition with city/enemy effects. Last indexed
sample19,047,000 draws,765 builds,426 entries, zero indexed errors/mismatches/
evictions; index reuse745/rejections0. Stopped only this exact-path-validated
process after the scripted input ended. The old log did not expose the skip flag
or worker mask: this proves gameplay with the candidate, not execution of every
worker-creation branch. The next build logs those entry flags explicitly.

The adjacent sub_8214EFF8 also had a trailing packet-only block: it emits72 or80
bytes depending on a fixed Xbox setup global, then advances device+40. Added
fingerprint-pinned tools/extract-native-device-defaults.cmake. Native host mode
uses the CPU prefix and original epilogue, preserving descriptor loops, default
state, all five dirty masks, and the interleaved device+10788 write (14). The
original final r3=3650 is retained. Command reservation/rollover, packet writes,
cursor publication and three packet-only fixed-global reads are omitted.

16 paired actual-original/native cases vary initial word patterns, overflow and
optional packet flag. They compare CPU memory/stack outside removed packet bytes
and cursor, return/LR/nonvolatile registers, untouched native command memory and
zero native rollovers. Retail negative controls verify exact72/80-byte emission
and overflow handling. Native uses a low-only arena, so it cannot depend on the
retail fixed-global mapping. All18 tests passed12.96s; after adding worker-entry
flag logging, the rebuilt immediate-tail suite passed3.66s.

Built `out/build/win-native-clean/edf2027-native-device-defaults.exe`, observer
OFF. This includes both worker-init and default-state replacements. Its runtime
validation is pending and no game is left running. The render-state ownership
audit remains separate and incomplete; no draw-state read was bypassed here.

### Packet-free default-state runtime

Validated final device-defaults SHA256
`E9229B8871C53F7B4604DC98B60690E67E26E16E7E47DC4B9555649DB4ACF12A`
as owned PID44756, started09:06:31, run
`out/native-bridge-run/binding-validation-20260910-090631-14ba1228`.
Entry logging confirms flags0x02000001, requested worker mask0x2, skipped=false,
result1, followed by the native default-state log. The exercised workload therefore
does not merely take EE50's no-worker skip branch. Map.820B4D58 inclusive loads
3774.9646/3705.5973ms complete. Inspected firing-check.png (reload animation,
not a muzzle-flash capture) and after-input.png. This remains a bounded hidden
startup/gameplay test, not a visible FPS benchmark or all-worker-mask proof.

Further render-state lifecycle audit: sub_82139A40 allocates20480 bytes through
82139638, which zero-fills the successful allocation through821E9BA0. Its type2
branch calls821479B0; the other branch calls821477A8. Both call8214EFF8 then
821470A8. Thus the second EFF8 caller identified this turn is alternate device
creation, not evidence of a reset caller. Reset/recreation coverage still needs
its own audit.

821470A8 initializes97 device setter entries from the table at0x82552518,
installing function/metadata words at device+56/+524 and calling each setter
indirectly with the table's default argument. Its direct indexed stores in this
loop are dispatch metadata, not writes to the six render words. A second table
initializes texture-related setters. This establishes another required publication
boundary and explains why a literal-offset store search alone is insufficient.
The39 direct-writer inventory is not yet an exhaustive native render-state contract.
No renderer source code or executable changed during this runtime/audit turn.

Final indexed sample21,616,000 draws,765 builds,426 entries, zero indexed errors,
source mismatches or cache evictions; published index reuse745/rejections0.
after-input.png shows100/120 ammunition and enemies in the street. Module
inspection finds System32 d3d11.dll and no Xenos-named module. Stopped only the
exact-path-validated owned PID44756 after the script ended; no game remains live.

## Startup and presentation

| Guest function | Observed dependency | Native replacement obligation |
| --- | --- | --- |
| `8213D298` | Allocates/zeros device writeback structures; calls `VdInitializeRingBuffer` at `8213D46C` and `VdEnableRingBufferRPtrWriteBack` at `8213D4A4`. Writeback objects are stored at device+10768 and +10772. | Preserve required CPU allocations/ownership while replacing command-ring setup and progress with native submission bookkeeping. Do not delete the whole initializer without accounting for its other writes. |
| `821476A8` | Registers callback `8213BEC0` with the device as user data at `8214771C`. | Supply native frame/completion notification semantics. The original callback is not independent of GPU MMIO. |
| `82147A10` | Unregisters the interrupt callback at `82147B44`, then calls `8213D298` during teardown. | Stop native callbacks before releasing device-owned state; verify teardown/recreation. |
| `82151460` | Calls `VdSwap` at `821515EC`, after reserving command space via `8213D160`. Advances device+40 afterward. | Present the native output and preserve relevant CPU-side presentation state without emitting or interpreting Xenos packets. |

Evidence: generated `edf2017_recomp.38.cpp`, `.40.cpp`, `.62.cpp`, `.26.cpp`.
SDK `src/kernel/xboxkrnl/xboxkrnl_video.cpp` shows that `VdSwap` constructs a
texture-fetch packet and `PM4_XE_SWAP`, followed by NOP packets. Its other
startup exports forward ring/interrupt services to `IGraphicsSystem`. Merely
clearing the plugin selection does not replace these guest paths.

## Interrupt side effects

`8213BEC0` has two distinct branches:

- Argument0 reads bit0 at guest MMIO `7FC86544`. If set, it increments
  device+15124, updates countdown/frame state at +15132/+15128, may clear
  writeback-object+4, and invokes an optional callback at device+15120 with
  a three-word stack record including +15124 and +15136.
- Argument1 reads callback/data fields +16/+20 of the object at device+10772,
  handles a sentinel failure, and clears a per-CPU bit in that object's first
  word under synchronization associated with device+10776.

The native backend cannot simply invoke the original argument0 branch without
its MMIO prerequisite. Nor can both branches be collapsed into an unconditional
frame-counter increment. Callback threading/reentrancy and wait signaling remain
to be verified. Evidence: `edf2017_recomp.40.cpp`, function `8213BEC0`.

## Submission and completion

`8213D160` checks requested command words against device+40/+44, calls
`8213CF60` to flush when space is insufficient, then `8213CC20` if necessary.
`8213CF60` includes additional submission work through `8213CDC0`; it is not
just a pointer reset. `8213CDC0` has recording and immediate branches, writes
submission descriptors, and updates device+13508/+40 with alignment and rollover.
Evidence: generated `.42.cpp`, `.52.cpp`, `.22.cpp`.

`8213C928` tests a requested completion value against:

- issued counter: device+10780;
- completed counter: word0 of the object pointed to by device+10768.

The unsigned predicate for entering/remain-in-wait is:

```cpp
target != 0 && uint32_t(issued - target) < uint32_t(issued - completed)
```

When target equals issued and device+12944 is zero, it may flush before checking
again. Its wait lifecycle uses `821394D8`, `82139688`, and `82139508`.
Evidence: `edf2017_recomp.39.cpp`, function `8213C928`.

This identifies a required semantic boundary, not permission to advance all
counters immediately. Native completion must correspond to submitted D3D11
work finishing and preserve ordering/wraparound and resource lifetime. The
producer(s) of issued values, all consumers and wait-helper side effects still
need auditing before replacement. No command-packet parser belongs in the
native renderer.

Further producer audit found two direct `device+10780` stores in generated code
(this does not exclude aliases or computed-offset writers):

- `8213D298` initializes issued to3 only when zero, and initializes completed
  to issued-2. It also sets the writeback object's second word from command
  cursor/state bits.
- `8213C788` emits40 bytes including writeback addresses, the current issued
  value and a cursor/state value. It then advances issued by2. A conditional
  path also writes completed and cursor/state directly to the writeback object.
  It preserves device+12952/+12956 snapshots and returns the advanced command
  pointer. Evidence: generated `.3.cpp`, `.38.cpp`.

Wait-helper audit:

- `821394D8` initializes a24-byte wait context containing device, reason,
  completed value, thread/KPCR-derived tick values and a timebase sample.
- `82139688` spins briefly, tracks completion progress, and checks a5000-unit
  no-progress interval from the tick source. It can call `82145620` and returns
  a continue/stop indication. Tick units and the called failure path have not
  been established here; do not label this a5000ms timeout without evidence.
- `82139508` accounts elapsed time in device+20024/+20032 depending on reason
  and may call the function at device+13068 with timing data.

Evidence: generated `.1.cpp`, `.71.cpp`, `.76.cpp`. A native wait adapter must
not retain the spin loop merely to claim GPU independence; completion ordering,
error/timeout handling and observable callbacks/accounting need a deliberate
replacement. No production synchronization hook has been changed by this audit.

## Remaining verification

An opt-in `--edf_native_fence_probe true` now observes `8213C788` alongside
the original path. It snapshots issued, verifies the original increments it by2,
and inserts a D3D11 event query after previously submitted native work. A bounded
FIFO publishes only event-completed values to its own host-side state, with
unsigned +2 sequence validation. It flushes submitted events and polls without
implicit flush or a busy-wait. It never writes guest completion counters.
`8213D298` clears the observed device lifetime; a probe error disables observation
for that device until reset. This is not a replacement for the guest wait loop.

WARP tests cover preceding rendering, ordered completion, unsigned wraparound,
capacity rejection, invalid sequence, empty state and deferred-context rejection.
All six native suites pass; the Windows executable builds. Process48488 started
06:22:23 with prefix `native-completion-probe` and the probe enabled. Live sequence
and lifecycle evidence is pending.

Early live sequence evidence is now available for device40002780: submitted9
reports completed5 with two pending events; later submitted257/513/769/1025
report completed253/509/765/1021 with two pending events. No probe fault was
reported in this startup interval. This verifies native event completion and
the observed +2 sequence during startup, not mission waits, teardown/reset or
permission to replace the guest's completed counter. Process48488 remains live.

The next probe extension observes `8213C928` after its original wait returns,
because that wait may flush and create the target event through `8213C788`.
It compares the current issued/target values against guest completion and the
latest polled native completion, distinguishing unknown native completion from
a known completed value. It logs bounded samples and pending counts without
waiting or writing guest state. Query/read failure disables that device's probe.
The exact retail unsigned-distance predicate is shared with tests covering
zero targets, older/equal/pending targets and ordered sequences crossing32-bit
wraparound. All six suites pass and the bridge object compiles. This extension
is not deployed into process48488 yet; its live run contains the producer probe
only and is being preserved for mission verification.

Producer-probe mission verification completed at06:31:52.837 in process48488:
all1634 indexed draws/queries were present and no errors were reported. Scene
and post-output hashes match the pre-probe baseline exactly (see migration log).
This proves observational equivalence for these captures, not native ownership
of guest synchronization. The completion queue is now explicitly non-copyable,
with compile-time ownership checks; all six suites pass.

After preserving the captures, process48488 was deliberately stopped and the
Windows executable rebuilt successfully. Process29784 started06:32:46 with
prefix `native-wait-probe`, the same retained menu schedule, batching, captures,
timings and `edf_native_fence_probe=true`. It includes the wait-boundary probe;
live wait comparisons are pending.

Early samples now show both domains satisfied after startup waits: for example
target31/issued37 reports guest_completed33 and native_completed33, and
target59/issued65 reports61 in both domains. The16th logged wait reports zero
known-native-pending waits. This is startup evidence only, not proof that every
mission wait or error/teardown path can be replaced. Process29784 remains live.

Later startup samples disprove equivalence at every return: the probe reports
seven known-native-pending waits by wait822. At06:33:17.947, target1151 and
issued1153 have guest_completed1151 but native_completed1149. Thus the original
wait's return alone cannot authorize native resource reuse. Wait1 also had no
known native completion; unknown is not a successful wait.

`NativeCompletionQueue::WaitUntil` now explicitly waits for already-submitted
native events with a deadline and stop token, sleeping between polls rather
than spinning. Its shared decision logic distinguishes complete, pending,
unsubmitted, timed-out and cancelled states. Missing/insufficient submitted
events cannot turn into an endless wait, and unknown completion cannot turn
into success. Tests cover these states, wraparound, cancellation without
publishing completion, and actual WARP event completion. All six suites pass
and the Windows bridge object compiles.

The opt-in `--edf_native_validate_wait true` exercises this operation after the
original guest wait, with a two-second diagnostic deadline. It requires both
the native bridge and fence probe flags; failures disable that device's probe
with an error. It does not overwrite counters, replace retail timeout policy,
or remove the original wait. This mode is not deployed into process29784 yet.

### Live native-wait validation, September 9

The observational run in process29784 reached its first mission capture at
06:42:17.303: 1634 indexed draws and queries, zero unavailable queries and zero
outside-scene draws. Its first scene SHA256 is
`314B6720C52E43ADFB613AC115BF1B9DAE57F7A03E0F9D0A9C135CA97E592D5A`;
its first output SHA256 is
`A177D96DA845E94F6B13C6B1F4EDC42BC880DE18C2FE16A1916AA3651E1C2DAB`.
Both match the previous baseline exactly. Captures remain partial, not proof
of complete gameplay rendering.

After preserving those captures, process29784 was deliberately stopped and
the Windows executable rebuilt successfully. All six native suites passed.
Process47308 started at06:44:09 with prefix `native-wait-validation` and
`edf_native_validate_wait=true`, alongside the bridge and fence-probe flags.
The retained scripted input schedule remains enabled.

At06:44:14.074, its first observed wait had target5, issued7, guest_completed5
and unknown native completion. The submitted native event was present: native
validation completed target5 at06:44:14.580 after506.0688ms, without changing
guest counters. The next seven logged native validations also succeeded.
Unknown completion therefore genuinely needed polling; it was not treated as
success or mistaken for an absent submission. Mission validation and lifecycle
coverage remain pending. Both original guest waits and Xenos are still active.

### Detached host presentation implementation notes

`d3d11_presenter.h/.cpp` now provides an app-owned HWND flip-discard swap chain
with RGBA8 back buffers, resize, zero-size/minimized suspension and optional
vsync. It rejects foreign/deferred contexts, wrong-thread calls, invalid
windows and oversized clients. DXGI failures throw; occlusion is a distinct
non-fatal result. Destruction unbinds/releases back-buffer references and
flushes deferred destruction before another presenter can use the same HWND.
The caller owns UI-thread scheduling and exclusive immediate-context access.

The new WARP/hidden-window test checks every pixel in cleared32x16 and17x9
back buffers, same-size reuse, resizing while the old target is bound,
suspend/resume, destruction/recreation, context/window/thread validation and
recovery after invalid sizes. Hidden-window Present may return occluded: this
test does not claim visible game presentation. All seven native suites pass.
The component is not connected to the app yet; frame composition, overlays,
UI-thread scheduling and shutdown coordination remain required.

`NativeFrameCompositor` now implements the GPU-side RGBA8 frame-to-back-buffer
draw, using a fullscreen triangle and clamped linear sampling. It supports
letterboxing/pillarboxing or stretch, respects source/target view mip sizes,
clears unused borders black and unbinds the sampled source afterward. It
requires already tone-mapped non-sRGB RGBA8 views; HDR conversion is not
silently substituted. Device/context mismatches and input/output aliasing
are rejected before changing state. The pass clears pipeline state, so the
caller must serialize immediate-context access and rebind subsequent UI/scene
state. It does not read back pixels during production rendering.

The presenter suite now renders through this compositor into an actual hidden
window's back buffer. Pixel checks cover letterbox, pillarbox, stretch, four
distinct corner colors (orientation), a nonzero source mip, hostile previous
pipeline state, and source-binding cleanup. HDR, alias and deferred-context
rejection are covered. All seven suites pass. This completes the composition
primitive, not app integration: completed-frame publication, overlays,
UI-thread scheduling and shutdown coordination are still missing.

The live native-wait validation run, process47308, was confirmed alive at
06:48:09. It logged successful validation1000 at06:45:31.549, with target1927
and completed1931; no disabled-device probe message was present. Its mission
capture is still pending. The new presenter is not part of this running binary.

At06:52:32 the same process remained alive; validation2000 had succeeded at
06:50:01.092 (target6141, completed6143). No disabled-device probe message was
present. The retained input schedule is approaching mission loading.

The native-wait validation run subsequently reached its first scene capture at
06:53:40.948 and output capture at06:53:41.371. All1634 indexed draws/queries
were present, with zero unavailable queries and zero outside-scene draws.
The scene and output SHA256 values match the earlier baseline exactly.
No disabled-device probe message was present through the inspected interval.
This validates this dual-wait mission capture, not removal of original waits.

`NativeFrameHandoff` now copies initialized RGBA8 surfaces to one owned GPU
snapshot. A serialized Visit callback uses an isolated D3D11.1 context state,
then clears consumer bindings and restores the producer's state even on
exceptions. This avoids leaking presentation pipeline state into gameplay or
retaining the consumer back buffer across a resize. GPU commands remain on
the same immediate context; there is no CPU readback in the handoff.

The bridge's opt-in `edf_native_publish_frames` creates this handoff and
publishes movie draws and ordinary output at the observed frame-end boundary.
These snapshots are explicitly classified Movie or PartialScene, not claimed
to contain complete gameplay/UI. `VisitNativePresentationFrame` holds the
bridge mutex for consumers; they may not reenter or retain borrowed resources.
Tests verify snapshot stability after producer overwrite/destruction, resize,
invalidation on failed publication, sequence/type metadata, pipeline restore
(including exceptions), and subsequent swap-chain resize. All seven suites
pass and the bridge object compiles. This code is not deployed in process47308;
app-side scheduling/consumption and overlays are still to be integrated.

### First app-side native preview integration

`NativePreviewWindow` now connects the handoff, compositor and swap chain to
the app UI thread using an owned Win32 window/timer. It is opt-in via
`edf_native_preview_window`, which requires `edf_native_publish_frames`.
Xenos still owns the original SDK window; this separate development surface
is validation infrastructure, not the completed single-window port. Native
resources are released in an isolated context callback, including before the
SDK's close-request path (whose installed implementation hard-exits).
`VisitContext` permits cleanup even when no published image is available.

The Windows executable built successfully and all seven native suites passed.
After preserving process47308's mission evidence, it was deliberately stopped.
Process45508 started at07:02:15 with prefix `native-host-preview`, publication,
preview, native-wait validation and the existing input/capture flags enabled.
The native preview window exists and is visible on the app UI thread. The
saved `native-host-preview-window-1.png` was visually inspected: its client
area is black. There is no successful-present or preview-error log yet.
Explicitly exposing the preview and sending one bounded timer tick did not
produce a successful-present message; the game process remained alive.

Bounded tick diagnostics now distinguish unavailable frames, unchanged frames
and DXGI occlusion. They compile but are not deployed in process45508 yet.
Investigate these results before claiming visible native presentation. The
capture script accepts `-TitleContains` to select this window without focusing
it. Remaining work includes visible presentation verification, native UI,
single-window integration, GPU initialization/wait ownership and Xenos removal.

The tick diagnostics were deployed after deliberately stopping process45508
to diagnose its black preview. Process59404 started07:06:20 with prefix
`native-host-preview-ticks` and the same flags/input schedule. By attempt120
at07:06:28.788, `available=true` and `occluded=true`; repeated later samples
have the same result. Publication is reaching the consumer. The HWND is
visible, not minimized, not DWM-cloaked; the console session and input desktop
are active. Exposing it above other windows did not resolve the occlusion.

An independent reproducer is available as
`edf_native_presenter_tests.exe --visible`. It shows a window and pumps
messages during61 present attempts, without ReXGlue, Xenos or frame handoff.
All61 attempts reported occlusion in this host session. The optional visible
mode now fails explicitly if none succeeds; default hidden-window tests still
pass all seven suites. This narrows investigation to the presenter/host display
path, but does not establish a driver or environmental root cause.

`NativeWindowPresenter::TestVisibility` now uses DXGI_PRESENT_TEST, and the
preview backs off to250ms visibility-only retries while occluded instead of
redrawing each16ms tick. Normal cadence resumes when visible. This follows
Microsoft's [DXGI status guidance](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-status);
a successful visibility test is not itself a presented frame. This backoff
change is not deployed in process59404 yet. Visible presentation remains a
failing verification gate, not a completed feature.

### Sandbox desktop explains the visible-test limitation

Further read-only host checks found the execution desktop is
`WinSta0/CodexSandboxDesktop-c898937c74f9ea8393928eec53fe7d2e`, while the active
input desktop is `Default`. The display-power notification reports on. Thus
these windows are on a non-active isolated desktop even though IsWindowVisible
is true. Do not change desktops or bypass product isolation to force a pass.
Actual visible output needs verification from a normal interactive launch.

Both standalone visible variants reproduce occlusion: `--visible` uses WARP;
`--visible-hardware` uses hardware. Hardware diagnostics report Present
`0x087A0001`, correct HWND, Windowed=1, flip-discard=4 and an attached output
covering0,0..2560,1440. The D3D11 debug queue has no messages. WARP's containing
output query is unsupported, which alone was not the explanation because
hardware also reproduces the condition. The visible modes now print their
render/input desktop names and still fail explicitly if no image presents.

Hardware testing also exposed a half-way UNORM clear-rounding difference:
0.5 yielded127 on hardware versus128 on WARP. The fixture now clears with
64/255,128/255,191/255 and retains exact byte assertions, avoiding an ambiguous
rounding case rather than relaxing the pixel checks. Renderer output was not
changed. Continue native integration using GPU/readback tests here, retaining
interactive presentation as an outstanding acceptance gate.

Process59404 reached its first scene/output captures at07:15:51.626 and
07:15:52.012. Both hashes match the baseline exactly, with1634 indexed draws
in the scene. This extends capture-equivalence evidence to frame publication
and repeated isolated preview consumption (including attempted composition),
not actual visible presentation. The process was confirmed alive at07:16:34.

### Native host UI draw primitive

`d3d11_ui.h/.cpp` now implements host overlay drawing independently of the
SDK's Xenos-backed immediate drawer: 20-byte XY/UV/RGBA vertices, optional
16-bit indices and signed base vertex, triangle/line lists, RGBA8 textures,
nearest/linear filtering, clamp/repeat addressing and pixel-space scissors.
Projection maps logical coordinates to the full render target. RGB uses
source-alpha blending; alpha adds source/destination, matching the inspected
SDK D3D12 immediate-drawer contract. This is host UI, not game XUI/HUD coverage.

Uploads use bounded reusable dynamic buffers. Draws validate actual target
dimensions, device ownership, RGBA8 view contracts, index/vertex bounds,
finite projection and input/output aliasing before changing the pipeline.
The draw replaces pipeline state but does not clear target contents, so the
SDK adapter must call it inside the isolated presentation scope.

The new WARP suite checks every pixel for scaled/scissored alpha blending,
four-color texture orientation, linear sampling, wrap versus clamp, negative
indexed base vertices, positive nonindexed offsets, empty scissors and line
coverage. It also rejects malformed ranges, failed-upload stale draws,
nonfinite geometry, overflowing projection, wrong target sizes, incomplete or
foreign textures, feedback loops, short texture data and deferred contexts.
All eight suites pass. SDK ImmediateDrawer adaptation and actual launcher/UI
integration remain pending; process59404 does not contain this new UI code.

`NativeImmediateDrawer` now adapts the installed SDK's ImmediateDrawer API to
that D3D11 renderer. It captures the app-bound target for a Begin/End scope,
copies SDK batch vertices without C++ type-aliasing violations, forwards
indexed/nonindexed commands and uses the SDK's own ScissorToRenderTarget
conversion. Texture wrappers retain dimensions and native sampler/resource
ownership. Foreign SDK texture types, malformed counts/pointers, invalid
coordinate spaces and incorrect Begin/batch/End ordering are rejected.
End can unwind an interrupted batch without retaining the captured target.

The separate `edf_native_sdk_ui_tests` target links the installed runtime and
native adapter, not the Xenos plugin. It exercises real SDK AppUIDrawContext,
textures and draw batches, checks pixels for scaled scissors and texture
upload, verifies caller batch memory is not retained, checks pixel-coordinate
fallback and tests malformed inputs/lifecycle recovery. It explicitly checks
that rexgpu-xenos.dll is not loaded before or after UI rendering. This test
passes in the full Windows build; the eight native-only suites remain separate.
The application's actual overlay ownership still needs switching to this
adapter during single-window integration; merely linking it is not that switch.

### Experimental native single-window application mode

`--edf_native_host true` now clears the app's GPU plugin selection, enables
the native bridge/publication path and supplies NativeImmediateDrawer through
OnCreateImmediateDrawer. The native device is initialized before launcher
overlays; OnPostSetup later supplies the game root without recreating that
device. NativeHostSurface owns timer-driven drawing on the SDK HWND via a
removable window subclass. It composes a published image when available and
draws the actual SDK ImGui overlays, including before guest startup. Occluded
UI updates use250ms cadence. `edf_native_host_capture` optionally reads back
one host frame after the third paint, with overwrite protection.

This mode is experimental: retail GPU initialization, waits and command-buffer
code are NOT yet replaced, and the normal default still selects Xenos. It is
mutually exclusive with the separate preview-window mode. Native UI rendering
does not imply every graphics option in the settings screen is implemented by
the native backend. Packaging still includes the Xenos plugin.

After preserving process59404's mission captures, it was deliberately stopped
and the Windows executable rebuilt successfully. Process17048 started07:31:44
with native host mode and `--settings true`, log `native-host-settings.log`.
The GPU capture `native-host-settings.bmp` was visually inspected: the real
settings screen has text, controls and clipping. All first three native frame
logs and the capture report Xenos_loaded=false; module enumeration independently
found d3d11.dll/rexruntime.dll but no rexgpu-xenos.dll. Visible Present remains
occluded on the sandbox desktop. An owned-window close request at07:32:35.794
completed the SDK close path and the process exited; no settings were saved.

Process46716 started07:32:36 with native host mode and normal guest boot,
log/capture prefix `native-host-boot`. It has no Xenos module loaded and remains
alive, but no gameplay image is published yet. Startup logs at07:32:36.916-917
show VdSetGraphicsInterruptCallback, VdInitializeRingBuffer and
VdEnableRingBufferRPtrWriteBack ignored because no GPU emulation is loaded.
The next task is to locate the active guest wait/init boundary and replace its
GPU dependencies, not to treat these ignored calls as a completed backend.
All eight native-only suites and the separate SDK UI integration test pass.

### First native-owned guest fence producer

Sampling process46716's hot guest thread49144 located the retail C928/C688
wait path in the generated native code. Its wait record at7010F970 identifies
device40002780, reason4 and recorded completion1. Issued=7 and the actual
writeback completion=1 atFFC9B000, with the compiled physical-heap translation
offset applied. An initial direct base+address read of that physical alias
was invalid as evidence; the corrected translated read is the authoritative
value. Wait-record tick fields were0, so timeout behavior also needs further
verification in native mode.

Native host mode now replaces `8213C788` itself. It retains CPU-side snapshots
at12952/12956, advances the issued value by2, and returns the existing40-byte
reservation advance to callers. It emits no fence PM4 packet and does not use
the original producer's conditional premature completion write. A D3D11 event
carries both the fence value and its cursor metadata. Completed cursor/value
pairs are published to the guest writeback only after Poll confirms the native
event, once per value (not repeatedly after a guest consumer clears a cursor).
Queue/publication state resets on device initialization. Guest stores validate
mapped/writable/aligned destinations and use an atomic big-endian word store.

The pending-wait helper `82139688` polls/publishes this native completion before
its original progress/error logic, then yields briefly between continuing wait
iterations. The original C928 wait and accounting helpers remain, and timeout
and cancellation semantics are not fully migrated. The normal observational
bridge/Xenos path remains unchanged and never writes completion counters.
WARP tests now verify cursor/value association and no early cursor publication,
including wraparound; all eight native-only suites pass.

After preserving the old boot evidence, process46716 was deliberately stopped
and the Windows executable rebuilt successfully. Process52004 started07:42:24
with native host mode and prefix `native-host-fences`. It submitted native
fences3 and5 and advanced beyond the previous startup wait to native shader
registration and72 texture loads by07:42:26.928. Module enumeration shows
d3d11.dll and no rexgpu-xenos.dll. No gameplay frame has been published yet.
The ignored interrupt/ring/writeback initialization calls are still present;
follow the next active guest boundary rather than treating their stubs as done.

The current SDK also supports a renderer owned directly by the application,
without implementing `IGraphicsSystem`. In `src/ui/rex_app.cpp`, SetupPresentation
calls OnPreSetup before loading a plugin. It loads only if config.graphics is
null and config.gpu_plugin is nonempty. With both absent, it still opens the
window, calls OnCreateImmediateDrawer, and creates detached overlays if that
returns a drawer. The app then owns rendering/presentation cadence.

`src/system/runtime.cpp` explicitly accepts no graphics system outside tool mode
and reports native rendering mode. `Window::GetNativeWindowHandle()` exposes
the platform window; AppUIDrawContext supports an app-owned render target, and
ImGuiDrawer::Draw accepts that context. These are integration points, not a
working D3D11 presenter in this port.

Important distinction: injecting a nonnull IGraphicsSystem with a null presenter
does **not** enter ReXApp's detached-drawer branch, which checks !graphics_system.
Choose the ownership model deliberately rather than installing an empty GPU
object and expecting detached overlays. An app-owned renderer also needs UI-thread
paint scheduling, resize/minimize handling and safe draining of queued callbacks
before destruction; WindowedAppContext's deferred-call lifetime rules apply.

Native presentation, initialization and synchronization are required with either
ownership model. A no-op graphics system or merely clearing the plugin name is
not a completed backend. Required proof remains a fresh build that
boots, presents correct gameplay/UI, handles mission transitions and teardown,
and runs without loading `rexgpu-xenos.dll` or processing GPU packets/MMIO.

### Native-only boot: engine pacing dependency confirmed

Revalidated live process52004 after the status review. Thread40116 still samples
at executable offset61E4F2, the division/reload loop in sub_821BEAB0, rather than
the earlier fence wait. Read-only process memory checks found pacer4001B230,
divisor[pacer+4]=1, current U64[8257C300]=0 and previous U64[8257C308]=0.
The device40002780 callback slot15120 contains821BEA98. These are virtual
addresses backed by the sampled guest base100000000, not physical aliases.

The complete registration chain is now established in generated code:
820B2140 constructs the pacer with divisor1 via821BEBF0; that constructor
registers821BEA98 through82139928, whose entire body stores the callback at
device+15120. Interrupt handler8213BEC0 calls this slot only on its reason0
branch after checking MMIO7FC86544 bit0. Callback821BEA98 increments8257C300.
Consequently removing the graphics interrupt source leaves the registered
engine heartbeat at zero even after native fence completion works.

The replacement belongs at the engine pacing boundary, driven by a native
monotonic clock, not by synthesizing Xenos MMIO or bypassing the wait. Preserve
the observed arithmetic: initial=(current-previous)/divisor; if initial<2,
wait until that quotient changes; consume current; return quotient except
values>4 become1; retain the timebase sample at object+8. The companion
821BEB38 reads these counters and stores quotient+1 at object+16, so replacing
only the wait's return value would leave inconsistent engine state.
Research FINDINGS sections43-44 identify this pacer as once-per-frame and
the simulation as fixed-step; uncapping it is not a valid migration.
Native clock ownership, its reset/lifecycle, and remaining device-side timing
consumers still need implementation and verification. No gameplay boot success
is claimed by this diagnosis.

### Native engine pacing implemented; standalone startup movie advances

NativePacingClock now derives a 60 Hz engine heartbeat from steady_clock,
independent of panel refresh, without a worker thread or GPU interrupts. Integer
second/remainder conversion avoids rounding a 1/60-second period. Native-only
821BEBF0 initializes the retail object layout and clock baseline but does not
register the old vblank callback. The epoch resets on pacer construction, not
on graphics-device initialization. Native821BEAB0 preserves the observed
quotient/wait/catch-up contract and the original821FAC28 timebase sample, using
1ms sleeps instead of a busy spin. Native821BEB38 refreshes the current counter
before its original reader executes. Both counters remain big-endian; writes
validate aligned writable guest storage and use InterlockedExchange64.
A separate pacing mutex means waits do not hold the native rendering mutex.
Normal reference-mode hooks still invoke their originals unchanged.

Negative/zero divisors are rejected explicitly in native mode; only divisor1
is observed in the retail constructor call. The native policy is fixed60Hz,
not arbitrary retail video-mode emulation. Remaining GPU-device timing fields
and interrupt side effects are not implemented by this engine clock.

New native pacing tests cover exact first/second tick boundaries, one-second
and day-long drift, reset/baseline, counter/delta wrap, invalid divisors,
uninitialized/backward clocks, both waiting cases and the >4-to-1 clamp.
All nine native-only suites passed; the bridge object and full executable
built successfully. The stalled verified process52004 was deliberately stopped
for deployment, with its logs preserved.

Process52392 started07:55:29 with native-host-pacing log/capture prefix and the
existing first-input-relative mission script. It passed the old startup stall:
engine ticks44/50 and catch-up results1 were logged; native movie plane uploads
began07:55:34.058 and movie draws began07:55:34.148. Subsequent movie and immediate
draw counts advance; module enumeration finds d3d11.dll and no Xenos DLL.
Reported frame-loop rates are only about11 FPS during this startup run, not a
handheld performance success. Movie draw submission does not prove visible
presentation or correct mission gameplay. Keep following this live process;
do not restart merely because the scripted mission sequence takes time.

### Standalone render-thread sampling and shader-pair reads

Process52392 remains the pacing build, with scripted input accepted at40s and
120s. After startup its reported game-loop rate dropped to roughly2-3 FPS.
Twelve render-thread58520 samples80ms apart all landed in ntdll+1607E4;
the installed ntdll export table identifies NtQueryVirtualMemory at1607D0.
Eight of those samples had the queried address at guest40005800/40005804,
the device's adjacent pixel/vertex binding words (device40002780+12416/12420).
This is evidence of memory-validation overhead, not a full performance profile
or proof that these reads account for every slowdown.

ReadShaderPair now validates those8 bytes together and preserves big-endian
pixel/vertex ordering. Indexed draws use that snapshot; immediate draws reuse
one lazily obtained snapshot across movie/output/pair validation in the same
hook, before the original guest helper executes. Nothing is cached across
draws, and no shader mismatch guard is removed. New tests cover single-block
validation, ordering, changed bindings, truncated storage and address overflow.
All nine native-only suites pass and the bridge object compiles. This change
is not yet linked/deployed: retain live process52392's mission progression
before deciding on the next evidence-preserving deployment. No speedup claimed.

### Capturing meaningful native host output

The original host capture occurred after three paints, before game imagery was
necessarily published. Added optional edf_native_host_capture_after_ms (default0,
negative values rejected before installing window callbacks) and
edf_native_host_capture_require_image (defaultfalse). They gate the existing
one-time overwrite-protected GPU capture; defaults retain the settings-UI
capture behavior. Capture logs now include elapsed time and image presence.
The first host paint with a published image is also logged even if it occurs
after the first three paints. This is readback instrumentation, not a claim
that an image is complete or that DXGI presentation succeeded. Both affected
translation units compile; the running52392 build predates these changes.

That live process continues accepting input, including355s/360s. Rates vary
with startup state (roughly2-3 FPS in some states,12-13 in others); this is
not a measured regression or improvement from the undeployed pair-read edit.
Its unsupported game-UI draws identify VS820608B0 / PS82060EC8, triangle-list
primitive4 with stride8, via caller8241E2CC. Research menu note0607 establishes
the owning viewport orthographic projection, but the basic brush program and
its complete constants/blending contract still need recovery. Host ImGui
support and successful movie draws do not cover this game-UI producer.

### Basic XUI textured brush recovered; native-only scene draws reached

The existing offline utility out/inspect_embedded_shader.exe (source alongside
it, not part of the port target) decoded guest_image.bin offsets608B0 and60EC8.
VS code216bytes fetches position.xy, forms the transform from c0/c1/c3,
projects through c4..7, adds c8.x times each projection row's z component,
and generates UV as dot(c10/c11.xy w, position.xy 1)+c9.xy. Its dp2add terms
also consume c255.x; preserve that explicitly rather than assume zero.
PS code60bytes samples tf0 at UV and multiplies RGBA by c1.

MakeNativeXuiTextureEffect implements this one identified pair in native HLSL.
No offline inspector or runtime microcode translation is linked into the
effect library. The new WARP pixel test checks position-derived UV (the supplied
UV attributes deliberately contain99), four distinct source texels, UV offset
with wrap, RGB tint and source-alpha multiplication. The first fixture used an
unsupported native-target format; corrected it to the existing RGBA16F target
with exactly representable fixture values. All ten native-only suites pass.
Live brush routing, the position-only triangle stream, guest constant/texture
binding, blend/depth/scissor state, and other brush variants remain unfinished.

Meanwhile verified process52392 passed the remaining input sequence and began
native indexed scene uploads by08:04:15. Its draw count reached1000 at08:04:42,
2000 at08:05:17, and3000 at08:05:48 with logged upload errors0. The game-loop
counter advances but reports roughly46.6 and59.3 seconds per frame at this point.
Module enumeration still finds d3d11.dll and no Xenos DLL. This is native-only
scene submission evidence, not pixel fidelity, a visible gameplay result or
handheld performance. This run has no scene capture prefix and its early host
capture predates game imagery; the pending delayed capture controls must be
deployed before the next run's visual validation.

### Position-only UI triangles

PositionTriangleStream now uploads the actual basic-brush input shape:
big-endian float2 positions,8-byte stride, triangle list, no supplied UVs.
It owns a bounded reusable dynamic buffer and POSITION-only input layout.
Validation rejects empty/non-triangle/oversized spans, nonfinite coordinates,
deferred contexts and contexts from another device before upload/draw.
The XUI pixel test now uses this stream instead of the16-byte post quad adapter.
It retains patterned UV/tint/alpha checks and adds world translation with an
unchanged local texture mapping. Invalid-input tests run before valid draws
to check recovery. All ten native-only suites pass. The game bridge does not
yet call this new stream; live binding/integration remains the next task.

### Textured XUI integrated; native title image captured

The immediate-draw hook now recognizes only VS820608B0 / PS82060EC8 and
routes its primitive4/stride8 triangle lists through PositionTriangleStream.
It validates the single POSITION float2 declaration, current output surface,
texture validity/type/non-aliasing, and supported non-reversed/no-depth state.
It uploads VS c0..11 and c255, PS c1, texture slot0 and its actual sampler,
then applies the decoded blend/raster/scissor state. A partial brush draw
preserves the prior target initialization state; it never declares the full
frame initialized or complete. Other brush programs remain unsupported.

The full executable built successfully. Verified process52392 was deliberately
stopped for deployment after12000 native indexed submissions; its log remains.
Process42712 started08:11:16 with prefix native-host-xui, the same input script,
scene/movie readbacks enabled, and a host capture gated on image presence and
35000ms host lifetime. It contains the pair-read optimization, native XUI
integration, and delayed capture controls. No performance comparison yet.

At08:11:37.046 it began native textured XUI draws (six vertices, initialized
output). At08:11:52.695 the host GPU capture completed, elapsed35061ms,
image=true and Xenos_loaded=false. Visually inspected native-host-xui.bmp:
the EDF2017 title logo and blue patterned background are rendered at1280x720.
This is a real native composed image, not a fixture or reference/Xenos capture.
The native movie120 BMP was also inspected and shows the D3 Publisher logo;
movie30 was black during the startup sequence. The early first-image host log
still reports presented=false on the isolated sandbox desktop. Readback does
not establish visible interactive presentation, complete menu/HUD coverage,
mission fidelity or acceptable handheld performance. Keep this run alive for
the configured mission captures.

### Batched live XUI device/constant reads

Verified process42712 remains alive and accepting scripted input, but the
render-thread50144 sample again lands in NtQueryVirtualMemory, this time while
reading the device viewport block. Title-state rates are approximately0.5 FPS
with the newly enabled UI draw workload; this is not acceptable performance.

ReadXuiDeviceWords now validates device[10332,12424) once per draw and decodes
only its render, viewport/scissor, declaration, output, texture and shader
binding fields. It retains no guest pointers or validation cache between draws.
The bridge checks the snapshot shader pair against its earlier identity gate.
VS c0..11 are also validated together and sliced into their named native
constants, rather than validating five separate adjacent ranges. c255 and PS
c1 remain separate reads. Tests compare every state mapping against the existing
individual readers, verify fresh values on subsequent reads, and reject short
blocks/address overflow. All ten native-only suites pass and the bridge object
compiles. This is pending deployment; no measured speedup is claimed.

Inspected SDK BaseHeap::QueryRegionInfo as a possible validation alternative.
It scans allocation page entries and reports SDK protection/state, not the OS
VirtualQuery contract used by the bridge. No substitution was made: heap
metadata is not by itself proof of equivalent OS mapping/guard validation, and
its forward scan also needs performance evaluation before adopting it.

### Solid and alpha-mask UI brush variants

Offline inspection of the registered source addresses identifies PS82060DB0
(36bytes) as RGBA=c0*c1, and PS82061848 (60bytes) as RGB=c1.rgb,
alpha=texture0(UV).a*c1.a. Earlier exploratory offsets60D10/60D88 were not
valid descriptors and failed bounds checks; registered60DB0 is authoritative.
Other decoded programs include radial sampling, color-matrix transforms and
two-sample shadows; those are not treated as equivalent to a textured brush.

The native XUI effect now has explicit solid and alpha-mask entries. Pixel
tests cover every pixel of the distinct-alpha texture and all solid RGBA
channels, including a solid draw without binding a texture. Bridge routing
accepts the three identified PS addresses only with the existing verified
VS820608B0/position-only declaration contract. Solid draws upload PS c0/c1
and do not require or validate an unused texture/sampler. Alpha-mask draws use
the checked texture and actual sampler just like textured draws. Logging names
the native PS variant. All ten suites pass and the bridge object compiles.
The variants and batched reads are pending deployment; no live coverage claim
is made for these two new programs, or for their possible other vertex-layout
pairings. Live process42712 remains on the textured-only build and has accepted
the400s input. No scene BMP has been produced by this run yet.

### Immediate-draw packet boundary audit and brush deployment

Read the retail821FD8F8 wrapper and821FD428 reservation/draw producer.
FD8F8 calls FD428, copies vertexCount*stride into its returned allocation,
publishes device13076 into cursor40, then optionally calls8252B718 (a bare
return in this image). FD428 temporarily changes byte12256 for immediate stride,
flushes the five dirty masks at device0/8/16/24/32 through state emitters,
restores12256, allocates vertex storage via8213C328, writes fetch/draw packets,
sets dirty bit4096 at device16, and records13076/13080. Flag10808 bit1 also
selects a command-recording branch updating12968. Its shader/declaration
emitter8213ECB0 is not simply an output sink: early code already changes
derived device10452 state. More of these CPU effects must be traced before
removing the original draw tail. No blind skip or fake success was added.

Verified process42712 finished all scheduled input events but was still in
non-indexed/UI rendering at573s, with no configured scene capture produced.
It was deliberately stopped to deploy the pending batched reads and solid/
alpha-mask variants, not because an observation timeout implied termination.
The previous log and verified title/movie BMPs remain preserved.
The full executable rebuilt successfully. Process48612 started08:21:16 with
prefix native-host-brushes, the existing input script and scene capture,
and the same35000ms/image-required host capture gate. Keep evaluating actual
progress rather than assuming the fixed wall-time script always reaches a
mission at these poor frame rates.

The new native-host-brushes.bmp was visually inspected after capture: the EDF
logo and animated blue background remain present. Its hash differs from the
previous wall-time capture, which is not a frame-synchronized comparison;
do not claim byte equivalence. The first title-state reading after40s is1.3 FPS
versus roughly0.5 previously, an early observation rather than a controlled
benchmark. Module enumeration finds d3d11.dll and no Xenos DLL. The live
process remains48612; its first Start press was observed at40318ms.

### Native ownership of immediate-draw float constants

Further ECB0 audit found derived device fields10400/10404/10408/10452,
byte10810 and cached pixel handle12424, plus helpersEAB0/EB68/E950/D750.
That CPU work remains in the retail tail. Read the complete DF00 and DB60
encoders: each walks a dirty-bit mask, copies CPU source values into command
packets, optionally callsD698 for packet space, and stores the final cursor40.
They do not modify their source values. Only DF00 is changed in this step.

After a successful native immediate draw, an exception-safe thread-local scope
now identifies the device whose constants were uploaded to D3D11. DF00 omits
redundant float-constant packet encoding only inside that scope, on that same
device, and for the exact vertex/pixel bank/source pairs16384/device+1792 and
17408/device+5888 (with overflow checks). It leaves source constants and cursor
unchanged; the original caller still clears its CPU dirty mask and performs
all other derived-state updates. Normal/reference mode, failed or unsupported
draws, other devices and other bank/source pairs retain the original encoder.
No draw itself is replaced with a no-op, and native event completion remains
separate from this constant-upload ownership.

The bridge object compiles and the ten existing native suites remain green;
those suites do not execute this retail-tail integration. It is not yet
deployed or runtime-verified. Live process48612 remains on the previous build.
The rest of the command-packet path, including other state/fetch/draw encoders,
is still present and the complete Xenos replacement goal is not achieved.

### Constant ownership guard tests and uncovered font path

Extracted the production ownership scope/guard into NativeConstantOwnership.
Tests exercise exact vertex/pixel bank sources, absent ownership, wrong device,
wrong/swapped bank, wrong source, overflow, a zero-device nested draw masking
its outer scope, a nested different device, and exception-safe restoration.
All ten native suites pass and the bridge object compiles. This verifies the
guard used by the hook, not the complete retail-tail runtime semantics.

Live process48612 still advances. Its logged unsupported caller821AE378
uses primitive13/stride16 and non-embedded handles40082150/400737A0. Generated
code identifies the enclosing function821ADCE8 as a font draw path: it unpacks
text color, walks glyph records and constructs four position/UV vertices before
the immediate draw. This is distinct from the embedded XUI brushes. Added
bounded diagnostic output for registered source-shader entry, stage, source
size and fingerprint alongside unsupported-draw identities, so the next
deployment can identify this actual shader pair without assuming PS_Bloom or
reusing the embedded brush program. No font coverage claim yet.

### Original font HLSL recovered; first standalone mission pixels inspected

The font setup821AD3B8 compiles a string through821FF9F8, then calls the raw
VS/PS constructors directly, bypassing the embedded-XUI identity wrappers.
Pointer82556148 in guest_image.bin resolves to source820179A8. The source has
FontVertexShader: position*Scale+Offset, z0/w1, uniform color, atlas UV*TexScale.
FontPixelShader selects value=dot(texel,ChannelSelector), uses value>0.5 to
decode luminance/alpha, lerps the decoded RGBA with texel by per-channel Mask,
then multiplies by vertex color. Follow the actual source alpha expression
2*(value>0.5 ? 1 : value), not the adjacent comment implying alpha1.
The glyph draw writes VS c1 color directly into device1808..1820; generic
material activation cannot be assumed to update this per-draw constant.

MakeNativeFontEffect implements that source behavior. WARP pixel tests cover
atlas UV scaling, color modulation, selected-channel values0/.25/.5/1 and
mask0/.5/1, preserving the exact threshold and alpha behavior. All ten suites
pass. Font handle identification, direct-register upload and live quad routing
remain to be integrated. This is not an alpha-mask brush substitution.

Process48612 has now produced native-host-brushes.1.bmp and output.1.bmp.
Both were visually inspected: the partial scene shows city roads, buildings,
trees/railings and the overhead ship, but lacks player/HUD; the final output
is nearly white. The Xenos module is still absent by module enumeration.
This is standalone mission pixel evidence, not complete gameplay fidelity.
Fixing the final post output and missing player/HUD remains required, as does
the very poor frame time. The earlier absence of captures was temporary,
not a reason to declare the process stopped.

### Live font routing deployed; runtime coverage still pending

Connected the recovered font effect to the current shared declaration/VS/PS
triple at8257C06C, read fresh for each candidate draw. The adapter checks the
known source pointer82556148, exact declaration elements, quad topology,
output surface, viewport/depth contract and nonaliased native atlas. It uploads
VS c1..c4 and PS c0..c1 directly on every draw, preserving retail glyph layout
and per-run color. Recognized font draws cannot fall through to stale Bloom
material state, including when their native validation fails. Partial text
preserves output initialization rather than claiming full-frame coverage.

Font pixel tests now use big-endian guest register uploads, including nonzero
unused float2 lanes, to verify the packed native cbuffer does not corrupt
adjacent variables. All ten native suites pass; the full executable builds.
These tests still do not prove real-game font coverage.

Stopped verified process48612 deliberately for deployment, preserving its
captures. Process55076 started08:39:23 with prefix native-host-font and the
existing scripted input. It includes both font routing and the scoped DF00
constant-packet omission. The log confirms those vertex/pixel omissions run.
The image-required35s capture was visually inspected: native title logo and
blue background remain present. Module enumeration finds d3d11.dll and no
Xenos module. Visible presentation remains occluded by the sandbox desktop.
At54s the process is live at about1.2 title FPS, with no font submission yet
observed. Do not claim text rendering or mission regression verification from
this title capture. Continue observing this same process toward menu/mission.

### Font declaration padding correction

At112s process55076 reached recognized font draws, all rejected by the strict
vertex-element comparison. Reinspection of821AD3B8 shows it writes only the
method/usage/index bytes at stack120..122 and132..134; padding123/135 is not
initialized.82149AB0 copies the full12-byte records to declaration+52. The
font validator now masks only those two padding bytes. Tests cover256 padding
pairs and every individual semantic-bit mutation, plus wrong element count.
All ten suites pass and the full executable builds. This explains a source
level incompatibility; actual live declaration contents were not read back.

Added bounded optional partial output captures at font draws1/100/1000 using
the existing scene prefix. Extended opt-in inclusive hook timing to separate
immediate native work from the remaining original draw tail. Verified55076
was deliberately stopped for deployment; its log/captures are preserved.
The replacement run uses prefix native-host-font-padding with hook timings
enabled and the same input schedule. Font rendering is not yet verified.

### Native font pixels verified and native-side timing evidence

Process54560 (started08:43:52) submitted its first recognized native font
draws at08:45:51 without the previous declaration rejection. Inspected
native-host-font-padding.font.100.bmp: title art and the partial string
"Pres" are visible. This capture occurs after an individual glyph draw,
not a completed frame; it proves actual atlas/glyph pixels, not complete
prompt/menu/HUD layout. Module enumeration still shows D3D11 and no Xenos.
Keep this process alive for the remaining scripted menu/mission progression.

At early title state, immediate native batches cost roughly265..294ms per256
calls versus0.10..0.15ms for the original tail. Earlier post/movie batches
cost roughly547..577ms native versus under1ms original. These are inclusive
CPU wall times, not GPU timestamps or a full-frame exclusive profile. They
point at native-side work rather than the remaining immediate packet tail
as the current measured cost. Activation native batches also cost~690ms.

Changed per-instance parameter decoding to validate header and record-array
blocks once instead of two header words plus three checks per record. No
cross-call pointer/value cache; updates remain live and empty lists do not
read a records pointer. Tests cover exact two-block reads, live updates,
truncated records, address overflow and existing malformed range cases.
All ten suites pass and the bridge object compiles. This optimization is
not deployed in54560 and has no measured runtime speedup yet.

### Per-draw device read window (pending deployment)

Six sequential samples of render thread10036 in live54560 all found RIP at
ntdll+1607E4, the previously identified NtQueryVirtualMemory syscall return.
This corroborates expensive repeated memory validation, but is not a complete
statistical profile. Consolidated XUI/font device reads into one validated
range device+1024..12424 per recognized draw. A short-lived GuestReadWindow
serves contained accesses; declaration, vertices, source identity and other
out-of-range reads still use GuestReader validation. It retains no values or
mapping across draws, guest calls, releases or allocations. This is the same
containing-object read strategy already used by ReadXuiDeviceWords, expanded
to include the device's constant and sampler fields.

Tests cover one validation for contained reads, both boundaries, live values,
fallback validation for external/crossing reads, backing truncation and address
overflow. All ten suites pass and the bridge object compiles. These changes
and instance batching remain undeployed;54560 is preserved to continue its
menu/mission progression. Do not claim a measured speedup yet.

Also inspected font.1000.bmp from54560: readable "Creating new game data.",
"Continue?" and "You will not be able to save" appear over the title art.
This extends font pixel evidence to a real dialog. It is still a mid-draw
partial capture, so missing dialog background/buttons or remaining text
cannot be diagnosed solely from this image.

### Later post-output evidence gate

Revisited the existing PostEffect evidence: history adapts by0.025 per frame,
and early history values produce exposure multipliers above30. First-three
white captures cannot distinguish that startup effect from an ongoing post
bug. Added optional indexed-output captures at frames30/60/120 in addition
to1/2/3, using frame numbers in filenames. This changes diagnostics only;
no exposure constant, shader expression or history value is altered. The
bridge object compiles. This remains pending deployment together with the
device read-window and instance batching changes.

At08:51:01 process54560 is confirmed live, t427s, recently advancing at~12FPS
after slower UI. The450s and500s scripted inputs have not yet occurred. No
new mission output capture is available at this observation; preserve this
run rather than treating that absence as a terminal result.

### Native ownership of selected render words; accumulated changes deployed

Re-read complete DB60 encoder: it copies dirty word runs into packets, calls
D698 for space when needed, and writes cursor40. It does not change source
state. Added a hook guarded by the existing successful-native-draw scope:
only bank8704/source device+10420, nonzero dirty masks wholly within
E400000000000000 (depth/blend/alpha/raster, indices0/1/2/5) omit encoding.
Those words were already decoded and bound by the successful native draw.
All mixed/unknown words, other banks/devices, unsupported and reference draws
retain the original encoder. No CPU source/cursor writes or fake completions;
the original draw tail still clears dirty masks and updates derived state.
Tests cover all64 individual bits, mixed masks, source/bank/device mismatch,
zero mask, absent scope, overflow and nested disabled scope. All ten suites
pass and the full executable builds. Retail-tail integration remains a live
verification requirement, not something those guard tests establish.

Process54560 completed all scheduled inputs and remained live in UI/post draws
past560s, with no mission capture yet. Deliberately stopped for deployment,
not because the observation implied a crash. Font/dialog artifacts and logs
remain preserved. New run prefix native-host-device-window includes instance
batching, XUI/font checked device windows, later output captures and the DB60
guard, with hook timings and the same scripted input enabled.

### Device-window deployment checked; movie/post extension pending

Process52892 started08:54:14. Its35s image-required host capture was inspected:
title logo/background still render. Module enumeration finds D3D11 and no
Xenos. Recent title batches cost154..194ms per256 immediate native calls,
compared with roughly265..294ms before; title FPS readings51..88s are1.7..2.3.
This is encouraging uncontrolled runtime evidence, not a frame-synchronized
benchmark or proof of playable performance. No DB60 omission log was yet
observed, so that narrower guard's live coverage is still unproven.

Extended the same local device read-window to recognized movie and post
draws, and batched their six vertex-element words into one validated read.
Topology/element comparisons remain unchanged (font-specific padding policy
is not applied to these paths). Templated ReadDrawViewport to accept either
the backing reader or checked window. The bridge object compiles and all ten
suites pass. This extension is not linked/deployed in52892; keep that run
progressing to menu/mission while retaining its new timing evidence.

### Unsupported-output visibility and indexed read batching

The old unsupported UI diagnostic only ran when the last linked material was
Bloom, and its first-five quota could be consumed by one font path. Added
bounded distinct shader-pair/topology reporting for unhandled ordinary output
draws independent of Bloom, including known embedded/material source identity.
Recognized movie/XUI/font/post draws (including their separately reported
failures) are excluded. This is diagnostic visibility, not added shader coverage.

Extended the per-draw device read window to indexed scene submissions; batched
vertex resource address/size and the32-byte index resource header. Vertex/index
payload spans still validate separately, and mesh/state correctness checks are
unchanged. The bridge object compiles and all ten suites pass. These changes,
like the movie/post extension, remain undeployed in52892. That process is live
and reached~183s with font submission logs but no new unsupported-pair evidence
from its older diagnostics. Preserve its continued scripted progression.

### Indexed draw upload ownership (pending deployment)

Inspected821FE358's upload prefix: DF00 receives banks16384/device+1792 and
17408/device+5888, followed by original CPU mask clears. DB60 receives
8704/device+10420 for render words; ECB0/D938 and other derived-state work
remain around it. Extended the existing ownership scope to this indexed tail
only after an actual successful native scene Draw. Upload-only probes,
unsupported/failed draws and reference mode use a zero scope, masking any
outer ownership. No fetch/draw or other state encoder is removed in this step.

Added a separate-thread test proving ownership is absent on another thread,
that thread can establish its own device scope, and the parent scope survives.
The bridge object compiles and all ten suites pass. Runtime integration is
still pending deployment. Process52892 remains live at346s, progressing at
~12..14FPS through the current non-mission stage. Keep its remaining355/400/
450/500s input events and captures under observation.

### Material activation read consolidation (pending deployment)

Activation now validates the112-byte material object once, covering the six
local/global constant/texture vector headers and pass pointer. Template upload
helpers accept that short-lived window; all referenced arrays, constant data,
sampler device state and names still use backing validation. String reads
explicitly delegate to the original bounded/page-aware reader; no name or
material value is cached between activations. Tests exercise all six empty
vectors through one validation and bounded/unterminated string behavior.
The bridge object compiles and all ten suites pass. This is not yet linked or
deployed in52892, and no activation-speedup claim is made.

After a verified wait on live52892, the run reached mission indexed draws at
09:03:02. Its first20 uploads/submissions report zero errors. Keep it alive
for the first scene/output captures and later exposure observations; successful
submissions alone do not prove pixels or gameplay fidelity.

### First mission regression capture and mesh-cache capacity

Inspected52892's scene1 and output1 captures: city geometry remains visible,
player/HUD remain absent, and final output is white. First tone-history R is
0.014701843. Keep this run for the later output-frame gates; startup exposure
is still not a proven explanation of the complete output problem.

Indexed native batches cost~3.5..4.2 seconds per256 draws, versus~0.7..1.4ms
for their original tails. At2000 submissions there are376 builds/1624 hits
and~28MB cached. Inspected cache policy: a separate256-entry cap can evict
before its64MB byte budget. Increased the default cap to1024 while preserving
the64MB budget, exact source-byte comparisons and resource invalidation.
Added separate entry/budget eviction counters and entry count to future live
logs. The old log alone cannot prove which cause drove every rebuild.

Tests exercise reuse past256 entries, a small configurable entry cap with LRU
ordering, byte-budget eviction attribution and zero-entry behavior. All ten
suites pass and the bridge object compiles. This capacity change and the
other pending optimizations are not deployed in52892; no mission speedup
claim is made. Its live output captures remain the current regression baseline.

### Immutable index-range fast validation (pending deployment)

NativeIndexedMesh now records full-buffer minimum/maximum indices during
construction. After the unchanged draw-count/topology bounds check, those
extrema can prove any subrange valid with a given signed base in constant
time. If they cannot, the original exact per-index loop remains: invalid
indices outside a requested subrange must not reject it. No validation is
removed, and cached mesh rebuilds recompute extrema from new source bytes.

Tests compare both16/32-bit paths against a per-index oracle across first/
count combinations, negative bases and INT32 extremes; a UINT32_MAX index
outside a valid subset exercises fallback and signed-overflow rejection.
All ten native suites pass. This change remains undeployed with the pending
cache/read-window/ownership work.52892 continues the slow mission baseline
for later output captures; no speedup or later exposure result is claimed.

### Opt-in SDK range-read validation and accumulated deployment

Three additional mission render-thread samples again landed in
NtQueryVirtualMemory. Inspected SDK BaseHeap::QueryRangeAccess: it checks every
page's current protection under the heap mutex and rejects out-of-heap ranges.
SDK file I/O already uses it for guest physical buffers. Added development
flag edf_native_guest_heap_reads (default false): for a nonempty readable range
wholly contained in one heap, use that metadata instead of VirtualQuery.
TranslateVirtual and alias-contiguity checks remain. Cross-heap, unknown or
non-readable metadata falls back to the original OS check. StoreWord and
StoreDoubleWord still perform their independent aligned/writable OS checks.
No mapping cache is introduced. This alternate validation requires runtime
comparison; unit suites do not prove its equivalence for every SDK mapping.

Full executable builds. Deliberately stopped verified52892 to deploy all
pending read-window/material/cache/index-validation/indexed-ownership changes
and test SDK reads. Baseline captures/logs are preserved; it did NOT reach
output30, so later exposure remains unverified. New prefix native-host-heap-
reads enables SDK reads and hook timings with the same input/capture settings.

### SDK reads reach60FPS title; Utility output adapter pending

Process50368 started09:10:36. SDK page-range reads are active on guest heap
40000000. Title logs sustain60FPS through at least65s, with native XUI draws
actually submitting. Inspected font1000: title art and partial "Press START
to c" are present (mid-glyph capture). D3D11 is loaded and Xenos is absent.
This is a substantial title-path improvement, not mission/handheld proof.

The new unsupported-pair diagnostic found caller821A7B48 stride20 and caller
821A79A4 stride12. The latter maps to Utility.dxsl VS_2D/PS_Main with source
fingerprintc885203e230fe745. Decoded the disc source: Utility2D transforms
position by _g_DX2DScale/Offset and passes vertex color; VS_2DTex/PS_Tex also
passes UV and modulates the texture.821A7A48 calls B94E8 -> B8E48 activation
before submitting20-byte vertices, so existing material constants remain
authoritative. The earlier stride20 handle lacked a registered source identity;
do not assume every20-byte draw has been covered.

Added a checked native Utility2D quad adapter for registered exact-source
VS_2D/PS_Main and VS_2DTex/PS_Tex pairs. Checks active pair, surface, viewport,
disabled depth, exact position/color[/UV] declaration, quad count/stride and
available texture inputs; uses the existing native indexed mesh decoder for
big-endian floats and packed D3DCOLOR. Keeps partial-output initialization and
only takes packet ownership after an actual Draw. Unsupported selection cannot
fall through into Bloom. This first adapter creates temporary mesh buffers per
draw; dynamic streaming optimization remains possible after live coverage.
Bridge object compiles; existing ten suites pass but do not exercise this live
adapter. It is NOT deployed in50368. Preserve that run's mission progression.

### Utility quad pixel contract tests

Added WARP pixel tests for Utility's2D position-scale/offset and vertex-color
behavior with12-byte position/color and20-byte position/color/UV streams.
The same native mesh conversion and012/023 quad indices preserve packed
ARGB80402010; a two-texel red/green texture with distinct alpha verifies UV
sampling and RGBA modulation. These tests model the decoded source contract,
not the complete retail activation integration. Added bound-resource identity
inspection and reject Utility draws that sample their output surface.
All ten suites pass and the bridge object compiles. This remains undeployed.
Process50368 is live and still reports60FPS through381s; its400/450/500s
scripted inputs and subsequent mission output remain under observation.

### Mission reaches interactive frame rates; later exposure inspected

50368 reached the mission and produced output1/2/3/30/60/120. Inspected30
and120: frame30 is strongly overexposed but shows city geometry; frame120
shows substantially adapted city output and distant characters. Player/HUD
are still absent from this capture. This proves that all-white first frames
were not persistent; it does not establish reference-matched exposure or
complete scene/gameplay fidelity. Do not change exposure constants based on
the first-three captures alone.

Recent mission FPS samples521..551s are34.9,58.0,54.8,30.0,30.2,30.3,32.9.
These are runtime observations, not a handheld benchmark. At~1.02million
indexed draws the log reports zero upload errors,340 mesh builds and340
resident entries,36,332,772 bytes with zero entry/budget evictions. Source
byte comparison and mesh validation remain enabled. D3D11 is loaded and
Xenos remains absent. This is a major performance improvement over the old
~50seconds/frame baseline, but the complete replacement goal is not achieved.

The faster draw rate rotates the5MB logs rapidly (five backups). Pending
timing diagnostics now report each phase at most once per5seconds after the
initial256 calls, accumulating all intervening counts/times. Bridge object
compiles. This logging change and the Utility adapter are not deployed in50368;
do not mistake absence from the current rotated log for absence from the run.

### Utility lines and quads deployed

Rotated logs also identify VS_2D/PS_Main primitive2 stride12 count2 from
821A79A4. Added line-list support to the exact-source Utility adapter. Native
mesh validation now shares a range checker with primitive width2/3; DrawLines
binds LINELIST explicitly. Tests cover valid16/32-bit line subsets, odd counts,
range/base rejection and the bound native topology. Existing Utility quad
pixel tests remain green; all ten suites pass and the full executable builds.

Deliberately stopped verified50368 for deployment after preserving its mission
output1..120 and logs. New prefix native-host-utility includes the adapter,
feedback guard and rate-limited timing reports. Host capture is now gated
at60000ms to inspect the dialog after the first Start; scene output captures
also include frames600/1800 to inspect later player/HUD visibility. SDK heap
reads remain opt-in enabled on this run, and the same input schedule is used.

### Live Utility layout corrected

45968 rejected Utility elements. Deliberately redeployed bounded declaration
diagnostics as42632/native-host-utility-elements. Runtime words prove the
textured20-byte layout is POSITION float2 at0, TEXCOORD float2 at8, D3DCOLOR
at16, with declaration order position/UV/color. The earlier assumed position/
color/UV layout was wrong. Corrected the live gate and pixel fixtures to those
actual offsets; all ten suites pass and full executable builds. Neither failed
run was treated as a crash; both were stopped deliberately for deployment.

55892 started09:26:47 with prefix native-host-utility-layout and the same60s
host capture, SDK-read flag and scripted input. At09:27:03 it reports successful
VS_2DTex/PS_Tex quad submissions1..5. This verifies routing through the corrected
declaration, not yet complete pixel/dialog fidelity. Preserve this run for the
host capture and subsequent mission/player/HUD checks.

### Utility dialog pixels and solid declaration recovered

55892's60s native-host-utility-layout.bmp shows the title and complete new-game
confirmation dialog background/text. The capture log confirms image=true and
Xenos_loaded=false; visible presentation remains occluded on the sandbox desktop.
The same run logs solid stride12 POSITION type0x2a23b9 at0 and COLOR0 at8.
This declares float3 overlapping color, but recovered Utility.dxsl VS_INPUT2D
uses only float2 position. Added an exact-layout, source-gated conversion to
float2 in a copied native declaration; guest bytes and the general mesh overlap
rejection remain unchanged. Pixel tests now start with that actual solid layout,
check packed color/XY, malformed-layout rejection, ignored padding and input
immutability. All ten suites pass; bridge object compiles. Variant-specific
first-success logs distinguish solid/textured lines/quads after deployment.

### Experimental SDK read commitment correction (pending deployment)

Reinspection confirms the earlier migration warning: QueryRangeAccess reads
current_protect only, while Decommit removes commitment without clearing that
field. Replaced the experimental fast-path check with fresh QueryRegionInfo
iteration requiring both commitment and read access for every intersected region.
Queries are page-aligned because SDK region_size counts whole pages even when
the query address is unaligned. No region cache; unsuccessful metadata checks
still use OS validation. Added boundary, decommitted/read-protected middle-region,
malformed extent and4GiB-end tests. All ten suites pass. SDK/host alias protection
equivalence and the performance of this stronger check remain unverified, so
the flag stays opt-in. Earlier30-58FPS measurements used the older protection-only
check and must not be attributed to this revision.55892 remains on that older
executable to preserve its pending mission600/1800 captures; neither this check
nor the solid declaration conversion has been deployed yet.

### Player and HUD verified after the intro; fixes deployed

55892 captured output600 at09:35:34 and output1800 at09:36:19.600 shows the
spacecraft intro;1800 shows the player soldier, allied soldiers, radar, crosshair,
health200/200 and AF14 ammo120/120 over the city. The earlier player/HUD absence
was therefore not proof of missing rendering. This does not prove all HUD states,
combat, effects, visual parity or sustained handheld operation. The run submitted
over3.3million indexed draws with errors0 before deliberate shutdown for deployment.

After verifying the exact process path, stopped55892 and successfully built the
full executable. Started a new native-host-committed-utility run with the same
input/capture settings. It contains the committed-region reader and normalized
solid Utility declaration. Runtime correctness and performance of this revision
still need verification; previous FPS numbers are not its benchmark.

New process59960 started09:37:32. Logs confirm committed-region validation is
active, native images are produced with Xenos_loaded=false, and title/menu
samples20..55s remain60FPS. At09:38:15.555 the previously rejected solid
VS_2D/PS_Main quad submits successfully (vertices4, lines=false). Textured
Utility also submits. Solid line routing and mission performance with the new
reader are still pending; preserve this live run for those checks.

### Native startup/build/package becomes the normal Windows path

Removed GPU_PLUGINS xenos from the host setup call and rexgpu-xenos from the
package list. OnPreSetup always clears gpu_plugin and enables the native host,
shader bridge and frame publication, including migration of old saved false
settings. Native host defaults true. Unsupported non-Windows game builds fail
configuration explicitly; portable native tools remain selectable. Legacy GPU
quality, VSync and upscaling controls are disabled, not advertised as native
features. SDK heap reads remain opt-in until broader validation is available.

Built from scratch in out/build/win-native-clean. Import inspection finds no
Xenos import in either executable or rexruntime.dll. It did expose a direct
amd_fidelityfx_dx12.dll import in this SDK's runtime. Initially removing that
DLL prevented SDK UI test startup; deliberately stopped that exact test process,
restored the mandatory runtime DLL staging, and reran all12 suites successfully.
This DLL is a loader dependency, not evidence of native FidelityFX support.

Packaged into a newly validated out/package/native-clean-20260909 (the existing
0.2.0 package was not touched). Contains exe, rexruntime, required FidelityFX,
controller database, README and licenses, and no Xenos DLL. Started package
process6140 at09:45:51 with separate user data and no native-host or heap-read
flags, to verify the default startup independently.59960 remains live for its
committed-reader mission captures. Build/package removal is progress, not proof
of complete GPU-boundary migration, fidelity, visible presentation or handheld
readiness. Old build folders can still contain previously staged Xenos files.

Default-package proof:6140 produced native-package-default.bmp at09:46:12 with
image=true, Xenos_loaded=false; inspected capture shows the title screen. Module
inspection confirms D3D11 and the package-local required FidelityFX DLL, no Xenos.
No native-host/heap-read flags were supplied. Deliberately stopped this bounded
probe after capture to avoid competing with59960's mission measurements.59960
reached mission output1..120 and then600; FPS samples516..536s are30.0,32.5,54.8,
53.7,30.1 with the committed-region reader enabled. These overlap the package
probe and are not isolated benchmarks. The ordinary default still uses OS read
validation, so the optimized run's performance must not be claimed as default.

### Native host reentrant-close lifetime correction

The overlay callback could release the app's unique host owner while Paint still
used this, its guard and presenter. Replaced ownership with a factory-created
shared host and a window-dispatch lifetime pin (no HWND ownership cycle). App
close/shutdown explicitly detaches the timer/subclass before releasing ownership.
Stop during painting defers GPU cleanup; Paint checks for detachment immediately
after overlays, skips capture/present/timer rearm, then releases resources outside
the presentation callback. WM_NCDESTROY follows the same path. Exception cleanup
does not dereference a cleared app immediate-drawer pointer.

New native_host_lifetime_tests uses the real host and WARP, with an isolated
context visitor that rejects recursive context access. Exercises nested timer
messages, owner release during overlays, destroyed HWND during painting,
repeated Stop, stale timer delivery and overlay exceptions. Full native-clean
build succeeds and all13 suites pass. Existing package native-clean-20260909 and
live59960 are older builds; this fix is in out/build/win-native-clean, not yet
repackaged. A bounded --settings startup/close probe is next; sustained combat
still needs analog/trigger scripted input support and later captures.

Real-app close probe38556 started09:50:57 from win-native-clean with --settings
and no native-host flag. native-host-close.bmp at09:51:00 shows the settings UI,
including disabled legacy GPU controls; log confirms Xenos_loaded=false. Requested
normal CloseMainWindow on the verified owned executable, received true, and
WaitForExit completed; subsequent Get-Process confirms it is absent. No forced
termination was used for this probe. PowerShell did not return a numeric exit
code, so this is normal window-close evidence, not an asserted exit-code0 check.
The SDK log says "Title terminated; hard-exiting process"; this does not prove
graceful teardown of all game resources. Reentrant host cleanup is covered by
the separate lifecycle tests rather than inferred from that process exit.
59960 remains live on the preceding build with over18.9million indexed draws and
zero logged indexed upload errors; that alone does not verify combat coverage.

### Analog scripted input and bounded combat captures deployed

Extended opt-in scripted input rows from three fields to optional nine fields:
start_ms duration_ms buttons_hex LT RT LX LY RX RY. Analog fields are decimal,
triggers0..255 and sticks-32768..32767. Partial/overflow/trailing-junk rows reject.
Last active analog row wins, including explicit zeroes; button-only rows do not
interrupt analog holds. Expiry or cleared reload releases analog state. Driver
uploads all six fields and logs transitions. Tests cover ranges, malformed rows,
overlap, boundaries and reload release. Full native-clean build and all13 suites
pass. Added opt-in indexed-output capture interval, capped at32 total captures.

After preserving59960's later player/HUD capture and over36.6million indexed
submissions with errors0, deliberately stopped its verified process for deployment.
43300 started09:55:03 from win-native-clean, prefix native-combat, separate
tools/native-combat-input.txt schedule and interval300. This is the current native
startup/lifecycle/analog-input build. Explicit legacy --edf_native_host false
tests startup migration; committed heap reads remain explicitly enabled. Planned
RT pulses at600/630s and forward movement at620s must be checked against actual
mission progression before execution (hot reload retains the first-poll clock).
These are scheduled inputs, not proof of combat rendering. Preserve this run.

### Real SDK heap read validation and default promotion (not yet linked)

Factored the committed-region adapter into guest_sdk_readable_range.h so tests
exercise the exact production query and page-alignment logic. A new isolated SDK
Memory test allocates across virtual40000000, executable80000000 and physical
A0000000/C0000000/E0000000 heaps. Checks unaligned cross-page ranges, read-only
and inaccessible middle pages, decommit/recommit, release, reserve-only memory,
heap boundaries and physical write watches. Every admitted range is independently
checked with VirtualQuery at TranslateVirtual's actual host pointer. All pass.
SDK SyncHostPageAccess source also confirms write watches clamp writes to read-only;
they do not remove reads from committed readable pages. This does not certify
arbitrary external host-protection changes or future SDK versions.

Changed edf_native_guest_heap_reads default to true, preserving false for OS-only
diagnostics and OS fallback for unsupported/untracked metadata. Shared adapter
and new default compile; all14 suites pass. Linking is intentionally deferred
because43300 is still running from win-native-clean toward its combat checks.
Its reader is already enabled explicitly and uses the equivalent inlined logic.
Do not restart it for this default-only deployment. Package remains older.

Reviewed VdInitializeRingBuffer, VdEnableRingBufferRPtrWriteBack and interrupt
registration SDK implementations: without graphics_system they return without
backend effects. Parent guest routines also allocate and initialize CPU buffers,
so they cannot be blindly removed. No hooks were added merely to silence these
warnings; replacing the remaining CPU command-buffer dependencies needs a
separate state/data-flow audit.

### First live movement/firing evidence and broader routing diagnostics

43300 reached mission at10:03:36. Inspected output1800: player/HUD after intro,
ammo120/120. Analog log confirms RT255 at600016ms and release605000ms; output3000
at10:05:15 shows ammo060/120. LY20000 at620024ms/release623018ms advances player
and camera down the street (output3600). Second RT pulse630020..635026ms is
followed by output3900 ammo120/120, consistent with magazine exhaustion and auto
reload, though those transition pixels were not captured. No logged errors were
found in this interval; indexed submits exceed9.2million with errors0. This is
input/game-state/HUD evidence, not full combat/effects or enemy-hit verification.
Added a longer RT hold710..725s and movement730..740s to the live schedule so a
later capture can land during firing. The first-poll clock is retained.

Pending bridge object now counts every immediate request as empty, submitted or
unsubmitted, with cumulative10s reports and up to64 caller/primitive/stride/active
shader signatures for nonempty unsubmitted paths. Unlike per-renderer error
counters, this also catches entirely unhandled routes. Unsubmitted does not
automatically mean missing pixels (invalid targets/teardown may explain it), and
the64-signature sample is explicitly capped. Object compiles; not deployed in
43300. Preserve live captures before linking this and the fast-reader default.

### Reload pixels verified; default-reader/coverage build deployed

Hot reload accepted14 inputs at705871ms. RT held710017..725000ms: output6000
shows108/120 ammo; output6300 at10:07:06 shows the red RELOAD label, progress bar
and changed player animation. Later output7200 shows085/120 after the reload
and remaining held fire. Movement730000..740002ms changes player/camera position,
though the median hedge appears to limit travel; future movement should move
onto the road first. These captures verify reload presentation and continued
input/gameplay, not projectile pixels, enemy damage or complete effects coverage.

After preserving the bounded captures and~19.7million indexed submissions with
errors0, deliberately stopped verified43300. Full native-clean build linked and
all14 suites pass.39944 started10:08:48 with prefix native-coverage, interval300
and the same hot-reload input schedule. No native-host or heap-read flags are
supplied: it uses the new defaults. Includes shared real-SDK-tested reader,
reentrant-close fix and immediate routing totals. Keep this process intact for
coverage inspection and mission progression. Earlier packaged artifact remains
older; the current executable is out/build/win-native-clean/edf2027.exe.

### Coverage run: startup-only unsubmitted group observed

39944 reports1771 nonempty unsubmitted draws, all sampled under Utility caller
821A7B48, primitive13/stride20, active VS4001c180/PS40008d30, native target/output0.
The group occurs around13s before the first title output scope and remains stable
through251s/13.2million requests. Do not label it harmless or a missing-pixel bug
without the actual guest target binding. Added bounded first-path diagnostics
for device12168, surface registry membership and matching known scene output
owner. Bridge object compiles; the extra diagnostic is not yet linked/deployed
because39944 remains live. Other prior-deployment changes are already active.

Updated hot-reload combat schedule:610..612s strafe right24000 to step off the
median, then620..630s forward24000. Other firing inputs retained. This follows
the captured hedge obstruction rather than assuming a longer forward hold alone
will reach enemies. Confirm actual movement in upcoming captures before claiming
enemy engagement. Handheld model/resolution/FPS preference requested
asynchronously; it does not block local renderer work.

### Independent native VSync and truthful simulation settings (staged)

SDK vsync is registered in command_processor.cpp, so native presentation must
not depend on that GPU-emulation registration. Added edf_native_vsync (default
true) owned by the native host and wired it directly to NativeWindowPresenter's
DXGI sync interval. Settings now changes that native flag and re-enables only
the VSync checkbox, not unsupported quality controls. Removed the selectable
simulation refresh rates: NativePacingClock is explicitly60Hz independent of
display refresh. Presenter diagnostics/tests verify sync intervals0->1->0.
Native host lifetime and presenter targets rebuilt; app objects compile and all
14 current suites pass. Does not prove visible refresh behavior on the sandbox
desktop. Changes are staged, not linked into39944; preserve its mission run.

Roadway approach schedule extended to forward730..770s, then forward+RT760..775s
(last active analog row wins). Captures capped32 may end near the latter window;
check actual frames rather than assuming enemy contact from scheduled inputs.

### Mission coverage exposes a 3D immediate path

At10:17:17.97939944 records unsubmitted caller821A7C5C, primitive6 (triangle
strip per SDK enum), stride16,20 vertices, active shaders4001ca80/40008c50.
It occurs between successful indexed scene draws7 and8; target/output0 alone
does not mean no scene, because the diagnostic omitted active_scene. Generated
sub_821A7B58 calls the immediate helper with stride16 and caller821A7C5C; decoded
Utility.dxsl has VS_3D using float3 position transformed by view/projection and
PS_Main returning vertex color. This is a likely missing3D Utility route, not
yet a proven shader identity/layout. Added bounded actual bound shader entry/
fingerprint, active_scene and declaration-word diagnostics for unsubmitted paths.
Object compiles. Preserve39944 through enemy approach; next deployment should
resolve this path and implement it with observed declaration/depth state, rather
than treating all unsubmitted calls as initialization noise.

### Enemy interaction captured; first 3D Utility adapter deployed

39944 output4500 shows roadway travel under the footbridge with distant ants.
Output7800 at10:21:43 shows close-range ants, player health50/200 and ammo102/120.
This proves enemy interaction/damage/HUD progression, not complete projectile or
effect fidelity. Additional unsubmitted paths:821A7898 primitive13 stride44,
VS4001dbc0 with PS4001f550 or4001e320;821A7D88 stride36, primitive13/24vertices
or primitive6/4vertices, VS4001e8c0/PS40008d30. Shader/layout diagnostics are needed
to implement those remaining paths. The capture limit was reached; no further
frames were inferred from the continued live log.

Added source-fingerprint/VS_3D/PS_Main-gated native scene strip adapter. Requires
primitive6, stride16,3..16384 vertices and float3 POSITION0 at0 plus D3DCOLOR at12,
with exact semantics/count (padding ignored). This layout is still a hypothesis
until runtime accepts it; mismatches reject and trigger the detailed diagnostics.
Uses existing material bindings, reversed-depth variant, scene target, viewport
and decoded render state. Converts the sequential strip to triangle-list indices
with alternating winding, preserving degenerates. Tests cover all index positions
up to65536 vertices, invalid limits and WARP pixel equivalence of converted solid/
textured strips against the quad path. They do not yet prove the live3D transform.

Deliberately stopped verified39944 after preserving captures. Full native-clean
build and all14 suites pass.6188 started10:24:42 with prefix native-utility3d and
the same roadway/fire schedule and interval300. Includes the new3D adapter,
detailed unsubmitted shader/declaration/target diagnostics, native VSync setting
and corrected simulation-clock UI. No renderer/reader flags are required. Live
3D acceptance/pixels remain pending; retain this run. Earlier package remains old.

6188 startup diagnostics resolve the earlier1771-draw ambiguity: at10:24:56 the
native active_scene is40001cd0, guest surface40007c00 is registered, and bound
shaders are exact Utility VS_2DTex/PS_Tex. Declaration is the already supported
20-byte POSITION0/UV8/COLOR16 layout. This is a2D Utility draw into the scene
pass, not a draw with no active rendering scope. The current2D adapter only
accepts ordinary output, so extend its target routing with verified scene-depth
handling rather than dismissing these draws as startup noise.

### Scene-targeted Utility 2D adapter compiled (live validation pending)

The adapter now chooses the active HDR scene before ordinary output, retaining
the exact Utility source, entry, declaration, topology and binding gates. Scene
begin records the actual device color surface after the original begin returns;
each Utility draw must match that surface. Feedback checks cover the actual
native color attachment for both shader stages. Scene draws use the existing
reverse-depth vertex bindings and decoded render state; ordinary output retains
its no-depth restriction because it has no depth attachment.

The bridge object compiled and all14 existing suites passed. These suites cover
the native depth/reversed-shader and mesh components, not live acceptance of this
new routing. PID6188 remains on the previous executable (confirmed live at10:31)
to preserve the pending mission coverage run; the new object is not linked or
deployed yet. No claim that the1771 missing draws are fixed in a live run yet.

Added WARP pixel tests for both solid and textured Utility scene overlays using
the retail vertex layouts, an HDR color target and D32S8 depth attachment. Normal
and reversed shader/viewport variants must produce the expected tint/UV pixels
with the matching LESS/GREATER test, and leave every clear pixel untouched with
the opposite comparison. The expanded quad suite passes. This verifies component
depth behavior, not the live scene-surface identity gate. Recompiled8219C7A8 also
confirms owner+8 is the device and that the viewport setup precedes return, so
recording its surface after the original begin observes the completed setup.

### Live 3D Utility acceptance, 10:33:12

PID6188 accepted the first five20-vertex Utility3D strips with reversed depth
in scene40001cd0 at10:33:12.984. The fingerprint/name/layout gate therefore
matches the actual mission route, rather than merely a source-derived hypothesis.
Output300 shows the intro city, civilians and mothership; it is visibly very
bright, so this capture is not evidence of correct exposure or complete fidelity.

Loading at10:33:06/09 also confirms caller8241E2CC's stride8 XUI path targets
scene40001cd0/surface40007c00. Its actual shaders did not match registered source
shaders, so cached active shader labels are insufficient. Added bounded diagnostics
for actual bound handles, embedded shader addresses, reverse-depth and depth state
to resolve this before extending the XUI adapter. These diagnostics and the2D
scene adapter are still pending deployment; retain6188 for later effect coverage.

### Particle and textured scene immediate adapters

6188 identified Vs_Particle/Ps_Particle fingerprint777f4cf51fb1b019 at10:34:05,
then Ps_ZParticle from the same source at10:35:35. The44-byte layout is float3
POSITION0@0, float2 TEXCOORD0@12, float2 TEXCOORD1@20 and float4 COLOR0@28.
Recovered source rotates/scales the particle corner in view space, projects it,
and samples texture times color; Ps_ZParticle clips sampled alpha below0.5 and
forces sampled alpha to1 before color multiplication. Use these compiled source
entries directly, not a substitute particle shader.

At10:34:44 and10:35:38, VS_3DTex/PS_Tex Utility source uses36-byte vertices:
float3 POSITION0@0, float2 TEXCOORD0@12, float4 COLOR0@20, with quads or strips.
Extended the native scene immediate adapter for these exact source/name/layout
contracts. It validates the captured scene color surface, handles texture feedback
and missing inputs, uses reverse-depth bindings and decoded native render state,
and emits indexed quads/strips. Added first-success reporting per variant.

Bridge compiled and all14 existing suites passed before the reporting-only change.
New particle/textured adapters still require dedicated pixel tests and live
acceptance; current6188 remains the previous executable. No effect-fidelity claim.

### Particle component tests and deployment

Added WARP tests using the44-byte particle layout and recovered billboard math.
Captured clip positions verify angle/radius offsets and normal/reversed depth;
per-pixel checks verify UVs, float4 tint, sampled alpha, and Ps_ZParticle rejection
of a low-alpha texel plus alpha replacement on the surviving texel. Tests pass;
identity matrices here do not establish all live view/projection behavior.

Deliberately stopped verified6188 at10:38:21 after17.85M indexed submissions with
zero indexed errors. Its logs/captures remain intact. Full native-clean rebuild
and all14 suites passed. Started a fresh native-particles probe with the same
combat schedule and capture interval300, now including scene Utility2D routing,
particle/textured3D adapters, surface validation and embedded/depth diagnostics.
Live acceptance of these new routes remains pending.

### Live startup gap closed; XUI scene routing prepared

PID60316 (native-particles, started10:38:50) accepted VS_2DTex/PS_Tex scene draws
with reverse_depth=true at10:39:03. At10:40:13 its immediate coverage totals were
3,679,451 requests/submissions, zero empty/unsubmitted draws. This closes the
observed startup1771-draw gap for this run, not all mission/effect coverage.

Extended the existing fingerprint/address-gated XUI adapter to select the active
scene before ordinary output, validate its recorded guest surface and check
feedback against its actual native color target. Scene draws can use a lazily
compiled reversed vertex shader, with every constant uploaded to the selected
variant; ordinary output retains the depth restriction. The position-only input
signature is unchanged, so the stream is shared. Bridge object compiles. Added
WARP tests sharing that stream across normal/reversed XUI shaders with a D32S8
attachment: expected textured pixels pass the matching depth comparison and all
pixels remain clear under the opposite comparison. Expanded XUI suite passes.
This XUI change is not deployed;60316 remains on the particle/Utility build to
collect mission acceptance and actual embedded shader diagnostics first.

Added component coverage for the36-byte VS_3DTex layout alongside particles:
float3 position, float2 UV and float4 color are verified through clip capture and
per-pixel reads in normal/reversed shader variants, using both quad and converted
triangle-strip indices. The recovered view/projection arithmetic is exercised
with identity matrices; this is not a complete live transform/fidelity test.
Native-particles host capture shows the title/new-game-data prompt clearly.
At10:42:23 immediate coverage remains11,135,816 submitted, zero unsubmitted;
mission adapter acceptance is still pending in this live run.

### Fetch descriptor packet ownership

Audited recompiled8213DDA0: it scans a high-bit descriptor mask, reads six-word
groups from device+1024, emits fetch-register18432 packets plus GPU cache-control
packets and updates cursor40. It does not alter the source descriptors or clear
the caller's dirty flags. Immediate caller821FD428 still performs derived-state
updates and clears device+16 after this call; its later allocation/draw encoding
remains in place and must not be skipped wholesale.

Added a DDA0 hook that omits packet encoding only inside successful native draw
ownership, for the same device and a nonzero high32-only mask with nonoverflowing
descriptor range. Unknown/mixed masks and unsupported draws retain the original
encoder. Tests cover all64 mask bits, combined/empty/mixed masks, device mismatch,
disabled nested scopes, restoration and overflow. Bridge object compiles and all14
suites pass. This and XUI scene routing remain undeployed while60316 completes
the current particle coverage run. Runtime regression validation is still needed.

60316 reached loading/mission at10:47:13..19. The previously ambiguous XUI draws
have actual embedded sources820608B0/82060EC8, float2 POSITION0 layout, reversed
viewport and depth word00700764 on scene40001cd0/surface40007c00. This confirms
the pending scene-XUI adapter's identity gate matches the missing path. Added
that actual depth word to the normal/reversed XUI pixel tests; the suite passes.
Solid3D Utility also still accepts live with the new scene-surface identity gate.
Particle/VS_3DTex acceptance remains pending later in the same live mission.

60316 accepted Vs_Particle/Ps_Particle stride44 at10:48:15 and VS_3DTex/PS_Tex
stride36 with24 vertices at10:48:52. Both use reversed depth and the checked scene
surface. Output2100 shows the player, allies, city, radar and AF14 HUD; it does
not isolate an effect or establish visual equivalence. The missed-draw total was
stable at28,270 from the two sampled loading-XUI signatures through10:48:33.
Alpha-tested particles and textured strips still need first-success confirmation.

Expanded fetch-ownership tests to include isolation from another thread and
restoration after an exception in a nested scope; the effect suite passes. The
pending fetch-encoding omission and scene-XUI extension remain undeployed.

### All observed scene-immediate variants accepted; XUI/fetch build deployed

60316 accepted the textured strip variant at10:49:45 and Ps_ZParticle at10:49:46,
both with reversed depth. All five currently supported scene-immediate variants
have now passed their live identity/layout/target gates. At10:50:03 coverage was
21,345,764 requests,21,317,494 submitted,28,270 unsubmitted, still only the two
loading-XUI signatures. This is bounded route acceptance, not complete fidelity
or all-mission coverage. Logs and captures through output4500 remain intact.

Deliberately stopped verified60316 to deploy the pending changes. Full clean
native build and all14 suites passed. Started native-scene-xui with the same
schedule and capture settings, including XUI scene/reversed-depth routing and
scoped fetch-descriptor packet omission. Runtime regression/coverage validation
of this build is pending; no completion claim.

32992 started native-scene-xui at10:51:02. Startup logs confirm scoped fetch
packet omission for masks8000000100000000 andFFFFFFFF00000000. At10:51:26,
366,463 immediate requests all submitted, with no unsubmitted paths. Loading
XUI and mission regression validation remain pending.

Prepared a separate immediate mesh cache (4MiB/256entries) for Utility2D and
scene-immediate adapters. It reuses the existing full-byte comparison cache:
keys identify guest data/topology/declaration/VS/depth variant; every declaration,
vertex and index byte, stride, index width and shader bytecode must still match.
Mutable/reused guest pointers cannot produce stale hits. Source shader teardown
clears both caches. Isolation avoids displacing long-lived indexed meshes.
Added10s build/hit/storage/eviction diagnostics. Bridge object compiles/all14
suites pass; expanded particle/textured pixel tests also pass through actual
cache hits. This optimization is not deployed and has no measured speedup yet.

Added explicit indexed coverage classification under the existing draw lock:
all hook requests are classified as empty, native-submitted or nonempty missed,
including calls outside scene scope that stop undergoing upload inspection after
the first20 draws. First64 missed signatures report caller/topology/scope and
actual bound shader/surface/declaration/index-buffer handles; cumulative totals
report every10s. Existing upload/error counters are retained, but no longer need
to stand in for coverage. Bridge compiles. These diagnostics and immediate mesh
caching remain pending deployment;32992 is still the scene-XUI/fetch build.

### Native multisample resource foundation (not enabled in game)

32992 logs a640x736 guest depth allocation with MSAA enum1 that the native
allocation hook rejects; full scene storage remains forced single-sample. Added
optional1/2/4-sample native color/depth resource creation with device format/quality
checks. Color targets keep a separate single-sample sampled texture; explicit
resolve now uses D3D11 ResolveSubresource for multisample surfaces and CopyResource
for single-sample surfaces. Converted luminance/RGBA targets remain single-sample.

WARP tests verify2x/4x color/depth dimensions/sample counts, depth/stencil clears,
unwritten resolve validity, exact HDR clear pixels through native resolve and
rejection of unsupported counts. Expanded texture suite passes. This does not
yet test per-sample edge averaging or enable MSAA in the game. Guest enum/sample
mapping, full-scene integration and multisampled depth/capture handling remain
required before enabling it; existing game callers retain default sample count1.

32992 accepted scene-targeted XUI with reverse_depth=true during loading at
10:59:32. At11:00:06 immediate coverage was20,123,119 requests/submissions with
zero empty or unsubmitted draws and zero sampled missing paths. This removes
the previously observed loading-XUI gap for this run; later combat, indexed
coverage and full visual fidelity remain separate validation requirements.

Added a native MSAA per-sample test: clear every sample blue, write red only to
sample0 with the native sample mask, then resolve. Every output pixel must be
half-red/half-blue for2x and quarter-red/three-quarter-blue for4x, including alpha.
Expanded texture suite passes, proving sample averaging rather than merely clear
copy behavior. MSAA is still not integrated into the live scene path.

Prepared multisample color diagnostics for scene integration: pixel reads and
BMP captures resolve2x/4x RGBA8/HDR surfaces into a private single-sample texture
before staging readback. They do not alter the engine's sampled texture or its
validity/resolve timing. Tests compare direct multisample diagnostic pixels/BMPs
against the explicit resolved image with unequal sample contents; texture suite
passes. Depth inspection remains a separate unsupported multisample diagnostic.

Scene allocation now obtains the actual bound guest color surface after scene
begin, validates its recorded MSAA mode0/1/2, and allocates native1x/2x/4x HDR
color plus matching D32S8 depth at full scene resolution. Sample-mode changes
recreate the scene resources. Color resolves use the native MSAA path; no eDRAM
tile layout is reproduced. Unsupported modes fail explicitly. Color diagnostics
use private resolves; the optional depth-coverage diagnostic reports unavailable
for multisample scenes rather than inventing a depth resolve or aborting color
capture. This does not change game depth testing/clears.

Bridge object and rebuilt quad/XUI suites compile; the14-suite run passes.
Live scene MSAA verification is pending deployment alongside immediate caching
and indexed coverage diagnostics. Current32992 still runs the previous build.

### MSAA/cache/coverage build deployed

Deliberately stopped verified32992 at11:04:24 after16.724M indexed submissions
with zero indexed errors; immediate coverage remained zero-miss through the
combat sequence. Its captures/logs are preserved. Rebuilt the complete native-clean
target and all dependent tests; all14 suites passed. Started PID48852 at11:04:51
with prefix native-msaa and the same scripted combat/capture settings. This build
includes native scene sample selection, MSAA color diagnostics, bounded immediate
mesh caching and full indexed request classification. Live validation is pending.

48852 confirms1280x720 native2x scene allocation at11:04:53. Startup remains
zero-miss, but at11:05:34 immediate cache statistics show26,153 builds/71 hits
and only4 entries: scratch guest addresses cycle across different geometry.
Changed candidate keys to a64-bit vertex-content hash plus declaration, shader,
primitive and depth variant. Full declaration/vertex/index byte and shader
identity comparisons still decide reuse, so collisions can only cause misses,
not stale rendering. Bounds remain4MiB/256entries. Added identical-content at
different-address and mutation/variant tests, and used content keys in existing
cached particle/textured pixel tests. Bridge compiles and quad suite passes.
The key change is not deployed; no improved cache-hit/performance claim yet.

Expanded immediate cache tests with an A/B/A content sequence: both candidate
meshes coexist, A's original GPU mesh is reused without a third build, then its
clip/pixel checks pass. Expanded2x/4x MSAA tests to write depth only into sample0
and draw again with LESS: sample0 must reject while the remaining samples pass,
producing exact complementary red/blue resolve fractions. Quad and texture suites
pass. These test results do not replace pending live mission MSAA validation.

Prepared `edf_native_output_capture_limit` (default32, clamped0..128) so later
combat/retry validation can retain bounded captures beyond the previous hard32
cap. Interval0 still disables only periodic captures; limit0 disables output
BMPs, and a nonempty scene prefix is always required. Existing no-overwrite and
initialized/indexed-output gates remain. This does not guarantee captures of
menus with no indexed output. Bridge compiles; the limit option and content-key
cache change are not deployed in48852. Future long probes can request limit64.

48852 entered native2x mission rendering at11:13:21. Indexed coverage at11:13:41
is680,005 requests/submissions, zero empty/unsubmitted; immediate coverage at
11:13:54 is20,119,985 submitted with zero misses. Native HDR resolves remain
initialized. Output300 shows the intro city/civilians/mothership, with the same
visibly bright intro issue observed before MSAA; no exposure/fidelity equivalence
claim. Actual indexed coverage now supports this specific mission interval rather
than relying only on upload counts. Combat MSAA behavior remains pending.

The first-five-output bloom-constant log occurs before mission entry and cannot
explain the bright intro capture. Added10s MiddleGray/LuminanceWhite reporting
while a scene-capture prefix is enabled, including indexed-frame/output-draw
counters to correlate with existing luminance-history pixel captures. No exposure
values were changed or clamped. Bridge compiles; this diagnostic remains pending
deployment with content-key caching and the extended capture limit.

### MSAA combat coverage and content-cache deployment

48852 accepted textured quads at11:14:53, textured strips at11:15:44 and
alpha-tested particles at11:15:45 with native2x scene storage. At11:16:23 indexed
coverage was9,724,214 submitted, zero missed; at11:16:24 immediate coverage was
21,503,166 submitted, zero missed. Output5100 shows the player/HUD on the roadway
under the bridge; full effect/exposure equivalence remains unproven.

Deliberately stopped the verified process after preserving captures/logs. Full
native-clean rebuild and all14 suites passed. Started native-content-cache with
the same combat schedule, interval300, and output capture limit64. This deploys
content-based immediate cache lookup and10s exposure-constant diagnostics. Later
retry/transition captures and measured cache effectiveness remain pending.

PID45952 started native-content-cache at11:17:23. At11:18:06 the immediate mesh
cache reports201 builds/25,962 hits,58,692bytes/201entries, no evictions; immediate
coverage is1,412,237 submitted with zero misses. The previous address-key run at
11:05:34 (~43s after start) reported26,153 builds/71 hits. This supports reduced
mesh allocation in a similar early sequence, not a controlled frame-rate or
handheld benchmark. Retain the content-key policy pending later combat metrics.
Exposure diagnostics are active (startup MiddleGray0.5/LuminanceWhite1.5); mission
values and later64-capture sequence remain pending. No source changes this audit.

Staged an experimental snapshot at out/package/native-content-cache-20260909
without deleting or modifying either older package. It contains the current exe,
rexruntime, required FidelityFX DLL, controller database, README, licenses and
NATIVE-BUILD.md with limitations/provenance. Exe SHA256 matches win-native-clean:
2F30B8D529025654122D341EB45AFEA1B25911394FDE412FC7350FA8EE7BB7B8.
PE imports of exe/rexruntime contain no Xenos dependency; no Xenos DLL is staged.
The package itself has not been independently launched;45952 remains the live
build-directory probe. This is not a completed release or handheld certification.

Independent package smoke PID5840 started11:20:56 with --settings, separate
requested user/cache paths, scripted input disabled and no scene capture. Module
inspection shows package-local rexruntime/FidelityFX and systemD3D11, no Xenos;
log confirms hardware native device and first3 host frames. The requested delayed
capture did not appear despite the process remaining responsive and its HWND
visible/nonminimized. Do not claim successful visual settings capture or package
gameplay. Deliberately stopped verified5840 after inspection;45952 remains live.
The delayed settings-capture issue needs investigation separate from the ongoing
combat probe. Package provenance updated with the limited smoke-test evidence.

### Package settings capture resolved; combat cache follow-up (11:29)

Repeated the independent package settings launch as PID30220 with explicit
`--name=value` arguments, including `--edf_native_host_capture_require_image=false`
and `--fullscreen=false`. The installed SDK CLI uses boolean flags: the earlier
separated `false` did not disable require-image, so a settings-only run waited
indefinitely for a game frame. The repeated probe captured after 1061 ms at
out/native-bridge-run/native-package-equals.bmp, with image=false and
Xenos_loaded=false. Visually inspected the complete settings panel in the GPU
readback. Native presentation still reports occluded on the isolated desktop;
this does not verify physical-screen presentation or package gameplay. Corrected
both READMEs to explicit boolean syntax and native VSync rather than plugin VSync.

Live build-directory PID45952 reached combat. Its output.5700 capture shows the
player, city and HUD at 200/200 health and 120/120 ammo. At 11:29:25 indexed
coverage was 12,058,815 requests, all submitted; at 11:29:27 immediate coverage
was 21,858,731 requests, all submitted. Neither counter proves visual fidelity.
The content-keyed cache's early-menu improvement does not extend uniformly to
combat: at 11:29:27 it had 1,223,714 builds, 1,561,112 hits and 1,223,458 entry
evictions, only 210,588 resident bytes. Changing immediate geometry still churns
immutable GPU meshes; investigate reusable dynamic geometry streams rather than
claiming this cache solves handheld allocation overhead. Mission MiddleGray=0.8
and LuminanceWhite=1.5 are observed engine values, not replacement exposure fixes.

### Native immediate dynamic vertices (11:36)

Replaced whole-content selection in both scene-immediate and Utility2D live
adapters with size/layout/shader/primitive/depth stream candidates. The immediate
cache now creates D3D11 dynamic vertex buffers with immutable indices. Exact
shader-bytecode, declaration, stride and index-byte comparison remains mandatory.
Equal vertex contents remain a cache hit; changed contents of the same size use
WRITE_DISCARD on the owning immediate context. This preserves queued draws and
reuses the buffer/input layout rather than recreating both for each mutation.
Changed sizes, index patterns, declarations or shaders rebuild; failed updates
invalidate the entry. Indexed/static mesh caching remains immutable by default.
Both modes account retained conversion attributes against their memory budget.

Full rebuild and all 14 suites passed. Expanded quad tests verify actual dynamic
buffer identity/usage, A/B/A UV pixel updates, an update after a queued draw but
before readback, rejection of wrong-size/immutable updates, shader/index-format
rebuilds, malformed-layout invalidation, and dynamic particle/textured strip/quad
layouts with normal/reversed clip and pixel tests. Live cache logs now separate
builds, hits and vertex updates. Driver-side renaming and real handheld FPS remain
unmeasured; this is not a claim that all transient allocations have disappeared.

Previous PID45952 was deliberately stopped after exact executable validation.
Its output.16500 capture shows player/allies/city/pickups, health200/200 and ammo
50/120 after scripted movement/fire. At 11:35:15 it had 33,801,238 indexed requests
with no misses; at 11:35:17,24,584,564 immediate requests with no misses, but
2,018,807 immediate mesh builds. Logs/captures are preserved.

New build-directory probe PID45412 started11:36:11 using the same scripted
schedule and native-dynamic-stream log/capture prefix,64 output captures maximum.
All CLI booleans use explicit equals syntax. Combat comparison remains pending;
the native-content-cache package is unchanged and excludes dynamic buffers.

### Immediate packet-allocation boundary implemented, not deployed (11:40)

Audited the complete generated FD428 routine in recomp.42, FD8F8 in recomp.10,
and allocator C328 in recomp.47. FD428 applies CPU-derived state, clears dirty
banks and restores temporary stride12256 before calling C328 at return address
821FD6E8. Its no-buffer branch restores cursor40 and returns without emitting
draw packets. FD8F8 checks that return before memcpy821E8320 and the cursor13076
commit. Its optional8252B718 callback is a bare return in this retail build.

Added a C328 hook restricted to that exact call site, same native-submitted
immediate device,16-byte alignment and the exact captured vertex payload word
count. Indexed scopes do not grant this ownership. Returning no guest storage
uses the existing no-buffer branch only after actual native submission; it does
not call the allocator or set its allocation-failure flag at10809. GPU-only
restore bit4096 and scratch13076/13080/13088 are not updated because the native
draw did not overwrite guest GPU fetch state. Unrelated allocation callers and
unsupported/nonnative draws retain the original allocator. CPU dirty/derived
updates before the allocation are deliberately retained, not reimplemented.

Ownership tests pass for exact arguments, wrong device/caller/count/alignment,
zero/unaligned/oversized/wrapping extents, indexed/unsubmitted nested scopes,
thread isolation and exception restoration. The bridge object compiles. These
are gate tests, not runtime equivalence proof. The executable has NOT been
relinked or restarted:45412 still runs dynamic buffers without this new bypass,
so its upcoming combat run remains a separate validation. At11:40:14 it had
12,713,171 immediate requests with no misses and3 mesh builds/544,199 updates.
Do not report live packet-allocation omission until a subsequent build/run
actually executes this hook and verifies gameplay/state transitions.

### Recompiled immediate-tail integration fixture (11:45)

Added edf_native_immediate_tail_tests. Its build step extracts FD428, FD8F8 and
their four register save/restore helpers unchanged from the current generated
sources. No generated retail implementation is copied into the checked-in test.
Controlled allocator, packet and derived-state helper dependencies let the test
execute both original-storage and native-owned no-storage paths, including a
command-cursor rollover. It asserts the same CPU helper sequence, cleared dirty
banks, restored temporary stride, unchanged allocation-failure flags, restored
nonvolatile registers/SP/LR, and preserved cursor on the native-owned exit.
Native-owned execution emits no draw packets, copies no vertex payload, and
leaves GPU scratch fields unchanged. Unowned execution still emits packets,
copies the payload and sets the original GPU scratch/restore fields.

All15 suites pass after rebuilding the new fixture. This verifies actual wrapper
control flow with controlled dependencies, not the full implementations of the
mocked state helpers or runtime compatibility across all callers. The bypass
still awaits relinking and live gameplay validation. Do not treat it as deployed.

PID45412 remains live on the dynamic-buffer-only executable and has now entered
the mission introduction. At11:45:04 it reports20,080,003 immediate requests,
all submitted,4 mesh builds and1,385,212 vertex updates with no evictions. The
preceding indexed report has653,383 requests, all submitted. Native scene VS_3D
is active; combat and later transition captures remain the next checks.

### Dynamic-buffer combat evidence and package-tail deployment (11:49)

Extended the actual-wrapper fixture to strides8/12/16/20/36/44 and vertex counts
4/20, with owned/unowned paths and cursor rollover:48 combinations pass. After
stopping the validated PID45412, the complete executable was rebuilt and all15
suites passed again. Dynamic-only captures are preserved: output.2700 shows
ammo60/120; output.3600 shows the player moved under the bridge with120/120 ammo.
At11:46:44 dynamic buffers had11 builds,17,521 hits,2,064,296 updates and no
evictions. Immediate coverage20,979,864 and indexed coverage6,063,720 requests
had no misses. Later log records VS_3DTex as well as Vs_Particle live submission.
This is scoped combat evidence, not a full mission/handheld performance claim.

Created a fresh package out/package/native-tail-20260909 without overwriting
older packages. Includes the new packet bypass, dynamic buffers, native MSAA,
required runtime DLLs, controller database, licenses and corrected README.
Executable SHA256 matches the rebuilt win-native-clean executable:
F34B8374859DD3D3D252E6B66F52D7693F8A4D06370CFC5A6EE44F96B83DA5AC.

Package gameplay PID59756 started11:48:46 from that package directory using the
existing test profile/cache and the native-package-tail log/capture prefix.
Read-only module inspection verifies package-local rexruntime/FidelityFX and
system D3D11, no Xenos module. The host publishes an image at11:48:48 (presentation
still occluded). At11:49:00 the new immediate-allocation hook executes and logs
omission of Xbox vertex allocation/copy/draw packets. Package mission/combat
validation is still pending; this is no longer merely an object-file change.

Logs rotate at about5MB and retain .1.log/.2.log alongside the active .log;
inspect those too when looking for startup evidence. Recorded earlier metrics
were inspected at the time, not guaranteed to survive later log rotation.

### Indexed draw packet-loop removal built, not deployed (11:55)

Audited FE358 in generated recomp.48 through its final epilogue. Its CPU dirty
state prefix ends at FE5B0; the remaining loop reads the index buffer and emits
draw/query packets, advances cursor40 and chunks large index counts. It has no
analogous always-called allocation hook at that boundary.

Added tools/extract-native-indexed-tail.cmake to generate an alternate routine
from the unchanged CPU prefix plus the original register/stack epilogue. Boundary
lookup fails the build if the expected function/labels are missing or reordered.
The original generated source is not edited. The native indexed hook calls this
alternate only after successful native submission; unsupported/nonnative draws
still use the full original routine. Existing scoped constant/fetch omissions
remain; other state helper calls are retained, so this does not yet remove every
legacy state packet encoder. CPU behavior is not replaced with guessed defaults.

The fixture now also extracts the full FE358 and save/restore14 helpers. With
controlled state-helper dependencies, original and native-prefix executions at
counts3/6/6000, nonzero base/first index and dirty banks have identical device
bytes except cursor40. Both execute the expected CPU helper calls and restore
SP/LR/nonvolatile sentinels. The original emits draw packets; the native prefix
does not touch the packet buffer or advance cursor40. This is scoped fixture
evidence, not complete shader/state or query behavior validation.

Full win-native-clean rebuild and all15 suites pass. Build-directory exe includes
the indexed change; live PID59756 and the native-tail package do NOT. That package
continues its independent immediate-bypass run unchanged, with16,220,821 immediate
requests and zero observed misses at11:54:49. Wait for its mission/combat check
before replacing it, then validate indexed packet omission in a separate run.

### Package combat verified; indexed-tail run started (12:03)

Expanded indexed CPU-tail comparison to24 combinations: three index counts,
clean/dirty CPU banks,16/32-bit index headers and command-cursor rollover. Both
paths retain matching device state exceptcursor40; only the original packet
loop needs the rollover helper. All15 suites pass after a full build check.

Package PID59756 completed the scheduled movement/fire sequence. Inspected
native-package-tail.output.8700.bmp: player/city/pickups/HUD,200/200 health and
50/120 ammo. The alpha-clipped particle variant Ps_ZParticle is logged live.
At12:02:20 immediate coverage22,634,126 requests had zero misses, with26 mesh
builds,33,490 hits,3,326,116 updates and no evictions. At12:02:27 indexed
coverage19,124,073 requests also had zero misses. Package-local runtime/module
checks and this combat segment now provide independent package gameplay
evidence; this does not prove mission completion, all content or handheld FPS.

Stopped59756 deliberately after exact package-executable path validation and
preserved its logs/captures. New build-directory PID46772 started12:03:33 with
native-indexed-tail log/capture prefix and the same scripted schedule. It includes
the indexed CPU-prefix extraction as well as the immediate bypass/dynamic
buffers. Native-tail package remains unchanged and excludes the indexed change.
Indexed omission must still be observed during actual mission rendering.

### Audited state-bank packet omission expanded (not yet linked)

Audited DB60's direct copy loop and its D698 spill path: source words are read,
packet storage/cursor are written; dirty/derived source state is owned by callers.
Extended native-owned DB60 omission to the exact banks/extents used by FD428 and
FE358:18688/9984/40,8192/10240/16,8448/10316/21,8576/10400/5,
8704/10420/12,8832/10468/21,8960/10552/38,9088/10704/8
(bank/device-byte-offset/word-count). Matching device, exact source address,
nonempty bounded dirty mask and nonwrapping complete source extent are required.
Unknown/mismatched banks and draws without successful native ownership retain
the original encoder. Callers still execute CPU-derived updates and dirty clears,
including the11568 store associated with bank18688; no CPU defaults are invented.

The fixture now extracts actual DB60 plus save/restore26. For all eight complete
banks it verifies original packet headers/payloads/cursor, unchanged CPU source
bytes, and no packet/cursor writes under native ownership. Overflow helper calls
remain controlled and are not exercised by these bounded-buffer fixtures. Gate
tests cover every dirty-mask bit, wrong sources, mixed masks and end overflow.
The first expanded fixture failed due to a1MB stack snapshot; moved that snapshot
to heap storage. After rebuilding tests, all15 suites pass and the bridge object
compiles. The running46772 executable remains the prior indexed-tail build: this
expanded bank omission is source/object-only until the next executable link/run.

### Vector-state packet copy omitted in source (12:11)

Completed the DC20 copy-loop audit: it copies six possible float4 records from
device10144 and control word10268 into command packets, using D698 for a spill;
its only direct device write is cursor40. The native-owned DC20 hook requires
the matching device, nonempty high-six-bit mask and nonwrapping source extent.
It preserves CPU records and the caller's bank32 dirty clear. Unknown masks and
unowned draws retain the original. This removes redundant packet encoding; it
does not add or prove support for native user clip planes.

Added actual generated DC20 and save/restore23 to the fixture. First/last,
noncontiguous and full vector masks exercise original and native-owned paths.
The fixture verifies original control-word preamble/cursor advancement, unchanged
CPU records and no packet writes under native ownership. Gate tests cover all64
mask bits, wrong device, empty/mixed masks, nested masking/restoration and source
end overflow. Spill handling remains a controlled dependency in these bounded
tests. All15 suites pass; bridge object compiles. No executable relink yet.

Live46772 remains the indexed-tail build without expanded DB60/DC20 omission.
At12:11:06 it reports18,442,442 immediate requests with no missed submissions;
its scheduled mission entry is still ahead. No restart was performed.

### Indexed-tail live scene/combat evidence; combined state bypass deployed

PID46772 executed the native indexed CPU-prefix path at12:12:03, explicitly
logging retained CPU state and omitted Xbox draw packets. Mission output.600
renders the invasion scene; output.2700 shows the player/HUD at120/120 ammo and
output.3600 shows forward movement and60/120 ammo. At12:13:55 indexed coverage
was5,449,987 requests with no misses; at12:13:56 immediate coverage20,905,995
also had no misses. These are scoped live scene/movement/fire checks, not a
full mission or visual-equivalence proof. Logs/captures remain preserved.

Stopped46772 after exact executable-path validation. Rebuilt the full executable
with expanded DB60 bank omission and DC20 vector-state omission; all15 suites
passed. Started PID58616 at12:14:54 with native-state-tail log/capture prefix and
the same conservative scripted schedule. This combines native indexed CPU-tail,
immediate allocation/packet bypass, dynamic buffers and expanded state encoders.
Its mission/combat validation remains pending. Existing packages were not
modified to contain this executable; native-tail package still lacks these later
indexed/state changes and retains its earlier independent combat evidence.

### Special-render packet helper replaced in source (12:18)

Audited complete D938 in recomp.14. Its simple and tiled branches read device
flags/target records and emit packets; direct device writes affect onlycursor40.
Its important CPU output is r3 = incoming dirty mask with bit0x100 cleared.
Added native-owned omission restricted to the exact FD428/FE358 call sites,
same device,dirty0x100 present and mode0. The replacement preserves that complete
64-bit return mask. Other callers/modes/unowned draws retain the original helper.

The fixture extracts real D938 and save/restore20. Twenty combinations exercise
simple, forced tiled, extra tiled-state, matching-target and mismatched-target
branches with owned/unowned scopes and modes0/1. Tests verify original packet
emission, unchanged CPU source records, ABI restoration, exact dirty-mask return,
and no packet/cursor writes for native-owned mode0. Gate tests reject wrong
devices/callers/modes/missing dirty bit and verify nested scope behavior.
All15 suites pass and the bridge object compiles. This helper replacement is not
yet linked into the running executable.

Live58616 is still the combined DB60/DC20 indexed/immediate bypass build. DC20
omission was observed at12:15:07 during startup. At12:17:57 it reports9,466,764
immediate requests with no misses. Its mission/combat check remains pending;
no restart or package overwrite was performed for the new D938 source change.

### Shader-load packet helper isolated; expanded fixture passes (12:26)

Audited complete EAB0 in recomp.73. It reads a relocatable program-load table,
emits four-word packets and advances cursor40 (CF60 on rollover); it does not
update CPU program metadata. Native ownership now omits this helper only at
the three ECB0 return addresses8213EDC8/8213EF3C/8213F04C on the same device.
ECB0 and its CPU updates remain intact. Unknown callers/unowned draws retain
the original. This hook and D938 are still pending executable deployment.

Actual EAB0 plus save/restore25 are extracted into the tail fixture. Eighty
combinations cover absent/empty tables, one/three records, early zero-count
termination, owned/unowned scopes, all three permitted callers plus an unknown
caller, and two address regions requiring different GPU relocation results.
Assertions cover control/header/address/register/count words, command extent,
unchanged device/program source bytes, and SP/LR/nonvolatile register restoration.
Native-owned calls write no packets; unknown callers still emit original packets.
Real command-buffer rollover remains outside this bounded-buffer fixture.
All15 suites pass after rebuilding the expanded fixture.

Also audited complete EB68, E950 and D750 bodies. These are NOT pure packet
helpers: EB68 maintains shader cache IDs/stride snapshots and calls E070;
E950 updates10810 and11552/11560 alongside inline shader upload and calls
E070/E800; D750 computes10432, updates10809 and returns dirty|0x100 alongside
an inline packet. Their CPU behavior must be retained during further extraction;
no wholesale omission was added for these routines, and callees remain to audit.

Live PID58616 reached the mission with expanded DB60/DC20 bypass. Inspected
output2700 (player/city/HUD120/120 ammo) and3900 (moved under bridge, firing,
56/120 ammo,200/200 health). At12:26:37 immediate requests21,839,838 and at
12:26:43 indexed requests12,167,307 both report zero unsubmitted draws. This
verifies the initial movement/fire segment, not full mission completion or
visual equivalence. Logs and captures are preserved for the next comparison.

### Shader/special-render omissions linked and validation run started (12:27)

Stopped58616 after revalidating its exact executable path and preserving its
initial combat evidence. Full executable link and all15 suites pass, now with
D938 and EAB0 hooks included. Started PID3460 at12:27:41 with native-shader-tail
log/capture prefix and the same input schedule. At12:27:44 it publishes an image
with Xenos_loaded=false; native immediate submissions report zero errors during
startup. Presentation remains occluded on the isolated desktop. Actual mission
execution of the two new hooks and their combat validation are still pending.
Existing packages are unchanged and do not include these later source changes.

Both new hooks were observed executing at12:27:43 during startup, and again
on another guest thread at12:27:55. Module inspection finds build-local
rexruntime and system D3D11, with no Xenos module. Mission validation remains
pending despite this successful startup execution.

### Mixed derived-state routine separated in source (12:31)

Added tools/extract-native-derived-tail.cmake for D750. The generated alternate
retains the original routine except the isolated block between the10809 flag
clear and the branch toD918: command-buffer rollover plus the C0004600/15 packet
and cursor40 store are removed. CPU calculations/stores at10432 and10809,
the full64-bit dirty|0x100 return, and original ABI epilogue remain unchanged.
The entire normalized source routine is fingerprint-checked before extraction;
changed instructions require re-audit, not just matching old label boundaries.

A native-owned hook selects this alternate only for the same device and exact
ECB0 return address8213F368. Unowned/unknown callers execute original D750.
The actual original routine and extracted alternate run against paired guest
arenas in1536 fixture combinations: four control values, eight render modes,
four low-bit patterns, two flag states, three shader-metadata variants and
normal/overflow command cursors. Tests compare device/source state except
cursor40, exact dirty return, SP/LR/nonvolatile restoration, original packet
words, and no packets/rollover calls from the alternate. The matrix explicitly
asserts that packet emission and CPU flag set/clear branches were exercised.
Overflow uses the fixture's controlled CF60 dependency, not real buffer growth.

All15 suites pass (1.61s total) and the bridge object compiles. The new derived
CPU tail is not yet linked into the running game. PID3460 remains alive at
12:31:47 with D938/EAB0 active and startup submissions reporting zero errors;
its scheduled mission entry is still ahead. No restart/package overwrite was
performed for this source change.

### Xbox shader instruction patcher bypassed in source (12:35)

Audited complete E070 in recomp.12, including its instruction sorting/merging
loops and final copy loop. Intermediate writes are stack-local; its only
non-stack writes copy12-byte patched instructions to the r4 shader-code buffer.
The native hook omits this work only from the EB68/E950 call sites8213EC30 and
8213EA8C with r6 exactly equal to the native-owned device's12256 stride table.
Unknown callers, other tables and address wrap retain original behavior.
EB68/E950's surrounding shader cache IDs/stride snapshots/device flags remain
intact. Recovered HLSL/input layouts are already used by native submission.

Added actual E070 to the fixture. A sparse4GB guest reservation commits only
the1MB fixture and a4KB page for two fixed lookup tables. Seventy-two cases
cover0/1/3 instruction records, matched/missing vertex declarations, two shader
variants, owned/unowned scopes, both audited callers and an unknown caller.
Tests verify copy counts, destination ownership, unchanged surrounding CPU
data and all saved integer registers/SP/LR. Lookup-table contents are synthetic;
these cases do not prove retail microcode equivalence or every sorting branch.
All15 suites pass (1.83s total), including gate/nesting/address-wrap checks;
the bridge object compiles. E070 and D750 changes are not yet linked/deployed.

Also read complete E800, E678 and E748. E678/E748 patch only shader-code output
through copies from/to their stack temporaries. E800 additionally updates the
word pointed to by its r6 argument, which E950 receives from ECB0's stack+80;
that output must be preserved. No E800/E678/E748 omission is implemented yet.

PID3460 remains alive at12:35:35, with19,063,838 immediate requests and zero
unsubmitted draws. Its mission/combat check is still pending; no restart was
performed for the new shader-patcher source change.

At12:36:34 PID3460 is still responding and has entered the mission intro.
Inspected native-shader-tail.output.900.bmp: invasion ships, textured mothership
and city render. Movement/fire validation of this run is still ahead.

### Shader-output CPU result separated; shader-tail combat verified (12:40)

Added fingerprint-checked E800 extraction that preserves its original control
flow and CPU output through r6 but removes E678/E748 calls. These callees only
rewrite shader instruction bytes, as audited previously. Native hook selection
requires the exact E950 return address8213EA50 and matching device in r29;
E950 holds its incoming device in that nonvolatile register at this call.
Other callers/devices retain original E800. The whole normalized E800 body is
fingerprint-checked so changed instructions require an explicit re-audit.

The fixture now extracts actual E800/E678/E748 alongside the alternate. Twenty-
four paired cases cover0/1/3 destination records, missing/matching/mismatched
shader outputs, leading/trailing unmatched entries and all three early exits.
Assertions compare CPU data/output, preserved native ABI, both original patch
helpers exercised and no native helper calls/instruction rewrites. Gate tests
cover device/caller mismatch and nested ownership. All15 suites pass (1.79s).

Live PID3460 completed the scheduled late movement/fire segment. Inspected
output7500: player further down the street, firing, pickups/HUD55/120 ammo and
200/200 health. At12:40:34 indexed requests15,950,307 and at12:40:35 immediate
requests22,379,162 both report zero unsubmitted draws. This run includes D938
and EAB0 but not yet D750/E070/E800. It does not establish full mission success,
visual equivalence or handheld performance. Captures/logs remain preserved.

Stopped3460 after exact executable-path validation and rebuilt the full game
with D750 CPU-tail separation, E070 bypass and E800 CPU-output separation.
All15 suites pass after the executable link (1.66s). Started PID56688 at12:41:34
with native-microcode-tail log/capture prefix and the same input schedule.
It remains responding at12:41:45; E070 and E800 native paths are observed at
12:41:37 and immediate submissions report zero errors. Mission/combat validation
of this combined build is pending. Existing packages remain unchanged.

D750's native CPU-only path also executes at12:41:37. Read-only module inspection
finds system D3D11 and build-local rexruntime, with no Xenos module loaded.

### Shader-upload allocation/copy removed in source (12:46)

Added fingerprint-checked E950 extraction. It retains argument setup, optional
E800 CPU-output calculation,10810 flags and11552/11560 stride snapshots, but
removes D160 allocation, five-word command header, shader-byte copy, cache-flush
call, instruction rewriting and final command-cursor store. Its code destination
is zero because the E800 CPU-only variant never dereferences that argument;
the alternate calls that variant directly, and does not call E070.
Original generated files are unchanged. Unknown/unowned E950 callers still run
the original; native selection requires the same device and one of ECB0's exact
return addresses8213EFB8/8213F070.

Audited complete41AB8 before omitting its call: generated cache-flush operations
are comments, and actual writes affect only the caller's stack scratch. The
native path makes no guest allocation attempt, so it performs required CPU
updates without inventing allocation-failure flags.

The fixture extracts actual E950, save/restore19 and41AB8. Seventy-two paired
cases cover shader sizes12/24/48 bytes, modes0/1/3, two shader variants, both
initial10810 high-bit states and optional linked shader output. Original D160
is a controlled successful allocator; source shader bytes use a sparse committed
guest page atC0070000. The original executes actual output/microcode helpers.
Comparisons verify matching CPU data except command storage/cursor, original
packet header/extent, preserved flags/stride snapshots/native ABI, unchanged
source code, and no native allocation/copy/packet writes. These are successful-
allocation comparisons, not real allocator failure/rollover coverage.

All15 suites pass (1.80s) and the bridge object compiles. E950 separation is not
yet linked into the running executable. PID56688 remains responding at12:46:15;
at12:46:08 it reports14,272,443 immediate requests with zero unsubmitted draws.
Its mission check remains ahead; no restart or package overwrite was performed.

### Main derived-state inline packets removed in source (12:51)

Completed the full ECB0 audit. Added fingerprint-checked extraction removing
both inline packet regions and their CF60 calls. The early region retains
dirty bit46 and the10452 read consumed by the following CPU update. The late
region's only CPU effects are preserved explicitly: under10808 bit0x40, clear
dirty bit3 and, when bit48 is set, clear bits47/48. Remaining CPU stores and
helper calls stay in original order, including10408/10452/12424/10400/10404,
10810 flags, shader cache handling and D750's dirty-mask result. No generated
retail source is edited. Native selection requires the same device and exact
FD428/FE358 callers8213FD4FC/8213FE3F4; other draw entry points stay original.

The fixture extracts actual ECB0 and compares it with the alternate in672
cases: seven dirty masks, four10808 modes, linked/unlinked shaders, three10810
states, cache hit/miss and normal/overflow command cursors. Already-current
declarations are included so a lone bit47 is tested without always triggering
new shader dirtiness. Tests compare CPU bytes, full64-bit dirty result, helper
sequence, native ABI and no native packet writes/rollover. Both executions use
the separately-tested CPU-only E950/D750 helpers; EB68's cache outcome is
controlled. This isolates ECB0's inline regions, not the full shader-cache
implementation or real command-buffer growth. All15 suites pass (2.48s).

The bridge object compiles, but ECB0/E950 changes are not yet linked into the
running game. PID56688 is responding at12:51:24 with mission captures available;
indexed2,171,939 and immediate20,413,359 requests report zero unsubmitted draws.
Its movement/fire check is underway. Existing packages remain unchanged.

At12:52 inspected native-microcode-tail.output.3000.bmp (strafing,60/120 ammo)
and3900 (moved under the bridge,120/120 after reload). Both show200/200 health
and the city/HUD. This supplies initial movement/fire/reload evidence for the
D750/E070/E800 build. At12:52:28 immediate21,180,556 and at12:52:37 indexed
7,918,446 requests report zero unsubmitted draws. The later scripted segment
and full mission were not completed in this run. Evidence is preserved.

Stopped56688 after exact path validation. Full link now includes E950 and ECB0
CPU-tail replacements; all15 suites pass (2.30s). Started PID21552 at12:53:44
with native-main-state-tail log/capture prefix and the same scripted inputs.
Both new hooks execute at12:53:46, and the host publishes an image with
Xenos_loaded=false. Module inspection finds build-local rexruntime and system
D3D11, no Xenos module. It is responding; presentation remains occluded and
mission/combat validation of this combined build is pending. No package changed.

### Main-state integration now exercises the real shader cache (12:56)

Expanded the main-state matrix from672 to1680 paired cases. The original two
controlled cache modes remain, and three added modes execute actual recompiled
EB68: cached ID/stride match, mismatched ID with safe refresh, and in-flight
rejection based on the existing completion/current/last-use counters. Both shader
variants have real cache records. Tests explicitly require all three branches
to execute, compare resulting CPU data and helper sequence, and retain the
full dirty-mask/native packet-omission/ABI checks. E070 remains native-owned
inside this integration scope; the separately-tested E950/D750 CPU variants
remain shared dependencies of the comparison.

These new cases use existing nonzero declaration IDs, so global atomic ID
creation is not covered by this matrix. Real command-buffer growth remains
controlled. All15 suites pass (3.35s); this change affects only fixture/test
build wiring, not the running game's renderer. PID21552 remains the combined
ECB0/E950 build and has not been restarted for these tests.

### Remaining startup dependency and completion reset ordering audit (12:59)

Read complete D298 and D1C8. The current native D298 hook still executes original
D298 after erasing completion tracking. That routine first calls D1C8 when an
issued fence exists; D1C8 emits a small packet, waits through C928 and then waits
for device10868 to clear. Only afterward does D298 free/reallocate command and
writeback storage. D298 also builds startup packets through BD90/inline writes
and flushes through D0F8. These paths are not removed by the draw-tail work.

Checked local SDK video exports: VdInitializeRingBuffer and
VdEnableRingBufferRPtrWriteBack forward to IGraphicsSystem, or warn/return when
no GPU system is loaded. VdSetSystemCommandBufferGpuIdentifierAddress is empty.
Thus current startup avoids Xenos but still calls GPU-ring setup exports whose
no-GPU behavior is relied upon. Preserve D298's allocations, CPU counters,
writeback initialization and error paths when removing these calls.

Reset-order risk: completion queues are currently erased before D298's drain,
not at the post-drain/pre-free boundary. A subsequent flush may create a new
queue, but that does not itself verify correct preservation of outstanding
native completion tracking through reset. This is a source-identified lifecycle
risk, not an observed live hang. Before altering initialization, add a reset
test that requires old completion tracking to survive drain, be discarded before
writeback storage replacement, and retain any new initialization submissions.
Do not simply move cleanup after the whole initializer: that could erase newly
submitted initialization work. No startup/reset code was changed in this audit.

### Native initializer no longer calls GPU-ring setup in source (13:03)

Added fingerprint-checked D298 extraction preserving its CPU allocation,
writeback/counter initialization, cleanup and error paths. It omits calls to
VdInitializeRingBuffer, VdEnableRingBufferRPtrWriteBack and both
VdSetSystemCommandBufferGpuIdentifierAddress sites. A native callback is inserted
atD2BC, after the optional D1C8 drain and before storage frees. Completion queues,
published values and fault tracking are retired there, not at hook entry or
after new initialization. The native hook selects this alternate; diagnostic
nonnative behavior remains separate. Startup packets/BD90/D0F8 and D1C8's own
packet writes remain for later removal; this is not a packet-free initializer.

The fixture extracts actual D298/D1C8 and save/restore24. Forty paired cases
cover no-issued/issued work, teardown/reinitialize, borrowed/allocated buffers
and successful/failed allocation positions. Controlled wait/allocator/free/
submission/export dependencies enforce old tracking present during drain,
retirement before free, and new tracking retained after submission. Tests compare
CPU memory, allocation/free counts, return/ABI and zero native ring-export calls.
Physical mapping and GPU waits are controlled, not actual hardware reset tests.
All15 suites pass (3.31s), and the bridge object compiles. This initializer
replacement is not yet linked/deployed; PID21552 continues the main-state run.

### Main-state combat verified; native initializer deployment (13:04)

Inspected native-main-state-tail.output.3600.bmp: player moved under the bridge,
firing with10/120 ammo and200/200 health, city/HUD rendered. At13:04:21 indexed
6,132,736 and at13:04:27 immediate21,073,339 requests report zero unsubmitted
draws. This verifies the initial movement/fire segment with E950/ECB0 CPU tails,
not the later scripted segment or full mission. Logs/captures remain preserved.

Stopped21552 after exact path validation. Full executable link includes the
native D298 initializer; all15 suites pass (3.32s). Started PID59664 at13:05:21
with native-device-reset log/capture prefix and unchanged scripted inputs.
The native reset boundary executes during startup: fence submissions3/5 precede
the13:05:22.878 reset, and fresh submissions7/9/11 follow it. The host publishes
an image at13:05:23.925 with Xenos_loaded=false. No ring-setup export warnings
appear in this startup log; module inspection finds system D3D11 and build-local
rexruntime, no Xenos module. The process is responding. This supplies live
startup/reset-sequence evidence, not arbitrary reset stress or full teardown
coverage. Mission/combat validation remains pending; packages are unchanged.

### Packet-free native drain implemented and tested (13:14)

The native D1C8 hook now selects a fingerprint-checked extraction that removes
the two-word 1480/131072 packet, cursor update and packet-capacity check. It
retains the original C928 call with current issued fence, flags4/0, and the
device10868 CPU busy loop, plus original stack/LR/nonvolatile preservation.
Nonnative diagnostic mode retains original D1C8.

Read complete C928, CF60, CDC0 and C5F0 before this change. For pending current
fences outside recording mode, C928 invokes CF60. CF60 reaches CDC0; its normal
submission branch invokes the native C788 producer without an empty-command
size test. Recording/special-mode branches remain unchanged. C5F0 emits optional
cache-range packets but does not make this normal fence submission conditional.
Thus removing D1C8's packet does not remove the normal wait's submission trigger.
CF60/CDC0 still contain legacy command-buffer bookkeeping and packet paths;
their complete replacement remains outstanding.

Added64 native drain cases using actual extracted C928 and save/restore29:
zero/nonzero/wrapping fence values, pending/completed state, recording mode and
cursor overflow. Controlled CF60 advances issuance; controlled wait polling
publishes completion only on the third poll. Assertions cover submission/wait
branch selection, wait creation/cleanup, argument preservation, unchanged packet
cursor, zero packet writes and return ABI. These are not hardware-query tests;
the CPU busy loop is retained by extraction but not independently stress-tested.
The existing40 paired reset cases now select native D1C8, explicitly verify its
packet omission and compare remaining CPU storage, allocations and lifecycle.
All15 suites pass (3.35s); bridge object compiles. Not yet linked into the live
executable: PID59664 remains the preceding native-initializer build.

Correction to earlier audit language: D0F8 appends four packet words and only
calls CF60 on capacity overflow; it is not itself an unconditional flush or
submission. The reset fixture's D0F8 stand-in injects synthetic new tracking to
check callback ordering. This is not evidence of a real D0F8 fence submission;
comments now state that distinction. The live startup fence logs remain separate
evidence. D0F8's only generated direct caller is D298 and its packet append is
another remaining removal candidate.

At13:14 PID59664 remains responding and has reached the mission intro. Inspected
native-device-reset.output.900.bmp: mothership/city scene rendered, with very
bright highlights; this is rendering evidence, not reference-matched fidelity.
Do not count this as combat validation of the new drain, which is not deployed.

### Initializer startup packets removed; combined drain deployed (13:19)

The fingerprint-checked native D298 now omits D560..D680's startup-packet
construction, BD90 append, inline11-word packet and D0F8 append. All CPU
allocation, cleanup, writeback/counter initialization, command-storage metadata,
sentinel and success/failure/ABI paths before that region remain. The command
cursor stays at its initialized empty position instead of advancing over packets.
The removed region's fixed-table copies only fed stack-local packet data;
KiApcNormalRoutineNop was checked in the local SDK and simply returns0.
This does not remove command-storage allocation or the separate CF60/CDC0 paths.

Reset tests now execute actual extracted D0F8, replacing its earlier synthetic
tracking stand-in. Expanded to80 paired cases with segment divisors16/32, covering
both direct packet append and original D0F8 rollover. The controlled rollover
destination is now reset-specific; the generic destination overlapped a separate
fixture packet region and caused the first comparison to fail. Assertions check
original packet headers/extent, zero native append/rollover calls, untouched
native packet storage, empty native cursor, matching remaining CPU storage,
allocation failures/free counts, post-drain retirement and ABI. Allocators,
BD90 copy and rollover implementation remain controlled; this is not a hardware
reset/allocator integration test. The64 actual-C928 drain cases also still pass.
Full executable linked; all15 suites pass (3.18s), diff check clean apart from
existing line-ending warnings.

Before deployment, PID59664 completed the scheduled input window. Inspected
output5700 (firing under bridge,109/120 ammo,200health) and7800 (advanced into ants,
50/120 ammo,125health). At13:18:16 indexed15,941,041 and13:18:25 immediate22,333,088
requests report zero unsubmitted. This is scripted combat/damage evidence for the
preceding initializer build, not a full mission or visual-fidelity proof.
Stopped59664 after exact executable-path verification; captures/logs preserved.

Started PID21172 at13:19:05 with native-init-packet-free log/capture prefix and
unchanged scripted inputs. It contains both native D1C8 and packet-free D298.
Startup reset callback executes13:19:06.100; native fences3/5 follow. Another
post-drain reset executes13:19:06.353, followed by new fences7/9/etc. Host image
is true at13:19:07.352 with Xenos_loaded=false. Module inspection finds build-local
rexruntime and systemD3D11, no Xenos. Process responding, presentation still
occluded. This verifies startup/drain/reset progress; combined-build mission and
combat validation remains pending. Packages unchanged.

### Flush cache-range packet setup removed in source (13:25)

Audited complete CF60, C5F0, BE68 and C868. Important CPU dependency: C5F0
invokes BE68 to atomically replace device11544's dirty-range record with
0x00000000ffffffff, returning the prior range through stack outputs. Skipping
the whole helper would leave this CPU record unconsumed.

Added fingerprint-checked native C5F0 CPU extraction. It retains original dirty
and flag reads, actual BE68 atomic exchange when dirty, and save/restore19 ABI;
it omits packet allocation/construction and returns zero packet words. The
native-host hook selects it only for CF60's return address8213CF9C. CF60 thus
skips the now-unneeded cache-packet enqueue but still calls CDC0 and preserves
its optional issued-minus-two wait and10809 bit2 update. This does not yet remove
CDC0/C868's remaining command paths. The second C5F0 caller, ECD8, consumes packet
addresses and retains original behavior pending its own migration; it is not
silently redirected to a zero/uninitialized address.

Added32 cache-range cases using actual original C5F0 and BE68 versus native
extraction: sentinel/zero/normal/wrapping ranges, command-range flag, two address
regions and controlled allocation success/failure. Tests compare device CPU
state on success, atomic exchange counts, zero native packet storage/allocation,
output-word contract and ABI. Native mode needs no packet allocation, so an
artificial original allocation failure does not prevent native dirty-range
consumption; that intentional difference is asserted, not hidden. Atomic helper
execution is real; concurrent exchange contention is not stress-tested.

Added32 actual-CF60 integration cases varying recording/special mode, global
wait enable, prior wait bit and dirty range. These execute native C5F0 and BE68,
require the following CDC0 boundary once, preserve optional wait arguments/order
and flag update, and reject cache-packet enqueue/allocation. CDC0 and C928 are
controlled boundaries here, not hardware submission/wait integration evidence.
All15 suites pass (3.37s); bridge object compiles and diff check has only existing
line-ending warnings. New cache-range hook is not yet linked/deployed. PID21172
remains the responding packet-free-initializer/drain build, continuing its
unchanged scripted mission test. Packages unchanged.

### Native ring submission no longer writes the Xbox doorbell (13:29)

Audited full C410, C018 and BCE0. C410 emits three-word indirect-buffer packets
into the Xbox ring and writes the ring cursor to MMIO0x7fc80714. It also preserves
CPU10820 bookkeeping and invokes observer/callback hooks at19956/20100. BCE0
only computes capacity against GPU ring readback and waits through the generic
wait object; its return is not consumed by C410. It does not reserve CPU storage.

Added fingerprint-checked native C410 extraction omitting BCE0, three packet
stores and the doorbell write, while retaining CPU cursor arithmetic/update,
all callback branches/ordering/arguments, and save/restore21 ABI. Native C788
already owns GPU completion submission. Nonnative diagnostics retain original
C410. C018's linked command-list writes and CDC0 special/recording paths are
still separate remaining work; this is not a completely removed command system.

Added48 paired C410 cases varying descriptor count0/1/3, ring wrap, wait bit,
observer presence and callback presence. Original CPU instructions are extracted
unchanged except the single MMIO store is redirected to an asserted test sink;
the capacity wait is controlled. Original/native indirect callbacks execute via
fixture dispatch-table slots and their complete argument/order traces match.
Assertions compare remaining CPU memory and ABI, exercise original packet stores,
require zero native ring bytes/reservation/doorbell calls, and count all callback
branches. This is not physical Xbox MMIO or real concurrent ring-pressure testing.
All15 suites pass (3.52s) and the bridge object compiles. C410 and prior cache-range
changes are not yet linked into PID21172, which continues the initializer run.

### Cache-range and ring-path changes deployed (13:31)

Inspected preceding initializer-build captures3000 (strafing,59/120 ammo) and4200
(under bridge,120/120 after reload), both200health with city/HUD. At13:29:58 indexed
7,393,209 and immediate21,194,413 requests report zero unsubmitted. This covers
the initial scripted movement/fire/reload segment with native D1C8/D298, not the
later script or full mission. Stopped21172 after verifying its exact executable
path; evidence remains preserved.

Full link now includes native C5F0 cache-range CPU handling and C410 ring CPU
handling. All15 suites pass (3.43s). Started PID55784 at13:30:51 using native-ring-free
log/capture prefix and unchanged inputs. Native fences3/5 precede the startup
post-drain reset at13:30:52.828; new fences7/9/etc follow. Host produces an image
at13:30:53.844 with Xenos_loaded=false. Module inspection finds systemD3D11 and
build-local rexruntime, no Xenos. Process responding; visible presentation remains
occluded. Combined-build mission/combat and reset stress remain pending. Packages
unchanged. The prefix describes the removed C410 ring path, not a claim that all
remaining guest command-list/recording paths have been removed.

### Linked-list completion dependency audit (13:36)

Cross-checked the research callgraph/menu sets in ../edf2027-analysis and read
C0E0, BFD8, C9F0, EBA0 and D240 completely, plus ECD8's completion/list tail and
512D8's signal call site. C0E0 restores CPU command-buffer metadata from saved
fields. BFD8 patches list links/terminators and clears list capacity; these
cannot be treated as entirely unused CPU data without migrating their callers.

C9F0 constructs signal packets carrying a callback address and argument.
ECD8 supplies callback8214EBA0; EBA0 writes device10900 and signals a per-CPU
event selected through guest r13. Executing that callback at packet construction
would be premature: enclosing C868 submission and its busy increment occur
later. Executing it on an arbitrary host/guest thread would also require handling
the original per-CPU event choice. D240 forwards caller-provided callbacks, and
512D8 supplies another callback82151248 at return821513F4. These require native
completion/callback ownership, not unconditional removal of C9F0/C018 writes.
This is a source-identified dependency, not an observed new runtime hang.

Added diagnostic-only C9F0/C868 hooks, enabled with native host plus existing
hook-timing flag. They preserve original execution and guest state, sample the
first8 and power-of-two calls per known caller bucket (plus separate unknown
bucket), and separate C868 increment/nonincrement paths. Logs include signal
flags/callback/argument/guest TLS or list busy/increment/recording mode. This
avoids a frequent flush path suppressing rare known signal/list callers in the
sample stream. Tracing is not a functional replacement or exhaustive coverage.
Bridge object compiles; all15 suites pass (3.34s before the final sampling-only
adjustment, whose object was rebuilt). These hooks are not linked into PID55784
yet; it remains the ring/cache-removal build running the unchanged mission test.
Next runtime deployment should identify actual callback/mode usage before
changing linked submission and callback completion semantics. Goal remains open.

### Completion callback CPU contracts now executable tests (13:39)

Read the complete51248 callback and local SDK interrupt dispatch. SDK interrupt
execution holds a global critical region, uses a guest thread context and
temporarily clears its PCR TLS pointer; graphics dispatch also selects an active
CPU. A plain call from a renderer worker is not equivalent.

51248 increments15136 and computes pending pacing state from the callback's high
16 bits and device15124/15128. When pending is nonpositive, it reads Xbox timing
registers0x7fc86530/0x7fc86584, compares the resulting percentage to the argument's
low16 bits, then either retries through15132=1 or clears writeback+4 and updates
15128. Native replacement needs native timing semantics, not dispatch of this
unchanged callback with fabricated GPU registers.

Added18 actual-EBA0 cases (six CPU indices, three callback arguments) verifying
device10900 publication occurs before KeSetEvent, with exact per-CPU event address
and arguments. Added162 actual51248 cases varying previous/step/carry/threshold/
timing values, covering signed pending behavior, counter wrap, retry and writeback
acknowledgement. Only the two MMIO reads are replaced with asserted fixture
values; all callback CPU instructions execute unchanged. KeSetEvent is controlled,
not real kernel event scheduling. These establish existing CPU contracts; they
do not implement native callback dispatch or remove51248's runtime MMIO yet.
All15 suites pass (3.55s). PID55784 remains the live ring/cache-removal build and
has reached mission intro; callback-trace hooks still await deployment.

### Callback tracing deployed; active pending-work gap observed (13:43)

Preceding PID55784 reached initial combat: inspected ring-free output2400
(60/120 ammo) and3600 (moved under bridge,120/120 after reload), both200health.
At13:41:47 indexed7,115,240 and13:41:55 immediate21,236,472 requests report zero
unsubmitted. Later scripted combat/full mission not completed. Stopped55784 only
after exact path verification; all captures/logs preserved. Full linked trace
build passes15 suites (3.34s).

Started PID61344 at13:42:38 with native-signal-audit prefix and unchanged input.
The first startup trace already exercises all three known C9F0 callers:
D288 and ECD8/ED64 use EBA0, flags0x02000000/0x02000001, on guest TLS30038000;
512D8/513F4 uses51248 with flags0, argument0x10000, on guest TLS30024000.
C868 from ECD8/EE44 increments busy by1. Subsequent normal CDC0 submissions see
busy1, then2, while new ECD8 signal/list submissions continue. These are active
startup paths, not merely speculative rare-content paths. The samples establish
increasing pending work in this interval, not a complete proof of perpetual growth
or an observed reset hang. Native completion must handle these CPU signal/event
contracts before removing linked packets or claiming safe arbitrary drain/reset.

Process responds; module inspection finds build-local rexruntime and systemD3D11,
no Xenos. Rendering/presentation and gameplay logs remain distinct from callback
completion correctness. Prior zero-unsubmitted draw counts do not prove this
completion contract. Next priority is native signal retirement and correct guest
callback/pacing semantics, with a drain/reset integration test; packages unchanged.

### Native signal queue implemented; CPU worker retirement traced (13:49)

Read complete E328/E8E0 and the EAD0 worker loop. Worker state is device+10812:
E328 decrements its+56 field (device10868) when the final CPU command list is
retired, under the spinlock at+60. It also updates worker nesting/active state,
signals its event and participates in list traversal. EBA0 wakes that worker
through the per-CPU event. Directly decrementing busy after a D3D query would
skip CPU work and is not an acceptable replacement.

Added NativeSignalQueue backed by D3D11 event queries. Capture stores callback,
argument and CPU-mask metadata associated with a command byte range; capture
alone cannot complete. SubmitRange arms only fully covered signals after actual
submission, ordering signals by byte position and batches by submission order.
Poll returns completed payloads once, without executing guest code or changing
guest counters. In-flight payloads survive command-storage reuse. Capacity
includes staged and submitted signals; overlapping staged storage, partial
submission, invalid alignment/wrap and invalid callback masks are rejected.
Allocations occur before emitting an event. Earlier completed payloads are
delivered before a later query failure is reported on a subsequent poll.

The CPU-mask helper follows the original C9F0 high-six-bit mask/default4 rule;
the local SDK INTERRUPT implementation confirms ascending CPU-bit dispatch.
Added D3D11/WARP tests proving capture/submission separation, unrelated/partial
range rejection, byte/batch order, reuse, in-flight capacity, exactly-once delivery
and preceding rendering coverage through pixel readback. All15 suites pass
(3.55s). No Xenos packet processing is used by this queue.

This queue is built and tested but not yet connected to C9F0/C868 or guest-event
dispatch. PID61344 is still the earlier live trace build; its pending-work gap
is not fixed by an unused queue. Next implementation must capture command spans,
arm at actual submit boundaries, dispatch known CPU events only after completion,
and poll during drain without bypassing E328/worker processing. Native51248 pacing
semantics remain separate required work. Goal and packages remain unchanged.

### Signal integration live failure; missing worker submission boundary (14:02)

The subsequent bridge integration captures EBA0 spans at C9F0, arms queries at
C868, publishes completed arguments and calls the selected CPU's KeSetEvent.
Drain polls these queries; reset rejects discarding pending signals. This code
is now built, but live testing contradicts its correctness. Original signal
packets and the Xbox-specific 51248 pacing callback are still retained.

PID36616 (`native-worker-signals`) exited during startup. Added bounded entry/
return diagnostics around the unchanged E8E0 CPU worker, then reproduced with
PID52512 (`native-worker-entry`), exit -1073740791. Workers do wake and consume
the published argument. Sampled calls alternate between preserving busy with
a saved continuation and decrementing busy on the next wake; busy still grows.
This is not a missing KeSetEvent or a justification for manual busy decrement.

Added capture-failure context and reproduced with PID56224
(`native-worker-capture-error`), same exit code. At14:01:29 the explicit failure
is `unsubmitted native signal command storage overwritten`: physical begin
0x1f070a88, bytes100, argument0xdf0c0100, pending53, unsubmitted53.
All three processes are terminal; no run remains active. Evidence is preserved.

Source inspection identifies a missing submission boundary: E640 at8214E720
consumes a CPU-list 0x82 descriptor and directly calls C410 at8214E740 with
word-count/address pairs, bypassing C868. Conversely C868 can append work to
the CPU list without actually reaching C410. Arming solely at C868 therefore
both misses worker-issued ranges and treats CPU-list enqueue as submission.
Next change must track actual C410 descriptor submissions, preserve the busy
increment-before-dispatch ordering, and test chained worker continuation and
storage reuse. Do not simply discard stale captures or decrement busy.

Full build and all15 suites pass (3.73s) after diagnostic additions. Those suites
do not cover this integrated asynchronous worker/submission sequence; their
success does not establish runtime correctness. No release package was updated.

### Actual submission tracking and worker-slot backpressure (14:07)

Moved query arming from C868 enqueue to C410's actual descriptor submissions,
including the direct E640 worker path. C410 snapshots CPU word-count/address
pairs before observer callbacks; it does not interpret Xbox GPU packets.
A per-device nested submission scope blocks dispatch until enclosing C868 has
finished its busy increment. Scope entry serializes with dispatch through the
runtime global critical region, but does not hold that global lock across guest
observer callbacks. PID53840 (`native-worker-submit`) reached more submissions
but still failed with53 unsubmitted captures at storage reuse, exit -1073740791.

Added limited NativeSignalQueue::Poll delivery: completed batch remainders stay
queued, ordered and counted. Bridge dispatch now delivers one signal only when
the guest worker's publication slot (device10900) is empty. E8E0's hook services
in-flight native queries after the original worker returns, so a query submitted
by that worker can wake its saved continuation before the indefinite event wait.
It does not call CPU jobs itself or decrement busy. No dedicated polling thread
or Xbox GPU interrupt execution was introduced.

Extended WARP tests for zero-budget polls, one-signal delivery limits, retained
batch remainders and exact pending counts. Full build and all15 suites pass
(3.55s). Broader integrated scheduling/reset tests remain required.

PID32100 (`native-worker-slot`,14:05:42) passed the previous early storage-reuse
failure. Sampled busy values remain0/1 at enqueue,1/2 at completion, with worker
returns retiring to0/1 through1024 callbacks. This is evidence of improvement,
not proof of arbitrary workload correctness. At14:05:56 it terminated with a
different fatal error on worker thread55404: indirect guest call address zero.
Exit -1073740791, confirmed terminal; no process remains active. The log does
not include LR, so the precise indirect-call site remains unproven. E8E0 has an
indirect job call at8214E9D8 using worker+16, populated by CPU opcode81000000;
this is a next diagnostic candidate, not an established cause. Do not bypass
the null call or replace CPU job execution with a busy decrement.

Latest build is still experimental and fails at startup transition. Completion
ownership is improved but not complete; original signal packets/51248 pacing
and full gameplay/handheld validation remain outstanding. Packages unchanged.

### Malformed CPU list traced to an exact null-job call (14:12)

Added fingerprint-checked extraction of unchanged E8E0/E640 with diagnostic
callbacks only. E8E0 hash4f866c4c2947d1751b25dd7a7ace5245c9d368e513035b29ede8b39dc2f2db12;
E640 hash2bd0cadb2fd34840d05d0b5ba86b940368907dbb303fb3c4c278428dd6aaabfe.
The originals still execute every CPU instruction, including the failing call.
With hook timings enabled, a bounded32-entry thread-local history captures CPU
command cursors, words, saved continuations and list addresses. It is dumped
only for a low cursor or null job; no invalid work is skipped.

PID58884 (`native-worker-null`) failed at14:08:25 with a guest null read on the
worker, followed by captured-storage reuse after the worker stopped progressing.
PID61896 (`native-worker-history`,14:10:43) supplied stronger evidence: at14:10:56
the exact null call is LR8214E9D8, worker400051bc, callback0/job-argument0.
The previous list retired at C0000000. The next published list is df354f00;
its first word is0 and word+4 is1001c400. The worker interprets that low-high-bit
word as50176 jobs and advances to the unaligned cursor df355f09, then calls0.
This establishes a malformed/stale CPU list at execution, not merely a missing
function registration. The observed contents suggest reused storage, but the
writer that corrupted/reused this list is not yet proven.

Next audit target: native C788 still arms its completion query immediately at
fence construction, before C868/C410 and any deferred CPU-list traversal.
PublishNativeCompletion then advances the guest cursor/writeback when that D3D
event finishes. This can be earlier than the actual command consumption now
performed by the awakened workers. Moving signals alone did not migrate this
fence/storage-lifetime boundary. Track fences at actual descriptor submission
and preserve their ordering/lifetime; do not mask the bad header, drop a CPU job
or indiscriminately mark busy complete. A causal storage-lifetime test is needed.

Full diagnostic build succeeds; all15 suites pass (3.37s). These tests still do
not prove integrated list-storage lifetime. Both new processes are confirmed
terminal with exit -1073740791. No process or updated package remains running.

### Fence construction separated from actual submission (14:16)

NativeCompletionQueue now supports Capture(begin,bytes,value,cursor) and
SubmitRange. Capture enforces the issued+2 sequence, capacity, aligned/nonwrapping
ranges and no overlap with unsubmitted storage, but emits no D3D event. C788
captures its original40-byte reservation (starting at r4, unlike C9F0's r4+4),
retains CPU issued/cursor metadata and still emits no Xbox fence packet.
C410 arms matching captured fences only at actual descriptor consumption.
Poll cannot pass an unarmed earlier fence even if a later query has completed.
Native reset rejects discarding unsubmitted fences.

Added WARP regressions for construction-not-completion, partial submission
rejection, unconsumed storage overlap, unrelated ranges, out-of-order arming
without out-of-order publication, submitted-storage reuse, uint32 wrap and
cursor/value association. Later rendering completion is forced by readback
before checking that an earlier unsubmitted fence still prevents publication.
Full build and all15 suites pass (3.92s). This tests the native queue contract,
not a complete retail allocator/worker integration fixture.

Started PID63352 at14:14:50 (`native-fence-submit`) with unchanged scripted input,
scene captures and64 output capture slots. At14:15:39 the process is responding
and has passed the previous ~14-second failure transition. At2048 callbacks,
the worker retires busy to1; sampled enqueues show0/1. No null-job, invalid-cursor,
capture-overwrite or other error appears in this interval. Native XUI and draw
logs progress. This supports the premature-fence-lifetime hypothesis, but does
not identify the exact writer of the former corrupted list or prove full
mission/reset correctness. The reported44.8FPS is a diagnostic on this machine,
not a handheld benchmark. Process remains active for longer scripted validation;
do not relink its executable or restart solely because observation expires.
No package was updated; goal remains active.

Follow-up14:17: PID63352 remained alive/responding, but its log stopped advancing
at14:15:39 through14:16:28. This weakens the preceding progress report; UI
responsiveness is not proof that gameplay is advancing. Source audit found
39688's pending-fence loop only published native fence completions, without
servicing completed CPU-worker signals. Added PollNativeWorkerSignals before
the bridge lock/fence publication there, preserving the original wait logic.
That supplies progress when a pending fence depends on a not-yet-woken worker;
it is not proof that this was the observed stall's only cause. Stopped63352
after exact-path validation to deploy this implementation change (not because
an observation timed out); evidence preserved. Full build succeeds. Started
a new run with `native-fence-worker-wait` prefix; its live outcome is pending.

### Read-only stall evidence; legacy worker ring copy removed (14:24)

PID56064 (`native-fence-worker-wait`,14:17:20) also stopped log progression
around14:18:00 while remaining alive/responding. Added a read-only inspection
tool `tools/read-native-worker-state.ps1 -ProcessId <pid>`. It verifies the exact
build executable path, requests only PROCESS_VM_READ and closes its handle.
It decodes big-endian words and the generated Windows top-alias +0x1000 offset.
It does not suspend threads, write guest memory or alter counters. Snapshots
are non-atomic across fields and must not be treated as synchronized captures.

Repeated snapshots at14:20:30/14:21:02/14:22:21 show device40002780, issued4387,
completed4379, worker lock1/active1/nesting0/busy2, published0, cursor4000521c,
listdf4bb480. Ring cursor and mask are both32767, ring readback(writeback+60)0.
This is inconsistent with merely an undelivered wake; the CPU worker is active.
Source audit found E488 still calls BD90, which copies GPU packet words into
the ring and invokes BCE0's readback-based capacity wait. The observed wrap
state satisfies that wait's condition. No thread instruction pointer was
captured, so the exact blocked instruction is inferred from these facts.

Extended the fingerprint-checked ring extractor with BD90
(0544fb55a39571ece444277ad0cb26bb6b12b6aa99194b0bb43948b61e1aba28).
Native BD90 omits BCE0 and packet source reads/stores, retaining original loop,
masked CPU cursor update, ring-end return and saved-register ABI. It does not
fake GPU readback progress. E488's CPU predicate state and observer callbacks
remain unchanged. Added12 paired tests against extracted retail BD90 with a
controlled capacity helper: zero/nonzero counts, cursor wrap and multiwrap,
CPU memory/return/ABI equivalence, zero native reservations and ring writes.
All15 suites pass (3.34s); full link succeeds.

Stopped56064 after exact-path validation to deploy this implementation change;
all evidence retained. Started `native-ring-copy-free` with unchanged scripted
input,60-second image capture and64 output slots. Runtime result pending.
The separate51248/vblank audit confirms15124 is incremented by the legacy GPU
ISR and all four pacing counters remain0 in the stalled snapshot; native pacing
replacement still needs work. No package update or completion claim.

### Ring wrap survives; worker signal encoding removed in next build (14:28)

PID45764 (`native-ring-copy-free`,14:25:01) advances beyond the prior stall.
At14:26:07 read-only snapshot shows issued9029/completed9023, busy1, worker
lock0/active0 and ring cursor2075 after wrap, while GPU ring readback remains0.
At14:27:59 rendering logs still advance. Inspected60-second host capture: title
background and save-data confirmation dialog. Capture reports image=true,
Xenos_loaded=false. This validates continued UI progress across ring wrap, not
full gameplay, visible desktop presentation or handheld performance.

Extended the fingerprint-checked extractor for C9F0
(feb720408d49834c8742f51aca3078c1c237f1814893b32a5a132a61651c963f).
For the audited EBA0 worker callback only, native code now keeps the actual CPU
reservation/return/ABI but omits every encoded GPU signal/wait/interrupt word.
Native sideband capture at the hook still associates the reserved92/100 bytes
with callback/argument/CPU mask and actual C410 submission. No worker callback
is run during construction. Reservation padding remains a transitional CPU
layout dependency, not a GPU stream interpreted by a native packet processor.
51248 and unknown callbacks continue through the original encoder and still
require migration; this is not complete signal-path removal.

Added21 comparisons against extracted retail C9F0: seven flag combinations,
three arguments, reservation size, CPU memory, preserved registers/LR and zero
native packet bytes. All15 suites pass (3.30s); bridge object builds. Current
PID45764 predates this C9F0 change and remains running for longer scripted
validation. Do not relink its executable. New signal encoding removal still
awaits full link/deployment and live verification. Packages unchanged.

### Combat verified; native worker signal packet removal deployed (14:39)

The `native-ring-copy-free` run remained alive and rendering through14:38:26.
Inspected output5100 (14:36:29): player, city, HUD and combat effects.
Inspected output7500 (14:37:53): player farther along the street, shell casings,
pickups and ammunition116/120 rather than120/120. This verifies mission entry,
scripted movement and firing on the synchronized ring-copy-free build, not
mission completion/reset or handheld performance. At14:38:16 the non-atomic
read-only snapshot shows issued94847/completed94843, busy1, ring cursor23057,
ring readback0 and all four legacy pacing counters0. Captures/logs retained.

After the scripted sequence, explicitly stopped PID45764 following exact-path
validation to deploy the next implementation; it did not terminate itself.
Removed C9F0's two device10772 reads as well as its packet stores: those reads
only construct addresses for the removed GPU packets. The original routine's
fingerprint remains checked. Added21 detached-device regression invocations
with an invalid device address, proving the native reservation tail no longer
reads a device/writeback page. All15 suites pass (3.55s); full executable links.
Launched `native-worker-packet-free` with the same scripted input. This build
now includes EBA0 packet-write removal; runtime validation is pending. Unknown
callbacks and51248 are not claimed migrated. Packages remain unchanged.

### Architecture comparison: Unleashed Recompiled

Consulted the primary renderer source on2026-09-09:
https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/gpu/video.cpp

Its guest function hooks replace graphics operations including render targets,
textures and indexed/immediate drawing. DrawIndexedPrimitive queues a typed
host command; ProcDrawIndexedPrimitive issues drawIndexedInstanced. Immediate
vertex data is copied into host-managed upload storage. Native command lists,
frame fences and separate transfer work own execution/resource reuse. These
are graphics API operations, not a Xenos packet interpreter. Its inclusion of
XenosRecomp shader support is distinct from runtime GPU command emulation.

EDF implication (our design inference, not a claim about identical engines):
retain game-side rendering/job semantics, move remaining driver scheduling and
storage lifetime to host-owned commands/resources, and remove obsolete GPU
ring/MMIO work at that boundary. Existing descriptor-address sideband tracking
and reservation padding are transitional dependencies, not the desired final
ownership model. Do not replace real CPU worker jobs with no-ops or equate a
native image with complete graphics-driver removal. D3D11 versus D3D12/Vulkan
is a backend choice; boundary and lifetime ownership are the relevant lesson.

The pure NativeSwapPacingState model is tested against the original51248 CPU
callback and per-tick bookkeeping, including wrap and long-pause catch-up.
It is not integrated into the renderer. Before integrating it, establish which
remaining present callers genuinely require that compatibility contract and
which can be replaced by native presentation ownership. No GPU ISR/MMIO
replacement or native51248 completion delivery is claimed by those tests.

### Native present stops emitting the SDK swap packet (source/test,14:45)

Read the complete51460 body and the SDK VdSwap implementation. The latter
decodes a Xbox texture fetch, resolves its physical address and writes256 bytes
of fetch/PM4_XE_SWAP/NOP packets. It does not update the system-command-buffer
outputs that51460 consumes. EDF already publishes native output at8219C840
and through the movie path; the native host presents that host-owned texture.

Added fingerprint-checked extraction of51460
(3e00c92e5c922f695d3d26238724119958bcc9b53de0047532a3f7b4f844f9f6).
The native-host branch in input_hooks now uses this CPU tail, omitting only
VdSwap's packet producer. The non-native branch and frame statistics/cap remain
unchanged. This is not complete presentation-driver removal: command-space
reservation, system-command-buffer initialization, profiling writes, scaler
state, later device packets/MMIO and final fence/wait helpers remain.

New presentation-tail suite compares the actual original/native wrapper
bodies in64 configurations: observer presence, profiling, front-buffer address
change, configuration change, persistence flag and dirty scaler state. Uses
actual extracted save/restore25 bodies, controlled CPU helpers and SDK exports;
checks preserved nonvolatile registers/LR/SP, CPU memory outside the removed
reservation, helper-call order and absence of swap packet bytes. It does not
exercise actual SDK texture validation or native image presentation. All16
suites pass (3.42s); production input-hook/presentation objects compile.

Live PID59588 (`native-worker-packet-free`,started14:39:27) predates this present
change and remains alive/rendering through14:44. Its60-second host capture is
retained. Full relink/deployment of presentation removal is intentionally
deferred while that worker-packet removal run progresses toward gameplay.
No package refresh, mission/reset completion or handheld claim.

### Presentation wrapper direct packet/MMIO writes removed (source/test,14:48)

Extended the same fingerprint-checked51460 native tail to omit all direct
GPU packet stores (the update-address stores except the r1 stack-frame write)
and its scanout-address MMIO write at7FC86110. CPU cursor updates, cached
front-buffer address19980, dirty flags, stack data and helper calls remain.
The native host's texture already owns scanout; no emulated register is needed.

Expanded paired tests to512 cases: previous state combinations plus scanout
register branch, persistence success/failure, and both zero/poisoned packet
storage. The original branch's MMIO is captured by a fixture and verified to
occur only for the expected address-change/flag combination. Native must emit
no MMIO and leave all packet bytes unchanged, including poisoned storage.
Both bodies must match CPU memory outside the packet arena, helper-call order
and preserved ABI. Persistence success exercises its matching physical-memory
release; helpers remain controlled fixtures, not a full SDK integration test.
All16 suites pass and the production presentation object builds. Full link and
runtime verification remain deferred for live PID59588's gameplay sequence.

The wrapper still reserves transitional command storage and calls other
driver helpers; this change removes its direct emissions, not every emission
reachable through those helpers. VdPersistDisplay was inspected and currently
allocates a dummy physical block that its caller releases; it is not replaced
by this patch. Full native presentation ownership remains unfinished.

At14:49:03 inspected `native-worker-packet-free.output.1800.bmp`: mission scene,
player/NPCs, city, overhead ships and HUD rendered. PID59588 remains responding
and indexed submission logs advance. This validates mission entry after EBA0
packet removal; later movement/firing, mission completion/reset and the pending
presentation changes are not yet verified by this run. Leave it running.

### Presentation no longer requests Xbox EDRAM retraining (source/test,14:51)

Audited51080 in full: graphics-driver critical region/section, optional EDRAM
worker call, EDRAM command generation/retry/drain, unconditional return1. The
presentation call supplies(device,0,0). Native D3D targets have no Xbox EDRAM
to retrain, so the native51460 tail now returns1 at this call site without
entering51080. Other callers are unchanged. Added a fingerprint gate for51080
(222e7c52c8674975e4f93c114814fac26cdbc2a51b30b5c20443a2536ba315d8).
The512-case presentation suite requires exactly one original EDRAM call and
none in native, preserving all other controlled helper ordering and CPU state.
All16 suites pass (3.61s); production object builds, deployment still pending.

Also audited390B8: writes a16-byte GPU event packet targeted at a guest physical
writeback address (including DEADBEEF marker). It is not removed yet because
its completion/writeback consumer needs a native ownership contract.51168
contains state packet writes followed by shader-state helpers42130/42050;
those callees were not audited here, so it is not treated as a pure no-op.
Live worker-packet-free output4200 at14:50:27 shows the player farther along the
street; the run remains alive while its longer firing sequence proceeds.

### Worker packet removal survives combat; presentation changes deployed (14:53)

Inspected worker-packet-free output5700 (14:51:21): shell casings and ammo99/120,
following movement visible in output4200. At14:51:54 issued99969/completed99963,
busy0 and worker lock/active0 in the non-atomic snapshot. Rendering remained
active through14:52:49. After the final scripted input, explicitly stopped
PID59588 following exact-path validation to deploy the accumulated presentation
changes. No spontaneous termination; all evidence retained. Full link succeeds.

Started `native-present-packet-free` with unchanged input/capture settings.
This is the first live build with51460 native swap/direct-packet/MMIO removal
and no per-present EDRAM retraining. Runtime result pending. No package update.

### Native result initialization; profiling consumer identified (14:56)

Native51460 now initializes its148-byte result block locally and zeros the
unused secondary command-buffer identifier, removing VdGetSystemCommandBuffer.
The SDK's BEEF0000/BEEF0001 identifiers were only input to removed VdSwap;
the CPU-consumed status and optional scaler/format-change fields remain zero.
The512 paired cases now use dirty stack storage, require no SDK buffer request,
and preserve the same non-packet CPU state. All16 suites pass (3.51s); production
object builds. This latest initialization change is not in live PID13292 yet.

Traced the390B8 consumer to the profiling branch at821382D4: reads three
little-endian timestamp slots from writeback+64 through+92, waits for nonzero
results, advances device20048 by2 and computes wrapped timestamp ratios.
Configuration at82138E70 enables it through device20056 and clears producer/
consumer counters. Read-only inspection now includes those three fields.
At14:55:14 all are0 in PID13292. This is optional profiling currently disabled,
not evidence it may be removed universally or supplied with invented timings.
It remains a native timestamp/completion migration requirement if enabled.

Live native-present-packet-free continues rendering through14:55:50; inspected
its60-second capture (14:54:16), showing title/save-data confirmation UI. Leave
the same process running toward gameplay; no relink/restart or package update.

### Display-format packet removal; gamma fidelity gap identified (15:00)

The interrupted implementation before the status question added the native
51168 display-format tail, fingerprint
e61bc0b94ca9a989ee0b77386e3209925336be5c5e4bc44410ca4f08af542ce5.
Its six direct packet writes are omitted, preserving CPU cursor, ABI and helper
calls. Native-host hook is in input_hooks.cpp; non-native calls remain original.
Added12 direct paired cases for three format values, optional supplied state
and rollover, with actual save/restore28 and controlled helper calls. All16
suites pass (3.46s); production objects compile. This is not in PID13292 yet.

Correction to earlier terminology:42130/42050 are display gamma-table uploads,
not shader-program setup. Full body inspection and SDK register_table.inc
identify DC_LUT_* registers:42050 packs256 RGB entries,42130 packs128 piecewise
linear entries from three16-bit channels. Each only reserves/writes GPU commands
and advances the CPU cursor, but removing them requires preserving their visual
effect in native presentation. NativeFrameCompositor currently just samples
the frame texture and does not apply a display LUT. This is an explicit visual
fidelity gap, not grounds to replace the uploads with unconditional no-ops.

Next boundary is to capture the CPU gamma tables into host-owned state and apply
them in the native compositor, including format/mode, lifetime and reset tests.
Only then remove these upload commands. Read-only inspection now exposes the
display format/color space and sparse raw default-ramp samples for evidence;
these snapshots do not establish which override ramp is active. PID13292 remains
alive/rendering at14:59; preserve its ongoing scripted test.

### Host-owned display gamma decoder (test-only foundation)

Added NativeDisplayGamma: copies1536 big-endian bytes into host-owned channels,
decodes256-entry10-bit mode or128-segment base/delta mode, preserving the six
discarded/hardwired-zero low bits. EvaluateCode accepts8-bit table indices or
10-bit PWL inputs with3-bit segment fractions; it returns raw integer precision,
not normalized/clamped final display color. No guest pointers, GPU register
headers or runtime packet interpretation. Size/mode/index validation is explicit.

Tests compare all768 table components against42050's RGB packing formula and
all3072 PWL input/channel combinations against the audited base/delta formula;
also check ownership after source mutation and invalid inputs. All17 suites
pass (3.68s). This establishes a CPU decoding model, not rendered gamma fidelity.
Native compositor integration, normalized output/saturation/interpolation
verification, capture hooks and per-frame/reset lifetime are still required.
Neither gamma upload path has been removed. Live PID13292 is unchanged.

### Native compositor gamma application verified in D3D11 (15:08)

Added optional explicit NativeDisplayGamma to compositor Draw. Shader applies
the table to source texels before bilinear host scaling, preserving alpha and
letterbox behavior. Null gamma restores the original sampled-frame pass.
Dynamic constant storage uses WRITE_DISCARD; no guest memory or GPU packets.
PWL normalized output divides by65472 and saturates; table mode divides by1023.
The PWL divisor was confirmed from the SDK's compiled apply_gamma_pwl_ps shader
float literal (1.52737048e-05), whose reciprocal is65472, not65535/65536.

Readback tests cover both modes at nine source levels across RGB, alpha,
disabling after enabled draws, and nonlinear correction before scaling.
Initial test read the unresolved sampled copy, yielding zeros; corrected it to
read the actual render surface. All17 suites pass (4.78s). CPU decoder tests
remain exhaustive over integer inputs; GPU tests use the current RGBA8 source
contract, not native10-bit source surfaces. This is an explicit compositor API
only: game table capture, frame-associated ownership and reset integration are
still pending. The default live app does not pass gamma yet, and uploads remain.

### Gamma capture and frame ownership integrated, new run (15:12)

Native42050/42130 hooks now snapshot the CPU table before the original upload,
decode into host-owned state and track the explicit device. Construction may
precede publishing the SDK global device pointer, so ownership does not depend
on reading that global. A different device without reset is rejected. Both
original GPU upload bodies are still called; removal awaits runtime validation.

Frame publication copies the currently captured optional table alongside its
GPU image. Both normal host and preview pass that immutable snapshot to the
compositor. Movie and scene producers participate. Invalidating/failed publish
clears gamma; an uncorrected subsequent frame cannot inherit it. Device reset
clears captured ownership and invalidates presentation. Added tests proving a
producer table mutation cannot affect a published frame and gamma does not
leak into a later uncorrected frame. Gamma updates occurring after image
publication take effect on the next publication; ordering during format/ramp
transitions still needs live verification. Native10-bit source fidelity remains
outside the current RGBA8 compositor contract.

All17 suites pass after full build (4.11s). Previous PID13292 remained rendering
through15:11:48; inspected output7500 showed movement and ammo118/120. Explicitly
stopped after exact-path validation to deploy integration, not due to a crash.
Retained evidence. Started PID62232 at15:12:16, prefix `native-display-gamma`,
same input/captures. This build also deploys native result initialization and
51168 direct-packet removal. Runtime validation pending; no package update.

### Gamma GPU uploads removed in next build (15:15)

Added audited native tails for42050 (fingerprint
044d37e295da3f21b8aa123ea2d3fa38e754f7dab41d2aad93f01479f52ffbdc) and42130
(c97de6ff5a51801574f6f6381f7e5e9d9184f467b87e59ae2d3e2bfd7fe99007).
Native hooks still capture tables, but now call tails omitting all GPU packet
stores and source halfword reads. CPU allocation/cursor advancement and saved
register ABI remain. The reservations are still transitional driver storage;
this does not claim removal of its allocator dependency.

Six original/native comparisons cover both modes and three source patterns.
They verify CPU memory outside packet storage, helper-call trace, ABI and
9236/5652-byte cursor advances. Native tail accepts an inaccessible source
pointer (the hook already owns its copied table), and leaves poisoned packet
storage untouched. Tests also decode actual original emitted words, comparing
all256 RGB entries and all128 PWL segments/eight fractions against the host
decoder. This strengthens the earlier formula-only validation. All17 suites
pass (4.25s); production objects compile; full link/deployment deferred.

Live PID62232 predates upload removal. It remains rendering through15:15:31.
Inspected its60-second host capture: title/save-data confirmation UI is visible
with native gamma applied, darker than prior uncorrected capture. This is not
a reference-image fidelity verdict. Continue the same run toward gameplay;
gamma transition timing, full mission/reset and handheld performance unproven.

### Gamma capture no longer reserves legacy command storage (source/test,15:18)

Callgraph and generated call-site audit found42050/42130 callers in51168 and
376D8/37760. Those callers retain CPU default-table construction/comparison/copy
independently; they do not use the upload's cursor or return to access data.
D160 solely ensures command capacity, potentially flushing/growing the buffer.
Once gamma is captured for host presentation, neither its packet reservation
nor loop is required. Native upload tails now only preserve the observed return
register (6434/table,-1/PWL), with no guest-memory access, allocator call or
device40 advance. Fingerprint gates for the full original bodies remain.

Paired tests explicitly check this changed boundary: original reserves/advances
9236/5652 bytes; native leaves its cursor and poisoned packet arena unchanged,
calls no allocator, preserves ABI/return, and requires no readable source table.
Other CPU memory must match. Actual original encoded tables still validate the
host decoder. All17 suites pass (4.00s); production objects compile. This replaces
the transitional reservation-preserving gamma tails documented above; it has
not been linked/deployed into live PID62232. The same run remains active.

### Display-format helper no longer maintains packet capacity (source/test)

Removed51168's complete six-word display-format preamble from its native tail,
including command-buffer limit reads, conditional CF60 flush, cursor arithmetic
and device40 write. Kept the format discriminator, actual default-ramp creation,
gamma capture calls and saved-register ABI. This follows the native ownership
boundary rather than reserving unused GPU words. The full original fingerprint
still gates generation. The12 paired cases now verify native cursor unchanged,
no capacity flush even in the rollover case, and the same remaining CPU helpers
and state; the original's intentional24-byte advance is checked separately.
All17 suites pass (3.83s), production object builds. Deployment remains pending
while the same native-display-gamma run progresses toward gameplay.

### Swap reservation removed (source/test, 15:25)

Removed 51460's sole D160 call and its associated 256-byte cursor advance.
The removed VdSwap was the reservation's only consumer. Native presentation
now neither allocates nor flushes/grows command storage for that swap packet.
The full original-body fingerprint remains the generation gate. Other packet
reservations and profiling calls are not implicitly removed by this change.

The 512 paired presentation cases check the original allocator call count,
require no native allocator call, and compare all non-packet CPU state after
normalizing exactly the original's 256-byte reservation (zero in observer mode).
Production object compiled; all 17 suites pass (3.91s). Not yet linked/deployed.

PID62232 was revalidated alive/responding at its exact build path. Inspected
native-display-gamma.output.7200.bmp (15:24:44): gameplay city, player in motion,
HUD ammo85/120. This is a pre-compositor scene capture, not final gamma fidelity
proof. The running build still predates gamma/display-format/swap reservation
removal; those changes need a fresh linked runtime test.

After continued rendering through15:25:44 and the scripted combat window,
explicitly stopped PID62232 after exact-path validation to deploy, not due to
a crash. Full executable link succeeded. Started PID48472 at15:26:13 with
prefix `native-present-allocation-free`, deploying all three reservation
removals above. Same combat input; final host capture now scheduled600000ms
after startup to inspect gameplay after compositor gamma. Revalidated process
alive/responding; gameplay validation remains pending. No package update.

### Swap timing boundary checked against real callback (source/test)

Audited512D8: if interval13220 is not0x80000000 and13456 has nonzero
upper20 bits, it brackets a GPU-side wait/signal/wait sequence with39148
profiling helpers. The signal callback is51248; its argument packs the selected
interval in the upper16 bits and11580's extracted7-bit phase threshold below.
Four callers include the two scene presentation wrappers8219C1F8/8219C6D8,
82151Axx setup and the worker's8214EB14 presentation branch. It cannot simply
be bypassed: later GPU work originally waits for the callback/vblank release.

Added actual51248 extraction to the presentation test fixture, gated by full
body SHA256 d9da0247f0d98989ac19abe1ad9773cc49790fd0985ea8d6703998b80ef0b76e.
Controlled original MMIO scanline/total reads supply phase1..100.1200 paired
cases compare NativeSwapPacingState::Complete with actual original CPU fields
15124/15128/15132/15136 and acknowledgement writes, covering threshold edges,
positive/negative pending intervals and counter wrap. All17 suites pass3.59s.

This validates the existing host bookkeeping model, not runtime integration.
Native replacement still needs host tick/phase sampling, ordered completion
delivery for51248 (current signal dispatcher only handlesEBA0), and a native
barrier preventing subsequent work from bypassing the refresh deadline.39148
profiling output also needs real native timestamps when enabled. No runtime
swap-timing hook was installed and no executable was relinked for this fixture.
PID48472 remains alive/responding with native UI draw progress; preserve its
ongoing scripted run and planned600-second final-compositor capture.

### Native swap wait implemented (source/test; deployment pending)

The read-only state snapshot now includes13220/13456/11580/20080. LivePID48472
at15:30:37 has interval1, flagsFECD7006, phase config80200000, profile mode0
and no vblank callback. Thus512D8's active branch is relevant, not hypothetical.

Added native512D8 CPU tail, full original SHA256 gate
5bcbf4d8a82b7d54cf1d842a1415d0f4c276b0edc3ebedcf27583fce8daf035c.
Preserves the enable gates, ABI and two39148 profiling boundaries. Replaces
the complete wait/signal/wait packet sequence, capacity check/flush and cursor
update with edf_native_swap_wait. No51248 signal packet or GPU writeback access.

The native helper serializes context producers while a real D3D event query
completes preceding immediate native work, then applies the verified callback
bookkeeping and waits for its60Hz host-clock refresh deadline. Updates CPU
tick/ack/pending/callback fields only; no fabricated GPU completion writeback.
Per-device clock is cleared at reset. Unsupported interval/vblank callback
fails explicitly. This conservative synchronous implementation may stall host
UI painting during the barrier; async queuing/performance remains future work.
Existing39148 profiling packets remain when profiling is enabled, not migrated
or silently discarded here. Engine heartbeat/FPS cap are unchanged; interaction
with this restored refresh pacing needs runtime frame-time measurement.

Added20 paired actual512D8 cases (five interval modes, enable/disable, capacity
rollover), checking preserved CPU/ABI/profiling order, removed148-byte fixture
reservation and unchanged poisoned packet storage. Host phase sampling uses
the same rational60Hz epoch as ticks, with126279 subsecond samples plus exact
tick-boundary tests. Production objects compile and all17 suites pass3.68s.
This is source/test only: PID48472 still runs the prior allocation-free build;
it was alive/responding and rendering at15:34:09. Do not relink its executable.
Native barrier runtime validation and full-combat capture remain outstanding.

### Allocation-free gameplay host capture and swap diagnostics (15:37)

Inspected native-present-allocation-free.bmp, captured after600 seconds: final
host image contains the gameplay city/player/HUD with gamma applied. This closes
the previous pre-compositor-only evidence gap, not reference-image fidelity.
The run continues through movement/firing; FPS samples at15:36:45..15:37:05 are
24.7..30.0. These are desktop scripted-run figures, not handheld measurements.

Added opt-in separate HookTiming buckets swap.gpu_wait and swap.refresh_wait
to the next build. Existing FPS cap defaults0/off and remains user-controlled.
Objects compile and all17 suites pass3.89s. No relink while PID48472 runs.

Further profiling audit:39148's timestamp destination uses endian bits2 and
82138040 (recomp57) reads its paired20076 entries with ordinary lwz before
subtracting them.390B8's separate ring is consumed with lwbrx at821382D4.
A future native timestamp replacement must preserve these different CPU byte
orders, not indiscriminately write both paths as little-endian timestamps.

### Native swap wait deployed (15:40)

PID48472 continued rendering through15:39:42. Inspected output7200: player
moving forward, ammunition85/120. Host capture log at15:36:14.791 confirms
image=true and Xenos_loaded=false. Explicitly stopped the exact-path process
after the final combat script window for deployment; no crash, evidence kept.

Full build/link succeeded. Started PID22360 at15:40:04, prefix native-swap-wait,
same combat script and600-second host capture, added frametime logging and
explicit FPS cap0. This build deploys native512D8 timing and both wait-duration
diagnostic buckets. Revalidated alive/responding and movie draws/worker progress
at15:40:14. Re-ran all17 suites successfully from the VS environment after an
initial plain-PowerShell ctest command failed to resolve; app was not restarted.
Gameplay/pacing performance validation pending. No release package update.

### Native swap runtime regression identified (15:42; unresolved)

PID22360 is live and native swap callbacks are actually advancing: snapshot
15:40:35 has ticks/ack1762, pending0, callbacks238. But startup FPS degrades to
roughly4..5 with repeated~535ms frame outliers. Diagnostic batch at15:40:40:
GPU wait256 calls,total16665.72ms,max509.34ms; refresh wait,total3434.17ms,
max18.77ms. Next GPU batch totals35092.18ms,max510.11ms. This is a failed
performance validation, not an acceptable finished native swap implementation.

The helper currently holds both the SDK global critical region and bridge
mutex through GPU polling/sleeps. SDK include/rex/thread/mutex.h explicitly
requires that critical region to remain fast/no IO; it excludes other guest
kernel activity. The bridge mutex also excludes UI presentation. These broad
locks must not span waits. Rework ordering with a dedicated submission barrier
and short locked polling/publication, allowing UI presentation and unrelated
CPU activity to progress. Do not simply remove the event query or let later
game draws overtake the wait. The timing evidence locates the long stall in
GPU wait; it does not yet prove which lock/driver interaction causes500ms.
Preserve PID22360/evidence unless deliberately deploying a tested fix.

### Short-lock swap wait deployed; residual stall (15:50)

Separated game GPU submission ordering (recursive submissions gate) from the
bridge context mutex. Graphics resource/binding/draw/resolve/publish hooks and
C410 submission take the gate before the context mutex; host presentation and
CPU worker bookkeeping do not. Swap holds the gate but releases context access
between event/refresh polls. Removed its SDK global critical-region hold entirely.
Reset tracking takes the gate before erasing per-device clock state. Callback
count is published before refresh sleep, avoiding overwriting a later stats reset.

All17 suites pass3.50s. Explicitly stopped degradedPID22360, still responding
at15:47:10, to deploy. Full link succeeded; PID13820 started15:47:47 with prefix
native-swap-short-lock, same script/capture/timing settings. First256 GPU waits
total5700.86ms,max245.22ms (previous16665.72ms,max509.34ms); refresh max18.97ms.
Startup still stutters and is not validated. Host reported presented=false
(occluded). Read-only window enumeration confirmed the owned SDL window visible
and not minimized. Raised only that window without activation at15:49:31 after
validating PID/path/window ownership; later measurements still show~250ms stalls.

Next source change: NativeCompletionQueue::Poll accepts allow_flush (defaultfalse);
blocking WaitUntil and the native swap actively poll withtrue. Other observers
retain DONOTFLUSH. Microsoft documents that repeatedly waiting with DONOTFLUSH
can prevent queued work from progressing:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_async_getdata_flag
This is a candidate liveness fix, not a proven cause of the residual runtime stall.

### Active query polling removes observed startup stall (15:52)

After17 passing suites3.55s, explicitly stopped livePID13820 to deploy the
active-poll change. Full build/link succeeded; startedPID61492 at15:52:09 with
prefix native-swap-active-poll, same settings/script. All17 suites pass after
full rebuild4.00s. Revalidated responding; leave running toward combat.

At15:52:30 the latest256 GPU waits total642.56ms,max13.82ms; the next batch
totals610.32ms,max8.41ms. Refresh waits remain~15ms average,max17.78..18.77ms.
Movie FPS samples15:52:31/36/41 are30.0, with~33.3ms average frame times and
~35ms maxima. This replaces the prior250..500ms startup wait outliers in the
observed samples. It supports active-poll liveness as the residual-stall fix,
but is not gameplay/long-run/handheld performance proof. No release package
update; full objective remains incomplete.

### Native timestamp span foundation (source/test, 15:58)

Added NativeGpuTimer with owned Begin/End timestamp queries enclosed in one
timestamp-disjoint interval. Results retain raw64-bit timestamps and the actual
GPU frequency; Milliseconds returns no value for disjoint/zero-frequency/backwards
samples, not a fake zero duration. One non-overlapping span per frame or less;
bounded pending capacity, explicit cancellation, active query closure on destruction,
and optional active-flush polling. Caller must serialize context access.

WARP tests cover real GPU work between timestamps, tag/lifetime retirement,
nested/unmatched calls, capacity rejection, cancellation, frequency conversion,
valid zero duration and invalid samples. All17 suites pass3.61s. These tests
exercise the component, not retail profiling integration. No executable relink.

The SDK's EVENT_WRITE_SHD implementation supplies its own counter_ (commented
as uncertain GPU/vblank counter), not an authoritative hardware time frequency.
390B8's three-counter ratio is dimensionless, whereas39148's delta is combined
with other hardware statistics by82138040/82138378. Do not invent a500MHz
conversion or replace these remaining profiling packets before their consumers
are moved to native, frequency-aware measurements.

References: Microsoft D3D11_QUERY recommends timestamp-disjoint once per frame
or less; D3D11_QUERY_DATA_TIMESTAMP_DISJOINT explains invalidation during power/
clock transitions, especially relevant to handheld operation:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_query_data_timestamp_disjoint
PID61492 remains live on the active-poll build and continues its scripted run.

### Gameplay target and mothership flicker investigation (September 9)

The user explicitly requires 60 FPS gameplay. The pending native engine pacing
change consumes an already elapsed tick rather than waiting for another tick;
the original two-argument rule is retained as RetailPacingPending for comparison.
The pacing tests cover 60 single-tick frames and 30 two-tick frames, both retaining
60 simulation steps. This is source/test evidence, not a 60 FPS runtime result.
PID61492 still runs the earlier active-poll executable, without this change.

The user reports intermittent black texture flicker on the mothership during the
spaceship sequence. The saved output frame600 shows textured hull and panels;
the final host BMP shows gameplay. Neither proves temporal correctness. Recent
live logs report zero missing texture bindings and binding errors, which does
not exclude stale data, shader errors, depth conflicts, or post-processing faults.
No cause or visual fix has been verified. The question whether individual panels,
the entire ship, or the full screen flickers is still unanswered.

Added an opt-in edf_native_output_capture_start_frame policy. A positive value
disables startup milestones and starts periodic captures relative to that indexed
output frame; interval1 and limit64 select exactly64 consecutive eligible frames.
For example start_frame600 targets the neighborhood of the existing mothership
capture, though input and camera timing may differ on another run. A capture
prefix is still required. Zero preserves previous milestone/periodic behavior.
Attempts remain bounded to128 even on write failure. Captures are pre-compositor,
and synchronous readback can perturb timing; they are not performance evidence.

The new policy tests verify defaults, disabled/negative settings, start offsets,
the exact bounded burst, and large frame counters. Production bridge object
compiled; all18 tests passed in3.65s. No executable relink or restart occurred;
PID61492 was revalidated responding at its exact expected executable path.
The diagnostic option is source-only until the next deployment. Full Xenos
replacement, flicker resolution, and measured60 FPS gameplay remain incomplete.

### Remove residual scanout/scaler reservations (source/test, September 9)

The native51460 wrapper no longer enters the command-capacity checks or advances
the command cursor for the blocks51824..518AC (8 bytes) and518AC..51928 (40 bytes).
Their GPU packet writes and scanout MMIO had already been removed, but the dummy
reservations still retained legacy flush/storage dependencies. The first block
now only retains the cached frontbuffer address at device19980; the second
retains the conditional r30 scaler-change bookkeeping. The full original function
hash guards extraction. Profiling390B8, the display-persistence block, and the
final CPU submission/fence helpers remain for further migration; this is not a
claim that the entire presentation path is independent of the legacy driver.

The original/native wrapper comparison now runs1024 pairs (256 modes, two packet
fills, two capacity states). It checks the exact removed8/40/256-byte reservation
differences, identifies the two removed capacity calls by their return addresses,
and compares all other non-packet memory, retained helper traces and saved ABI
registers. Both normal and exhausted command-capacity fixtures pass. Production
native_present object compiled, all18 suites passed3.76s. No live executable
relink or deployment; the user session remains on the previous binary.

### Remove persistence packet reservation (source/test, September 9)

Removed the remaining16-byte persistence packet reservation and its capacity
flush at return address8215194C in51460. The native replacement supplies the
same stack descriptor addresses and twelve zeroed descriptor bytes to the
retained VdPersistDisplay call. Its return handling, conditional physical-memory
release and device10809 flag update remain unchanged. This does not yet remove
the persistence export itself or the final submission/fence helpers.

The1024 original/native pairs now assert no net native cursor advance with
controlled helper fixtures, rather than only normalizing an observed difference.
They identify all three removed capacity checks separately, validate the exact
persistence arguments and descriptor, and cover success/failure cleanup plus
normal/exhausted capacity. Production object compiled; all18 tests pass3.87s.
These fixtures do not prove called helpers are packet-free. No live executable
relink or restart; PID61492 was revalidated responding at its expected path.

### Presentation helper closure audit (September 9, no replacement yet)

The retained final51460 call to377E8 always supplies r4=0. Inspection of the
actual generated helper, rather than its wrapper-test stub, finds eight direct
packet words (32 bytes), a capacity flush, a call to42328, a call to34BD8, and
five64-bit all-ones writes to device0..39 (CPU dirty-state bookkeeping).
42328 writes four packet words (16 bytes) plus a capacity check; its only
non-stack, non-cursor CPU write updates bits12..13 of device10788.377E8 passes
those same existing bits back to42328, so that update is an identity at this
specific call site. This does not justify suppressing42328 at unrelated callers.

34BD8 emits scissor/tile-state packets, including per-tile loops and intermediate
capacity flushes. Its complete body writes only stack, GPU packet destinations
and device40 (command cursor); it has no other CPU-memory outputs. The native
draw path binds its viewport/scissor independently, but that ownership and dirty
state propagation must be covered when replacing this whole call chain. Do not
globally stub the helper based on a presentation-only contract.

Audited normalized original-body SHA256 values:
-377E8:259534f3abd2c3cf5dd9a23c87284b63cbf0f52fa48e0ef80a0a054831cb4c7b
-42328:e7393ab98f0163be7ae44ba6ea384cca3751712cc451ddc0433ec3c67564a12e
-34BD8:a462f733037112635b9631807d89855a8996c55336ab222f631b4b6d4ba99a99

Next integration should extract real fixtures for this chain and preserve the
dirty-state contract while removing its GPU-state re-emission at native present.
The existing1024 wrapper pairs stub377E8 and therefore do not cover this chain.
Other retained helpers also need closure:47028 dispatches to462D0/465C8/46CC0
according to global/device flags, and39228 includes CPU frame accounting plus
optional profiling paths. This audit is new dependency evidence, not a runtime
fix or proof of complete GPU removal. PID61492 remains live/responding; no
deployment was attempted during the user's play-test session.

### Native presentation reset closure implemented (source/test, September 9)

51460 now calls edf_native_present_reset in place of377E8. This preserves the
five64-bit CPU dirty masks at device0..39, without the original mode/scissor
packet re-emission, cursor reservations, or capacity flushes. It is scoped to
this native presentation call site; other377E8/42328/34BD8 callers are untouched.
The native draw paths explicitly bind viewport/scissor state when drawing.
The extractor checks all three original-body hashes listed above in production
as well as tests, and tests include the real original helper bodies and ABI
helpers. The prior377E8 test stub has been removed.

6144 isolated original/native reset comparisons cover normal/exhausted capacity,
all256 flag combinations (including target mismatch), all four mode values,
and zero/one/three tiles. They compare non-packet CPU memory, prove the original
cursor advances, and prove the native packet arena remains unchanged. The1024
whole-wrapper pairs now execute the actual original reset chain; the fixture's
ordinary path removes an additional64 bytes of legacy commands while preserving
the dirty masks and all retained helper behavior. Stack scratch is outside the
CPU-memory comparison; original stack/LR restoration is checked explicitly.

Production native_present object compiled. All18 suites pass4.35s. Runtime
rendering fidelity after this change is not yet verified; no executable relink
or restart occurred. PID61492 remains responding on the earlier active-poll
binary. Remaining helper/profiling dependencies and the flicker/60 FPS runtime
requirements still prevent claiming complete Xenos replacement.

### Remaining47028 dispatch: live branch evidence (September 9,16:20)

Added read-only branch inspection to read-native-worker-state.ps1. The dispatch
uses byte82578E1C and bit3 of device byte20400. Snapshot of verified PID61492,
device40002780, at16:20:33 reads both bytes zero: selected path is immediate
return. The read is non-atomic and proves only that sampled state, not all runs.

Nonzero combinations still lead to462D0/465C8/46CC0. Source inspection finds
resource-management helpers, copied descriptors, persistent CPU fields and an
indirect callback in addition to command emissions. These paths cannot safely
be replaced with an unconditional no-op. In particular465C8 has substantial
packet creation and dirty/resource bookkeeping;462D0 includes an indirect call;
46CC0 builds resource-related state and can call462D0. The ordinary observed
return path is not proof this remaining dependency has been migrated.

An asynchronous question asks whether the user is ready to restart for the
pending native build and capture run. Until answered, preserve the verified live
play-test process. There is still safe source/audit work; this is not a goal
blocker and full replacement remains active.

### Active native worker/fence polling (source/test, September 9)

The C928 wait audit confirms conditional submission when target equals issued,
an early return during recording (device12944), modular fence comparisons, and
394D8/39688/39508 timeout/error/accounting helpers. It is not yet replaced in
full. Its pending-loop39688 hook already publishes actual native completions,
but both that publication and worker signals were still passive DONOTFLUSH
polls. Extended the previously fixed swap-wait active polling to these waits.

NativeSignalQueue::Poll now accepts allow_flush (default false). Only worker
continuation waiting, device signal draining and the pending39688 loop request
true; the latter also actively polls the native fence queue before publication.
Passive enqueue/capture observations keep false. Existing submission-depth
deferral, one-slot worker publication and native event completion requirements
are unchanged. No captured-but-unsubmitted range is armed by polling.

WARP tests cover active polling without submission, zero-limit backpressure and
ordered single-slot retirement of real submitted signals. Bridge object compiled;
all18 tests pass4.33s. This addresses a native wait-progress risk, not a verified
runtime stall/flicker fix and not full C928 migration. PID61492 still responds
on the previous executable; no relink/restart or new performance claim.

### Separately linked native candidate (September 9,16:26)

Created out/build/win-native-clean/edf2027-native-candidate-20260909.exe without
overwriting the live edf2027.exe. Refreshed the18 application objects, recomp
objects and native libraries, then used Ninja's expanded compilation-database
link arguments in native-candidate-20260909.rsp. Only output EXE/import-library/
PDB names were changed; the original post-build copy commands were not executed.
The existing live executable/runtime/assets were not replaced or launched again.

Candidate size31388672 bytes, SHA256:
79B3A1605775F97A547C32CAAA4C6F832D65EB2B4894FFF39BF19309A3EDA51C

This binary now includes the pending60 FPS engine pacing change, bounded burst
capture policy, presentation reservation/reset removals and active worker/fence
polling. It is linked, not yet runtime-validated. PE imports include d3d11.dll,
D3DCOMPILER_47.dll and rexruntime.dll, with no direct Xenos plugin import. Static
imports alone do not establish absence of dynamic plugin loading or remaining
legacy GPU-driver logic. No full replacement or60 FPS claim follows from this.

PID61492 remains responding at the original edf2027.exe path. Await the user's
restart preference before replacing the play-test session. The candidate is a
development artifact using adjacent runtime/assets, not an updated release
package. Existing package/native-tail-20260909 remains older.

### Paired scene/output capture for flicker (source-only, September 9)

Added optional edf_native_output_capture_scene_color=false. When enabled, each
selected output capture also captures that scene's color surface under the same
submission/context locks and indexed-frame number, with a .scene-color.N.bmp
suffix. This uses the existing HDR readback path and rejects missing/invalid
scene surfaces or existing files. It shares the bounded output capture attempts,
so at most128 additional files are created. Defaults leave capture cost unchanged.
Both files are diagnostic BMPs, not raw HDR or final gamma/UI reference images.
The scene color is sampled at output completion; this is not a GPU draw replay.
Synchronous readback may perturb timing and is not suitable for FPS measurement.

Source inspection found full-content vertex/index cache validation and explicit
post-activation instance-constant patching, not evidence of a stale-data cause.
The paired captures should help distinguish scene-local black patches from
post-output changes on the next mothership run. No cause or fix is established.
Production bridge object compiled; all18 tests pass4.22s. Live PID61492 is
unchanged. The separately linked candidate hash79B3A160... predates this capture
option and remains immutable; relink under a new name before using this option.

### Remove Xbox display-persistence allocation (source/test, September 9)

Native51460 no longer calls VdPersistDisplay or its conditional
MmFreePhysicalMemory cleanup. The inspected SDK source implementation at
rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_video.cpp:418 allocates a64-byte no-access
placeholder, stores its address and returns1; it does not persist image content.
The audited caller immediately frees that allocation. NativePresentationFrames
already owns the published image, so native presentation now only acknowledges
the pending persistence flag by clearing device10809 bit0x10. No fake pointer,
fabricated successful allocation, or Xbox packet/storage reservation is used.
Unrelated callers of either export remain untouched.

The1024 wrapper comparisons retain original export success/failure fixtures,
verify their exact invocation counts, then exclude only the deliberately removed
allocation/release calls from the native trace. All compared CPU state remains
equal, including the flag. Production object compiled; all18 tests pass4.31s.
The previous section about retaining the export describes an earlier stage and
is superseded here. No live executable change and no new candidate relink.

### Scaler dependency audit (September 9, pending implementation)

51050 is not a pure GPU packet helper:4F300 expands the supplied28-byte config
into a56-byte descriptor;50FD0 copies it to device13348, sets device10810 bit0x10,
derives dimensions from device13408's resource (or device13168/13172), then calls
4F378.4F378 derives another descriptor via4F1D8 and invokes the graphics
notification export. Removing51050 wholesale would lose CPU configuration.

50D60 separately computes scaler parameters through4F1D8 and507F8 before
building commands.507F8 calls VdQueryVideoMode and50518 and writes derived
values through output pointers; these effects require a full ownership audit,
not treating the entire helper as packet-only. After507F8,50D60 initializes
stack command storage, reserves packet space, builds/copies commands, invokes
VdInitializeScalerCommandBuffer, and updates device40. The inspected SDK scaler
export fills the supplied destination with NOP words and returns its word count;
it does not itself perform scaling. Native host scaling does not need those
NOPs, but the preceding CPU parameter work must be preserved or replaced.

Next candidate change: extract and test50D60's CPU prefix with the original
parameter-helper contract, cutting command generation after507F8 while retaining
the original ABI restoration. Verify every remaining store/call in that suffix
before editing. Current wrapper tests still stub50D60/51050 and do not cover
this dependency. No scaler change or runtime fidelity claim was made this turn.

### Native scaler CPU prefix implemented (source/test, September 9)

Native51460 now routes both50D60 call sites to edf_native_scaler_cpu_tail.
The extracted variant retains the original4F1D8/507F8 parameter work and ABI
restoration, then omits everything after507F8 except the epilogue: stack command
fill,220-word reservation, GPU packet stores, SDK scaler command generation,
packet memcpy and cursor advance. The complete original helper is guarded by
SHA25699a371d6a6f00e52f1fdbbbc26d4509cd14ac2ceeed0defda56b62508125bde7.
This preserves parameter-helper side effects, rather than pretending they are
packet-only.4F1D8/507F8 themselves remain original CPU/helper dependencies.

192 paired original/native scaler fixtures cover64 parameter seeds and three
SDK command-count results (0,7,200). Controlled parameter helpers capture the
computed arguments and model persistent CPU state; tests compare non-packet,
non-stack memory, selected nonvolatile registers and stack/LR restoration.
Original cursor advances80+4*count bytes; native does not advance or write the
packet arena. The whole-wrapper fixture keeps a controlled scaler call shim;
the isolated scaler fixture is the coverage for this new CPU prefix, not a test
of actual507F8 arithmetic or native scaling visual fidelity.

Added missing extractor dependencies for the earlier reset-helper shards/ABI
helpers and the new scaler shard, so changes rerun hash validation. Production
object compiled; all18 tests pass4.30s. No executable relink or deployment.
The old candidate and live process do not include this change. Actual host
scaling/crop/filter fidelity, the flicker and measured60 FPS remain unverified.

### Real scaler descriptor coverage (September 9)

Replaced the copy-only4F1D8 fixture with its actual generated body and real
save/restore helpers. Guarded hash:
e651c7704e7ff7b9f7022bd2c00d67a1357fc7443c59a216842bc5c759b6cadf.
The192 scaler pairs now include default source rectangles, default output width,
derived output height, device13212 and controlled VdQueryVideoFlags results.
The real helper performs rectangle fallback/aspect arithmetic and writes the
expanded descriptor; it does not emit GPU packets. Its video-flags export stays
controlled in tests.507F8 remains a controlled fixture, so its arithmetic and
video-mode dependency are not yet covered by these comparisons.

Added the corresponding generated-shard build dependencies. Production object
compiled and all18 tests pass4.26s. This strengthens CPU preservation evidence
for the prior scaler command removal, not runtime fidelity or completion proof.
No executable relink, process restart, or release-package update.

### Non-interactive candidate launch audit (September 9)

Checked whether the candidate could be smoke-tested without disturbing the live
play-test. The current app/native sources expose no headless or hidden-window
verification switch. The SDK RunWindowedApp calls cvar::Init, then initializes
SDL and creates the application. cvar::Init catches CLI::ParseError (including
help parsing) and continues; it does not exit on --help. Therefore --help is not
a safe load-only test for this executable. No candidate process was launched.
Do not infer runtime verification from link/import checks or attempt a second
interactive session silently. The requested restart preference is still pending;
source migration and isolated native tests can continue without that restart.

### Native scaler configuration ownership (source/test, September 9)

Native51460 now replaces51050 with edf_native_scaler_config: snapshots the six
source words, expands the mode field to the two3/0 fields, zeroes the remaining
descriptor fields, stores all56 bytes at device13348 and sets device10810 bit0x10.
It no longer constructs a notification-only descriptor or calls the Xbox
graphics notification export. The inspected SDK export asserts type1 then
returns0; it does not dispatch callbacks. The native variant retains that return
value without calling the export. Unrelated51050/50FD0/4F378 callers are intact.

192 comparisons execute the real51050->4F300->50FD0->4F378->4F1D8 chain against
the native replacement. Cases include default/non-default rectangle and output
dimensions, mode field, optional frontbuffer resource, initial flag bits and
controlled video flags. They compare all non-stack CPU memory and saved ABI
registers/return, and require no helper calls from the native variant. Original
bodies are hash-guarded and all generated dependencies are listed in CMake.
Whole-wrapper tests keep their controlled call boundary; these isolated tests
provide the real configuration-chain comparison. Production object compiled;
all18 suites pass4.39s. No runtime deployment or candidate relink occurred.

### Configuration chain integrated into wrapper tests (September 9)

Removed the original51050 and native configuration-call stubs from the whole
presentation fixture. The original wrapper now executes the real descriptor/
notification chain; the native wrapper calls the same native configuration
implementation used in production. The192 isolated configuration cases remain.

The1024 wrapper pairs now exercise how the configuration flag influences later
branches: mode bit8 (changed config) can cause the later40-byte scaler packet
block even when the fixture's preexisting scaler flag (mode bit32) is absent.
Updated the explicit original cursor/capacity expectations for that interaction.
CPU-memory and retained-call comparisons pass without changing the production
implementation.50D60 parameter-generation calls remain a controlled boundary
in these whole-wrapper tests, as documented; they have separate paired coverage.
Production object compiled; all18 tests pass4.54s. No runtime deployment.

### Fence wait entry policy checked against original (September 9)

Added an explicit Complete/Recording/Submit/Wait entry decision policy in
guest_fence.h. Compared 686 combinations of issued, target and completed fence
values and recording state against the hash-guarded original 8213C928 body.
Values include zero and wraparound boundaries. Tests verify the helper-call
sequence and saved ABI state. Recording is an early return, not proof of GPU
completion.

Submission and timeout helpers remain controlled fixtures: these checks do not
prove real progress, completion after submission, timeout accounting, or native
wait integration. Production C928 has not been replaced. The production present
object compiled and all 18 test suites passed in 4.34 seconds. No runtime
deployment occurred; the live play-test process was left untouched. Mothership
black-texture flicker remains unconfirmed and unresolved pending a new capture.

### Updated diagnostic candidate linked (September 9, 16:56 local)

Built out/build/win-native-clean/edf2027-native-capture-20260909.exe after
refreshing all 18 application objects, recompiled gameplay and native libraries.
Used the current Ninja link command with separately named executable, import
library and PDB outputs; did not run the normal live-target link or post-build
copies. SHA256: 4A51069B8EDA1BEF07A14867CAEADE760580EA6E84E3668B8BB224405281AC22.
Size: 31,397,888 bytes. This candidate supersedes the earlier 16:26 candidate
for diagnostic use, without overwriting it.

Includes paired scene-color/output capture support and the later persistence,
scaler and configuration changes described above. No launch has occurred;
PID 61492 remained responding on the original edf2027.exe. Linking is not a
runtime or visual-fidelity test. The original C928 outer wait remains intact:
it contains CPU ordering/accounting, not a GPU command interpreter, so merely
rewriting it would not itself remove more emulation.

For a subsequent approved capture run, use a fresh scene-capture prefix,
edf_native_output_capture_scene_color=true, a positive start frame, interval=1
and a bounded capture limit. Frame numbers count indexed outputs, not wall-clock
time. Captures introduce readback overhead and must not be used as a 60 FPS
performance measurement. Mothership flicker still requires reproduction.

### 47028 branch classified as legacy capture (September 9, 16:58 local)

Further source tracing narrows the previously unexplained presentation branch:
this is a capture/file-output lifecycle, not ordinary scanout. Evidence:

- 82145720 copies a caller-provided 260-byte path buffer to 0x82578D18 and
  sets the adjacent enable byte at 0x82578E1C.
- 82146CC0 copies that path, splits it at a backslash, creates a symbolic
  link, formats a filename and calls 82145B20 to initialize the operation.
- 821465C8 uploads one of two embedded shader programs (557 or 949 words),
  binds the passed surface descriptor, draws into alternating buffers selected
  by device+20388, tracks a fence at device+20392, and passes a previous buffer
  to 821FA450. That helper directly calls NtWriteFile. This supports capture
  conversion/recording ownership; the exact file codec is not established.
- 821462D0 drains pending work, writes a display-information/header record,
  and closes handles. It also retains an indirect notification path.

Extended the read-only worker inspection with capture counters/layout and byte
counts. At 16:58:25 local, PID 61492 selected the no-op branch: global enable
and device flags were both zero, as were the five capture fields. This is a
non-atomic current-state observation, not proof that capture never activates.

Consequently, do not treat this code as the normal native present operation or
silently delete it as unused. Its eventual replacement needs native source
surface readback/conversion, completion ordering, output format and handle
lifecycle fidelity. Existing diagnostic BMP capture is not a compatible
replacement for that contract. No production behavior was changed and this
inspection neither reproduces nor fixes the mothership flicker. The supplied
analysis callgraph lists these helpers but did not identify this lifecycle.

### Render/resolve alias hazard regression (September 9)

Added 32 alternating binding-order iterations to native_texture_tests. A native
HDR target renders into its surface while its previous resolved texture remains
bound as PS slot 3 and VS slot 5. The test queries all three bound views, checks
that clearing the render surface leaves sampled history unchanged, unbinds the
sampled views as the bridge does, resolves, then verifies every output pixel.
Both binding orders retain their views; every explicit resolve produces the
expected alternating HDR values. All 18 suites passed in 4.55 seconds.

This checks the single-sample native render/resolve separation against an actual
D3D11 WARP context. It does not replay the mothership's materials, cover every
resource type, or rule out binding/lifetime errors elsewhere in the bridge.
No production renderer change or flicker fix is claimed. The running game and
the separately linked diagnostic candidate were not modified.

### Indexed state capture trace (source only, September 9)

Indexed submissions explicitly bind their cached blend/depth/rasterizer state,
viewport, vertex shader and pixel shader at every draw. Unsupported stencil,
alpha-test/coverage and polygon-offset states are rejected by the state builder,
not silently mapped to default depth bias. This inspection found no stale-state
fix to apply and does not establish the cause of the reported black patches.

Added opt-in edf_native_capture_indexed_state, gated by the existing capture
prefix and frame-selection policy. Logs up to 256 pre-draw states for each
selected next-output candidate: scene, shaders, VB/IB/declaration, draw range,
blend/depth/raster/alpha words, write mask, scissor enable and depth range.
Failed output can reuse the candidate index, so the cap persists across those
retries. A trace is not proof that the draw completed or that a corresponding
output image exists. No texture bytes or filenames from game state are logged.

Production bridge object compiled successfully. This option remains disabled
by default, has not been exercised in a game session, and is not included in
the already-linked 16:56 candidate. No executable was relinked or restarted.

### Native polygon fill state (source/test, September 9)

Replaced the blanket polygon-mode rejection with direct D3D11 solid/wireframe
state selection. The encoding was checked against local SDK registers.h
PA_SU_SC_MODE_CNTL and xenos.h PolygonModeEnable/PolygonType; no SDK graphics
header or runtime dependency was added. Disabled polygon mode ignores inactive
face-type fields. Dual mode accepts matching front/back types, or the surviving
face's type when the other face is culled. Lines select wireframe and triangles
select solid. Point fill, mixed visible-face types, reserved modes, both-face
culling and polygon offset remain explicitly unsupported.

Tests cover 192 dual-mode combinations and 192 inactive-field combinations,
querying native rasterizer descriptors and checking rejection separately from
assertions. The native library and draw test built; all 18 suites passed before
a test-assertion isolation improvement, and the updated draw test passed after
it. This adds native state coverage, not proof of exact wireframe pixel fidelity
or a mothership flicker fix. No game executable was relinked or restarted.

### Polygon coverage verified by native draws (September 9)

Added 24 WARP triangle draws at 32x32: solid/wireframe, both vertex windings,
both front-face conventions, and no/front/back culling. Each draw reads back
the complete target. Checks require solid interior coverage, an empty wireframe
interior with nonzero edge coverage, complementary front/back culling, and
reversed culling when either winding or front-face convention is flipped.
Surviving culled geometry must retain the uncullled coverage count. The draw
suite rebuilt and passed in 0.20 seconds including the existing tests.

This verifies native coverage relationships, not an exact comparison to Xbox
edge rasterization or the game's mothership sequence. No live process changed.

### Dynamic native blend constants (source/test, September 9)

82135418 writes the packed color's RGBA float components to device+10336..10348.
Native render state now accepts constant-color/inverse and constant-alpha/inverse
factors. All seven bridge state-binding sites read those CPU fields only when
the state needs a constant. Dynamic values are not part of the pipeline cache
key. RGB constant-alpha factors replicate alpha into the D3D11 blend vector;
alpha-channel constant-color and constant-alpha both use its alpha component.
Mixed RGB constant-color and constant-alpha factors remain rejected because
one native blend vector cannot express both generally. Missing constants and
nonfinite/out-of-range constants are rejected, not replaced with guessed values.

Native pixel readback tests cover all four factors and a second constant update
on each cached state, plus rejection of missing and mixed factors. Production
bridge object and native library compiled; no game executable was relinked or
restarted. This expands native state coverage, not a confirmed flicker fix.

Blend-factor handoff follow-up: moved the CPU field reader into guest_draw_state.h
and tested distinct RGBA values, big-endian float decoding, a subsequent update,
address overflow and a truncated final byte. Production uses that same reader.
The effect suite passed and the production bridge object compiled. These tests
use controlled device bytes; they do not execute the original packed-color setter
or prove live-game use of constant blending. No executable was relinked.

### Current diagnostic executable (September 9, 17:11 local)

Linked out/build/win-native-clean/edf2027-native-state-20260909.exe from the
current refreshed objects and libraries, using the current Ninja link inputs
and separate executable/import-library/PDB output names. Includes the indexed
state trace, polygon fill support, dynamic blend constants and shared CPU color
reader, in addition to the earlier paired-frame diagnostics and native-tail
changes. Size 31,405,568 bytes; SHA256
3A136463D4B3039577DB69C859BEBEC8B24F700A8059912FA3D155E207EBB237.
All 18 suites passed in 4.46 seconds. Neither prior candidate was overwritten.

PID 61492 still responds on the original edf2027.exe. The new executable has
not been launched, so it has no runtime, mothership-flicker or handheld-FPS
verification. This is the newest diagnostic candidate, not a release build.

### Native three-point GPU timing (source/test, September 9)

The 821382D4 profiling branch consumes previous-end/current-start/current-end
timestamps. Correction to earlier shorthand: this branch does not spin waiting
for nonzero values. A missing end returns through 821387C4; when end exists it
advances the consumer by two before checking the other two values.

Extended NativeGpuTimer with an optional single middle marker within the same
timestamp-disjoint interval. Poll publishes all requested markers together and
rejects unordered or disjoint intervals. FractionAfterMiddle reports
(end-middle)/(end-begin) only with valid nonzero duration/frequency; absent data
is not invented. A marker without an active span or a duplicate marker throws.

The native draw test passes with a real WARP three-marker interval and synthetic
ratio/invalid-interval cases. This is required timing support, not yet production
replacement of 390B8/38158: marker placement, profiling reset and consumer
publication still need integration. No live process or candidate was changed.

### Present profiling packet replacement integrated (source, September 9)

Native-host 390B8 now places native timestamp markers instead of calling its
original 16-byte GPU-event packet emitter or capacity-submit path. Checks retain
the two known callsites (return addresses 82151544/8215162C) and expected guest
ring-slot addresses. NativePresentProfiler owns paired start/end sequencing,
including a previous-end marker for subsequent samples and a first sample with
no previous history. The original present wrapper still owns producer counters
and its at-most-three-outstanding admission rule.

Native-host 38158 mode 6 consumes one completed query sample in FIFO order,
validates its producer tag against device+20048, and advances that consumer by
two only after a sample is available. It computes the three-point ratio and
uses the original scale/clamp constants. Missing/invalid samples return the
original unavailable float sentinel 0x7fffffff (NaN), not zero or fake timing.
No guest GPU completion values or timestamp slots are fabricated. Other
38158 modes remain original, including the separate unresolved 39148 path.

38E00 configuration requests reset native profiling ownership when the original
device guards permit the profiling enable/disable/reset operation. Dropping a
profiler closes its active disjoint interval; old query results cannot publish
into a new profiling session. No pipeline/interpreter SDK was introduced.

Production bridge object and native library compiled. WARP tests queue three
paired samples across 32-bit sequence wrap, distinguish missing first history,
verify FIFO retirement, reject orphan/duplicate/mismatched markers and resume
after draining. The draw suite passes in 0.21 seconds. Actual hook execution,
configuration reset and timing values have not been play-tested; no live process
or previously linked candidate was changed. This supersedes the preceding
timing-support-only integration status, not the remaining overall migration gaps.

### Real profiling emitter in presentation comparison (September 9)

The 1024 whole-wrapper pairs now execute the hash-guarded original 390B8 packet
emitter on the reference side. The native side retains a controlled marker
boundary that checks the same two callsites and expected ring slots. Tests count
both original profiling capacity checks when the command buffer is full and
the extra 32 bytes emitted when profiling is enabled. Native packet storage and
cursor remain untouched; non-packet CPU state and retained call ordering agree.

Corrected the swap-reservation fixture to reserve at its current cursor, so it
cannot hide the first profiling packet's cursor advance by resetting to the
arena base. The original helper hash is checked in production extraction too,
with shard 65 included in generation dependencies. Presentation tests pass in
0.89 seconds and the production present object compiles. The marker fixture does
not execute D3D11 queries or the new consumer hook; those still require combined
runtime validation. No live process or executable changed.

### Read-only live window inspection (September 9)

Verified PID 61492 was responding, then captured only its game window with the
existing capture-game-window.ps1 tool. Inspected
out/native-bridge-run/live-window-20260909-1725.png: the game shows Mission Failed,
Retry Mission/Quit Mission, zero player health and ants close to the camera.
This is current old-build visual evidence, not a mothership reproduction or a
test of the new diagnostic executable. No input, restart or process mutation
was performed. Fresh opening-sequence footage requires advancing/restarting the
game; the user's requested restart preference is still unanswered.

### Profiling consumer compared with original (September 9)

Added the hash-guarded full 38158 consumer and its real unsigned-to-double helper
to the presentation fixture. Forty-two cases run mode 6 across ring-index and
timestamp wrap, valid ratios and each missing timestamp. They check the result,
consumer advancement, saved GPR/FPR state, stack and return address. The shared
NativeProfileResult helper used in production matches valid results and the
unavailable NaN classification. Unrelated consumer branches throw in the fixture.

The first attempt used an incorrectly calculated jump-table page and stalled;
stopped only the verified isolated test process. Corrected the page to 82009000,
then the suite passed in 0.86 seconds with a 30-second timeout. Disabled Windows
crash dialogs in this test executable to avoid interactive stalls on future
fixture faults. No game process was stopped or restarted. This tests consumer
math/CPU effects, not combined live hook/query/reset execution.

### Profiling replacement candidate linked (September 9, 17:28 local)

Refreshed all application objects, native libraries and test targets, then linked
out/build/win-native-clean/edf2027-native-profile-20260909.exe using separate
executable/import-library/PDB names. Size 31,418,368 bytes; SHA256
CC1DFD499E2F1312FB8BF56C830C4165BF3466C84F8F8E4443BD29974DE24E36.
Includes the native present profiler and shared result helper, plus the prior
native state and diagnostic-capture changes. All 18 tests passed in 4.89 seconds.

PID 61492 remained responding on the original executable. No existing candidate
was overwritten and no new game process was launched. This is now the newest
diagnostic candidate, not proof of runtime fidelity or a release artifact.

### Remaining hardware-statistics ownership boundary (September 9)

39148 belongs to a separate four-slot statistics ring, not the migrated present
ratio ring. It uses device+20080 mode 2, selects the next slot after +20088,
and skips when that slot equals +20084. It emits a wait-start/end timestamp
into the 32-byte buffer at +20076 but does not advance the producer itself.

39228 allocates four query objects at +20060..20072 through 42480, configures
hardware counter selectors through 42518, and issues the next query through
42B38 before advancing +20088. 38040 checks that query's fence through 42B20
(query+12 -> D080), preserves the prior 480-byte result block, obtains the new
block through 441D8, subtracts the matching wait timestamps, advances +20084
and caches the result for the CPU frame at +20092. This shared query/result
contract must be migrated before replacing 39148 timestamps independently;
raw native timestamps cannot safely be combined with the retained counter block.

Extended read-native-worker-state.ps1 with the four query handles, wait-buffer
pointer, producer/consumer and cached frame so later runs can identify activation.
No production profiling behavior changed in this audit. The next source boundary
is query creation/configuration/issue/readback, not just the small marker emitter.

### Hardware-statistics consumer narrowed (September 9, after latest build)

Current generated-source direct-call search and the independent analysis
callgraph agree: the only direct caller of 38158 is 38E00. Its command-255
branch walks sixteen enabled statistic descriptors and forwards each float
result to FA0D8. FA0D8 (shard 32) sends a tagged identifier and the float bits
through an optional indirect callback at the object behind global 82000800,
offset 24, using event code 39. It does not directly feed rendering or gameplay
decisions. The callback target and indirect callers remain to be established;
this is not proof that all consumers are inert.

The only direct callers of 42480, 42518 and 42B38 are in 39228; 441D8 is called
by 38040. Query issue 42B38 ends by assigning device+10780 to query+12 after
writing its register-readback commands. It does not create a separate completion
event at its tail. Therefore retaining query+12 alone cannot make the native
480-byte payload valid. Configuration 42518 reserves 120 words, emits selector
register writes, then a further six-word control sequence after a capacity check.

Next migration decision: establish the optional callback's consumers and replace
the statistics publication contract at that level, with native measurements and
explicit unavailability for Xbox-only counters. Do not invent a 480-byte Xbox
counter payload or remove query synchronization in isolation. No production
behavior was changed by this audit.

The user-requested build produced edf2027-latest.exe beside its existing runtime
dependencies (31,418,368 bytes, September 9 17:37 local). All 18 tests passed in
4.29 seconds. The existing game was not stopped and the new executable was not
launched; visual fidelity and 60 FPS remain unverified.

### Gameplay performance triage after user-reported failure (September 9)

The original live process was still PID 61492, edf2027.exe started at 15:52;
edf2027-latest.exe was not running. Its current 17:39 log reported 15.9--17.5
FPS and individual frames up to 274.57 ms. These current measurements are not
a controlled gameplay benchmark or measurements of the latest candidate.

A historical active-scene interval at 16:02 gives a more specific lead:
20.0 FPS / 50.00 ms average frames, with swap.gpu_wait averaging 3.15 ms
(805.5441 / 256 calls) and swap.refresh_wait averaging 20.58 ms
(5269.0094 / 256 calls). Indexed native hooks consumed about 718 ms over a
five-second reporting interval; activation native hooks about 646 ms. These
inclusive, differently aligned buckets must not be summed as a frame breakdown.
The refresh-wait cost is large enough to prioritize pacing validation over
speculative mesh-upload changes; this does not establish the entire bottleneck.

Source inspection confirms indexed geometry already has exact-content caching,
and immediate geometry already reuses dynamic buffers. No blanket cache bypass,
draw dropping, or quality reduction was applied. Compare the latest candidate's
pacing against the old run first, without diagnostic image readbacks in the
performance pass. Use a separate consecutive-frame capture pass for the flicker.

The read-only inspector also now reports the optional statistics callback chain:
at 17:39:10, global 82000800 pointed to 30002000, whose object pointer was zero.
No callback was installed in this snapshot. This observation does not justify
removing potential callback behavior in other configurations.

### Automated flicker investigation (September 9, 18:10 local)

User authorized closing the prior game, then reported latest-build prelude
black-square flicker, excessive brightness and 30 FPS gameplay. They explicitly
asked for autonomous debugging, not manual screenshots. Diagnostic runs may be
launched and stopped in scope; do not request the user to catch the flicker.

Host cadence source now only rearms its Win32 timer when switching between
16 ms visible and 250 ms occluded cadence. Previously every Present restarted
the timeout, adding a fresh delay after possible VSync waiting. Built separate
edf2027-cadence.exe; 18 tests passed (4.52 s), but the tests do not establish
60 FPS runtime performance. User-launched processes were not visible at the
subsequent process inspection. Start-Process launched PID50060 previously exited
without a logged cause. Foreground exec sessions have kept the automated runs
alive reliably; do not infer a crash cause from that difference alone.

Captured and analyzed paired output/scene-color bursts:

- flicker-burst-20260909: frames 1..128, initial street view, no exact magenta
  invalid-color markers.
- prelude-mid-burst-20260909: frames 200..327, camera tilting toward mothership;
  isolated magenta scene-color markers begin at frame258, coordinate390,3.
- mothership-burst-20260909: frames 600..727, ship fills the sky. Isolated
  scene-color markers include603 at698,509 and604 at567,509.

The output is visibly brighter than scene color (for example frame640), consistent
with post processing but not proof that tone mapping is incorrect. None of these
bursts yet proves the reported large black square; threshold scans detect only
small scattered newly black pixels. BMP magenta is the nonfinite marker, but
an identical finite color can collide with it; direct floating readback remains
necessary. Do not report the flicker fixed or its cause established.

Added tools/analyze-native-capture-burst.ps1: read-only 24-bit top-down BMP scan
of black/magenta counts and newly black bounds. Fixed PowerShell null-string
marshalling for the first frame. It analyzed all three 128-frame paired bursts.

Added edf_native_probe_frame (default0 preserves first-scene behavior) to delay
the existing per-draw pixel probe until a requested indexed output candidate.
Built edf2027-flicker-probe.exe (31,421,440 bytes). Its run used frame258 and
pixel390,3, but did not reproduce the invalid value at that fixed coordinate.
The camera is not deterministic by output index: the comparable marker397,28
appeared at284 instead of262 in the previous run. Therefore repeat fixed-pixel
probing is the wrong next step; scan a region for invalid float pixels and
attribute the first producing draw without relying on frame-exact replay.

At this entry the diagnostic process is PID61508, edf2027-flicker-probe.exe,
foreground exec session97186. Prefix mothership-pixel-probe-20260909; 32 paired
captures258..289 complete; no invalid-RGB-origin log. Its automated menu input
tools/native-flicker-input.txt ends at61 seconds, no movement/fire. Earlier
owned runs61016/60980/6408 were deliberately stopped after preserving captures.
Revalidate live process/session before continuing. User screenshots not needed.

### Confirmed mothership normal-decoder defect and correction (September 9)

Added FindNativeInvalidColorPixel: bounded rectangle readback of RGBA16F,
row-pitch-aware, detects NaN/infinity in RGB only, ignores finite magenta and
nonfinite alpha. RGBA8 is finite by definition. Tests cover offset coordinates,
all RGB channels, signed infinities, NaN, excluded rows, unchanged source/validity,
and oversized regions. Region probe flags default to the prior single-pixel
behavior; draw budget defaults4096 and is clamped1..65536. These expensive
diagnostics remain opt-in and are not performance benchmarks.

Uncorrected edf2027-region-probe.exe, PID19544, prefix
mothership-region-probe-20260909: first probe frame300 found pixel400,39 invalid
after draw504910. VS_Blend and PS_Main shared fingerprint9b2c2827c6c18465,
matching c_Mech01.dxsl (11637 source bytes). RGB was actual float NaN, alpha
0.8227539. All29112 sampled normals and tangents were finite/nonzero, unlike
the previously fixed missing-tangent bug. The scene is explicitly cleared
before these draws. The shader calls Common.fx tex2D_DXT5N_xGxR, whose Z
reconstruction is sqrt(1-dot(N.xy,N.xy)) with no domain guard.

Added a GPU regression executing the authored decoder with a sampled HDR
texture. Independently quantized XY can leave the unit disk, including
decoded(1,1) and (1,1/255). The regression failed with nonfinite RGB before
the correction. Its initial fixture errors (combined sampler resource name
and explicit resolve content validity) were corrected before that failure.

Native compilation now adapts only the named normal-decoder function, both
in direct effect source and Common.fx includes, to reconstruct Z from
max(0,1-dot(XY,XY)), using a conditional that preserves actual NaN inputs.
No game assets were edited; no global sqrt/NaN scrub, GPU emulation, or
lighting/exposure adjustment was added. Finite in-domain values and XY remain
unchanged. The regression now passes, including a genuine-NaN input check.
All18 test targets were refreshed and passed in5.01s; all44 disc effects,
130 shader entries +66 reverse-depth variants, and90 linked passes compiled.

Built edf2027-normal-fix.exe, SHA256
2BA5AE9BE915616769F474889356A99EA5D5FA0954F05C3C7DBDC98835E26C9E.
Validation run PID59800, foreground exec session65277, prefix
mothership-normal-fix-20260909, same frame300 full1280x720 region probe.
At18:22:34 it exhausted32768 draws through output candidate318 with NO
invalid-RGB-origin report. All128 paired captures300..427 also have zero
magenta markers. This verifies the observed normal-decoder NaN defect is
removed, not every manifestation of the user-reported black square.
Output310 remains visibly overbright; brightness and60FPS remain unresolved.
The old region-probe process19544 was deliberately stopped before validation.
Current validation process59800 was left running; revalidate before use.

### Untiled native scene lifecycle (experimental, September 9 follow-up)

Later work is tracked in `native-performance-vsync-audit-20260909.md`; the
process reference above is historical, not a live-process assertion.
`edf_native_untiled_scene` bypasses the audited Xbox tiling begin/end callers
and maintains native begin/end pairing. Native full-resolution scene targets
remain responsible for color/depth and resolve. A native viewport CPU-state
replacement handles both initial setup and later active-scene viewport resets
using native dimensions; other targets keep their own dimensions. Paired
captures verify full-width rendering after an initial640-pixel clamp regression.

The viewport replacement originally incurred millisecond-scale per-word OS
validation overhead. CPU-owned viewport/transform/scissor/dirty state now uses
batched ordinary stores with fresh committed writable-range proof for ordinary
virtual heaps. Physical/XEX heaps retain OS validation, and fence/worker writes
retain their original atomic publication path. Tests cover byte order, bounds,
permissions, commitment and physical-watch exclusion. A profiled mission run
verifies viewport writes below1us/call and full-width gameplay/HUD.

This remains OFF by default pending clean performance and broader fidelity
validation. It does not establish full Xenos removal: the default still retains
the CPU tiling workflow, and remaining submission/statistics/capture contracts
must be completed. Brightness and sustained60FPS are not resolved.

Follow-up: after clean-performance, movement/firing, pause/resume and Mission1
retry checks documented in the performance audit, untiled is now the native
default. Explicit `edf_native_untiled_scene=false` retains a comparison path.
`edf2027-native-untiled-default.exe` was verified at startup without an untiled
override. Earlier OFF-by-default statements above describe historical builds.
This removes CPU tile recording from the default native path, not every remaining
Xbox contract. The D1C8 native drain already omits its packet while preserving
completion and worker waits; inspecting the original generated body alone is
not evidence that the native build executes its packet encoder.

### ECD8 cache-packet encoding removal with fallback reservation preserved

C5F0's remaining known caller8214ECD8 consumes its address output as fallback
CPU storage, so CF60's allocation-free replacement is not appropriate there.
Added a second fingerprint-checked extraction retaining the exact C328 allocation,
failure branch, BE68 atomic dirty-range exchange and address conversion/output.
Only cache packet stores and exposed packet word count are removed. ECD8 therefore
skips its cache-only C868 enqueue, while its C9F0 worker-signal reservation and
later submission path remain unchanged. This is not removal of every CPU list or
reservation, and native code does not interpret those reserved bytes as packets.

32 paired cases cover dirty/sentinel ranges, command-cache flags, aliases and
allocator failure. The native address output, device CPU state, allocation and
exchange counts match retail; packet bytes remain untouched and words return0.
Stack/LR and nonvolatile registers are checked. Full rebuild/all18tests pass4.66s.
The tests directly cover C5F0, not the whole ECD8 caller or live allocation-failure
recovery. Enclosing caller validation remains required before broad completion
claims. No clean FPS improvement is claimed for this inactive-in-recent-runs path.

### Enclosing ECD8 submission regression

Added the unchanged generated ECD8 body to the CPU-tail fixture.128 paired cases
cover recording/nonrecording, special/forced submission, dirty/sentinel ranges,
and all8 combinations of cache/signal/list allocation success/failure. The native
run uses the new reservation-preserving C5F0; the reference uses retail C5F0.
Allocation calls, dirty exchanges, restore/list-clear calls, device CPU state,
return/ABI, worker-signal arguments and incrementing worker submission ranges
match. The only descriptor removed is the nonincrementing cache-only enqueue.
The targeted fixture passes2.42s.

C9F0/C868 and CPU list helpers are controlled dependencies in this enclosing
test. C868 models the busy increment and records descriptor arguments; it is not
real queue traversal or D3D completion. Failure fallback stack storage is seeded
deterministically in both runs. These tests establish the enclosing control-flow
contract, not general real-world allocator recovery or live worker completion.

### Live cache-packet-free worker completion

Preserved edf2027-native-cache-packet-free.exe SHA256
FEBA97A02115D5F1E65AD88AB235ADE850D6EA3485FDE2E9A6136B774F6D1B3E.
Ran ownedPID51796 with edf_native_untiled_scene=false specifically to exercise
ECD8; graphics remained native D3D11, mesh-watch audit off, hook timing on.
Log native-cache-packet-free-legacy-20260909.log. ECD8/ED64 worker signals and
EE44 incrementing descriptors execute repeatedly (23 words); no EDF8 cache-only
enqueue or error/critical lines found in the run.

Read-only non-atomic snapshots show issued/completed3793/3791 at22:02:01,
15053/15047 at22:02:47,25603/25597 at22:03:32,33433/33427 at22:04:11. Worker
lock returns to0; final busy0. Ring cursor25506 then3177 confirms continued
progress across wrap. These observations establish ongoing completion, not a
single successful enqueue or an atomic queue-consistency snapshot.

Inspected native-cache-packet-free-legacy-20260909.png: Mission1 world/player/HUD
visible. Runtime modules include d3d11.dll and no xenos-named module. Stopped
exact owned process after path validation; WaitForExit true. This validates the
changed caller through mission entry/stationary gameplay, not arbitrary allocation
failure under live load, mission teardown, brightness fidelity, or clean60FPS.
The normal untiled default is unchanged; false was only a diagnostic override.

### No silent native signal/cache packet fallback

Native C9F0 now rejects callbacks other than implemented EBA0 before entering any
Xbox encoder. The known51248 producer has already been replaced at512D8 by native
swap pacing; an unexpected caller now reports its address instead of recording a
GPU interrupt packet that has no consumer. Native C5F0 likewise rejects unknown
callers after dispatching CF60/ECD8 to their respective CPU implementations.
Non-native reference branches remain isolated behind edf_native_host=false (the
port forces native mode during setup). Removed obsolete retained-cache auditing
that became unreachable after this boundary check.

These are explicit unsupported-operation guards, NOT implementations of unknown
callbacks and not proof of full feature coverage. Known-path regression tests
remain applicable; they do not inject the new production error branches. Full
build/all18tests passed4.91s before the subsequent dead-code-only cleanup.

### Texture-lock cache packet removal (2026-09-10)

Audited the remaining direct callers of shared resource lock 82134408. VB/IB
wrappers use access codes 10/12 and already select its fingerprint-checked
CPU-only extraction. Texture subresource wrapper 8213AD70 (generated shard32)
uses access14 at return address8213AE24; this previously still encoded cache
packets. The native bridge now selects the same extraction for that exact pair.
Other access14 call sites remain outside this change.

The omitted 821344E8..82134570 block only reserves/encodes cache packets and
updates the packet cursor. Parent-resource fence selection occurs before it;
off-thread dirty-range accumulation, both resource dirty-range slots, CPU alias
return and atomic lock count remain in the extraction. SetStreamSource37410
and SetIndices375C0 also remain: inspection shows CPU binding and retirement
bookkeeping, not a reason to delete them wholesale as GPU packet emitters.

Expanded the actual-generated-helper differential fixture to 576 cases:
VB/IB/texture/surface types, absent/present parent flags/pointers, nonzero
subresource arguments, render/other thread, cached/uncached, fenced/unfenced,
and six lock-flag combinations. Tests compare waits/arguments, return/stack/LR,
nonvolatile registers, resource and parent records, and off-thread dirty ranges;
the native packet cursor/payload remain untouched. All18tests passed12.58s.
This tests the shared helper with controlled waits, not the whole AD70 wrapper,
real GPU readback, every texture format or live texture-lock coverage.

The first link was blocked by user-owned PID44044 running instance-bindings.
Left that process untouched and built a separate edf2027-native-texture-lock.exe.
Final candidate SHA256 (includes bounded first-three texture-lock hit logging):
3E7E222D6611D2D36C39E362047F6F11D79C8072CC411D034769326EE6416AE9.
Running instance-bindings remains E663925F...7EC7F443. No runtime test or FPS
measurement of the texture-lock candidate yet; no concurrent game was launched.

### Enclosing texture-lock regression

Added unchanged generated AD70 to the test fixture and extracted the production
34408 hook itself, replacing only its native-mode configuration lookup with a
test switch. Thus the real access14/caller8213AE24 gate runs inside AD70, not a
handwritten imitation of that dispatch. Layout/format helpers A8B0/39C30 and
completion waits are controlled dependencies.

108 paired cases vary level, six lock flags, three block sizes and three offsets.
AD70's actual pitch multiplication and returned alias/offset arithmetic match
retail, along with output records, dirty ranges, waits, stack/LR and nonvolatile
registers. The native packet buffer/cursor remain untouched. A negative control
checks that retail really emits 44 bytes on the packet-taking flag combinations.
All18tests passed13.43s; after adding that negative-control assertion the rebuilt
targeted fixture passed3.57s. The game binary was unchanged this turn.

This closes the enclosing CPU-contract test gap, not live lock coverage or
general texture-layout/readback correctness. PID44044 was absent at the initial
read-only process check; no game was launched or stopped during this test work.

### Live texture-lock validation

Candidate 3E7E222D...6416AE9 ran as owned PID22548 from 04:23:36 through
04:27:31. Artifacts: out/native-bridge-run/binding-validation-20260910-042336-294786b4/.
Used the movement/fire script and IntegerPostCenters with fresh user settings,
automatic captures disabled and mesh-watch audit off. The new texture-lock hit
log appeared on three threads (3624,45892,53500), proving the guarded native
replacement executes in this startup/mission workload.

Inspected intro.png (mothership/city), firing.png (different street orientation,
muzzle flash, shell casings, impact effects, AF14 62/120) and after-input.png
(street/player/HUD, AF14 100/120 after continued fire/reload). This supplies
active firing and movement/turning evidence, not a full mission or pixel-perfect
effect/brightness comparison. All retained logs contain zero error lines.
Final indexed sample: 12,431,000 draws, 765 builds, 12,430,235 hits, 426 entries,
zero evictions or source mismatches. Published index reuse745/rejections0;
retained vertex reuse1/replacements0. Module inspection found d3d11.dll and no
xenos-named module. This is not a transitive dependency or all-feature audit.

Stopped only exact-path-validated owned PID22548 and waited for exit. Hidden
presentation is not a visible FPS benchmark. Sampled index-publication logging
also executed (observed publications1024/2048); no loading-time speedup measured.
No executable changed during this validation. Texture formats/readback outside
this workload, remaining geometry writer ownership, broader missions and handheld
performance remain unfinished.

### Repeatable import audit and fresh-directory startup

Latest candidate recheck (2026-09-10): ran the read-only PE audit directly on
`edf2027-native-buffer-notifications.exe` (B17C7B0A...042F506). Passed with two
packaged non-system dependencies: rexruntime.dll and amd_fidelityfx_dx12.dll;
no Xenos import or staged Xenos DLL. Current application configuration still
clears gpu_plugin and forces native D3D11. This is dependency/configuration
evidence, not proof that all guest graphics-resource bookkeeping is removed.
The latest shader-retirement/buffer-notification runtime regression remains
pending while non-owned PID6508 runs the verified metadata-loads candidate.

Added tools/audit-native-dependencies.cmake and audit_native_dependencies build
target. The read-only audit follows PE imports with CMake/dumpbin, stops at
Windows System32/API-set prerequisites, rejects unresolved/conflicting imports,
rejects non-system imports outside the executable directory and rejects staged
Xenos-named DLLs. Initial whole-OS recursion reported optional Windows feature
imports; explicitly excluding resolved System32 files bounds the audit to the
redistributable application's dependency closure. It does not prove the absence
of dynamic LoadLibrary calls or arbitrary GPU emulation code inside another DLL.

Both the native build target and fresh-directory audit passed. Non-system imports
are rexruntime.dll and its required amd_fidelityfx_dx12.dll. Negative controls in
out/native-dependency-audit-066b51356f3e42f692bb06fbb9f85d52 rejected missing
rexruntime.dll and a copied runtime deliberately named rexgpu-xenos.dll. The
latter was only a filename fixture, not an actual Xenos plugin; renamed that
copy back to rexruntime.dll afterward, and the corrected folder passed.

The fresh directory contains the unchanged 3E7E222D...6416AE9 candidate, those
two DLLs and gamecontrollerdb.txt. Owned PID33180 ran there 04:31:54..04:32:59
using externally supplied game assets and a fresh user directory. Artifacts:
out/native-bridge-run/binding-validation-20260910-043154-152c873a/.
Inspected startup.png: movie imagery/subtitle text renders. No logged errors.
Module paths prove rexruntime and FidelityFX loaded from this fresh directory;
d3d11 loaded from System32 and no Xenos-named module appeared. Stopped only the
exact-path-validated owned PID and waited for exit. This is a fresh-folder loader
and startup check on the development machine, not a clean Windows installation,
redistributable installer certification, full gameplay test or handheld test.
