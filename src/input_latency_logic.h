// EDF2027 - input-to-photon latency tracing (edf_native_input_latency_trace): the pure logic.
//
// Every keyboard press and mouse motion event the window receives is followed through the
// frame pipeline, one stage at a time, with the host time of each hand-off:
//
//   input    the UI thread received the event (native_kbm_driver.h -> native_kbm.cpp)
//   tick     the simulation step whose pad poll took it (sub_821B09A0, native_kbm.cpp); the
//            step is labelled with the heartbeat tick its budget came from (821BEAB0)
//   record   the render helper (821A5080) began the first frame whose published tick
//            (NativeFrameMotion::tick, the camera/pose publication) includes that step
//   submit   that frame's scene image was submitted and published to the host queue
//            (SubmitSceneFrameLocked -> NativeBackendFrameQueue::Publish)
//   acquire  the host presenter copied the image it shows first that contains the input: the
//            image itself, or a newer one when the host skipped it (low-latency mailbox)
//   present  the host's Present call for that image returned
//   photon   the vblank that put it on screen: DXGI frame statistics (SyncQPCTime of the
//            present) when the swap chain reports them, else the next return of the frame-
//            latency waitable object after the present (the flip that retired it), flagged
//            as an estimate
//
// Events move between stages in blocks that keep their order, so every stage is a FIFO and
// each hand-off is a prefix move. Pure: times are integer nanoseconds on one monotonic clock
// (the app uses steady_clock, which is QueryPerformanceCounter on Windows), so the unit tests
// drive it with a fake clock.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace edf::latency {

enum class InputKind : uint8_t { kMouse, kKey, kCount };
inline constexpr std::array<const char*, 2> kInputKindNames{"mouse", "key"};

enum class Stage : uint8_t {
  kInputToTick, kTickToRecord, kRecordToSubmit, kSubmitToAcquire, kAcquireToPresent, kPresentToPhoton, kTotal,
  kCount
};
inline constexpr size_t kStageCount = static_cast<size_t>(Stage::kCount);
inline constexpr std::array<const char*, kStageCount> kStageNames{
    "input_tick", "tick_record", "record_submit", "submit_acquire", "acquire_present", "present_photon", "total"};

// One input followed to the screen.
struct Sample {
  InputKind kind = InputKind::kMouse;
  int64_t input = 0, tick_time = 0, record = 0, submit = 0, acquire = 0, present = 0, photon = 0;
  uint64_t tick = 0, sequence = 0, present_id = 0;
  bool photon_estimated = false;
};

inline std::array<double, kStageCount> StageMs(const Sample& s) {
  const auto ms = [](int64_t from, int64_t to) { return double(to - from) / 1e6; };
  return {ms(s.input, s.tick_time), ms(s.tick_time, s.record), ms(s.record, s.submit), ms(s.submit, s.acquire),
          ms(s.acquire, s.present), ms(s.present, s.photon), ms(s.input, s.photon)};
}

struct Percentiles {
  size_t n = 0;
  double p50 = 0, p90 = 0, p99 = 0, max = 0, mean = 0;
};
// Nearest-rank percentiles (the value at rank ceil(p * n)); the same rule as
// tools/latency-report.py so the two agree on the same samples.
inline Percentiles Summarize(std::vector<double> values) {
  Percentiles out;
  out.n = values.size();
  if (values.empty()) return out;
  std::sort(values.begin(), values.end());
  const auto rank = [&](double p) {
    const size_t k = size_t(std::ceil(p * double(values.size())));
    return values[std::min(values.size(), std::max<size_t>(k, 1)) - 1];
  };
  out.p50 = rank(0.50);
  out.p90 = rank(0.90);
  out.p99 = rank(0.99);
  out.max = values.back();
  double sum = 0;
  for (double v : values) sum += v;
  out.mean = sum / double(values.size());
  return out;
}

// Time the UI thread spent unable to take input events (inside a paint, which includes
// the legacy frame-latency wait in Present). An event arriving at a uniformly random time
// during a blocked span of length b waits b/2 on average, so over a window W the expected
// added delivery delay is sum(b^2) / (2 W).
class UiBlockAccumulator {
 public:
  void Add(int64_t begin, int64_t end) {
    if (end <= begin) return;
    const double b = double(end - begin);
    blocked_ns_ += b;
    blocked_sq_ += b * b;
  }
  double BlockedPercent(int64_t window_ns) const {
    return window_ns > 0 ? 100.0 * blocked_ns_ / double(window_ns) : 0.0;
  }
  double ExpectedDelayMs(int64_t window_ns) const {
    return window_ns > 0 ? blocked_sq_ / (2.0 * double(window_ns)) / 1e6 : 0.0;
  }
  void Reset() { blocked_ns_ = blocked_sq_ = 0; }

