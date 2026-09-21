#include "native_graphics/native_backend_host.h"
#include "native_graphics/d3d12_backend.h"
#include <rex/cvar.h>
#include <iostream>
#include <stdexcept>
REXCVAR_DEFINE_BOOL(edf_native_host_timings,true,"Test","Host timing test");
REXCVAR_DEFINE_BOOL(edf_native_vsync,false,"Test","test");
REXCVAR_DEFINE_BOOL(edf_native_unlock_framerate,false,"Test","test");
REXCVAR_DEFINE_STRING(edf_native_host_capture,"","Test","test");
REXCVAR_DEFINE_INT32(edf_native_host_capture_after_ms,0,"Test","test");
REXCVAR_DEFINE_BOOL(edf_native_host_capture_require_image,false,"Test","test");
namespace edf::native {
std::function<void()> frame_ready;
void SetNativeBackendFrameReadyCallback(std::function<void()> callback) { frame_ready=std::move(callback); }
bool NativeFramerateUnlockActive() { return REXCVAR_GET(edf_native_unlock_framerate); }
void SetNativeBackendFrameConsumerActive(bool) {}
bool VisitNativeBackendFrame(uint64_t,const std::function<NativeBackendFrameCopied(const NativeBackendPublishedFrame&)>&,NativeBackendFrameVisitTiming*) {return false;}
}
using namespace edf::native;
void Require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
struct Dispatch {
  std::mutex mutex; std::condition_variable ready; std::vector<std::function<void()>> pending;
  bool Enqueue(std::function<void()> callback) {
    std::lock_guard lock(mutex); pending.push_back(std::move(callback)); ready.notify_all(); return true;
  }
  void Pump() {
    std::function<void()> callback;
    {
      std::unique_lock lock(mutex);
      Require(ready.wait_for(lock,std::chrono::seconds(2),[&]{return !pending.empty();}),"host did not dispatch paint");
      Require(pending.size()==1,"host queued duplicate paints");
      callback=std::move(pending.front()); pending.clear();
    }
    callback();
  }
};
int main() {
  try {
    for(unsigned mode=0;mode<3;++mode) {
      NativeD3D12Options options{}; options.prefer_warp=true; options.debug_layer=true;
      auto backend=std::shared_ptr<NativeRenderBackend>(CreateNativeD3D12Backend(options));
      const auto window=CreateWindowExW(0,L"STATIC",L"D3D12 host test",WS_OVERLAPPEDWINDOW,
        0,0,320,240,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
      Require(window!=nullptr,"host test window creation failed");
      Dispatch dispatch; unsigned callbacks=0; uint32_t first_width=0;
      std::shared_ptr<NativeBackendHost> owner; std::weak_ptr<NativeBackendHost> weak;
      owner=NativeBackendHost::Create(window,backend,[&](NativeBackendRenderTarget& target) {
        ++callbacks;
        if(callbacks==1) first_width=target.width();
        if(mode==0 && callbacks==2) Require(target.width()>first_width,"host resize retained old back buffer");
        if(mode==1) {
          owner->Stop(); owner.reset(); Require(!weak.expired(),"host destroyed during overlay callback");
        }
        if(mode==2) {DestroyWindow(window); throw std::runtime_error("expected overlay failure");}
      },[&](auto callback){return dispatch.Enqueue(std::move(callback));});
      weak=owner; dispatch.Pump();
      if(mode==0) {
        Require(callbacks==1,"host initial paint failed");
        Require(bool(frame_ready),"locked host did not register live-toggle notification");
        rex::cvar::SetFlagByName("edf_native_unlock_framerate","true");
        frame_ready();
        Require(backend->PresentationStatistics().has_value(),"attached D3D12 host has no display feedback interface");
        rex::cvar::SetFlagByName("edf_native_vsync","true");
        Require(SetWindowPos(window,nullptr,0,0,640,480,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"host resize failed");
        dispatch.Pump(); Require(callbacks==2,"host resized paint failed");
        rex::cvar::SetFlagByName("edf_native_vsync","false");
        rex::cvar::SetFlagByName("edf_native_unlock_framerate","false");
        frame_ready();
        dispatch.Pump(); Require(callbacks==3,"host VSync-off restoration failed");
        auto retained_ready=frame_ready;
        owner->Stop(); owner.reset();
        Require(!frame_ready,"stopped host retained its producer callback");
        rex::cvar::SetFlagByName("edf_native_unlock_framerate","true");
        retained_ready(); // A producer may have copied the callback before Stop.
        rex::cvar::SetFlagByName("edf_native_unlock_framerate","false");
      } else if(mode==1) Require(callbacks==1 && weak.expired(),"host callback ownership leaked");
      else {Require(!IsWindow(window) && callbacks==1,"host failed reentrant window destruction");owner.reset();}
      if(IsWindow(window)) DestroyWindow(window);
      Require(backend->DrainValidationMessages().empty(),"host lifetime GPU validation failed");
    }
    std::cout<<"D3D12 host resize and lifetime tests passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
