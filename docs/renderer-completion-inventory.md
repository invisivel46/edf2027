> Historical as of 2026-09-22; current status in [renderer-status.md](renderer-status.md).

# Renderer completion inventory

2026-09-21. User-directed change of approach: batch the remaining reverse
engineering and migrate complete subsystems instead of continuing isolated
ownership flags and one test launch per flag. This inventory is a work map,
not a claim that static analysis establishes complete runtime coverage.

## Evidence collected

`tools/ghidra/inventory-renderer.ps1` ran successfully against the existing
`edf2027-geometry/guest_image.bin` project in read-only mode. Temporary function
definitions were discarded. `RendererInventory.java` exports root decompilation,
reachable defined functions, direct edges, computed transfers, and p-code STORE
sites. It uses the generated function-address manifest to expand missing direct
targets. STORE sites include stack saves and are not automatically gameplay writes.

The first 16-root scan was too narrow (365 emitted functions and 60 indirect
sites). The final batch expands roots using current graphics hooks, externally
classified render/model methods and the complete verified tables containing
those methods. External class/role names are testimony; external port-completion
labels are deliberately excluded because they describe a different codebase.

| Final census | Result |
|---|---:|
| Explicit, classified and table-derived roots | 2,509 |
| Functions in emitted direct closure | 9,216 |
| Ghidra function definitions | 9,236 |
| Emitted functions absent from Ghidra export | 0 |
| Indirect call sites with raw-image-verified instruction addresses | 3,332 / 3,332 |
| Recovered table pointers matching raw image and loaded Ghidra image | 6,157 / 6,157 |
| Selected renderer/model class tables | 187 |
| Current graphics hook/macro boundaries | 237 |
| Explicit original-function calls in hook bodies | 158 |
| Explicit named CPU-tail/adapter calls in hook bodies | 26 |
| Platform synchronization imports in hook bodies | 2 |
| Additional original calls through forwarding macros | 87 |
| Original function passed to a helper as a callback | 1 |

**These are coverage counts, not missing-port counts.** The expanded closure
includes shared utilities and gameplay dependencies. Native hook presence does
not establish replacement; no explicit original call in a hook does not establish
independence either. Conditional fallbacks, audit paths and producer obligations
need semantic review. This is a complete exported census for the stated seed and
source scope, not proof of complete runtime renderer closure.

Ghidra's 20 additional functions include 16 `savegprlr` entries and four other
definitions. Its missing-target export contains 5,689 references to 182 unique
targets: 171 map to generated symbols and 11 to emitted internal labels. These
are recorded with locators rather than silently discarded. Definition coverage
does not establish that every function body is complete.

The store ledger contains 87,579 p-code STORE sites: 37,322 use explicit `r1`
stack-base syntax and 50,257 do not. This is a syntactic triage only: stack aliases,
escaped locals, vector writes and target object lifetimes require review.
The indirect ledger separates 2,697 apparent vtable-slot loads, 404 other
constant-offset loads and 231 register/function-pointer calls. These are load
patterns, not resolved target populations. All 3,332 remain explicitly marked
unresolved at the receiver-population level.

Artifacts:

- `out/ghidra/renderer-inventory/{functions,edges,sites,missing}.tsv`
- `out/ghidra/renderer-inventory/<address>.c`
- `out/renderer-inventory/renderer-boundaries.csv`
- `out/renderer-inventory/renderer-indirect-sites.csv`
- `out/renderer-inventory/generated-call-edges.csv` and `source-manifest.csv`
- `out/renderer-inventory/summary.json`
- `out/renderer-inventory/complete-function-inventory.csv`: every selected function,
  root provenance, native boundary evidence, direct/indirect counts and Ghidra coverage.
- `out/renderer-inventory/indirect-site-ledger.csv`: exact call instruction, emitted
  source locator, load chain, possible slot and explicit resolution status.
- `out/renderer-inventory/core-callback-candidates.csv`: 28 candidate table entries
  for the eight outer-frame calls; pointer verification is separate from receiver proof.
- `out/renderer-inventory/native-original-dependencies.csv` and
  `macro-original-dependencies.csv`: explicit and macro-generated original calls.
- `out/renderer-inventory/native-dependency-contexts.md`: source context for all
  186 explicit original/adapter call sites, including branch guards.
- `out/renderer-inventory/native-dependency-classification.csv`: source-reviewed
  control-flow classification of all 186 explicit sites; callee write contracts
  remain a separate unresolved field. Reviews are pinned to the source SHA-256.
- `out/renderer-inventory/native-original-references.csv`: original function
  references passed to helpers, which a direct-call-only scan misses.
- `out/renderer-inventory/adapter-{extraction-manifest,functions,calls,store-sites}.csv`:
  freshly regenerated adapter code, source hashes, retained calls and scalar stores.
- `out/renderer-inventory/state-write-ledger.csv`: exact instruction-level write
  candidates with stack-base syntax separated, without invented ownership labels.
- `out/renderer-inventory/ghidra-unresolved-direct-targets.csv` and
  `ghidra-additional-functions.csv`: reconciliation exceptions and source mappings.
- `out/renderer-inventory/{complete-summary,reconciliation,review-summary}.json`.
- `out/renderer-inventory/{root-manifest,native-source-manifest,external-role-evidence,verified-vtable-pointers}.csv`.
- `out/renderer-ghidra-final.log`: successful read-only Ghidra run.

Reproduce in this order:

```powershell
tools/enumerate-geometry-writer-callers.ps1 -OutputDirectory out/renderer-inventory
python tools/inventory-renderer-boundaries.py
python tools/complete-renderer-inventory.py
tools/ghidra/inventory-renderer.ps1
python tools/resolve-renderer-inventory-sites.py
python tools/summarize-renderer-inventory.py
python tools/classify-renderer-dependencies.py
python tools/inventory-renderer-adapters.py
tools/ghidra/inventory-renderer.ps1 -Script RendererRegistration.java
python tools/inventory-renderer-registrations.py
```

Java/installation paths are workspace defaults in the launcher. External input
is read-only `D:/roms2/edf2027-analysis/edfdb.sqlite` (SHA-256
`c66faeab57d1e3561ba100068a98730c03b0557fa1dbae35b17f9bbab0e2fac1`)
and `guest_image.bin` (SHA-256
`91922ff98eeab36a595673f773123530d67489f1614dd6a5ba758e3e455efb1a`).
The SQLite connection uses immutable read-only mode. Current-source boundary
scanning covers `src/native_graphics/*.{cpp,h}`; host scheduling and other source
directories remain separate integration review surfaces. `out/` artifacts are
local generated outputs; durable snapshots are recorded in Epistemic.

## Concrete callback and forwarding boundaries

| Outer-frame instruction | Receiver / slot | Candidate targets from verified tables |
|---|---|---|
| `821A5158` | manager+132 / 1 | `821BE8D0`, base `821D5978` |
| `821A51D8` | world list / 2 | map manager `820B4310` |
| `821A5264` | listener list / 3 | Sato `8216DA80`, shared `8252B718` |
| `821A5290` | view / 4 | player `820D3FD0`, free `820D2390`, game base `820D3050`, motion `8217B0B0`, shared `8252B718` |
| `821A52A8` | manager+132 / 2 | `821BE9D8`, shared `8252B718` |
| `821A52E4` | manager+132 / 3 | `820B0B80`, shared `8252B718` |
| `821A52F8` | manager+132 / 4 | `820AFEE8`, shared `8252B718` |
| `821A5368` | listener list / 4 | Noguchi `820A4DD0`, Sato `8216E630`, shared `8252B718` |

Receiver offsets/slots are cross-checked with `native_frame_dispatch.h` and
emitted instructions. Table pointers are verified; class-family assignments and
the completeness of receiver populations are still provisional. Table class
names alone do not prove registration in these lists. Bucket dispatch separately
calls slot 4 at `821A3C50`; static method `820B2670` appears in nine recovered
map-object class tables. Registration, removal and unsupported modes must close
these populations before replacing dispatch wholesale.

`EDF_RENDER_PHASE` explicitly forwards nine boundaries: `821A3BA0`, `821B2C28`,
`820D3FD0`, `821BE9D8`, `820B0B80`, `821C9478`, `820B35A0`, `8216DA80`,
`820A6978`. Its body only adds timing. This declaration alone therefore provides
no evidence of native replacement. Other paths may bypass these hooks; that is a
separate reachability question. The other forwarding macros cover 37 map-timing
hooks, four tree invalidators and 37 render-state setters. The state setters call
the original before publishing state; tree hooks invalidate before forwarding.

## Retained-call review

All 186 explicit hook call sites now have a reviewed control-flow category in
`native-dependency-classification.csv`. This review distinguishes five visibility
audit calls, 20 native-host-disabled fallback sites, 16 shader-bridge-disabled
fallback sites, four material-activation fallback sites and 12 exact ownership
predicate fallbacks. Other categories include producer/lifetime forwarding,
active rendering compatibility work, unsupported callbacks and platform imports.
These categories describe the inspected branches; they do not prove complete
callee effects or that every branch is exercised at runtime.

Concrete remaining boundaries identified by that review:

| Boundary | Observed retained responsibility | Inventory implication |
|---|---|---|
| Model `821C9C20` | Original called even after native pose interpolation, at bridge line 4330 | Interpolation is not native model-pass ownership. |
| Uploads `821A1738` / `821A17D8` | Original upload precedes conditional native scratch overwrite | Include upload destinations and dirty-state transitions in animated-content migration. |
| Static group `821D96D8` | Extracted indexed CPU tail runs after accepted draws or geometry handoff, lines 3400/3540 | Submission replacement leaves CPU-state obligations. |
| Object `821C0C00` | Gather fallback invalidates snapshots, invokes original and refreshes live view | Unsupported object callbacks prevent a fully retained static selection contract. |
| Signal `8213C9F0` | Extracted route selected only for callback `8214EBA0`; others call original | Native-host enabled does not close the signal callback population. |
| Lock `82134408` | Extracted path covers access 10/12 or access14 at LR `8213AE24` | Other access/caller combinations remain original. |
| Texture `82201458` | Passed as `ImportTexture` callback at line6404; helper calls it at lines1254/1278 | Added a function-reference ledger; not visible to the earlier direct-call scan. |
| `KfAcquireSpinLock` / `KfReleaseSpinLock` | Native submission synchronizes device+10872 | Corrected earlier classification: these two are platform imports, not CPU tails. |

All 16 production adapter extraction recipes ran successfully against current
generated source, independently of potentially stale build outputs. They produced
34 definitions, 110 selected guest/adapter/import call sites and 247 scalar store sites.
All 24 distinct adapter symbols referenced by explicit hook calls were found.
ABI save/restore helpers, native C++ helper effects and inline
vector/atomic stores are outside those selected call/store counts.

For example, the regenerated indexed CPU tail copies entry r3 to r31, then
conditionally clears 64-bit device words at offsets 0,8,16,24,32. When the
device+24 mask has bit1 set, it also writes sign-extended `-16777216` at
device+11568. Its remaining selected nested call is
`__imp__edf_native_main_state_cpu_tail`. The immediate preparation additionally
temporarily writes device+12256 and restores its saved byte before returning no
guest vertex storage. Main-state extraction still calls shader-cache,
shader-upload and derived-state extractions. This is an explicit chain of CPU
state work to assign, not evidence that deleting the draw tail is safe.

## Startup registration chains

A separate read-only Ghidra run decompiled ten registration/constructor functions.
The instruction ledger independently matches 4,186 database instruction words
to the raw image. Outputs are `out/ghidra/renderer-inventory/registration/`,
`registration-instructions.csv`, `registration-direct-calls.csv`,
`startup-listener-registration.csv` and `registration-summary.json` under the
inventory directory. Run log: `out/renderer-registration-ghidra.log`.

Startup `820B1F60` loads the manager from global `8257C030`, constructs the
following objects, wraps each in a shared pointer on its stack and calls
`821A68C8`. That helper inserts into the manager+2228 list via `8219EA48`.
The insertion copies the shared-pointer object to node+12 and its reference
counter to node+8, matching frame dispatch's object read from node+12.

| Constructor | Installed vtable | Constructor call | Registration call | Frame slot3 / slot4 |
|---|---|---|---|---|
| `820A9618` | `82001648` (Noguchi) | `820B21AC` | `820B21F8` | `8252B718` / `820A4DD0` |
| `82170488` | `820118D4` (Sato) | `820B220C` | `820B2258` | `8216DA80` / `8216E630` |
| `82195AB8` | `82015E1C` (Okada) | `820B226C` | `820B22B8` | `8252B718` / `8252B718` |
| `821E4C08` | `82020360` (Iwasaki) | `820B22CC` | `820B2318` | `8252B718` / `8252B718` |

The same startup constructs the backend through `820B1028`, which installs
vtable `82001EF0`, and passes its shared pointer to `821A4980`. That setter
copies the object into manager+132 and the reference counter into manager+128.
Thus the four backend frame slots have a traced startup assignment as well.

These chains strengthen the earlier class-name candidates into observed static
registration paths. They do not exclude later replacement, alias writes or
indirect registration calls. The direct-call database lists only `820B1F60` as
a caller of `821A68C8`; that is a scoped negative result, not a whole-program
proof that no other registration mechanism exists. World/view/bucket receiver
registration required further tracing; the world/view factory paths below now
cover eight concrete insertion paths. Bucket population remains open.

The registration Ghidra batch now also covers the camera transition and embedded
list primitives (21 functions total). `821A5A10` reads pending camera records
through the sentinel at manager+36, takes each non-null record+8 pointer, and
calls `821A1628(manager, camera+4)` before clearing the pending list at manager+32.
`821A1628` first unlinks its second argument from any previous list, then inserts
it after the first argument. Its reciprocal next/previous stores establish a
move operation, not an allocation or reference-count increment.

Camera base constructor `821CE168` initializes that node at camera+4, writes
camera itself at camera+12 (node+8), records the manager at camera+16 from the
construction context, and builds the derived matrices. The player camera chain
`820D44D8 -> 820D39A8 -> 821CE168` then replaces the base vtables, ending at
`82003FA4`. Consequently the active-list payload location consumed by frame
dispatch is consistent with this constructed player-camera layout. Destructor
`821CDDD8` changes the vtable to `82019D4C` and unlinks camera+4 via `821A1678`.
The transition itself does not identify every possible producer; three concrete
pending-list producers are now traced below. Exhaustive population remains open.

World objects use a different embedded-node layout: `821AD1F8` initializes a
node at object+8 and places the object pointer at object+16 (again node+8).
The map-manager constructor chain `820B6068 -> 821C79F0 -> 821AD1F8` installs
`82002624` on the final object. This matches the frame world's node+8 payload
layout; the map factory insertion is traced below. Equal node payload
offsets are not evidence that world and camera ownership or insertion paths are
interchangeable.

### World and camera factories

The expanded read-only registration batch now covers 36 functions and 5,548
raw-image-verified instructions. `world-camera-factory-registration.csv` records
the constructor and insertion instruction addresses for these eight paths:

| Family | Factory â†’ constructor | Installed table | Frame callback |
|---|---|---|---|
| Effect objects | `820A53D8 â†’ 82113028` | `820072D4` | slot2 `820D4850` |
| Map | `820A5448 â†’ 820B6068` | `82002624` | slot2 `820B4310` |
| Game objects | `820A54B8 â†’ 820D6C40` | `8200427C` | slot2 `820D4850` |
| Boss objects | `820A5528 â†’ 820D4928` | `82003FBC` | slot2 `820D4850` |
| Map effects | `820A5598 â†’ 820B3620` | `82002214` | slot2 `820B3610` |
| Player camera | `820BF180 â†’ 820D44D8` | `82003FA4` | slot4 `820D3FD0` |
| Free camera | `820BF0D0 â†’ 820D2578` | `82003E28` | slot4 `820D2390` |
| Motion camera | `820CEEC8 â†’ 8217BED0` | `82012C5C` | slot4 `8217B0B0` |

Every listed world factory calls `821A1628(manager+44, object+8)` after successful
construction. The five insertion instructions are `820A541C`, `820A548C`,
`820A54FC`, `820A556C`, and `820A55DC`. These prove four additional world-table
candidates beyond the earlier map-only list. In particular, `820D4850` and
`820B3610` now require callback-body review alongside `820B4310`.

The three camera factories pass the constructed camera pointer to `8218E7A8`
with the list at manager+32. This helper allocates a 12-byte queue record, writes
the camera pointer to record+8, links the record, and updates the list count via
`821C3D78`. Insertion instructions are `820BF200`, `820BF150`, and `820CEF48`.
The already-traced `821A5A10` consumes these records into the active view list.
The factories temporarily publish construction context at `8257C320` and clear
it on return; the camera base constructor reads that context's manager field.

Class names remain external naming testimony, while table values, constructor
chains, argument flow and list writes are independently inspected. These paths
establish what the factories can register, not which game modes invoke each
factory or the absence of other writers. The core callback CSV now carries this
stronger evidence and includes all five traced world tables.

### World-to-bucket dispatch and scratch writes

The next Ghidra batch extends that analysis to 43 functions (5,703 instruction
words verified against the raw image). The previously uncovered world methods
are small adapters: `820D4850` tail-calls visibility gather `820B4038` with
`r4=world+48` and `r5=context`; `820B3610` tail-calls `820B35A0` with the same
arguments. `820B35A0` traverses that list and calls `821C0C00(object,context)`
for each node+8 object. The direct-call database has exactly those two callers
(`820B4038`, `820B35A0`) for `821C0C00`, and only `821C0C00` for `821A3B80`.
Indirect callers and alias writes are not excluded by that result.

`821C0C00` selects the following paths, cross-checked against emitted PPC:

| Input condition | Action | Direct state writes |
|---|---|---|
| object halfword+64 nonzero | Return | None in this helper |
| object word+52 equals0 | Invoke object vtable slot4 immediately | None before that callback |
| object word+52 equals1 | Compute `(context.float40 * context.float0 + context.float4) * object.float56` | Save context.float40 at object+44; quantized key bytes at object+40/+41 |
| object word+52 equals2 | Compute `object.float56 * 65536.0` | Same as mode1 |
| Other nonzero mode | PPC loads a stack float not initialized on this path | Unsupported input contract; do not invent an ordering formula |

Modes1/2 clamp the computed value using raw-image constants0.0 and65535.0,
convert to integer, store its low byte at object+40 and next byte at object+41,
then invoke `821A3B80(object.word32, object)`. Nonfinite values and unsupported
modes need an explicit contract before replacing this computation.

`821A3B80` inserts at the head indexed by the low key byte:
`manager + 168 + 4*object.byte40`, storing the old head at object+60.
`821A3BA0` traverses all256 low-byte heads in ascending order and reinserts each
node by its high key byte at `manager + 1192 + 4*object.byte41`. It then traverses
high buckets255 down to1, copies each object.float44 to context.float40, invokes
object slot4, and follows object+60. High bucket0 is excluded by the observed
loop. `821A5080` clears the512 heads before each view's world traversal.

The exact writes to replace as render scheduling scratch are therefore object
key bytes40/41, saved depth44, link60, the512 manager heads, and context32..44.
Object input fields32/52/56/64 and slot4 target lifetime still require producer
ownership and writer review. Slot4 can execute either immediately during gather
or later during bucket traversal; porting only the bucket loop does not cover
mode0 objects. The native `DispatchNativeFrameBuckets` mirrors the observed
bucket traversal, but slot4 callbacks remain explicit dependencies.

The shared target `8252B718`, present in several candidate tables, is a single
`blr` instruction in the raw-verified export. It adds no stores or nested calls;
its incoming return register is unchanged. It should not be counted as a missing
render implementation merely because a table points to it.

### Renderable constructor and slot-4 census

The constructor-call search rooted at `821C2090` found 84 table-bearing candidate
functions, 83 edges, 84 distinct tables, and 47 distinct slot-4 targets. Every
slot pointer matches the raw image. These are candidates: a table-bearing caller
can construct a contained object, so the call graph alone does not prove
same-object inheritance or runtime registration. Class names remain testimony.

`RendererRenderable.java` successfully decompiled all 131 distinct constructors
and methods in this manifest. The follow-up ledger verifies 22,553 instruction
words against the raw image and records 2,658 direct unconditional branch sites
(including linked calls, tail transfers, internal branches and return helpers).
36 of the 47 method outputs contain decompiler warnings, including save-helper
inlining notices. Ghidra also follows tail transfers: for example, `820BBA48`
branches to `821C9C20`, whose body appears in the decompilation. Decompiler call
names must therefore not be mistaken for direct calls made by the entry body.

The following grouping is a work map from inspected bodies and raw branch
targets; it is not a count of missing native implementations:

| Family | Representative entry points | Observed route and remaining boundary |
|---|---|---|
| Static objects and rocks | `820B2670`, `820BAF90` | Tail transfer to `821BEE68`; decompilation shows list insertion, with LOD selection in the first method. Join this to retained static selection instead of duplicating a separate renderer for each named class. |
| Model-backed objects | `820BB270`, `820BBA48`, `821152D0`, `82115AE8`, `82117298`, `82118648`, `82120168`, `8218A658` | Call or tail-transfer to `821C9C20`; sky also changes its translation from the view before matrix/pose setup. Shared model and pose ownership remains the central dependency. |
| Animated actors and vehicles | `820D7448`, `820DEA08`, `820E7FB8`, `820EA398`, `820ECCC0`, `820F5630`, `820F9928`, `820FFFC0`, `82100D00`, `82108BE8`, `8210E6C0`, `8219A2D0`, `821E2250`, `821E5810` | Shared routes through `820DB268`, `820FC528`, `8210AE48` and model helpers, plus per-object constants and attachments. The soldier path conditionally calls `820DE790`; vehicle attachment helpers remain separate dependencies. |
| Composite and broken models | `820EC180`, `8211FAA8` | Model setup/draw plus per-piece transforms; broken-object callback copies object+708 to object+712 before matrix and pose calls. Publish needed values and resolve the copy's ownership. |
| Projectiles, particles and glass | `82113308`, `82114A98`, `82117CD0`, `82119A10`, `8211A0E8`, `8211B088`, `8211BB80`, `8211D250`, `8211DB70`, `8217D6E0` | Direct or tail routes to `821A7640`, with laser/web additional routes to `821A8628`/`821A8090`. Retaining the underlying draw hook does not replace effect geometry construction or its input lifetime. |
| Lines, sparks and muzzle effects | `820B8D28`, `8211E7A0`, `8211F540`, `82121848`, `821897A8` | Distinct helpers `821A7E08`, `821A8090`, `821A88E8`, `821A8360`, `821A8628`; geometry, view dependence and submission state need explicit retained inputs. |
| Grass and other effects | `82172698`, `8217C4A0`, `8217ECB8` | Grass traverses cells via `82171F58` and batch helpers `8218D380/398/430`; other effects submit via `821A7C70` or `8217EA40`. Their mutable counters and scratch require ownership review. |
| Debug/test objects | `8210E220`, `821E5558` | Reach `821A8CF0`; establish whether required modes instantiate them before excluding them. |
| Empty method | `8252B718` | Verified single `blr`, shared by 14 candidate tables; not a missing implementation. |

A concrete cadence-sensitive write is now located: `8217C4A0` loads object+612
at `8217C4B0`, subtracts one at `8217C4B8`, and writes it back at `8217C4C0`
before calling `821A7C70`. All three instruction words match the raw image.
Its meaning is not yet established, but executing this callback more often
changes object state more often. The independent renderer must preserve the
intended update schedule rather than replay this write per displayed frame.

The base constructor initializes object+32 from construction context
`8257C310` at context+64, mode+52 to zero, scale+56 to 1.0, and halfword+64
to zero in its decompilation. Later derived-constructor assignments and runtime
writers are still unresolved. Constructor defaults do not prove live values.

Reproduce this batch with:

```powershell
python tools/inventory-renderable-families.py
tools/ghidra/inventory-renderer.ps1 -Script RendererRenderable.java
python tools/summarize-renderable-methods.py
```

Outputs under `out/renderer-inventory/`:
`renderable-constructor-candidates.csv`, `renderable-constructor-edges.csv`,
`renderable-method-candidates.csv`, `renderable-method-review.csv`,
`renderable-instructions.csv`, `renderable-direct-branches.csv`, and the two
family/review summary JSON files. The method review contains all 47 addresses,
their class-name testimony, exact body-file locators, direct branch targets,
and decompiler-named native boundaries. An empty native-boundary column is
not evidence of a missing port: upstream native selection may bypass a method.

#### Constructor argument and installation verification

`python tools/verify-renderable-constructors.py` now independently tracks entry
`r3`, nonvolatile register copies, constants, and call clobbers through each
constructor's straight-line prefix. It verifies every consumed instruction
against the raw image, stops at unsupported instructions/control-flow splits,
and recognizes save-only helpers by inspecting their instruction bodies.
It establishes that **all 83 discovered constructor edges pass the same entry
object**, and directly proves **81 of 84 table installations at object+0**.
This removes the contained-object ambiguity for these particular edges; it
does not establish that the reverse-call search found every constructor.

The other three installations occur after a conditional call to `821C0ED8`.
Manual PPC inspection establishes their saved receiver register and table
store; neither arm changes that nonvolatile receiver register:

| Constructor | Entry object saved at | Conditional region | Table installation |
|---|---|---|---|
| `820E5688` | `820E5698`, r31 | `820E56C0..820E56D4` | `820E56E4`, table `82004F6C` through r31 |
| `8211F728` | `8211F738`, r31 | `8211F764..8211F778` | `8211F788`, table `820077BC` through r31 |
| `82120B88` | `82120B9C`, r29 | `82120BC8..82120BDC` | `82120BEC`, table `820077F4` through r29 |

Thus all 84 candidate table assignments are now located on the entry object,
conditional on execution reaching the assignment. The automated CSV deliberately
leaves those three prefix proofs incomplete rather than treating manual review
as an automated result. Their instruction words are in the raw-verified
`renderable-instructions.csv`. Runtime object lifetimes, creation by game mode,
and later vtable changes remain separate inventory obligations.

This also corrects a decompiler presentation hazard: in 15 constructors the
receiver is printed as the return value of a floating-point save helper. For
example, `821E8810..821E8838` consists of `stfd` instructions followed by `blr`;
it does not produce a new object pointer. The PPC register copies retain the
entry object across that helper. A sixteenth literal mismatch (`820D6000`)
is simply signed formatting: `-0x7DFFC014` equals table `82003FEC` modulo 2^32.

New artifacts are `renderable-table-installations.csv`,
`renderable-constructor-call-arguments.csv`, `renderable-constructor-review.csv`,
and `renderable-constructor-proof-summary.json` under `out/renderer-inventory/`.

### Scheduling setters and effect lifecycle dependency

The expanded state batch decompiled 45 functions successfully and independently
verified 2,861 instruction words and 714 direct call/transfer sites into them.
`renderer-state-instructions.csv` and `renderer-state-callers.csv` retain exact
locators. The setter bodies establish these field contracts:

| Helper | Observed operation | Direct incoming sites in the database |
|---|---|---:|
| `821C0AF8` | Store argument r4 to object word+52 (dispatch mode) | 25 |
| `821C0B00` | Store float argument f1 to object+56 (ordering scale) | 2 |
| `821C0B90` | Clear bit0x8 of object halfword+64 | 1 |
| `821C0BA0` | Set bit0x8 of object halfword+64 | 1 |
| `821C0D70` | Change object word+72; insert/unlink object+120 according to value1 | 8 |

The two bit setters are called from `820DC578` at `820DC83C` (clear) and
`820DC688` (set). Since `821C0C00` skips objects when the entire halfword+64
is nonzero, this bit participates in render suppression. Other bits and alias
writes remain unresolved. The scale setter's direct callers are `820F4B80`
at `820F4E0C` and grass constructor `82172F78` at `821730AC`. The grass
constructor sets mode2 immediately before its mode-setter call at `821730A0`.
The forwarding entry `82202A30` loads its receiver from argument object+620
then tail-transfers to the same store helper. Its two callers, `82249818` and
`8224D070`, show token/parser-like state and pass values0/1. This is not yet a
proven render-object writer: a shared function address and equal field offset
do not establish receiver type. The raw incoming-site count retains this edge,
but renderer ownership must exclude it unless receiver provenance is established.

The effect counter is now linked to a second callback. In table `82012C78`,
slot4 is `8217C4A0`, which decrements object+612 before drawing. Slot3 is
`8217C3A8`, which returns when the signed counter is positive and otherwise
tail-transfers to `821C0ED8`. Constructor `8217C840` initializes the counter
from object+388 in its decompilation. `821C0ED8` sets object byte+36 to1,
ORs mask0x1 into halfword+212, calls `821A6AA8(manager=object.word32,object)`
once while byte+36 was zero, and recursively applies itself to children via
object+24 and sibling+20. It queues a later action rather than immediately
calling the destructor; the consumer is traced below.

This is a concrete cross-callback state dependency: moving only draw submission
to an independently paced loop would change how quickly this counter reaches
the marking condition. Its intended cadence and behavior when hidden need to
be preserved explicitly. It cannot be classified as disposable render scratch.

The base-registration helpers also have exact list destinations now:
`821A4150` inserts embedded node object+108 into manager+84; `821A4160`
inserts embedded node object+120 into manager+100. Their decompilations follow
the common list primitive and show the corresponding unlink/relink writes.
The base constructor uses both; `821C0D70` can change the second membership.
These lists are distinct from world render traversal at world+48 and must not
be conflated with it based only on their similar node layout.

Reproduce:

```powershell
python tools/inventory-renderer-state.py
tools/ghidra/inventory-renderer.ps1 -Script RendererRenderable.java -AnalysisSet state
```

The state manifest and decompilations live under
`out/ghidra/renderer-inventory/{state-functions.txt,state/}`. Incoming-site
counts are exact for direct transfers in the inspected database, not evidence
that inlined/indirect/alias writers do not exist.

#### Effect update, marking queue, and destruction chain

The effect manager table `820072D4` uses slot1 `82113020`, which sets its list
argument to world+48 and tail-transfers to `820B3508`. That helper walks the
list's node+8 object pointers, skips objects with byte+36 already set, and
invokes object slot3 at raw site `820B3560`. For effect table `82012C78`, this
is the previously traced counter check `8217C3A8`. The same world slot1 target
also occurs in boss manager table `82003FBC` and map-effect table `82002214`.

`821A4BA0(manager,steps)` traverses world manager+44 and calls world slot1
inside its step loop, guarded by manager state. Its native hook still calls
the original dispatcher (`guest_shader_bridge.cpp:3103`), optionally replacing
the step count. Thus this object slot3 is reached through the step dispatcher,
while object slot4 is reached through visibility/bucket dispatch. No fixed
frequency or one-to-one ratio between these phases is established by the
static analysis.

`821A6AA8` inserts a 20-byte record into manager+72 using `820CD218`. The
record stores the object pointer at+16 and an embedded lifetime link at+8;
the temporary link is attached through object+4 before being copied into the
queue record. This is not merely a raw pointer in an unrelated list.

