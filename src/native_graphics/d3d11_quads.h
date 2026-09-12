#pragma once
#include "d3d11_effect.h"
#include "d3d11_render_state.h"

namespace edf::native {
// XUI basic brushes consume only big-endian float2 positions in triangle lists.
class PositionTriangleStream {
 public:
  PositionTriangleStream(ID3D11Device& device,const NativeShader& vertex_shader);
  PositionTriangleStream(const PositionTriangleStream&)=delete;
  PositionTriangleStream& operator=(const PositionTriangleStream&)=delete;
  void Draw(ID3D11DeviceContext& context,std::span<const uint8_t> guest_vertices);
 private:
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> vertices_;
  UINT capacity_=0;
};
// Verified retail PostEffect VS_Main and its unconditional full-screen pixel entries. An unknown or
// modified shader, partial quad, masked/blended draw, or missing input cannot
// establish initialized target contents. This is not a frame-fidelity claim.
bool CanInitializeReductionTarget(const NativeShader& vertex,const NativeShader& pixel,
  std::span<const uint8_t> guest_vertices,const NativeViewportState& viewport,
  const RenderStateWords& state,uint32_t width,uint32_t height,bool inputs_bound);
// Immediate POSITION0=float2, TEXCOORD0=float2 stream used by the engine's
// post quads. Other vertex declarations must use their own checked adapter.
class QuadStream {
 public:
  QuadStream(ID3D11Device& device, const NativeShader& vertex_shader);
  void Draw(ID3D11DeviceContext& context, std::span<const uint8_t> guest_vertices);
  // XUI's observed immediate producer uses triangle strips with the same
  // float2 position/float2 UV layout. Shader identity remains a separate gate.
  void DrawTriangleStrip(ID3D11DeviceContext& context,std::span<const uint8_t> guest_vertices);
 private:
  void DrawStream(ID3D11DeviceContext& context,std::span<const uint8_t> guest_vertices,bool strip);
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> vertices_;
  UINT capacity_ = 0;
};
}
