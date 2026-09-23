#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace edf::native {
// Loading-screen trace (edf_native_load_trace, off by default). Every category
// is an inclusive CPU wall time plus a call count (and bytes where there are
// bytes), split into time spent on the engine thread - the thread that runs
// the mission script, LoadMap and the model constructors, so the loading
// screen's critical path - and on any other thread. Categories nest (a model
// construction contains its file-backed copy and its publication), so they
// locate time; they are not an exclusive partition. The engine thread's own
// CPU time is reported beside them by the caller: what is left once the
// hooked categories are subtracted is guest CPU (parsing, decompression,
// object construction) or guest waits (guest.wait).
enum class LoadTraceKind : uint8_t {
  FileRead,          // SDK NtReadFile, synchronous (bytes requested)
  FileReadNotify,    // our write notification around each read
  TextureSnapshot,   // copying the DDS source before the guest loader runs
  TextureGuest,      // guest D3DX loader (untile/upload into guest memory)
  TextureLockWait,   // waiting for the bridge mutex to register the texture
  TextureCreate,     // DDS decode + D3D12 resource creation (bytes = source)
  ShaderRegistration,
  ModelConstruct,    // model VB/IB constructor: guest alloc + copy + publish
  ModelPublish,      // native publication: write drain, snapshot, index buffer
  ModelRetire,       // model VB/IB cleanup (native retirement + guest free)
  PoolRetire,        // native part of pool block / allocation release hooks
  ResourceDestroy,   // native part of final resource destruction (82134220)
  PreloadGeometry,
  PreloadMaterial,
  GuestWait,         // guest kernel waits (events, semaphores, sleep, file)
  BridgeMutexWait,   // contended bridge mutex acquisitions
  PresenterFrame,    // loading-screen presenter scene setup (8219C7A8)
  EngineTransition,  // the engine thread's frame transition hook (821A4DE8)
  MapLoad,           // LoadMap (820CBD28), inclusive
  Count };
inline constexpr const char* kLoadTraceNames[]{
  "file.read","file.read_notify","texture.snapshot","texture.guest","texture.lock_wait","texture.create",
  "shader.registration","model.construct","model.publish","model.retire","pool.retire","resource.destroy",
  "preload.geometry","preload.material","guest.wait","bridge.mutex_wait","presenter.frame",
  "engine.transition","map.load"};
static_assert(std::size(kLoadTraceNames)==size_t(LoadTraceKind::Count));

struct LoadTraceSample {
  static constexpr size_t kCount=size_t(LoadTraceKind::Count);
  std::array<uint64_t,kCount> calls{},engine_nanos{},other_nanos{},bytes{};
};

class NativeLoadTrace {
 public:
  static NativeLoadTrace& Get() { static NativeLoadTrace trace; return trace; }
  void Add(LoadTraceKind kind,uint64_t nanos,uint64_t bytes,bool engine) {
    const auto index=size_t(kind);
    calls_[index].fetch_add(1,std::memory_order_relaxed);
    (engine?engine_nanos_:other_nanos_)[index].fetch_add(nanos,std::memory_order_relaxed);
    if(bytes) bytes_[index].fetch_add(bytes,std::memory_order_relaxed);
  }
  LoadTraceSample Sample() const {
    LoadTraceSample sample;
    for(size_t index=0;index<LoadTraceSample::kCount;++index) {
      sample.calls[index]=calls_[index].load(std::memory_order_relaxed);
      sample.engine_nanos[index]=engine_nanos_[index].load(std::memory_order_relaxed);
      sample.other_nanos[index]=other_nanos_[index].load(std::memory_order_relaxed);
      sample.bytes[index]=bytes_[index].load(std::memory_order_relaxed);
    }
    return sample;
  }
  // "name=calls/engine_ms+other_ms[/MB]" for every category that moved.
  static std::string Format(const LoadTraceSample& now,const LoadTraceSample& before) {
    std::string text;
    char item[160];
    for(size_t index=0;index<LoadTraceSample::kCount;++index) {
      const auto calls=now.calls[index]-before.calls[index];
      if(!calls) continue;
      const double engine=double(now.engine_nanos[index]-before.engine_nanos[index])/1e6;
      const double other=double(now.other_nanos[index]-before.other_nanos[index])/1e6;
      const auto bytes=now.bytes[index]-before.bytes[index];
      if(bytes) std::snprintf(item,sizeof(item),"%s=%llu/%.1f+%.1fms/%.2fMB ",kLoadTraceNames[index],
                              (unsigned long long)calls,engine,other,double(bytes)/(1024.0*1024.0));
      else std::snprintf(item,sizeof(item),"%s=%llu/%.1f+%.1fms ",kLoadTraceNames[index],
                         (unsigned long long)calls,engine,other);
      text+=item;
    }
    if(!text.empty()) text.pop_back();
    return text;
  }
 private:
  std::array<std::atomic<uint64_t>,LoadTraceSample::kCount> calls_{},engine_nanos_{},other_nanos_{},bytes_{};
};

// Set on the engine thread by the frame transition hook (the first time the
// trace sees it), so every scope can attribute its time without a lookup.
inline thread_local bool native_load_trace_engine_thread=false;

// One timed region; nothing is read or written unless `enabled`.
class LoadTraceScope {
 public:
  using Clock=std::chrono::steady_clock;
  LoadTraceScope(bool enabled,LoadTraceKind kind,uint64_t bytes=0):kind_(kind),bytes_(bytes),enabled_(enabled) {
    if(enabled_) start_=Clock::now();
  }
  ~LoadTraceScope() { Finish(); }
  LoadTraceScope(const LoadTraceScope&)=delete;
  LoadTraceScope& operator=(const LoadTraceScope&)=delete;
  void SetBytes(uint64_t bytes) { bytes_=bytes; }
  void Finish() {
    if(!enabled_) return;
    enabled_=false;
    const auto nanos=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start_).count());
    NativeLoadTrace::Get().Add(kind_,nanos,bytes_,native_load_trace_engine_thread);
  }
 private:
  LoadTraceKind kind_;
  uint64_t bytes_;
  bool enabled_;
  Clock::time_point start_{};
};
}
