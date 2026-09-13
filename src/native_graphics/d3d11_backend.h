#pragma once
#include "native_render_backend.h"
#include <d3d11.h>
#include <memory>

namespace edf::native {
// The D3D11 implementation of the backend seam.
//
// This exists so the port can happen in two steps that each leave a working
// game: move the bridge onto the seam with D3D11 still underneath, then change
// which backend it asks for. Doing it in one step would mean every rendering
// difference could be either a wiring mistake or a backend mistake, with no
// way to tell them apart.
//
// It is also what makes the conformance test worth anything. The same test run
// against both backends is a comparison; run against one it is only a
// description of that one.
struct NativeD3D11BackendOptions {
  bool prefer_warp=false;
  bool debug_layer=false;
};

void RegisterNativeD3D11Backend();

// Wraps a device the caller already owns instead of creating one.
//
// This is what makes the port incremental. Backends cannot be mixed inside a
// frame - a D3D12 render target cannot be composited by a D3D11 path - so a
// half-ported renderer is only possible while everything is still on one
// device. Adopting the bridge's existing device lets paths move onto the seam
// one at a time, each verifiable by playing the game, with the unported ones
// still using the context directly. Only once every path is on the seam does
// swapping in D3D12 become a single change.
//
// The returned backend does not own the device and will not reset its state on
// destruction; the caller's context keeps whatever was bound.
std::unique_ptr<NativeRenderBackend> AdoptNativeD3D11Backend(ID3D11Device& device,
                                                             ID3D11DeviceContext& context);
std::unique_ptr<NativeRenderBackend> CreateNativeD3D11Backend(const NativeD3D11BackendOptions& options={});
}  // namespace edf::native