 private:
  double blocked_ns_ = 0, blocked_sq_ = 0;
};

// The display's refresh period from the times the presenter's just-in-time wait returned
// after blocking (each such return is a flip): the shortest interval among the last 64 in
// [2 ms, 100 ms], so a missed refresh (an interval of two periods) does not count.
class RefreshPeriodEstimator {
 public:
  void Flip(int64_t t) {
    if (last_) {
      const int64_t interval = t - last_;
      if (interval >= 2'000'000 && interval <= 100'000'000) {
        intervals_[next_++ % intervals_.size()] = interval;
        count_ = std::min(count_ + 1, intervals_.size());
      }
    }
    last_ = t;
  }
  // 0 until eight intervals were seen.
  int64_t PeriodNs() const {
    if (count_ < 8) return 0;
    return *std::min_element(intervals_.begin(), intervals_.begin() + std::ptrdiff_t(count_));
  }

 private:
  std::array<int64_t, 64> intervals_{};
  size_t next_ = 0, count_ = 0;
  int64_t last_ = 0;
};

class Tracker {
 public:
  // Events older than `expire_ns` (by input time) that are still in flight are dropped as
  // expired: input taken while a menu held the game, a frame never shown, a lost trace.
  // Each stage holds at most `max_events`; beyond that the oldest are dropped (overflow).
  struct Limits {
    size_t max_events = 8192;
    int64_t expire_ns = 2'000'000'000;
  };
  Tracker() = default;
  explicit Tracker(Limits limits) : limits_(limits) {}

  void Input(InputKind kind, int64_t t) {
    Expire(t);
    Event e;
    e.s.kind = kind;
    e.s.input = t;
    Push(pending_, e);
  }
  // A pad poll that gave the game its input. Everything received by `t` was taken.
  void Consumed(int64_t t, uint64_t tick) {
    MovePrefix(pending_, consumed_, [&](const Event& e) { return e.s.input <= t; },
               [&](Event& e) { e.s.tick_time = t; e.s.tick = tick; });
  }
  // A pad poll that withheld input from the game (the F1 menu gate, no focus): what was
  // received by `t` never reaches a frame, so it is not a latency sample.
  void Discarded(int64_t t) {
    while (!pending_.empty() && pending_.front().s.input <= t) {
      pending_.pop_front();
      ++discarded_;
    }
  }
  // The render helper began a frame showing ticks up to `tick`.
  void FrameRecorded(uint64_t tick, int64_t t) {
    MovePrefix(consumed_, recorded_, [&](const Event& e) { return e.s.tick <= tick && e.s.tick_time <= t; },
               [&](Event& e) { e.s.record = t; });
  }
  // A scene image was published. Every frame recorded before it is covered by it: the
  // renders are sequential and the image is the last of them (one not published on its
  // own - an unpublished frame - is shown by the next one that is).
  void FrameSubmitted(uint64_t sequence, int64_t t) {
    MovePrefix(recorded_, submitted_, [&](const Event& e) { return e.s.record <= t; },
               [&](Event& e) { e.s.submit = t; e.s.sequence = sequence; });
  }
  // The host took image `sequence`. Older images it skipped carry their inputs into this one.
  void FrameAcquired(uint64_t sequence, int64_t t) {
    MovePrefix(submitted_, acquired_, [&](const Event& e) { return e.s.sequence <= sequence; },
               [&](Event& e) { e.s.acquire = t; });
  }
  // The host presented what it last acquired, as DXGI present `present_id`.
  void FramePresented(uint64_t present_id, int64_t t) {
    MovePrefix(acquired_, presented_, [&](const Event&) { return true; },
               [&](Event& e) { e.s.present = t; e.s.present_id = present_id; });
  }
  // Exact photon: DXGI says present `present_id` reached the screen at `t`. Earlier
  // presents still waiting were on screen by then (an upper bound, flagged).
  void Photon(uint64_t present_id, int64_t t) {
    while (!presented_.empty() && presented_.front().s.present_id <= present_id) {
      Event e = presented_.front();
      presented_.pop_front();
      e.s.photon = std::max(t, e.s.present);
      e.s.photon_estimated = e.s.present_id != present_id;
      Complete(e);
    }
  }
  // Estimated photon: the frame-latency waitable object returned at `t` after blocking,
  // which is the flip that retired every present made before it.
  void DisplaySignal(int64_t t) {
    while (!presented_.empty() && presented_.front().s.present < t) {
      Event e = presented_.front();
      presented_.pop_front();
      e.s.photon = t;
      e.s.photon_estimated = true;
      Complete(e);
    }
  }

