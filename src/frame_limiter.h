// Render-cap frame limiter (edf_fps_cap): an absolute deadline schedule, an
// adaptive spin margin and a precise sleep primitive (high-resolution
// waitable timer, then a QueryPerformanceCounter spin).
//
// The schedule and the margin controller are pure (times are integer
// nanoseconds on any monotonic clock) so they can be tested with a fake clock.
// PreciseSleeper is the real primitive; it uses its own QPC clock
// (PacerClock), which the hook also uses to feed the schedule.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define EDF_FRAME_LIMITER_PAUSE() _mm_pause()
#else
#define EDF_FRAME_LIMITER_PAUSE() std::this_thread::yield()
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

namespace edf {

// Absolute deadline grid for a frame-rate cap. Slot k of the current grid is
// origin + k * 1e9 / cap, computed from the slot index rather than by adding a
// truncated period, so a 120 FPS grid does not drift (8333333 ns * 120 would
// lose 40 ns a second). Each call is one frame release: it returns the time
// the frame may be released at (a time <= now means release immediately).
//
// - A release that finds the grid slot already passed by at most the hitch
//   tolerance keeps the grid (no wait); the next slot stays on the grid, so
//   small overruns do not shift the phase or lower the average rate.
// - A release later than that is a real hitch: the grid is rebased at now, so
//   there are no catch-up credits (no burst of short frames after a stall).
// - A cap change, or a cap of 0 in between, restarts the grid at now.
class FrameDeadlineSchedule {
 public:
  struct Release {
    int64_t deadline_ns = 0;  // release time; <= now means no wait
    bool reset = false;       // first frame, cap change or cap re-enabled
    bool rebased = false;     // hitch: the grid restarted at now
  };

  // A hitch is lateness past the slot beyond an eighth of a period, at most
  // 1 ms. Timer/spin exit error is microseconds and stays on the grid; a frame
  // whose own work overran the slot by more than this restarts the grid.
  static int64_t HitchToleranceNs(int fps_cap) {
    if (fps_cap <= 0) return 0;
    return std::min<int64_t>(1000000, 1000000000LL / fps_cap / 8);
  }

  Release Next(int64_t now_ns, int fps_cap) {
    if (fps_cap <= 0) {
      active_ = false;
      cap_ = 0;
      return {now_ns, true, false};
    }
    if (!active_ || fps_cap != cap_) {
      Restart(now_ns, fps_cap);
      ++resets_;
      return {now_ns, true, false};
    }
    const int64_t slot = SlotTime(index_ + 1);
    if (now_ns - slot > HitchToleranceNs(fps_cap)) {
      Restart(now_ns, fps_cap);
      ++rebases_;
      return {now_ns, false, true};
    }
    // Never wait more than one period (a caller or clock anomaly, e.g. a
    // steady clock that went backwards): restart the grid one period ahead.
    const int64_t period = SlotTime(1) - origin_;
    if (slot - now_ns > period) {
      Restart(now_ns + period, fps_cap);
      return {origin_, false, true};
    }
    ++index_;
    return {slot, false, false};
  }

  void Reset() { active_ = false; }
  bool active() const { return active_; }
  int cap() const { return cap_; }
  uint64_t rebases() const { return rebases_; }
  uint64_t resets() const { return resets_; }

 private:
  void Restart(int64_t origin_ns, int fps_cap) {
    active_ = true;
    cap_ = fps_cap;
    origin_ = origin_ns;
    index_ = 0;
  }
  int64_t SlotTime(int64_t index) const {
    // index * 1e9 stays well inside int64 for any realistic run (> 290 years at
    // 1000 FPS); split anyway so the product never overflows.
    const int64_t whole = index / cap_, part = index % cap_;
    return origin_ + whole * 1000000000LL + part * 1000000000LL / cap_;
  }

  bool active_ = false;
  int cap_ = 0;
  int64_t origin_ = 0;
  int64_t index_ = 0;
  uint64_t rebases_ = 0, resets_ = 0;
};

// How long before a deadline the sleeper stops using the OS timer and starts
// spinning. It tracks the timer's observed oversleep: fast attack (one
// oversleep beyond the margin raises it at once, so the next frames are not
// late) and slow release (it decays toward the recent oversleep over about 32
// waits, so a single preemption does not keep the CPU spinning for long).
class SpinMarginController {
 public:
  static constexpr int64_t kGuardNs = 100000;  // added to each observation

  SpinMarginController(int64_t initial_ns, int64_t minimum_ns, int64_t maximum_ns)
      : minimum_(minimum_ns), maximum_(std::max(minimum_ns, maximum_ns)),
        margin_(std::clamp(initial_ns, minimum_, maximum_)) {}

  // oversleep_ns: actual timer wake minus the requested wake time.
  void Observe(int64_t oversleep_ns) {
    const int64_t want = std::max<int64_t>(0, oversleep_ns) + kGuardNs;
    if (want > margin_) margin_ = want;
    else margin_ -= (margin_ - want + 31) / 32;
    margin_ = std::clamp(margin_, minimum_, maximum_);
  }
  void SetMinimum(int64_t minimum_ns) {
    minimum_ = std::clamp<int64_t>(minimum_ns, 0, maximum_);
    margin_ = std::clamp(margin_, minimum_, maximum_);
  }
  int64_t margin_ns() const { return margin_; }
  int64_t minimum_ns() const { return minimum_; }
  int64_t maximum_ns() const { return maximum_; }

