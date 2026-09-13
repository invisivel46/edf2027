#include "native_graphics/native_host_surface.h"
#include "native_graphics/guest_shader_bridge.h"
#include <iostream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace {
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
int context_depth=0;
void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
HWND Window() {
  auto window=CreateWindowExW(0,L"STATIC",L"native lifetime test",WS_OVERLAPPEDWINDOW,
    0,0,320,240,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  Require(window!=nullptr,"test window creation"); return window;
}
void Paint(HWND window,const edf::native::NativeHostSurface* host) {
  SendMessageW(window,WM_TIMER,reinterpret_cast<UINT_PTR>(host),0);
}
}
// Exercise the real host surface without the game's global bridge. Reject
// recursive context access: closing during overlays must not reacquire it.
namespace edf::native {
bool VisitNativePresentationFrame(const NativeFrameHandoff::Consumer&) {return false;}
// No bridge here, so no scene and no scene-shared frame. The host surface
// must still work: this is the standalone-lifetime case the surface exists
// to be testable in.
bool VisitNativeSceneSharedFrame(NativeFrameHandoff::SharedFrame&,uint64_t&) {return false;}
bool VisitNativePresentationContext(const std::function<void(ID3D11Device&,ID3D11DeviceContext&)>& visitor) {
  Require(context_depth==0,"reentrant presentation context access");
  ++context_depth;
  struct Guard {~Guard(){--context_depth;}} guard;
  visitor(*device.Get(),*context.Get()); return true;
}
}
int main() {
  try {
    Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
      D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP creation");
    using edf::native::NativeHostSurface;
    HWND window=Window();
    std::shared_ptr<NativeHostSurface> owner;
    std::weak_ptr<NativeHostSurface> weak;
    int callbacks=0;
    owner=NativeHostSurface::Create(window,[&](UINT,UINT) {
      ++callbacks;
      Paint(window,owner.get()); // Nested timer must not reenter painting.
      owner->Stop(); owner.reset();
      Require(!weak.expired(),"host destroyed inside active overlay");
    });
    weak=owner;
    const auto id=reinterpret_cast<UINT_PTR>(owner.get());
    Paint(window,owner.get());
    Require(callbacks==1 && weak.expired(),"host lifetime did not end after callback unwind");
    SendMessageW(window,WM_TIMER,id,0);
    Require(callbacks==1,"detached timer called stale host");
    DestroyWindow(window);

    window=Window(); callbacks=0;
    owner=NativeHostSurface::Create(window,[&](UINT,UINT) {
      ++callbacks; DestroyWindow(window);
    });
    Paint(window,owner.get());
    Require(callbacks==1 && !IsWindow(window),"destroy window inside paint");
    owner->Stop(); owner->Stop(); owner.reset();
    Require(context_depth==0,"context scope leaked after destruction");

    window=Window(); callbacks=0;
    owner=NativeHostSurface::Create(window,[&](UINT,UINT) {
      ++callbacks; owner->Stop(); throw std::runtime_error("expected overlay failure");
    });
    Paint(window,owner.get());
    Paint(window,owner.get());
    Require(callbacks==1 && context_depth==0,"exception cleanup or detached timer failed");
    owner.reset(); DestroyWindow(window);
    // SDK-style deferred dispatch: queued work must be bounded and must not
    // retain the host after Stop, including Stop inside an active overlay.
    for(bool close_in_paint:{false,true}) {
      window=Window(); callbacks=0;
      std::mutex queue_mutex; std::condition_variable queued;
      std::vector<std::function<void()>> pending;
      owner=NativeHostSurface::Create(window,[&](UINT,UINT) {
        ++callbacks; owner->Stop(); owner.reset();
        Require(!weak.expired(),"deferred host destroyed during paint");
      },[&](std::function<void()> callback) {
        std::lock_guard lock(queue_mutex); pending.push_back(std::move(callback)); queued.notify_all(); return true;
      });
      weak=owner;
      std::function<void()> callback;
      {
        std::unique_lock lock(queue_mutex);
        Require(queued.wait_for(lock,std::chrono::seconds(2),[&]{return !pending.empty();}),"ticker failed to dispatch");
        Require(!queued.wait_for(lock,std::chrono::milliseconds(40),[&]{return pending.size()>1;}),"ticker queued duplicate paints");
        callback=std::move(pending.front()); pending.clear();
      }
      if(!close_in_paint) {owner->Stop(); owner.reset();}
      callback();
      Require(callbacks==(close_in_paint?1:0) && weak.expired(),"deferred stop/lifetime failed");
      DestroyWindow(window);
    }
    Require(GetModuleHandleW(L"rexgpu-xenos.dll")==nullptr,"lifetime test loaded Xenos");
    {
      std::atomic<unsigned> attempts=0;
      edf::native::NativeUiTicker rejected([&](auto) {++attempts; return false;},[]{});
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
      while(!attempts.load() && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
      rejected.Stop();
      Require(attempts==1,"ticker retries after UI loop rejects dispatch");
    }
    std::cout<<"native host lifetime tests passed\n";
    return 0;
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n'; return 1;}
}
