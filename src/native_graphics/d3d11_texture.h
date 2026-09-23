#pragma once
#include "native_render_backend.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <span>
#include <cstdint>
#include <vector>
#include <array>
#include <stdexcept>

namespace edf::native {
// Scene-only override; post-processing and UI targets stay single-sampled.
inline uint32_t NativeSceneSamples(uint32_t guest_mode, int32_t override_samples) {
  if (guest_mode>2) throw std::runtime_error("unsupported guest scene sample mode");
  if (override_samples==0) return 1u<<guest_mode;
  if (override_samples==1 || override_samples==2 || override_samples==4)
    return static_cast<uint32_t>(override_samples);
  throw std::runtime_error("native MSAA must be 0 (game default), 1 (off), 2 or 4");
}
struct NativeTexture {
  // Created through the backend. The D3D11 handles below name the same
  // objects, and are kept only while the paths that sample this texture are
  // still D3D11; they go when those paths move onto the seam.
  std::shared_ptr<NativeBackendTexture> backend;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> resource;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
  uint32_t width = 0, height = 0, mip_count = 0;
  uint32_t format = 0;
  bool cube = false;
  // Describes initialized pixel data, not complete game-frame fidelity.
  // DDS imports are initialized. Render-target textures become valid only
  // after initialized surface contents are resolved; allocation alone isn't data.
  bool content_valid = true;
};
struct NativeRenderTarget {
  enum class Conversion { none, luminance, rgba8 };
  // What draws go into, created through the backend. Declared sampled when a
  // conversion has to read it back.
  std::shared_ptr<NativeBackendRenderTarget> backend_surface;
  // The same surface's D3D11 handles, for the paths that have not moved:
  // presentation hand-off, the HDR/depth diagnostics, and the output-merger
  // binding. They go with those paths.
  Microsoft::WRL::ComPtr<ID3D11Texture2D> surface;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
  NativeTexture sampled;
  // What a pipeline drawing into this has to declare. Remembered rather than
  // asked for: D3D11 would answer from the resource, and neither target API
  // will.
  uint32_t format = 0, samples = 1;
  bool content_valid = false;
  Conversion conversion=Conversion::none;
  // A converting target's resolve is a full-screen draw that reads the surface
  // and writes the converted result. It was a compute dispatch writing through
  // an unordered-access view, which is the one thing in this renderer the seam
  // cannot express - and a per-pixel copy is a draw on any API.
  std::shared_ptr<NativeBackendRenderTarget> converted_target;
  std::unique_ptr<NativeBackendBuffer> convert_vertices;
  NativeBackendPipeline* convert_pipeline=nullptr;
};
struct NativeDepthTarget {
  std::shared_ptr<NativeBackendRenderTarget> backend_target;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> surface;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> target;
  uint32_t width = 0, height = 0;
  uint32_t format = 0, samples = 1;
  bool has_stencil = false;
  bool depth_valid = false, stencil_valid = false;
};
// Native host depth storage. Guest depth format mapping and explicit
// depth resolves are separate contracts; no implicit conversion is performed.
// `clear_depth` is the optimized clear the target declares (0 for the
// reversed-Z scene); `sampled` gives a single-sampled target a depth-plane
// SRV as backend_target->texture() (NativeBackendTextureDesc::sampled) and is
// ignored when multisampled, so a caller may ask for it unconditionally.
NativeDepthTarget CreateNativeDepthTarget(NativeRenderBackend& backend,uint32_t width,
                                          uint32_t height,DXGI_FORMAT format,uint32_t samples=1,
                                          float clear_depth=1.0f,bool sampled=false);
void ClearNativeDepthTarget(ID3D11DeviceContext& context,NativeDepthTarget& target,
                            bool depth,bool stencil,float depth_value,uint8_t stencil_value);
void ClearNativeDepthTarget(NativeBackendRecorder& recorder,NativeDepthTarget& target,
                            bool depth,bool stencil,float depth_value,uint8_t stencil_value);
NativeRenderTarget CreateNativeRenderTarget(NativeRenderBackend& backend, uint32_t width,
                                           uint32_t height, DXGI_FORMAT format,uint32_t samples=1);
// Separate surface and sampled texture retain the guest's explicit resolve
// boundary and permit a pass to sample the previous resolved contents.
void ResolveNativeRenderTarget(ID3D11DeviceContext& context, NativeRenderTarget& target);
// The same resolve recorded. Required for a converting target on a backend
// that is not D3D11, because the conversion is a draw.
//
// A converting target's resolve therefore leaves the recorder's render targets,
// viewport, scissor and pipeline set to the conversion's own. It cannot put
// them back: a recorder has no getters, deliberately, because neither target
// API keeps state to read. Callers that cache what they last bound must treat
// this as having bound something else. The compute pass this replaces did
// restore them, which is exactly the assumption that has to go.
void ResolveNativeRenderTarget(NativeBackendRecorder& recorder, NativeRenderTarget& target);
// Full-surface packed ARGB clear. Does not implicitly resolve the sampled view.
void ClearNativeColorTarget(ID3D11DeviceContext& context, NativeRenderTarget& target,
                           uint32_t argb);
void ClearNativeColorTarget(NativeBackendRecorder& recorder, NativeRenderTarget& target,
                           uint32_t argb);
// The guest's packed ARGB clear colour, as the four floats a clear takes.
// Shared so the two overloads cannot disagree about channel order.
std::array<float,4> NativeClearColor(uint32_t argb);
// Luminance pair: R32F surface -> half-precision R111 sampled values.
// RGBA16F host storage represents the guest texture's fixed channel mapping.
NativeRenderTarget CreateNativeLuminanceTarget(NativeRenderBackend& backend,uint32_t width,uint32_t height);
// HDR render surface -> clamped/quantized RGBA8 sampled bloom texture.
NativeRenderTarget CreateNativeBloomTarget(NativeRenderBackend& backend,uint32_t width,uint32_t height);
NativeRenderTarget CreateNativeOpaqueFrameTarget(NativeRenderBackend& backend,uint32_t width,uint32_t height);
// Direct color resolve to RGBA8, without exposure, bloom or gamma. Destination
// is a dedicated bloom/opaque-frame conversion pair, never the HDR history.
void ResolveNativeRgba8Frame(NativeBackendRecorder& recorder,const NativeRenderTarget& source,
                             NativeRenderTarget& destination);
// Initial upload only: a 1x1 R16F texture fits in its first 4KiB backing page.
// Uniform zero bytes have the same value for any tiling/endian layout.
bool ImportZeroLuminanceHistory(ID3D11DeviceContext& context,NativeRenderTarget& target,
                                std::span<const uint8_t> initial_page);
bool ImportZeroLuminanceHistory(NativeBackendRecorder& recorder,NativeRenderTarget& target,
                                std::span<const uint8_t> initial_page);
// Native DDS upload: retains BC1/BC2/BC3 compression and all authored mip/face
// data. RGB mask formats and alpha-only A8 are converted to RGBA8. No Xbox texture descriptors,
// tiled GPU memory, or GPU command processing is involved.
// Creates through the backend rather than a device. The decode above is
// already shared, so this is the whole of what was API-specific about loading
// the game's textures.
NativeTexture CreateNativeDdsTexture(NativeRenderBackend& backend, std::span<const uint8_t> dds);
// Diagnostic only: clamp linear HDR RGB to [0,1], or retain RGBA8 output bytes,
// in a top-down 24-bit BMP.
// Nonfinite RGB becomes magenta. This is not the game's tone mapping, and
// reading a partial target does not mark its contents valid for presentation.
std::vector<uint8_t> CaptureNativeHdrBmp(ID3D11DeviceContext& context,ID3D11Texture2D& surface);
// Packed pixels to a 24-bit BMP, and the same capture taken through the seam.
// The D3D11 one above reads an ID3D11Texture2D, which a scene drawn on another
// backend has not got - and a port whose output cannot be looked at is a port
// nobody can say is working.
std::vector<uint8_t> EncodeNativeBmp(std::span<const uint8_t> pixels,uint32_t width,uint32_t height,
                                     uint32_t format);
// Blocking: it waits for the GPU. Diagnostics only, never on a frame path, and
// never inside an open frame - submit first.
std::vector<uint8_t> CaptureNativeBmp(NativeRenderBackend& backend,NativeBackendRenderTarget& target,
                                      uint32_t format);
// A resolved scene is a texture rather than a target, and it is the picture
// worth looking at.
std::vector<uint8_t> CaptureNativeBmp(NativeRenderBackend& backend,NativeBackendTexture& texture,
                                      uint32_t format);
// Diagnostic one-pixel readback, without quantizing HDR or changing validity.
std::array<float,4> ReadNativeColorPixel(ID3D11DeviceContext& context,ID3D11Texture2D& surface,uint32_t x,uint32_t y);
std::array<float,4> ReadNativeColorPixel(NativeRenderBackend& backend,NativeBackendTexture& texture,
                                      uint32_t format,uint32_t x,uint32_t y);
// Diagnostic region scan. Returns the first nonfinite RGB pixel in row order;
// finite magenta and nonfinite alpha alone are not errors. No rendering state
// or content-validity flag is changed. Coordinates are absolute surface pixels.
// With include_negative, a channel at or below -1 also stops the scan: the
// game's tone curve turns a large negative into saturated white, so it is a
// visible defect even though it is a perfectly finite value.
bool FindNativeInvalidColorPixel(ID3D11DeviceContext& context,ID3D11Texture2D& surface,
                                uint32_t x,uint32_t y,uint32_t width,uint32_t height,
                                uint32_t& found_x,uint32_t& found_y,bool include_negative=false);
// Unquantized HDR range of a whole diagnostic color surface. The BMP capture
// clamps to [0,1], so it cannot show whether a scene still carries highlights
// above 1 for the tone curve to compress. This reads the real values instead.
// Nonfinite pixels are counted, not folded into the min/max or the mean.
struct NativeHdrRange {
  uint64_t pixels=0,nonfinite_pixels=0;
  std::array<float,3> minimum{},maximum{};
  std::array<double,3> mean{};
  // Pixels whose maximum finite channel exceeds 1, 2, 4 and 8.
  std::array<uint64_t,4> above{};
  // Negative radiance cannot come from a correct scene, and the game's tone
  // curve turns a large negative into saturated white, so count it separately
  // and keep the worst pixel's coordinates for the invalid-RGB draw probe.
  uint64_t negative_pixels=0;
  uint32_t worst_x=0,worst_y=0;
};
NativeHdrRange InspectNativeHdrColor(ID3D11DeviceContext& context,ID3D11Texture2D& surface);
struct NativeDepthCoverage {
  uint64_t changed_pixels=0,nonfinite_pixels=0;
  float minimum=1,maximum=0;
  std::array<uint64_t,3> vertical_bands{};
};
// Read the development scene's D32/D32S8 depth, without altering validity.
NativeDepthCoverage InspectNativeDepth(ID3D11DeviceContext& context,ID3D11Texture2D& surface,float clear_depth);
}
