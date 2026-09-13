# Native rendering coverage

How this port answers "does the native renderer handle everything the game can
ask for?" — and what that answer is currently worth.

A rejected draw is dropped from the frame. Until now the only way to notice was
for someone to see a missing mesh, and the two places that recorded rejections
capped at 64 caller-keyed entries, so past that, gaps were invisible. Coverage
work starts by fixing the accounting, not by playing more.

## Coverage is three numbers, all of which must be zero

- draws requested but not submitted natively,
- distinct draw contracts rejected,
- exceptions thrown out of the bridge.

`Native contract coverage` reports all three every ten seconds alongside the
existing per-path counters, and `clean=true` requires both zero rejections and
zero *omitted* rejections. An unknown number of unrecorded gaps is not the same
as none, so reaching the retention limit is counted and reported rather than
silently dropping contracts.

## Identity is the contract, not the caller

`src/native_graphics/native_contract_ledger.h` keys a rejection by what the
renderer would have to support — draw path, vertex and pixel *effect source
fingerprints*, a hash of the declaration bytes, topology, stride, index width,
element count — and deliberately excludes caller addresses. Shader handles are
guest addresses the engine reuses; the source fingerprint identifies the disc
source that would actually have to be implemented.

This matters for reading the numbers. Keying by caller made one unsupported
layout drawn from thirty call sites look like thirty problems, and two different
layouts from one call site look like one. `rejected` counts draws lost;
`distinct_rejected` counts gaps to close.

`--edf_native_contract_limit` bounds retention (default 4096).
`--edf_native_contract_coverage=true` additionally records every *submitted*
contract, so a run can enumerate what the content exercises; that costs a set
lookup on every draw and gameplay submits over a hundred thousand a second, so
it is opt-in and meant for capture runs, not play.

## The disc is a closed world, except for declarations

Three of the four contract axes can be enumerated from the disc with no game
running, which is what makes offline CI coverage possible at all:

| Axis | Source on disc | Checker | Status |
|---|---|---|---|
| Shaders | 44 `.dxsl` | `edf_native_shader_check` | all effects, variants and linked passes compile |
| Textures | 83 `.dds` | `edf_native_texture_check` | every DDS asset on the disc |
| Geometry | 263 `.sgo` + 68 `.dxm` | `edf_native_geometry_check` | **not statically recoverable — see below** |
| Render state | technique pass blocks in the 44 `.dxsl` | none yet | outstanding |

**Vertex declarations cannot be read off the disc.** A scan of all 331 model
assets for the 12-byte element encoding the runtime consumes finds nothing, and
a negative control confirms why: the runtime's vertex type words
(`0x2a23b9`, `0x2c23a5`, `0x1a2286`, …) **never appear in the assets at all**.
The guest synthesises the declaration at load from a more compact model
description, so no scan of the files can recover it. Recovering it statically
would mean reverse-engineering that description and the `821AD3B8` construction
path; that has not been done.

## So declarations are captured once and replayed forever

`--edf_native_contract_export=<path>` writes a line-based catalog of every
declaration the run observed — the bytes, not just the identity, precisely
because the disc cannot supply them — together with every contract seen:

```
# edf2027 native draw contracts v1
declaration 1111111111111111 00000000002a23b9...
submitted indexed 0000000000000000 0000000000000000 1111111111111111 4 52 2 5
```

`edf_native_geometry_check <game directory> <catalog>` then constructs a real
`NativeIndexedMesh` for every captured declaration against **every vertex entry
the disc can supply**, on WARP, with no display and no GPU clocks. One capture
run makes that layout checkable in CI forever.

A declaration most entries reject is ordinary — an entry that does not consume
those semantics has no business binding it. A declaration *no* entry accepts is
a real gap, because the game drew it. The tool reports those individually with
their reasons, lists every distinct rejection reason across all pairs so a new
one cannot appear unnoticed, and exits nonzero.

Validated both ways against the disc's 44 effects / 66 distinct vertex entries:

- `tests/fixtures/geometry-contract-sample.txt` — the skinned `c_Mech01`-shaped
  layout recovered from the live invalid-RGB probe (stride 52, POSITION0,
  NORMAL0, TEXCOORD0, BLENDINDICES0 as UBYTE4, BLENDWEIGHT0): 66/66 pairs
  construct.
- `tests/fixtures/geometry-contract-negative-control.txt` — an unknown attribute
  type and an overlapping layout: both reported uncovered with
  `packed/non-float mesh attributes not implemented` and
  `overlapping mesh attributes`, 0/132 constructed, exit 1.

## What this can and cannot claim

**Provable:** every captured declaration is bindable, every disc effect
compiles, every disc texture imports. Those are closed sets and the checkers
walk all of them.

**Not provable:** that the catalog contains every declaration the content can
produce. It is a lower bound that grows with play, so a missing declaration is a
capture gap, never a false pass. Bounding it needs content coverage — all eight
maps, weapons, menus — with the catalog merged across runs until it stops
growing. Only the Mission 1 intro and early street gameplay have been captured
so far.

Runtime-only combinations (viewport and scissor extents, draw ranges, which
passes actually execute) are outside all of this and are bounded only by
fail-closed accounting plus content coverage.

## Outstanding

- Render-state enumeration from the technique pass blocks, through
  `CreateNativeRenderState`, to close the fourth axis statically.
- Merging catalogs across runs, so coverage accumulates instead of being
  per-run, and a run reports only newly seen contracts.
- Immediate, font, movie, XUI and utility paths record rejections but have no
  offline replay equivalent yet.

## First real capture (2026-09-12)

A user gameplay session on a healthy host exported
`tests/fixtures/geometry-contract-mission1.txt`: **13 distinct vertex
declarations and 28 draw contracts, with zero rejections**
(`rejected_draws=0, distinct_rejected=0, omitted=0, errors=0, clean=true`).
Replaying it offline builds **858 of 858 (declaration, vertex entry) pairs**
against the disc's 44 effects and 66 distinct vertex entries, with no
declaration that no entry can bind.

The same session confirmed the sampled geometry comparison on content nobody
scripted: compared 40,979 against trusted 4,169,237, `unreported_changes=0`,
`revoked=false`. Frame rate held 50-60 during play.

Two limitations this capture exposed, one fixed:

- **Fixed.** Submitted contracts recorded `stride=0` and `index_width=0`,
  because the indexed hook resolves both inside the draw while the accounting
  runs after it. The checker fell back to the minimum stride the elements imply,
  which is a lower bound rather than the size the game actually drew - a padded
  layout was being tested at the wrong width. Both are now hoisted and captured.
- **Open.** The run's log stopped at 22:12:45 while the catalog kept updating to
  22:14:33, so the final in-log counters are missing and the export is the only
  complete record. The catalog is rewritten whole each time and is therefore
  still authoritative, but the log truncation is worth understanding before
  relying on log-side coverage numbers for a long session.

13 declarations is a small slice: one session, early content. The number is
expected to grow with maps, weapons and menus until it stops.
