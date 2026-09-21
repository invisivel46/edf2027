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
 private:
  size_t limit_;
  std::deque<std::shared_ptr<NativeBackendCompletion>> pending_;
};
}