  std::vector<Sample> TakeCompleted() { return std::exchange(completed_, {}); }
  size_t in_flight() const {
    return pending_.size() + consumed_.size() + recorded_.size() + submitted_.size() + acquired_.size() +
           presented_.size();
  }
  uint64_t expired() const { return expired_; }
  uint64_t overflow() const { return overflow_; }
  uint64_t discarded() const { return discarded_; }
  void ResetCounters() { expired_ = overflow_ = discarded_ = 0; }

 private:
  struct Event {
    Sample s;
  };
  template <class Pred, class Update>
  void MovePrefix(std::deque<Event>& from, std::deque<Event>& to, Pred pred, Update update) {
    while (!from.empty() && pred(from.front())) {
      Event e = from.front();
      from.pop_front();
      update(e);
      Push(to, e);
    }
  }
  void Push(std::deque<Event>& to, const Event& e) {
    if (to.size() >= limits_.max_events) {
      to.pop_front();
      ++overflow_;
    }
    to.push_back(e);
  }
  void Complete(const Event& e) {
    if (completed_.size() < limits_.max_events * 4) completed_.push_back(e.s);
    else ++overflow_;
  }
  void Expire(int64_t now) {
    for (auto* stage : {&pending_, &consumed_, &recorded_, &submitted_, &acquired_, &presented_})
      while (!stage->empty() && now - stage->front().s.input > limits_.expire_ns) {
        stage->pop_front();
        ++expired_;
      }
  }

  Limits limits_{};
  std::deque<Event> pending_, consumed_, recorded_, submitted_, acquired_, presented_;
  std::vector<Sample> completed_;
  uint64_t expired_ = 0, overflow_ = 0, discarded_ = 0;
};

// One "Input latency:" log line for a window of samples of one kind. Stage fields are
// p50/p90/p99/max in milliseconds; tools/latency-report.py parses exactly this shape.
inline std::string FormatReport(InputKind kind, const std::vector<Sample>& samples, size_t estimated,
                                double ui_block_percent, double ui_block_delay_ms, bool low_latency, bool vsync,
                                uint64_t expired, uint64_t discarded, uint64_t overflow) {
  std::array<std::vector<double>, kStageCount> stages;
  for (const Sample& s : samples) {
    if (s.kind != kind) continue;
    const auto ms = StageMs(s);
    for (size_t i = 0; i < kStageCount; ++i) stages[i].push_back(ms[i]);
  }
  char buffer[160];
  std::string line = "Input latency: kind=";
  line += kInputKindNames[size_t(kind)];
  std::snprintf(buffer, sizeof buffer, " n=%zu", stages[0].size());
  line += buffer;
  for (size_t i = 0; i < kStageCount; ++i) {
    const auto p = Summarize(stages[i]);
    std::snprintf(buffer, sizeof buffer, " %s=%.2f/%.2f/%.2f/%.2f", kStageNames[i], p.p50, p.p90, p.p99, p.max);
    line += buffer;
  }
  std::snprintf(buffer, sizeof buffer,
                " photon_estimated=%zu ui_block_pct=%.1f ui_block_delay_ms=%.2f low_latency=%d vsync=%d expired=%llu "
                "discarded=%llu overflow=%llu",
                estimated, ui_block_percent, ui_block_delay_ms, low_latency ? 1 : 0, vsync ? 1 : 0,
                static_cast<unsigned long long>(expired), static_cast<unsigned long long>(discarded),
                static_cast<unsigned long long>(overflow));
  line += buffer;
  return line;
}

// QueryPerformanceCounter ticks to nanoseconds, split the way MSVC's steady_clock splits
// them, so a DXGI SyncQPCTime lands on the steady_clock time base the rest of the trace uses.
inline int64_t QpcToNs(int64_t ticks, int64_t frequency) {
  if (frequency <= 0) return 0;
  const int64_t whole = (ticks / frequency) * 1'000'000'000;
  const int64_t part = (ticks % frequency) * 1'000'000'000 / frequency;
  return whole + part;
}

}  // namespace edf::latency
