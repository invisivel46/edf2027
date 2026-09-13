#pragma once
#include "d3d11_presenter.h"
#include "d3d11_frame_compositor.h"
#include "d3d11_shared_surface.h"
#include "native_backend_present.h"
#include <commctrl.h>
#include <functional>
#include <memory>
#include <chrono>
#include "ui_ticker.h"

namespace edf::native {
// How this window gets a backend to present with.
//
// A factory, not a lookup, and the distinction matters. The presenting backend
// must own its device: presentation happens on the window thread, outside the
// renderer's context lock, and a backend sharing the renderer's immediate
// context would then be driving it from two threads at once. Borrowing the
// bridge's backend would do exactly that whenever the selected one is the
// adopted D3D11 backend.
//
// Injected rather than looked up because the host surface is also built
// standalone by its lifetime test, which has no bridge. An unset factory is
// the normal state for that test, not an error.
using NativeHostBackendFactory=std::function<std::unique_ptr<NativeRenderBackend>()>;
void SetNativeHostBackendFactory(NativeHostBackendFactory factory);

// App-owned rendering on the SDK HWND when no GPU plugin owns its presenter.
// Construct/destroy on that window's thread. Removing the subclass before
// destruction makes already-queued timer messages harmless.
class NativeHostSurface : public std::enable_shared_from_this<NativeHostSurface> {
 public:
  static std::shared_ptr<NativeHostSurface> Create(HWND window,std::function<void(UINT,UINT)> overlays,
      NativeUiTicker::Dispatch dispatch={});
  ~NativeHostSurface();
  void Stop(); // Detach immediately; defer GPU cleanup while a callback is active.
  NativeHostSurface(const NativeHostSurface&)=delete;
  NativeHostSurface& operator=(const NativeHostSurface&)=delete;
 private:
  NativeHostSurface(HWND window,std::function<void(UINT,UINT)> overlays);
  static LRESULT CALLBACK WindowProcedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
  void Paint();
  void ReleaseResources();
  HWND window_;
  std::function<void(UINT,UINT)> overlays_;
  std::unique_ptr<NativeWindowPresenter> presenter_;
  // The backend path: the frame is composited into a shared surface here and
  // presented to the window by the backend. Null, or refusing, means the
  // D3D11 presenter above stays in charge - a window that shows nothing would
  // be a far worse outcome than one presented by the API it always used.
  // Owned, and on its own device; see NativeHostBackendFactory.
  std::unique_ptr<NativeRenderBackend> present_backend_;
  std::unique_ptr<NativeBackendWindowPresenter> backend_presenter_;
  NativeSharedSurface shared_;
  bool backend_present_failed_=false;
  bool logged_backend_present_=false;
  std::unique_ptr<NativeFrameCompositor> compositor_;
  std::unique_ptr<NativeUiTicker> ticker_;
  uint64_t paints_=0;
  UINT timer_interval_=16;
  std::chrono::steady_clock::time_point started_=std::chrono::steady_clock::now();
  bool logged_game_frame_=false;
  bool painting_=false,failed_=false,captured_=false;
  struct Timing {
    std::chrono::steady_clock::time_point last{},report{};
    uint64_t samples=0,intervals=0,repeated=0,skipped=0,last_sequence=0;
    double interval_ms=0,interval_max=0,acquire_ms=0,acquire_max=0,present_ms=0,present_max=0;
  } timing_;
  uint64_t timing_occluded_=0;
};
}
