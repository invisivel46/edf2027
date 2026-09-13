#pragma once
#include "d3d12_device.h"
#include "native_render_backend.h"
#include <memory>

namespace edf::native {
// The D3D12 implementation of the backend seam.
//
// Registration is an explicit call rather than a static initialiser. A static
// one would make the set of available backends depend on link order and on
// whether the linker felt like keeping an object nothing references - which is
// exactly the kind of thing that works on one machine and silently produces a
// shorter backend list on another.
void RegisterNativeD3D12Backend();

std::unique_ptr<NativeRenderBackend> CreateNativeD3D12Backend(const NativeD3D12Options& options={});
}  // namespace edf::native
