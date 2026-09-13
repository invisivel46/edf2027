# Native renderer backend migration

Planning note for moving the native renderer off D3D11 onto a D3D12/Vulkan
backend. Written before any backend code exists, so the measured surface and the
design consequences are on record rather than discovered halfway through.

## Why

The draw thread is the constraint, not the GPU. Measured in steady Mission 1
gameplay at 60 fps on a 20-core i7-14700:

| | |
|---|---|
| draw-thread hooks, summed top-level | **62.8%** of one thread |
| `indexed.native` | 26.4% (1.83 us x 142,385/s) |
| `activation.native` | 18.7% (4.16 us x 44,917/s) |
| draws per frame | ~2,370 |
| GPU span per frame | 12.4 ms of 16.67 (includes submission gaps) |
| idle logical processors | 27 of 28 |

Two D3D11 properties cap what can be done about that:

- **Per-draw driver cost.** *This estimate was wrong; see "What the per-draw
  cost actually is" below.* It originally read: roughly 1-2 us per draw is
  spent inside the runtime on validation, state resolution and hazard
  tracking, so at 2,370 draws a frame that is ~3 ms we cannot optimise away
  from the outside. Measured, it is 0.30 us per draw and ~0.71 ms a frame.
- **One immediate context.** Draws cannot be issued from more than one thread.
  Deferred contexts exist but are widely slower for many small draws, which is
  exactly this game's profile, so they are not a shortcut.

D3D12 and Vulkan move validation to pipeline build time and make multithreaded
command recording a first-class path. That is the only route to using more than
one of those 28 processors for submission.

**Vulkan is the better second backend if only one is built.** The stated target
is handheld PCs; handheld means Steam Deck means Linux, where D3D12 only runs
through Proton.

## What the per-draw cost actually is

`edf_native_backend_bench` runs the same workload through both APIs: 2,370
draws a frame, a material activation every 1.6 draws as measured, one texture
per draw cycled through 64, a blend state change every 16 draws, and trivial
geometry so the number is submission cost and not shading. Hardware, 300
frames, after a discarded warm-up. Stable to about 2% across runs.

| | per draw | per frame |
|---|---|---|
| D3D11 (dynamic constant buffer, WRITE_DISCARD) | 0.298 us | 0.71 ms |
| D3D12 (root CBV from the upload ring) | 0.080 us | 0.19 ms |

D3D12 is **3.7x cheaper per draw**, which is a real result and it holds when
the workload changes state rather than repeating one bind.

**But the frame-level claim above was wrong, and it was mine.** I estimated
1-2 us of driver cost per draw and ~3 ms a frame. The measurement says 0.30 us
and 0.71 ms, so moving to D3D12 saves about **0.52 ms of a 16.67 ms frame** -
worth having, and nothing like the headline the "Why" section implied.

Two consequences worth taking seriously:

- The draw hook costs 1.83 us per draw in the game. If only 0.30 us of that is
  the API, then **1.5 us is our own work** - guest reads, mesh lookup,
  parameter decode - and that is where the larger prize is, not in the API.
- Stage 0 makes this smaller still. Collapsing 77.4% of draws into instanced
  ones cuts the API cost to roughly 0.16 ms a frame on D3D11 alone.

What survives untouched is the argument that actually motivated the migration:
**a single immediate context cannot submit from more than one thread.** 27 of
28 logical processors are idle. That is a structural ceiling no amount of
per-draw tuning reaches, and it needs stage 3. The per-draw saving is a bonus,
not the reason.

## Does multithreaded recording actually pay?

This is the argument the per-draw correction left standing, so it was measured
too, with `--threads=N --bridge-us=1.5`. The simulated 1.5 us of non-API work
per draw is the difference between the game's measured 1.83 us draw hook and
the 0.30 us of API cost above: guest reads, parameter decode, mesh lookup.
2,370 draws a frame, 120 frames, hardware.

| | ms/frame | vs one thread |
|---|---|---|
| D3D11, one thread (today) | 3.77 | - |
| D3D12, one thread | 3.75 | 1.00x |
| D3D11, 4-thread decode, serial submit | 2.02 | 1.87x |
| D3D12, 4 recorders | 1.05 | 3.59x |
| D3D12, 8 recorders | 0.73 | 5.13x |

Three things fall out, and the third is the one that matters.

**Threading the API calls alone buys nothing.** With no simulated work, going
from 1 to 4 recorders moved 0.197 ms to 0.133 ms. There is not enough
submission work to be worth splitting - the per-frame barrier costs about as
much as it saves.

**The win is in threading our own work, not the API.** Put the 1.5 us back and
four threads take 3.77 ms to 1.05 ms. That is the whole prize, and almost none
of it is the graphics API.

