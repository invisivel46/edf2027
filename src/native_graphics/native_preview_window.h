#pragma once
#include "d3d11_presenter.h"
#include "d3d11_frame_compositor.h"
#include <memory>

namespace edf::native {
// Development surface while Xenos owns the SDK window. All messages, timers
// and destruction run on the app UI thread; no queued raw callbacks.
class NativePreviewWindow {
 public:
  NativePreviewWindow();
  ~NativePreviewWindow();
  NativePreviewWindow(const NativePreviewWindow&)=delete;
  NativePreviewWindow& operator=(const NativePreviewWindow&)=delete;
 private:
  static LRESULT CALLBACK WindowProcedure(HWND,UINT,WPARAM,LPARAM);
  void Paint();
  void ReleaseGraphics();
  HWND window_=nullptr;
  std::unique_ptr<NativeWindowPresenter> presenter_;
  std::unique_ptr<NativeFrameCompositor> compositor_;
  uint64_t last_sequence_=0;
  uint64_t paint_attempts_=0;
  UINT last_width_=0,last_height_=0;
  bool failed_=false;
};
}