`821A5A10` consumes that queue using its sentinel at manager+76. For each
non-null record+16 object, it calls virtual slot1 with argument1 at
`821A5CE0`. It then clears the queue, unlinks each record's embedded+8 node
via `821A1678`, and frees the record. For effect table `82012C78`, slot1 is
`8217CE60`: its decompilation releases object+640 storage, releases wrappers
at+528/+480, calls base cleanup `821C1FE8`, and frees the object when argument
mask0x1 is set. The lifetime contracts of those nested release helpers still
need review before a native replacement can retire equivalent resources.

The observed main-loop path in `821A6508` calls step dispatch at `821A65D4`,
then cleanup at `821A663C`, then the camera/object post-update phase
`821A4DE8` at `821A6644`, subject to its branch conditions. `821A6158` is
another explicit cleanup entry. The inventory therefore separates these
obligations: effect state publication, render-only geometry generation,
step-phase counter consumption, queued destruction, and retained-resource
retirement. A complete migration must preserve their ordering and visibility
semantics, not just forward the final draw call.

#### Cleanup ownership and existing native retention

Base cleanup `821C1FE8` restores table `82019A2C`, calls `821C1080`, unlinks
embedded nodes at object+144/+120/+108, clears storage fields+96/+100/+104
after their cleanup, invokes further cleanup at+80 and+12, and walks the
observer list rooted at object+4 to zero each observer's payload+8. Thus the
marking queue's embedded lifetime link can have its record+16 object pointer
cleared by object cleanup; the consumer's null check has an observed purpose.
The attached-object helper `821C1080` repeatedly invokes slot0 with argument1
on non-null node+8 receivers in object+144's list and clears object+208.
The types in that attached-object list are still unresolved.

Wrapper cleanup `821B5258` zeros wrapper+0 and calls `820E60B8(wrapper+4)`
twice, at `821B5278` and `821B5280`. This is present in the raw instructions,
not a decompiler duplicate. `820E60B8` acts only when its argument+8 is nonzero:
it decrements the referenced entry's word+48, invokes `820E5C60` on the zero
transition, and clears argument+8. Consequently the second call has no further
reference-count decrement on a normal returning first call. The entry types
and zero-count callback still require resource-family review.

| Obligation | Guest evidence | Existing native evidence | Remaining inventory/implementation boundary |
|---|---|---|---|
| Object removal and address reuse | Queue cleanup `821A5A10`, base cleanup `821C1FE8` | Static hooks `820B33B0` and `820B2870` retire scene adapter/source owners around construction/destruction | The inspected `src` tree has no references to effect destructor `8217CE60` or base cleanup `821C1FE8`; static hooks do not establish coverage of effect lifetimes. |
| Observer invalidation | Base cleanup zeros node+8 payloads rooted at object+4 | Static source generations reject mismatched owner lifetimes | Preserve the guest observer contract and use native lifetime identifiers for every new effect/actor publication. |
| CPU effect geometry storage | Effect destructor releases object+640 storage through `820B25B8` | Retained static geometry owns backend allocations through shared pointers | Effect geometry generation must publish its own retained data before guest storage is released; generic buffer retirement alone does not establish this. |
| Resource wrappers | `821B5258` / `820E60B8` release references, with zero-count helper `820E5C60` | `NativeSceneMaterial` retains backend and texture generations; copied constant byte vectors | Resolve each effect wrapper's resource kind and retirement before replacing its guest render callback. |
| Already published static instances | Guest model replacement/destruction can remove current owners | `NativeSceneAdapter::Retire` removes live entries; prior publications retain shared instance/geometry/material references | Source-level lifetime retention exists; this does not prove GPU completion, backend shutdown ordering, or equivalent retention for unported content. |

Native source locators: `guest_shader_bridge.cpp:3782`, `:3789`, `:3810`;
`native_scene_adapter.cpp:95` and `:183`; `native_scene.cpp:159` and `:176`;
`native_scene_sources.h` (`Born`, `Retire`, `AcquireSnapshot`);
`native_scene.h` (`NativeSceneMaterial`, `NativeSceneObject`, `NativeSceneSnapshot`);
`d3d11_mesh.h:105` (`RetainedDraw`). These were inspected as implementations,
not accepted solely from their comments. Static owner retirement erases mutable
indexes and weak caches; existing shared publications remain separate owners.

### Calls through native helpers and hooked entrypoints

`python tools/inventory-native-reentry.py` scans all 169 `.cpp`/`.h` files
under `src/native_graphics`, masking comments and quoted strings. Its 251
sites comprise 189 explicit original/adapter/import calls, 53 calls through
hookable `sub_` entrypoints, seven indirect resolver invocations, and two
`ImportTexture` forwarding-parameter invocations. The earlier hook-body
ledger covers 186 of these sites. The three additional named import calls
are `KeSetEvent` at bridge line2856, `NtReadFile` at6942, and
`RtlFillMemoryUlong` at6955; they are platform calls, not newly discovered
unported draw functions.

The 53 hookable calls retain their target's native-boundary classification
in `native-reentry-sites.csv`. They include selection/group dispatch, material
shader/constant/texture setters, submission/fence polling, resource creation
and destruction, and state-retirement helpers. Calls at4714 are constant-upload
audit oracles; calls at4724 preserve aliased upload behavior. Their distinction
matters: calling `sub_...` can enter an existing native hook, while calling
`__imp__sub_...` explicitly bypasses it. Neither syntax alone proves complete
native ownership.

| Bridge line | Indirect target source | Observed purpose and unresolved scope |
|---:|---|---|
| 4243 | Object slot4 from `DispatchNativeFrameBuckets` | Renderable families mapped above; runtime population remains open. |
| 4245 | `DispatchNativeFrame` phase target | World/view/backend/listener callbacks and direct phases; core callback ledger applies. |
| 4746 | Material operation's setter | Only accepted offset0xC8 reaches this ordinary callback: setter `82137978`; bounded population verified below. |
| 4760 | Same material setter | Sampled state-write audit oracle; native writes are computed and compared. |
| 5722 | Submission helper target or target-vtable byte slot | Observer callbacks can mutate/reset device state; snapshot validation follows each callback. |
| 6082 | Device+13068 | Fence accounting callback from `EndNativeFenceRecord`; invoked only when non-null and the record kind is nonzero. |
| 6856 | Certification/debug monitor callback | `native_pix_monitor.h` selects certification monitor word0 or debug monitor+24; capture states11..17 return to the original path. |

This is an explicit remaining callback worklist, not seven missing functions:
each resolver can select multiple targets and can reach hooked native entries.
The census does not expand macros or prove arbitrary function-pointer aliases
absent. Macro-generated original calls remain in their separate 87-site ledger.
The source hashes and exact lines are saved in
`native-reentry-source-manifest.csv`, `native-reentry-sites.csv`, and
`native-reentry-summary.json`.

Correction to the earlier signal classification: bridge line5477 is a fallback
only with native host disabled. The preceding guard at5458 rejects any
native-host callback other than `8214EBA0` by throwing; the recognized callback
uses the extracted CPU tail. `classify-renderer-dependencies.py` now records
that full control flow. The pinned bridge hash is unchanged, so this corrects
the review rather than describing a source-code change.

#### Material-state resolver population

`inventory-material-setters.py` exports the complete accepted offset-to-setter
map from `native_material_render_state.h`: **36 offsets**, of which **35** have
CPU mirror cases. The sole no-mirror offset is **0xC8**, mapped to scissor setter
`82137978`. The script checks that the mirror switch covers exactly the other
35 accepted offsets and records the source hash. This verifies route coverage,
not numerical equivalence of every mirror to the guest implementation.

The activation builder at bridge4663..4670 reads `{offset,value}` pairs from
instance+96's descriptor, rejects counts above4096, obtains each expected
setter from that fixed map, and requires device-table pointer
`device+56+offset` to equal it. An unknown offset or changed pointer throws
before dispatch. Therefore line4746's ordinary callback target is restricted
to `82137978` on this accepted path. The sampled audit resolver at4760 can
select the other 35 mapped setters; those calls have a separate oracle role.

The scissor hook is an `EDF_RENDER_STATE_SETTER` expansion: it calls the
original setter and then publishes completed native render state. Raw PPC
`82137978..82137984` stores the enable value at device+11584 and tail-transfers
to `821370E0` with the rectangle at device+12400. The latter reads the viewport
at device+0x3058..0x3064, copies the supplied rectangle to+0x3070..0x307C,
conditionally clips when enable is nonzero, preserves the high bits of packed
words+0x2844/+0x2848, writes packed coordinates, and calls `82134BD8`.
These instructions are in the state ledger and the two Ghidra bodies.

The remaining migration obligation at this resolver is consequently explicit:
publish scissor enable and rectangle with the enclosing viewport/pass, preserve
the packed CPU-state/dirty bookkeeping required by any retained consumers, and
remove the original scissor helper only when those consumers are accounted for.
It is not an arbitrary unknown material callback family.

Reproduce the map with `python tools/inventory-material-setters.py`. Outputs:
`out/renderer-inventory/material-setter-population.csv` and
`material-setter-summary.json`. Ghidra reproduction uses the existing state
batch command. No renderer implementation was changed for this investigation.

#### Submission observer and monitor registration paths

`SubmitNativeObservers` supplies three target forms to bridge5722: device+19956
object vtable byte offset24 (slot6), the same object's byte offset28 (slot7),
and the direct callback at device+20100. Special mode (device byte+10809 bit0x2)
uses only the object callbacks; ordinary mode may invoke both object and direct
callbacks. Their presence is optional and the helper rereads pointers between
notifications. Each native callback is followed by device-generation validation.

The construction path is now concrete: `821387E8` calls `82144E20`, stores its
non-null result at device+19956, and invokes slot10. `82144E20` obtains840 bytes
through certification-monitor allocation and installs table **82009C74**. Raw
image verification yields slot6 **82144868**, slot7 **8252B718** (empty return),
and slot10 **82144240**. The capture-idle path `82138858` also clears the
device+19956 field; arbitrary later pointer writes remain unexcluded.

`82144868` is a command-stream observer, not an empty accounting callback:
it scans packet headers, emits spans through `82144308`, invokes table slot3
for selected packets, and recursively invokes slot6 for indirect packets
0x3F/0x37 with adjusted addresses and a depth argument. It also invokes slot0,
slot8, `82141AB8`, and conditionally `8214EBD0`. This identifies a capture/monitor
subsystem boundary that needs an explicit supported-mode policy; native submit
ownership does not establish that this observer is independent of guest packet
data or monitor services.

The direct callback writer is debug-control handler **82138E00**, command
**0x22**, storing the supplied argument at device+20100 (`82138E68`). Its
registration path **82138FC0** passes `{2,82138E00}` to the selected monitor
with command0x2F, and also registers `82138B98` using command0x1C. Hence this
callback is externally supplied through a concrete monitor protocol; its target
set cannot be inferred from direct callers of `82138E00` (there are none in
the direct-call database).

The binary constants at **8200071C** and **82000800** contain **30003000** and
**30002000**, respectively. These locate runtime certification/debug monitor
pointers; they are not themselves callback function addresses. The native PIX
helper selects certification handler word0 or debug handler+24, matching the
registration path's selection. Runtime contents and host-provided targets are
still unverified.

For fence profiling, the immediate-offset search finds device+13068 loaded by
`82139508` at **82139574**, but finds no instruction with that immediate that
stores it. This is a scoped negative result: indexed, aliased, bulk initialization
and runtime-supplied writes are not excluded. It does not prove the callback
stays null. `observer-field-sites.csv` records all raw-verified immediate-offset
sites for13068/19956/20100; `observer-field-summary.json` records table slots and
monitor constants. Reproduce with `python tools/inventory-observer-fields.py`.

#### Local SDK monitor defaults and provenance limit

The application's `out/build/win-amd64-release/CMakeCache.txt` resolves ReXGlue
to `D:/roms2/edf3-translation-project/rexglue/sdk_ffx/win-amd64`. The adjacent
`rexglue-sdk/out/build/win-amd64/CMakeCache.txt` names that installation prefix
and the `rexglue-sdk` source tree; its `ffx_install.log` names the same destination.
This connects the inspected source tree to the configured install location,
but does not prove that current source bytes produced the installed library.

In `rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_module.cpp:44..108`, both
`kernel_debug_monitor` and `kernel_cert_monitor` default to false. Their disabled
branches allocate four-byte export variables and store zero. Enabled branches
allocate and zero monitor structures, with callback trampoline construction
commented out as an AOT/JIT migration TODO. The variables are exported as
`KeDebugMonitorData` ordinal0x59 and `KeCertMonitorData` ordinal0x266.
`src/kernel/CMakeLists.txt:36..37` also comments out the corresponding monitor
implementation files pending translation. No override of these flags was found
in the inspected application source/configuration files; command-line or external
configuration overrides are not excluded.

This supports a default-null expectation for that SDK source configuration.
It is not a runtime-null guarantee for bridge6856 or a reason to delete the
capture/monitor branches from the inventory. In particular, enabling a monitor
in this source does not establish a valid callback trampoline, and fence profile
field device+13068 remains a separate initialization question.

`python tools/inventory-sdk-monitor.py` records six provenance files, the exact
monitor-source excerpt, and hashes in `sdk-monitor-provenance.json`. Installed
`rexruntime.lib` SHA-256 is
`7f9eb12afe99a3a87337684210d93279edf66233ffd6ccb2877b73d12dfbbe95`;
the inspected module source SHA-256 is
`6611458122bd116662fa5e42c43f8337eb582c689a491e48802889da06b266ea`.

## Retained callee contracts and adapter closure

`python tools/inventory-retained-contracts.py` groups all 186 explicit hook
calls into 171 distinct callee symbols: 145 guest originals, 24 extracted
adapters and two platform imports. `retained-callee-contracts.csv` preserves
every source call site, path classification and work batch. Guest rows attach
Ghidra definition coverage and direct/indirect site counts. Adapter rows follow
calls through other extracted adapters, stopping at guest entries, native
helpers and indirect calls. All artifacts are under `out/renderer-inventory/`.

`retained-adapter-terminal-edges.csv` has 75 root-relative boundary rows at 65
unique source sites: 43 hookable guest-entry rows, 25 native-helper rows and
four indirect rows and three platform imports. Shared callees repeat across roots. These are conservative
syntactic paths, not evidence that every branch runs. Plain `sub_*` calls can
enter native hooks; they must not be counted as explicit original fallbacks.
The four indirect rows belong to counter reset (three) and worker audit (one).
The three platform import sites are `ExCreateThread` and
`KeSetBasePriorityThread` in worker initialization, and `KeUnlockL2` in worker
command handling. The original scanner only selected `sub_*`/`edf_native_*`
names, so it omitted these imports. It now captures every named `__imp__`
call, and the audit independently checks the source locations against all
regenerated adapter files. The earlier count of 107 calls is superseded by
110; retained closure boundary counts similarly rise from 72 to 75. This
correction does not change the 186 top-level hook-call sites.

Other adapter definitions, such as ring submission, exist in the extraction
inventory but are not necessarily reached from these 24 retained symbols.

The indexed CPU tail illustrates why a closed call graph is insufficient:
its extracted closure has six adapters, 43 scalar store sites and no terminal
calls in the selected guest/native/indirect call families. Its shader-cache
helper nevertheless retains an atomic update and global-lock operations.
In `native_shader_cache_cpu_tail.cpp:113`, the emitted `lis -32168` and
`addi -29428` form address `0x82578D0C`; the compare-and-swap at line 143
increments that global when the shader record's `+48` value is zero, retrying
and skipping the excluded wraparound results. It then writes the assigned
value to shader record `+48`, cache entry `+40`, cached words `+48/+56`,
progress value `+64`, and device `+11552/+11560`. Moving this work requires
preserving identity allocation, cache invalidation and synchronization, even
though the extracted shader-patching call was removed. These are observations
of the generated adapter, not a proof of all native consumers' requirements.

`adapter-nonscalar-operations.csv` adds seven explicit compare-and-swap sites
and their lock operations across the extracted definitions. This supplements
the earlier scalar-store ledger; it is a scoped syntax detector, not universal
write coverage. Callee effect ownership remains pending in the contract ledger.

### Retained adapter callback provenance

The contracts Ghidra batch decompiled twenty-six entries; 3,692 instructions were
independently checked against the guest image. `retained-callback-review.csv`
maps all four indirect sites in the retained adapter closure to their target
expressions, activation conditions and outstanding population questions.
`retained-adapter-terminal-edges.csv` now includes these expressions and guards.

| Original call site | Target provenance | Local activation and remaining boundary |
|---|---|---|
| `82139270` | Debug monitor through `Word(Word(82000800))`, interface at monitor `+32`, callback at interface `+16` | Counter flags at device `+15144` contain `0x400`. A null monitor sets the interface temporary to zero but still loads address `16`; null default alone does not prove this path safe or unreachable. |
| `8213941C` | Same monitor/interface, callback at interface `+12` | Flag `0x400` clear and `0x100` set. Arguments are `820097D8` and `(flags & 255) << 20`. The callback return updates device `+15148`; success changes profiling flags. |
| `821394CC` | Certification monitor through `Word(Word(8200071C))`, callback at monitor `+4` | Both monitor and callback must be nonzero; callback argument is zero. Default-null SDK evidence is conditional on configuration and does not enumerate external implementations. |
| `8214E9D4` | `worker = Word(entry_r3)`, callback at worker `+16` | Ordinary job command (high bit clear), with arguments entry handle, worker `+20`, job data at `+32`, index at `+24`. The native wrapper retains dispatch; null logging neither throws nor skips it. There is no target allowlist here. |

All four original sites contain raw instruction `4E800421` (`bctrl`). The
native signal callback guard accepting only `8214EBA0` belongs to a different
path and does not close the worker job callback population. The latter still
needs command-producer and target-population provenance for worker `+16`.

The worker registration mechanism is now identified: command `0x81000000`
copies `Word(command+4)` to worker `+16` at `8214EA0C`, and
`Word(command+8)` to worker `+20` at `8214EA18`, then advances the command
cursor by 12 bytes. Ghidra and raw instructions agree. The decoder supplies
no target restriction. An aligned guest-image search found no such command
immediately followed by a known function entry; this only rules out that
specific static representation. It does not rule out runtime-built commands,
loaded resources, unknown entry points, or encoded addresses.

`821477A8` also establishes why direct stores cannot close the profiling flag
population. After obtaining the debug monitor interface, it publishes pointers
to device `+15144` and `+15148` in interface `+4/+8` when interface word zero
is greater than eight (`821478D8/DC`). With any nonnull interface it publishes
device `+20000/+20004` pointers at interface `+24/+28` (`821478E8/EC`). The
external monitor therefore receives aliases to flags, results, counters and
timing state. This proves the alias exposure; the external provider's actual
writes still depend on its implementation and configuration. Six raw-verified
producer/escape sites are recorded in `retained-callback-producers.csv`.

The immediate `10828` hit at `820BDECC` is a different case: its base was
formed by `lis -32256`, so the address is the constant `82002A4C`, not a
device-relative worker field. It must not be counted as worker registration.

Immediate-offset searches for `10828` and `15144` are recorded separately in
`contract-field-sites.csv`. They include candidate writes in `8249A8D0`, but
matching an offset does not establish the same receiver type. Those candidate
writes are not claimed as renderer-device producers. Bulk, indexed and aliased
writes remain outside this search's proof scope.

Ghidra reported the `8214EBA0` and `8214EBD0` decompilations as successful but also emitted a
bad-instruction/control-flow truncation warning at an imported tail. Their C bodies
are therefore not complete behavioral proofs; raw instructions and generated
code remain necessary. Reproduce the focused batch with:

```powershell
python tools/inventory-contract-callbacks.py
powershell -NoProfile -File tools/ghidra/inventory-renderer.ps1 -Script RendererRenderable.java -AnalysisSet contracts
python tools/inventory-static-pass-contracts.py
python tools/inventory-retained-contracts.py
python tools/integrate-renderer-findings.py
python tools/audit-renderer-inventory.py
```

The publication route is now connected to the worker decoder. `8214ECD8`
derives a command-list argument from device `+13000` and calls `8213C9F0`
with callback `8214EBA0` at `8214ED60`. The native signal path records that
argument and, upon completion, publishes it to device `+10900` before waking
the selected worker event (`guest_shader_bridge.cpp:2843–2856`). It refuses to
overwrite an occupied publication slot and verifies the global device identity.
Worker initialization makes the worker base device `+10812`, so `+10900` is
the same publication field as worker `+88`. The worker thread `8214EAD0`
waits, resets its event, checks the worker record and calls `8214E8E0`.

An additional publication entry, `8214EBD0`, writes the same field and wakes
the event selected by device `+11192`; the submission-observer implementation
`82144868` has a conditional call to it. Neither publication entry validates
the callbacks embedded in the list. Signal-target validation therefore closes
the wakeup operation's target, not the command stream's job targets.

`8214EC00` supplies one concrete producer: it preserves device list metadata,
reserves 252 bytes in the list rooted at device `+13000`, writes opcode
`0x80000000`, two device-derived words, and copies 240 bytes from device
`+12476`. This is a state command, not the `0x81000000` callback-install
command; the latter's producer population is still open.

The grouped contract ledger now attaches scoped local effects for indexed
draw, counter reset and the worker adapter, and the central findings index
joins those effects to their native callers. Worker-local writes cover
`+0/+16/+20/+24/+28/+32/+36/+48/+52/+80/+84/+88/+108`, including the atomic
lock and command cursor/nesting state. These entries are explicitly partial:
the callback and command-helper effects cannot be inferred from local stores.

### Static group entry contracts

`static-pass-contracts.csv` distinguishes the original entry-owned instructions
from Ghidra's expanded tail-call bodies. Twelve entries have raw-checked local
effects and native routing attached to the central findings index:

| Entry | Direct effects and retained route |
|---|---|
| `821D96D8` (81 instructions) | Group `+4/+8` delimit the instance chain and `+12` supplies geometry/material inputs. After a nonempty traversal, `821D9810` clears group `+4`. Geometry setters, material activation, per-instance upload and indexed draw have separate effects. |
| `821B94E8` (4 instructions) | Loads device into `r4` through global `8257BFB4` and tail-branches to `821B8E48` at `821B94F4`. The generated call is hookable. Ghidra expands the material body here, but this is not a separate unported material implementation. |
| `821B8E48` (247 instructions) | Four explicit non-stack stores update sampler words at device `+1036+24*slot` and OR device dirty word `+16`. Its native hook selects native activation when shader bridge and material activation are enabled; aliased uploads, texture retirement and scissor behavior retain their own contracts. |
| `821D9600` (53 instructions) | Traverses 12-byte constant records between instance `+16/+20` and calls `82149248`. Its only direct store is stack setup, but its upload calls have transitive effects. The native hook still forwards the original before synchronizing the instance. |
| `82149248` (67 instructions) | Copies 16-byte constant records into the register bank beginning at device `+16*(start+112)`, then ORs the supplied mask into device's 64-bit dirty word at `+0`, including on the zero-count path. Five raw vector-store sites implement bulk/tail copies. |
| `82137410` (73 instructions) | Native stream setter retains fetch descriptor/address words at `+1788-8*stream` / `+1784-8*stream`, dirty word `+16`, resource binding `+12188+4*stream`, stride byte `+12256+stream`, and previous-buffer retirement before publishing the native binding. |
| `821375C0` (36 instructions) | Retires the previous index resource and updates device `+12164` before native binding publication. Retirement is performed even when the replacement address equals the old address. |
| `82149A90` (7 instructions) | Writes declaration at device `+11536` and ORs bit 51 into dirty word `+16`. The native hook reproduces these effects and publishes the native declaration. |
| `82141440` | Reserves linked retirement-record blocks and updates current block/cursor/end. It does not consume the records or release the referenced resources. |
| `82141AB8` | Cache-range helper; the native bridge path preserves CPU scratch/register outputs without the original cache-line loop. It is not a geometry-write notification or a retirement consumer. |
| `8213C328` | Converts words to requested bytes, selects ring/metadata allocation, updates accounting and marks failure. The native immediate-draw guard does not suppress retirement allocator calls. |
| `821415A8` | Raises the request to metadata `+168` minimum and dispatches allocator callback `+172` with context `+164` under the global device's `+13552` critical section. Callback population remains open. |

Stream/index retirement shares `RetireNativeBoundResource` in
`native_index_binding.h`: a nonzero device fence at `+10780` is stored to
previous resource `+8`; otherwise a matching mask at device `+10784` causes
an eight-byte deferred record to be appended through cursor `+13148` / end
`+13152`. The record contains `(previous >> 2)` plus the legacy tag's high bit,
and low word `FFFFFFFF`. Queue growth still calls `82141440`; stream and index
hooks preserve the original caller tag locations (`SP-64` and `SP-48`). This
is an explicit resource-lifetime obligation, distinct from native buffer
binding, and its queue consumers still require ownership review.

Allocator `82141440` calls `8213C328(device,502,128)`. On its normal path it
stores the new block at device `+13144`, clears its link word and header bit
31, and publishes an encoded block address either through owner
`Word(device+13140)+112` or previous block `+0`. It writes the previous block's
used-record count as `(old_cursor-old_block-8)>>3`, preserving header bit 31.
It then calls the hooked cache-range helper and sets cursor/end to block
`+8/+2008`: capacity for 250 eight-byte records. If device byte `+10809` has
bit `0x40`, it instead uses the scratch pointer at `+15152` without linking
the new allocation through that normal path. These branch distinctions must
remain visible when replacing the queue.

The focused scan also inspected `82141340` (allocation bounds) and
`8213FAF8` (another record producer). Neither is claimed as the retirement
consumer. Matching the cursor fields or following the cache call is not enough
to establish resource release or GPU completion.

The allocator call chain also has an explicit dynamic boundary. `8213C328`
selects `8213C120` when device `+13140` is null, `82141340` when the metadata
owner's `+152` is nonzero, and `821415A8` otherwise. The last route calls
`Word(owner+172)` at raw `bctrl` site `82141608`, passing `Word(owner+164)`,
flags, a mutable size pointer and alignment. Its minimum request is owner
`+168`. This callback's registration and implementation remain unclosed.
The native `OwnsImmediateAllocation` guard requires caller `821FD6E8` and
alignment 16; retirement allocation returns to `82141460` with alignment 128,
so that native suppression does not remove this path. This finding adds a
concrete allocation callback obligation; it does not identify the retirement
record consumer.

`inventory-retirement-provenance.py` records the bounded registration search
in `retirement-owner-provenance.csv`: eight immediate-`13140` references
across the instruction database, none a scalar store at that displacement.
Within `82130000..82152000`, scalar stores starting at offsets `160..172`
yield one apparent `+164` candidate, `82132290`. Raw instruction `3BE1FEC0`
at `82131A50` establishes its `r31` base as a stack frame, and the record
at frame `+144` is passed to `RtlRaiseException` at `82132294`. That hit is
excluded from allocator registration; testing only for base register `r1`
would have missed the stack alias. No `+168/+172` registration is established
by this search. Bulk, indexed, vector, aliased and external initialization are
not excluded, so neither the allocator callback nor its owner is declared
unreachable. These results are recorded as search limitations, not completion.

The constant uploader's Ghidra output is truncated at VMX instructions despite
an `ok` decompilation status. The raw store words at `821492E8/F0/F8`,
`82149300` and `82149330` have primary opcode 31 and extended opcode 231;
the generated implementation labels them `stvx` and stores whole vectors.
The external instruction database labels these words `stvewx`, so its mnemonic
column is not accepted as proof of store width. The inventory verifies the
raw words and generated operations. Source/destination overlap and copy order
remain relevant; replacing this with an arbitrary bulk copy is not established
by this analysis.

This resolves a potential double-count in the migration worklist: references
to `821B94E8` reach the existing material hook. It does not prove that material
compatibility replay can be removed; required device state and resource
retirement effects must still be assigned to the producer or native consumer.

## Inventory integration audit

`python tools/audit-renderer-inventory.py` checks current source hashes, exported
row counts, Ghidra definition/body presence, focused method inclusion, constructor
argument records, and the material setter population. The first audit found
33 of the 47 subsequently discovered render methods missing from the earlier
broad census. Those methods had focused decompilations but their direct callees
had not been integrated into the central graph.

`complete-renderer-inventory.py` now adds explicitly investigated class tables,
constructors, registration/state entries, and named native reentry targets.
Shared stubs do not recursively select every unrelated class table. The rebuilt
read-only Ghidra run completed with9,227 definitions covering all9,207 emitted
functions; all3,330 indirect instruction locators verify, and all47 focused
render methods are present. The report's census numbers above reflect this
rebuild, not the earlier8,515-function graph.

After discovering new roots, rerun the broad builder, Ghidra inventory, locator
resolver, summary, dependency classifier, registration enrichment, renderable
review and native reentry export, in that order. Registration enrichment must
follow the summary script because it adds the traced factory paths. Then run
`python tools/inventory-renderable-routes.py`,
`python tools/inventory-renderable-helper-routes.py`,
`python tools/inventory-helper-callbacks.py`,
`python tools/inventory-state-save-offsets.py` and
`python tools/inventory-decompiler-quality.py`, then
`python tools/integrate-renderer-findings.py` and rerun the integration audit.
The current machine-readable audit is
`out/renderer-inventory/inventory-completion-audit.json`.

The structural audit passes; semantic completion remains false. The joined
`out/renderer-inventory/renderer-function-findings.csv` contains all 9,216
functions, with focused findings attached to 750 and 8,466 retained as census
only. It joins all 47 render methods, 84 constructors, callback candidates,
native hook path reviews, material setters and focused instruction evidence.
Each row keeps evidence, outstanding contracts and implementation status
separate. The audit verifies exact census coverage, focused finding inclusion
and named native reentry target inclusion. The original broad CSV's initial
review field is superseded by this joined evidence index.

The 186 hook-call path reviews still mark callee effect contracts pending.
Neither those contracts nor the census-only rows count missing renderer
implementations: the broad closure includes shared gameplay and runtime code.
Renderer relevance and ownership must be resolved before claiming an exhaustive
semantic inventory. Merely exporting instructions is not a completed ownership
review.

The implementation and runtime acceptance gates below describe migration work
identified by the inventory. The user expanded the goal on 2026-09-22 to resume
renderer implementation once sufficient evidence supports a complete boundary.
Unresolved inventory items remain open while supported implementation proceeds.

### Saved decompiler output quality

The warning inventory covers 732 saved C outputs for 687 unique functions:
16 core, 43 registration, 131 renderable, 45 state, 26 contract, seven helper, six pass, 247 retained and 211 dispatch outputs.
These focused decompilations are distinct from the 9,236 function definitions.
Of the saved outputs, 339 contain warnings. The warning ledger records 316
inlined-helper notices, 30 decompiler restarts, 38 removed-block notices and
twenty-seven bad-instruction/truncation notices, plus four other warnings. Inlined-helper notices alone are not
evidence of broken control flow.

Thirteen outputs covering eleven unique functions explicitly report bad instructions and truncated control flow:
`8212F4B8`, `8212FC28`, `821340D0`, `8213BDF8`, `8213BE68`, `8213DF00`, `8213FAF8`, `821409A0`, `82149248`, `8214EBA0` and `8214EBD0`. Their successful export status
does not establish a complete body. Raw-instruction review remains necessary;
the constant uploader and worker import tails have scoped reviews above.
Warning-free output likewise does not establish correctness or native ownership.

