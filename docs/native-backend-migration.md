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

- **Per-draw driver cost.** Roughly 1-2 us per draw is spent inside the runtime
  on validation, state resolution and hazard tracking. At 2,370 draws a frame
  that is ~3 ms we cannot optimise away from the outside.
- **One immediate context.** Draws cannot be issued from more than one thread.
  Deferred contexts exist but are widely slower for many small draws, which is
  exactly this game's profile, so they are not a shortcut.

D3D12 and Vulkan move validation to pipeline build time and make multithreaded
command recording a first-class path. That is the only route to using more than
one of those 28 processors for submission.

**Vulkan is the better second backend if only one is built.** The stated target
is handheld PCs; handheld means Steam Deck means Linux, where D3D12 only runs
through Proton.

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
