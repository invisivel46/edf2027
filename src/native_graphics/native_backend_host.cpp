#include "native_backend_host.h"
#include "d3d11_texture.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <fstream>
#include <stdexcept>

REXCVAR_DECLARE(bool,edf_native_vsync);
REXCVAR_DECLARE(bool,edf_native_unlock_framerate);
REXCVAR_DECLARE(bool,edf_native_host_timings);
REXCVAR_DECLARE(std::string,edf_native_host_capture);
REXCVAR_DECLARE(int32_t,edf_native_host_capture_after_ms);
REXCVAR_DECLARE(bool,edf_native_host_capture_require_image);
REXCVAR_DEFINE_STRING(edf_native_display_trace,"","EDF2027",
  "Optional CSV of DXGI display feedback; empty disables display tracing.");
namespace edf::native {
std::shared_ptr<NativeBackendHost> NativeBackendHost::Create(HWND window,
    std::shared_ptr<NativeRenderBackend> backend,Overlay overlay,NativeUiTicker::Dispatch dispatch) {
  auto host=std::shared_ptr<NativeBackendHost>(new NativeBackendHost(window,std::move(backend),std::move(overlay)));
  std::weak_ptr<NativeBackendHost> weak=host;
  host->ticker_=std::make_unique<NativeUiTicker>(std::move(dispatch),[weak] {
    if(auto pinned=weak.lock()) pinned->Paint();
  });
  SetNativeBackendFrameReadyCallback([ready=host->ticker_->FrameReadyCallback()] {
    if(NativeFramerateUnlockActive()) ready();
  });
  return host;
}
NativeBackendHost::NativeBackendHost(HWND window,std::shared_ptr<NativeRenderBackend> backend,Overlay overlay)
    : window_(window),backend_(std::move(backend)),overlay_(std::move(overlay)),compositor_(*backend_) {
  if(!IsWindow(window) || GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId() || !overlay_)
    throw std::runtime_error("backend host requires an owned UI-thread window and overlay callback");
  if(REXCVAR_GET(edf_native_host_capture_after_ms)<0)
    throw std::runtime_error("native host capture delay must be nonnegative");
  const auto display_trace=REXCVAR_GET(edf_native_display_trace);
  if(!display_trace.empty()) {
    if(std::filesystem::exists(display_trace)) throw std::runtime_error("display trace path exists");
    display_trace_.open(display_trace);
    if(!display_trace_) throw std::runtime_error("cannot open display trace");
    display_trace_<<"epoch_ms,qpc,qpc_frequency,sequence,statistics_status,present_count,present_refresh_count,sync_refresh_count,sync_qpc,last_present_status,last_present_count\n";
  }
  if(!SetWindowSubclass(window,WindowProcedure,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this)))
    throw std::runtime_error("backend host window subclass failed");
}
NativeBackendHost::~NativeBackendHost() { Stop(); }
void NativeBackendHost::Stop() {
  SetNativeBackendFrameReadyCallback({});
  SetNativeBackendFrameConsumerActive(false);
  if(ticker_) ticker_->Stop();
  if(window_) {
    RemoveWindowSubclass(window_,WindowProcedure,reinterpret_cast<UINT_PTR>(this));
    window_=nullptr;
  }
}
void NativeBackendHost::Paint() {
  if(painting_ || failed_ || !window_) return;
  if(IsIconic(window_)) { SetNativeBackendFrameConsumerActive(false); timing_={}; ticker_->SetOccluded(true); return; }
  RECT rect{};
  if(!GetClientRect(window_,&rect) || rect.right<=0 || rect.bottom<=0) return;
  painting_=true;
  struct Guard { bool& value; ~Guard(){value=false;} } guard{painting_};
  try {
    using Clock=std::chrono::steady_clock;
    const bool timed=REXCVAR_GET(edf_native_host_timings);
    const auto entered=timed?Clock::now():Clock::time_point{};
    auto milliseconds=[](auto duration) { return std::chrono::duration<double,std::milli>(duration).count(); };
    NativeBackendFrameVisitTiming visit_timing;
    VisitNativeBackendFrame(sequence_,[&](const NativeBackendPublishedFrame& frame) {
      NativeBackendTextureDesc desc{};
      desc.width=frame.width; desc.height=frame.height; desc.format=frame.format;
      auto found=imported_frames_.find(frame.generation);
      if(found==imported_frames_.end()) {
        auto imported=backend_->OpenSharedTexture(frame.texture,desc);
        if(!imported) throw std::runtime_error("backend host cannot import the scene image");
        found=imported_frames_.emplace(frame.generation,std::move(imported)).first;
        while(imported_frames_.size()>3) {
          auto oldest=imported_frames_.begin();
          if(oldest==found) ++oldest;
          imported_frames_.erase(oldest);
        }
      }
      generation_=frame.generation;
      if(!snapshot_ || snapshot_width_!=frame.width || snapshot_height_!=frame.height || snapshot_format_!=frame.format) {
        snapshot_=backend_->CreateTexture(desc,{});
        snapshot_width_=frame.width; snapshot_height_=frame.height; snapshot_format_=frame.format;
      }
      auto& imported=*found->second;
      if(!backend_->WaitSharedFence(frame.fence,frame.value))
        throw std::runtime_error("backend host cannot wait for the scene image");
      backend_->BeginFrame();
      auto& recorder=backend_->Recorder();
      recorder.CopyTexture(*snapshot_,imported);
      recorder.ReleaseSharedTexture(imported);
      backend_->Submit();
      auto completion=backend_->MarkCompletion();
      sequence_=frame.sequence; gamma_=frame.gamma;
      return NativeBackendFrameCopied{nullptr,0,std::move(completion)};
    },timed?&visit_timing:nullptr);
    if(timed && visit_timing.lock_ms+visit_timing.copy_ms+visit_timing.release_ms>=5)
      REXLOG_INFO("Native host slow frame acquisition: sequence={}, lock_ms={}, copy_ms={}, release_ms={}",
        sequence_,visit_timing.lock_ms,visit_timing.copy_ms,visit_timing.release_ms);
    const double acquire_ms=timed?milliseconds(Clock::now()-entered):0;
    const auto width=uint32_t(rect.right),height=uint32_t(rect.bottom);
    if(width_!=width || height_!=height) {
      backend_->AttachWindow(window_,width,height); width_=width; height_=height;
    }
    backend_->BeginFrame();
    auto* target=backend_->BackBuffer();
    if(!target) throw std::runtime_error("backend host has no back buffer");
    if(snapshot_) compositor_.Draw(backend_->Recorder(),*snapshot_,*target,true,gamma_?&*gamma_:nullptr);
    else backend_->Recorder().ClearColor(*target,{0,0,0,1});
    try { overlay_(*target); }
    catch(...) { backend_->Submit(); throw; }
    backend_->Submit();
    if(!window_) return;
    ++paints_;
    const auto capture=REXCVAR_GET(edf_native_host_capture);
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now()-started_).count();
    if(!capture.empty() && !captured_ && paints_>=3 && elapsed>=REXCVAR_GET(edf_native_host_capture_after_ms) &&
       (snapshot_ || !REXCVAR_GET(edf_native_host_capture_require_image))) {
      if(std::filesystem::exists(capture)) throw std::runtime_error("native host capture path exists");
      const auto bmp=CaptureNativeBmp(*backend_,*target,28);
      std::ofstream output(capture,std::ios::binary);
      output.write(reinterpret_cast<const char*>(bmp.data()),std::streamsize(bmp.size())); output.close();
      if(!output) throw std::runtime_error("native host capture write failed");
      captured_=true;
      REXLOG_INFO("Native D3D12 host GPU capture: {}; elapsed_ms={}, image={}",capture,elapsed,bool(snapshot_));
    }
    const auto before_present=timed?Clock::now():Clock::time_point{};
    backend_->Present(REXCVAR_GET(edf_native_vsync));
    const double present_ms=timed?milliseconds(Clock::now()-before_present):0;
    if(display_trace_.is_open()) {
      if(const auto stats=backend_->PresentationStatistics()) {
        LARGE_INTEGER now{},frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
        const auto epoch=std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
        display_trace_<<epoch<<','<<now.QuadPart<<','<<frequency.QuadPart<<','<<sequence_<<','<<stats->result<<','
          <<stats->present_count<<','<<stats->present_refresh_count<<','<<stats->sync_refresh_count<<','
          <<stats->sync_qpc<<','<<stats->last_present_result<<','<<stats->last_present_count<<'\n';
        if(paints_%300==0) display_trace_.flush();
        if(!display_trace_) throw std::runtime_error("display trace write failed");
      } else throw std::runtime_error("display feedback unavailable on this backend");
    }
    // A fixed 60 Hz UI ticker backpressures the ordered scene queue even when
    // guest swap/heartbeat waits are removed. The render cap/VSync set cadence;
    // frame-ready notifications wake the experimental host for faster frames.
    ticker_->SetOccluded(false);
    if(timed) {
      auto& t=timing_;
      const auto now=Clock::now();
      if(t.last!=Clock::time_point{}) {
        const auto interval=milliseconds(now-t.last);
        t.interval_ms+=interval; t.interval_max=(std::max)(t.interval_max,interval); ++t.intervals;
      }
      t.last=now;
      if(t.report==Clock::time_point{}) t.report=now;
      if(sequence_ && t.last_sequence) {
        if(sequence_==t.last_sequence) ++t.repeated;
        else if(sequence_>t.last_sequence) t.skipped+=sequence_-t.last_sequence-1;
      }
      t.last_sequence=sequence_;
      ++t.samples; t.acquire_ms+=acquire_ms; t.present_ms+=present_ms;
      t.acquire_max=(std::max)(t.acquire_max,acquire_ms); t.present_max=(std::max)(t.present_max,present_ms);
      if(now-t.report>=std::chrono::seconds(5)) {
        REXLOG_INFO("Native D3D12 host pacing: samples={}, interval_avg_ms={}, interval_max_ms={}, acquire_copy_avg_ms={}, acquire_copy_max_ms={}, present_avg_ms={}, present_max_ms={}, repeated_images={}, skipped_sequences={} (CPU Present return cadence, not scanout timestamps)",
          t.samples,t.intervals?t.interval_ms/t.intervals:0,t.interval_max,t.acquire_ms/t.samples,t.acquire_max,
          t.present_ms/t.samples,t.present_max,t.repeated,t.skipped);
        const auto last_sequence=t.last_sequence;
        t={}; t.last=t.report=now; t.last_sequence=last_sequence;
      }
    } else timing_={};
    if(paints_<=3 || (snapshot_ && paints_%600==0))
      REXLOG_INFO("Native D3D12 host frame: count={}, image={}, sequence={}; composition and SDK overlays on D3D12",
        paints_,bool(snapshot_),sequence_);
    for(const auto& message:backend_->DrainValidationMessages())
      REXLOG_WARN("Native D3D12 host validation: {}",message);
  } catch(const std::exception& error) {
    failed_=true; SetNativeBackendFrameConsumerActive(false); if(ticker_) ticker_->Stop();
    REXLOG_ERROR("Native D3D12 host stopped: {}",error.what());
  }
}
LRESULT CALLBACK NativeBackendHost::WindowProcedure(HWND window,UINT message,WPARAM wparam,
    LPARAM lparam,UINT_PTR,DWORD_PTR reference) {
  auto* self=reinterpret_cast<NativeBackendHost*>(reference);
  const auto pinned=self->weak_from_this().lock();
  if(pinned && message==WM_NCDESTROY) self->Stop();
  return DefSubclassProc(window,message,wparam,lparam);
}
}
