#pragma once
#include "d3d11_effect.h"
#include <span>
#include <vector>
#include <cstdint>

namespace edf::native {
struct NativeUiVertex { float x,y,u,v; uint32_t color; };
static_assert(sizeof(NativeUiVertex)==20);
struct NativeUiTexture {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> resource;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
};
NativeUiTexture CreateNativeUiTexture(ID3D11Device& device,UINT width,UINT height,
    std::span<const uint8_t> rgba,bool linear,bool repeat);
struct NativeUiDraw {
  UINT count=0,index_offset=0;
  int base_vertex=0;
  bool lines=false;
  RECT scissor{}; // Render-target pixels; SDK adapter performs coordinate conversion.
  const NativeUiTexture* texture=nullptr; // Null means solid vertex color.
};
// Host overlay renderer, not a replacement for the game's XUI shader path.
// Upload/Draw require exclusive immediate-context access. Draw replaces pipeline
// state but preserves target contents; use the isolated presentation scope.
class NativeUiRenderer {
 public:
  explicit NativeUiRenderer(ID3D11Device& device);
  NativeUiRenderer(const NativeUiRenderer&)=delete;
  NativeUiRenderer& operator=(const NativeUiRenderer&)=delete;
  void Upload(ID3D11DeviceContext& context,std::span<const NativeUiVertex> vertices,
              std::span<const uint16_t> indices);
  void Draw(ID3D11DeviceContext& context,ID3D11RenderTargetView& target,
            UINT width,UINT height,float coordinate_width,float coordinate_height,
            const NativeUiDraw& draw);
 private:
  void CheckContext(ID3D11DeviceContext& context) const;
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  NativeShader vertex_,pixel_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> vertices_,indices_,projection_;
  Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
  NativeUiTexture white_;
  UINT vertex_capacity_=0,index_capacity_=0,vertex_count_=0;
  std::vector<uint16_t> index_values_;
};
}
