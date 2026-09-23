#pragma once
#include "native_render_backend.h"
#include <d3d12.h>
#include <functional>
#include <span>

namespace edf::native {
// Raw D3D12 access for work the backend seam does not express: FidelityFX
// records its own compute dispatches into a command list it is handed, with
// its own root signature, descriptor heaps and barriers. Obtained from
// NativeRenderBackend::D3D12Raw(); null on any other backend.
//
// The command list only exists inside RecordRaw, because that is the one
// point where the backend can say where in the stream it is. Ordering:
//
//   * RecordRaw(recorder, ...) calls recorder.BeginExternal(). A packet
//     recorder (NativeParallelRecorder) flushes there: pending draws are either
//     replayed onto recorder 0 or recorded by the workers and submitted, and
//     either way recorder 0's command list is then the tail of the stream. The
//     body records into that list, so it executes after every draw recorded
//     before RecordRaw and before every draw recorded after it. The flush is
//     the same one a ClearColor or CopyTexture already costs.
//   * A direct recorder (no packet workers) records into its own list, which
//     is ordered against the other recorders' lists exactly as its draws are.
//   * `uses` are transitioned first, in this list. A resource's first use in a
//     list resolves its barrier in the list's preamble at submit (the same
//     rule every draw follows), a later one records the barrier in place, so
//     at the body's first command each listed resource is in the given state.
//   * The body must leave every resource it was handed in the state it was
//     handed in (FFX does: FfxApiResource.state is both its input and output
//     state), or transition it through pass.Transition, which keeps the
//     backend's record right. Anything else desynchronises state tracking.
//   * Afterwards the recorder's descriptor heaps and graphics root signature
//     are set again and every binding is forgotten (BeginExternal's contract).
//
// Resources handed out here stay owned by the backend handle; the pointer is
// valid while the handle lives, and must not be released or kept past it.
class NativeD3D12RawPass {
 public:
  virtual ID3D12Device* device() const=0;
  virtual ID3D12GraphicsCommandList* commands() const=0;
  virtual ID3D12CommandQueue* queue() const=0;
  // Puts a backend resource in `state` at this point of commands(), and
  // records that it is there. Handles of another backend throw.
  virtual void Transition(NativeBackendTexture& texture,D3D12_RESOURCE_STATES state)=0;
  virtual void Transition(NativeBackendRenderTarget& target,D3D12_RESOURCE_STATES state)=0;
 protected:
  ~NativeD3D12RawPass()=default;
};

// One resource a raw pass needs, and the state it needs it in. Exactly one of
// texture or target. A sampled render target's texture() and the target are
// one resource and share one state.
struct NativeD3D12RawUse {
  NativeBackendTexture* texture=nullptr;
  NativeBackendRenderTarget* target=nullptr;
  D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;
};

class NativeD3D12RawAccess {
 public:
  virtual ID3D12Device* Device() const=0;
  virtual ID3D12CommandQueue* Queue() const=0;
  // The resource behind a handle this backend created. A depth target's is
  // the typeless resource when it was created `sampled` (its desc reports
  // R32G8X24_TYPELESS; FFX's ffxApiGetResourceDX12 maps that to R32 depth).
  virtual ID3D12Resource* Resource(NativeBackendTexture& texture) const=0;
  virtual ID3D12Resource* Resource(NativeBackendRenderTarget& target) const=0;
  // See the ordering notes above. Throws when no frame is open, when
  // `recorder` is not this backend's, or when the body throws (after the
  // recorder's state has been restored).
  virtual void RecordRaw(NativeBackendRecorder& recorder,std::span<const NativeD3D12RawUse> uses,
                         const std::function<void(NativeD3D12RawPass&)>& body)=0;
 protected:
  ~NativeD3D12RawAccess()=default;
};
}  // namespace edf::native
