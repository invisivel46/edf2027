#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>

namespace edf::native {
// Blend, depth, raster, alpha control, RT0 channel mask, scissor enable.
using RenderStateWords = std::array<uint32_t,6>;
struct NativeViewportState {
  D3D11_VIEWPORT viewport;
  D3D11_RECT scissor;
  bool reverse_depth = false;
  void Bind(ID3D11DeviceContext& context) const;
};
NativeViewportState ScaleNativeCanvasScissor(NativeViewportState state,float scale_x,float scale_y,bool centered);
NativeViewportState MakeNativeViewport(uint32_t x,uint32_t y,uint32_t width,uint32_t height,
  float min_depth,float max_depth,bool scissor_enabled,const std::array<int32_t,4>& scissor);
// Caller must select the reversed-depth vertex variant when reverse_depth is
// true. The viewport itself always has a legal ascending native range.
NativeViewportState MakeNativeDrawViewport(uint32_t x,uint32_t y,uint32_t width,uint32_t height,
  float min_depth,float max_depth,bool scissor_enabled,const std::array<int32_t,4>& scissor);
struct NativeRenderState {
  Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;
  bool requires_blend_factor = false;
  bool replicate_blend_alpha = false;
  void Bind(ID3D11DeviceContext& context) const;
  void Bind(ID3D11DeviceContext& context,const std::array<float,4>& factor) const;
};
NativeRenderState CreateNativeRenderState(ID3D11Device& device,const RenderStateWords& words);
}
