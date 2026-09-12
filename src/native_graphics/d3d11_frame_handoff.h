#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <functional>
#include <cstdint>
#include <optional>
#include "native_display_gamma.h"

namespace edf::native {
enum class NativeFrameKind { Movie, PartialScene };
// A single GPU snapshot, independent of guest resource lifetime. Publish and
// Visit MUST share the renderer's context mutex. Callbacks must not reenter
// this object or retain its borrowed resources. GPU ordering uses the same
// immediate context, so CPU readback/waits are unnecessary.
class NativeFrameHandoff {
 public:
  using Consumer=std::function<void(ID3D11Device&,ID3D11DeviceContext&,
      ID3D11ShaderResourceView&,uint64_t,NativeFrameKind,const NativeDisplayGamma*)>;
  NativeFrameHandoff(ID3D11Device& device,ID3D11DeviceContext& context);
  NativeFrameHandoff(const NativeFrameHandoff&)=delete;
  NativeFrameHandoff& operator=(const NativeFrameHandoff&)=delete;
  void Publish(ID3D11Texture2D& source,NativeFrameKind kind,const NativeDisplayGamma* gamma=nullptr);
  void Invalidate() { valid_=false; gamma_.reset(); }
  // Temporarily isolates pipeline state, restoring it even if the callback
  // throws. Valid means initialized pixels, NOT complete game-frame fidelity.
  bool Visit(const Consumer& consumer);
  // Isolated context access for host resource creation/destruction, even when
  // no frame exists. Same serialization and non-reentry contract.
  void VisitContext(const std::function<void(ID3D11Device&,ID3D11DeviceContext&)>& consumer);
 private:
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context_;
  Microsoft::WRL::ComPtr<ID3DDeviceContextState> presentation_state_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> snapshot_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
  UINT width_=0,height_=0;
  uint64_t sequence_=0;
  NativeFrameKind kind_=NativeFrameKind::PartialScene;
  bool valid_=false;
  std::optional<NativeDisplayGamma> gamma_;
};
}
