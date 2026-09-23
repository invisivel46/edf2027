#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace edf::native {
// Dispatcher must enqueue without waiting for UI execution. At most one callback
// may be queued/executing. Stop never waits for the UI to drain that callback.
class NativeUiTicker {
 public:
  using Dispatch=std::function<bool(std::function<void()>)>;
  NativeUiTicker(Dispatch dispatch,std::function<void()> paint)
      : state_(std::make_shared<State>()),thread_([state=state_,dispatch=std::move(dispatch),paint=std::move(paint)](std::stop_token stop) {
        auto deadline=std::chrono::steady_clock::now();
        std::unique_lock lock(state->mutex);
        while(!stop.stop_requested()) {
          if(!state->pending.exchange(true)) {
            state->requested=false;
            // Just-in-time presentation (edf_low_latency): the display wait happens here,
            // on this thread, so the paint starts right after the flip and the UI thread,
            // which also delivers input, never sleeps on the display.
            const auto before=state->before_dispatch;
            lock.unlock();
            if(before) before();
            if(!dispatch([state,paint] {
              struct Done { std::shared_ptr<State> state; ~Done(){
                std::lock_guard guard(state->mutex);
                state->pending=false; state->wake.notify_all();
              } } done{state};
              if(state->active.load()) paint();
            })) {
              // SDK rejects deferred calls after its loop exits. Do not keep
              // waking an already stopped UI loop until the host is destroyed.
              state->pending=false; state->active=false; return;
            }
            lock.lock();
          }
          const auto now=std::chrono::steady_clock::now();
          deadline=now+std::chrono::nanoseconds(state->period.load());
          state->wake.wait_until(lock,stop,deadline,[&]{
            return state->requested && !state->occluded && !state->pending.load();
          });
        }
      }) {}
  ~NativeUiTicker() { Stop(); }
  void Stop() { state_->active=false; thread_.request_stop(); if(thread_.joinable()) thread_.join(); }
  void SetOccluded(bool value) {
    std::lock_guard lock(state_->mutex);
    state_->occluded=value;
    state_->period=value?250000000:16666667;
  }
  // Runs on the ticker thread before each paint is dispatched (never while the lock is held).
  // It may block for about a display refresh; Stop waits for it to return.
  void SetBeforeDispatch(std::function<void()> before) {
    std::lock_guard lock(state_->mutex);
    state_->before_dispatch=std::move(before);
  }
  // Safe to retain after Stop/destruction; requests coalesce while painting.
  std::function<void()> FrameReadyCallback() const {
    return [weak=std::weak_ptr<State>(state_)] {
      if(auto state=weak.lock()) {
        std::lock_guard lock(state->mutex);
        if(!state->active.load()) return;
        state->requested=true; state->wake.notify_all();
      }
    };
  }
 private:
  struct State {
    std::atomic<bool> active{true},pending{false};
    std::atomic<int64_t> period{16666667};
    std::mutex mutex;
    std::condition_variable_any wake;
    bool requested=false,occluded=false;
    std::function<void()> before_dispatch;
  };
  std::shared_ptr<State> state_;
  std::jthread thread_;
};
}