**But D3D11 cannot collect it.** The honest control is D3D11 with the same
decode split across four threads and submission left serial, which is allowed:
2.02 ms, only 1.87x. It stalls on the one thing D3D11 cannot parallelise. At
eight threads it is 1.81 ms and barely moving, while D3D12 reaches 0.73 ms.

So **D3D12 is worth about 0.97 ms a frame beyond what restructuring alone can
get** - 48% of the restructured frame. Not the headline the original "Why"
section implied, and not nothing either.

### What this says about the order of work

Restructuring the bridge to do its per-draw work off the submit thread is the
prerequisite for both, and it pays 1.87x on D3D11 as it stands, with no
backend migration at all. It should come first. D3D12 then removes the ceiling
that restructuring runs into.

### Caveat that could overturn this

The simulated work is perfectly parallel: no shared state, no locks, no guest
memory. The real work reads guest memory through the write-ownership registry
and the parameter decode paths, and how much of that can run concurrently is
not known yet. If it turns out to be mostly serialised, the 3.59x is an upper
bound nothing reaches, and the honest answer would be that neither the
restructuring nor the migration pays. That is the next thing to measure, and
it should be measured before either is built.

## Measured surface to replace

```
D3D11-specific code      3,529 lines across 14 files
API-independent code    10,630 lines (effect parsing, formats, guest logic)
distinct context methods    47
distinct D3D11 types        20
```

The renderer is already split usefully: `edf_native_effects` (SGSL/DXSL parsing,
movie/XUI/font effects) and every `guest_*.h` are API-independent and stay. Only
`edf_native_d3d11` and the bridge's call sites move.

Of the 47 context methods, about ten are **getters** (`VSGetShader`,
`OMGetRenderTargets`, `CSGetShaderResources`, `SOGetTargets`, ...). They exist to
save and restore state around foreign work. See the state-ownership consequence
below: they have no equivalent to port.

## What the shaders actually bind

`edf_native_shader_check <game> --slots` reports the widest slot any disc
shader uses, across 44 effects / 130 shader entries. A D3D12 root signature is
fixed at creation, so it has to be sized from this rather than from the API
maximum:

| | used | D3D11 maximum |
|---|---|---|
| PS constant buffers | 1 | 14 |
| PS textures | 7 | 128 |
| PS samplers | 7 | 16 |
| VS constant buffers | 2 | 14 |
| VS textures / samplers | none | - |
| widest constant buffer | 3,824 bytes (`m_Water01_DNDNAC.dxsl:PS_Main`) | - |
| vertex input elements | 9 (`c_Mech01.dxsl:VS_Blend`) | 32 |

This is small enough to change the design rather than merely inform it. The
three constant buffers can be **root CBVs** - a GPU virtual address written
straight into the root arguments, with no descriptor heap traffic at all. At
44,917 activations a second that removes the single highest-frequency piece of
descriptor work in the frame, which is the opposite of what consequence 4
assumed when it said this path was the most likely place to get lifetime wrong.
The lifetime problem stays (the memory is still fenced ring memory); the
descriptor problem disappears.

Textures still need a descriptor table, because a root SRV can only address a
buffer. Seven per draw, from a per-frame descriptor ring.

Caveat on record: these are the **disc** shaders. The renderer also compiles
its own HLSL for UI, font, movie and post, which this tool does not enumerate.
The root signature is therefore sized from evidence but **verified against
reflection at pipeline creation**, so a shader that needs more fails loudly
instead of silently losing a binding.

## Five consequences that decide the design

**1. There is no device state to query, so we must own it.**
The getters above read state back out of the D3D11 context. D3D12 and Vulkan
keep no such state. Every one of those call sites becomes "read our own recorded
state", which means the backend interface has to be the authority on bound
state rather than a thin pass-through. This is the single largest structural
change and it touches the bridge, not just the backend.

**2. `SwapDeviceContextState` isolation becomes separate command lists.**
`d3d11_frame_handoff.cpp` swaps into an isolated D3D11.1 context state so
presentation work cannot clobber renderer state, then restores. Neither target
API has this. The replacement is recording renderer and presentation work into
separate command lists, which is cleaner, but it is a redesign of the handoff
rather than a translation of it.

**3. Independent state objects become pipeline objects.**
Blend, depth-stencil, rasteriser, input layout and the shader pair are set
independently today. In both target APIs they fuse into one pipeline object that
must be created ahead of use and cached on the whole combination. `RenderStateWords`
is already a six-word key with a cache behind it, so the shape exists - but the
key has to grow to include the shader pair and input layout, and pipeline
creation is far more expensive than `CreateBlendState`, so a cold miss during
gameplay is a visible hitch rather than a small cost.