`out/renderer-inventory/decompiler-quality.csv` records each output's SHA-256,
analysis set, warning categories and truncation flag. `decompiler-warnings.csv`
preserves each warning and exact file/line locator. These fields are joined
to the central function index without changing census-only rows into semantic
findings. The integration audit now passes 77 structural checks, including
warning-scan output coverage, current hashes and joined truncation flags.

### Retained content routes

`tools/inventory-renderable-routes.py` joins the 47 render methods to six
source-reviewed content boundaries. The pose (`821C9478`), mesh (`821B2C28`)
and overlay (`820D3FD0`) hooks are `EDF_RENDER_PHASE` timing wrappers that
unconditionally forward to their original functions. The model hook
`821C9C20` forwards immediately when interpolation is disabled or its matrix
range is invalid; its interpolation path installs a temporary pose context
and then also calls the original model function. This review describes normal
rendering paths, not exceptions as successful completion.

Upload hooks `821A1738` and `821A17D8` likewise execute their originals first.
When the model context matches the source and destination is valid, they
overwrite shader scratch with respectively 12 words per matrix or a transposed
16-word matrix. That supplies interpolated inputs while retaining original
model/mesh execution; it is not a complete native content pass.

Verified direct branch instructions connect ten methods to `821C9C20`:
sky, tree, mothership dummy, bomb, sentry gun, grenade, missile, broken object,
broken piece and shell case. Sky and broken object also directly call
`821C9478`, giving 12 branch sites across these ten methods. Class labels are
the existing external class-name evidence, not runtime population proof.
The other 37 methods are not declared independent: helpers and indirect
dispatch can still lead to these boundaries. No branch-to-CTR instruction
occurs in the 47 method bodies themselves; their callees remain relevant.

Artifacts `content-boundary-contracts.csv`, `renderable-content-routes.csv`
and `renderable-content-route-sites.csv` preserve bridge hashes, hook locators,
method classes and actual branch sites. The central findings index joins all
six contracts and ten direct routes. The audit checks source hashes, exact
47-method coverage, branch-site correspondence and contract integration.

The helper trace (`inventory-renderable-helper-routes.py`) expands direct
branches until the first recorded native boundary. It verifies 6,829 raw
instructions across 132 bodies and records 395 inter-function branch sites.
All 47 render methods are covered: 24 reach the model boundary `821C9C20`,
20 reach dynamic geometry/material boundaries, two reach static selection
`821BEE68`, and one is the verified empty return. These are graph paths,
not evidence that every branch executes in a particular mission.

The 20 dynamic routes reach `82135078`, `82135108`, `82149A90`, `821FD8F8`,
`821B8E48` and `821E9BA0`; 12 also reach `82135578`. The trace preserves
14 distinct first native boundaries in total. It stops even at timing-only
hooks, so it does not silently treat the code behind a hook as replaced.
Register save/restore entries are reconciled using generated-symbol and
Ghidra-definition evidence. All direct targets have either a database body,
a native boundary or a mapped symbol; mapped imports are explicit stops.

Eight unique unresolved CTR sites remain before those native boundaries:
`8219CEA4`, `8219CA6C`, `8219F800`, `821F03D0`, `821E9FDC`, `821EA010`,
`82130034` and `821EFB00`. Their 160 per-method witness rows reflect shared
helper reachability, not 160 different call instructions. The corresponding
20 methods also reach mapped platform imports. Neither this trace nor its
absence of unknown direct targets establishes runtime callback closure.

`renderable-helper-routes.csv` summarizes each method;
`renderable-helper-witnesses.csv` gives one path to each reached boundary or
frontier per method. `renderable-helper-instructions.csv` and
`renderable-helper-edges.csv` preserve raw words and call sites. The central
index joins all 47 summaries. The audit checks method coverage, branch-word
correspondence and joined findings; raw-image equality is asserted by the
trace generator itself.

### Shared helper callback contracts

A read-only Ghidra `helpers` batch successfully decompiled the seven functions
containing the eight shared CTR sites. `helper-callback-review.csv` records
local target expressions, argument flow, effects, output hashes and unresolved
registration obligations; all eight reviews are joined to the function index.

The earlier syntactic vtable labels at `8219CEA4` and `8219CA6C` require
correction. Both load a device pointer from owner+8 and add an offset before
loading a callback. The save path loads `Word(device+input_offset+524)`,
passes device in r3 and saves the returned value with the offset in an
eight-byte record. It appends a `0x7fffffff/count` marker. The restore path
pops that marker and dispatches `Word(device+saved_offset+56)` with device
and saved value, in reverse record order. These are device state dispatch
tables, not object-vtable slot131/slot14 proofs. Both mutate owner+44 count;
the save path can grow owner+40 capacity. Device callback initialization is
still open. This local review supersedes the generic pattern labels at those
two sites without claiming their complete target populations.

The other six sites have distinct local roles:

| Site | Observed dispatch and behavior | Still unresolved |
|---|---|---|
| `8219F800` | Optional object at `8257C00C`, vtable+4, arguments object/1/entry argument/variadic area. Null object skips dispatch. Reached on malformed restore marker; diagnostic purpose is inferred. | Object registration and all callers. |
| `82130034` | Allocator+1412 override, allocator/mutable pointer/size pointer arguments. Null override uses `NtAllocateVirtualMemory`; success continues allocation bookkeeping. | Override registration and allocation ownership. |
| `821E9FDC` | Target is result of `KeTlsGetValue(Word(825566F8))`, called with `Word(825566F4)` and no local target-null guard. Null returned data leads to a 196-byte allocation. | TLS publisher and callback identity. |
| `821EA010` | Global callback at `8257C590`, arguments `Word(825566F4)` and new allocation. Nonzero result enters runtime-record initialization. | Publisher and record lifetime. |
| `821F03D0` | Optional callback at `8257C5A8`; null returns0, otherwise normalizes callback result to boolean. | Callback identity and meaning of predicate. |
| `821EFB00` | Callback at `825892E0`; null calls `821F6318(2)` and traps22. | Handler registration and activation. |

These reviews distinguish state preservation and shared runtime machinery
from actual submission without assuming those paths are unreachable or safe
to remove. They do not resolve callback target populations. Reproduce the
focused export with `inventory-renderer.ps1 -Script RendererRenderable.java
-AnalysisSet helpers` before regenerating helper reviews and decompiler quality.

`inventory-state-save-offsets.py` now covers all ten direct calls to
`8219CE18` in the instruction database. Raw argument setup and image contents
establish four fixed arrays:

| Array | Offsets, in saved order (hex) | Direct callers |
|---|---|---:|
| `82015070` | `c8` | 3 |
| `820176F0` | `3c,48,4c,60,64,68,34,38,28,30,6c` | 1 |
| `82017720` | `48,4c` | 3 |
| `82017728` | `48,4c,30` | 3 |

The twelve distinct offsets all occur in the native material-state map.
This relates the restore operations to existing expected setters; it does not
prove device-table initialization or callback identity at runtime. Offset
`c8` retains the scissor callback; the other eleven have native material CPU
mirror cases. State save still requires getter semantics, and dynamic restore
does not automatically use the material activation mirror.

`state-save-callers.csv` retains every call site and argument instruction
locator; `state-save-argument-instructions.csv` verifies 151 instruction-window
rows against the raw image. `state-save-offsets.csv` records each raw array
word address, getter displacement (`offset+524`), setter displacement
(`offset+56`) and native expected setter. Indirect callers and later array
mutation remain outside this fixed direct-caller population.

### Device state dispatch initialization

`821470A8` initializes 97 scalar-state entries from 12-byte records at
`82552518`: getter, setter, default value. For offsets0 through384 in steps
of4, it installs the setter at device+offset+56 and getter at device+offset+524,
then invokes the setter with device and default value. Raw indexed stores
`821470EC`/`821470F4`, dispatch `82147104`, strides and loop bound are verified.
All twelve previously identified save offsets select setters matching the
native material-state map. This establishes installation, not absence of later
mutation or the identity of every live device passed to the save helper.

A read-only Ghidra `dispatch` batch decompiled the initializer and all twelve
selected getters. Eleven getters consist of device-word loads, optional masks
or shifts, and return. Getter `821353B8` (offset0x64) reads device+0x2884,
performs PPC `fmadds` using constants at `82009650`/`820008D4`, then `fctidz`,
stores conversion scratch at entrySP-16 and returns the low32 bits. It has no
device-state store, but floating conversion semantics must be preserved.

Artifacts from `inventory-device-state-table.py` are `device-state-table.csv`,
`device-state-initializer-instructions.csv`, `device-state-getters.csv` and
`device-state-getter-instructions.csv`. The initializer has155 raw-verified
instructions. `device-sampler-table.csv` also exports the subsequent20 records
at `825529A8`. The initializer installs their setters at device+444+offset and
getters at device+912+offset; it calls each setter with device, sampler index
and default value for26 indices. It repeats the same callback installation
for each index. Detailed sampler getter/setter effects remain a separate review.

**Census gap repaired:** the twelve selected getters were absent from the earlier
9,042-function census because they were discovered through initialization data
rather than direct branches. The root builder now includes both targets from
all97 scalar and20 sampler records, with exact table-record provenance.
The rebuilt census contains2,500 roots and9,207 emitted functions, all covered
by9,227 Ghidra definitions. The central index joins dispatch target provenance
and the twelve scoped getter effects. Two new audit checks require every
initialized scalar/sampler target to be in the census and joined index.
These coverage checks do not establish later table immutability or per-instance
runtime identity. Regenerate the device table exports before rebuilding roots.

### Sampler operation effects

The expanded `dispatch` Ghidra batch exports all20 sampler getters and20
setters. `inventory-sampler-contracts.py` verifies610 instructions and records
every pair's local effect, exact decompiler output/hash and store sites. There
are no outgoing linked calls in these40 bodies. This is a local effect census,
not a claim that every operation has an equivalent native replacement.

Packed sampler words live at device+24*slot+`400/404/40c/410/414` (hex).
Setters also OR the slot's dirty bit into device+16, with conditional dirty
updates for anisotropy and minimum/maximum LOD. Those conditional operations
still update their byte controls when the packed-word update is skipped.
Byte arrays at device+slot+`2d84`, `2d9e`, `2db8`, and `2dd2` retain anisotropy,
minimum LOD, maximum LOD and mip override inputs. Min/mag filter setters read
these controls and previous alias bits, so later state depends on operation
order; a canonical backend sampler key alone does not preserve all inputs.

The native `ApplyNativeMaterialSampler` program covers material texture/filter,
mip and bias operations and retains those controls. Its reader rejects slots
>=16, while the original initializer covers26 sampler indices. This is a
scope difference requiring classification of the remaining slots and callers,
not proof that ten active samplers are missing. The full20-setter population
is broader than the material program. Floating-point setters/getters also
require PPC conversion semantics, particularly bias and the packed four-bit
field at dispatch offset0x3c.

Artifacts `sampler-contracts.csv`, `sampler-instructions.csv` and
`sampler-contract-summary.json` are joined to the central index. Three audit
checks enforce exact table-target coverage, current output hashes and joined
contracts. Run `inventory-sampler-contracts.py` after regenerating the dispatch
Ghidra batch and before integrating findings.

### Complete scalar dispatch body classification

The `dispatch` batch now includes every getter/setter target from the97 scalar
records, alongside the sampler targets and initializer:211 unique functions
successfully decompiled. `inventory-scalar-dispatch.py` verifies1,443 raw
instructions for170 unique scalar targets and exports301 store sites.

Of those170 functions,72 have no stores or outgoing branches in their bodies;
95 contain stores requiring destination review; one has an outgoing tail;
one returns zero; and one is an empty return. The zero-return getter
`821D5978` serves16 table entries. Empty setter `8252B718` serves10 entries.
These repeated stubs should not be counted as separate missing implementations.
No-store bodies can still perform conversions or affect machine condition
state; this classification is not a claim of complete purity.

The sole outgoing branch is scissor setter `82137978` at `82137984`, tailing
to `821370E0`. Its decompilation includes the tail's clip/scissor work, again
showing why raw body boundaries matter. That tail remains an explicit
transitive effect obligation and matches the earlier material scissor review.

`scalar-dispatch-review.csv`, `scalar-dispatch-instructions.csv`,
`scalar-dispatch-stores.csv` and `scalar-dispatch-outgoing.csv` preserve the
classification, table uses, output hashes and instruction locators. All170
rows are joined to the central index. Three audit checks require full scalar
target coverage, current output hashes and joined classifications. Regenerate
`inventory-scalar-dispatch.py` after the full dispatch Ghidra export. Destination
ownership and replacement value equivalence remain separate semantic work.

### Scalar store address provenance

`inventory-scalar-store-owners.py` resolves all301 scalar-body store addresses:
266 are relative to the entry device pointer,31 to the entry stack pointer,
and four to headers reached through device pointers. The bounded raw-register
review verifies that the entry bases are not overwritten before each store;
it also follows indexed conversion scratch back to entrySP-16. This closes
local address provenance, not object lifetime, caller identity or value parity.

The four external-header writers are setters `821360D8`, `82136188`,
`82136238` and `821362E8`, at dispatch offsets `134/138/13c/140` (hex).
They read resource pointers from device+`12168/12172/12176/12180`, respectively,
and may write the resource's word+28. The code always saves the requested
value at device+`11756/11760/11764/11768`. Header mutation is guarded by a
nonnull resource, a format nibble in `{2,3,10,12}`, and a requested value
different from the existing bit. It changes format bits16..19, updates the
corresponding device cached word at `10244/10252/10256/10260`, and sets a
device+24 dirty bit. These are resource mutations as well as cached device
state; removing the setters requires preserving the header consumers and
alias/lifetime rules. They are outside the36-offset native material setter map.

`scalar-store-owners.csv` retains each raw store and its address expression,
with supporting load/add instruction locators for indirect bases.
`scalar-store-function-summary.csv` joins those destinations to table uses.
Two new audit checks enforce exact store-site coverage and joined provenance.
Run this generator after `inventory-scalar-dispatch.py`. The scissor tail lies
outside these local bodies and retains its separate effect contract.

### Resource-header format transitions in pass setup and exit

The instruction database contains four direct calls to `821360D8`, and no
direct calls to the other three header-changing setters. This does not exclude
table dispatch: all four are installed and invoked by device initialization.
The four direct callers pass device=`Word(owner+8)` and values1,1,0,0:

| Caller / site | Local pass contract |
|---|---|
| `8219C258` / `8219C510` | Resource setup passes1 after attempting the color-surface allocation, before continuing depth allocation or failure handling. |
| `8219C5A8` / `8219C65C` | Binds owner+132/+136 color/depth, initializes shared clear-color words at `8257BFC0..CC` only when `8257BFD0` bit0 is clear, calls `821409A0`, then passes1. |
| `8219C678` / `8219C6C0` | Calls native-replaced `82140E98` then passes0. See the untiled boundary contract below. |
| `8219C930` / `8219C998` | Mode1 supplies owner+104 to `82140E98`; other modes supply0. Then passes0, writes owner+96 byte=1, binds owner+112 color and+120 depth. |

The native scene-end hook at `guest_shader_bridge.cpp:7417` performs native
resolve/presentation bookkeeping but still calls original `8219C930` at7449.
Thus the resource-header change, owner flag and guest surface rebinding remain
part of its retained execution. Existing native scene allocation uses an
explicit `R16G16B16A16_FLOAT` target; that alone does not establish equivalence
for all guest surface format mutations. Header consumers and pass boundaries
must be preserved when eliminating this original call.

A read-only `passes` Ghidra batch decompiled all four callers.
`inventory-pass-format-contracts.py` verifies327 raw instructions, including
the exact immediate value and device-load setup at each call. Its artifacts
are `pass-format-contracts.csv`, `pass-format-instructions.csv` and
`pass-format-summary.json`; all four contracts are joined to the central index.
Rebuild these after the `passes` export and before integration.

### Tiled entry/exit already replaced

Source review corrects a potential overcount in the pass worklist:
`821409A0` and `82140E98` are unconditional native replacements. Neither hook
calls its original function. Begin accepts only return address `8219C654`,
requires the device absent from `untiled_devices`, inserts it and returns0.
End accepts `8219C990` or `8219C6B8`, requires a matching device entry, erases
it and returns0. Wrong callers or unmatched begin/end pairs throw.

The whole instruction database has exactly the three corresponding direct
calls (`8219C650`, `8219C98C`, `8219C6B4`); their raw return addresses match
the source guards. This is direct-call coverage, not an indirect-call absence
proof. Native clear/resolve ownership belongs to enclosing hooks, and the
original enclosing scene-end function still performs the format transition,
owner flag and binding restoration described above.

The legacy end decompilation contains tile replay/command allocation and worker
completion behavior; those are bypassed at this native boundary and must not
be counted as currently retained effects through these calls. The legacy begin
decompilation additionally truncates at `82140D18`; the warning ledger now
records this fifth truncated output. A successful export remains distinct
from a complete legacy-body analysis.

`inventory-untiled-boundaries.py` pins both hook bodies to the bridge hash and
checks all three direct caller return addresses. Its contract/caller CSVs and
summary are joined to the central index. This establishes the native routing,
not full native/guest output parity or completion of the enclosing pass.

### Exhaustive current native-boundary routing

`inventory-native-boundary-routes.py` partitions all237 current graphics
boundaries and joins them to the function index. This is route coverage,
not a missing-port count:

| Route | Boundaries | Evidence scope |
|---|---:|---|
| Explicit original/adapter/import paths | 145 | The existing186 call-site reviews preserve branch conditions and retained callee obligations. |
| Macro original forwarding | 87 | Timing, tree invalidation or state publication surrounds original calls. |
| Native cursor update | 1 | `8213BD90` snapshots/publishes the cursor, mirrors device+10820 and returns the masked end. |
| Native submission with guest observers | 1 | `8213C410` copies descriptors; its helper dispatches guest observers, validates cursor generations, submits native frames and completion ranges. |
| Native untiled replacement | 2 | `821409A0`/`82140E98`, with the caller/pairing guards reviewed above. |
| Original passed to helper | 1 | `82201458` passes its original to `ImportTexture`, whose disabled-bridge and upload paths call it. |

The five hooks without an explicit original/adapter call in their own bodies
are therefore not five independent renderer replacements. Texture forwarding
is hidden behind a function argument, and descriptor submission still calls
guest observers via `ResolveIndirectFunction`. These source-level distinctions
are retained in `native-boundary-routes.csv`, alongside source hashes and
links to detailed call-site evidence. All237 routes are joined; new audit
checks enforce current census coverage, source hashes and integration.
Run the route generator after dependency classification and before integration.

### Retained-call effect review progress

`inventory-retained-contracts.py` now joins the completed original-body reviews
to both explicit and macro call sites. `retained-call-effect-reviews.csv`
preserves all186 source-path reviews and adds current callee-effect evidence:
165 sites across153 callees have partial local effects;21 need callee-effect
review. All24 extracted adapters in this explicit-call ledger now have some
local-effect evidence; none remains structure-only.
Ownership remains pending for the partially reviewed adapters as well.
`macro-call-effect-reviews.csv` preserves all87 forwarding calls;38 have
partial local effects from the accumulated original-body reviews. The remaining49 macro
calls are still awaiting corresponding effect contracts.

These statuses supersede the blanket pending field for assessing **local
review progress**; no call is declared safe to remove. The original pending
field remains because transitive ownership, lifetime and runtime obligations
are not closed. Native hook replacement effects are deliberately not joined
as effects of explicit `__imp__` calls, which select the original function.
All partial reviews retain their source ledger locators. New audit checks
verify exact explicit/macro call-site coverage and evidence presence, and
macro effects are joined to the central function index.

### Small retained-body contracts

The `retained` Ghidra batch adds twelve small original bodies. Their79 raw
instructions expose nine outgoing branch sites and four locally store-bearing
functions. Local review is now attached to thirteen additional explicit call
sites, with tail effects kept separate:

- `821C0B88` stores f1 as float32 at object+76 and returns.
- `821A1678` unlinks a two-pointer node, patches nonnull neighbors and clears
  the node's pointers; it does not allocate or free in its body.
- `821394D8` fills a six-word timing/state record from arguments, device state,
  a thread-relative pointer and the low timebase.
- `821BEB38` computes a64-bit scheduling quotient from globals `8257C300/308`
  and the sign-extended divisor at object+4, then stores object+16. Raw/generated
  trap and unsigned division semantics are retained; database opcode names
  alone were incomplete.
- `8212F4B8` selects release tails from r4 bit31; `8212FC28` tails the generated
  `MmFreePhysicalMemory` import with r3=0 and the original pointer in r4.
- `821349B8` and `82134AD8` prepare a value from object+24, set r5=0 and tail
  `82134640`; the first masks off its low two bits.
- `8213B5A0` shifts arguments and tails `8213AE38`.
- `821A17D8` guards a null vector then tails `821C8000` with destination/source.
- `82137F98` is a single tail branch to hookable `82137988`.
- `821BEF10` checks container invariants with traps, selects a nested object,
  then tails `821B2B00`.

Ghidra folds several tail bodies into these outputs, and two release-wrapper
outputs truncate at the imported target. `small-retained-contracts.csv`,
`small-retained-instructions.csv` and `small-retained-branches.csv` preserve
the actual body boundaries. Neither a tiny wrapper nor a no-local-store result
establishes that its transitive effects can be removed. Regenerate these with
`inventory-small-retained.py` after the `retained` export, before refreshing
the retained-call ledger and integrated findings.

### Binding and matrix tails

The first expansion added two reviewed tails (14 functions,
316 raw-image-verified instructions, 12 outgoing branch sites and six locally
store-bearing bodies). `821C8000` has exactly sixteen alternating `lfs`/`stfs`
pairs and a return: destination element i reads source element
`(i % 4) * 4 + i / 4` using integer division. This is a sequential float matrix
transpose; overlapping buffers and PPC floating semantics must be preserved.
It supplies the local copy effect behind wrapper `821A17D8`.

`82137988`, reached through wrapper `82137F98`, writes color-target bindings,
cached resource headers, requested/effective color masks and dirty state. Its
decompiled body can also rewrite the bound resource's format nibble at +28;
binding is therefore more than pointer publication. Slot zero recomputes a
recording-state flag and calls `821378E0` at `82137AF8` with the color target
or fallback depth target. That helper's local effects are reviewed below.
The raw outgoing branches contain the save/restore helpers and `821378E0`;
there is no explicit resource-retirement call in this body. Detailed color
binding value equivalence remains open. These effects and their decompilation
hashes are joined through `small-retained-contracts.csv`; all 77 structural
checks still pass, without establishing semantic completion.

### Scene, scheduling and viewport contracts

The scene/scheduling expansion covered 26 bodies, 691 raw-image-verified instructions,
35 direct outgoing branch sites and two CTR call sites. Sixteen bodies contain
store instructions (including stack stores). The additional contracts are:

- `820B2510`: null input tails a diagnostic helper; nonnull input atomically
  decrements global `82578650`, preserving interrupt state around the retry,
  then tails `821E8CE8`. Expanded release effects are not local effects.
- `820B2870`: installs vtable `82002158`, calls `821BFFE0` on three embedded
  objects at +496, +452 and +408, then calls `820B3728` on the owner.
- `821A1628`: unlinks a node from its previous neighbors and inserts it at
  the supplied head. Its ordered neighbor/head writes need preservation.
- `820B4310`: calls selection `820B4038(owner,owner+372,arg)`, then
  `821C61D8(owner,arg)`, then `821C3BB8(owner+240)`. Local stores are stack-only.
- `821C3BB8`: traverses the sentinel list, with invariant traps before and
  after `821D96D8(Word(node+8))`. The traversal itself has only stack stores;
  group execution remains a transitive effect.
- `821BEE68`: walks 28-byte records and writes each into the associated
  object's link at +4, preserving the former link in record+4. This is a
  mutating list pass, with range traps and live end-bound checks.
- `821A4170` / `821A41E8`: inspect global owner `Word(8257C030)`, conditionally
  call `821D5800(owner+148)`, compare a thread-relative value to owner+2288,
  and may invoke receiver `Word(owner+132)` at vtable slot +32. Exact CTR
  sites are `821A41CC` and `821A4248`; r3 is the receiver. Afterwards the
  first writes argument state at entry-owner+2264 and sets byte+2262; the
  second clears byte+2262. Receiver implementations remain unresolved.
- `821D5800`: a three-instruction wrapper taking Word(input+8), setting r4
  to -1 and tailing `8212FB48`. The wait/import behavior expanded by Ghidra
  belongs to that tail. `821F9FC8` simply returns
  `Word(Word(r13+256)+332)`, with no calls or stores.
- `821378E0`: reads resource+24, writes device+10240 and a selected value at
  +10556, dirties device+24/+32, copies globals `820095F4..82009600` to
  device+12400..12412, then tails `821371D0(device,820095DC)` at `82137974`.
- `821371D0`: returns without device writes if color0 and depth are both
  absent. Otherwise it chooses recorded or surface dimensions, clamps the
  unsigned requested extents, writes viewport/depth state at +12376..12396,
  calls scissor setter `821370E0`, writes six float transforms at
  +10376..10396 and ORs dirty bits 0xfc into device+24. Depth endpoints use
  float loads/stores, despite misleading unsigned casts in the decompilation.
  The native hook at `guest_shader_bridge.cpp:7190` conditionally replaces
  this body for native scene viewports and otherwise calls the original.

`small-retained-indirect.csv` preserves the two raw CTR instructions alongside
the direct-branch and instruction ledgers. These findings increase local
retained-call coverage at that stage to 38/186 explicit sites; 127 needed callee-effect
review and 21 have adapter structure but unresolved ownership. Native value
equivalence, transitive effects and receiver populations remain separate gates.

### Scissor packets, overflow and wait chain

Six more local contracts expand the retained batch to 32 bodies and 1,017
raw-image-verified instructions, with 51 outgoing direct branches and two CTR
sites. Twenty-one bodies have store instructions, including stack writes.

`821370E0` saves the four requested scissor words at device+12400..12412.
It starts from the viewport rectangle and, if device+11584 is nonzero,
intersects it with the request using signed comparisons. It writes packed
15-bit corners at +10308/+10312 while preserving mask `0x80008000`, then calls
`82134BD8(device,left,top,right,bottom)` at `82137188`. The local body does
not store a dirty bit; its command-packet effects occur in the callee.

`82134BD8` writes a four-word scissor packet on its simple path. The recorded
target path instead intersects each 16-byte region at device+12476 with the
input, incorporates corresponding offsets at +12716, emits seven words per
region, optionally emits an override packet, and restores recorded packet
state. Its packed packet coordinates use 14 bits, distinct from the cached
15-bit corners. It writes the cursor at device+40, checks it against +48,
and invokes `8213CF60` on overflow. Region count and flags are read live.

`8213CF60` conditionally collects pending data through `8213C5F0`, allocates
through `8213C328`, and forwards nonempty data through `8213C868`. It always
calls `8213CDC0`. Under its flag/global guards it calls `8213C928` and then
sets device byte+10809 bit2. It returns device+40. The native hook at
`guest_shader_bridge.cpp:6214` uses a native flush when `edf_native_host` is
enabled, but still invokes collection, submission and wait boundaries;
the original body remains its disabled-mode fallback.

The scheduling wait chain is `821D5800 -> 8212FB48 -> 82132ED0`.
`8212FB48` merely sets r5=0. Timeout helper `8212FB28` returns null for low32
timeout -1; otherwise it writes `-10000 * uint32(timeout)` as a 64-bit value
and returns the buffer. `82132ED0` passes the handle, literal1, low byte of
entry r5 and timeout pointer to `8252CFBC`, mapped in generated registration
to `NtWaitForSingleObjectEx`. It retries status `0x101` only when entry r5 is
nonzero; negative signed status calls `8212F328` and returns -1. The scheduling
wrapper therefore takes the null-timeout, r5=0 route. Platform wait semantics
and the error helper remain separate obligations; Ghidra's expanded wrapper
output is not counted as additional local instructions.

That expansion brought the joined ledger to 39 explicit call sites with partial local effects,
126 awaiting callee-effect review and 21 awaiting adapter ownership review.
All 77 structural checks pass; native equivalence and complete callback
population coverage remain unproven.

### Collection, submission and progress wait

The collection/submission expansion covered 36 functions, 1,320 raw-image-verified
instructions, 74 outgoing direct branches and two CTR sites. Four additional
original-body reviews attach to the explicit fallback-call ledger:

- `8213C5F0` allocates eleven words for each enabled cache source. It checks
  the exact 64-bit sentinel `0x00000000ffffffff` at device+11544 and byte+10810
  bit2, consumes the first range through `8213BE68`, and uses +13484/+13488
  for the second. It fills packet memory and output address/count pointers.
  Zero work or allocation failure writes count zero but leaves output address
  untouched. Range consumption and allocator effects remain separate.
- `8213C868` chooses a queued or immediate submission route. The queued
  path acquires the spinlock at device+10872, rechecks +10868, invokes
  `8213C018`, updates +10868 by the caller's increment and releases the lock.
  Otherwise it creates a stack `[count,address]` descriptor, updates +10868
  and invokes `8213C410(device,descriptor,1)`. Return-value and lock ordering
  differ between routes and must be retained.
- `8213CDC0` handles three descriptor paths selected by device flags and
  +12944: append `[address,count]` through +13160, append
  `[count|0x82000000,address]` through +13008, or finalize and submit through
  `8213C788` / `8213C868`. It then aligns the next cursor to 32 bytes, invokes
  `8213CC20` on overflow, or stores +13508/+40 and conditionally rounds
  +13496 to a page boundary. Overflow allocators and descriptor lifetimes
  remain transitive obligations.
- `8213C928` compares target/current/completed progress values, may flush if
  the target equals current, initializes a stack diagnostic record, polls
  through `82139688`, and finishes through `82139508`. Its own stores are
  stack-only; this does not make the wait or its diagnostic helpers removable.
  Raw carry-based loop comparison is preserved as evidence alongside the
  decompilation.

That expansion brought the explicit ledger to 43 partially reviewed call sites, 122 awaiting
callee-effect review and 21 awaiting adapter ownership review. The macro
ledger remains 37/87 partially reviewed. All 77 structural checks pass;
transitive ownership, callback populations and native equivalence remain open.

### Queue publication and wait diagnostics

Five additional contracts bring the retained batch to 41 functions and 1,563
raw-image-verified instructions, with 90 outgoing direct branches and three
CTR sites. Thirty bodies contain stores, including stack writes.

`8213C018` fills a four-word queue block, updates owner+4/+8, executes
`eieio`, flushes the new block through `82141AB8`, patches the previous cursor
to point to it, executes another `eieio`, and flushes the patched range.
The ordering and alias-address conversion are part of the effect contract.

`8213CB30` closes the old block link, requests 4,224 bytes through
`8213C328`, links and flushes a successful allocation, or chooses the device's
fallback buffer at +15152 and clears owner+0. It sets the owner block/cursor/
limit and returns block+4. `82141500` requests 72 bytes before checking the
fallback flag; otherwise it links the new descriptor block into the prior
block or owner+116, publishes the prior count, flushes, sets device+13160/
13164 and returns block+8. The normal branch has no explicit null-allocation
guard. These observations do not establish allocator failure guarantees.

