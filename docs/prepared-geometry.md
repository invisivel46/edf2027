# Prepared queued geometry

`edf_native_prepared_geometry` reuses a prepared indexed draw for the queued
material caller (`821D97E8`). The descriptor belongs to one cached mesh and
contains a validated first index, count and signed base vertex. A changed range
must be validated before replacing it. Mesh indices are immutable and its
vertex extent is fixed, so repeated draws of that descriptor need no second
range validation inside submission.

Each draw still checks tracked writers, resource lifetime/revision, snapshot
identity and the existing byte-verification schedule. A synchronous borrowed
identity API returns versions without constructing another array of shared
snapshot handles. It shares the owning observation API's checks and schedule;
failure consumes no observation. No guest callback or GPU operation runs while
the writer mutex is held.

The prepared route additionally requires the same backend, resource key,
shader/declaration, stride, exact owned source spans, index width and registered
vertex/index storage. Only then can it omit snapshot repackaging and redundant
registry attachment. Active writers, changed revisions, missing ownership,
changed geometry/layout/range, strict audits and scheduled comparisons return
to the ordinary guarded path. A newly compared immutable mesh may then be
prepared for later draws. State and material preparation remain per draw.

The cache owns the descriptor; it is used only during the existing registry
lock, so resource invalidation cannot leave a borrowed mesh alive past its
cache entry. Reuse may span groups or frames when the exact immutable geometry
remains the same. It does not cache a visibility decision or skip simulation.

## Checks

- Guest-memory tests cover writer exclusion, revisions, resource retirement,
  ownership, verification cadence and borrowed identities. The owning and
  borrowed APIs share the same validation implementation.
- Mesh tests check range/base identity, rejection of an invalid preparation,
  retention of the previous valid descriptor, and rendered readback through
  prepared submission. Existing cache/source/invalidation tests remain active.
- The release build and native hook-boundary audit pass.

The gameplay comparison uses the same executable, the fixed
`out/renderer-performance-input.txt` script, unlocked rendering capped at 120
FPS, sampled hooks every 64 calls, and game seconds 199–241. GPU instancing and
shared-constant reuse remain enabled in both runs. The
`Native prepared geometry` counter reports complete guarded preparation hits.

## September 18, 2026 measurement

| Metric | Disabled | Enabled |
| --- | ---: | ---: |
| FPS, eight intervals / 40 seconds | 81.250 | 80.825 |
| Mesh acquisition, sampled microseconds/call | 0.2394 | 0.2034 |
| Native indexed draw, sampled microseconds/call | 0.9804 | 0.9286 |
| Queued pass, sampled milliseconds/call | 5.087 | 4.982 |
| Render helper, sampled milliseconds/call | 10.967 | 10.974 |

The targeted acquisition cost fell about 15% and native indexed draw cost
about 5.3%. Overall FPS did **not** improve in this single matched pair
(-0.52%), and helper time was essentially unchanged. The shortcut is retained
for the measured local CPU saving, not as an established frame-rate gain.
Acquisition subphases shifted because owned-mesh matching now occurs during
guarded preparation; compare the complete acquisition/draw scopes rather than
attributing the difference to a single nested subphase.

Both runs logged zero errors. Evidence: `out/prepared-geometry-comparison.json`,
the matching enabled/baseline process, FPS and sampled report files,
`out/prepared-geometry-memory-tests.log`, and
`out/prepared-geometry-mesh-tests.log`. The feature is enabled by default;
`--edf_native_prepared_geometry=false` selects the previous preparation path.
