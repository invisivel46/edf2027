#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace edf::native {
struct NativePresentationDiagnostics {
  HRESULT last_present=S_FALSE;
  UINT last_sync_interval=0;
  DXGI_SWAP_CHAIN_DESC chain{};
  HRESULT output_result=E_FAIL;
  DXGI_OUTPUT_DESC output{};
};
// App-owned HWND presentation, independent of the Xbox GPU plugin. All calls
// and destruction belong to the window thread, with exclusive context access.
// BeginFrame may unbind render targets. Callers must rebind their draw state;
// no borrowed back-buffer references may survive a resize or destruction.
class NativeWindowPresenter {
 public:
  NativeWindowPresenter(HWND window, ID3D11Device& device, ID3D11DeviceContext& context);
  ~NativeWindowPresenter();
  NativeWindowPresenter(const NativeWindowPresenter&) = delete;
  NativeWindowPresenter& operator=(const NativeWindowPresenter&) = delete;
  // Zero-sized/minimized clients suspend presentation without allocating.
  bool BeginFrame(UINT width, UINT height);
  ID3D11RenderTargetView* target() const { return target_.Get(); }
  // false means occluded/suspended, not device failure. Failures throw.
  bool Present(bool vsync = true);
  // Query visibility without submitting another image. Used to idle while
  // occluded; a successful test does not prove that a frame was displayed.
  bool TestVisibility();
  NativePresentationDiagnostics Diagnostics() const;
 private:
  void CheckThread() const;
  HWND window_;
  DWORD thread_;
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
  UINT width_ = 0, height_ = 0;
  bool suspended_ = true;
  HRESULT last_present_=S_FALSE;
  UINT last_sync_interval_=0;
};
}
