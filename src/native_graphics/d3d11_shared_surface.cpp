#include "d3d11_shared_surface.h"
#include <dxgi1_2.h>

namespace edf::native {
void NativeSharedSurface::Release() {
  if(shared_texture_) { CloseHandle(shared_texture_); shared_texture_=nullptr; }
  if(shared_fence_) { CloseHandle(shared_fence_); shared_fence_=nullptr; }
  target_.Reset();
  texture_.Reset();
  fence_.Reset();
  width_=height_=0;
}

bool NativeSharedSurface::Resize(ID3D11Device& device, uint32_t width, uint32_t height) {
  if(!width || !height) return false;
  if(texture_ && width_==width && height_==height) return valid();
  // The old handles are closed before the new ones exist, so a failed resize
  // cannot leave the previous surface advertised with a texture that is gone.
  Release();

  D3D11_TEXTURE2D_DESC desc{};
  desc.Width=width;
  desc.Height=height;
  desc.MipLevels=desc.ArraySize=1;
  desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc={1,0};
  desc.Usage=D3D11_USAGE_DEFAULT;
  desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
  desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED;
  if(FAILED(device.CreateTexture2D(&desc,nullptr,&texture_)) ||
     FAILED(device.CreateRenderTargetView(texture_.Get(),nullptr,&target_))) { Release(); return false; }

  Microsoft::WRL::ComPtr<IDXGIResource1> shareable;
  if(FAILED(texture_.As(&shareable)) ||
     FAILED(shareable->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,
                                          nullptr,&shared_texture_))) { Release(); return false; }

  Microsoft::WRL::ComPtr<ID3D11Device5> fencing;
  if(FAILED(device.QueryInterface(IID_PPV_ARGS(&fencing))) ||
     FAILED(fencing->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence_))) ||
     FAILED(fence_->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&shared_fence_))) { Release(); return false; }

  width_=width;
  height_=height;
  return true;
}

void NativeSharedSurface::Signal(ID3D11DeviceContext& context) {
  if(!fence_) return;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext4> fenced;
  if(FAILED(context.QueryInterface(IID_PPV_ARGS(&fenced)))) return;
  fenced->Signal(fence_.Get(),++value_);
}
}  // namespace edf::native
