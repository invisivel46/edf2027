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
std::unique_ptr<NativeRenderBackend> CreateNativeD3D11Backend(const NativeD3D11BackendOptions& options={});
}  // namespace edf::native