**4. Dynamic constants need an upload ring and fences.**
`UpdateSubresource` and `Map(WRITE_DISCARD)` currently carry constants at
**44,917 activations/s**. Both targets require explicitly managed upload memory
plus fencing so a buffer is not rewritten while a frame still reads it. This is
the highest-traffic path in the renderer and the most likely place to get
lifetime wrong.

**5. Hazard tracking becomes explicit barriers.**
D3D11 inserts barriers implicitly. Every render-target write, resolve, copy and
subsequent sample needs an explicit transition. Missing one is a silent
corruption that appears only on some hardware, which makes this the hardest
class of bug to catch in this project's usual way.

## Staged plan

**Stage 0 - instancing (do first, independent of this).** 77.4% of draws are
collapsible into a preceding instanced draw, with zero register-shape breaks
across 14.7M draws. It helps D3D11 now and makes every later stage cheaper:
fewer pipeline binds, fewer command-list entries, less to record per thread.

**Stage 1 - the interface.** Introduce a backend interface at the level the
bridge actually needs (resources, pipeline state, bindings, targets, clears,
draws, copies, dynamic uploads, queries) rather than a 1:1 mirror of D3D11's 47
methods. A D3D11 implementation sits behind it and keeps working. This is where
consequence 1 gets paid, and it is worth doing on its own merits.

**Stage 2 - the second backend.** Vulkan preferred. Pipeline cache, descriptor
management, upload ring, barriers.

**Stage 3 - multithreaded recording.** The actual payoff, and only reachable
from stage 2.

## Where the D3D12 backend actually is

Built and tested on WARP, so the suite runs on a machine with no D3D12
hardware. Selected by name through the registry (`d3d12`, or `d3d12-warp` to
force the software rasteriser), which is how a rendering difference gets
attributed: reproduces on WARP, the bug is ours; does not, it is the driver's.

Working, each verified by reading pixels back rather than by inspection:

- Device, queue, per-frame command allocators, fences. 32 frames through 3
  slots, readback matching the frame that wrote it.
- Fenced upload ring, shared by constants, buffer and texture staging.
- Root signature sized from the shader measurement; constant buffers as root
  CBVs, so a material activation costs no descriptor work at all.
- Pipeline cache on the fused description, with scissor deliberately excluded.
- Every shader validated against the root signature by reflection before it
  can reach a pipeline.
- View descriptor ring, sampler tables cached by combination.
- Flat triangle: 2,016 of 4,096 pixels where half is expected.
- Textured draw: a 2x2 texture uploaded and sampled, all four quadrants
  checked so a flip or row swap cannot pass.
- Indexed instanced draw with a per-instance input element: four instances,
  one per quadrant, nothing in the centre. This is the stage-0 primitive.

Not done, and loud rather than silent about it:

- **No presentation.** Frames render to a texture; nothing reaches a window.
- No vertex-stage textures or samplers (no disc shader uses any).
- No MSAA resolve, no query readback, no mipped texture upload.
- `SupportsParallelRecording()` returns false, because it is stage 3.
- **The bridge still calls D3D11 directly.** This backend is not wired to the
  game; that is the 13 touchpoints and it is the largest remaining piece.

### Consequence 5 is no longer a prediction

Twice now, disabling resource barriers produced **pixel-identical, fully
passing output** - the same 2,016 covered pixels, every colour check green -
and only the drained validation messages caught it. That is why validation
draining is on the seam rather than in one backend, and why the tests read it
instead of leaving it in the debugger where an automated run never looks.

### Seam gaps found by building against it

The interface was written before any backend existed, and four things in it
were wrong. Each is now fixed rather than worked around:

- It required pipelines everywhere and created them nowhere.
- It had `Submit` but no `BeginFrame`. D3D11 needed none; everything else does.
- It declared a sampler type with no way to create one.
- It could not read its own output, so a backend could not be compared against
  the one it replaces.

## What this replaces

A renderer that currently works: 60 fps held, 18.9M indexed draws submitted with
zero unsubmitted, zero rejected contracts, 21/21 tests. That is the bar a second
backend has to meet before it can become the default, and the contract ledger
plus the geometry checker already give a way to measure it: a backend that drops
draws will show up as rejected contracts rather than as someone noticing a
missing mesh.

## Not decided

- Whether to vendor an existing dual-backend render interface rather than write
  one. A game-agnostic RHI is the reusable part of comparable projects, unlike
  their renderers, which are welded to their own guest interception. Confirm
  what such a layer actually is and its licence terms before committing; this
  repository already vendors third-party licences under `LICENSES/`.
- Whether D3D12 is built at all, or Vulkan only.
