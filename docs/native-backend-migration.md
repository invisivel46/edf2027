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

### The caveat, answered: why the current code cannot be threaded at all

The simulated work above is perfectly parallel - no shared state, no locks.
The real work is not, and the reason is sharper than "some shared state".

Every registered shader owns one `ShaderBindings`, and inside it **one GPU
constant buffer per constant slot**, with a CPU byte image beside it. A
material activation writes that byte image; the draw that follows calls
`Bind`, which does `UpdateSubresource` into that one buffer and clears a
dirty flag. So a frame's 44,917 activations are not independent work items:
they are overwrites of a single buffer per shader, and correctness depends
entirely on each draw being submitted between one activation and the next.

Two consequences, and the second is the important one.

**Threads cannot be added to the current code, on either API.** This is not a
data race that a mutex would fix. One buffer can hold one material's values
at a time, so two draws with the same shader cannot be in flight with
different constants no matter how they are synchronised. This game reuses
shaders heavily - 130 shader entries across ~2,370 draws a frame - so that is
the common case, not an edge one.

This also corrects the D3D11 control in the table above. It assumed the
non-API work could be split off and submitted serially. In the code as it
stands it cannot: the "decode" *is* the mutation of the shared buffer. The
1.87x is what a restructured D3D11 could reach, not what today's could.

**The fix is the thing D3D12 already does.** Making draws independent means
each carrying its own resolved constant bytes instead of pointing at one
shared buffer. That is exactly `NativeBackendRecorder::SetConstants(stage,
slot, bytes)`: bytes go into the fenced upload ring and their address into a
root CBV, so every draw has its own copy and nothing is overwritten in place.
The seam was designed that way because D3D11's renaming had no equivalent,
and it turns out to be the same change parallelism needs.

So the honest summary is better than the one the measurement alone gave:
restructuring is required either way, it is the larger part of the work, and
on D3D11 it buys 1.87x and then stops on serial submission, while the D3D12
backend needs no further change to go past it.

What remains genuinely unmeasured is contention elsewhere in the draw path -
`model_buffers` retain/acquire, the render-state cache, the write-ownership
registry. Those are ordinary shared containers and can be sharded or made
lock-free; none has the one-buffer-per-shader property that makes the
constant path structurally serial.

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

**Stage 1 - the interface.** *Done, and with two backends behind it rather
than one; see the status sections below.* Introduce a backend interface at the level the
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
- Presentation: flip-discard, three buffers, nine frames to a real window.
- Parallel recording from two threads: 1,024 pixels from each, neither
  writing into the other's half.
- Occlusion query returning 2,016 samples, matching the pixel count the flat
  triangle test measures independently.
- Mipped texture upload, checked by sampling level 1 explicitly.
- 4x multisampled target, resolved and sampled: 2,016 fully covered pixels
  matching the single-sample count, plus 64 partially covered ones - exactly
  the length of the diagonal, and values single-sample rasterisation cannot
  produce.

Not done, and loud rather than silent about it:

- **The bridge still calls D3D11 directly.** The game can now create and
  report a backend (`--edf_native_backend`), but nothing draws through one
  yet. See "How the port actually has to happen" below.
- No vertex-stage textures or samplers (no disc shader uses any, so this is a
  declared limit rather than a missing feature).
- Wiring is the 13 touchpoints and is the largest remaining piece, and it
  cannot be a translation: see the constant-buffer finding below.

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

## Backend compatibility: what the seam can and cannot express

Every capability the renderer uses is now expressible through the seam and
produces **bit-identical output on both backends**. Checked by running the same
rendering through each and comparing pixels, not by inspection:

flat draw, textured draw, block-compressed (BC1) textures, mipped upload
sampled at an explicit level, indexed instanced draws with a per-instance
element, constant blend factor, depth testing against a depth target,
two render targets from one draw, 4x multisample resolve, occlusion queries.
Zero differing pixels on every one, including the resolve, where a tolerance
for differing sample positions turned out to be unnecessary.

Four real gaps were found by checking the renderer's own state rather than
assuming, and each would have rendered wrongly rather than failed:

- **BC row pitch computed in texels rather than blocks.** A 2x2 level of a BC
  format still costs a whole 4x4 block, so the second row of blocks came from
  the wrong offset.
- **BC format numbers shifted by one**, putting BC4's 8-byte blocks in with
  BC3's 16-byte ones. This would have halved every BC3 mip offset - the game's
  commonest texture format. The BC1 test passed anyway, which is why the format
  table is now proved by static_assert rather than by a test.
- **No constant blend factor.** The decode computes `requires_blend_factor` and
  the renderer honours it; the backends bound a hardcoded white. Those
  materials would have drawn in the wrong colour. A draw that needs one and was
  not given one is now refused, as the renderer refuses it.
- **No MIRROR_ONCE address mode**, which the guest sampler decode does emit.

