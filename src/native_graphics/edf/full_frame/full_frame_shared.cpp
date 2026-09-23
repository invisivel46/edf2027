// The full frame's shared pass helpers (full_frame_shared.h), moved from guest_shader_bridge.cpp unchanged.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "full_frame_shared.h"
#include "../../native_frame_times.h"
#include <rex/logging.h>
#include <chrono>
#include <mutex>
#include <set>

namespace edf::native {
void NativeFullFrameNoted(const char* pass,const std::string& reason) {
  static std::mutex mutex;
  static std::set<std::string> reported;
  std::lock_guard lock(mutex);
  if(reported.size()<64 && reported.insert(std::string(pass)+": "+reason).second)
    REXLOG_INFO("Native full frame {}: {} (not a declined draw)",pass,reason);
}
void NativeFullFrameDeclined(const char* pass,const std::string& reason) {
  edf::native::FrameEventCounters().pass_declines.fetch_add(1,std::memory_order_relaxed);
  // Every decline drops the draw (or the pass) it was for.
  if(NativeCoverageCensusOn())
    edf::native::CoverageCensus().Add(edf::native::NativeCoverageStatus::Uncovered,0,std::string("pass:")+pass,"declined",1,reason);
  static std::mutex mutex;
  static std::set<std::string> reported;
  std::lock_guard lock(mutex);
  if(reported.size()<64 && reported.insert(std::string(pass)+": "+reason).second)
    REXLOG_INFO("Native full frame {} declined: {}",pass,reason);
}
}  // namespace edf::native
