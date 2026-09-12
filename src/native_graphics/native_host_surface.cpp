#include "native_host_surface.h"
#include "guest_shader_bridge.h"
#include "d3d11_texture.h"
#include <rex/logging.h>
#include <rex/cvar.h>
#include <fstream>
#include <stdexcept>
#include <algorithm>

REXCVAR_DEFINE_BOOL(edf_native_host,true,"EDF2027",
                   "Native D3D11 host (required by this port; legacy false settings are migrated at startup)");
REXCVAR_DEFINE_BOOL(edf_native_vsync,true,"EDF2027",
                   "Synchronize native D3D11 presentation to the display; does not change the 60 Hz simulation clock");
REXCVAR_DEFINE_BOOL(edf_native_host_timings,false,"EDF2027",
                   "Report host delivery intervals, image repeats and CPU acquisition/Present waits (diagnostic)");
REXCVAR_DEFINE_STRING(edf_native_host_capture,"","EDF2027",
                     "Optional one-time native host UI/frame GPU readback BMP path (development)");
REXCVAR_DEFINE_INT32(edf_native_host_capture_after_ms,0,"EDF2027",
                    "Minimum elapsed host lifetime before the one-time GPU capture (development)");
REXCVAR_DEFINE_BOOL(edf_native_host_capture_require_image,false,"EDF2027",
                   "Wait for a published game image before the one-time host GPU capture (development)");
