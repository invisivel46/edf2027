#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace edf::native {
// First-use event log (edf_native_first_use_log, off by default): one record
// per piece of work that a later run, or an earlier moment, could have done
// instead - a D3DCompile, a shader read back from the disk cache, a pipeline
// built or waited for on the thread that asked for it, a precompile batch. The
// frame-time counters (native_frame_times.h) say how many happened in a slow
// frame; these records say which ones, how long each took and on what thread,
// so a hitch can be named by its effect entry or pipeline key.
//
// Header-only and free of logging, so the shader and pipeline libraries can
// record without depending on the bridge. The bridge drains the queue once per
// swap and writes each record as a "Native first use:" line stamped with the
// swap it was drained in (tools/frame-time-report.py sorts them into mission
// phases). While the log is off, recording costs one relaxed load.
enum class NativeFirstUseKind : uint8_t {
  ShaderCompile,   // D3DCompile ran (a bytecode cache miss).
  ShaderDisk,      // Bytecode read back from the disk cache (preprocess + read).
  ShaderWait,      // Waited for another thread's compile of the same key.
  PipelineBuild,   // CreateGraphicsPipelineState on the thread that asked.
  PipelineWait,    // Waited for a prebuild worker's build of the same pipeline.
  Precompile,      // Boot-time shader precompile summary (native_shader_precompile.h).
  Count };
inline constexpr const char* kNativeFirstUseNames[]{
  "shader_compile","shader_disk","shader_wait","pipeline_build","pipeline_wait","precompile"};
static_assert(std::size(kNativeFirstUseNames)==size_t(NativeFirstUseKind::Count));

struct NativeFirstUseEvent {
  NativeFirstUseKind kind{};
  double ms=0;
  bool background=false;  // Recorded on a thread no frame waits for (a warmer or precompile worker).
  std::string key,detail;
};

class NativeFirstUseLog {
 public:
  static NativeFirstUseLog& Get() { static NativeFirstUseLog log; return log; }
  void SetEnabled(bool enabled) { enabled_.store(enabled,std::memory_order_relaxed); }
  bool enabled() const { return enabled_.load(std::memory_order_relaxed); }
  void Record(NativeFirstUseKind kind,double ms,std::string key,std::string detail={}) {
    if(!enabled()) return;
    std::lock_guard lock(mutex_);
    // Bounded: a run that never drains (a test, a tool) must not grow without
    // limit. Dropped records are counted and reported with the next drain.
    if(events_.size()>=kLimit) { ++dropped_; return; }
    events_.push_back({kind,ms,background_thread,std::move(key),std::move(detail)});
  }
  std::vector<NativeFirstUseEvent> Take(uint64_t* dropped=nullptr) {
    std::lock_guard lock(mutex_);
    if(dropped) { *dropped=dropped_; dropped_=0; }
    return std::exchange(events_,{});
  }
  // Set on threads whose work no frame waits for, so a record says whether it
  // could have been a hitch.
  static inline thread_local bool background_thread=false;

 private:
  static constexpr size_t kLimit=16384;
  std::atomic<bool> enabled_{false};
  std::mutex mutex_;
  std::vector<NativeFirstUseEvent> events_;
  uint64_t dropped_=0;
};

// Times a scope for a first-use record; nothing is read unless the log is on.
class NativeFirstUseTimer {
 public:
  NativeFirstUseTimer() : on_(NativeFirstUseLog::Get().enabled()) {
    if(on_) start_=std::chrono::steady_clock::now();
  }
  bool on() const { return on_; }
  double ms() const {
    return on_?std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start_).count():0;
  }
 private:
  bool on_;
  std::chrono::steady_clock::time_point start_{};
};
}  // namespace edf::native