Deliberately still absent, with reasons:

- **Stencil.** The renderer's own decode refuses it too, so the backends are as
  capable as the thing they replace. It would have to be added to both at once.
- **Vertex-stage textures and samplers.** No shader uses any - not the 44 disc
  effects, and not the renderer's own UI, font, movie and post HLSL, which bind
  only t0/s0/b0. Reflection at pipeline creation refuses a shader that needs
  more, so this fails loudly if it ever stops being true.

## What stands between here and D3D12 by default

Compatibility is no longer the blocker. The renderer is.

Every resource the game draws with is still made of D3D11 objects created by
these files, and D3D12 cannot borrow them - unlike the D3D11 backend, which can
adopt the renderer's own device. They have to be built through the seam:

| file | lines | D3D11 references |
|---|---|---|
| `d3d11_texture` | 555 | 84 |
| `d3d11_mesh` | 527 | 50 |
| `d3d11_ui` | 156 | 44 |
| `d3d11_render_state` | 124 | 30 |
| `d3d11_quads` | 136 | 27 |
| `d3d11_frame_compositor` | 115 | 25 |
| `d3d11_bindings` | 332 | 24 |
| `d3d11_frame_handoff` | 64 | 14 |
| the rest (`gpu_timer`, `completion`, `signals`, `presenter`, `effect`) | 552 | 32 |

Until those are ported there is nothing for D3D12 to draw, so making it the
default would mean a device that costs memory and start-up time and renders
nothing. The default stays D3D11 until the port lands; the flip itself is one
line and the conformance test is what says it was clean.

### Port progress

The files above are mostly guest logic wrapped in a thin D3D11 shell - the DDS
loader was 96 lines of which ten were D3D11 - so the port is much less new code
than the line counts suggest. The guest halves are being lifted out first,
because each one is shared, safe, and verified by the tests that already exist:

| done | what moved out |
|---|---|
| render state decode | guest blend / depth / raster words |
| guest vertex streams | validation, endian swap, quad-to-triangle expansion |
| DDS decode | header, formats, channel masks, cube faces, mip chains |
| mesh input layout | neutral, retained, owns its names, fingerprinted |
| shader constants | `ConstantImages` - the bytes a draw carries itself |

What remains is the thin half, and it is one connected change rather than five
separable ones: vertex and index buffers become backend buffers, the draw site
asks for a pipeline instead of binding a shader, textures and render targets
become backend resources, and the compositor and presenter follow. It has to
land together because a mesh drawn through a recorder needs a pipeline, which
needs the layout and the render state, which needs the targets' formats.

Doing it on the adopted D3D11 backend first means the game keeps working and
any difference is a wiring mistake rather than a backend one - and the
conformance test already says the two backends agree on everything the
renderer does.


## The frame-time finding that none of the reasoning found

Everything above about threading was reasoned from per-draw arithmetic in a
menu scene. Scripted combat, seven minutes of it, found something none of that
arithmetic could have:

| | frames | fps | `immediate.context_wait` |
|---|---|---|---|
| present inside the renderer's lock | 3,072 | 7.3 | 11.043 us/call |
| present outside it | 24,508 | **58.4** | **0.021 us/call** |

The draw thread was spending 21.4 seconds of a 420-second run blocked on the
renderer's context mutex - about 11 microseconds on each of 1.9 million
immediate draws. `immediate.native` fell from 12.250 to 1.239 us per call, and
the entire difference is that wait. The work was always about 1.2 us.

The cause was the host surface presenting inside the frame visitor, which
holds the renderer's lock. Necessary for the D3D11 compositor, which shares
the context; not necessary for a backend presenting from its own device, and
expensive, because that present waits on a fence for a frame slot - holding
the renderer's lock while waiting on a GPU.

### Why the menu measurements missed it

A menu scene is 97% vsync idle. Contention on a lock shows up as a wait, and
a thread that has nothing to wait for does not contend. Every per-draw figure
taken there was accurate and every conclusion drawn from it about where frame
time goes was wrong, because the scene had no frame-time pressure to expose.

The lesson is cheap to state and was expensive to learn: **measure the
workload that matters, even when a representative one is inconvenient to
produce.** `EDF_INPUT_SCRIPT=tools/native-combat-input.txt` drives it without
a human at the controls, and it cost seven minutes.

### Where the frame goes now

At 58.4 fps, per frame: 8.6 ms waiting for vsync, 3.1 ms in the game's own 60
Hz pacing sleep, 2.35 ms waiting for the GPU, and about 1.5 ms of our CPU work
across the immediate and XUI paths. The game is at its frame cap.

That is a UI-heavy scene, though - the run recorded no indexed draws at all,
so the geometry path and the two binding optimisations built for it are still
unmeasured. The combat script runs to 775 seconds and the run was cut off at
420, which is the whole reason: it measured the menu.