namespace edf::native {
std::shared_ptr<NativeHostSurface> NativeHostSurface::Create(HWND window,std::function<void(UINT,UINT)> overlays,
    NativeUiTicker::Dispatch dispatch) {
  auto host=std::shared_ptr<NativeHostSurface>(new NativeHostSurface(window,std::move(overlays)));
  if(dispatch) {
    KillTimer(window,reinterpret_cast<UINT_PTR>(host.get()));
    std::weak_ptr<NativeHostSurface> weak=host;
    host->ticker_=std::make_unique<NativeUiTicker>(std::move(dispatch),[weak] {
      if(auto pinned=weak.lock()) pinned->Paint();
    });
  }
  return host;
}
NativeHostSurface::NativeHostSurface(HWND window,std::function<void(UINT,UINT)> overlays)
    : window_(window),overlays_(std::move(overlays)) {
  if(!IsWindow(window) || GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId() || !overlays_)
    throw std::runtime_error("native host surface requires owned UI-thread window and overlay callback");
  if(REXCVAR_GET(edf_native_host_capture_after_ms)<0)
    throw std::runtime_error("native host capture delay must be nonnegative");
  const auto id=reinterpret_cast<UINT_PTR>(this);
  if(!SetWindowSubclass(window,WindowProcedure,id,reinterpret_cast<DWORD_PTR>(this)))
    throw std::runtime_error("native host window subclass failed");
  if(!SetTimer(window,id,timer_interval_,nullptr)) {
    RemoveWindowSubclass(window,WindowProcedure,id);
    throw std::runtime_error("native host timer failed");
  }
}
NativeHostSurface::~NativeHostSurface() { Stop(); }
void NativeHostSurface::Stop() {
  if(ticker_) ticker_->Stop();
  if(window_) {
    const auto id=reinterpret_cast<UINT_PTR>(this);
    KillTimer(window_,id); RemoveWindowSubclass(window_,WindowProcedure,id);
    window_=nullptr;
  }
  if(!painting_) ReleaseResources();
}
void NativeHostSurface::ReleaseResources() {
  if(presenter_ || compositor_)
    VisitNativePresentationContext([&](auto&,auto&) { presenter_.reset(); compositor_.reset(); });
}
void NativeHostSurface::Paint() {
  if(painting_ || failed_ || !window_ || IsIconic(window_)) return;
  RECT rect{};
  if(!GetClientRect(window_,&rect) || rect.right<=0 || rect.bottom<=0) return;
  const UINT width=UINT(rect.right),height=UINT(rect.bottom);
  painting_=true;
  struct Guard { bool& value; ~Guard(){value=false;} } guard{painting_};
  try {
    bool presented=false;
    using Clock=std::chrono::steady_clock;
    const bool timed=REXCVAR_GET(edf_native_host_timings);
    const auto entered=timed?Clock::now():Clock::time_point{};
    auto milliseconds=[](auto duration) { return std::chrono::duration<double,std::milli>(duration).count(); };
    uint64_t sequence=0;
    bool attempted_present=false;
    double acquire_ms=0,present_ms=0;
    auto render=[&](ID3D11Device& device,ID3D11DeviceContext& context,ID3D11ShaderResourceView* frame,const NativeDisplayGamma* gamma) {
      // Includes bridge lock acquisition and context isolation, not pure lock time.
      if(timed) acquire_ms=milliseconds(Clock::now()-entered);
      if(!presenter_) {
        presenter_=std::make_unique<NativeWindowPresenter>(window_,device,context);
        compositor_=std::make_unique<NativeFrameCompositor>(device);
      }
      if(!presenter_->BeginFrame(width,height)) return;
      if(frame) compositor_->Draw(context,*frame,*presenter_->target(),true,gamma);
      else {
        context.ClearState();
        const float black[]{0,0,0,1}; context.ClearRenderTargetView(presenter_->target(),black);
        auto* target=presenter_->target(); context.OMSetRenderTargets(1,&target,nullptr);
      }
      overlays_(width,height);
      // UI callbacks can close the window or drop the app's owning reference.
      // The window procedure pins our lifetime; Stop has detached the timer.
      if(!window_) return;
      ++paints_;
      const auto capture=REXCVAR_GET(edf_native_host_capture);
      const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-started_).count();
      if(!capture.empty() && !captured_ && paints_>=3 &&
         elapsed>=REXCVAR_GET(edf_native_host_capture_after_ms) &&
         (frame || !REXCVAR_GET(edf_native_host_capture_require_image))) {
        if(std::filesystem::exists(capture)) throw std::runtime_error("native host capture path exists");
        Microsoft::WRL::ComPtr<ID3D11Resource> resource; presenter_->target()->GetResource(&resource);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if(FAILED(resource.As(&texture))) throw std::runtime_error("native host capture target is not a texture");
        const auto bmp=CaptureNativeHdrBmp(context,*texture.Get());
        std::ofstream output(capture,std::ios::binary);
        output.write(reinterpret_cast<const char*>(bmp.data()),std::streamsize(bmp.size())); output.close();
        if(!output) throw std::runtime_error("native host capture write failed");
        captured_=true;
        REXLOG_INFO("Native host GPU capture: {}; elapsed_ms={}, image={}, Xenos_loaded={}",
          capture,elapsed,frame!=nullptr,GetModuleHandleW(L"rexgpu-xenos.dll")!=nullptr);
      }
      const auto before_present=timed?Clock::now():Clock::time_point{};
      attempted_present=true;
      presented=presenter_->Present(REXCVAR_GET(edf_native_vsync));
      if(timed) present_ms=milliseconds(Clock::now()-before_present);
      if(paints_<=3 || (frame && !logged_game_frame_))
        REXLOG_INFO("Native host frame: count={}, image={}, presented={}, Xenos_loaded={}",
          paints_,frame!=nullptr,presented,GetModuleHandleW(L"rexgpu-xenos.dll")!=nullptr);
      if(frame) logged_game_frame_=true;
    };
    if(!VisitNativePresentationFrame([&](auto& d,auto& c,auto& image,auto serial,auto,auto* gamma) {sequence=serial; render(d,c,&image,gamma);}))
      VisitNativePresentationContext([&](auto& d,auto& c) {render(d,c,nullptr,nullptr);});
    if(timed && presented) {
      auto& t=timing_;
      const auto now=Clock::now();
      if(t.last!=Clock::time_point{}) {
        const auto interval=milliseconds(now-t.last);
        t.interval_ms+=interval; t.interval_max=(std::max)(t.interval_max,interval); ++t.intervals;
      }
      t.last=now;
      if(t.report==Clock::time_point{}) t.report=now;
      if(sequence && t.last_sequence) {
        if(sequence==t.last_sequence) ++t.repeated;
        else if(sequence>t.last_sequence) t.skipped+=sequence-t.last_sequence-1;
      }
      t.last_sequence=sequence;
      ++t.samples; t.acquire_ms+=acquire_ms; t.present_ms+=present_ms;
      t.acquire_max=(std::max)(t.acquire_max,acquire_ms); t.present_max=(std::max)(t.present_max,present_ms);
      if(now-t.report>=std::chrono::seconds(5)) {
        REXLOG_INFO("Native host pacing: samples={}, interval_avg_ms={}, interval_max_ms={}, acquire_isolate_avg_ms={}, acquire_isolate_max_ms={}, present_avg_ms={}, present_max_ms={}, repeated_images={}, skipped_sequences={} (CPU Present return cadence, not scanout timestamps)",
          t.samples,t.intervals?t.interval_ms/t.intervals:0,t.interval_max,t.acquire_ms/t.samples,t.acquire_max,
          t.present_ms/t.samples,t.present_max,t.repeated,t.skipped);
        const auto last_sequence=t.last_sequence;
        t={}; t.last=t.report=now; t.last_sequence=last_sequence;
      }
    } else {
      timing_={}; // Exclude occlusion and disabled periods from cadence.
      if(timed && attempted_present && !presented) {
        if(++timing_occluded_<=3 || !(timing_occluded_&(timing_occluded_-1)))
          REXLOG_INFO("Native host pacing unavailable: DXGI occluded, count={}, acquire_isolate_ms={}, present_ms={} (game FPS is not visible delivery)",
            timing_occluded_,acquire_ms,present_ms);
      }
      if(!timed) timing_occluded_=0;
    }
    if(!window_) { ReleaseResources(); return; }
    // Keep UI logic moving even while occluded (including detached setup),
    // with a low-frequency update instead of a full-rate invisible draw loop.
    // Replacing a Win32 timer resets its deadline. Do not add a fresh 16 ms
    // delay after each Present (which may already have waited for VSync).
    // Rearm only when entering or leaving the low-frequency occluded mode.
    const UINT interval=presented?16:250;
    if(ticker_) ticker_->SetOccluded(!presented);
    if(interval!=timer_interval_) {
      if(!ticker_ && !SetTimer(window_,reinterpret_cast<UINT_PTR>(this),interval,nullptr))
        throw std::runtime_error("native host timer update failed");
      timer_interval_=interval;
    }
  } catch(const std::exception& error) {
    failed_=true;
    if(ticker_) ticker_->Stop();
    if(window_) KillTimer(window_,reinterpret_cast<UINT_PTR>(this));
    else ReleaseResources();
    REXLOG_ERROR("Native host surface stopped: {}",error.what());
  }
}
LRESULT CALLBACK NativeHostSurface::WindowProcedure(HWND window,UINT message,WPARAM wparam,
    LPARAM lparam,UINT_PTR id,DWORD_PTR reference) {
  auto* self=reinterpret_cast<NativeHostSurface*>(reference);
  // Reentrant close/reset must not delete self, its paint guard or callback
  // until this dispatch has unwound. No ownership cycle with the HWND.
  const auto keep_alive=self->weak_from_this().lock();
  if(!keep_alive) return DefSubclassProc(window,message,wparam,lparam);
  if(message==WM_TIMER && wparam==id) {self->Paint(); return 0;}
  if(message==WM_NCDESTROY) self->Stop();
  return DefSubclassProc(window,message,wparam,lparam);
}
}
