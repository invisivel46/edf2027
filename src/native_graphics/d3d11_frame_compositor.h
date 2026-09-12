#pragma once
#include "d3d11_effect.h"
#include "native_display_gamma.h"

namespace edf::native {
// Present an already tone-mapped RGBA8 frame. No guest memory, GPU packets,
// readback, or implicit HDR conversion. Optional explicit display gamma applies
// to source pixels before scaling; a null gamma preserves the original pass.
// Owns pipeline state during Draw (ClearState); caller must serialize context
// access and rebind any subsequent scene/UI state. Target remains bound for UI.
class NativeFrameCompositor {
 public:
  explicit NativeFrameCompositor(ID3D11Device& device);
  void Draw(ID3D11DeviceContext& context, ID3D11ShaderResourceView& source,
            ID3D11RenderTargetView& destination, bool preserve_aspect = true,
            const NativeDisplayGamma* gamma = nullptr);
 private:
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  NativeShader vertex_, pixel_;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> gamma_constants_;
};
}