## Threading: three designs, and why only one of them can work

The simulation said 3.59x at four threads. Getting there needs a design that
fits how the game actually calls us, and two of the three obvious ones do not.

**Fan out each hook.** Dead. There is no batch to spread: a material
activation carries about 2.5 parameter uploads and each draw is its own call.
Dispatching 0.63 microseconds of work to a pool costs more than doing it.

**Decode ahead of the draw.** Also dead, and less obviously. The idea was that
the hook copies the guest bytes it must read now - the game may overwrite them
the moment it returns - and queues the conversion, so following draws submit
while it happens beside them. Measured against the real call order, there are
no following draws to overlap with: the hooks strictly alternate, activate,
draw, activate, draw. The draw that would hide the latency is the one waiting
for it. Nothing overlaps.

**Record, then submit.** The only one left. Hooks record what each draw needs -
pipeline, constant bytes, textures, buffers, draw arguments, viewport, target -
into a per-frame buffer, and the frame is decoded and submitted at the end.
That gives a batch, which is what both dead designs were missing, and it is
what makes parallel recording on several command lists reachable.

The record format is the seam. A recorded draw is a `NativeBackendPipelineDesc`
plus the arguments to `SetConstants`, `SetTexture` and `DrawIndexed`, which is
what those calls were shaped for. So **threading and the port are the same
work**, not two things to schedule against each other.

### What is built

`NativeDecodeWorkers` is the pool that design needs, and it is finished and
tested: 4,000 contended jobs, out-of-order completion, drain, destruction with
work outstanding. Retirement only advances past tickets nothing is still
working on - the naive "highest finished ticket" would let a draw bind
constants that were never written, and the test catches that mutant by name.

Zero workers runs every job inline, so threading off is the same code path
rather than a special case.

### What the ceiling actually looks like

Measured per activation, in a menu scene where the sub-phases are visible:

| | us/call |
|---|---|
| `activation.native` total | 2.438 |
| of which `activation.original` - the guest's own recompiled code | 0.778 |
| `activation.textures` | 0.577 |
| `activation.resolve` | 0.518 |
| `activation.params_ps` + `params_vs` | 0.627 |
| `activation.bind` - must stay on the submitting thread | 0.314 |

Roughly a third of an activation is the game's own function and cannot move at
all, and the bind cannot leave the submit thread. That is a smaller
parallelisable fraction than the simulation assumed, so 3.59x should be read
as an upper bound that nothing will reach rather than a target.

These are menu numbers. Gameplay is the case that matters - it is where
`indexed.native` dominates and where the earlier 62.8% draw-thread figure came
from - and the same breakdown has not been taken there yet. It should be, before
the record buffer is sized or the worker count is chosen.

## How the port actually has to happen

Two facts decide this, and neither was obvious before a backend existed.

**Backends cannot be mixed inside a frame.** A D3D12 render target cannot be
composited by a D3D11 path, and two D3D11 devices cannot share a texture
either. Taken alone, that would force the whole 3,529-line port to land as one
change that either works or does not.

**But a backend can adopt the device the renderer already owns.** With
`AdoptNativeD3D11Backend`, ported paths and unported paths draw into the same
targets, so the port can go one path at a time with the game playable after
each. `--edf_native_backend=d3d11` does exactly this in the game today.

So the order is:

1. **Backend selection in the game.** Done. `--edf_native_backend` creates a
   backend inside the real process and reports it; an unknown name is refused
   with the reason and the valid names in the log.
2. **Move paths onto the seam, D3D11 adopted underneath.** Each path
   verifiable by playing. The renderer keeps working throughout, and any
   rendering change is a wiring mistake, because the backend underneath has
   not changed.
3. **Restructure constants as each path moves.** Not a separate step: a path
   on the seam already carries its own constant bytes through `SetConstants`,
   which is what makes draws independent. `ShaderBindings::ConstantImages`
   supplies them.
4. **Flip to D3D12.** One change, once nothing calls the context directly.
   The conformance test is what says the flip was clean.
5. **Then threads**, which only step 3 makes possible.

### What has to move, measured

The bridge itself barely touches D3D11 directly - about 17 context calls. The
work is in what it hands the context to: 69 sites pass `*state.context.Get()`
to a helper, and those helpers are the port.

| | |
|---|---|
| `state.context` / `state.device` references in the bridge | 137 |
| direct context calls in the bridge | ~17 |
| sites handing the context to a helper | 69 |
| D3D11-specific lines across the renderer | 3,529 |

Helpers to move, roughly in dependency order: `d3d11_render_state` (already
decoded through the shared path, so only the objects remain),
`d3d11_bindings`, `d3d11_mesh`, `d3d11_texture`, `d3d11_quads`, then the
effect paths (`d3d11_ui`, font, movie, XUI) and the presenter.

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
