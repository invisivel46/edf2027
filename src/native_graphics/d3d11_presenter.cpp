#include "d3d11_presenter.h"
#include <stdexcept>

namespace edf::native {
namespace {
void Check(HRESULT result, const char* operation) {
  if (FAILED(result)) throw std::runtime_error(operation);
}
}
NativeWindowPresenter::NativeWindowPresenter(HWND window, ID3D11Device& device,
    ID3D11DeviceContext& context)
    : window_(window), thread_(GetCurrentThreadId()), device_(&device), context_(&context) {
  if (!IsWindow(window) || GetWindowThreadProcessId(window,nullptr)!=thread_)
    throw std::runtime_error("native presenter requires an owned window on the calling thread");
  Microsoft::WRL::ComPtr<ID3D11Device> owner;
  context.GetDevice(&owner);
  if (owner.Get()!=&device || context.GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
    throw std::runtime_error("native presenter requires the device's immediate context");
  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
  Check(device.QueryInterface(IID_PPV_ARGS(&dxgi_device)),"native presenter DXGI device");
  Check(dxgi_device->GetAdapter(&adapter),"native presenter DXGI adapter");
  Check(adapter->GetParent(IID_PPV_ARGS(&factory)),"native presenter DXGI factory");
  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width=1; desc.Height=1; desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount=2; desc.Scaling=DXGI_SCALING_STRETCH;
  desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD; desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
  Check(factory->CreateSwapChainForHwnd(&device,window,&desc,nullptr,nullptr,&swap_chain_),
        "native presenter swap chain creation");
  Check(factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER),
        "native presenter window association");
}
NativeWindowPresenter::~NativeWindowPresenter() {
  // The immediate context retains bound views independently of this object.
  // Drop that reference before releasing the flip chain; otherwise recreating
  // a presenter for the same HWND can fail even after this object is gone.
  context_->OMSetRenderTargets(0,nullptr,nullptr);
  target_.Reset();
  swap_chain_.Reset();
  context_->Flush();
}
void NativeWindowPresenter::CheckThread() const {
  if (GetCurrentThreadId()!=thread_ || !IsWindow(window_))
    throw std::runtime_error("native presenter window/thread lifetime violation");
}
bool NativeWindowPresenter::BeginFrame(UINT width, UINT height) {
  CheckThread();
  suspended_=true;
  if (!width || !height || IsIconic(window_)) return false;
  if (width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
    throw std::runtime_error("native presenter client size exceeds D3D11 limits");
  if (!target_ || width!=width_ || height!=height_) {
    context_->OMSetRenderTargets(0,nullptr,nullptr);
    target_.Reset();
    Check(swap_chain_->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0),
          "native presenter resize failed (release borrowed back-buffer references)");
    Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
    Check(swap_chain_->GetBuffer(0,IID_PPV_ARGS(&buffer)),"native presenter back buffer");
    Check(device_->CreateRenderTargetView(buffer.Get(),nullptr,&target_),"native presenter target view");
    width_=width; height_=height;
  }
  suspended_=false;
  return true;
}
bool NativeWindowPresenter::Present(bool vsync) {
  CheckThread();
  if (suspended_) return false;
  last_sync_interval_=vsync?1u:0u;
  const auto result=swap_chain_->Present(last_sync_interval_,0);
  last_present_=result;
  Check(result,"native presenter present failed");
  return result!=DXGI_STATUS_OCCLUDED;
}
bool NativeWindowPresenter::TestVisibility() {
  CheckThread();
  if(suspended_) return false;
  const auto result=swap_chain_->Present(0,DXGI_PRESENT_TEST);
  Check(result,"native presenter visibility test failed");
  return result!=DXGI_STATUS_OCCLUDED;
}
NativePresentationDiagnostics NativeWindowPresenter::Diagnostics() const {
  CheckThread();
  NativePresentationDiagnostics result;
  result.last_present=last_present_;
  result.last_sync_interval=last_sync_interval_;
  Check(swap_chain_->GetDesc(&result.chain),"native presenter description failed");
  Microsoft::WRL::ComPtr<IDXGIOutput> output;
  result.output_result=swap_chain_->GetContainingOutput(&output);
  if(SUCCEEDED(result.output_result)) result.output_result=output->GetDesc(&result.output);
  return result;
}
}