`82139688` delays, tracks progress in record+8/+12, refreshes the clock under
its thread/owner condition, and returns 1 while unsigned elapsed time is below
5,000. It invokes `82145620` on timeout and returns zero; device byte+10809
bit2 also causes zero. The timeout handler remains a separate contract.

`82139508` adds elapsed low-timebase ticks to device+20032 for mode3 or
+20024 for other nonzero modes. It may call `Word(device+13068)` at exact
CTR site `821395B4`: r3=0, r4=mode, r5 preserves entry r5, r6 carries the
thread-clock difference, and f1 carries the scaled elapsed value. This is
a direct function-pointer field, not an assumed vtable slot. Its target
population and the role of incoming r5 remain unresolved. A database query
for instruction immediate 13068 found only the load at `82139574`; it found
no direct-offset writer. Aliases, bulk initialization and computed offsets
are not excluded by that negative result.

That expansion brought the explicit ledger to 45 partially reviewed call sites, 120 awaiting
callee-effect review and 21 awaiting adapter ownership review. All 77 structural
checks pass; the new callback remains explicitly unresolved.

### Timeout recovery and resource wrappers

The timeout/resource-wrapper expansion contained 47 bodies and 1,702 raw-image-verified
instructions, with 102 outgoing direct branches and three CTR sites.

`82145620` logs and invokes `82145468` using device fields +13064, +10772
and +20000. If the reloaded +13064 is nonzero, it sets byte+10809 bits0/1,
writes current-2 into the completion pointer from +10768, clears +10868
and calls `82138010`. That call uses live argument registers; the caller
does not restore r3 to the device. The null-handler branch logs and traps22.
The reset helper is reviewed below; diagnostic helper effects remain separate.

`82139638` calls `821395E0`, conditionally initializes its result through
`821E9BA0(pointer,0,originalArgument)`, and returns the saved pointer.
`82139760` decrements object+52 unless it equals one; in that case it calls
`82147A10`, releases the pointer at object-4 through `8212F4B8` and returns
zero. The local decrement is not atomic and has no explicit zero guard.

`8241E180` and `8241E1D0` call factories `8214AFD0` and `8214AED8`, write the
result through entry r5 unconditionally, and return zero or `0x8007000e`
according to the returned pointer. Factory effects remain transitive.
`8214B0A0` stores the input at device+12160, calls `82147BA0` with the internal
destination device+11920, stores live post-call r4 at +11536 and sets dirty
bit51 at +16. The raw caller does not reload r4; helper preservation was
checked in the following expansion.

That expansion brought the explicit ledger to 50 partially reviewed sites, 115 awaiting
callee-effect review and 21 awaiting adapter ownership review. The macro
ledger remains 37/87 partially reviewed. All 77 structural checks pass;
semantic completion remains unproven.

### Resource factories, declaration conversion and destruction

The factory/converter expansion contained 53 bodies and 2,285 raw-image-verified
instructions, with 137 outgoing direct branches and five CTR sites.

`82138010` reads no input arguments: it sets global `82578CF8` to15 and
returns zero when the global was zero, otherwise returns `0x80004005`.
This resolves the timeout caller's live-r3 question. `821395E0` allocates
size+alignment, computes `(allocation+alignment+3)&~(alignment-1)`, and
stores the original allocation at result-4; no local alignment validation
is present.

`8214AFD0` and `8214AED8` allocate an object plus metadata and a separate
payload, copy input data, and free the object if payload allocation fails.
The former zeros 872 bytes and copies metadata at +872, then invokes
`821497D0`; the latter initializes a 40-byte header and copies metadata at
+40. Their shared allocation size depends on input bit0 and metadata length.
Input invariants, helper writes and the paired resource lifetimes remain open.

`82147BA0` builds 12-byte declaration elements beginning at output+52 from
the input flags, appends a terminator and writes count/state fields. Its body
never assigns r4 and calls only GPR save/restore helpers for registers27 and
above. Thus `8214B0A0` stores device+11920 at +11536 as intended by its register
flow. Full format-table equivalence and the terminator's scratch-byte behavior
still need review; the local contract does not claim bit-for-bit native parity.

`82147A10` coordinates device destruction and releases, with flag-dependent
draining, buffer frees, platform calls and shared-table clears. Two CTR sites
`82147AB8` / `82147ADC` call through the global table reached from `82000800`,
slot+24, with arguments `(28,0)` and `(48,2)`. Their target populations remain
unresolved. The body also clears optional table fields and `Word(Word(82000720))`.
Its exact outgoing calls are retained in the branch ledger; destructor naming
alone is not evidence that all linked resources are owned or released.

That expansion brought the explicit ledger to 51 partially reviewed sites, 114 awaiting
callee-effect review and 21 awaiting adapter ownership review. All 77 structural
checks pass. Full renderer behavioral coverage remains incomplete.

### Lock ranges, atomic merge and drain

Six additional contracts bring the retained batch to 59 bodies and 2,462
raw-image-verified instructions, with 147 outgoing direct branches and five
CTR sites.

`82134958` and `82134A78` prepare the nine arguments for `82134408`. Zero
length selects the full resource span and resets offset to zero. The first
masks base/length, supplies type10 and a ninth argument `0x03000000`; the
second uses type12, sets flag2 and supplies a zero ninth argument. Their
local stores are stack-only; lock/header/wait effects are in the callee.

Ghidra truncates `8213BDF8`, but its 28 raw/generated instructions show an
atomic range merge: low32 becomes the unsigned minimum with entry r4 and
high32 the unsigned maximum with entry r5. It uses `ldarx`/`stdcx.` retries,
restores interrupt state, and retries the snapshot if another writer changed
it. This body is not empty. The inventory keeps both the truncation warning
and the instruction-based local contract.

`8213D1C8` emits two command words, publishes the cursor, waits through
`8213C928(device,current,4,0)`, then spins on device+10868 until zero.
There is no local timeout in that final spin. `821390B8` emits four words
including a converted address and `0xdeadbeef`, with the same cursor-overflow
helper. Packet-consumer meanings are not inferred from these constants.

`8213BC48` waits on the progress word at `Word(device+10768)+4`, using its
two low bits and aligned address. It initializes a mode2 diagnostic record,
polls `82139688`, and finishes through `82139508`, retaining the previously
documented timeout and callback effects.

That expansion brought the explicit ledger to 57 partially reviewed sites, 108 awaiting
callee-effect review and 21 awaiting adapter ownership review. All 77 structural
checks pass. Eight focused outputs have truncated decompilations; local raw
reviews do not establish full native equivalence or callback coverage.

### Shared lock body and native lock adapters

`82134408` selects the resource or parent fence, waits through `8213C928`,
then either emits a cache packet on the matching thread or accumulates the
range through `8213BDF8`. Its suffix conditionally merges packed 128-byte
dirty bounds at resource+20/+24, converts the returned alias when requested,
and always atomically adds `0x100` to resource+0. The dirty-bound update is
skipped when flags mask1 is set. This review adds 141 verified instructions;
the retained batch totals 60 bodies and 2,603 instructions.

The hash-gated extraction recipe was rerun successfully. The extracted
`edf_native_buffer_lock_cpu_tail` preserves the wait, off-thread range merge,
header bounds, alias conversion and atomic count. It removes original
`821344E8..8213456C`, the render-thread cache-packet block and cursor update.
Its suffix is wrapped by `edf_native_begin_lock_header_write` and
`edf_native_complete_lock_header_write`, which track 28 bytes of the header
after the retained services finish. The extracted `82134958` / `82134A78`
wrappers retain their arguments and substitute this adapter as the callee.

The hook selects the adapter for access10/12, or access14 from LR `8213AE24`,
when `edf_native_shader_bridge` is enabled; other cases retain the original.
The three adapter call sites now carry scoped effect reviews in the joined
ledger. This does not close ownership, writer concurrency or native parity.
Evidence is the extracted file, recipe and bridge source at lines6827/6964;
the existing extraction hashes still match.

That expansion brought the explicit ledger to 61 partially reviewed sites, 107 awaiting callee
effects and 18 with adapter structure but no local effect review. All 77
structural checks pass. Semantic completion remains unproven.

### Cache consumption and native drain adapters

`8213BE68` atomically exchanges the dirty-range word for
`0x00000000ffffffff`, then returns its previous low/high halves through two
output pointers. Its 21 raw/generated instructions preserve reservation
retry and interrupt-state restoration. Ghidra truncates this body, so the
local contract uses the instruction evidence. The retained batch now has
61 bodies and 2,624 verified instructions.

The extracted cache adapters share the original presence tests but differ:

- `edf_native_cache_range_cpu_tail` consumes the dirty range when present,
  always returns zero packet words, leaves the address output untouched,
  and performs no allocation or packet encoding.
- `edf_native_cache_reservation_cpu_tail` retains allocation and its failure
  branch. On success it consumes the range and publishes the converted
  allocation address, but returns zero packet words and encodes no packets.
  Zero work or failure leaves the address untouched and does not consume
  the range.

The native-host hook selects these only for return addresses `8213CF9C`
and `8214ED04`, respectively; other native-host callers throw. Disabled
native-host mode retains the original function.

`edf_native_device_drain` retains the fence wait and device+10868 loop while
removing packet reservation, encoding and cursor publication. Each loop
iteration polls native worker signals, checks queued/delivery work under
locks, throws for unsubmitted signals and sleeps while work remains.
Delivery callback ownership and progress guarantees remain open.

Both extraction recipes reran successfully with their source hash checks;
the regenerated files match the existing extraction manifest. The three
adapter call sites now have scoped effect reviews. The explicit ledger has
64 partially reviewed sites, 107 pending callee effects and 15 adapter sites
without local-effect review. All 77 structural checks pass; ownership and
complete behavioral coverage remain unproven.

### Device reset lifecycle

`8213D298` adds 255 raw-image-verified instructions to the retained batch
(62 bodies, 2,879 instructions). It conditionally drains prior work, flushes
and frees owned command/ring buffers and writebacks, clears cursor fields,
and returns success for null configuration. Otherwise it allocates or borrows
storage, allocates/zeros 96-byte and 32-byte writebacks, initializes device
and completion fields, configures platform ring state and emits startup
packets. Allocation failure returns `0x8007000e`; the local failure epilogue
does not unwind preceding allocations. Caller cleanup obligations remain open.

`edf_native_device_reset` preserves command storage, writeback allocation,
field initialization and error routes. It omits the ring allocation/borrowed
ring pointer, ring translation/exports and packet span `8213D560..8213D680`,
leaving the initialized command cursor empty. The native hook initializes
its submission cursor only after a successful reset with nonnull configuration.

The added completion-retirement helper runs after the conditional drain and
before storage is freed. Under submission/state/delivery locks it rejects
pending worker deliveries/signals and unsubmitted fences, retires completion
queues, clocks, published completions, cursor tracking and faults, and
conditionally clears gamma state and invalidates presentation. These are
observed checks and mutations, not proof that every concurrent lifetime is safe.
The extracted source, recipe and bridge helper are now joined as adapter
effect evidence; the existing extraction-hash checks still pass.

That expansion brought the explicit ledger to 66 partially reviewed sites, 106 pending callee
effects and 14 adapter sites without local-effect review. All 77 structural
checks pass. Full behavioral coverage remains incomplete.

### Unlock, gamma and worker-signal adapters

Five additional adapter call sites now have local-effect reviews.
The extracted unlock wrappers select resource+24 (with low-bit masking for
`821349B8`) and invoke an extracted `82134640`. It atomically subtracts
`0x100` from the header and consumes/reset dirty bounds only for the final
lock count. `NativeCacheFlushCpu` replaces the guest cache helper, preserving
scratch/register effects without geometry-write notification. The extraction
recipe reran successfully; header ownership and concurrency remain open.

The gamma table/PWL adapters only set r3 to6434/-1. Their native-host hooks
first decode 1,536 bytes, retain native gamma state under the mutex and reject
a device change without reset. The adapter bodies themselves perform no
guest-memory writes. Decode equivalence is a separate obligation.

The worker-signal adapter suppresses packet stores while retaining cursor
arithmetic and returning the end address. The hook accepts callback
`8214EBA0`, checks the span, and captures callback/argument/CPU-mask metadata
in the native queue under locks, including the pending-delivery capacity
check. It does not execute the callback there; unsupported native-host
callbacks throw. Completion and delivery ownership remain open.

That expansion brought the explicit ledger to 71 partially reviewed sites, 106 pending callee
effects and nine adapter sites without local-effect review. All 77 structural
checks pass. No new Ghidra output was needed for this extracted-adapter batch;
the existing 547 outputs and their warning flags remain unchanged.

### Worker initialization and command traversal adapters

Ghidra exports for `8214EE50` and `8214E640` add 273 raw-image-verified
instructions. The retained batch now contains 64 bodies and 3,152 instructions.

`8214EE50` initializes device/worker lists and records, iterates six selected
worker bits, creates threads at `8214EAD0` with 32 KiB stacks, and sets priority17.
It returns zero on thread-creation failure without local cleanup of earlier
threads. The native adapter retains these effects, substitutes zero for the
ring alias, and omits setup-packet reservation/stores/cursor publication.

`8214E640` traverses CPU command records. Its direct effects include a
248-byte worker-state copy, loop flags/counters, continuation pointers and
descriptor submission. Other opcodes invoke tiling/state helpers and
`KeUnlockL2`. The native command adapter retains that traversal and adds a
diagnostic call before each command read. The diagnostic records host-side
command history/visit counts when enabled; invalid cursors are logged before
the original read proceeds. This adapter is not an independent replacement
for the command execution path.

Both extraction recipes reran successfully and match the manifest hashes.
Their scoped effects brought the explicit call ledger to 73 sites that
have partial effects, 106 need callee-effect review and seven adapter sites
still lack local-effect review. All 77 structural checks pass; worker thread,
command-population and transitive-helper ownership remain open.

### Shader upload and output adapter review

Both hash-gated extraction recipes reran successfully and the retained-contract
manifest checks passed. This review concerns the extracted adapters; it does
not assign their reduced effects to the original guest functions.

`edf_native_shader_upload_cpu_tail` removes allocation/copy/packet emission,
the `8213E070` call and device+40 cursor publication. It still reads shader
metadata, sets device+10810 bit7 according to whether entry r4 is nonzero,
and copies 16 bytes from device+12256 to device+11552. Original E070 also reads
these stride fields without refreshing them; removing it does not itself
explain source freshness. Source ownership and equivalence remain open.
When entry r4 is nonzero it directly invokes the
output adapter using entry r6 as the output pointer.

`edf_native_shader_output_cpu_tail` removes calls to `8213E678`/`8213E748`
but retains descriptor traversal and reads. Early exits leave the output
untouched when metadata byte+8 has low three bits7, metadata word+20 has
mask0x40000, or the optional peer word+20 has mask0x20000. Otherwise it
replaces output bits20..23 with the low four bits of
`max((peer_word20 & 31)-1, 0)`, or zero without a peer. This output word is
the adapter's only direct non-stack store. Descriptor bounds and output
ownership still require review.

The public hooks select these adapters through `OwnsShaderUpload` and
`OwnsShaderOutputPatch`; failing those guards retains the originals.
That review brought the explicit ledger to 75 sites with partial local effects, 106 pending
callee-effect reviews and five adapters with structure-only reviews.
No additional Ghidra exports or renderer implementation changes were made.

### Device defaults, derived state and swap waiting

Three more adapter recipes reproduced successfully with matching extraction
hashes. Their effects now join the retained-call ledger, bringing it to
78 sites with partial local effects, 106 pending callee reviews and two
structure-only adapter reviews (immediate and main state).

* Device defaults (`8214EFF8` adapter) initialize 26 descriptor words at
  device+1024 with stride24 and 18 at+1648 with stride8, a 64-bit range
  sentinel at+11544, scalar defaults, and five all-ones dirty words at+0..32.
  The removed packet tail's interleaved CPU write at+10788 remains14 and
  the return register remains3650. The hook then publishes native render state.
  The exact scalar values are recorded in `retained-callee-contracts.csv`.
* Derived state (`8213D750` adapter) writes only device+10432 bits0/1 and
  conditional device+10809 mask4, apart from stack saves. Its inputs include
  bound-shader metadata and device state. It returns entry r4 OR0x100, has
  no remaining calls, and retains cursor/limit reads even though packet
  emission and rollover are removed. The public hook retains an original
  fallback outside its ownership guard.
* Swap wait (`821512D8` adapter) skips the wait for mode0x80000000 or zero
  masked device+13456. Otherwise it brackets the native wait with calls to
  `82139148(device,1)` and `(device,0)`. Normal exit sets device+10809 mask0x20.
  The native helper submits a frame, waits for GPU completion/credit and
  pacing, and updates device+15124/+15128/+15132/+15136. It rejects unsupported
  modes, uninitialized state and a nonnull callback at+15120. Exceptions can
  bypass the closing marker and final flag write; equivalence remains open.

A new Ghidra export of `82139148`, cross-checked against all55 raw-image
instructions, establishes that these bracket calls can still emit packets.
When device+20080 equals2 and the next profiling index differs from+20084,
the routine reserves through `8213CF60` if needed, writes four words
(`0xC0025800`, `0x80000003`, converted destination OR2, `0xDEADBEEF`), and
advances device+40 by16. Its destination comes from device+20076 and the
selected index/entry flag. It does not advance the profiling index locally.
The activation population and native ownership of that packet remain open;
the surrounding native wait does not prove this retained path is packet-free.

The retained Ghidra batch now contains65 bodies and3,207 verified instructions;
the full focused export set contains550 outputs for532 unique functions.
All77 structural checks pass. No renderer implementation or runtime behavior
was changed by this inventory work.

### Immediate/main-state adapters and original special state

Both remaining adapter recipes reproduced with matching hashes. The immediate
wrapper invokes its extracted prepare routine with entry r7 as stride. Prepare
temporarily stores `(stride >> 2) & 255` at device+12256 and sets dirty+16 bit51
when this differs from cached byte+11552. It processes and clears the five
64-bit dirty words at+0/+8/+16/+24/+32, conditionally stores
`0xFFFFFFFFFF000000` at+11568, and invokes the main-state adapter when dirty+16
bits49..52 require it. It restores the old stride and returns zero. Allocation,
vertex copy, cursor commit and packet-bank calls are absent from this adapter;
nested metadata/cache effects still apply. The native-host flag selects it;
otherwise the hook calls the original immediate routine.

The main-state adapter selects metadata through device+12420/+12416/+11536.
Its direct non-stack destinations are+10452 (low-three-bit mode),+10810
(flags3/6),+10408 (peer metadata),+12424 (effective mode), and+10400/+10404
(combined metadata). It returns the modified dirty mask rather than storing
device+16 locally. Its five conditional non-prologue call sites enter upload
(two), cache (two), and derived-state (one) adapters. It retains dirty-mask
changes from the removed packet spans. The public hook's ownership guard and
the draw adapters' direct calls are distinct entry routes.

New Ghidra exports for original `8213D750` and `8213D938` add258 raw-image-
verified instructions. D750 retains flag computation and returns input mask
OR0x100, but can also emit `0xC0004600,15` and advance the cursor after clearing
device+10809 mask4. D938 returns input mask with0x100 cleared; its local
non-stack writes are packet memory and device+40. It has a two-word simple
path and a recorded-region path with per-region packets, optional override,
restore packets and cursor-overflow calls. These original effects are recorded
separately from the adapters and native replacement.

The retained batch now has67 bodies and3,465 verified instructions. The
explicit ledger has82 partial local-effect reviews and104 pending reviews;
all24 explicit adapter targets have partial evidence, with ownership still
open. All77 structural checks pass; this does not establish renderer completion.

### Packet-bank encoders and their overflow helper

Five additional Ghidra exports add368 raw-image-verified instructions.
The four bank encoders scan runs in a 64-bit dirty mask, copy selected source
records into packet memory and publish device+40. Their direct non-stack
writes do not update the source records or the caller's dirty-mask memory.
All enter their run-processing loop without an initial zero-mask guard;
the valid-input population remains part of the caller contract.

| Original | Source and packet behavior | Retained dependency |
|---|---|---|
| `8213DB60` | Caller-supplied source/bank; one 32-bit word per mask bit. Each run emits a header followed by sequential words. | `8213D698`, granularity1, when the run exceeds device+44 capacity. |
| `8213DC20` | Thirteen setup words, including device+10268; then 16-byte records beginning at device+10144, bank0x2388, with alignment/header words. | Entry cursor rollover via `8213CF60`; run overflow via `8213D698`, granularity4. |
| `8213DDA0` | 24-byte descriptors beginning at device+1024, bank0x4800. A direct finish emits words `0x25000,0,0x25000,0`; an overflow finish emits `0x25000,0,0,0`. This observed difference is not treated as equivalent without packet interpretation. | `8213D698`, granularity6, and conditional final rollover through `8213CF60`. |
| `8213DF00` | Caller-supplied source/bank; **64 bytes per mask bit**, copied by four vector loads and four vector stores with aligned-down addresses. Header padding aligns the payload. | `8213D698`, granularity16. Ghidra truncates the vector path; the generated PPC translation supplies the loop evidence. |

`8213D698` computes space from device+44 and the cursor, reserves a header word,
rounds the copy size to the supplied granularity, and copies sequential words.
While words remain, it publishes device+40, calls `8213CF60`, reloads the cursor
and repeats. It returns the final cursor; its caller performs the final
publication. Division/trap guards, zero-count input behavior and source bounds
remain caller obligations. The known wait/submission effects of `8213CF60`
remain transitive effects of these encoders, even though the local destinations
are packet memory and the cursor.

The retained batch now has72 bodies and3,833 verified instructions. The
explicit ledger has86 partial local-effect sites and100 pending sites.
All77 structural checks pass; full ownership and runtime equivalence remain
unproven. No renderer implementation was changed.

### Original shader upload, patching and cache effects

Four retained Ghidra exports add640 raw-image-verified instructions. Their
contracts describe the originals separately from the previously reviewed
native adapters.

* `8213E950` reserves shader bytes plus a five-word prefix through `8213D160`.
  Allocation failure exits before metadata updates. Success copies code through
  `821E8320`, flushes the source range through `82141AB8`, optionally invokes
  output patching, sets device+10810 bit7, calls `8213E070`, copies the16-byte
  stride snapshot and publishes the command cursor. Allocation, byte-count
  alignment and copy-helper effects remain separate obligations.
* `8213E800` retains the descriptor-matching loop and output nibble update
  described in the adapter review, but also calls `8213E678` and `8213E748`
  to patch the code buffer. Its only local non-stack store is the output word;
  that does not account for its callees' instruction writes.
* `8213E070` constructs12-byte instruction records in stack scratch, then
  reorders/coalesces marked groups and copies each record to code-buffer
  offsets selected by patch descriptors. Declaration records, shader templates,
  stride bytes and two lookup tables supply inputs. It **reads** the stride
  buffer passed through r6; it does not refresh it. This corrects the earlier
  suggestion that removing E070 might remove a stride refresh. Scratch bounds,
  descriptor bounds and instruction-format equivalence remain open.
* `8213EB68` compares cached declaration identity and masked stride snapshots.
  A mismatched cache entry still in flight returns zero without updating it.
  Otherwise it patches code and, when needed, assigns an identity by atomically
  incrementing global`82578D0C`, skipping zero and0xFFFFFFFF. It writes the
  declaration identity, cache identity/snapshot, current fence and device stride
  snapshot. A cache hit still updates the fence and device snapshot; it is not
  a read-only lookup.

The retained batch now has76 bodies and4,473 verified instructions. The
explicit ledger has89 partial local-effect sites and97 pending sites.
All77 structural checks pass; ownership, helper effects and runtime populations
still prevent a complete semantic-inventory claim.

### Shader patch targets and reservation dependency

Three more Ghidra exports add123 raw-image-verified instructions.
`8213E678` and `8213E748` locate a descriptor list using shader metadata and
the low12 bits of an input halfword. Each descriptor selects a12-byte code
record; iteration stops only after processing a descriptor with mask0x1000.
Neither helper has a local descriptor-count or destination-bound check.

The generated instructions for E678 initialize the replacement triple
`0xC8000000, 0, 0x02000000`, then copy it through `821E8320` to each selected
code record. Ghidra's saved C omits the first two initializations, so that
output alone is insufficient to establish the patch contents. E748 copies
each existing record to stack scratch, replaces its first word's low nibble
with bits8..11 of the peer descriptor, then copies it back. Their direct
stores are stack writes; code-buffer mutation occurs through the copy helper.

`8213D160` checks whether the requested word count fits between device+40
and+44. If needed it calls `8213CF60`, rechecks, then calls `8213CC20` to grow
the buffer. It returns zero on growth failure or the current cursor on success;
it does not advance that cursor locally. Flush/submission and growth effects
remain transitive obligations, along with request-size arithmetic invariants.

The retained batch now has79 bodies and4,596 verified instructions. These
three functions extend helper coverage rather than changing the explicit-call
ledger:89 sites remain partially reviewed and97 await local-effect review.
All77 structural checks pass; semantic completeness remains unproven.

### Buffer growth and allocation failure effects

Three retained exports add258 raw-image-verified instructions. `8213CC20`
dispatches to the internal ring allocator, an owner-buffer allocator, or the
previously reviewed callback allocator `821415A8`. Success installs the new
cursor/limits and clears allocation bookkeeping. Failure installs device+15152
as a4,800-byte fallback buffer and sets device+10809 mask0x40. A newly failed
allocation can still wait through `8213C928` and clear device+13520. An already
set failure flag instead reinstalls the fallback and returns zero immediately.
Thus a zero return does not mean the device was unchanged.

`8213C120` allocates regions within existing ring memory. The even-flags path
can wrap, reset device+13504, increment generation+13500 and set byte+10810
mask4 before later protected-range checks return failure. Success reports
the actual byte count and calls the previously reviewed `8213BC48` wait helper.
The odd-flags path uses the requested alignment and separate wrapped-region
limits. Neither branch performs a local heap allocation.

`82141340` uses the owner buffer at device+13140. Its even-flags path returns
the owner's initial buffer only when device+48 is zero, publishing its pointer
and size. Its odd-flags path allocates downwards, rejects overlap with the
cursor plus164 bytes, then lowers device+13496/+44/+48. It has no calls.
Alignment, arithmetic wraparound, protected-range and owner-lifetime invariants
remain open and are recorded in the function contracts.

The retained batch now has82 bodies and4,854 verified instructions. These
helper reviews leave the explicit ledger at89 partial sites and97 pending.
All77 structural checks pass; semantic completion remains unproven.

### Resource destruction, clear dispatch and profiling activation

Four more retained exports add547 raw-image-verified instructions.
`82134220` destroys resources according to the header's type nibble. Several
types wait on resource+8 before releasing payload memory; texture-like paths
can release two allocations, and special types call additional cleanup helpers.
Every path releases the header. This routine does not locally decrement a
reference count. Its CTR branch implements a local switch, not an external
callback. The exact type/access/free mapping is in the function contract.

`821340D0` unpacks an input color with vector operations into stack scratch,
then calls `82133D20` once for a null rectangle list or once per16-byte
rectangle. Its local writes are stack/argument storage; actual clear effects
belong to the callee. Ghidra truncates this body, so the generated PPC
instructions provide the scoped control-flow and vector evidence.

`82138858` is a device diagnostic state machine, with drains, optional
device+19956 object release, global state transitions and external monitor
callbacks. `82139228` updates frame/time counters, resets timing accumulators,
and initializes or advances profiling queries. Their callback target
populations and nested cleanup/configuration effects remain open.

The profiling review resolves one writer relevant to the earlier swap finding:
when device+20080 equals1, `82139228` creates missing query objects, initializes
indices, allocates the32-byte profiling buffer if absent, and sets+20080 to2.
This establishes an activation route for the `82139148` profiling-packet
predicate. It does not establish which runtime settings or callers request
mode1, or whether the retained packet is needed in the native path.

The retained batch now has86 bodies and5,401 verified instructions. The explicit
ledger has93 partial local-effect sites and93 pending. All77 structural checks
pass; resource lifetimes, callback populations and runtime equivalence remain
unresolved.

### Profiling activation through statistics queries

Two more retained exports add490 raw-image-verified instructions. The literal
device+20080 writer search identifies `82138040` setting mode1 at`82138088`
and the previously reviewed `82139228` setting mode2 at`821393D8`. This search
does not exclude writes through aliases or bulk initialization.

`82138040` returns success immediately if the current frame was already
sampled. Otherwise mode0 becomes1 and returns unavailable. In mode2 it polls
the next query; when ready it copies480 bytes of global history, fetches new
query data, updates global timing values, advances the consumer index and
marks the frame sampled. Reading profiling statistics therefore has retained
state and resource effects.

`82138158` invokes that poller for statistics selectors7 through16. Its own
selector6 also advances device+20048 before rejecting some unavailable samples.
The native hook replaces only selector6; the other selectors call the original.
This supplies a concrete route from statistics queries to mode1, then mode2,
then the conditional profiling packets in`82139148`. Actual runtime selector
usage and native packet necessity remain unverified.

The instruction database has ten direct call sites from`82138158` to the
poller, and one direct caller of`82138158`: `82138E00` at`82138F60`. Indirect
callers and external diagnostic requests remain open. The retained batch now
contains88 bodies and5,891 verified instructions. The explicit ledger has94
partial local-effect sites and92 pending; all77 structural checks pass.

### Statistics dispatcher enable mask and table

The retained export of `82138E00` adds111 raw-image-verified instructions.
The dispatcher first requires a nonnull global device, nonzero device+52 and
nonzero64-bit device+10752. Command0 clears the statistics mask at`82578CFC`;
command0x10 clears a selected bit and0x11 sets it. Selector6 additionally
controls device+20056 and resets indices+20048/+20052 when enabled.
Command0x22 stores device+20100.

Command0xFF walks16 triples at`82552AA0`, calling the statistics getter and
report helper for enabled entries. `inventory-profiling-dispatch.py` verifies
the immutable image hash and exports the exact mask/report/selector mapping
to `profiling-dispatch-table.csv`. The selectors are0 and3..17; entries7..16
reach the profiling poller. This table establishes that the retained command
dispatcher can request those selectors when their mask bits are enabled.
Commands0xE0..0xE2 can also enter cursor-overflow handling under owner/thread
guards.

The native dispatcher hook always executes the original, then conditionally
erases a native profiler for reset/selector6 enable-disable commands. The
instruction database has no direct caller of `82138E00`; indirect or external
command invocation remains unresolved. Static enable commands and selector
mapping do not establish actual runtime activation.

The retained batch now has89 bodies and6,002 verified instructions. The explicit
ledger has95 partial local-effect sites and91 pending. All77 structural checks
pass; the new table extractor additionally checks the image hash and selector
sequence. Semantic completion remains unproven.

### Retained depth-target binding

The retained export of `82137CB8` adds 184 raw-image-verified instructions.
It stores the depth resource at device+12184. With a nonnull depth resource
and no color target at +12168, it calls `821378E0`, whose viewport/scissor
effects remain transitive. It copies resource fields into +10248/+10432,
updates binding and recording flags, and dirties device state. The null path
clears the depth-format low bits and the corresponding flag.

Both paths update depth-enable bits at +10420 from +11540/+11544 and set
dirty masks at +16 (0x100, 0x800 and bit 49). The local body has no resource
header/refcount writes, retirement call or packet/cursor writes; this does
not establish those properties for its transitive helpers. The hook at
`src/native_graphics/guest_shader_bridge.cpp:7259` always calls the original
before `PublishNativeRenderState`.

