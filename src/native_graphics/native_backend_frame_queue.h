#pragma once
#include "native_backend_frame.h"
#include "native_render_backend.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <vector>

namespace edf::native {
// One producer and one presenter. Three surfaces provide a one-frame cushion
// between their independent clocks, with no overwrite/drop while visible.
// Only the host-copy completion marker permits a consumed surface to be reused.
class NativeBackendFrameQueue {
 public:
  static constexpr size_t kSlots=3;
  using Clock=std::chrono::steady_clock;
  void SetReadyCallback(std::function<void()> callback) {
    std::lock_guard lock(mutex_); ready_callback_=std::move(callback);
  }
  size_t Reserve(Clock::time_point deadline) {
    std::unique_lock lock(mutex_);
    for(;;) {
      Check();
      for(size_t n=0;n<kSlots;++n) {
        const auto index=(next_+n)%kSlots;
        auto& slot=slots_[index];
        if(slot.state==State::Copied && Complete(slot)) slot={};
        if(slot.state==State::Free) {
          slot.state=State::Writing; next_=(index+1)%kSlots; return index;
        }
      }
      if(Clock::now()>=deadline) throw std::runtime_error("presentation frame queue timed out");
      changed_.wait_for(lock,std::chrono::milliseconds(1));
    }
  }
  void Publish(size_t index,NativeBackendPublishedFrame frame,std::shared_ptr<void> owner) {
    std::unique_lock lock(mutex_); Check();
    auto& slot=slots_.at(index);
    if(slot.state!=State::Writing || !owner || !frame.texture || !frame.fence || !frame.value ||
       !frame.sequence || frame.sequence<=last_sequence_)
      throw std::logic_error("invalid queued presentation frame");
    if(!active_) DiscardReady();
    ready_.push_back(index);
    slot.frame=std::move(frame); slot.owner=std::move(owner); slot.state=State::Ready;
    last_sequence_=slot.frame.sequence;
    changed_.notify_all();
    auto notify=active_?ready_callback_:std::function<void()>{};
    lock.unlock();
    if(notify) notify();
  }
  // Copies the oldest ready image (FIFO, primed with two), or with `newest`
  // (edf_low_latency) the newest one at once, discarding the older ready images:
  // a presenter that can show one image per refresh shows the latest instead of
  // working through a backlog that only adds refreshes of delay.
  bool Visit(uint64_t after_sequence,
      const std::function<NativeBackendFrameCopied(const NativeBackendPublishedFrame&)>& copy,
      NativeBackendFrameVisitTiming* timing=nullptr,bool newest=false) {
    const auto entered=timing?Clock::now():Clock::time_point{};
    std::unique_lock lock(mutex_); Check();
    const auto acquired=timing?Clock::now():Clock::time_point{};
    const auto ms=[](auto span){return std::chrono::duration<double,std::milli>(span).count();};
    if(timing) { *timing={}; timing->lock_ms=ms(acquired-entered); }
    if(!active_) { active_=true; primed_=false; }
    while(!ready_.empty() && slots_[ready_.front()].frame.sequence<=after_sequence) {
      Discard(slots_[ready_.front()]); ready_.pop_front(); changed_.notify_all();
    }
    if(newest) {
      while(ready_.size()>1) { Discard(slots_[ready_.front()]); ready_.pop_front(); ++skipped_; changed_.notify_all(); }
    }
    if(ready_.empty()) { primed_=false; return false; }
    if(!newest && !primed_ && ready_.size()<2) return false;
    primed_=true;
    auto& slot=slots_[ready_.front()];
    try {
      auto copied=copy(slot.frame);
      if(!copied.completion) throw std::runtime_error("frame copy has no completion marker");
      slot.completion=std::move(copied.completion); slot.state=State::Copied;
      ready_.pop_front(); changed_.notify_all();
      if(timing) timing->copy_ms=ms(Clock::now()-acquired);
      auto notify=ready_.empty()?std::function<void()>{}:ready_callback_;
      lock.unlock();
      if(notify) notify();
      return true;
    } catch(...) {
      // Copy submission may have happened. Never recycle a surface without a
      // marker proving that the potentially submitted read has finished.
      if(!lock.owns_lock()) lock.lock();
      failure_=std::current_exception(); changed_.notify_all(); throw;
    }
  }
  // Optional diagnostic windows may sample the newest image without consuming
  // the primary presenter's FIFO. Their copy markers also pin the surface.
  bool VisitMirror(uint64_t after_sequence,
      const std::function<NativeBackendFrameCopied(const NativeBackendPublishedFrame&)>& copy) {
    std::lock_guard lock(mutex_); Check();
    Slot* latest=nullptr;
    for(auto& slot:slots_) if((slot.state==State::Ready || slot.state==State::Copied) &&
        slot.frame.sequence>after_sequence && (!latest || slot.frame.sequence>latest->frame.sequence))
      latest=&slot;
    if(!latest) return false;
    try {
      latest->readers.reserve(latest->readers.size()+1);
      auto copied=copy(latest->frame);
      if(!copied.completion) throw std::runtime_error("mirror copy has no completion marker");
      latest->readers.push_back(std::move(copied.completion));
      return true;
    } catch(...) { failure_=std::current_exception(); changed_.notify_all(); throw; }
  }
  size_t ready() const { std::lock_guard lock(mutex_); return ready_.size(); }
  // Ready images the presenter dropped for a newer one (newest mode).
  uint64_t skipped() const { std::lock_guard lock(mutex_); return skipped_; }
  void SetActive(bool active) {
    std::lock_guard lock(mutex_);
    if(active_==active) return;
    active_=active; primed_=false;
    // A stopped/minimized host cannot consume. Keep background rendering
    // bounded by retaining only its next newly published frame while inactive.
    if(!active && !failure_) DiscardReady();
    changed_.notify_all();
  }
  void Fail(std::exception_ptr failure) {
    std::lock_guard lock(mutex_); failure_=std::move(failure); changed_.notify_all();
  }
 private:
  enum class State { Free,Writing,Ready,Copied };
  struct Slot {
    State state=State::Free;
    NativeBackendPublishedFrame frame;
    std::shared_ptr<void> owner;
    std::shared_ptr<NativeBackendCompletion> completion;
    std::vector<std::shared_ptr<NativeBackendCompletion>> readers;
  };
  static bool Complete(const Slot& slot) {
    return (!slot.completion || slot.completion->Complete()) &&
      std::all_of(slot.readers.begin(),slot.readers.end(),[](const auto& reader){return reader->Complete();});
  }
  static void Discard(Slot& slot) {
    if(slot.readers.empty()) slot={};
    else { slot.state=State::Copied; slot.completion.reset(); }
  }
  void Check() const { if(failure_) std::rethrow_exception(failure_); }
  void DiscardReady() {
    for(const auto index:ready_) Discard(slots_[index]);
    ready_.clear();
  }
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::array<Slot,kSlots> slots_;
  std::deque<size_t> ready_;
  size_t next_=0;
  uint64_t last_sequence_=0,skipped_=0;
  bool active_=true,primed_=false;
  std::exception_ptr failure_;
  std::function<void()> ready_callback_;
};
}
