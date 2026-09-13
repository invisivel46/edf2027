#include "d3d11_frame_handoff.h"
#include <dxgi1_2.h>
#include <stdexcept>

namespace edf::native {
NativeFrameHandoff::NativeFrameHandoff(ID3D11Device& device,ID3D11DeviceContext& context)
    : device_(&device) {
  Microsoft::WRL::ComPtr<ID3D11Device> owner;
  context.GetDevice(&owner);
  Microsoft::WRL::ComPtr<ID3D11Device1> extended;
  if(owner.Get()!=&device || context.GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE ||
     FAILED(context.QueryInterface(IID_PPV_ARGS(&context_))) || FAILED(device_.As(&extended)))
    throw std::runtime_error("native frame handoff requires matching D3D11.1 immediate context");
  const auto level=device.GetFeatureLevel();
  if(FAILED(extended->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,
      __uuidof(ID3D11Device),nullptr,&presentation_state_)))
    throw std::runtime_error("native presentation context-state creation failed");
}
void NativeFrameHandoff::Publish(ID3D11Texture2D& source,NativeFrameKind kind,const NativeDisplayGamma* gamma) {
  Invalidate(); // A failed publication must not expose a stale prior frame.
  Microsoft::WRL::ComPtr<ID3D11Device> owner;
  source.GetDevice(&owner);
  D3D11_TEXTURE2D_DESC desc{}; source.GetDesc(&desc);
  if(owner.Get()!=device_.Get() || desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM ||
     desc.MipLevels!=1 || desc.ArraySize!=1 || desc.SampleDesc.Count!=1)
    throw std::runtime_error("native frame publication requires same-device single-level RGBA8 surface");
  if(!snapshot_ || width_!=desc.Width || height_!=desc.Height) {
    desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags=0;
    // Shareable so another API can sample this surface directly. Costs nothing
    // when nobody opens it, and saves a full frame through system memory when
    // somebody does.
    desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    if(FAILED(device_->CreateTexture2D(&desc,nullptr,&texture)) ||
       FAILED(device_->CreateShaderResourceView(texture.Get(),nullptr,&view)))
      throw std::runtime_error("native frame snapshot allocation failed");
    if(shared_texture_) { CloseHandle(shared_texture_); shared_texture_=nullptr; }
    Microsoft::WRL::ComPtr<IDXGIResource1> shareable;
    if(SUCCEEDED(texture.As(&shareable)))
      shareable->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,
                                    nullptr,&shared_texture_);
    snapshot_=std::move(texture); view_=std::move(view);
    width_=desc.Width; height_=desc.Height;
  }
  // Created once, on first publication, because a device that never publishes
  // should not pay for a fence nobody waits on.
  if(!fence_ && shared_texture_) {
    Microsoft::WRL::ComPtr<ID3D11Device5> fencing;
    if(SUCCEEDED(device_.As(&fencing)) &&
       SUCCEEDED(fencing->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence_)))) {
      if(FAILED(fence_->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&shared_fence_))) fence_.Reset();
      if(FAILED(context_.As(&fenced_context_))) fence_.Reset();
    }
  }
  context_->CopyResource(snapshot_.Get(),&source);
  // Signalled after the copy, so a consumer that waits for this value is
  // guaranteed a finished surface rather than one still being written.
  if(fence_ && fenced_context_) fenced_context_->Signal(fence_.Get(),++fence_value_);
  if(gamma) gamma_=*gamma;
  ++sequence_; kind_=kind; valid_=true;
}
NativeFrameHandoff::SharedFrame NativeFrameHandoff::Shared() const {
  if(!valid_ || !shared_texture_ || !shared_fence_) return {};
  return {shared_texture_,shared_fence_,fence_value_,width_,height_,DXGI_FORMAT_R8G8B8A8_UNORM};
}

bool NativeFrameHandoff::Visit(const Consumer& consumer) {
  if(!valid_) return false;
  if(!consumer) throw std::runtime_error("native frame consumer is empty");
  VisitContext([&](auto& device,auto& context) { consumer(device,context,*view_.Get(),sequence_,kind_,gamma_?&*gamma_:nullptr); });
  return true;
}
void NativeFrameHandoff::VisitContext(
    const std::function<void(ID3D11Device&,ID3D11DeviceContext&)>& consumer) {
  if(!consumer) throw std::runtime_error("native context consumer is empty");
  Microsoft::WRL::ComPtr<ID3DDeviceContextState> previous;
  context_->SwapDeviceContextState(presentation_state_.Get(),&previous);
  struct Restore {
    ID3D11DeviceContext1& context;
    ID3DDeviceContextState& state;
    ~Restore() {
      // Do not retain the consumer's back buffer or snapshot in the isolated
      // state. Those references would prevent swap-chain resize/destruction.
      context.ClearState();
      context.SwapDeviceContextState(&state,nullptr);
    }
  } restore{*context_.Get(),*previous.Get()};
  consumer(*device_.Get(),*context_.Get());
}
}