The retained batch now contains 90 bodies and 6,186 verified instructions.
The explicit ledger has 96 partial local-effect sites across 92 callees and
90 pending sites. All 77 structural checks pass; transitive ownership,
callback populations and runtime equivalence remain unresolved.

### Fence producer packet and completion writes

`8213C788` adds 56 raw-image-verified instructions. It saves device cursor
and generation into +12952/+12956, emits ten words into the caller's buffer,
advances the issued counter at +10780 by two, and returns buffer+40. It does
not locally advance device+40 or check buffer capacity. The packet carries
converted completion addresses, the issued value, and cursor OR generation&3.
When device+19956 is zero and byte +10809 has mask 2, it also writes the
issued value and composite cursor directly into completion memory.

The native-host branch at `src/native_graphics/guest_shader_bridge.cpp:5221`
captures an event, preserves the bookkeeping and return size, and omits the
original packet and immediate completion writes. Non-native mode retains the
original. Queue publication, ordering and lifetime equivalence are separate
obligations; this local review does not prove them.

The retained batch now has 91 bodies and 6,242 verified instructions. The
explicit ledger has 97 partially reviewed sites and 89 pending sites. All
77 structural checks pass; semantic completion remains unproven.

### Worker signal encoding and decompiler predicate correction

`8213C9F0` adds 72 raw-image-verified instructions. It encodes the callback
and argument into a 23-word packet beginning at entry r4+4, with an optional
two-word prefix. The original does not invoke the callback locally or update
device fields. It returns the address of the last written word: entry r4+92
or +100. There is no local capacity check.

The prefix predicate is `(flags & 0xC0FFFFFF) == 0`, as shown by the generated
`rlwinm.` and conditional branch. Ghidra adds an incorrect `flags == 0`
conjunct. Nonzero flags limited to bits 24..29 therefore distinguish the two
expressions. The six-bit signal mask comes from those bits, with zero mapped
to 4. Packet addresses use device+10772, reloaded for the second address.

The native hook at `src/native_graphics/guest_shader_bridge.cpp:5453` supports
callback `8214EBA0` through its CPU tail and queued capture; other callbacks
throw in native-host mode. This establishes local routing, not complete
callback population or delivery-order equivalence.

The retained batch now has 92 bodies and 6,314 verified instructions. The
explicit ledger has 98 partially reviewed sites and 88 pending sites. All
77 structural checks pass; semantic completion remains unproven.

### Native signal span and queue ownership review

The hash-gated worker signal extraction retains the generated prefix condition,
including nonzero flags confined to bits 24..29. It removes packet stores and
both device+10772 loads but preserves pointer increments. The returned address
is the last word, so the hook's range beginning at `begin+4` with length
`return-begin` covers exactly the original 23 or 25 words. `NativeSignalCpuMask`
matches the original six-bit extraction and zero-to-4 default. This resolves
the local predicate/span question raised by the decompiler discrepancy.

Source review of `src/native_graphics/d3d11_signals.cpp` establishes these
queue-local rules: capture rejects empty, unaligned, overflowing and overlapping
unsubmitted ranges; submission rejects partial overlap and orders contained
signals by command address. A completion event is established before captured
entries are erased. Completed batches are consumed FIFO, with completed signals
retained until acknowledgement and the pending count reduced on acknowledgement.
The backend completion implementation is a separate dependency.

`NativeSignalDelivery` in `d3d11_signals.h` blocks delivery during nested
submission and clears each CPU bit only after its wake callback returns. If a
later wake throws, earlier successful CPU bits remain cleared. This is a local
retry property, not proof that a throwing wake had no partial effects. Caller
locking, guest publication, backend completion and actual callback populations
still require end-to-end ownership review. No runtime execution was performed.

### Signal publication, submission scopes and backend completion

The call-site review in `guest_shader_bridge.cpp:2788` establishes that
`NativeSignalSubmissionScope` increments a per-device depth under the SDK
critical region and delivery mutex, then decrements depth under the delivery
mutex on destruction. The C868 native dispatch encloses its CPU transaction
in this scope and polls after leaving it. C410 snapshots descriptors under
the recursive submission gate and a scope; the owned submission service adds
a nested scope. Its observer calls remain indirect and validate the cursor
generation after returning. Before arming signal and fence ranges it validates
again and calls `SubmitSceneFrameLocked`.

`PollNativeWorkerSignals` at bridge line 2806 first checks submission depth.
When a delivery slot is empty, it takes the renderer mutex followed by the
delivery mutex, peeks one completed signal, enqueues it in CPU delivery, then
acknowledges queue ownership. It releases these locks before acquiring the
SDK critical region and delivery mutex for guest publication. An occupied
guest slot at device+10900 defers delivery. It also checks the global device
identity before proceeding.

For each selected CPU, the wake lambda checks callback `8214EBA0`, writes the
argument to device+10900, copies the PPC context, and invokes `KeSetEvent` with
r3=device+11228+56*cpu, r4=1, r5=0. It does not call arbitrary guest callback
code or retire the worker busy counter locally. A throw after publication but
before successful wake leaves a side-effect window; the current source review
does not establish recovery or exactly-once delivery for that case.

The D3D12 backend at `d3d12_backend.cpp:1128` refuses completion marking while
a frame is open, creates a fence if needed, signals the queue with the next
value, then publishes that value. Its completion object at line 773 compares
the completed fence value to the target and throws on the device-removed
sentinel. These are source-level ordering observations. SDK event semantics,
observer target populations, worker consumption and all exception paths remain
separate obligations; no runtime validation is implied.

### Original worker callback and thread-entry contracts

`8214EBA0` has 11 instructions, although Ghidra truncates after its publication
store. The generated instructions show device=Word(Word(0x82000720)), a write
of entry r3 to device+10900, CPU selection from byte r13+268, and a tail call
to `KeSetEvent(device+11228+56*CPU,1,0)`. It has no local slot-busy check,
CPU-index bound check or busy-counter retirement. Native delivery adds guards
and selects CPUs from the captured mask instead of the current PCR byte.

`8214EAD0` has 51 instructions. It waits on worker-record+32, using a stack
timeout value of -300000 only when record+4 matches owner+380. Status 0x102
enters a timeout-service loop under the global device's critical section at
+13524; device byte +10810 mask 2 gates `821512D8` and `82151460`. Other wait
statuses reset the event, then either exit when owner+4 is zero or dispatch
`8214E8E0(record)` and repeat. Local stores are stack-only; command consumption
and busy retirement belong to helpers and remain to be reviewed.

Integration exposed a census omission: the callback was present in focused
Ghidra exports but absent from the broad direct-call graph. Explicit callback
and thread-entry roots now preserve their verified registration roles. The
census grows to 2,501 roots and 9,208 functions; the retained review contains
94 bodies and 6,376 raw-image-verified instructions. This is an inventory
coverage correction, not a newly discovered missing native implementation.

### Worker consumer and command-supplied callback target

`8214E8E0` adds 123 raw-image-verified instructions. Its owner is the pointer
at worker-record+0. It acquires owner+0 using reservation/conditional-store
instructions, increments the participant count at +48, and consumes publication
+88 into a continuation or new command cursor. With no publication the first
participant clears the count and releases the lock. Ghidra's atomic pseudo-code
was cross-checked against the generated instructions.

Command `0x81000000` loads the following two words into owner+16 (callback)
and +20 (argument). A job command initializes index/count/data/next-cursor
fields +24/+28/+32/+36. The indirect call at `8214E9D4` passes worker record,
owner+20, owner+32 and owner+24 after releasing owner+0. Thus the target is
command-supplied, not a vtable slot; its producer population remains unresolved.

Other high-bit commands coordinate a barrier at owner+108. The last participant
calls `8214E640`; others release the lock, spin on the barrier, and reacquire.
Exact command `0xC0000000` delegates list progression to `8214E328` and exits
on a null result. There is no direct owner+56 busy-counter retirement in this
body, so retirement remains a helper obligation. Bounds, callback effects and
cross-thread invariants remain open.

The native hook retains the audited worker adapter and then services completed
signals until a publication appears or no submitted/pending delivery remains.
This does not replace the CPU job dispatcher. The retained ledger now contains
95 bodies and 6,499 verified instructions; 99 explicit sites have partial
local-effect reviews and 87 remain pending. All 77 structural checks pass.

### Worker list completion and busy retirement

`8214E328` adds 53 raw-image-verified instructions. It acquires the spinlock
at owner+60 and reloads the command word. A nonterminal word releases that lock
and returns word+4. Terminal `0xC0000000` decrements owner+48; only the last
participant decrements busy at owner+56, and only when continuation+80 is zero.
That participant signals owner+64 and clears owner+52. Other participants reset
the event. All terminal paths save the cursor at +36, release the spinlock,
clear owner+0, wait on owner+64 and return zero without checking wait status.

The reviewed initialization in `8214EE50` stores device+10812 as the worker
owner. Therefore owner+56 is device+10868 (busy), and owner+88 is device+10900
(publication). This connects native signal publication to retained CPU
consumption and retirement. Retirement is conditional on completed participants
and absent continuation; it cannot be replaced by merely acknowledging GPU
completion. Counter-underflow invariants, continuation writers and platform
event semantics remain separate review obligations.

The retained batch contains 96 bodies and 6,552 verified instructions. All
77 structural checks pass; explicit pending-site counts are unchanged because
this helper was reached transitively rather than as a direct native hook call.

### Worker callback producer search: negative static result

`tools/inventory-worker-command-candidates.py` verifies the immutable image
hash and scans aligned `0x81000000` words with space for a callback/argument
pair. Its 16 matches have no following word equal to a known database function
entry. The CSV preserves every match and exact marker-address data references;
none is promoted to an executed command record.

An exact generated-source literal search for the signed, unsigned and hexadecimal
forms finds two sites, both consumers: `8214E8E0` and `8214E640`. The source
locators are saved in `worker-command-literal-sites.csv`. These results narrow
the next search to other construction routes, but do not exclude computed
constants, byte stores, copied templates, external inputs or dynamic buffers.
The callback population at `8214E9D4` remains unresolved. This standalone search
does not add a semantic-completion gate or change the 77 structural checks.

### Continuation writer and worker-slot wait

The `8214E640` interpreter's `0x8C` command tests its second word against
owner+364. A match stores the following command address into continuation+80
and switches to owner+96. This is a concrete writer for the continuation that
suppresses busy retirement in `8214E328`; the source of the command stream
and all writers remain unclosed. A literal device-offset search also finds
publication through `8214EBD0`, whose CPU selection reads device+11192 rather
than the PCR byte used by `8214EBA0`. This is a located path, not a completed
callback-population review.

The newly reviewed `8214E5B8` has 33 raw-image-verified instructions. For six
slots at device+11208 with stride 56, a nonzero entry+4 enables waiting. It
snapshots `(Word(entry-4)&~3)|(Word(entry)&3)`, begins a wait record, polls
`82139688` before comparing completion, reloads the pointer at entry+16 on
each comparison, and finishes through `82139508`. Its local stores are stack
only. `WaitNativeWorkerSlots` preserves this local order; the native hook
uses a cloned context and retained wait services. Timeout, accounting and
service effects are not removed by the native loop replacement.

The retained batch now contains 97 bodies and 6,585 verified instructions.
The explicit ledger has 100 partially reviewed sites and 86 pending. All 77
structural checks pass; callback producer and continuation invariants remain
open.

### Alternate worker publication callback

The 11 instructions of `8214EBD0` publish entry r3 to device+10900 and tail-call
`KeSetEvent(device+11228+56*index,1,0)`. The index is read from device+11192
before publication. Ghidra truncates after the store, so the event target and
arguments come from the generated instructions, verified against the raw image.
There is no local slot-busy check, index bound check or busy retirement.

The database identifies one direct caller, `82144868`. Its call at `82144AC8`
passes object+812 when nonnull, after an object virtual call and `82141AB8`,
then clears object+812 only after the callback returns. The database has no
data-reference or vtable rows targeting EBD0; that negative result does not
exclude indirect uses. Object identity, producers of +812 and event failure
handling remain unresolved. This is a separate publication route from EBA0's
PCR-based CPU selection and the native signal hook's captured CPU mask.

The retained review now contains 98 bodies and 6,596 verified instructions.
All 77 structural checks pass. Pending explicit-call counts remain unchanged;
this review closes a local transitive contract, not the producer population.

### Pending publication producer identified

The raw-decoded `stw +812` search yields 11 sites, including an explicit stack
store and unrelated object candidates. It does not treat equal offsets as equal
object types. `inventory-worker-publication-writers.py` preserves these sites,
verifies their image bytes, and separately verifies vtable `82009C74` slot 3
as `821445A8` and slot 6 as `82144868`.

Within the command-84 branch of `821445A8`, after its entry command-filter
guards and the global device+10809 mask-2 check, callback object+832 is compared
to `8214EBA0`. Equality copies object+836 to pending field +812. Otherwise an
indirect call invokes object+832 with object+836. The previously reviewed flush
caller later routes the pending pointer through `8214EBD0` and clears it.
`82144240` initializes these three fields to zero. This identifies a producer
for the alternate publication route and its structural object relationship.

The companion JSON records full-body raw-byte verification separately from
the limited semantic review of these branches. Runtime receiver identities,
writers of callback/argument fields +832/+836, entry-filter activation and other
indirect callback targets remain open. This standalone review does not expand
the retained-body contract count or claim those whole functions are reviewed.

### Packet parser reaches the publication command

The reviewed portion of `82144868` scans entry r5 words starting at entry r4.
Header bits 30..31 select the packet type. Type 0 skips the encoded payload;
types 1 and 2 advance only past the header. Type 3 extracts opcode from bits
8..15, payload count as `((header>>16)&0x3fff)+1`, and a flag from bit 0.
Except for special opcodes 55 and 63, the indirect call at `82144968` invokes
vtable byte offset 12 with `(object,opcode,payload,count,flag)`, then advances
past the payload. For verified table `82009C74`, that target is `821445A8`.
This establishes the structural path from an opcode-84 packet to the pending
publication branch. Special/nested packet paths remain outside this review.

The callback and argument fields +832/+836 remain unresolved inputs. Searches
for direct stores, wider stores and literal address formation did not identify
a matching producer in the reviewed methods. That is not evidence that the
fields stay zero: indexed stores, copies, subclass behavior, external writes
and alternate object identities remain possible. No callback population is
closed by this packet-parser finding. The route JSON now records this bounded
review alongside its existing raw-byte and vtable verification.

### Nested packet traversal and complete local parser contract

A fresh Ghidra export of `82144868`, cross-checked against all 157 raw
instructions, extends the earlier branch-only review. Special opcodes 55 and
63 report preceding bytes through `82144308` kind 7. They traverse a nested
buffer only when the packet flag is clear or either object mask pair
(+824/+816, +828/+820) intersects, and entry depth is below 2. Payload word 0
is converted by subtracting 0x40000000 below 0x20000000, otherwise 0x41000000;
payload word 1 supplies a 20-bit word count. Vtable+24 receives the converted
address, count and depth+1, followed by a cache-range flush. In verified table
`82009C74`, this virtual target is the same parser. The local depth guard does
not establish a bound for arbitrary other implementations of that slot.

Entry conditionally calls vtable+32. Nonzero-depth calls emit opening/closing
report records. Finalization reports remaining bytes, invokes vtable+0,
flushes the original range, then publishes and clears pending object+812.
That clear is the only local nonstack store; reporting, virtual calls and
publication carry transitive effects. Payload bounds, arithmetic overflow,
actual receiver populations and reporter behavior remain unproven.

The retained batch now has 99 bodies and 6,753 verified instructions. All 77
structural checks pass. Earlier branch-only JSON remains scoped evidence;
the expanded contract is in `small-retained-contracts.csv`.

### Observer reporting allocation and pointer exposure

The `82144308` export adds 114 raw-image-verified instructions. Object+776
mask 0x20000000 suppresses reporting. Otherwise it first reports a five-word
header through monitor command 43. With a nonzero payload size, kinds other
than 8 report the original pointer; kind 8 allocates with flags 0x24800000,
copies the payload, reports that temporary pointer, and frees it. Allocation
and copying still occur when no monitor target is available, and there is no
local allocation-null check before copying.

Three indirect sites select the monitor through the global at 0x8200071C or
the fallback table at 0x82000800, reloading between reports. Stack descriptors
contain payload counts truncated to whole words. Local stores are stack-only,
but callees allocate, copy and free, and external callbacks can inspect the
exposed data. Callback retention, actual target registration, failure handling
and payload lifetime remain unresolved; the function cannot be classified as
effect-free merely because it reports diagnostics.

The retained batch now contains 100 bodies and 6,867 verified instructions.
All 77 structural checks pass. The whole renderer inventory remains incomplete.

### Device factory output and failure contract

`82139A40` adds 45 raw-image-verified instructions. It clears the output
pointer before requesting 20,480 bytes with alignment 128 through `82139638`.
Mode 2 sets device byte +10808 mask 0x80, calls `8213C0B8`, then `821479B0`.
Other modes store flags at +20416, supplying default bit 0x04000000 when
neither mask 0x100 nor 0x3F000000 is set, then call `821477A8` with presentation
parameters. Only a nonzero initializer result publishes the device pointer
and returns success. Allocation or initialization failure returns 0x8007000E;
initialization failure first invokes `82139760`.

The native hook always executes the original factory. At the audited caller
return address `8219E4D4`, optional render dimensions update two input blocks
after dimension-range and original-width checks. This does not replace device
allocation, initialization or cleanup. Initializer effects and whether release
fully unwinds each partial failure remain separate lifetime obligations.

The retained ledger now contains 101 bodies and 6,912 verified instructions.
101 explicit call sites have partial local-effect reviews and 85 remain pending.
All 77 structural checks pass; semantic completion remains unproven.

### Initializer failure stages and external publication

`821477A8` and `821479B0` add 154 raw-image-verified instructions. Normal
initialization calls `82147738`, creates two critical sections, sets device
+10810 mask 4 and calls `821476A8`. It then publishes the global device before
configuration lookup, command-buffer reset, worker creation and presentation
setup. Each of those later stages can return failure without local rollback.
The factory's release path must therefore handle an already published,
partially initialized device; this review does not prove that it does.

Later successful stages expose pointers to device timing/profiling fields
through the external monitor table, flush pending work and initialize timing
scales. Final training/validation failures log diagnostics but still return
success. Mode-2 initialization is shorter: after `82147738`, it calls
`8213C0B8`, sets three sentinel words, initializes defaults and calls
`821470A8`. The factory already called `8213C0B8` before this mode-2 initializer.
The repeated call is observed, not assumed redundant.

These local contracts distinguish failure stages from transitive helper cleanup
obligations. The retained batch now contains 103 bodies and 7,066 verified
instructions. All 77 structural checks pass; full lifetime coverage remains open.

### Base allocation and platform registration

`82147738` and `821476A8` add 63 raw-image-verified instructions. The base
initializer sets device reference count +52 to 1, copies the global 64-bit
value at `82552B60` to +10752, records the current thread at +10760, clears
+10764, and allocates 4,800 bytes with flags 0xB5800000. It stores the result
at +15152 even on failure and returns whether it is nonnull. This establishes
that the factory's release sees an initialized reference count after this
allocation fails. The reviewed destructor conditionally frees +15152, but that
matching field alone does not prove all partial-initialization cleanup.

`821476A8` performs platform registration rather than allocating: it calls
`VdInitializeEngines`, `VdShutdownEngines`, then initializes again with two
additional static pointers. It registers termination record `82552DA0` and
graphics interrupt callback `8213BEC0` with the device as context. It does not
check import results and returns 1. Consequently its caller's zero-result
branch is not an ordinary return path of this body; exceptions and platform
side effects remain separate. Registration lifetimes and callback activation
must still be traced through the platform implementation.

The retained batch contains 105 bodies and 7,129 verified instructions. All
77 structural checks pass; semantic completion remains unproven.

## Registered graphics interrupt callback review

`8213BEC0` is passed by `821476A8` to `VdSetGraphicsInterruptCallback`
with the device as context. It was absent from the direct-call census; adding
the verified registration target as an explicit root adds one function and two
indirect sites. Both Ghidra refreshes completed successfully in read-only mode.
The original body in `generated/default/edf2017_recomp.40.cpp:4513` contains
69 instructions, all checked against the immutable guest image by
`tools/inventory-small-retained.py`.

- Reason 1 reads the callback from `Word(device+10772)+16`. The sentinel
  `0x0BADF00D` logs through `82145078` and traps. A nonzero callback is invoked
  at `8213BF14` with the argument at the reloaded writeback pointer +20.
  Afterwards it reads the current CPU byte at `r13+268`, acquires the raised-IRQL
  spinlock at device+10776, clears that CPU bit in writeback word 0, masks the
  result to six bits, and releases the lock. PPC `slw` semantics govern the
  shift; the decompiler expression alone is not a portable C implementation.
- Reason 0 requires bit 0 of MMIO word `0x7FC86544`. It increments device+15124,
  decrements a positive signed countdown at +15132, and on reaching zero writes
  MMIO zero through the writeback pointer +4 and copies +15124 to +15128.
  A nonzero callback at device+15120 is invoked at `8213BFC8` with a stack record
  containing `[device+15124, device+15136, 0]`.
- Other reasons return. Callback targets, their publishers, teardown ordering,
  and runtime activation remain unresolved.

The inspected adjacent SDK source, `rexglue-sdk/src/kernel/xboxkrnl/`
`xboxkrnl_video.cpp:315`, only registers the callback when a graphics system
exists. `src/graphics/graphics_system.cpp:291` stores the callback/context;
its dispatch method executes it with `[source, context]`, and `MarkVblank`
requests source 0 on CPU 2. This is source-scoped evidence: the linked SDK
binary and the application's graphics-system configuration have not been
established here, so this does not prove runtime activation or inactivity.
The source excerpts and hashes are retained in
`out/renderer-inventory/interrupt-sdk-source-evidence.json`.

The current retained batch is 106 bodies, 7,198 verified instructions,
415 outgoing branch sites, 28 CTR sites and 95 locally store-bearing bodies
(including stack stores). All 77 structural checks pass. The 101 partially
reviewed explicit call sites and 85 pending sites are unchanged because this
callback is a registration root. Semantic inventory completion remains unproven.

## Vblank ticker registration and teardown

The next focused Ghidra batch reviewed `82139928`, `821BEBF0`, `821BEC50`
and `821BEA98`: 61 original instructions, all verified against the immutable
image. The callback `821BEA98` was another address-only target absent from the
direct census; it is now an explicit registration root.

| Original body | Observed contract |
|---|---|
| `82139928` | Stores r4 to device+15120 and returns; no local lock or ownership operation. |
| `821BEBF0` | Writes ticker vtable `82019918`, entry divisor to +4, and 64-bit 1 to +16. Registers `821BEA98` through the setter on `Word(Word(0x8257BFB4)+8)`, then returns the ticker. |
| `821BEA98` | Increments 64-bit global `0x8257C300` by one using load/add/store. It ignores the interrupt's stack record and has no local atomic or lock operation. |
| `821BEC50` | Clears the callback through the same global device chain, changes the ticker vtable to `820170DC`, and conditionally calls `820B2510` when entry flags bit0 is set. It does not check the previous callback identity or locally synchronize with dispatch. |

The native constructor hook at `guest_shader_bridge.cpp:6140` calls the
original only when `edf_native_host` is false. Otherwise it initializes the
ticker fields and host pacing clock without registering the callback. The
native swap path at line5367 rejects nonzero device+15120. These branches
explain the intended separation in source; they do not prove that every runtime
configuration has a zero callback or that concurrent destruction is safe.

The existing destruction review was refined: on the normal branch of
`82147A10`, call site `82147B44` unregisters the platform interrupt with
`VdSetGraphicsInterruptCallback(0,0)`. This precedes `8213D298(device,0)`,
clearing the global device pointer, unregistering termination record
`82552DA0`, and shutting down engines. Device byte+10808 mask0x80 skips this
branch. Unregistration alone does not prove that callbacks in flight have drained.

`tools/inventory-interrupt-publishers.py` exports eight raw-verified literal
`stw` candidates at offsets15120/15136. Only proven receiver chains should be
assigned to device state: several other candidates manipulate allocation or
dimension fields in different functions. Computed/indexed/wide stores and bulk
initialization remain outside this syntactic scan. The candidate CSV and summary
are under `out/renderer-inventory/interrupt-publisher-*`.

Current retained coverage is 110 bodies and 7,259 verified instructions;
102 explicit sites have partial local contracts and 84 await those reviews.
All 77 structural checks pass, with semantic completion still false. The
writeback+16/+20 signal callback publishers and runtime lifetime closure remain
open; this batch establishes one concrete vblank target, not an exhaustive
runtime receiver population.

## Ticker and ring-range wait contracts

Focused Ghidra exports and raw-image verification now cover `821BEAB0`,
`8213BCE0` and the ticker's final helper `821FAC28`, adding 84 instructions.

`821BEAB0` snapshots the previous 64-bit tick at `8257C308` and the divisor
at ticker+4. The divisor is sign-extended from 32 bits and then used in unsigned
64-bit division; zero traps. It computes initial steps from current tick
`8257C300` minus the previous tick. It repeatedly reloads the current tick and
recomputes steps, exiting when the initial count was at least two or the new
count differs from that initial count. The previous tick and divisor remain
fixed during the loop. It publishes the last sampled current tick to
`8257C308`, replaces unsigned step counts above four with one, calls
`821FAC28(ticker+8)`, and returns the low32 step count.

The generated instructions establish those sampling points; the decompiler's
repeated global expressions should not be taken as additional memory loads.
`821FAC28` samples the timebase, samples once more if the first sample's low32
bits are zero, stores the final 64-bit sample through its argument, and returns
one. There is no retry loop. The native ticker wait at bridge line6157 also
calls this helper, but obtains tick progress from a host clock and has an
explicit unlocked-render branch. That scheduling policy remains a separate
contract from the original busy-wait body.

`8213BCE0` computes `finish=(start+size)&Word(device+13480)` and reads the
consumed cursor through `Word(device+10768)+60`. Its unsigned pending predicate
is `start<finish ? start<consumed && consumed<=finish : start<consumed || consumed<=finish`.
If pending, it initializes a stack wait record with mode1, calls `82139688`
before each further cursor read, and stops when that helper returns zero or
the predicate becomes false. It calls `82139508` when leaving an entered wait
and returns `finish` on every path, including a helper-stopped wait. Its local
stores are stack stores; progress and diagnostic effects belong to the helpers.

`native_allocator_wait.h:25` and `:40` preserve the inspected predicate,
poll-before-read order and return offset. `RunNativeAllocatorWait` at bridge
line6268 supplies the original wait-record/progress/end services using a cloned
144-byte stack frame and the original return-address values. This is a scoped
source comparison, not proof of runtime completion or platform-service parity.

The retained ledger now has 113 bodies and 7,343 verified instructions.
104 explicit call sites have partial local contracts, with 82 pending. All77
structural checks pass; full semantic inventory completion remains unproven.

## Scene begin/end retained contracts

The focused batch adds original bodies `8219C7A8`, `8219C840`, `8219C5A8`
and `8219C678`: 173 raw-image-verified instructions. The begin hook at
`guest_shader_bridge.cpp:7323` always calls its original before native setup;
the end hook at line7480 calls its original after native output/scene handling.
These are retained dependencies even when native frame publication succeeds.

`8219C7A8` returns zero if owner+8 has no device. Otherwise it calls
`8219C5A8`, builds a stack viewport with zero origin, owner+84/+88 dimensions,
and float constants from `820008CC`/`820009A4`, calls `821371D0` with the
reloaded device, and returns one. All local stores are on the stack; helper
binding, clear and viewport effects remain transitive.

`8219C5A8` binds owner+132 as color slot0 through `82137F98`, binds owner+136
through `82137CB8`, and conditionally initializes shared floats
`8257BFC0..8257BFCC` from owner+16..+28 times float `82009650`. It skips that
initialization when bit0 of `8257BFD0` is already set. **Store ordering differs
from the decompiler's presentation:** the original stores the first float,
then sets the flag, then stores the remaining three floats. No local lock or
atomic operation establishes publication safety. It then calls `821409A0`
with owner+144/+148 and the shared float array, and `821360D8(device,1)`.
The argument register r8 is not assigned locally before `821409A0`; its role
must be established in the callee rather than invented from a prototype.

`8219C840` does nothing to owner state when the device is null. Otherwise it
loads a destination at `owner+uint32((Word(owner+140)+31)<<2)` without a local
index bound. Owner byte+96 selects `8219C678(owner,destination)` when zero,
or `8213FAF8(device,...)` otherwise. After that call it clears byte+96, then
checks words+44,+60,+76 in order: each nonzero word triggers `8219F7A0` with
its diagnostic string before being cleared. Exceptions/nonlocal exits in those
helpers can therefore prevent later cleanup; the local body has no rollback.
There is no fixed return value.

`8219C678` calls `82140E98` with the destination in r6, shared address
`8257BFE0` in r7, zero r4/r5/r9/r10, and float `820009A4` in f1. It leaves
r8 unassigned locally. It then calls `821360D8` with the reloaded device and
zero. Its local stores are stack-only; resolve and control effects belong to
the callees. The separate `8213FAF8` branch in scene end also writes zero stack
words at SP+92/+100; those arguments are preserved in the contract even though
Ghidra omits them from its displayed call.

Current retained coverage is 117 bodies and 7,516 verified instructions;
106 explicit sites have partial local contracts and 80 await review. All77
structural checks pass. Transitive ownership and complete renderer semantics
remain unresolved; this batch does not establish that either boundary can be
removed from native execution.

## Static bounds classifier contracts

The retained Ghidra batch adds `821C3070`, `821C3178` and `821B0258`, with
252 original instructions verified against the image. `821C3070` has no stores
or outgoing calls. It returns zero for a rejected sphere, one for a sphere with
no boundary intersections, and two for an intersection. Depth tests use view
offsets96/100; side tests use coefficient pairs32/40,48/56,68/72,84/88 and
the point's x/z or y/z. Each side value uses a single-rounded multiply followed
by a single-rounded fused multiply-add. Comparisons at the radius boundaries
must retain the original strict/non-strict branch behavior.

`821C3178` builds the eight center-plus/minus-extent corners on its stack and
calls `821B0258` in place on each. It counts near/far, paired x-plane and paired
y-plane rejections, testing the second member of each pair only if the first
does not reject. It returns one if all corners pass, zero if any one of the
six rejection counts reaches eight, otherwise two. The immutable image contains
`3f800000` (1.0) at `820008CC` and `00000000` (0.0) at `820009A4`; the latter
is the side-plane threshold. Ghidra's rewritten comparison expressions should
not be used to infer unordered/NaN behavior independently of the PPC branches.

`821B0258` reads XYZ and all used matrix coefficients before storing destination
Y, Z and X in that order. It never reads or writes a fourth lane. Its x evaluation
order starts with matrix[0]*x, then y, then z, then translation; y/z start with
their z terms, then y, then x, then translation. Every PPC multiply/add/fused-add
rounding point is retained in the contract. In the box classifier its only
writes therefore affect local corner XYZ; uninitialized fourth lanes in some
corner slots do not reach this helper's input loads.

