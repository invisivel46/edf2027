#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>

namespace edf::native {
// One wait on a swap chain's frame-latency waitable object per Present, taken either just
// in time by another thread (edf_low_latency: the presenter's ticker thread, so the UI thread
// that delivers input never sleeps on the display) or, as before, by Present itself.
//
// The waitable object is a semaphore: every completed flip releases one count and every
// successful wait takes one. A wait taken ahead of Present is held as a credit and spent by
// the next Present instead of waiting again, so each Present is still matched by exactly one
// wait. The handle belongs to the swap chain and is replaced when it is rebuilt (resize);
// Acquire waits on its own duplicate so the owner can close the original meanwhile, and a
// credit taken from a chain that has since been replaced is not kept.
//
// Ops are the platform calls (DuplicateHandle, WaitForSingleObject, CloseHandle); the unit
// tests supply a fake semaphore.
class NativePresentSlot {
 public:
  struct Ops {
    std::function<void*(void* handle)> duplicate;             // null on failure
    std::function<bool(void* handle,uint32_t timeout_ms)> wait;  // true when signalled
    std::function<void(void* handle)> close;
  };
  explicit NativePresentSlot(Ops ops):ops_(std::move(ops)) {}
  // The owner's new handle (null: no swap chain). Any credit belonged to the old one.
  void Attach(void* handle) {
    std::lock_guard lock(mutex_);
    handle_=handle; credit_=false; ++generation_;
  }
  // Any thread. True when a credit is now held: already held, or waited for now.
  bool Acquire(uint32_t timeout_ms) {
    void* duplicate=nullptr;
    uint64_t generation=0;
    {
      std::lock_guard lock(mutex_);
      if(credit_) return true;
      if(!handle_) return false;
      duplicate=ops_.duplicate(handle_);
      generation=generation_;
    }
    if(!duplicate) return false;
    const bool signalled=ops_.wait(duplicate,timeout_ms);
    ops_.close(duplicate);
    std::lock_guard lock(mutex_);
    if(!signalled || generation!=generation_) return false;
    credit_=true;
    return true;
  }
  // The presenting thread, right before Present: true when a held credit was spent (skip
  // the wait); false when Present has to wait as before.
  bool Spend() {
    std::lock_guard lock(mutex_);
    const bool held=credit_;
    credit_=false;
    return held;
  }
  bool held() const { std::lock_guard lock(mutex_); return credit_; }
 private:
  Ops ops_;
  mutable std::mutex mutex_;
  void* handle_=nullptr;
  uint64_t generation_=0;
  bool credit_=false;
};
}