 private:
  int64_t minimum_, maximum_, margin_;
};

// The limiter's clock: QueryPerformanceCounter in nanoseconds on Windows, the
// steady clock elsewhere. Only differences are meaningful.
struct PacerClock {
#ifdef _WIN32
  static int64_t Frequency() {
    static const int64_t frequency = [] {
      LARGE_INTEGER value{};
      QueryPerformanceFrequency(&value);
      return value.QuadPart > 0 ? int64_t(value.QuadPart) : int64_t(1);
    }();
    return frequency;
  }
  static int64_t Ticks() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
  }
  static int64_t TicksToNs(int64_t ticks) {
    const int64_t f = Frequency();
    return ticks / f * 1000000000LL + ticks % f * 1000000000LL / f;
  }
  static int64_t NsToTicks(int64_t ns) {
    const int64_t f = Frequency();
    // Round up so a spin never ends before the nanosecond deadline.
    return ns / 1000000000LL * f + (ns % 1000000000LL * f + 999999999LL) / 1000000000LL;
  }
  static int64_t NowNs() { return TicksToNs(Ticks()); }
#else
  static int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }
#endif
};

// Sleeps until a PacerClock deadline: an OS timer wait until margin before it,
// then a pause-instruction spin on the performance counter. On Windows the
// timer is a CREATE_WAITABLE_TIMER_HIGH_RESOLUTION waitable timer (Windows 10
// 1803+; about 0.5 ms granularity without raising the process timer
// resolution). Without it a plain waitable timer is used with a larger margin.
// Not thread-safe: one sleeper per pacing thread.
class PreciseSleeper {
 public:
  struct Result {
    int64_t late_ns = 0;       // exit time minus deadline (>= 0 unless it returned at once)
    int64_t oversleep_ns = 0;  // timer wake minus requested wake (0 without a timer wait)
    int64_t spin_ns = 0;       // time spent spinning
    bool timer_wait = false;
  };

  explicit PreciseSleeper(int64_t minimum_margin_ns = 250000)
      : margin_(1000000, minimum_margin_ns, 2000000) {
#ifdef _WIN32
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    high_resolution_ = timer_ != nullptr;
    if (!timer_) {
      timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
      margin_ = SpinMarginController(2000000, std::max<int64_t>(minimum_margin_ns, 1000000), 4000000);
    }
#endif
  }
  ~PreciseSleeper() {
#ifdef _WIN32
    if (timer_) CloseHandle(timer_);
#endif
  }
  PreciseSleeper(const PreciseSleeper&) = delete;
  PreciseSleeper& operator=(const PreciseSleeper&) = delete;

  Result WaitUntil(int64_t deadline_ns) {
    Result result;
    int64_t now = PacerClock::NowNs();
    if (now >= deadline_ns) {
      result.late_ns = now - deadline_ns;
      return result;
    }
    const int64_t wake = deadline_ns - margin_.margin_ns();
    if (wake > now) {
      result.timer_wait = SleepFor(wake - now);
      now = PacerClock::NowNs();
      if (result.timer_wait) {
        result.oversleep_ns = now - wake;
        margin_.Observe(result.oversleep_ns);
      }
    }
    const int64_t spin_start = now;
#ifdef _WIN32
    const int64_t target = PacerClock::NsToTicks(deadline_ns);
    int64_t ticks = PacerClock::Ticks();
    while (ticks < target) {
      EDF_FRAME_LIMITER_PAUSE();
      ticks = PacerClock::Ticks();
    }
    now = PacerClock::TicksToNs(ticks);
#else
    while ((now = PacerClock::NowNs()) < deadline_ns) EDF_FRAME_LIMITER_PAUSE();
#endif
    result.spin_ns = std::max<int64_t>(0, now - spin_start);
    result.late_ns = now - deadline_ns;
    return result;
  }

  void SetMinimumMarginNs(int64_t minimum_ns) {
    margin_.SetMinimum(high_resolution_ ? minimum_ns : std::max<int64_t>(minimum_ns, 1000000));
  }
  int64_t margin_ns() const { return margin_.margin_ns(); }
  bool high_resolution() const { return high_resolution_; }

 private:
  bool SleepFor(int64_t ns) {
#ifdef _WIN32
    if (timer_) {
      LARGE_INTEGER due{};
      due.QuadPart = -std::max<int64_t>(1, ns / 100);  // relative, 100 ns units
      if (SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0) &&
          WaitForSingleObject(timer_, INFINITE) == WAIT_OBJECT_0)
        return true;
    }
#endif
    // No usable timer: the standard sleep (millisecond granularity); the
    // margin controller learns its oversleep like a timer's.
    std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
    return true;
  }

  SpinMarginController margin_;
#ifdef _WIN32
  HANDLE timer_ = nullptr;
#endif
  bool high_resolution_ = false;
};

}  // namespace edf
