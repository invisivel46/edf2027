#pragma once
#include "native_backend_compositor.h"
#include "native_backend_frame.h"
#include "ui_ticker.h"
#include <Windows.h>
#include <commctrl.h>
#include <map>
#include <fstream>

namespace edf::native {
class NativeBackendHost : public std::enable_shared_from_this<NativeBackendHost> {
 public:
  using Overlay=std::function<void(NativeBackendRenderTarget&)>;
  static std::shared_ptr<NativeBackendHost> Create(HWND window,
    std::shared_ptr<NativeRenderBackend> backend,Overlay overlay,NativeUiTicker::Dispatch dispatch);
  ~NativeBackendHost();
  void Stop();
  // Called on the UI thread, once per host, after the error is logged, when the host stops
  // on an error (a lost or hung GPU, a device that could not be created): without it the
  // window stays on its last image and only the log says why.
  using FailureHandler=std::function<void(const std::string&)>;
  static void SetFailureHandler(FailureHandler handler);
 private:
  NativeBackendHost(HWND window,std::shared_ptr<NativeRenderBackend> backend,Overlay overlay);
  static LRESULT CALLBACK WindowProcedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
  void Paint();
  HWND window_;
  std::shared_ptr<NativeRenderBackend> backend_;
  Overlay overlay_;
  NativeBackendCompositor compositor_;
  std::map<uint64_t,std::unique_ptr<NativeBackendTexture>> imported_frames_;
  std::unique_ptr<NativeBackendTexture> snapshot_;
  uint32_t snapshot_width_=0,snapshot_height_=0,snapshot_format_=0;
  std::unique_ptr<NativeUiTicker> ticker_;
  std::optional<NativeDisplayGamma> gamma_;
  uint64_t sequence_=0,generation_=0,paints_=0;
  uint32_t width_=0,height_=0;
  bool painting_=false,failed_=false,captured_=false;
  std::ofstream display_trace_;
  struct Timing {
    std::chrono::steady_clock::time_point last{},report{};
    uint64_t samples=0,intervals=0,repeated=0,skipped=0,last_sequence=0;
    double interval_ms=0,interval_max=0,acquire_ms=0,acquire_max=0,present_ms=0,present_max=0;
  } timing_;

  std::chrono::steady_clock::time_point started_=std::chrono::steady_clock::now();
};
}
