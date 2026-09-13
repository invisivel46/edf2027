#pragma once
#include "native_render_state_decode.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <type_traits>

namespace edf::native {
// The root signature and the pipeline cache - consequence 3 of the migration
// note, where blend, depth, raster, input layout and the shader pair stop being
// five independent objects and fuse into one.
//
// The shape below is not a guess. `edf_native_shader_check --slots` reports the
// widest slot any of the 44 disc effects actually binds: 2 vertex constant
// buffers, 1 pixel constant buffer, 7 pixel textures, 7 pixel samplers, and no
// vertex textures at all. Declaring the D3D11 maxima instead would cost root
// space and descriptor traffic on every one of ~2,370 draws a frame.
//
// Constant buffers are root descriptors rather than a table: at 44,917 material
// activations a second, binding one is then a GPU virtual address written into
// the root arguments, with no descriptor to allocate or copy. Textures cannot
// be, because a root SRV can only address a buffer.
struct NativeD3D12RootLayout {
  static constexpr uint32_t kVertexConstantBuffers=2;
  static constexpr uint32_t kPixelConstantBuffers=1;
  // Eight, not the measured seven: a table is bound whole and the round number
  // costs nothing, while a shader that needs an eighth would otherwise have to
  // rebuild the signature.
  static constexpr uint32_t kPixelTextures=8;
  static constexpr uint32_t kPixelSamplers=8;

  // Root parameter indices, in the order they are declared.
  static constexpr uint32_t kVertexConstants0=0;
  static constexpr uint32_t kVertexConstants1=1;
  static constexpr uint32_t kPixelConstants0=2;
  static constexpr uint32_t kPixelTextureTable=3;
  static constexpr uint32_t kPixelSamplerTable=4;
  static constexpr uint32_t kParameterCount=5;
};

// Throws, naming the shader's own slot, when a shader binds outside the layout
// above. The disc shaders were measured; the renderer's own UI, font, movie and
// post HLSL was not, so this is the check that keeps an unmeasured shader from
// silently losing a binding.
void ValidateAgainstRootLayout(std::span<const uint8_t> bytecode, bool pixel, const std::string& name);

Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateNativeD3D12RootSignature(ID3D12Device& device);

// Everything that must match for two draws to share a pipeline. Compared as
// raw bytes, so it must have no padding to leave uninitialised - asserted
// below, because a padding byte would make identical draws miss the cache and,
// worse, make the cache's answer depend on stack garbage.
struct NativeD3D12PipelineKey {
  uint64_t vertex_shader=0,pixel_shader=0;  // Stable shader identity, caller's choice.
  uint64_t input_layout=0;                  // Hash of the element descriptions.
  // The render state words that are pipeline state. Scissor enable is
  // deliberately absent: D3D11 kept it in the rasteriser object, but in D3D12
  // the scissor rectangle is a command, so two draws differing only in
  // scissoring share a pipeline instead of building a second one.
  uint32_t blend=0,depth=0,raster=0,alpha=0,write_mask=0;
  uint32_t topology=0;                      // D3D12_PRIMITIVE_TOPOLOGY_TYPE.
  uint32_t render_targets=0;
  uint32_t rtv_format[8]{};
  uint32_t dsv_format=0;
  uint32_t sample_count=1;
  uint32_t sample_quality=0;
};
static_assert(std::has_unique_object_representations_v<NativeD3D12PipelineKey>,
              "the pipeline key is compared as bytes, so it must not contain padding");

class NativeD3D12PipelineCache {
 public:
  NativeD3D12PipelineCache(ID3D12Device& device, ID3D12RootSignature& signature);

  struct Request {
    NativeD3D12PipelineKey key;
    D3D12_SHADER_BYTECODE vertex{},pixel{};
    std::span<const D3D12_INPUT_ELEMENT_DESC> input_layout;
    RenderStateWords state{};
  };
  // A miss builds a pipeline, which is far more expensive than CreateBlendState
  // ever was - a cold miss during gameplay is a visible hitch, not a small
  // cost. That is why misses are counted: the number says whether the cache is
  // being warmed properly or is quietly rebuilding every frame.
  ID3D12PipelineState& Get(const Request& request);

  uint64_t hits() const { return hits_; }
  uint64_t misses() const { return misses_; }
  size_t size() const { return pipelines_.size(); }

 private:
  ID3D12Device* device_=nullptr;
  ID3D12RootSignature* signature_=nullptr;
  std::map<std::string,Microsoft::WRL::ComPtr<ID3D12PipelineState>> pipelines_;
  uint64_t hits_=0,misses_=0;
};
}  // namespace edf::native
