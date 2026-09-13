#pragma once
#include <filesystem>
#include <memory>
#include "d3d11_frame_handoff.h"
namespace edf::native {
class GuestMeshWatchAudit;
// Weak registration: the application owns the audit within SDK Memory lifetime.
void SetNativeMeshWatchAudit(std::weak_ptr<GuestMeshWatchAudit> audit);
// Transitional reference-run bridge: constructs native resources from the
// live guest shader loader while the existing renderer remains the oracle.
void InitializeGuestShaderBridge(const std::filesystem::path& game_root);
// Serialized, pipeline-isolated access to the latest published native image.
// No callbacks occur until publication is enabled and an image is available.
// Handles for the published frame, so a backend can sample it in place.
// False when nothing has been published or the surface is not shareable.
bool VisitNativePresentationSharedFrame(NativeFrameHandoff::SharedFrame& shared,uint64_t& sequence);
bool VisitNativePresentationFrame(const NativeFrameHandoff::Consumer& consumer);
bool VisitNativePresentationContext(
    const std::function<void(ID3D11Device&,ID3D11DeviceContext&)>& consumer);
}
