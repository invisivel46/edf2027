# Queued world-matrix instancing

`edf_native_world_instancing` enables GPU instancing for compatible indexed
draws issued by the queued material loop at `821D97E8`. Direct recorders and
unsupported shaders retain the ordinary draw path. No guest simulation or
animation update is skipped.

At shader registration, an optional vertex variant reads four matrix vectors
from vertex stream 15 (`EDFINSTANCE0` through `EDFINSTANCE3`, divisor 1).
The wrapper reconstructs the world matrix using the original shader's reflected
row/column orientation. Reversed depth uses the existing wrapper. Compilation
retains the original world declaration and verifies the consumed uniform slots
and offsets and the complete output signature before accepting the variant.
Unsupported declarations, entry signatures, resources and layouts fall back.

The parallel recorder combines consecutive packets only when their geometry,
index range, material/raster/target bindings and all constant bytes outside
the world matrix agree. It preserves order, caps a batch at 256 instances,
and stores matrix bytes in frame-owned storage until recording completes.
Queries, transient geometry, pre-existing instanced draws and explicit ordinary
draws are not merged. Resource mutations retain the recorder's flush boundaries.
Serial replay and worker replay both restore complete state after an instanced
packet, so an ordinary draw cannot inherit the alternate shader accidentally.

The producer still observes geometry, validates bounds, patches guest constants
and prepares each draw. The savings come from fewer recorded GPU draws and
fewer repeated uploads/binds; this does not eliminate the whole queued CPU pass.

## Checks

- `edf_native_backend_conformance_tests` compares ordinary and instanced pixels
  for row-major/column-major matrices and normal/reversed depth on D3D12 WARP.
  It exercises worker and serial replay, a 256-instance batch split, changed
  vertex/pixel constants, scissor changes and an explicit fallback draw. Exact
  merge counters are checked, and backend validation reports no errors.
- `edf_native_shader_check <game-directory> --instancing` reports accepted
  variants and fallback reasons. The retail assets produce 43 normal and 43
  reversed world variants; all 130 original entries and 90 linked passes pass.
- The release build and native hook-boundary audit pass.

Gameplay A/B runs use the same executable, the fixed
`out/renderer-performance-input.txt` script, unlocked rendering with a 120 FPS
cap, sampled hooks every 64 calls, and game seconds 199–241. The
`Native world instancing` log reports actual merged groups and folded draws.

## September 18, 2026 result

| Metric | Disabled | Enabled |
| --- | ---: | ---: |
| FPS, eight intervals / 40 seconds | 73.025 | 76.875 |
| Render helper, sampled milliseconds/call | 11.714 | 11.410 |
| Queued pass, sampled milliseconds/call | 5.526 | 5.597 |
| Worker recording CPU, milliseconds/batch | 1.131 | 0.656 |
| Producer wait, milliseconds/batch | 0.354 | 0.272 |

The enabled counter window folded 4,879,200 of 7,020,392 geometry packets
(69.5%) into 520,800 instanced groups. These counters include geometry outside
the eligible queued pass. Worker counter windows differ in length (41.182
seconds disabled, 31.249 enabled), so their costs above are normalized per
batch rather than comparing raw totals. Both FPS windows are 40 seconds.

This single matched pair showed a 5.27% FPS increase. The merge checks add
producer work: indexed submission rose from 1.073 to 1.145 microseconds/call,
and the queued pass itself did not improve. The measured benefit is downstream
recording and waiting; further gains require reducing per-instance preparation.
Both runs logged zero errors, and both gameplay captures were visually checked.
The automated ordinary/instanced pixel comparisons are exact; the gameplay
captures are not synchronized simulation frames and are not an exact pixel test.

Local evidence: `out/world-instancing-comparison.json`, the corresponding
baseline/enabled process, FPS and sampled JSON files, `out/world-instancing-*.bmp`,
`out/world-instancing-tests.log`, and `out/world-instancing-shaders.log`.
The feature is enabled by default; `--edf_native_world_instancing=false` disables
merging for comparison while retaining ordinary rendering.

## Shared constant preparation

`edf_native_world_constant_reuse` avoids constructing another full immutable
vertex constant image when only the reflected world matrix changed. The
recorder compares the bytes outside that matrix against the retained snapshot,
then owns just the new 64-byte matrix in its producer state. Compatible draws
append that matrix directly to the batch stream; their common constants retain
the first snapshot. Non-world changes still create a full image.

A batch break, ordinary draw or active query materializes the complete current
image before recording. Changing pipelines also materializes pending state.
Push/pop state includes the deferred matrix, so restoring a saved state cannot
inherit a later object's transform. Frame reset retires the snapshots and
overrides together. Geometry observation, bounds checking and resource-update
flush boundaries are unchanged.

The conformance test additionally saves a matrix after a merged draw, changes
it, restores it, then renders an ordinary draw in another color. Its pixels
match ordinary rendering for both matrix orientations, depth variants and
worker/serial paths. Tests also verify fewer copied constant bytes and exact
batch counts. `Native world constants` logs successful reuses and bytes copied
into complete immutable snapshots; matrix-stream copies are separate.

The matched September 18 comparison kept GPU instancing enabled in both runs
and changed only `edf_native_world_constant_reuse`:

| Metric | Reuse disabled | Reuse enabled |
| --- | ---: | ---: |
| FPS, eight intervals / 40 seconds | 77.475 | 79.425 |
| Full constant-snapshot bytes per geometry draw | 3,845.77 | 1,396.23 |
| Native indexed submission, microseconds/call | 1.115 | 0.998 |
| Queued pass, sampled milliseconds/call | 5.505 | 5.301 |
| Render helper, sampled milliseconds/call | 11.496 | 11.144 |

This pair showed a 2.52% FPS increase and 63.69% fewer full snapshot bytes per
geometry draw. Counter windows span 30.873 seconds disabled and 37.844 enabled;
bytes are normalized per draw, while both FPS windows span 40 seconds. The
enabled counter window includes 6,504,000 successful shared-constant reuses.
Both runs logged zero errors. These are one pair of sampled Mission 1 runs,
not an all-content or statistically established FPS guarantee.

The release build, backend conformance tests and native hook-boundary audit
passed. Evidence is in `out/world-constant-reuse-comparison.json`, the matching
baseline/enabled process and report JSON files, and
`out/world-constant-reuse-tests.log`. Reuse is enabled by default; setting
`--edf_native_world_constant_reuse=false` preserves instancing while disabling
this producer optimization.
