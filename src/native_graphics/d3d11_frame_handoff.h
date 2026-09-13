#pragma once
#include <d3d11_4.h>
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

  // Cross-API handles for the published frame, so another API can sample it
  // where it lies instead of reading it back through system memory.
  //
  // The fence is what makes that safe: the copy into the snapshot is GPU work,
  // and a consumer that sampled without waiting for it would read a surface
  // mid-write. Both handles are owned here and stay valid for the run; the
  // value increases with every publication.
  struct SharedFrame {
    void* texture=nullptr;
    void* fence=nullptr;
    uint64_t value=0;
    uint32_t width=0,height=0,format=0;
    explicit operator bool() const { return texture && fence; }
  };
  SharedFrame Shared() const;
  uint64_t SharedSequence() const { return sequence_; }
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
  Microsoft::WRL::ComPtr<ID3D11Fence> fence_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext4> fenced_context_;
  void* shared_texture_=nullptr;
  void* shared_fence_=nullptr;
  uint64_t fence_value_=0;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
  UINT width_=0,height_=0;
  uint64_t sequence_=0;
  NativeFrameKind kind_=NativeFrameKind::PartialScene;
  bool valid_=false;
  std::optional<NativeDisplayGamma> gamma_;
};
}
