#pragma once
#include "native_render_backend.h"
#include <deque>

namespace edf::native {
// Presentation credits are separate from guest resource fences. A credit can
// admit the next CPU frame while the GPU finishes an older frame, but cannot
// acknowledge any guest fence or release an upload allocation.
class NativeFrameFlight {
 public:
  explicit NativeFrameFlight(size_t limit=1):limit_(limit) {
    if(!limit || limit>3) throw std::invalid_argument("invalid frame flight limit");
  }
  void Submit(std::shared_ptr<NativeBackendCompletion> completion) {
    if(!completion) throw std::invalid_argument("null frame completion");
    if(pending_.size()>=limit_) throw std::logic_error("frame submitted without a credit");
    pending_.push_back(std::move(completion));
  }
  bool Ready() {
    while(!pending_.empty() && pending_.front()->Complete()) pending_.pop_front();
    return pending_.size()<limit_;
  }
  size_t pending() const { return pending_.size(); }
  // Live change (edf_low_latency). Call after Submit and before waiting for
  // Ready: a lower limit then simply waits for more of what is in flight, and
  // the next Submit always finds a credit.
  void SetLimit(size_t limit) {
    if(!limit || limit>3) throw std::invalid_argument("invalid frame flight limit");
    limit_=limit;
  }
  size_t limit() const { return limit_; }
 private:
  size_t limit_;
  std::deque<std::shared_ptr<NativeBackendCompletion>> pending_;
};
// edf_low_latency's frame credit. One frame in flight takes the GPU's queue out of the
// input-to-screen path, but it also makes the CPU wait for every frame's GPU tail, which
// lengthens the engine loop - and the loop's render already lags its step by one loop, so a
// longer loop costs twice. The fake-clock model in tests/input_latency_tests.cpp puts the
// line at the display: while the GPU-bound loop is still faster than the display refresh,
// two frames are faster to the screen; once the GPU cannot keep up with the display, one
// frame is. So: one frame for kHoldFrames after a swap that had to wait for the older frame
// (the GPU is the limit) at a loop interval longer than the display's refresh period (or an
// unknown one), then two again to look.
class NativeFrameCreditPolicy {
 public:
  static constexpr uint32_t kHoldFrames=120;
  size_t Limit() const { return hold_?1:2; }
  // After each swap's wait: `waited`, whether the wait at Limit() had to block;
  // `loop_ns`, the time since the previous swap; `refresh_ns`, the display's refresh
  // period (0 when unknown).
  void Observe(bool waited,int64_t loop_ns,int64_t refresh_ns) {
    if(loop_ns>0) loop_ns_=loop_ns_?(loop_ns_*7+loop_ns)/8:loop_ns;
    if(hold_) { if(!--hold_) loop_ns_=0; return; }  // look again with a fresh loop time
    const bool keeps_up=refresh_ns>0 && loop_ns_<refresh_ns;
    if(waited && !keeps_up) hold_=kHoldFrames;
  }
  bool gpu_bound() const { return hold_!=0; }
 private:
  uint32_t hold_=0;
  int64_t loop_ns_=0;
};
}
