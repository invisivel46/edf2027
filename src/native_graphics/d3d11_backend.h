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

// Wrap resources the renderer already owns so a path can be ported before the
// paths that create its inputs are.
//
// This is the other half of adoption, and the reason the port can go one path
// at a time: a ported draw needs a render target and textures, and until the
// code that creates those has moved, they are D3D11 objects. Both throw if the
// backend is not an adopting D3D11 backend, because doing this across devices
// would silently produce a resource the target device cannot use.
//
// Neither takes ownership. The caller must keep the underlying view alive, and
// must not hold the wrapper across a resize that replaces it.
std::unique_ptr<NativeBackendTexture> AdoptNativeD3D11Texture(
    NativeRenderBackend& backend, ID3D11ShaderResourceView& view, uint32_t width, uint32_t height);
// The D3D11 objects behind a resource this backend created, for consumers that
// have not moved onto the seam yet.
//
// The mirror image of adoption: adoption wraps a D3D11 resource so a ported
// path can use it, this unwraps a seam resource so an unported path can. Both
// exist only while the port is half done, and both disappear with it. Each
// returns null if the resource did not come from a D3D11 backend, which is a
// real case - the player can select d3d12 - and so is checked, not assumed.
ID3D11Buffer* NativeD3D11Buffer(NativeBackendBuffer& buffer);
ID3D11ShaderResourceView* NativeD3D11TextureView(NativeBackendTexture& texture);
ID3D11Texture2D* NativeD3D11TextureResource(NativeBackendTexture& texture);
// The device itself, for the few things that still create D3D11 objects of
// their own: an input layout to bind, a stream-output replay to diagnose with.
// Null if this is not a D3D11 backend.
ID3D11Device* NativeD3D11BackendDevice(NativeRenderBackend& backend);

std::unique_ptr<NativeBackendRenderTarget> AdoptNativeD3D11RenderTarget(
    NativeRenderBackend& backend, ID3D11RenderTargetView* colour, ID3D11DepthStencilView* depth,
    uint32_t width, uint32_t height);
}  // namespace edf::native
