#pragma once
#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace edf::native {
// CPU bookkeeping shared by the legacy swap-completion callback and vblank
// handler. Integration supplies native clock ticks/phase, never GPU registers.
// A true return requests acknowledgement of the pending swap; callers own the
// writeback and must apply it at the corresponding native completion boundary.
struct NativeSwapPacingState {
  uint32_t ticks=0,acknowledged=0,pending=0,callbacks=0;
  // Render acceptance without a synthetic refresh wait. GPU completion is
  // checked separately by the caller and must never be inferred from this.
  bool CompleteUnpaced(uint32_t interval) {
    if(interval<1 || interval>3) throw std::invalid_argument("invalid native swap interval");
    ++callbacks; pending=0; acknowledged=ticks;
    return true;
  }
  // Native presentation has no Xbox raster phase. Once the renderer grants
  // a presentation credit and the requested interval has elapsed, acknowledge
  // acceptance without another synthetic vblank. This does not acknowledge a
  // guest resource fence. Display VSync belongs to the DXGI presenter.
  bool CompleteNative(uint32_t interval) {
    if(interval<1 || interval>3) throw std::invalid_argument("invalid native swap interval");
    ++callbacks;
    pending=interval-ticks+acknowledged;
    if(static_cast<int32_t>(pending)>0) return false;
    pending=0;
    acknowledged=ticks;
    return true;
  }
  bool Complete(uint32_t argument,uint32_t phase_percent) {
    if(!phase_percent || phase_percent>100)
      throw std::invalid_argument("invalid native swap phase");
    ++callbacks;
    pending=(argument>>16)-ticks+acknowledged;
    if(static_cast<int32_t>(pending)>0) return false;
    if(phase_percent>(argument&0xffff)) { pending=1; return false; }
    acknowledged=ticks;
    return true;
  }
  bool Advance(uint64_t elapsed_ticks) {
    const auto previous=ticks;
    ticks+=static_cast<uint32_t>(elapsed_ticks);
    if(static_cast<int32_t>(pending)<=0 || !elapsed_ticks) return false;
    if(elapsed_ticks<pending) { pending-=static_cast<uint32_t>(elapsed_ticks); return false; }
    // Catch-up acknowledges on the tick that reaches zero, not the last tick
    // sampled after a long pause. Match repeated individual CPU tick updates.
    acknowledged=previous+pending;
    pending=0;
    return true;
  }
};
// Engine heartbeat, independent of display refresh and GPU interrupts. The
// retail simulation is fixed-step; a 120 Hz handheld panel must not double it.
class NativePacingClock {
 public:
  using Clock = std::chrono::steady_clock;
  void Reset(Clock::time_point now, uint64_t baseline) {
    epoch_ = now;
    baseline_ = baseline;
    initialized_ = true;
  }
  uint64_t Sample(Clock::time_point now) const {
    if (!initialized_) throw std::logic_error("native pacing clock not initialized");
    if (now < epoch_) throw std::logic_error("native pacing clock moved backwards");
    const auto ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - epoch_).count());
    // Split before multiplying: exact 60 Hz boundaries, no rounded-period drift.
    return baseline_ + (ns / 1000000000) * 60 + (ns % 1000000000) * 60 / 1000000000;
  }
  uint32_t PhasePercent(Clock::time_point now) const {
    (void)Sample(now); // Same initialization/backwards-time contract.
    const auto ns=static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now-epoch_).count());
    return uint32_t(((ns%1000000000)*60%1000000000)/10000000)+1;
  }
  float Fraction(Clock::time_point now) const {
    (void)Sample(now);
    const auto ns=static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now-epoch_).count());
    return float(double((ns%1000000000)*60%1000000000)/1000000000.0);
  }
 private:
  Clock::time_point epoch_{};
  uint64_t baseline_ = 0;
  bool initialized_ = false;
};

inline uint64_t NativePacingSteps(uint64_t current, uint64_t previous, uint32_t divisor) {
  // Retail sign-extends this field before unsigned division. Negative divisors
  // are not a supported native pacing configuration; never silently reinterpret.
  if (!divisor || divisor > INT32_MAX) throw std::invalid_argument("invalid native pacing divisor");
  return (current - previous) / divisor;
}
inline bool RetailPacingPending(uint64_t initial, uint64_t steps) {
  return initial < 2 && steps == initial;
}
// The native renderer already enforces presentation pacing. Consume an elapsed
// simulation tick immediately instead of the retail loop's extra-tick wait.
// Keep elapsed-step accounting: rendering at 60 Hz must not double game speed.
inline bool NativePacingPending(uint64_t steps) { return steps==0; }
inline uint32_t NativePacingResult(uint64_t steps) {
  return steps > 4 ? 1 : static_cast<uint32_t>(steps);
}
}  // namespace edf::native
