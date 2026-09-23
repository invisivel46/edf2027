#pragma once
#include "d3d12_device.h"
#include "native_render_backend.h"
#include <filesystem>
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
std::unique_ptr<NativeRenderBackend> CreateNativeD3D12SceneBackend(bool warp,uint32_t workers);
// Upload-ring size for backends built through the registry, in megabytes.
// Set before the backend is created; ignored afterwards. Exists because the
// right value is a measurement of one frame's constants, not a constant.
void SetNativeD3D12UploadMegabytes(uint32_t megabytes);
// Turns the debug layer on for backends built through the registry, including
// the hardware one. Only the WARP variant had it, which is the wrong way round
// for finding a hang: the hang happens on hardware, and WARP is far too slow to
// reach the part of the game where it happens.
void SetNativeD3D12DebugLayer(bool enabled);
// Where the scene backend keeps its persistent pipeline manifest
// (d3d12_pipelines.bin, or d3d12_pipelines_warp.bin for WARP). Empty (the
// default) keeps no manifest. Only the scene backend uses it: it is the one
// that builds the game's pipelines, and one writer per file keeps two backends
// from overwriting each other's.
void SetNativeD3D12SceneCacheDirectory(std::filesystem::path directory);

std::unique_ptr<NativeRenderBackend> CreateNativeD3D12Backend(const NativeD3D12Options& options={});
}  // namespace edf::native