The native implementations in `native_scene_visibility.h:52` and
`native_scene_tree.h:16` use the inspected sphere tests and affine box arithmetic.
The bridge's optional audit at lines3187/3193 explicitly calls the originals
and compares classification results. That audit branch is not itself evidence
of a runtime match. Published input lifetimes, exceptional floating-point values,
compiler behavior and receiver populations remain separate from this local review.

Current retained coverage is 120 bodies and 7,768 verified instructions;
109 explicit call sites have partial local contracts, with 77 pending. All77
structural checks pass and semantic completion remains false.

## Oriented bounds and four-component transform

`821B0198` and `821C33E8` add 368 raw-image-verified instructions to the
retained Ghidra review. The transform reads all four source components and all
16 matrix floats before storing destination Y,Z,W,X. It also saves/restores
f29..f31 at SP-24/-16/-8 without adjusting SP. Its x arithmetic starts with z,
then adds x,y,w using single-rounded fused operations; y/z/w start with z,
then y,w,x. The original instruction sequence, rather than reassociated
decompiler expressions, defines rounding and overlapping-buffer behavior.

`821C33E8` treats its third argument as a center followed by three oriented
half-axis vectors. It constructs eight stack corners from sequential
single-precision additions/subtractions, transforms them in place through
`821B0198`, then uses six rejection counters and the all-corners-inside count
to return outside0/inside1/intersecting2. Both its local stores and its helper
destinations are stack-local; this does not establish the lifetime of the
original input snapshot.

**New equivalence obligation:** the first two corner slots (SP+112 and SP+128)
retain the center's W from box+12; the other six use 1.0 from `820008CC`.
The native `NativeVisibilityBox` in `native_scene_visibility.h` constructs all
eight corners with W=1.0. Consequently this difference requires either a
producer invariant proving box+12 is always 1.0 on this path, or a separate
argument showing W cannot affect the classification. Neither follows from
these two bodies. This is not a demonstrated runtime defect. Trace the bound
producer `821B2B00` and publication paths before deciding whether a fix is needed.

The optional bridge audit invokes these originals at lines4050/4081; no audit
was run during this analysis. Ghidra exported both bodies successfully, with
one inline-helper and one restart warning on the oriented-box output. Current
coverage is 122 retained bodies and 8,136 verified instructions;111 explicit
sites have partial local contracts and 75 await review. All77 structural checks
pass; the full semantic inventory remains incomplete.

## Bound producer preserves fourth components

`821B2B00` and its linear transform helper `821B0130` add 72 raw-verified
instructions to the retained review. The producer builds three axis XYZ vectors
from source+16/+20/+24 with zero off-axis components, copies source+28..+36
to the center XYZ, and copies source+40 to destination+64 as the radius. It
transforms each axis in place through `821B0130`, then the center through
the already reviewed affine helper `821B0258`. The radius has no local scale
recomputation.

Neither the producer nor those helper calls writes destination+12/+28/+44/+60
under ordinary valid, non-stack-overlapping arguments. The linear helper writes
only XYZ, has no translation term, and loads its source and used matrix values
before storing Y,Z,X. Thus the producer **preserves** the center W; it does not
establish W=1.0. Ghidra displays the first `821B0130` call with only one argument,
but the original register flow supplies destination+16 as both r3/r4 and the
entry matrix as r5. The instruction flow controls this contract.

`ReadNativeSceneVisibility` at `native_scene_visibility.h:117` reads all16
floats from owner+288 into the published box, without normalizing the fourth
components. The next required evidence for the oriented-box W question is the
object initializer and any later writes to owner+300, not another invocation
of this bound producer. A broad literal-offset search returns many unrelated
objects and stack stores; equal offsets alone cannot establish a writer to this
bound. No runtime defect or complete native-equivalence claim follows yet.

Current retained coverage is 124 bodies and 8,208 verified instructions.
The explicit pending count remains75 because these are transitive helpers.
All77 structural checks pass; semantic inventory completion remains unproven.

## Bound initializer: initial W is overwritten

The static-artifact constructor chain reaches the shared initializer:
`820B33B0 -> 820B36C0 -> 821C23F8 -> 821C2090`. Existing focused Ghidra
outputs and generated instructions were inspected for this chain. The new
`tools/inventory-bound-initialization.py` verifies all238 raw instructions
across those four bodies, hashes their saved decompilations, and checks exact
instruction patterns for the relevant bound stores. Its review is scoped to
initialization, not a complete contract for every constructor helper.

`821C2090` sets r29=owner+288 at `821C2124` and initially stores float1.0
to bound+12 at `821C2188`. It also initializes the other three W lanes to1.0.
However, it later copies64 bytes from `Word(0x8257C310)` to owner+224. At
`821C21F8` it selects owner+272, the final16 bytes of that copied matrix,
and copies them to owner+288..+303. The store at `821C220C` overwrites both
center Z and W. Thus this local initialization sequence obtains center W from
the supplied matrix's byte+60, rather than retaining the initial1.0.

This narrows the oriented-box question to the construction-context matrix and
subsequent writers. It does not prove a nonunit W occurs, and helper calls after
the copy remain separate possible effects. The initial literal1.0 store must
not be cited alone as proof that the bound always has W=1.0. Exact sites and
source hashes are in `out/renderer-inventory/bound-initialization-review.json`.
The retained-contract counts are unchanged because this scoped investigation
does not claim complete review of these four constructors.

## Construction-context publication paths

Focused Ghidra and raw-image review adds factories `820FFEF0` and `821A5EF0`,
120 instructions in total. Both copy eight doublewords from a caller-supplied
matrix into the construction context before publishing its address through
`8257C310`. Neither modifies the matrix's last component. This establishes
two concrete publication routes, not an exhaustive list of context writers.

`820FFEF0(manager,matrix,context)` writes manager to context+64, copies the
matrix, publishes the context, allocates976 bytes and calls `82117898` on a
nonnull allocation. It subsequently reads returned object+28 without a local
null check. A zero word triggers a diagnostic; that case or nonzero byte+36
invokes the object's virtual slot+4 with argument1 and returns zero. Ordinary
success returns the object. Both ordinary exits clear the global context.

`821A5EF0(manager,matrix,name,context)` first looks up a registry entry through
`821A5E48`. Lookup failure logs the inline or heap name and returns without
publishing a context. Success copies the matrix, writes manager to context+64,
zeros context+68 and publishes the context. It invokes registry virtual slot0
at `821A5FA4`, then checks the returned object with the same +28/+36 pattern;
the rejection destructor call is at `821A5FE8`. It clears the global context
on ordinary exit. Registry receiver populations remain unresolved.

Neither body restores an earlier global context, provides local serialization,
or guards against a nonlocal exit before clearing the pointer. These are
inventory obligations for caller serialization, nesting and lifetime; they do
not establish a runtime race or a failed allocation in practice. For the bound
W question, the required invariant moves to caller-supplied matrices and any
later context/bound writes. Neither factory proves W=1.0 by normalization.

Current coverage is 126 retained bodies and 8,328 verified instructions.
All77 structural checks pass. The75 pending explicit call-site reviews are
unchanged; callback populations, input invariants and semantic completion remain
open.

## Renderable dispatch and bucket insertion

`821C0C00` and `821A3B80` add 66 raw-verified instructions to the retained
contract ledger. These bodies already had registration decompilations; the
new review connects their instruction-level effects to retained native call
sites. The observation hook at bridge line4147 always forwards the original,
and the gather fallback at line4119 also invokes it.

`821C0C00` returns without object writes when halfword object+64 is nonzero.
Otherwise mode word+52 controls the path. Mode0 calls virtual slot+16 at
`821C0C34`, preserving incoming object/parameter registers r3/r4. The possible
runtime receiver population remains a separate unresolved question.

For nonzero modes it stores float parameter+40 to object+44. This is an
`lfs/stfs` copy, despite the decompiler's displayed integer cast. Mode1 computes
a single-rounded fused multiply-add of parameter+40, parameter+0 and parameter+4,
then multiplies by object+56. Mode2 multiplies object+56 by float `82019A20`.
Other modes load SP+80 without a preceding local initialization. It clamps using
ordered comparisons against `820009A4`/`82018EB4`, converts with PPC `fctidz`,
and splits the low16 bits into bytes object+40 (low) and object+41 (high).
It then calls `821A3B80(Word(object+32),object)`.

`821A3B80` chooses `manager+4*(42+Byte(object+40))`, writes the previous bucket
head to object+60, and installs the object as the new head. It does not read
the high key byte, check duplicates, allocate a node, or locally synchronize.
Thus nonzero-mode dispatch is a live intrusive-list mutation, not merely a
visibility query. Mode validity, bucket clearing, repeat insertion and consumer
ordering require caller/manager evidence before replacing this path.

Current coverage is 128 retained bodies and 8,394 verified instructions;
113 explicit sites have partial local contracts and 73 await review. All77
structural checks pass; semantic completion remains unproven.

### Bucket consumption and per-view reset contracts

The retained batch now includes `821A3BA0` and `821A5080`: 255 additional
instructions checked word-for-word against the immutable image. This formalizes
the earlier dispatch census as local effect contracts rather than claiming new
function discovery. The retained Ghidra run exited successfully.

`821A3BA0` initializes context32/36/40 to floating zero and context44 to one.
It traverses all256 low-byte heads in ascending order, saving each old object60
link before prepending the object to its high-byte bucket. It does not clear the
old low heads or high heads. It then visits high buckets255 down to1, excluding
zero, copies object.float44 into context.float40 and invokes vtable byte-offset16
at `821A3C50` with r3=object and r4=context. Crucially, it reads object60 **after**
the callback. Callback mutation and object lifetime therefore affect traversal.
There is no local cycle check, duplicate check, cleanup or serialization.

`821A5080` enters the view loop only when manager bytes2261/2262 are both zero
and at least one of bytes2216/2217 is nonzero. It initializes stack context via
`821D5930`, snapshots the view end pointer, then for each view:

1. Invokes manager132's receiver slot+4 with the view and view index.
2. Sets context fields, increments manager136, and clears all512 bucket heads
   at manager168..2212 (`821A5190`) before world callbacks gather objects.
3. Traverses world receivers at node8 through slot+8, calls `821A3BA0`, then
   traverses the manager2232 sentinel list's node12 receivers through slot+12.
4. Invokes view slot+16 and manager132's receiver slot+8, then reads the next
   view link. World and overlay next links are also read after their callbacks.

Even if the view body is skipped or empty, manager132 receiver slots+12 and+16
still run (`821A52E4`, `821A52F8`), with the receiver reloaded between them.
Finally the manager2232 list receives slot+16 callbacks for each phase index
starting at manager140, stopping on equality with reloaded manager144. This is
an equality loop, not a less-than bound. The only direct nonstack stores in the
outer body are manager136 and the512 heads; nested effects remain separate.
The two Ghidra-removed trap blocks at `821A5220`/`821A5324` follow self-comparisons
and are unreachable under ordinary PPC execution. Other iterator trap checks
remain present and were not discarded from the contract.

Source comparison: `native_frame_dispatch.h:8` retains the post-callback link
read and skipped bucket zero; its outer helper at line36 retains live manager
and list accesses. `guest_shader_bridge.cpp:4234` substitutes the bucket helper,
but calls `ResolveIndirectFunction` for object and outer callbacks and retains
the full original fallback at line4252. The native call adapter explicitly
sets r4/r5, whereas several original callback sites only assign r3 locally;
receiver signatures must establish which incoming registers matter. This is
an outstanding equivalence question, not a demonstrated runtime failure.

Per-view reset explains the ordinary head initialization boundary; it does not
prove that callbacks cannot reenter dispatch, free queued objects, or insert the
same object twice. Nor does it justify dropping finish callbacks when a view is
skipped. These remain receiver/lifetime obligations for the complete inventory.

Current totals: 130 retained bodies, 8,649 raw-verified instructions, 471 outgoing
branch sites and41 indirect sites. Explicit local contracts cover114 of186 call
sites (108 callees);72 await review. Macro local contracts cover38 of87 sites.
The615 saved focused outputs cover579 unique functions; repeated exports do not
increase the682 functions with focused findings. Structural audit passes;
semantic inventory completion remains unproven. No renderer implementation or
runtime behavior was changed in this batch.

### Static gather marks and hierarchy root traversal

The next retained Ghidra batch covers `820B4038` and `821C61D8`, with127
additional instruction words verified against the immutable image. Their
generated bodies are at `edf2017_recomp.57.cpp:372` and
`edf2017_recomp.21.cpp:8628`; fresh focused outputs are in the retained directory.

`820B4038` snapshots list head/end and context serial12, derives matrix/view-plane
addresses from context16, and visits each node8 object. An object whose word48
already equals the serial is skipped. Otherwise **word48 is written before any
visibility test**, including for an object subsequently rejected. The function
transforms object288 into context32..44 via `821B0198`, then skips on ordered
`-single(context40*context8) > object.float76`. Ghidra prints the inverse path
as `<=`; that spelling is not equivalent for unordered floating comparisons.
The PPC `fcmpu`/`bgt` sequence is authoritative. The native source at
`guest_shader_bridge.cpp:4069` uses `!(depth>object.distance)`, preserving this
branch predicate; this source observation is not a runtime parity result.

Survivors undergo sphere classification (`821C3070`); only result2 invokes the
oriented box classifier (`821C33E8`). A nonzero final result calls `821C0C00`.
The next list link is read after all these calls. Local nonstack stores are the
seen marks, while helper writes include the context transform, sorting scratch,
bucket links and unresolved callback effects. Serial reuse/wrap and callback
membership mutation must therefore be included in the producer/consumer contract.
The native gather still writes owner48; its published-membership path drops the
snapshot before retained callbacks (`guest_shader_bridge.cpp:4113`) and resumes
with the post-callback link. This does not establish purity or independence.

`821C61D8` clears manager100 and104 before validating hierarchy storage. It
requires nonnull manager52 and nonzero arithmetic `(manager56-manager52)>>5`,
selects the first level's vector header at manager52+16, and validates unsigned
start/end ordering. It reloads the level base, checks the second header matches
the first, and snapshots the terminal root pointer. It then advances144bytes
per root, calling `821C5FC8(manager,node,context)` and checking node against the
current vector end before and after each call. The equality termination target
is the snapshot; the validity bound is refreshed after calls. This distinction
matters if a callback changes the vector. No local lock or allocation protects it.

`native_scene_tree.h:56` mirrors the initial counter reset and checks current
root bounds around traversal; it additionally rejects malformed level ranges,
nonmultiple144 root spans and excessive depth. These extra guards are recorded
as contract differences, not proof of a reachable defect. Root traversal alone
does not close `821C5FC8` or its recursive descendant/leaf effects.

Current retained totals are132 bodies and8,776 raw-verified instructions,
480 outgoing branches and41 indirect sites. Explicit local-effect coverage is
116sites across110callees, with70sites pending; macro coverage remains38/87.
All77 structural checks pass, while semantic completion remains unproven.
No renderer implementation or runtime experiment was changed or run.

### Recursive hierarchy classification and accepted descendants

`821C5FC8` and `821C56C0` add103 raw-verified instructions to the retained
contracts. The former returns immediately for zero node116 occupancy; otherwise
it increments manager100, copies all16 bytes of node32 to stack80, transforms
that center through `821B0198`, and applies `821C3070` with radius node64.
Sphere result2 invokes `821C3178` with center node32 and extents node48. A zero
result returns without traversing children or the leaf list.

For a surviving node, manager byte108 enables a virtual call at `821C6098`,
vtable byte-offset24, with `(manager,node+32,node+48,classification)` in r3..r6.
The classification survives this call. Subsequent occupancy/child reads can
observe callback mutations. Classification1 or a null first child invokes
`821C56C0`; otherwise all eight children at node84..112 are classified
recursively, loading each pointer only when its turn arrives. Only manager100
is directly written outside the stack; neither helper increments manager104.

`821C56C0` checks occupancy again. A null first child dispatches the leaf list
through `820B4038(manager,node+120,context)`; otherwise it recursively visits
eight child slots. **Accepting a node bypasses descendant node classification,
not object visibility tests.** Both helpers assume the remaining child pointers
are valid when the first is nonnull; neither has a null-child, depth, cycle or
local synchronization guard. Later child loads can see changes from earlier
leaf callbacks. This is a mutable traversal boundary, not an immutable walk.

The new Ghidra output omits the first eight bytes of the stack-center copy and
prints the leaf gather call with only two arguments. The generated PPC and raw
words retain both 64-bit stores and the incoming r5 context. These omissions
are explicitly not treated as absent initialization or an absent argument.

`tools/inventory-hierarchy-callback.py` verifies the known map table82002624:
slot2 points to `820B4310`, and slot6 (byte-offset24) to `820B3DC8`. Both pointer
values match the immutable image; the artifact is
`out/renderer-inventory/hierarchy-callback-candidate.json`. This identifies a
candidate implementation, not an exhaustive receiver population. The native
tree hook (`guest_shader_bridge.cpp:3155`) explicitly falls back to the original
root traversal when manager108 is nonzero, preserving this unresolved callback.
Its ordinary leaf lambda at line3201 still invokes the hookable gather entry.

The retained batch now contains134 bodies and8,879 verified instructions,
491 outgoing branch sites and42 indirect sites. There are619 saved focused
outputs for583 unique functions. Explicit effect-review coverage remains116/186
because these two helpers are nested dependencies; macro coverage remains38/87.
All77 structural checks pass; receiver/lifetime closure remains unproven.

### World-step parameter copies and hierarchy callback wrapper

Five additional contracts (`820B3DC8`, `820B4250`, `820B3508`, `821A1730`,
`821A16D8`) add116 raw-verified instructions. Two retained Ghidra refreshes
completed successfully while expanding this batch.

The hierarchy callback candidate `820B3DC8` is six instructions: replace r3
with Word8257C034, preserve center/extent in r4/r5, set r6=-1,r7=0,r8=1, then
tail-call `821A8B48`. It discards the incoming classification argument. There
are no local stores. Ghidra expands the geometry-building tail into this
wrapper's output; those expanded stores and `821A7B58` calls must not be
attributed to the six-instruction wrapper. The tail's complete effects and
manager108 activation writers remain open.

`820B4250` builds a stack vector whose first lane depends on the **old**
owner364 bit0x40 (Float82000B34 if set, otherwise zero), followed by zero,zero,one.
It increments owner364 before calling `821A1730` with descriptor Word(owner360).
Next it reloads owner.float356, places that old value in the vector's first lane,
advances owner356 by a single-precision addition of Float820022A8, and calls
`821A1730` with descriptor Word(owner352). Both calls receive global8257C02C
as r3. It then visits the owner372 object list through `820B3508` and calls the
previously verified no-op `8252B718`. There is no local cadence guard or lock.

`821A1730` sets r6=1 and tails `821A16D8`. The latter ignores r3, returns for a
null descriptor, and copies min(unsigned requested count,descriptor.word8)
16-byte records to descriptor.word0. It reloads the destination base per record
and performs load/store of the first64bits before load/store of the second64bits.
There are no local packet, dirty-bit or device calls. Thus these two world-step
calls are specifically **CPU parameter-record copies**, not evidence of direct
GPU submission. Alias/overlap behavior and destination lifetime remain relevant;
the helper does not establish who eventually consumes the copied parameters.

`820B3508` snapshots list head and end, calls object vtable byte-offset12 at
`820B3560` when object.byte36 is zero, and reads the next node after the callback.
It sets r3=object but does not locally initialize other argument registers.
Its only local stores are stack saves. The object callback population and its
state updates remain separate from the two owner-field writes above.

The native world hook (`guest_shader_bridge.cpp:3210`) snapshots old owner356/364
when material ownership/audit is enabled, always invokes the original world step,
then publishes hierarchy and the saved animation values. This ordering is a
retained producer boundary: replaying the original step at render cadence would
advance its owner fields and invoke object updates again. Neither the static
review nor the existing publication code proves those callback effects can be
removed or repeated independently.

Current totals:139 retained bodies,8,995 raw-verified instructions,497 outgoing
branches,43 indirect sites. Explicit local effects cover117sites across111callees;
69sites await review. Macro coverage remains38/87. All77 structural checks pass,
but complete semantic inventory coverage remains unproven. This batch changed
analysis tools and documentation only; no gameplay or renderer test was run.

### Hierarchy box geometry and immediate submission

The `821A8B48`, `821A7B58`, `821FD8F8` batch adds198 raw-verified instructions.
The first computes single-precision center plus/minus extent bounds, initializes
eight16-byte stack records with the supplied color in each fourth word, and
reuses them for three calls to `821A7B58`. Two type3 submissions trace five
vertices around the bottom and top closed rectangles; one type2 submission
contains the eight endpoints of four vertical edges. Each call passes count4,
the supplied mode/flag, and the same stack buffer. Its stores are local stack
stores; the submission helper carries the downstream effects.

The primitive-count table at82008888 is now included in
`hierarchy-callback-candidate.json`: raw entries at82008898 and820088A0 give
type2 multiplier2/bias0 and type3 multiplier1/bias1. Thus the observed count4
becomes eight and five vertices respectively. This check supports the concrete
buffer sizes and does not validate arbitrary type indices.

`821A7B58` snapshots the device from global8257BFB4+8 before `8219CE18`, then
reloads a device for mode-specific `82135078`/`82135108` calls (mode0 uses6/7,
mode1 uses1/1; other modes skip these calls). It applies `82135578` with
flag==1 to the earlier device, invokes `821B94E8` on owner176, validates the
owner52/56 selection, and passes selected-record28 to `82149A90`. It converts
primitive count through the table and calls `821FD8F8` with stride16. Finally
it invokes `8219C9D8` on the reloaded global owner. There is no local rollback,
type-index bound or state restoration. Device identity across those calls and
the binding/material helpers' effects remain separate obligations.

`821FD8F8` calls `821FD428(device,type,count,stride)`. If the result is nonnull,
it calls `821E8320(result,source,low32(count*stride))`, then copies device13076
into device40. A clear bit0x80 in device10808 additionally calls the verified
no-op `8252B718`. Its direct nonstack store is the device cursor; reservation
and payload-copy effects are transitive. It does not locally guard length
overflow or roll back a failed/nonlocal copy. The native immediate hook still
chooses an extracted CPU tail for native-host mode or the full original at
`guest_shader_bridge.cpp:10005`; reviewing the original does not prove that
those two routes are equivalent or that the payload lifetime is closed.

This traces the known hierarchy callback candidate to a concrete geometry
submission path. It does not show that the callback is enabled at runtime,
resolve every manager receiver, or close every downstream helper. Current
totals are142 retained bodies,9,193 verified instructions,520 outgoing branch
sites and43 indirect sites;118 explicit sites have partial local contracts and
68await review. All77 structural checks pass; semantic completion remains
unproven. No renderer code or gameplay execution changed in this batch.

### Immediate reservation: effects before allocation and deferred cursor commit

`821FD428` adds299 raw-verified instructions. Its existing extracted CPU-tail
review is in `inventory-retained-contracts.py:107`; this batch records the full
original's distinct behavior. The retained Ghidra run completed successfully.

It computes words=`low32(count*stride)>>2`, saves device.byte12256, temporarily
sets it to `(stride>>2)&255`, and marks dirty16 bit51 if that value differs from
device.byte11552. It then snapshots and processes dirty banks0/8/16/24/32 through
the original constant/state encoders, clearing each applicable bank afterward.
Dirty24 bit1 also writes sign-extended0xFF000000 to device11568. Those state
changes happen **before** allocation, including on an eventual failure path.

After restoring byte12256, it checks cursor40 against limit48, optionally calls
`8213CF60`, then requests `8213C328(device,words,16)`. A null result stores the
current cursor to device40 and returns zero. There is no restoration of the
already-cleared dirty banks or undo of earlier helper effects.

Success emits13 setup words, including an encoded allocation address and word
length. If device10808 bit1 is clear, it appends a three-word draw packet. If
set, it reserves16bytes of metadata through the device12960 block, extending
that block with `8213CB30` when necessary, records the pre-draw cursor, updates
device12968, and appends12 draw/control words referencing metadata+4. It then
sets dirty16 bit0x1000 and stores final cursor13076, payload pointer13080, and
word count13088. It returns the payload pointer; it does **not** locally commit
the successful final cursor to device40. The caller `821FD8F8` copies the payload
first and commits device40 from13076 afterward.

The original uses wrapped32-bit multiplication and truncates its allocation
word count by shifting two bits. `821FD8F8` copies the wrapped byte product.
No local stride-divisibility or length-overflow guard proves these sizes agree
for arbitrary callers. This is an input-contract obligation, not evidence that
a valid game call currently overruns storage. Reservation lifetime, packet
consumption, nonlocal failures and concurrent access remain separate questions.

The already-reviewed native CPU extraction preserves derived-state work and
dirty clearing but returns zero after restoring the stride byte, omitting
guest vertex allocation and packet construction. This intentional difference
must be assessed alongside the native draw, not described as identical behavior.
The shared copy helper `821E8320` has alignment-dependent paths; its complete
effect review remains pending rather than being inferred from a memcpy label.

Current totals:143 retained bodies,9,492 verified instructions,539 outgoing
branches and43 indirect sites. There are628 focused outputs covering590 unique
functions;690 census functions have focused findings. Explicit effect coverage
remains118/186 and macro coverage38/87. All77 structural checks pass, with
semantic completion still unproven. No renderer implementation was changed.

### Shared payload copy alignment and overlap contract

`821E8320` adds264 raw-verified instructions and a fresh retained Ghidra output.
It saves the incoming destination register as64bits at SP-8 without moving SP,
and restores that value as its return value. This stack write occurs even for
zero bytes. There are no outgoing calls, allocations, locks, atomics or vector
stores in this body.

For a destination not aligned to8, it copies an initial byte prefix only when
the unsigned count exceeds the distance to the next8-byte boundary. Subsequent
source alignment selects the path: source mod8 zero uses8-byte transfers,
mod8 four uses4-byte transfers, and other alignments use bytes. At128bytes or
more it first aligns the output to128, then processes128-byte blocks before
the remaining units/bytes. The aligned8 path interleaves16 loads/stores with
read-ahead; the aligned4 path uses eight groups of four words. The unaligned
source path loads each four-byte group in reverse address order, assembles a
big-endian word, and stores that word while advancing forward.

All paths move forward; there is no overlapping-range direction test or
backward-copy branch. Under ordinary valid, nonoverlapping, nonwrapping input
ranges independent of its stack save, its stores cover the requested destination
extent plus SP-8. Overlap can observe the precise load/store order and must not
be replaced by an assumed memmove contract. The emitted `dcbt`/`dcbtst` operations
are cache hints (no-ops in the generated recompiler source), not cache-line zero
writes. Ghidra omits the ABI stack save from its higher-level output; raw PPC
remains the source for that effect.

This closes the local copy-body review used by immediate vertices, resource
payloads and shader patches. It does not establish allocation size, buffer
lifetime, absence of overlap or concurrent writers at every caller. In particular,
the immediate path's truncated allocation word count versus copied byte count
remains a caller precondition to verify.

Current totals:144 retained bodies and9,756 raw-verified instructions, with
539 outgoing branches and43 indirect sites. Explicit local-effect contracts
cover119sites across113callees;67sites await review. Macro coverage remains38/87.
All77 structural checks pass and semantic completion remains unproven.
No renderer implementation or runtime test changed in this batch.

### Model buffer construction, reset and pool release

Seven retained bodies add340 raw-verified instructions. `821D7468` resets a
vertex owner: when byte52 is nonzero it queries device state through `82134840`
and `82134AE8`, conditionally calls `82137410` with zero binding arguments and
r8=4096, releases owner32 through `821D3EA0`, then clears byte52. It always clears
owner56/60/64. `821D75F8` follows the corresponding index route through
`821375C0(device,0)` and clears only owner56 after the conditional release.
Neither locally frees the owner object itself.

`821D7530(owner,source,stride,count)` first resets the old vertex owner, requests
`821D4700(owner+32,low32(stride*count))`, reads destination owner48, and copies the
requested bytes through `821E8320`. It invokes `822CFDC0` and `822D01C0` before
writing owner56=stride,60=count,64=bytes and byte52=1, returning1. `821D76A8`
similarly resets the index owner, uses `low32(count<<1)` bytes and `822CFE58`,
then writes owner56=count and byte52=1. These constructors do not locally test
allocation/helper results or null destination before copy, nor roll back the
old-resource reset. This is an unresolved helper failure contract, not proof
that allocation failures are reachable or mishandled in ordinary execution.

`821D3DC8(pool,handle)` enters the pool critical section through import8252CECC.
For active handle16, it validates the packed iterators, invokes `821D3748` on
the selected block and inner iterator, revalidates the outer iterator, and uses
`821D3928` to decide whether `821D3B68` removes the outer block. It clears
handle16 afterward but leaves handle0..12 intact. Ordinary exit calls import
8252CEBC. The generated import table maps those addresses to
`RtlEnterCriticalSection` and `RtlLeaveCriticalSection`; no local exceptional
cleanup path establishes lock release after a trap or nonlocal helper failure.

`821D3748` clears the inner node's allocation flag at16, adds node12 bytes to
block4, and coalesces with adjacent free nodes. It merges into a free predecessor
first, then merges a free successor, using `821D3698` to erase the redundant
list nodes. Packed iterators contain list owner in the high32bits and node in
the low32bits; repeated sentinel/owner checks can trap. The reviewed caller
provides the pool lock; this body has no independent critical-section call.
The generated `821D3698` source confirms unlink/free/count-decrement behavior,
but its full retained contract remains a separate next review.

`821D3A40` resets the list sentinel's next/previous links and count before
destroying the detached old nodes. For each, it saves the next pointer, frees
nonnull node8 physical backing through `8212FC28`, calls `820A58B0(node+16)`,
releases node20, zeroes that pointer, and releases the node. The function has
no local lock. Reentry, concurrent users, and helper failure can therefore not
be dismissed merely because the visible list has already become empty.

Native bridge paths retire model metadata before cleanup and pool reuse
(`guest_shader_bridge.cpp:6505`,6519,6531,6606). The block-release hook restricts
its physical-range retirement to return address821D3E30, the reviewed locked
edge, and still calls the original coalescer. Vertex/index construction chooses
native construction or original fallback at lines6777/6784. These observations
identify retirement ordering; they do not prove GPU completion or close every
allocator, binding and physical-backing lifetime.

Current totals:151 retained bodies,10,096 verified instructions,574 outgoing
branches and43 indirect sites. Explicit local contracts cover126sites across
120callees, with60sites pending; macro coverage remains38/87. All77 structural
checks pass and semantic completion remains unproven. Renderer code was unchanged.

### Pool handle replacement, erase helpers and failure output

Six helper contracts add298 raw-verified instructions. `821D3EA0` tails
`821D3DC8(Word8257C330,handle)`. `821D4700` first calls that release, reloads
the global pool, then calls `821D4490(pool,handle,bytes)` and returns its result.
It has no local rollback. Release and allocation each acquire their own lock;
there is no wrapper-level critical section spanning both operations.

