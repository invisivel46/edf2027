# Render helper profiling

The September 18, 2026 gameplay profile instruments the original helper
(`821A5080`) with the existing `edf_native_hook_timings` and
`edf_native_hook_sample_period` controls. New inclusive phases cover visibility
traversal (`820B4038`), draw buckets (`821A3BA0`), model/mesh rendering
(`821C9C20` / `821B2C28`), pose preparation (`821C9478`), scene end, overlay,
and frame finishing. Phases nest and may run on different threads; their
totals are not an additive frame-time breakdown.

`tools/report-native-geometry-runs.py --all-phases --thread <id>` now isolates
hook timings for the logged render-helper thread. FPS and worker counters
remain process-wide. This matters especially for pose preparation: the same
routine also runs on the simulation thread and must not be charged to the
helper just because both use the same phase name.

## Main finding

Expanded profiling identifies the queued material rendering pass (`821C3BB8`)
as the largest measured block: about **7.04 ms per call**, inside a helper
averaging **13.88 ms** in Mission 1. It iterates material groups (`821D96D8`),
which bind geometry/material once, then call the instance-constant patch
(`821D9600`) and indexed draw (`821FE358`) separately for each instance.
The group phase nests inside the queue and must not be added to its time.

Other measured helper phases: child rendering 1.81 ms, finishing 1.16 ms,
and the direct object list 0.74 ms per call. Draw-bucket dispatch was about
1.15 microseconds. The measured pose routine on the helper thread averaged
0.26 microseconds per call; its simulation-thread work is excluded here.
These are instrumented observations for this mission, not all-content claims.

The next substantive optimization target is the queued groups: avoid repeated
per-instance native draw preparation and evaluate instancing for compatible
groups. Preserving constant layouts, resource lifetimes and draw order remains
necessary. A traversal rewrite alone did not reduce this submission work.

Evidence: `out/render-helper-queued-process.json` and
`out/render-helper-queued-profile.json` (helper thread 14240). The report's
thread filter was checked against a stopped log: it preserves helper totals,
excludes simulation-only calls, leaves FPS unchanged, and returns no hook
samples for an absent thread. The final release build and native hook-boundary
audit passed. No performance improvement is claimed for the retained change,
which consists of profiling hooks and reporting support.

The baseline gameplay window measured 13.84 ms per helper invocation and
63.15 FPS. Draw buckets averaged 1.14 microseconds per call. Pose preparation
was also a small part of the measured work. Sampling the helper found the
original visibility traversal among its largest individual guest routines
(75 of 2,000 samples), followed by its transform routine (50 samples).
The full timing instrumentation itself appears in the samples, so these are
diagnostic measurements, not an uninstrumented performance claim.

## Consecutive owned mesh reuse

`edf_native_owned_mesh_hit` enables a short cache-hit path before the full mesh
acquisition call. Consecutive indexed draws with the same resource key, backend,
shader bytecode, declaration identity, stride, index width and exact owned
vertex/index spans reuse the cached mesh without copying the acquisition
function's temporary shared ownership handles. Dynamic meshes and generated
indices use the existing path. A failed shortcut leaves the cache unchanged.

This runs after guarded geometry observation. Resource invalidation, snapshot
commit, draw-bound validation, constants and recording remain in their existing
order. It does not combine draws or change instance transforms. The native quad
tests exercise successful reuse and rendering, changed keys/declarations/layouts,
unowned equal bytes, changed source extents, invalidation and cache clearing.

Matched performance runs use the same executable with this flag disabled and
enabled, `edf_native_hook_sample_period=64`, full hook timings disabled, the
fixed `out/renderer-performance-input.txt` script, and game seconds 199–241.
`tools/report-native-sampled-hooks.py` reports sampled microseconds per call as
well as estimated work per 60 Hz interval. Per-call cost avoids confusing more
rendered frames per second with more expensive individual operations. Estimates
remain inclusive and low-count outer scopes can be noisy.

The matched September 18 pair measured:

| Metric | Disabled | Enabled |
| --- | ---: | ---: |
| FPS (eight intervals, 40 seconds) | 73.15 | 74.775 |
| Mesh lookup, microseconds/call | 0.1013 | 0.0712 |
| Whole mesh acquisition, microseconds/call | 0.2623 | 0.2467 |
| Native indexed draw, microseconds/call | 1.0923 | 1.0432 |
| Queued material pass, milliseconds/call | 5.526 | 5.347 |
| Render helper, milliseconds/call | 11.842 | 11.722 |

Both runs logged zero errors. This is one matched pair, not a statistically
established 2.22% FPS gain; the approximately 30% reduction in the targeted
lookup cost supports retaining the shortcut, enabled by default. Full-hook
profiles above carry more instrumentation overhead and are not comparable FPS
baselines. GPU instancing was added subsequently; see [world instancing](world-instancing.md)
for its separate implementation and measurements.

Evidence: `out/owned-mesh-hit-comparison.json`, the corresponding baseline and
enabled `*-fps.json` / `*-sampled.json` reports, and their process JSON files.
The release build, native quad/cache rendering tests, native hook-boundary
audit and a sampled-report unit-conversion/window check passed.

## Rejected visibility traversal experiment

An experimental `NativeVisibilityTraversal` replaced the register-by-register bookkeeping of
`820B4038` with local variables and a native linked-list loop. It retains:

- The original transform, coarse and refined culling, and draw routines.
- The original order of calls and their guest return addresses.
- The per-render generation marker, including marking before culling.
- The original single-precision distance cutoff and unordered comparison.
- Reading the next link after drawing, since callbacks can change the list.
- The guest stack extent and nonvolatile register/return contract.

It did not cache visibility across frames or skip animation updates and was
restricted to the render helper's thread-local scope. It was **removed** after
measurement: traversal averaged 24.695 versus 24.672 microseconds per call,
and the helper averaged 13.840 versus 13.878 ms. FPS was 63.15 versus 64.85,
but without a corresponding reduction in the targeted cost this is not
evidence of a useful optimization. The original traversal remains in use.

## Verification

The experimental differential check executed a freshly extracted copy of the
actual generated guest routine and the native loop over identical state.
It compared callback order/arguments, object/list memory, return value, stack,
return address and nonvolatile registers in 1,000 seeded cases. Cases included
empty lists, already visited and duplicate objects, both culling stages,
unordered distance comparisons, and callback changes to next links. Transform,
culling and drawing were controlled callbacks in this test; their actual game
implementations remained unchanged. The test passed, as did the release build;
the gameplay capture was inspected and both runs logged zero errors. The
experimental implementation and test were removed with the rejected change;
copies remain under `out/rejected-native-visibility-*` as local evidence.

Local baseline evidence: `out/render-helper-baseline-profile.json`,
`out/render-helper-samples.json`, and `out/render-helper-sample-counts.json`.
The executable and symbol-map executable had matching `.text` hashes before
sampling. `out/render-helper-native-loop.asm` confirms direct calls to the
four retained guest routines.
