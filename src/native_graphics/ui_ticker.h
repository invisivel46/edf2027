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
        std::mutex mutex; std::condition_variable_any wake;
        std::unique_lock lock(mutex);
        while(!stop.stop_requested()) {
          if(!state->pending.exchange(true)) {
            if(!dispatch([state,paint] {
              struct Done { std::shared_ptr<State> state; ~Done(){state->pending=false;} } done{state};
              if(state->active.load()) paint();
            })) {
              // SDK rejects deferred calls after its loop exits. Do not keep
              // waking an already stopped UI loop until the host is destroyed.
              state->pending=false; state->active=false; return;
            }
          }
          const auto now=std::chrono::steady_clock::now();
          deadline+=std::chrono::nanoseconds(state->period.load());
          if(deadline<now) deadline=now+std::chrono::nanoseconds(state->period.load());
          wake.wait_until(lock,stop,deadline,[]{return false;});
        }
      }) {}
  ~NativeUiTicker() { Stop(); }
  void Stop() { state_->active=false; thread_.request_stop(); if(thread_.joinable()) thread_.join(); }
  void SetOccluded(bool value) { state_->period=value?250000000:16666667; }
 private:
  struct State {std::atomic<bool> active{true},pending{false}; std::atomic<int64_t> period{16666667};};
  std::shared_ptr<State> state_;
  std::jthread thread_;
};
}