`821D4490` enters the pool critical section and aligns bytes with wrapped32-bit
`(bytes+15)&0xfffffff0`. It walks the pool28 list, asking `821D4090` for a suitable
inner block. A validated nonsentinel result writes the outer iterator at
handle0, inner iterator at8 and inner-node8 payload pointer at16, unlocks and
returns1. If no existing block works, it calls `821D4380` with aligned size when
signed32(aligned)>1MiB, otherwise1MiB, then tries the new block. Ordinary failure
unlocks and returns0 **without clearing the output handle**. Helper mutations
may already have occurred; traps have no local exceptional-unlock path.

Combined with the prior constructor review, this narrows the failure question:
the old handle is released first, `821D4700` propagates failure, and
`821D7530`/`821D76A8` do not check it before reading owner48 and copying data.
Reachable failure behavior still depends on `821D4380`/`821D4090` and allocation
policy. This static chain is not a demonstrated gameplay failure.

`821D3698(out,list,iterator)` validates the iterator owner and sentinel, saves
the node's next pointer, unlinks the node, calls `820B2510`, decrements list8,
then returns the packed `(owner,next)` iterator through out. `821D3B68` has the
same outer structure but also destroys the node's physical backing and child
list before freeing the node. Both skip their erase branch if the node equals
the supplied list's sentinel; neither locally proves the packed iterator owner
equals the supplied list. The callers' equality checks therefore matter.

`821D3928` scans inner nodes and returns1 only for allocation flag16 **equal
to1**, rather than for every nonzero value. It returns0 at its snapshotted end
pointer, with additional checks against the current sentinel. It performs no
stores or calls. The coalescer treats zero as free, so the valid flag domain is
a required data invariant across allocation and release. The self-comparison
trap at821D3940 is unreachable under ordinary PPC execution, matching Ghidra's
removed-block notice; other iterator checks remain part of the contract.

Current totals:157 retained bodies,10,394 verified instructions,597 outgoing
branches and43 indirect sites. There are642 focused outputs for604 unique
functions;696 census functions have focused findings. Explicit local contracts
remain126/186 and macro contracts38/87 because these are nested helpers.
All77 structural checks pass; complete semantic coverage remains unproven.
No renderer implementation or runtime experiment changed in this batch.

### Pool split ordering and backing-allocation failure

Four helpers add245 raw-verified instructions. `821D4090` aligns the request
with wrapped32-bit arithmetic and selects the first node whose flag16 is zero
and whose size12 is large enough under a signed comparison. If its size is at
most signed(aligned+256), it consumes the entire node, writes flag1, and subtracts
the entire node size from block4. Otherwise it prepares an allocated record
`[old address,aligned size,1]`, advances the free node's address, reduces its size
and block4 **before** calling `821D3EB0` to insert the allocated record. No local
rollback protects those mutations if insertion fails nonlocally. No-fit returns
the snapshotted sentinel iterator. Zero and overflowed requests have no separate
local rejection.

`821D3EB0` calls `821D3C30`, then obtains the inserted node from the original
node's predecessor link and returns it through the output iterator. Null-owner
and sentinel checks can trap; it does not establish allocation success itself.

`821D4380` constructs a temporary inner-list sentinel, inserts an outer pool
node through `821D42F0`, cleans up the temporary record, and calls `821D3FF0`
on the new first node. A zero result causes `821D3B68` to erase that outer node
and returns the current sentinel iterator; success returns the new node.
Its initial20-byte sentinel allocation is followed by conditional pointer
initialization: a null allocation would still reach a store at pointer+4.
That is a local missing-null-guard observation, not proof of a null-returning
heap policy or an observed failure.

`821D3FF0` requests backing through `8212FB98(bytes,-1,0,1028)` and stores the
returned pointer in block0 before testing it. Null returns0; success sets
block4=bytes and inserts the free record `[pointer,bytes,0]` through `821D3C30`,
then returns1 without checking the insertion helper. These ordinary paths write
flags0/1 as expected by the coalescer and allocation predicate; they do not
exclude other writers or corruption.

The backing-null branch therefore connects through outer-node removal to
`821D4490` returning0 and `821D4700` propagating it. The model constructors still
ignore that result. Actual allocator failure policy, insertion behavior and
runtime reachability remain to verify before claiming a concrete defect.
The allocator lock is supplied by the reviewed `821D4490` caller; these four
bodies do not add their own critical sections.

Current totals:161 retained bodies,10,639 verified instructions,611 outgoing
branches and43 indirect sites. There are646 saved focused outputs for608 unique
functions and700 census functions with focused findings. Explicit local-effect
coverage remains126/186; macro coverage38/87. All77 structural checks pass;
semantic completion remains unproven. No renderer code or gameplay test changed.

### Pool insertion and allocator null-return routes

Six bodies add181 raw-verified instructions. `821D3C30` allocates20bytes, copies
the three-word record, invokes `821D39A0(list,1)`, then links the new node before
the supplied position. `821D42F0` allocates28bytes, copies two payload words,
copies the child list through `821D4248`, invokes `821D3AC8(list,1)`, then links
the outer node. Both conditionally initialize node0 but test node+4 and node+8
separately; a null allocation would still reach low-address stores. Neither
locally returns an allocation-failure status or rolls back an unlinked allocation
if a count/copy helper exits nonlocally. The inner record is copied by value;
outer child-list ownership still depends on `821D4248`.

`820B24A8` returns zero for size0 or a null result from `821E8C20`. On success
it atomically increments global82578650, using reservation retry and interrupt
state save/restore, then returns the pointer. Thus a nonnull guarantee cannot
be inferred from this wrapper. `821E8C20` calls the heap accessor and allocator,
optionally retries via `821F03B0` when global8257C5AC enables that path, and
returns zero after writing error12 through `821EFC18` when retries stop. Requests
above unsigned0xfffff000 also take a failure-handler/error12/null route.
Provider and failure-handler behavior remain external to this local contract.

`8212FB98` calls import8252CF1C, mapped by the generated import table to
`MmAllocatePhysicalMemoryEx`. It passes the requested byte count, flags and
alignment, optionally clears flag0x20000000 under global825787D0, and turns a
specified address into an inclusive min/max range with alignment0. Address-1
uses min0/max-1 and preserves alignment. On a null result it calls `8212F380(8)`
before returning the result if that helper returns. `8212F380` is a single tail
branch to `8212F310`; its error effect is not assumed to throw or terminate.

These verified routes strengthen the missing-null-guard concern in the pool
insertion code. They do not establish a production out-of-memory scenario, the
configured failure-handler result, or a runtime crash. The count helpers and
child-list copy remain transitive effects to review, as does physical error-state
handling. No renderer implementation or runtime experiment changed.

Current totals:167 retained bodies,10,820 verified instructions,637 outgoing
branches and43 indirect sites. There are652 focused outputs for614 unique
functions and706 census functions with focused findings. Explicit local
contracts remain126/186 and macro contracts38/87. All77 structural checks pass;
semantic completion remains unproven.

### Child-list copying, count limits and physical allocation error state

Five additional bodies add 193 raw-verified instructions. Fresh retained Ghidra
exports and the instruction contracts cover `821D4248`, `821D3CC0`, `821D39A0`,
`821D3AC8` and `8212F310`.

`821D4248` constructs a new sentinel and zero count at destination+4/+8, then
uses `821D3CC0` to copy the source range. It does not release an existing
destination list. The range helper allocates new 20-byte nodes and copies three
payload words, including any backing address; it does not copy the backing
allocation. It validates the source iterator owner and end, calls `821D39A0`
before linking each node, and reads the next source link after helper calls.
There is no local rollback, locking, cycle check or destination-owner identity
check. The pointer+4/+8 checks still do not provide a null-allocation guard.
Ghidra shows an uninitialized stack byte passed as r7 by the constructor, but
the reviewed range helper never reads incoming r7; this is not evidence of a
behavioral defect on this edge. Source/destination aliasing and failure during
partial construction remain preconditions to resolve.

`821D39A0` uses limit `0x15555555`; `821D3AC8` uses `0x0CCCCCCC`.
Both test unsigned wrapped `(limit - count) < increment`. The limit path calls
`820A6908`, `820A6818`, `820A0350` and `820A6028`, with conditional temporary
cleanup through `820B2510`. If those helpers return, execution reloads and
increments the count just as on the ordinary path. These bodies contain no
saturation or early failure return. The diagnostic/exception behavior of their
callees remains a transitive contract to review.

`8212F310` returns without writes when Word(r13+336) is nonzero. Otherwise it
stores incoming r3 at Word(r13+256)+352 and returns. There are no calls, retries
or throws in this body. Consequently the previously reviewed physical-allocation
failure edge `8212FB98 -> 8212F380(8) -> 8212F310` conditionally records error 8
and returns; the local error helper does not establish a nonnull allocation
guarantee. Actual allocation-failure reachability remains untested.

Current totals: 172 retained bodies, 11,013 verified instructions, 653 outgoing
branches and 43 indirect sites. There are 657 focused outputs for 619 unique
functions and 711 census functions with focused findings. Explicit local
contracts remain 126/186 across 120 callees, with 60 pending; macro contracts
remain 38/87, with 49 pending. All 77 structural checks pass; semantic completion
remains unproven. No renderer implementation or runtime experiment changed.

### List-limit diagnostic dispatch and termination path

Eleven further bodies add 342 raw-verified instructions. `820A6908` initializes
an inline string (length 0, capacity 15), scans the supplied C string, and
assigns it through `820A62E8`. The bytes at `82002920` read `list<T> too long`.
`820A6818` constructs a diagnostic object containing a copy of that string at
object+8 and finishes with vtable `8200142C`. The string assignment helpers
`820A62E8` and `820A61F0` distinguish inline from allocated buffers, handle
self-source ranges, and delegate resizing and checked copying. Their provider
allocation/error semantics remain separate.

`820A0350` invokes the optional global function pointer at `82578E60` with the
diagnostic object in r3, then reloads the object's vtable and invokes offset 8.
If that returns, it calls `8216B808` again. The global callback can change the
object, so the initial table is not proof of the eventual receiver population.
Raw image words at `82001430` and `82001434` are respectively `820A60A0` and
`820A03F8`: the former returns the embedded message buffer, and the latter
tails `8216B808`. These two verified table targets were added as explicit roots;
the expanded closure also brings in four other functions through table roots.

`8216B808` invokes vtable offset 4 for the message pointer, then calls
`8216B778` with the prefix at `820111F4` (`exception: `). `8216B778` calls the
output helpers for prefix, message (or `unknown` at `82001448`) and the final
string at `82019AA0`, then calls `821EB320`. This last mapped body conditionally
calls diagnostic/signal helpers based on global `82556710`, optionally builds
a stack exception record with code `0x40000015`, and finally calls
`821E9E60(3)`. It contains no ordinary return instruction or epilogue. This
supports a termination-path interpretation, not an established C++ unwinding
contract; the provider and global callback behavior still need resolution.

Ghidra's saved `821eb320.c` continues beyond the mapped body and displays the
next function's write to `82556710` and its return. Those statements are not
local effects of the reviewed body. The generated body ends after the call at
`821EB3CC`, and all its database instructions match the immutable image.
Likewise Ghidra omits the optional callback's r3 argument in `820a0350.c`;
the raw call sequence preserves the diagnostic object there.

If dispatch returns, `820A6028` releases an allocated embedded string through
`820B2510`, resets its inline state and changes the vtable, without freeing the
diagnostic object itself. The count helpers' subsequent increment remains
conditional on their callees returning. No local rollback was added or inferred.
The previous section's four diagnostic addresses and heap-free address have
been corrected from erroneous `821A...`/`821B...` spellings to `820A...`/`820B...`.

Current totals: 183 retained bodies, 11,355 verified instructions, 688 outgoing
branches and 46 indirect sites; 668 focused outputs for 630 unique functions.
The broad inventory contains 9,216 functions from 2,509 roots and 187 selected
tables, with 722 functions carrying focused findings and 8,494 census-only.
All 77 structural checks pass. Explicit effect review remains 126/186 and
macro review 38/87; transitive semantic closure remains unproven. No renderer
implementation or runtime experiment changed.

### Worker registration and render-target lifecycle boundaries

Six bodies add 193 raw-verified instructions. `8243A000` snapshots the worker,
mode, callback and context, invokes worker vtable offset 12, stores a callback
word followed by its context word, reloads the vtable and invokes offset 20,
then returns zero. The raw switch at `8243A050` maps mode 1 to offsets 60/76,
mode 2 to 64/80, mode 4 to 56/72, and every other value (including 3) to 52/68.
It does not call the newly registered callback, validate its address, retire
the previous context or signal work locally.

Both known tables `82064F60` and `82065678` contain registration at slot 14,
`824339F0` at slot 3 and `82433A60` at slot 5. The latter two call
`RtlEnterCriticalSection(worker+16)` and `RtlLeaveCriticalSection(worker+16)`
respectively, then return zero. The raw pointers and switch are reproducibly
checked by `tools/inventory-worker-registration.py`, producing
`out/renderer-inventory/worker-registration-review.json`. This establishes a
locked registration route for these table candidates, not an exhaustive receiver
population or proof that old callbacks have drained. The native audit hook at
`guest_shader_bridge.cpp:2893` forwards in both branches and records registration
inputs only. Ghidra omits the first virtual call's arguments; PPC preserves the
incoming worker in r3 and the other registration arguments there.

`821B8828` snapshots the device from global `8257BFB4`, saves the getter results
in owner+44/+48, binds owner+12 as color target and zero as depth, then sets
owner byte+40. There is no active-flag guard or nesting stack in this body.
`821B88B0` returns immediately when that flag is zero. Otherwise it clears the
flag before resolving through `8213FAF8` into owner+4, restores saved color/depth
bindings, and releases each nonnull saved reference through `821347C0` before
clearing its field. Failure/reentry behavior and transitive resolve/reference
effects remain separate. The native begin hook runs after the original; native
end runs before the original when the flag is set.

`821B8C30` first calls `821B89B0`, writes dimensions and format fields, and
computes single-precision reciprocals without a local zero-dimension guard.
It requests `8213B730(width,height,1,1,0,format,0,3)`. Allocation failure logs
the string at `82019700` and returns zero; success attaches the resource through
`8219D090`, calls `821B8AD8`, and returns whether its low byte is nonzero.
The dimensions are already changed before allocation succeeds, with no local
rollback. The native hook registers the target only after a nonzero result.
Ghidra omits the attachment call's resource argument; PPC explicitly passes it
in r4. Reset, attachment and backing creation remain transitive contracts.

Current totals: 189 retained bodies, 11,548 raw-verified instructions, 710
outgoing branches and 48 CTR branch sites; 674 focused outputs for 636 unique
functions. The broad census remains 9,216 functions, with 724 carrying focused
findings. Explicit local-effect coverage advances to 131/186 sites across 124
callees, leaving 55 pending; macro coverage remains 38/87. All 77 structural
checks pass; semantic completion remains unproven. No renderer implementation
or runtime experiment changed.

### Render-target resource pairs and partial construction

Four additional bodies contribute 180 raw-verified instructions. `821B89B0`
resets the resource pairs at owner+8/+12 and owner+0/+4, in that order, each
guarded by a nonnull resource pointer. For owner+12 it queries `82134840` and
`82134AE8`, conditionally clears color binding 0, and calls `820AFF68(owner+8)`.
That helper decrements the ordinary, non-atomic reference count, calls
`821347C0` and `820B2510` when the count becomes zero, then clears both pair
words. The caller's following count/release check is redundant on an ordinary
return from this helper, since the pair has been cleared.

For owner+4 the same query pair conditionally leads to eight `8213BA98` calls
for indices 0 through 7, with resource zero and masks `1ULL << (43-index)`.
It then decrements/releases the primary pair and clears it. Reset does not
locally clear the active byte+40, saved bindings+44/+48, dimensions or format.
A null resource can leave its control word untouched because of the guard;
valid pair invariants therefore remain relevant. No local locking, GPU drain
or rollback is established.

`8219D090(pair,resource)` decrements/releases the old pair and clears both
words before installing the new resource. A nonnull incoming resource is stored
at pair+4, and `820B24A8(4)` allocates its new reference counter. The function
stores the allocator result at pair+0 and unconditionally loads/increments/stores
through it, even when that result is zero. Fresh Ghidra output and raw instructions
agree on this missing null-failure path. There is no local AddRef of the incoming
resource, same-resource identity check, atomic count or rollback. Passing the
same resource being released requires an independent ownership justification.

`821B8AD8` requests backing through
`8213B850(owner.width,owner.height,Word(owner+20),0,zeroedStackRecord)`.
A null result logs at `82019608` and returns zero. Success attaches the result
at owner+8 using `8219D090`, queries `8213B978`, compares the returned first word
with owner+20, and logs a mismatch at `82019688`. It still returns one after a
mismatch if logging returns. On the `821B8C30` route, the primary texture has
already been attached before this call, and backing failure does not locally
release it. This is a partial-construction contract, not evidence of an observed
runtime allocation failure. Provider release/allocation and eventual caller
cleanup remain open.

Current totals: 193 retained bodies, 11,728 verified instructions, 735 outgoing
branches and 48 CTR branch sites; 678 focused outputs for 640 unique functions.
The 9,216-function census has focused findings on 728 functions and 8,488
census-only rows. Explicit coverage remains 131/186 (55 pending); macro coverage
remains 38/87. All 77 structural checks pass; semantic closure remains unproven.
No renderer implementation or runtime experiment changed.

### Resource release, backing provider and descriptor

Three more bodies add 144 raw-verified instructions. `821347C0` decrements the
resource's own count at resource+4 with an `lwarx`/`stwcx.` retry loop and
interrupt-state save/restore. This is distinct from the non-atomic wrapper
counter reviewed above. A nonzero new count is returned. On zero, a resource
whose header low nibble is 4 and bit `0x40000000` is set first recursively
releases Word(resource+24), then calls `82134220(resource)` and returns zero.
There is no local underflow/null guard or GPU wait; destruction and retirement
semantics still belong to the downstream helper.

`8213B850` allocates a 48-byte header using `8212F420(48,0x64800000)`, calls
`8213B280` to initialize it and output size information, then sets header bit
`0x00100000`. A nonnull incoming layout pointer returns this header without
entering the subsequent local allocation branch. This is the route taken by
`821B8AD8`, which passes a pointer to a zeroed 12-byte stack record. It does not
by itself establish what storage the initializer associates with that record.

For a null layout pointer, the provider calls `82141940(size,&base)`. Failure
frees the header through `8212F4B8(header,0x24800000)` and returns zero. If the
wrapped unsigned base+size exceeds 2048, it first calls `821418F0(base,size)`,
then frees the header and returns zero. Success sets header bit `0x80000000`
and replaces the low 12 bits of header+28 with base. For header+40 low-six-bit
formats 22/23, the first call with global `82578D00` zero also sets that global
to one and masks header+32 to 17 bits. Initializer/allocation and synchronization
contracts remain separate from these verified local writes.

`8213B978` produces a descriptor. Header bit `0x40000000` delegates to
`8213A790(Word(resource+24),Word(resource+28)>>28,out)`. Otherwise it writes
format from resource+40, zeroes output+8/+12/+20, decodes dimensions from
resource+36, and copies the low two bits of the big-endian halfword at
resource+24 to output+16. Both paths set output+4 to 4 after the helper returns.
There is no local reference increment or allocation.

Current totals: 196 retained bodies, 11,872 verified instructions, 744 outgoing
branches and 48 CTR sites; 681 focused outputs for 643 unique functions.
Focused findings cover 730 of the 9,216 census functions. Explicit local-effect
coverage is 132/186 sites across 125 callees, leaving 54 pending; macro coverage
remains 38/87. All 77 structural checks pass; semantic completion remains
unproven. No renderer implementation or runtime experiment changed.

### Backing-header initialization and coverage deduplication

`8213B280` adds one new body and 149 raw-verified instructions. It initializes
header type 4, resource count 1 and header+20 to `0xffff0000`, then calls
`82139BC0` for the size. It writes pitch/mode, dimensions, format and layout
bitfields, header+44 as the low word of size*5120, and the two caller outputs.
Format low-six-bit value 54 uses table index 7; formats 22/23 have special
auxiliary-field handling. Ordinary formats do not locally initialize header+32.

A nonnull layout pointer supplies three words at offsets 0/4/8. A null pointer
uses default base/tile zero and auxiliary -1, except special formats may use
auxiliary zero when global `82578D00` is zero. Thus the zeroed layout passed by
`821B8AD8` selects explicit zero fields and bypasses that default selection.
The body contains no allocator call and does not set header bit `0x80000000`.
The size helper's effects and full native bitfield equivalence remain separate.

The existing `82134220` destruction review was rechecked against the raw
nine-byte dispatch table at `820095C0`. Types 1/2/3/6/7/8/9 conditionally call
`8213C928` with tags 9/11/13/7/8/17/15 before their resource-specific release
paths. Type 4 skips its local backing release when header `0x40000000` is set;
otherwise `0x80000000` controls `821418F0`, and header+32 bit 0 controls clearing
global `82578D00`. All ordinary paths free the header through `8212F4B8`.
This recheck does not add another unique body or prove downstream retirement.

During reconciliation a repeated destruction address was found in this batch's
manifest and Python effects dictionary. Both duplicates were removed. The audit
now independently checks manifest-address uniqueness and dictionary-key
uniqueness, preventing duplicate successful export rows or silently overwritten
contracts from inflating coverage. A fresh Ghidra export after deduplication and
the full pipeline pass all 79 structural checks; semantic completion is false.

Current coverage is 197 retained bodies, 12,021 verified instructions, 747
outgoing branches and 48 CTR sites; 682 focused outputs for 644 unique functions.
Focused findings cover 731 of 9,216 census functions. Explicit local-effect
coverage remains 132/186, with 54 pending; macro coverage remains 38/87. No
renderer implementation or runtime experiment changed.

### Matrix scratch upload and resource-worker traversal

Two retained original bodies add 87 raw-verified instructions. `821A1738`
returns for a null descriptor, otherwise clamps the requested count to the
unsigned descriptor+16 limit and snapshots the destination at descriptor+0.
Each matrix reads source offsets 0/16/32/48, 4/20/36/52 and 8/24/40/56 and
writes twelve consecutive floats. Source advances 64 bytes; destination advances
48. The loads and stores are interleaved, so arbitrary overlapping source and
destination are not covered by an in-place transpose claim. There are no calls,
stack stores, allocations or device dirty-bit writes in this body. The native
hook at `guest_shader_bridge.cpp:4335` keeps this original upload and then
overwrites a bounded destination range with retained poses when source identity
matches the current model context.

`821A4FC0(manager,listOwner)` first invokes offset 32 on the receiver at
listOwner+132. It walks the list at listOwner+2228, repeatedly reloading its
sentinel at listOwner+2232. Each node supplies its object at node+12; the helper
invokes that object's vtable offset 24 with Word(manager+2264) in r4, reloaded
for each callback. It checks the current node against the current sentinel
before and after the callback, and only then reads the next link. Finally it
reloads listOwner+132 and invokes offset 36. The three sites are `821A4FE4`,
`821A5044` and `821A5070`. The self-comparison trap at `821A5000` is unreachable.
Local writes are stack-only; callback effects, receiver populations, lock
semantics and exceptional cleanup remain separate. The native hook forwards
this traversal; its optional 256-record diagnostic snapshot is not a coverage
proof or replacement for the live traversal.

Current totals: 199 retained bodies, 12,108 verified instructions, 749 outgoing
branches and 51 CTR sites; 684 focused outputs for 645 unique functions.
Explicit local-effect coverage is 134/186 sites across 127 callees, leaving 52
pending. Macro coverage remains 38/87. All 79 structural checks pass; semantic
completion remains unproven. No renderer implementation or runtime experiment
changed.

### Material population, copying and clearing

Three retained bodies add 117 raw-verified instructions. `821BC230` calls
`821BBAD8` for the two parameter regions at instance+0 and instance+36, then
`821BA120` using the second source object. Each low-byte failure returns zero
immediately, without local rollback of earlier helper writes. Success copies
source+8 into instance+96 through `821B5C70`, stores the source pointer at
instance+108 and returns one. The source pointers are reloaded between helpers.
An early failure does not locally clear the old instance+108 value. The native
hook retires previous parameters before the original and publishes only when
the low-byte result succeeds.

`821BD110` copies the two parameter regions using `821BCF98` at offsets 0/36
and `821BC588` at offsets 12/24/48/60. It then calls `821BC8C0` for offset 72
and `821B5C70` for offsets 84/96, copies Word(source+108) to destination+108,
and returns the destination. It has no local failure checks, alias guard or
rollback. Deep-copy and allocation behavior still belong to the helpers.
Its native hook retires destination parameters before forwarding and publishes
unconditionally after an ordinary return.

`820ABE88` clears the arrays at offsets 96, 84 and 72 in that order. For each
nonnull first word it calls `820B25B8`, then clears the pointer, offset+8 and
offset+4 words. A null pointer bypasses those count/capacity resets. It then
calls `820AB928(instance+36)` and `820AB928(instance)`. There is no local
instance+108 reset or object free. The native hook retires parameters before
forwarding. Contained-object destruction and final owner lifetime remain open.

Current totals: 202 retained bodies, 12,225 verified instructions, 769 outgoing
branches and 51 CTR sites; 687 focused outputs for 648 unique functions.
Explicit local-effect coverage advances to 137/186 sites across 130 callees,
leaving 49 pending; macro coverage remains 38/87. All 79 structural checks pass;
semantic completion remains unproven. No renderer implementation or runtime
experiment changed.

### Material-array copy helpers and region cleanup

Five additional bodies contribute 159 raw-verified instructions. The four copy
helpers snapshot source+8 as the element count, call a resize helper, and skip
copying if its low byte is zero or the count is zero. All four nevertheless
return the destination pointer; none propagates a resize-failure status.

| Copy helper | Resize helper | Per-element operation |
|---|---|---|
| `821BCF98` | `821BCA50` | 16-byte record: copy first word, then call `8211A2C0` for the field at +4 |
| `821BC588` | `821BC2D8` | Four sequential word load/store pairs, 16-byte stride |
| `821BC8C0` | `821BC618` | Seven sequential word load/store pairs, 28-byte stride |
| `821B5C70` | `8210ACE8` | Two sequential word load/store pairs, 8-byte stride |

Each reloads source and destination base pointers for each element. The plain
word-copy paths do not locally clone allocations referenced by copied words or
increment their references. The embedded field in `821BCF98` has a separate
helper contract. Resize failure, aliasing, allocation and contained-resource
ownership remain separate; no local rollback exists in these copy bodies.
This explains why the enclosing `821BD110` destination return cannot be treated
as a successful deep-copy status.

`820AB928` clears region arrays at offsets 24 then 12 using `820B25B8` for
nonnull pointers, resetting each pointer and its two control words only on that
branch. It then calls `820AB650(region)` for the first array. A null pointer
leaves its other control words unchanged. The body does not free the enclosing
object, and first-array contained destruction remains delegated.

Current totals: 207 retained bodies, 12,384 verified instructions, 785 outgoing
branches and 51 CTR sites; 692 focused outputs for 653 unique functions.
Focused findings cover 736 of 9,216 census functions. Explicit coverage remains
137/186 and macro coverage 38/87, with 49 pending in each ledger. All 79
structural checks pass; semantic completion remains unproven. No renderer
implementation or runtime experiment changed.

### Material storage replacement and nested-array destruction

Six bodies add 241 raw-verified instructions. `821BC2D8` and `8210ACE8`
free a nonnull old pointer before allocating replacement arrays with 16-byte
and 8-byte strides respectively. Counts above `0x0fffffff` and `0x1fffffff`
are passed to `820B2550` as an allocation request of `0xffffffff`. Success
stores the requested count at owner+8/+4 and returns one; null allocation
stores a null pointer and returns zero. No element initialization or old-data
preservation occurs. When the old pointer is already null, old count words are
not cleared before allocation and may remain after failure.

`821BC618` applies the same replacement sequence with 28-byte records and
limit `0x09249249`. It initializes record+0/+4 to zero, float+12 from
`820009A4`, and words+16/+20/+24 to one. Record+8 remains unwritten locally.
`821BCA50` first calls `820AB650`, allocates a four-byte count prefix followed
by 16-byte records, and initializes only record+4/+8/+12 to zero. Record+0
remains unwritten. Its overflow checks also substitute `0xffffffff`; a zero
count still requests the four-byte prefix. None of these bodies restores old
storage after failure or provides local locking.

`820AB650` takes the destruction count from the allocation prefix at data-4,
not owner+8. It visits records in reverse, freeing each nonnull embedded array
at record+4 through `820B25B8` and clearing that array's three control words.
It then frees the prefixed allocation and clears owner+0/+8/+4. An initially
null data pointer returns without count clearing. There is no local validation
that the prefix count and allocation extent agree.

`8211A2C0` copies the embedded array by calling `8211A018` for its snapshotted
count, then copying each 16-byte element as two sequential doubleword pairs.
It returns the destination even if resizing fails. The resize provider and
pointer-valued element ownership remain separate. Combined with the previously
reviewed outer copies, the release-before-allocation ordering makes source/
destination aliasing a caller obligation; an arbitrary self-copy safety claim
would be unsupported.

Current totals: 213 retained bodies, 12,625 verified instructions, 803 outgoing
branches and 51 CTR sites; 698 focused outputs for 659 unique functions.
Focused findings cover 742 of 9,216 census functions. Explicit coverage remains
137/186 and macro coverage 38/87. All 79 structural checks pass; semantic
completion remains unproven. No renderer implementation or runtime experiment
changed.

### Array allocation, zero-count behavior and release errors

Four bodies add 116 raw-verified instructions. `8211A018` releases old storage
before allocating a replacement array of 16-byte records. It initializes each
record to the floats at `820009A4` for XYZ and `820008CC` for W, then publishes
the pointer and count words on success. Oversized counts become a request of
`0xffffffff`. It has no rollback or old-data preservation.

`820B2550` returns zero immediately for a zero-byte request. Otherwise it calls
the previously reviewed `821E8C20` heap provider and increments global
`82578654` atomically only for a nonnull result. This array counter is distinct
from scalar allocation counter `82578650`. Consequently zero-count requests
to the plain material-array recreators return failure after any old storage
has been released. The prefixed `821BCA50` array remains different: zero
elements still request four bytes for its prefix.

`820B25B8` routes a null pointer to diagnostic `8219F7A0(8200209C)` without
changing the counter. A nonnull pointer atomically decrements `82578654`, then
tails `821E8CE8`. That provider queries the heap through `82132F38` and calls
`82132330(heap,0,pointer)`. A zero result obtains an error via `8212EAB8`, maps
it through `821EFBB0`, and stores it through the address returned by
`821EFC18`. There is no local counter restoration, retry or GPU wait. These
facts define bookkeeping and failure ordering; they do not prove allocation
failure reachability or the external heap implementation's lifetime guarantees.

Current totals: 217 retained bodies, 12,741 verified instructions, 816 outgoing
branches and 51 CTR sites; 702 focused outputs for 662 unique functions.
Focused findings cover 745 of 9,216 census functions. Explicit coverage remains
137/186 and macro coverage 38/87. All 79 structural checks pass; semantic
completion remains unproven. No renderer implementation or runtime experiment
changed.

### Gamma and shader-load packet emission

