#pragma once
#include <d3d11_4.h>
#include <wrl/client.h>
#include <cstdint>
#include <stdexcept>

namespace edf::native {
// A render target this device draws into and another API samples.
//
// The fence is the whole point. Drawing into the surface is GPU work, and a
// consumer that sampled as soon as the CPU returned would read it mid-write -
// which looks like tearing or a frame of garbage rather than like missing
// synchronisation. Signal() is called after the last draw of the frame, and
// the consumer waits for that value before sampling.
//
// Returns handles rather than lending resources, because the consumer is on a
// different device and can only take a handle.
class NativeSharedSurface {
 public:
  NativeSharedSurface()=default;
  ~NativeSharedSurface() { Release(); }
  NativeSharedSurface(const NativeSharedSurface&)=delete;
  NativeSharedSurface& operator=(const NativeSharedSurface&)=delete;

  // False when the device cannot share, in which case the caller must keep
  // using whatever path it had - sharing is an optimisation, not a contract.
  bool Resize(ID3D11Device& device, uint32_t width, uint32_t height);
  // Signals the fence after everything recorded so far.
  void Signal(ID3D11DeviceContext& context);

  ID3D11RenderTargetView* target() const { return target_.Get(); }
  ID3D11Texture2D* texture() const { return texture_.Get(); }
  void* shared_texture() const { return shared_texture_; }
  void* shared_fence() const { return shared_fence_; }
  uint64_t value() const { return value_; }
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  bool valid() const { return shared_texture_ && shared_fence_; }
  // Public so an owner can release it inside whatever scope its device
  // requires, rather than only at destruction.
  void Release();

 private:

  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
  Microsoft::WRL::ComPtr<ID3D11Fence> fence_;
  void* shared_texture_=nullptr;
  void* shared_fence_=nullptr;
  uint64_t value_=0;
  uint32_t width_=0,height_=0;
};
}  // namespace edf::native
