// Input latency: the trace (input_latency_logic.h), the mouse path (native_kbm_logic.h),
// the just-in-time present slot (native_present_slot.h), the presenter's ticker hook
// (ui_ticker.h), the frame credit (native_frame_flight.h) and the scene frame queue's
// newest/consumed modes (native_backend_frame_queue.h), plus a fake-clock model of the whole
// VSync-on, uncapped, unlocked pipeline that states the latency each mode should have.
#include "input_latency_logic.h"
#include "native_kbm_logic.h"
#include "native_graphics/native_backend_frame_queue.h"
#include "native_graphics/native_frame_flight.h"
#include "native_graphics/native_present_slot.h"
#include "native_graphics/ui_ticker.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0;
void Check(bool ok, const std::string& message) {
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
bool Near(double a, double b, double tolerance = 1e-6) { return std::fabs(a - b) <= tolerance; }
constexpr int64_t kMs = 1'000'000;

// ---- the tracker --------------------------------------------------------------------------
void TestTrackerChain() {
  using namespace edf::latency;
  Tracker tracker;
  // Two mouse events and a key press before the step's pad poll at 10 ms (tick 7).
  tracker.Input(InputKind::kMouse, 1 * kMs);
  tracker.Input(InputKind::kKey, 2 * kMs);
  tracker.Input(InputKind::kMouse, 9 * kMs);
  tracker.Consumed(10 * kMs, 7);
  // Arrives after the poll: belongs to the next step.
  tracker.Input(InputKind::kMouse, 11 * kMs);
  // A render of tick 6 does not show tick 7's step.
  tracker.FrameRecorded(6, 12 * kMs);
  Check(tracker.TakeCompleted().empty(), "nothing completes before a present");
  tracker.FrameSubmitted(40, 13 * kMs);  // image 40 is the tick-6 frame
  tracker.FrameRecorded(7, 20 * kMs);
  tracker.FrameSubmitted(41, 30 * kMs);
  // The presenter took image 41 directly (40 had been shown earlier and carries nothing).
  tracker.FrameAcquired(41, 35 * kMs);
  tracker.FramePresented(500, 36 * kMs);
  Check(tracker.TakeCompleted().empty(), "a present is not a photon");
  // DXGI reports present 499 on screen: not ours yet.
  tracker.Photon(499, 37 * kMs);
  Check(tracker.TakeCompleted().empty(), "an earlier present's photon completed a later one");
  tracker.Photon(500, 50 * kMs);
  const auto done = tracker.TakeCompleted();
  Check(done.size() == 3, "three inputs reach the screen with present 500");
  if (done.size() == 3) {
    const auto& s = done[0];
    Check(s.kind == InputKind::kMouse && s.tick == 7 && s.sequence == 41 && s.present_id == 500 && !s.photon_estimated,
          "the first sample carries its tick, image and present");
    const auto ms = StageMs(s);
    Check(Near(ms[size_t(Stage::kInputToTick)], 9) && Near(ms[size_t(Stage::kTickToRecord)], 10) &&
              Near(ms[size_t(Stage::kRecordToSubmit)], 10) && Near(ms[size_t(Stage::kSubmitToAcquire)], 5) &&
              Near(ms[size_t(Stage::kAcquireToPresent)], 1) && Near(ms[size_t(Stage::kPresentToPhoton)], 14) &&
              Near(ms[size_t(Stage::kTotal)], 49),
          "stage durations");
    Check(done[1].kind == InputKind::kKey, "key press traced");
  }
  Check(tracker.in_flight() == 1, "the input after the poll is still waiting for its step");
}

void TestTrackerMailboxAndEstimates() {
  using namespace edf::latency;
  Tracker tracker;
  tracker.Input(InputKind::kMouse, 0);
  tracker.Consumed(1 * kMs, 1);
  tracker.FrameRecorded(1, 2 * kMs);
  tracker.FrameSubmitted(10, 3 * kMs);
  tracker.Input(InputKind::kMouse, 4 * kMs);
  tracker.Consumed(5 * kMs, 2);
  tracker.FrameRecorded(2, 6 * kMs);
  tracker.FrameSubmitted(11, 7 * kMs);
  // The presenter skipped image 10 and took 11: the first input rides along in 11.
  tracker.FrameAcquired(11, 8 * kMs);
  tracker.FramePresented(3, 9 * kMs);
  // No DXGI statistics: the next waitable return is the estimate; one before the present is not.
  tracker.DisplaySignal(9 * kMs - 1);
  Check(tracker.TakeCompleted().empty(), "a flip before the present is not its photon");
  tracker.DisplaySignal(16 * kMs);
  const auto done = tracker.TakeCompleted();
  Check(done.size() == 2 && done[0].sequence == 10 && done[0].acquire == 8 * kMs && done[0].record == 2 * kMs &&
            done[0].photon_estimated,
        "skipped image: input shown by the newer image (first image and record time kept), estimate flagged");
  // A DXGI report for a later present completes earlier ones as an upper bound.
  tracker.Input(InputKind::kKey, 20 * kMs);
  tracker.Consumed(21 * kMs, 3);
  tracker.FrameRecorded(3, 22 * kMs);
  tracker.FrameSubmitted(12, 23 * kMs);
  tracker.FrameAcquired(12, 24 * kMs);
  tracker.FramePresented(4, 25 * kMs);
  tracker.Photon(5, 40 * kMs);
  const auto late = tracker.TakeCompleted();
  Check(late.size() == 1 && late[0].photon_estimated && late[0].photon == 40 * kMs,
        "photon of a later present is an estimate for an earlier one");
}

void TestTrackerDiscardExpireOverflow() {
  using namespace edf::latency;
  Tracker tracker(Tracker::Limits{4, 100 * kMs});
  tracker.Input(InputKind::kMouse, 0);
  tracker.Input(InputKind::kMouse, 1 * kMs);
  tracker.Discarded(1 * kMs);  // the F1 menu held the input
  Check(tracker.discarded() == 2 && tracker.in_flight() == 0, "withheld input is not a sample");
  tracker.Input(InputKind::kMouse, 2 * kMs);
  tracker.Consumed(3 * kMs, 1);
  tracker.Input(InputKind::kMouse, 200 * kMs);  // the first never reached a frame
  Check(tracker.expired() == 1 && tracker.in_flight() == 1, "stale in-flight input expires");
  for (int i = 0; i < 6; ++i) tracker.Input(InputKind::kMouse, (201 + i) * kMs);
  Check(tracker.overflow() == 3 && tracker.in_flight() == 4, "a stage keeps at most max_events");
}

void TestSummaries() {
  using namespace edf::latency;
  std::vector<double> values;
  for (int i = 1; i <= 100; ++i) values.push_back(i);
  const auto p = Summarize(values);
  Check(p.n == 100 && p.p50 == 50 && p.p90 == 90 && p.p99 == 99 && p.max == 100 && Near(p.mean, 50.5),
        "nearest-rank percentiles");
  const auto one = Summarize({7.0});
  Check(one.p50 == 7 && one.p99 == 7, "one sample");
  Check(Summarize({}).n == 0, "no samples");

  UiBlockAccumulator ui;
  // Blocked 15 ms of every 16 ms for one second: an event waits 15^2 / (2 * 16) = 7.03 ms.
  for (int i = 0; i < 1000 / 16; ++i) ui.Add(int64_t(i) * 16 * kMs, int64_t(i) * 16 * kMs + 15 * kMs);
  const int64_t window = int64_t(1000 / 16) * 16 * kMs;
  Check(Near(ui.BlockedPercent(window), 93.75, 1e-9) && Near(ui.ExpectedDelayMs(window), 225.0 / 32.0, 1e-9),
        "UI-thread block: fraction and expected delivery delay");

  Check(QpcToNs(10'000'000, 10'000'000) == 1'000'000'000 && QpcToNs(3, 3'000'000) == 1000 &&
            QpcToNs(24'000'001, 24'000'000) == 1'000'000'041,
        "QPC ticks to steady_clock nanoseconds");

  Sample s;
  s.input = 0; s.tick_time = 1 * kMs; s.record = 2 * kMs; s.submit = 3 * kMs; s.acquire = 4 * kMs;
  s.present = 5 * kMs; s.photon = 6 * kMs;
  const auto line = FormatReport(InputKind::kMouse, {s}, 0, 50.0, 4.0, true, true, 1, 2, 3);
  Check(line.rfind("Input latency: kind=mouse n=1 input_tick=1.00/1.00/1.00/1.00", 0) == 0 &&
            line.find(" total=6.00/6.00/6.00/6.00") != std::string::npos &&
            line.find("low_latency=1 vsync=1 expired=1 discarded=2 overflow=3") != std::string::npos,
        "report line: " + line);
}

// ---- the mouse path -----------------------------------------------------------------------
void TestMouseRouter() {
  using namespace edf::kbm;
  MouseRouter router;
  // Not on foot yet: motion goes to the stick fallback, and no late sample is taken.
  auto stick = router.OnPoll(10.0f, 0.0f, 1.0f);
  Check(stick.x > 0.0f && !router.late_allowed(), "stick fallback before the first aim update");
  float dx = 0, dy = 0;
  bool late_called = false;
  router.OnAim(dx, dy, [&](float&, float&) { late_called = true; });
  Check(!late_called && dx == 0.0f, "an aim update right after a stick poll takes no late motion");
  // On foot: raw counts accumulate unchanged per tick (no smoothing, no acceleration)...
  router.OnPoll(3.0f, -2.0f, 1.0f);
  router.OnPoll(4.0f, 1.0f, 1.0f);  // a second poll before the aim (a catch-up step)
  Check(router.late_allowed(), "on-foot poll allows the late sample");
  router.OnAim(dx, dy, [](float& x, float& y) { x = 5.0f; y = 0.5f; });
  Check(dx == 12.0f && dy == -0.5f, "per-tick delta is the exact sum of the raw counts plus the late ones");
  router.OnAim(dx, dy);
  Check(dx == 0.0f && dy == 0.0f, "each count is applied once");
  router.Reset();
  Check(!router.late_allowed(), "a reset (menu, focus loss) withdraws the late sample");

  // The angle is linear in counts: splitting motion across ticks turns by the same total.
  const float whole = AimInputForCounts(100.0f, 1.3f, 0.5f);
  const float split = AimInputForCounts(37.0f, 1.3f, 0.5f) + AimInputForCounts(63.0f, 1.3f, 0.5f);
  Check(Near(whole, split, 1e-5), "aim input is linear in counts (no curve)");

  // The sweep: +rate for 500 ms, still, -rate, still; a cycle nets zero whatever the wake-ups.
  Check(Near(SweepCounts(0, 500, 2.0f), 1000.0) && Near(SweepCounts(500, 1000, 2.0f), 0.0) &&
            Near(SweepCounts(1000, 1500, 2.0f), -1000.0) && Near(SweepCounts(0, 2000, 2.0f), 0.0, 1e-3),
        "sweep square wave");
  double total = 0, t = 0;
  for (; t < 3100; t += 1.37) total += SweepCounts(t, t + 1.37, 3.0f);
  Check(Near(total, SweepCounts(0, t, 3.0f), 1e-2), "sweep is cadence-free");
}

// ---- just-in-time present slot ------------------------------------------------------------
struct FakeSemaphore {
  std::mutex mutex;
  int count = 0;
  int waits = 0;
};
edf::native::NativePresentSlot::Ops FakeOps(FakeSemaphore& sem, std::atomic<int>& open_handles) {
  return {[&open_handles](void* handle) -> void* { ++open_handles; return handle; },
          [&sem](void*, uint32_t) {
            std::lock_guard lock(sem.mutex);
            ++sem.waits;
            if (sem.count <= 0) return false;  // the fake never blocks: no count is a timeout
            --sem.count;
            return true;
          },
          [&open_handles](void*) { --open_handles; }};
}
void TestPresentSlot() {
  using edf::native::NativePresentSlot;
  FakeSemaphore sem;
  std::atomic<int> open_handles{0};
  int handle = 0;
  NativePresentSlot slot(FakeOps(sem, open_handles));
  Check(!slot.Acquire(10) && !slot.Spend(), "no window: nothing to wait on, Present waits as before");
  slot.Attach(&handle);
  sem.count = 1;  // the swap chain starts with room for one present (maximum latency 1)
  Check(slot.Acquire(10) && sem.count == 0, "just-in-time wait takes the count");
  Check(slot.Acquire(10) && sem.waits == 1, "a held credit is not waited for twice");
  Check(slot.Spend() && !slot.Spend(), "Present spends the credit once, then waits itself");
  Check(!slot.Acquire(10), "no flip yet: the wait times out and holds nothing");
  sem.count = 1;  // the flip
  Check(slot.Acquire(10), "the flip releases the next present");
  slot.Attach(&handle);  // the swap chain was rebuilt (resize)
  Check(!slot.Spend(), "a credit of the old swap chain is not spent on the new one");
  Check(open_handles.load() == 0, "every duplicate handle is closed");

  // A rebuild during the wait: the count came from the old chain and is not kept.
  NativePresentSlot* self = nullptr;
  NativePresentSlot raced({[](void* h) -> void* { return h; },
                           [&](void*, uint32_t) { self->Attach(&handle); return true; }, [](void*) {}});
  self = &raced;
  raced.Attach(&handle);
  Check(!raced.Acquire(10) && !raced.Spend(), "a wait that straddles a swap-chain rebuild holds nothing");
}

// ---- the presenter's ticker --------------------------------------------------------------
void TestTickerDeadlineGrid() {
  using Clock = std::chrono::steady_clock;
  using std::chrono::nanoseconds;
  const nanoseconds period(16'666'667);
  const Clock::time_point origin = Clock::time_point{} + std::chrono::hours(1);
  Check(edf::native::NextUiTickerDeadline(Clock::time_point{}, origin, period) == origin + period,
        "the first timed wake is one period from now");
  // Wakes that land 0..1.4 ms after their slot (timer latency) keep the grid: 600 wakes span
  // exactly 600 periods, where now + period after each wake ran 0.7 ms per wake slow (57.2 Hz).
  Clock::time_point deadline{}, now = origin, first{};
  for (int i = 0; i < 601; ++i) {
    deadline = edf::native::NextUiTickerDeadline(deadline, now, period);
    if (i == 0) first = deadline;
    now = deadline + nanoseconds((i * 7919 % 15) * 100'000);  // the next wake, 0..1.4 ms late
  }
  Check(deadline - first == period * 600, "timed wakes keep an absolute grid (60.000 Hz on average)");
  // A wake later than the next slot restarts the grid at now + period (no catch-up burst).
  const auto late = origin + period * 2 + nanoseconds(1'000'000);
  Check(edf::native::NextUiTickerDeadline(origin + period, late, period) == late + period,
        "a slot that has passed restarts the grid");
  // A slot more than one period ahead (the period shrank from the occluded 250 ms) restarts it.
  Check(edf::native::NextUiTickerDeadline(origin + std::chrono::milliseconds(250), origin, period) == origin + period,
        "a slot more than a period ahead restarts the grid");
}

void TestTickerBeforeDispatch() {
  std::mutex mutex;
  std::vector<char> order;
  edf::native::NativeUiTicker ticker(
      [](std::function<void()> callback) { callback(); return true; },  // paints inline on the ticker thread
      [&] { std::lock_guard lock(mutex); order.push_back('p'); });
  ticker.SetBeforeDispatch([&] { std::lock_guard lock(mutex); order.push_back('w'); });
  auto ready = ticker.FrameReadyCallback();
  for (int i = 0; i < 5; ++i) {
    ready();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  ticker.Stop();
  std::lock_guard lock(mutex);
  // The first paint may be dispatched before SetBeforeDispatch lands.
  if (!order.empty() && order.front() == 'p') order.erase(order.begin());
  bool alternates = !order.empty() && order.size() % 2 == 0;
  for (size_t i = 0; i < order.size(); ++i) alternates = alternates && order[i] == (i % 2 ? 'p' : 'w');
  Check(alternates && order.size() >= 4, "every paint is preceded by exactly one display wait");
}

// ---- frame credits -----------------------------------------------------------------------
struct FakeCompletion final : edf::native::NativeBackendCompletion {
  std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
  bool Complete() const override { return done->load(); }
};
void TestFrameFlight() {
  edf::native::NativeFrameFlight flight(2);
  auto a = std::make_shared<FakeCompletion>(), b = std::make_shared<FakeCompletion>();
  flight.Submit(a);
  Check(flight.Ready(), "latency 2: one frame in flight admits the next");
  flight.Submit(b);
  flight.SetLimit(1);  // edf_low_latency switched on, after the submit, as the swap wait does
  Check(!flight.Ready(), "latency 1 waits for every frame in flight");
  *a->done = true;
  Check(!flight.Ready(), "latency 1 still waits for the newest frame");
  *b->done = true;
  Check(flight.Ready() && flight.pending() == 0, "drained");
  flight.Submit(std::make_shared<FakeCompletion>());
  flight.SetLimit(2);
  Check(flight.Ready(), "back to 2");

  const int64_t refresh = 16'666'667;
  edf::native::NativeFrameCreditPolicy policy;
  Check(policy.Limit() == 2, "two frames while the GPU keeps up");
  policy.Observe(false, 20 * kMs, refresh);
  Check(policy.Limit() == 2, "a swap that did not wait keeps two");
  policy.Observe(true, 20 * kMs, refresh);
  Check(policy.Limit() == 1 && policy.gpu_bound(), "GPU-bound slower than the display: one frame");
  for (uint32_t i = 0; i < edf::native::NativeFrameCreditPolicy::kHoldFrames - 1; ++i)
    policy.Observe(false, 25 * kMs, refresh);
  Check(policy.Limit() == 1, "held for kHoldFrames");
  policy.Observe(false, 25 * kMs, refresh);
  Check(policy.Limit() == 2, "then two again to look");
  policy.Observe(true, 9 * kMs, refresh);
  Check(policy.Limit() == 2, "a GPU-bound loop still faster than the display keeps two (fresh loop time)");
  edf::native::NativeFrameCreditPolicy unknown;
  unknown.Observe(true, 9 * kMs, 0);
  Check(unknown.Limit() == 1, "an unknown refresh: GPU-bound means one frame");

  edf::latency::RefreshPeriodEstimator estimator;
  int64_t t = 0;
  for (int i = 0; i < 7; ++i) estimator.Flip(t += refresh);
  Check(estimator.PeriodNs() == 0, "the refresh is unknown before eight intervals");
  for (int i = 0; i < 20; ++i) estimator.Flip(t += (i % 3 ? refresh : 2 * refresh));  // missed refreshes
  estimator.Flip(t += 500'000);  // a spurious early return is not an interval
  Check(estimator.PeriodNs() == refresh, "the refresh period ignores missed refreshes");
  bool threw = false;
  try { flight.SetLimit(0); } catch (const std::invalid_argument&) { threw = true; }
  Check(threw, "limit 0 rejected");
}

// ---- scene frame queue -------------------------------------------------------------------
edf::native::NativeBackendPublishedFrame FrameOf(uint64_t sequence) {
  edf::native::NativeBackendPublishedFrame frame;
  static int texture = 0, fence = 0;
  frame.texture = &texture;
  frame.fence = &fence;
  frame.value = sequence;
  frame.sequence = sequence;
  return frame;
}
std::shared_ptr<void> Owner() { return std::make_shared<int>(0); }
edf::native::NativeBackendFrameCopied Copied() {
  auto done = std::make_shared<FakeCompletion>();
  *done->done = true;
  return {nullptr, 0, done};
}
void TestFrameQueueModes() {
  using Queue = edf::native::NativeBackendFrameQueue;
  using Clock = Queue::Clock;
  {
    Queue queue;
    for (uint64_t s = 1; s <= 3; ++s) queue.Publish(queue.Reserve(Clock::now() + std::chrono::seconds(1)), FrameOf(s), Owner());
    uint64_t seen = 0;
    queue.Visit(0, [&](const auto& frame) { seen = frame.sequence; return Copied(); });
    Check(seen == 1 && queue.ready() == 2, "FIFO takes the oldest image");
    queue.Visit(seen, [&](const auto& frame) { seen = frame.sequence; return Copied(); }, nullptr, true);
    Check(seen == 3 && queue.ready() == 0 && queue.skipped() == 1, "newest mode takes the latest and drops the rest");
  }
  {
    Queue queue;
    queue.Publish(queue.Reserve(Clock::now() + std::chrono::seconds(1)), FrameOf(1), Owner());
    uint64_t seen = 0;
    Check(!queue.Visit(0, [&](const auto& frame) { seen = frame.sequence; return Copied(); }),
          "FIFO primes with two images");
    Check(queue.Visit(0, [&](const auto& frame) { seen = frame.sequence; return Copied(); }, nullptr, true) &&
              seen == 1,
          "newest mode shows a lone image at once");
  }
}

// ---- model of the VSync-on, uncapped, unlocked pipeline ------------------------------------
// One engine loop (821A6508) per iteration: the swap (+24: submit and publish the previous
// frame's image, then the frame credit; `gate`, rejected: also wait until the presenter took
// it), then the step's pad
// poll (the input sample), then `work` of simulation and render (the render uses the sample of
// the previous iteration: the 821A4DE8 publication lags one loop), then the end-frame's slot
// reservation, which blocks while the queue's three slots are full. The presenter: legacy
// takes the oldest image (primed with two) right after its previous Present returned, composes,
// then waits in Present for the frame-latency object (the previous present on screen, maximum
// latency 1) and presents; low latency waits for that object first, then takes the newest
// image and presents at once. An image is on screen at the first refresh after its Present.
// Uses the real NativeBackendFrameQueue for slots, priming and FIFO/newest.
struct ModelResult {
  double sample_to_photon_ms = 0;  // pad poll to the vblank showing the frame that used it
  double frames_per_refresh = 0;   // distinct images shown per refresh
};
struct ModelMode {
  bool jit = false;     // the presenter waits for the display before its paint, on the ticker thread
  bool newest = false;  // the presenter takes the newest image (mailbox), no priming
  bool gate = false;    // the producer waits in the swap until the presenter took its image
  size_t flight = 2;    // GPU frames the swap lets the CPU run ahead of; 0: NativeFrameCreditPolicy
  double gpu_ms = 0;    // GPU time of one scene frame (0: never the limit)
};
ModelResult RunModel(ModelMode mode, double refresh_ms, double work_ms, double compose_ms) {
  using Queue = edf::native::NativeBackendFrameQueue;
  using Clock = Queue::Clock;
  constexpr int64_t kNever = std::numeric_limits<int64_t>::max();
  const int64_t P = int64_t(refresh_ms * 1000), W = int64_t(work_ms * 1000), C = int64_t(compose_ms * 1000);
  const auto vblank_after = [P](int64_t t) { return (t / P + 1) * P; };  // microseconds
  const int64_t end = 3000 * P, warmup = 200 * P;
  Queue queue;
  std::vector<int64_t> sample_of(1, -1);  // image sequence -> the pad-poll time it shows

  enum class Producer { kSwap, kFlight, kGate, kWork, kReserve } producer = Producer::kSwap;
  const int64_t G = int64_t(mode.gpu_ms * 1000);
  std::vector<int64_t> gpu_end_of(1, 0);  // image sequence -> its scene GPU work finished
  int64_t gpu_free = 0;
  edf::native::NativeFrameCreditPolicy policy;
  int64_t last_swap = 0;
  int64_t tp = 0, reserve_time = 0;
  bool have_frame = false, reserved = false;  // an end-framed image waiting for the next swap
  size_t frame_slot = 0;
  int64_t frame_sample = -1, previous_sample = -1, sample = -1;

  enum class Host { kIdle, kWaitSlot, kPresent } host = Host::kIdle;
  int64_t th = 0, last_display = 0, host_next = 0;
  bool host_needs_publish = false;  // legacy: not primed; wait for another image
  uint64_t shown = 0, acquired = 0, distinct = 0, latency_n = 0;
  double latency_sum = 0;

  const auto try_reserve = [&](size_t& slot) {
    try { slot = queue.Reserve(Clock::now()); return true; } catch (const std::runtime_error&) { return false; }
  };
  const auto visit = [&] {
    return queue.Visit(acquired, [&](const auto& frame) { acquired = frame.sequence; return Copied(); }, nullptr,
                       mode.newest);
  };

  for (;;) {
    int64_t producer_time = kNever;
    if (producer == Producer::kSwap || producer == Producer::kWork || producer == Producer::kFlight) producer_time = tp;
    if (producer == Producer::kGate && queue.ready() == 0) producer_time = std::max(tp, th);
    if (producer == Producer::kReserve) {
      size_t slot;
      if (!reserved && try_reserve(slot)) { reserved = true; frame_slot = slot; reserve_time = std::max(tp, th); }
      if (reserved) producer_time = reserve_time;
    }
    int64_t host_time = kNever;
    if (host == Host::kIdle && queue.ready() > 0 && !host_needs_publish) host_time = th;
    if (host == Host::kWaitSlot || host == Host::kPresent) host_time = host_next;
    const int64_t now = std::min(producer_time, host_time);
    if (now == kNever || now > end) break;

    if (host_time <= producer_time) {  // the presenter
      th = now;
      if (host == Host::kIdle) {
        if (mode.jit) {
          host = Host::kWaitSlot;  // the ticker waits for the frame-latency object
          host_next = std::max(th, last_display);
        } else if (visit()) {
          host = Host::kPresent;  // compose, then Present waits for the object
          host_next = std::max(th + C, last_display);
        } else {
          host_needs_publish = true;
        }
      } else if (host == Host::kWaitSlot) {
        if (visit()) { host = Host::kPresent; host_next = th + C; }
        else { host = Host::kIdle; host_needs_publish = true; }  // FIFO not primed yet
      } else {
        // On screen at the first refresh after the Present and after the scene's GPU work.
        const int64_t display = vblank_after(std::max(host_next, gpu_end_of[acquired]));
        if (acquired != shown) {
          shown = acquired;
          ++distinct;
          if (host_next > warmup && sample_of[shown] >= 0) {
            latency_sum += double(display - sample_of[shown]);
            ++latency_n;
          }
        }
        last_display = display;
        host = Host::kIdle;
      }
      continue;
    }
    tp = now;  // the engine loop
    switch (producer) {
      case Producer::kSwap:
        if (have_frame) {
          sample_of.push_back(frame_sample);
          gpu_free = std::max(tp, gpu_free) + G;  // submitted here; the GPU runs frames in order
          gpu_end_of.push_back(gpu_free);
          queue.Publish(frame_slot, FrameOf(sample_of.size() - 1), Owner());
          have_frame = false;
          host_needs_publish = false;
          // The frame credit: wait for the GPU to finish the frame `flight - 1` before this one.
          const size_t newest = gpu_end_of.size() - 1;
          const size_t limit = mode.flight ? mode.flight : policy.Limit();
          const int64_t credit = newest + 1 > limit ? gpu_end_of[newest + 1 - limit] : 0;
          if (!mode.flight) {
            policy.Observe(credit > tp, (tp - last_swap) * 1000, P * 1000);  // model times are microseconds
            last_swap = tp;
          }
          if (credit > tp) { tp = credit; producer = Producer::kFlight; break; }
          if (mode.gate) { producer = Producer::kGate; break; }  // presentation credit
        }
        [[fallthrough]];
      case Producer::kFlight:
        if (producer == Producer::kFlight && mode.gate) { producer = Producer::kGate; break; }
        [[fallthrough]];
      case Producer::kGate:
        previous_sample = sample;
        sample = tp;  // this step's pad poll
        tp += W;
        producer = Producer::kWork;
        break;
      case Producer::kWork: {
        frame_sample = previous_sample;  // this render shows the previous loop's publication
        size_t slot;
        if (try_reserve(slot)) { frame_slot = slot; have_frame = true; producer = Producer::kSwap; }
        else producer = Producer::kReserve;
        break;
      }
      case Producer::kReserve:
        reserved = false;
        have_frame = true;
        producer = Producer::kSwap;
        break;
    }
  }
  ModelResult result;
  result.sample_to_photon_ms = latency_n ? latency_sum / double(latency_n) / 1000.0 : 0.0;
  result.frames_per_refresh = double(distinct) / double(end / P);
  return result;
}
void TestPipelineModel() {
  for (const double refresh : {1000.0 / 60.0, 1000.0 / 144.0}) {
    const double work = refresh * 0.4, compose = 0.5;
    const auto legacy = RunModel({}, refresh, work, compose);
    const auto jit = RunModel({true, false, false}, refresh, work, compose);
    const auto jit_newest = RunModel({true, true, false}, refresh, work, compose);
    const auto low = RunModel({true, true, false, 0}, refresh, work, compose);  // edf_low_latency
    const auto paced = RunModel({true, true, true}, refresh, work, compose);
    std::printf("model refresh=%.2f ms: legacy %.2f refreshes; + just-in-time presenter %.2f; + newest image %.2f; "
                "producer paced to the display instead %.2f\n",
                refresh, legacy.sample_to_photon_ms / refresh, jit.sample_to_photon_ms / refresh,
                jit_newest.sample_to_photon_ms / refresh, paced.sample_to_photon_ms / refresh);
    std::printf("model refresh=%.2f ms work=%.2f ms: legacy sample->photon %.1f ms (%.2f refreshes, %.2f images/refresh), "
                "low latency %.1f ms (%.2f refreshes, %.2f images/refresh)\n",
                refresh, work, legacy.sample_to_photon_ms, legacy.sample_to_photon_ms / refresh,
                legacy.frames_per_refresh, low.sample_to_photon_ms, low.sample_to_photon_ms / refresh,
                low.frames_per_refresh);
    Check(legacy.frames_per_refresh > 0.95 && low.frames_per_refresh > 0.95,
          "both modes show a new image every refresh when rendering outpaces the display");
    Check(legacy.sample_to_photon_ms / refresh > 5.0, "legacy queues about six refreshes behind the display");
    Check(low.sample_to_photon_ms / refresh < 2.2, "low latency: about two refreshes");
    Check(low.sample_to_photon_ms < paced.sample_to_photon_ms,
          "a producer paced to the display adds a refresh (the render lags the step by one loop)");
  }
  // GPU-bound, VSync on, 60 Hz: the frame credit (edf_native_frame_latency) with the low-latency presenter.
  const double refresh = 1000.0 / 60.0;
  for (const double gpu : {0.5, 0.9, 1.3}) {
    const auto two = RunModel({true, true, false, 2, gpu * refresh}, refresh, 0.3 * refresh, 0.5);
    const auto one = RunModel({true, true, false, 1, gpu * refresh}, refresh, 0.3 * refresh, 0.5);
    std::printf("model refresh=%.2f ms gpu=%.2f ms cpu=%.2f ms: latency 2 -> %.1f ms (%.2f images/refresh), "
                "latency 1 -> %.1f ms (%.2f images/refresh)\n",
                refresh, gpu * refresh, 0.3 * refresh, two.sample_to_photon_ms, two.frames_per_refresh,
                one.sample_to_photon_ms, one.frames_per_refresh);
    const auto adaptive = RunModel({true, true, false, 0, gpu * refresh}, refresh, 0.3 * refresh, 0.5);
    std::printf("  adaptive credit (edf_low_latency) -> %.1f ms (%.2f images/refresh)\n", adaptive.sample_to_photon_ms,
                adaptive.frames_per_refresh);
    Check(adaptive.sample_to_photon_ms <= std::min(one.sample_to_photon_ms, two.sample_to_photon_ms) + 5.0,
          "the adaptive credit is within a few ms of the better fixed one");
    if (gpu < 0.6)
      Check(adaptive.sample_to_photon_ms <= two.sample_to_photon_ms + 0.5,
            "frames outrunning the display keep two frames of credit");
    if (gpu > 1.0)
      Check(adaptive.sample_to_photon_ms <= one.sample_to_photon_ms + 0.5,
            "a GPU that cannot keep up with the display gets one frame of credit");
  }
}
}  // namespace

int main() {
  TestTrackerChain();
  TestTrackerMailboxAndEstimates();
  TestTrackerDiscardExpireOverflow();
  TestSummaries();
  TestMouseRouter();
  TestPresentSlot();
  TestTickerDeadlineGrid();
  TestTickerBeforeDispatch();
  TestFrameFlight();
  TestFrameQueueModes();
  TestPipelineModel();
  if (failures) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "input latency tests passed\n";
  return 0;
}