Three original bodies add 161 raw-verified instructions. `82142050` reserves
2309 words through `8213D160`, emits five setup words and 256 nine-word records,
reading three 256-entry big-endian halfword planes from the source. `82142130`
reserves 1413 words, emits five setup words and 128 eleven-word records, reading
pairs of halfwords from each of the three planes. The contracts retain the
packet constants and pair ordering; both finally store the cursor at device+40.
Their local non-stack writes are packet-buffer stores and this cursor update.
Reservation side effects and source lifetime remain separate. The native-host
hooks capture the gamma source and invoke their CPU tails; fallback invokes
the corresponding original body.

`8213EAB0` reads the optional program section offset at program+20. A nonzero
offset supplies a section with a byte length at +24 and records beginning at
+32. Each eight-byte record contains a halfword index, halfword count and address
offset. Count zero terminates traversal. Otherwise it adds the incoming base
address, calls `8213CF60` if device+40 exceeds device+48, emits four words using
the device+12440 header and encoded address/index/count, and publishes device+40.
There is no local table-bound validation beyond the section end comparison.
Its local non-stack writes are also packets and cursor only; capacity-helper
effects remain transitive. The native hook omits this body only when
`OwnsShaderLoadPackets(device,LR)` succeeds, forwarding to the original otherwise.

Current totals: 220 retained bodies, 12,902 verified instructions, 823 outgoing
branches and 51 CTR sites; 705 focused outputs for 665 unique functions.
Explicit local-effect coverage is 140/186 sites across 133 callees, leaving
46 pending. Macro coverage remains 38/87. All 79 structural checks pass;
semantic completion remains unproven. No renderer implementation or runtime
experiment changed.

### Tree cleanup, world destruction and object refresh

Four bodies add 139 raw-verified instructions. `820B5F38` clears the range
between owner+52 and owner+56 through `820B5EB8`, frees the outer allocation,
zeros owner+52/+56/+60, and calls `821AD278`. The range helper walks 32-byte
level records. Each nonnull inner array contains 144-byte roots whose node at
+120 is unlinked through `821A1678` before the array is freed. It then clears
the level's +20/+24/+28 pointers. It uses an equality-terminated range with no
local extent validation. The caller passes an uninitialized stack byte as r6,
but this helper reads neither incoming r5 nor r6. The native owner-clear hook
retires its tree publication before forwarding.

`820B5FA8` sets vtable `82002624`, clears global `82578678`, unlinks owner+372,
and calls cleanup helpers for fields at +328/+308/+288/+240/+224/+208. It
conditionally frees the string buffer at +144, resets its length/capacity and
first halfword, cleans +128/+112, then calls `820B5F38`. There is no local
enclosing-object free, lock or rollback. The native hook retires both the tree
publication and world-animation metadata before this original destructor.
Contained helper lifetimes remain separate contracts.

`820B2DF8` is an object-refresh path. It calls `821C0D70(owner,0)`, conditionally
passes the nonnegative value at owner+400 to `820B2AC0` and sets that field
to -1. For a nonzero count at +404 it visits 44-byte records starting at +408,
calling `821BEDF0(record,owner+224)` and reloading the count after each call.
It finishes through `821C1120`. There is no local bound on a negative or
changing count. The native hook forwards first, then publishes world/visibility
and updates the scene adapter only in queued mode for a known source generation.

Current totals: 224 retained bodies, 13,041 verified instructions, 848 outgoing
branches and 51 CTR sites; 709 focused outputs for 669 unique functions.
Explicit coverage advances to 143/186 sites across 136 callees, leaving 43
pending. Macro coverage remains 38/87. All 79 structural checks pass; semantic
completion remains unproven. No renderer implementation or runtime experiment
changed.

### Membership-mode transitions and aggregate bounds

Three bodies add 162 raw-verified instructions. `821C0D70` returns immediately
if owner+72 already equals the requested mode. Otherwise it writes the new mode
before tailing either `821A4160(Word(owner+32),owner)` for mode 1 or
`821A1678(owner+120)` for every other mode. Thus a same-mode request does not
repair list membership, and tail failure can follow the mode write.
`821BEDF0` is a three-instruction forwarding wrapper to
`821BED40(record,Word(record+28),matrix)`, with no local stores or null guard.

`821C1120` zeroes owner+208, snapshots the list start at +144 and end at +156,
and returns with old bounds intact if the start is null. For each node it calls
the object at node+8 through vtable offset 4 at `821C11C0`, reloads node+8,
and reads that object's center at +48 and extent at +64. It accumulates component
minima of center-minus-extent and maxima of center-plus-extent using the PPC
floating comparison branches. The next link is read after the callback.

After traversal it writes the visited count at owner+208, center at +160,
extent at +176, both W lanes as one, and the result of `821B03C8(owner+176)`
at +192. Center and extent use the original single-precision sum/difference
and half-scale ordering. A nonnull start already equal to end still writes
bounds derived from the initial sentinel float values, while leaving count zero.
Callback receiver identity, mutation, NaN behavior and list validity remain
necessary preconditions; there is no local lock or cycle guard.

Correction to the preceding refresh description: inspection of `820B2AC0`
shows mode-dependent construction of records from owner+368 data, count writes
at +404 and optional resource creation. Calling it an indexed removal was not
supported and has been corrected. Its complete contract remains to be reviewed.

Current totals: 227 retained bodies, 13,203 verified instructions, 856 outgoing
branches and 52 CTR sites; 712 focused outputs for 671 unique functions.
Focused findings cover 748 of 9,216 census functions. Explicit coverage remains
143/186 and macro coverage 38/87. All 79 structural checks pass; semantic
completion remains unproven. No renderer implementation or runtime experiment
changed.

### Mode-dependent model and collision reconstruction

`820B2AC0` adds 206 raw-verified instructions and completes the local review
behind the previous wording correction. It first zeroes owner+404. Mode zero
looks up UTF-16 key `model` at `820021C4`, publishes its count at owner+404,
and iterates 12-byte source records into 44-byte destination records beginning
at owner+408. It follows relative offsets to each name, calls `821C07B8` using
the world at global `82578678`, releases temporary string storage, copies a
source float into record+40 and transforms through `821BEDF0`. Source data and
the destination count are reloaded during traversal, with no local destination
capacity check.

The nonzero-mode path instead looks up `breakmodel` at `82002198`, constructs
one destination record, stores the float at `82002194` into owner+448,
transforms it and sets owner+404 to one. Optional collision lookup uses
`collision` at `820021B0` for mode zero and `breakcollision` at `82002174`
otherwise. The two routes follow different relative-offset layouts and require
a type-4 node with a nonzero data offset.

The common path transforms bounds through `821BEF10` if the count is nonzero,
then calls `821C1080`. If collision data exists it allocates 192 bytes, calls
`821AE980`, and links a successful object through `820D4720(owner+144,object+28)`.
It then calls `821AF6C0(objectOrZero,data,owner+224,0)` even if allocation or
construction failed. This is a verified null-argument path, not a claim that
the callee crashes. Finally it calls `821C1120` to recompute aggregate bounds.
There is no local rollback or explicit old-record release; helper ownership,
valid input extents and collision-initializer null handling remain open.

Current totals: 228 retained bodies, 13,409 verified instructions, 884 outgoing
branches and 52 CTR sites; 713 focused outputs for 672 unique functions.
Explicit coverage advances to 144/186 sites across 137 callees, leaving 42
pending. Macro coverage remains 38/87. All 79 structural checks pass; semantic
completion remains unproven. No renderer implementation or runtime experiment
changed.

### Model registry references and collision attachment

Two bodies add 248 raw-verified instructions. `821AF6C0` calls
`821D7110(data,0)` before checking the collision header for `0x00435353` and
version 258. Invalid headers log and return zero; an index at or beyond the
signed count also returns zero. There is no independent negative-index guard.
An accepted entry is data+Word(data+12)+index*12. The function writes that
pointer at object+176, conditionally grows the shared structure at `8257C078`,
stores the supplied matrix pointer at object+184 and the entry-relative payload
pointer at +180, then returns one.

There is no object-null check, data copy or reference increment locally.
Consequently the null-object route from `820B2AC0` reaches a write at address
176 when its header/index pass validation. This establishes the static failure
path, not runtime allocation-failure reachability. Invalid data may return first.
The data and matrix are borrowed pointers whose lifetime must be established
by their owners; conversion and shared-capacity helper effects remain separate.

`821C07B8` resets the destination through `821BFF38`, constructs and normalizes
a temporary path, and looks it up through `821C4660`. It stores registry and
lookup result at record+32/+36. A missing result logs, resets the record again,
cleans its temporary strings and returns zero. Success prepares destination
storage through `821C0618`, then walks the result's list and fills 28-byte
destination elements. Each element receives a pointer to source node+8 and a
self pointer. Bounds and live-sentinel checks trap on invalid positions, but
there is no deep clone of source node payloads.

It then looks up an eight-byte key through `821C0048`, stores the result at
record+28, cleans temporary strings and returns one without testing that final
result for null. The enclosing `820B2AC0` ignores this constructor's return.
Reset, storage preparation, registry source lifetimes and caller handling of
partial construction remain open.

Current totals: 230 retained bodies, 13,657 verified instructions, 915 outgoing
branches and 52 CTR sites; 715 focused outputs for 674 unique functions.
Focused findings cover 750 of 9,216 census functions. Explicit coverage remains
144/186 and macro coverage 38/87. All 79 structural checks pass; semantic
completion remains unproven. No renderer implementation or runtime experiment
changed.

### Tree-node initialization and copying

Three bodies add 106 raw-verified instructions. `821C4EB8` initializes four
vectors at node+0/+16/+32/+48 to XYZ zero and W one. It clears the embedded
links at +120/+124 through `821D1880`, clears +128/+132, occupancy+116,
field+80 and eight child pointers at +84 through +112. It returns the node.
It does not locally initialize +64, +68 through +76 or +136 through +143,
nor unlink existing neighbors or free old children. The untouched radius field
at +64 therefore needs its later producer; this alone is not a runtime defect.

`821C5D28` copies the first 64 bytes through sequential doubleword pairs, then
float+64, word+80, the eight child pointers and occupancy+116. It resets
destination links +120/+124, clears +128 and only then reads source+132 into
destination+132. Child pointers are copied verbatim, without cloning their
referents or incrementing references locally. It leaves +68 through +76 and
+136 through +143 untouched. Source/destination overlap and use on an already
linked destination require caller invariants.

`821D1880` only zeroes its two link words; it does not unlink neighboring nodes.
Both native tree hooks invalidate publication before calling the originals.
After an ordinary return, queued visibility mode registers a new membership
node at +120, with the copy hook also supplying the copied +132 value.

Current totals: 233 retained bodies, 13,763 verified instructions, 919 outgoing
branches and 52 CTR sites; 718 focused outputs for 676 unique functions.
Explicit coverage advances to 146/186 sites across 139 callees, leaving 40
pending. Macro coverage remains 38/87. All 79 structural checks pass; semantic
completion remains unproven. No renderer implementation or runtime experiment
changed.

### Declaration construction and byte-fill provider

Two bodies add 92 raw-verified instructions. `82149AB0` scans 12-byte input
elements until the leading big-endian halfword equals 255. It counts elements,
tracks the maximum stream number, and marks a byte at stackMask+stream in a
zeroed 16-byte mask. There is no local stream<16 check, scan bound or count
overflow guard. It allocates count*12+56 bytes, returns zero on allocation
failure, otherwise clears 56 header bytes and installs the count, maximum stream,
mask, initial reference count and resource header. `821E8320` copies only the
element records to header+52; the terminator is not copied. Empty input leaves
maximum stream zero. Valid stream indices and a reachable terminator therefore
remain caller preconditions. The native hook forwards then publishes the returned
declaration handle through `PublishNativeDeclarationContents`.

`821E9BA0` fills leading bytes until four-byte alignment or exhaustion, replicates
the low byte of the value into a word, writes 16-byte blocks, then up to three
words and three final bytes. It preserves r3, performs no loads, calls, stack
writes or allocations, and writes exactly the requested ordinary valid,
nonwrapping extent. Zero count writes nothing.

Ghidra incorrectly represents the conditional LR returns at `821E9C2C` and
`821E9C34` as unrecovered indirect calls, producing four warnings. The raw PPC
instructions are `bdzlr`, not callback dispatch. The local contract excludes
those invented calls and retains the actual byte-tail behavior.

Current totals: 235 retained bodies, 13,855 verified instructions, 924 outgoing
branches and 52 CTR sites; 720 focused outputs for 678 unique functions.
Explicit coverage advances to 148/186 sites across 141 callees, leaving 38
pending. Macro coverage remains 38/87. All 79 structural checks pass; semantic
completion remains unproven. No renderer implementation or runtime experiment
changed.

### Forward and backward bulk-copy boundaries

Two bodies add 89 raw-verified instructions. `821E8740` is an independent
forward copy. It writes a byte prefix to align the destination to four bytes,
then copies words and final bytes. An unaligned source is read as four bytes
in reverse offset order, assembled into a big-endian word before the store.
It preserves the destination return register and has no calls, stack writes,
allocation or locking. Overlap behavior follows the sequential loads/stores;
the ordinary nonoverlapping extent is copied exactly.

`821EA320` compares addresses as signed 32-bit values. Equal addresses return
without writes; a smaller destination tails `821E8320`; a larger destination
copies backward inline. It aligns the destination end with byte pairs, then
copies words or assembled unaligned words, and finishes with backward byte
pairs. The ordinary backward return reaches the original destination address.
The signed comparison matters when reasoning about ranges across the sign
boundary; this review does not assert arbitrary-address memmove equivalence.

The native hooks save the original destination/count, begin write tracking
before execution and notify completion afterward. The move hook tracks only
its inline backward branch; the forward branch reaches the existing `821E8320`
hook and equal-address calls have no write. Count-register changes therefore
do not shorten the reported write extent.

Current totals: 237 retained bodies, 13,944 verified instructions, 925 outgoing
branches and 52 CTR sites; 722 focused outputs for 680 unique functions.
Explicit coverage advances to 150/186 sites across 143 callees, leaving 36
pending. Macro coverage remains 38/87. All 79 structural checks pass; semantic
completion remains unproven. No renderer implementation or runtime experiment
changed.

### Shader-record and material deleting destructors

Two bodies add 123 raw-verified instructions. `820AB2F8` returns unchanged for
a null owner data pointer. Otherwise it reads the count at data-4, visits
72-byte shader records in reverse through `821B5DC0`, frees the prefixed array
through `820B25B8`, then zeroes owner+0/+8/+4. It does not validate the prefix
count or extent locally. The native hook calls `ForgetOwner` before forwarding
when the shader bridge is enabled; contained shader destruction remains a
separate helper contract.

`820ABF20` selects array destruction when flags mask 2 is set. It reads the
prefix count at pointer-4 and visits 112-byte material elements in reverse.
For each element it clears nonnull arrays at +96/+84/+72/+60/+48, destroys
the region at +36 through `820AB650`, clears +24/+12, then destroys the region
at +0. Array clears free storage then reset pointer, offset+8 and offset+4;
null pointers skip those resets. Element+108 is not cleared locally. If flags
mask 1 is also set, it frees the prefixed allocation; both array branches return
the prefix address, even if freed.

Without flags mask 2 it calls `820ABE88(pointer)`, optionally frees the scalar
object through `820B2510` under mask 1, and returns the original pointer. There
is no local rollback or lock. Prefix integrity, aliasing and contained helper
lifetimes remain caller/provider obligations. The native hook retires material
parameters with the corresponding array flag before forwarding.

Current totals: 239 retained bodies, 14,067 verified instructions, 944 outgoing
branches and 52 CTR sites; 724 focused outputs for 682 unique functions.
Explicit coverage advances to 152/186 sites across 145 callees, leaving 34
pending. Macro coverage remains 38/87. All 79 structural checks pass; semantic
completion remains unproven. No renderer implementation or runtime experiment
changed.

### Texture allocation and binding retirement

Two bodies add 166 raw-image-verified instructions. `8213B730` allocates a
52-byte header through `8212F420(52,0x64800000)`, then calls `8213AF70` with
the header and two stack size outputs. It allocates the primary payload and,
when the second size is nonzero, an auxiliary payload. Both payload requests
use flags `0x8c800000 | ((2 | ((~usage >> 2) & 1)) << 28)`.
Primary allocation failure frees the header with flags `0x24800000` and
returns zero. Auxiliary allocation failure frees the header first, then the
primary payload with flags `0xb1800000`, and returns zero. Success merges the
page-aligned payload addresses into header+32/+48, preserving their low twelve
bits. No local validation or exception rollback is present; layout and allocator
contracts remain separate. The native hook at `guest_shader_bridge.cpp:7656`
always forwards, then records creation metadata for a successful result when
enabled, invalidating any old texture entry under the state mutex.

`8213BA98(device,index,resource,dirtyMask)` snapshots the old pointer from
device+12272+4*index. A nonnull new resource supplies six packed cache words
at device+1024+24*index, preserving selected fields from the old cache. Its
low mip nibble is the maximum of resource+44 bits2..5 and the device byte at
11678+index; the high mip nibble is the minimum of resource bits6..9 and the
byte at11704+index. Both results are masked to nibbles. Only this nonnull
branch ORs the requested 64-bit mask into device+16.

The new pointer is stored before processing the previous resource. If the
previous pointer is nonnull and device+10780 is nonzero, that value is stored
at oldResource+8. Otherwise, when device+10784 intersects oldResource+0,
an eight-byte retirement record is appended through device+13148. Exhausted
capacity invokes `82141440`. The record is `[oldResource>>2 | priorStackBit31,
0xffffffff]`, and the cursor advances by eight. The stack tag bit is read from
newSP+80 without local initialization. A null binding leaves cached descriptors
and the dirty mask untouched while still processing the old resource. There
is no local index validation, same-pointer guard, AddRef, release or lock.

The native hook at `guest_shader_bridge.cpp:7853` replaces this body only when
both shader-bridge and material-activation settings are enabled. It explicitly
passes the prior stack word at entrySP-80 to `SetNativeTextureResource` and
supplies a guest `82141440` callback with a matching 160-byte stack adjustment.
Thus the stack bit is an observed input to preserve, not grounds to replace it
with zero. Queue consumption, allocator lifetime and full native equivalence
remain separate obligations.

Current totals: 241 retained bodies, 14,233 verified instructions, 955 outgoing
branches and 52 CTR sites; 726 focused outputs for 684 unique functions.
Explicit partial local-effect coverage is 155/186 sites across 147 callees,
leaving 31 pending; macro coverage remains 38/87. All 79 structural checks
pass; semantic completion remains unproven. No renderer implementation changed.

### Pixel and vertex shader binding defaults

`82149608` and `821498C8` add 226 raw-image-verified instructions. They process
the previous shader resource before writing the new binding at device+12416
(pixel) or +12420 (vertex). Retirement uses the same device+10780 immediate
tag or device+10784/header predicate and eight-byte queue record as texture
binding. Their 128-byte stack frames place the preserved retirement tag at
entrySP-48; exhausted queue capacity calls `82141440`. There is no local
same-pointer guard, reference increment or lock.

Even a null shader writes the binding and marks state dirty. Pixel binding
ORs bits52 then49 into the 64-bit word at device+16 in two stores. Vertex
binding clears bit7 of byte10810 and ORs bit51 into device+16. For nonnull
shaders, the defaults section begins at shader+40 (pixel) or +872 (vertex).
A nonzero relative word at section+20 locates a block whose first 64-bit mask
clears device+8 (pixel) or +0 (vertex). A nonzero 64-bit block+8 additionally
sets bit1 in device+24.

The defaults stream starts at block+32, with byte extent from block+24:

- Phase one skips eight-byte entries while their second halfword is nonzero;
  a zero entry consumes its four-byte header and ends this phase.
- Phase two reads halfword destination-offset/count headers, then copies
  count*4 payload bytes to device+1024+offset through `821E8320`. A zero count
  ends the phase after consuming the header.
- Phase three reads the same header shape but consumes count/2 AND/OR pairs,
  writing `(old & keep) | value` to successive destination words. The actual
  loop subtracts two modulo65536 until zero, without an inner extent check or
  even-count validation. An odd count cannot reach zero by that arithmetic.

These bodies perform outer cursor/end checks but do not independently validate
copy payload extents, destination offsets or the mask-loop count. Valid stream
shape is therefore a provider obligation; malformed-stream behavior is not
established as equivalent to the native parser.

The hooks at `guest_shader_bridge.cpp:7871,7878` select replacement only when
both shader-bridge and material-activation settings are enabled, and then
publish the binding when the bridge is enabled. `BindNativeShaderResource`
at7756 supplies the matching retirement tag and guest allocator stack frame.
`native_shader_binding.h:50` first calls `ReadNativeShaderDefaults`, which
snapshots all defaults into host storage and validates extent, alignment and
counts before retirement or binding writes. The original reads defaults after
those mutations and applies payloads as it walks. Aliasing, concurrent mutation
and exception ordering therefore remain explicit parity obligations. With a
nonnull shader but zero relative defaults offset, the native helper also
reads/writes the unchanged constant mask while the original skips that write.
Pixel dirty-bit stores are combined in the native helper. These observations
identify differences requiring assessment, not proof of a reachable defect.

Current totals: 243 retained bodies, 14,459 verified instructions, 963 outgoing
branches and 52 CTR sites; 728 focused outputs for 686 unique functions.
Explicit partial local-effect coverage is 157/186 sites across149 callees,
leaving29 pending; macro coverage remains38/87. All79 structural checks pass;
semantic completion remains unproven. No renderer implementation changed.

### Original main derived-state body

`8213ECB0` adds 434 raw-image-verified instructions to the retained-body ledger.
This review supplements the existing extracted CPU-tail contract. The original
snapshots vertex shader device+12420, pixel shader+12416 and declaration+11536,
then dereferences vertex metadata without a local null guard. The selected
vertex metadata is shader+872+Word(shader+896), with an alternate from +904
when the no-pixel path sees shader+872 flag0x20. Pixel metadata comes from
shader+40+Word(shader+64).

Direct CPU destinations are device+10452 (low-three-bit mode), byte10810
(metadata-derived bit3 and cache-selection bit6), +10408 (pixel metadata),
+12424 (effective recording mode), and +10400/+10404 (combined shader metadata).
Without a pixel shader, mode becomes5; a pixel reload selects mode4. These
mode changes also set dirty bits51 and3 in the returned mask. Pixel metadata
can set byte10810 bit3 without clearing an already-set bit. Program selection
and cache results govern bit6. This body does not directly store the resulting
mask back to device+16; it returns the modified entry-r4 mask, or the result
of `8213D750` when dirty bit49 invokes that helper.

The original has additional packet effects. Pixel reload under dirty bit52
calls `8213EAB0`, emits a three-word program packet and sets dirty bit46.
Vertex state routes through program-load `8213EAB0`, cache `8213EB68` and
upload `8213E950` according to dirty bits51/52, metadata compatibility,
declaration presence and device flags. Under device byte10808 mask0x40 the
late region emits a recording preamble, optional mode/state/program packets,
and restores recorded values from device+12432/+12436. That route clears
dirty bit3 and, when bit48 was set, bits47/48. Outside recording mode, the
late region emits only the selected optional vertex-program packet. Inline
packet paths call `8213CF60` when cursor+40 exceeds limit+48 and commit the
new cursor to device+40.

`OwnsMainStatePackets` selects the extracted CPU tail in the native hook at
`guest_shader_bridge.cpp:10011`; otherwise the original executes. The extractor
in `tools/extract-native-main-state.cmake` checks the original body fingerprint,
preserves CPU dirty-mask transitions, removes program-load/inline packets and
directly routes cache, upload and derived-state calls to their extracted
adapters. The original-body effect row remains separate from that adapter row.
Metadata validity, transitive helper ownership, exact packet consumption and
native equivalence remain open; this review does not make the original safely
removable. The retirement allocator inspected at the start of this batch was
already covered in the earlier static-pass section and was not counted again.

Current totals: 244 retained bodies, 14,893 verified instructions, 976 outgoing
branches and52 CTR sites;729 focused outputs for687 unique functions. Explicit
partial local-effect coverage is158/186 sites across150 callees, leaving28
pending; macro coverage remains38/87. All79 structural checks pass; semantic
completion remains unproven. No renderer implementation changed.

### Scene setup, model matrix upload and camera derivation

Three bodies add247 raw-image-verified instructions. `821BE8D0` copies scene
480..503 to a temporary viewport, converts floats488/492/496/500 to integer
coordinates and dimensions using `fctidz` plus low-word stores, then supplies
depth endpoints1/0. It calls `821371D0` with the global graphics device and
optionally `821340D0` for clear flags48/color0xff000000/depth0. It then uploads
scene+32/+96 through `821A17F8`/`821A19F0`, calls `82135530(device,1)` and
returns1. Its local stores are stack-only. The native hook selects published
camera inputs or a live fallback and always forwards to the original.

`821C9C20` validates its iterator with trap22 and traverses52-byte model
records. In the unskinned branch every record selects a64-byte matrix by
record+44, checks vector bounds, uploads through `821A17D8`, then calls
`821B2C28`. In the other branch it first validates and uploads the whole
matrix vector through `821A1738`, then snapshots the model-record range.
Only records with byte+48 zero receive the additional single-matrix upload;
every record is drawn. Globals and matrix-vector bounds are reloaded per item.
Its local stores are also stack-only; upload and draw effects are transitive.
The interpolation hook still executes this original traversal while scoped
host poses override the two upload destinations, preserving source bones.

`821CDDF8` derives projection at scene+32 from field-of-view+480, width/height
+496/+500 and image constants0.1/1000. Its first aspect computation uses
double division followed by single rounding. It copies world+416 to view+96,
negates translation XYZ, calls `821C8750` and `821B0130`, copies view to+160
and combines it with projection through `821C8198`. It copies world to+224,
calls `821C26C0` at+288 using the reciprocal aspect via `fdivs`, and copies
float+484 to+400. There is no local zero-dimension or finite-value check.
The native interpolation hook only substitutes pose/FOV at416..483 for the
audited producer return address821A4EB0 and unlocked divisor1; scoped cleanup
restores those authoritative inputs after deriving outputs. Matrix helper
semantics and publication lifetime remain separate obligations.

Current totals:247 retained bodies,15,140 verified instructions,998 outgoing
branches and52 CTR sites;732 saved focused outputs for687 unique functions.
Explicit partial local-effect coverage is165/186 sites across153 callees,
leaving21 pending; macro coverage remains38/87. These bodies were already in
other focused analysis sets, so unique decompilation coverage is unchanged.

## Remaining implementation batches

Implementation resumed on2026-09-22 from the reviewed shader setter contract.
`SetNativeShaderResource` now retires the old resource and writes binding/dirty
state before reading defaults, matching the ordering observed in82149608 and
821498C8. A regression fixture aliases an old-resource retirement fence with
the new defaults' constant-clear mask: the pre-change implementation failed
with `shader defaults sampled before retirement`; the corrected helper clears
the expected bit. It also preserves the two pixel dirty stores and skips the
constant-mask write when the relative defaults offset is zero. Targeted tests
cover those effects and binding state at a parser-validation failure.

This closes specific observed ordering differences, not all shader parity
obligations. Default payloads are still collected before application, and the
native parser still rejects malformed extents/alignment/counts. Aliasing with
subsequent constant writes and invalid-input behavior remain distinct from
the original. Full executable validation is recorded separately from the
baseline gameplay run, which began before this change.

| Batch | Concrete boundary | Current evidence and remaining work | Completion gate |
|---|---|---|---|
| Callback closure | `821A5080`, `821A3BA0`, `820B4310`, `820D3FD0` | Native outer traversal still invokes world, bucket, overlay and presentation callbacks. The emitted outer helper has eight indirect call sites. Resolve each receiver/vtable slot and enumerate relevant implementations, including unsupported modes. | Every displayed-frame callback has a native owner or an explicit producer-only role; no unresolved callback silently enters the original renderer. |
| Producer state | `820B4250`, `821A4DE8`, `821CDDF8` | Ghidra shows `820B4250` increments owner+364 and advances float owner+356 while uploading globals. Camera matrices are published, but their original producer/interpolation remains. Separate simulation mutations from render-only work. | Render repeats at independent cadence do not advance simulation, effects, camera authority or resource lifetimes. |
| Complete static selection | `821C61D8`, `820B4038`, `821BEE68`, `821C3BB8` | Native hierarchy/source/LOD selection exists; current work attaches list membership and hierarchy to the same publication. Hidden/mode fields, duplicate marks, unknown callbacks and mutation fallbacks remain live. Resolve their writers and publish their state. | Selection consumes one retained generation with correct visibility, LOD, ordering, removal and address reuse, without live game-memory selection reads. |
| Static pass execution | `821D96D8`, `821B94E8`, `821B8E48`, indexed CPU tail | Published geometry/material/world draws work, including immutable selection. Per-group CPU geometry/material replay and draw-tail side effects remain. Classify every replay write and resource-retirement obligation before moving it out of render execution. | A complete static pass renders from retained inputs without legacy setup, activation or draw submission. |
| View and pass boundaries | `821BE8D0`, `821BE9D8`, target begin/end paths | Owned inherited viewport/targets exist; first-pass inputs still import current state. Ghidra shows scene end writes device+0x2D54, clears a bit at+0x28B4 and updates dirty bits at+0x10. Preserve required producer state while replacing render boundaries. | Explicit retained targets, camera, viewport, scissor, clears and pass transitions, with correct resize/recreation lifetimes. |
| Remaining content | `821C9478`, `821C9C20`, `820D3FD0`, callback closure | Pose interpolation still calls the original model path; animated/skinned objects, effects, shadows and UI are not fully independent native scene passes. Resolve callback families and migrate each complete pass with its assets/parameters. | All required content draws without the original render helper and matches reference gameplay across missions. |
| Presentation and scheduling | `820B0B80`, `821A5080`, native host/frame loop | Native backend/host presentation exists; original finish callbacks and helper-dependent frame generation remain. Design one producer publication/consumer scheduling contract for the full frame. | Independent frame generation/interpolation at sustained 120 Hz, with simulation cadence preserved and unique rendered images measured. |
| Acceptance | Runtime scenarios, not a Ghidra function | Existing long audits prove individual paths, not completion. Menu-only scripts do not constitute controlled movement/firing tests. | Controlled camera/movement/firing, mission/load transitions, removal/LOD changes, visual comparison, resource lifetime stress, and non-audit performance measurements. |

## Next batch investigation

1. Resolve the renderer-facing indirect sites using vtable/data references and
   instruction-level argument flow; separate utility/allocator callbacks from
   render callbacks. Expand roots for newly identified render implementations.
2. For each original call retained in native hooks, assign its writes to
   simulation state, resource lifetime, compatibility-only state, or native
   rendering. Cross-check decompiler statements against PPC instructions.
3. Turn that map into a complete static-pass producer/consumer change, including
   pass entry/exit and compatibility handoff. Avoid another series of unrelated
   flags before the whole boundary is understood.
4. Validate the complete batch, then migrate remaining content and scheduling.

The current inventory does not yet resolve every indirect target. Ghidra output
contains decompiler warnings (including removed unreachable blocks), so emitted
instructions remain an independent cross-check. Neither absence from this graph
nor a decompiler's omitted argument proves that a dependency is absent.
