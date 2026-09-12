#include "native_preview_window.h"
#include "guest_shader_bridge.h"
#include <rex/logging.h>
#include <stdexcept>

namespace edf::native {
NativePreviewWindow::NativePreviewWindow() {
  WNDCLASSW wc{};
  wc.lpfnWndProc=WindowProcedure; wc.hInstance=GetModuleHandleW(nullptr);
  wc.lpszClassName=L"EDF2027NativePreview";
  wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
  wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
    throw std::runtime_error("native preview window class registration failed");
  window_=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,
    L"EDF2027 native D3D11 preview - incomplete graphics",WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT,CW_USEDEFAULT,960,580,nullptr,nullptr,wc.hInstance,this);
  if(!window_) throw std::runtime_error("native preview window creation failed");
  if(!SetTimer(window_,1,16,nullptr)) {
    DestroyWindow(window_); window_=nullptr;
    throw std::runtime_error("native preview timer creation failed");
  }
  ShowWindow(window_,SW_SHOWNOACTIVATE);
}
NativePreviewWindow::~NativePreviewWindow() {
  if(window_) DestroyWindow(window_);
}
void NativePreviewWindow::ReleaseGraphics() {
  // The presenter destructor unbinds targets. Isolate that operation too.
  if(presenter_ || compositor_)
    VisitNativePresentationContext([&](auto&,auto&) { presenter_.reset(); compositor_.reset(); });
}
void NativePreviewWindow::Paint() {
  if(failed_ || IsIconic(window_)) return;
  RECT client{};
  if(!GetClientRect(window_,&client) || client.right<=0 || client.bottom<=0) return;
  const UINT width=UINT(client.right),height=UINT(client.bottom);
  try {
    bool occluded=false,unchanged=false;
    const bool available=VisitNativePresentationFrame([&](auto& device,auto& context,auto& view,uint64_t sequence,NativeFrameKind kind,const NativeDisplayGamma* gamma) {
      if(sequence==last_sequence_ && width==last_width_ && height==last_height_) { unchanged=true; return; }
      if(!presenter_) {
        presenter_=std::make_unique<NativeWindowPresenter>(window_,device,context);
        compositor_=std::make_unique<NativeFrameCompositor>(device);
      }
      if(!presenter_->BeginFrame(width,height)) return;
      if(!presenter_->TestVisibility()) { occluded=true; return; }
      compositor_->Draw(context,view,*presenter_->target(),true,gamma);
      if(!presenter_->Present(false)) { occluded=true; return; }
      if(last_sequence_==0 || sequence%120==0)
        REXLOG_INFO("Native host preview: presented sequence={}, kind={}, {}x{}; no Xenos pixels used",
          sequence,kind==NativeFrameKind::Movie?"movie":"partial scene",width,height);
      last_sequence_=sequence; last_width_=width; last_height_=height;
    });
    // Use visibility-only retries while occluded, without redrawing the
    // snapshot on every timer tick. Restore normal cadence when visible.
    SetTimer(window_,1,occluded?250:16,nullptr);
    if(++paint_attempts_<=3 || paint_attempts_%120==0)
      REXLOG_INFO("Native preview tick: attempt={}, available={}, unchanged={}, occluded={}, last_presented={}",
        paint_attempts_,available,unchanged,occluded,last_sequence_);
  } catch(const std::exception& error) {
    failed_=true; KillTimer(window_,1);
    REXLOG_ERROR("Native preview stopped: {}",error.what());
  }
}
LRESULT CALLBACK NativePreviewWindow::WindowProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
  auto* self=reinterpret_cast<NativePreviewWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(message==WM_NCCREATE) {
    self=static_cast<NativePreviewWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    self->window_=window;
    SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
  }
  if(self) {
    if(message==WM_TIMER && wparam==1) { self->Paint(); return 0; }
    if(message==WM_PAINT) {
      PAINTSTRUCT paint{}; BeginPaint(window,&paint); EndPaint(window,&paint);
      self->last_width_=0; self->Paint(); return 0;
    }
    if(message==WM_CLOSE) { DestroyWindow(window); return 0; }
    if(message==WM_DESTROY) {
      KillTimer(window,1);
      try { self->ReleaseGraphics(); }
      catch(const std::exception& error) { REXLOG_ERROR("Native preview cleanup: {}",error.what()); }
      return 0;
    }
    if(message==WM_NCDESTROY) {
      SetWindowLongPtrW(window,GWLP_USERDATA,0); self->window_=nullptr;
    }
  }
  return DefWindowProcW(window,message,wparam,lparam);
}
}
