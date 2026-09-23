// Render-cap limiter (frame_limiter.h): the deadline schedule and the spin
// margin controller against a fake clock, and a loose timing check of the real
// sleep/spin primitive (EDF_FRAME_LIMITER_SKIP_TIMING=1 skips that part).
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS  // std::getenv
#endif
#include "frame_limiter.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
int failures = 0;
void Check(bool condition, const char* expression, int line) {
  if (!condition) {
    std::cerr << "line " << line << ": CHECK(" << expression << ") failed\n";
    ++failures;
  }
}
#define CHECK(value) Check(static_cast<bool>(value), #value, __LINE__)

constexpr int64_t kMs = 1000000, kUs = 1000;
constexpr int64_t kSecond = 1000000000;

void TestFirstReleaseAndGrid() {
  edf::FrameDeadlineSchedule schedule;
  const int64_t t0 = 5 * kSecond;
  auto release = schedule.Next(t0, 120);
  CHECK(release.reset && !release.rebased && release.deadline_ns == t0);
  // Slots are origin + k * 1e9 / 120, exact: slot 1 at 8333333, slot 3 at 25 ms.
  release = schedule.Next(t0 + 2 * kMs, 120);
  CHECK(!release.reset && !release.rebased && release.deadline_ns == t0 + 8333333);
  release = schedule.Next(t0 + 9 * kMs, 120);
  CHECK(release.deadline_ns == t0 + 16666666);
  release = schedule.Next(t0 + 17 * kMs, 120);
  CHECK(release.deadline_ns == t0 + 25 * kMs);
}

void TestNoDrift() {
  // Each frame calls right after its release plus 4.5 ms of work; after 120
  // releases the grid is exactly one second on, and after an hour (432000
  // frames) still exactly an hour: no truncated-period drift.
  edf::FrameDeadlineSchedule schedule;
  int64_t now = 123456789;
  const int64_t origin = schedule.Next(now, 120).deadline_ns;
  int64_t deadline = origin;
  for (int frame = 1; frame <= 432000; ++frame) {
    now = deadline + 4500 * kUs;
    const auto release = schedule.Next(now, 120);
    CHECK(!release.rebased && !release.reset);
    deadline = release.deadline_ns;
    if (frame == 120) CHECK(deadline == origin + kSecond);
  }
  CHECK(deadline == origin + 3600 * kSecond);
  CHECK(schedule.rebases() == 0);
}

void TestSmallOverrunKeepsGrid() {
  edf::FrameDeadlineSchedule schedule;
  const int64_t t0 = 0;
  schedule.Next(t0, 120);
  // Slot 1 at 8.333 ms; the frame arrives 0.6 ms late (within the 1 ms
  // tolerance): released at once (deadline in the past), grid kept.
  auto release = schedule.Next(t0 + 8933333, 120);
  CHECK(!release.rebased && release.deadline_ns == t0 + 8333333);
  CHECK(release.deadline_ns < t0 + 8933333);
  // The next slot is still on the original grid.
  release = schedule.Next(t0 + 10 * kMs, 120);
  CHECK(release.deadline_ns == t0 + 16666666);
  CHECK(schedule.rebases() == 0);
  CHECK(edf::FrameDeadlineSchedule::HitchToleranceNs(120) == 1 * kMs);
  CHECK(edf::FrameDeadlineSchedule::HitchToleranceNs(1000) == 125 * kUs);
  CHECK(edf::FrameDeadlineSchedule::HitchToleranceNs(30) == 1 * kMs);
}

void TestHitchRebases() {
  edf::FrameDeadlineSchedule schedule;
  schedule.Next(0, 120);
  schedule.Next(1 * kMs, 120);  // slot 1 at 8.333 ms
  // A 40 ms stall: slot 2 (16.667 ms) is long gone. No catch-up credits: the
  // frame is released now and the grid restarts here.
  const int64_t late = 16666666 + 40 * kMs;
  auto release = schedule.Next(late, 120);
  CHECK(release.rebased && release.deadline_ns == late);
  release = schedule.Next(late + 3 * kMs, 120);
  CHECK(!release.rebased && release.deadline_ns == late + 8333333);
  CHECK(schedule.rebases() == 1);
  // Just past the tolerance also rebases; exactly at it does not.
  edf::FrameDeadlineSchedule edge;
  edge.Next(0, 120);
  CHECK(!edge.Next(8333333 + 1 * kMs, 120).rebased);
  CHECK(edge.Next(16666666 + 1 * kMs + 1, 120).rebased);
}

void TestCapChanges() {
  edf::FrameDeadlineSchedule schedule;
  schedule.Next(0, 120);
  schedule.Next(1 * kMs, 120);
  // 120 -> 60: restart at now, then 16.667 ms slots.
  auto release = schedule.Next(9 * kMs, 60);
  CHECK(release.reset && release.deadline_ns == 9 * kMs);
  release = schedule.Next(10 * kMs, 60);
  CHECK(release.deadline_ns == 9 * kMs + 16666666);
  // Cap 0 is uncapped: released at once, every time, and the grid is dropped.
  release = schedule.Next(11 * kMs, 0);
  CHECK(release.deadline_ns == 11 * kMs && !schedule.active());
  release = schedule.Next(12 * kMs, 0);
  CHECK(release.deadline_ns == 12 * kMs);
  // Re-enabling the same cap restarts rather than continuing the old grid.
  release = schedule.Next(13 * kMs, 60);
  CHECK(release.reset && release.deadline_ns == 13 * kMs);
  CHECK(schedule.Next(14 * kMs, 60).deadline_ns == 13 * kMs + 16666666);
  // Reset() (placement switch) restarts too.
  schedule.Reset();
  CHECK(schedule.Next(20 * kMs, 60).reset);
}

void TestClockAnomaly() {
  // A caller clock that went backwards by more than a period never produces a
  // wait longer than one period.
  edf::FrameDeadlineSchedule schedule;
  schedule.Next(100 * kMs, 120);
  const auto release = schedule.Next(80 * kMs, 120);
  CHECK(release.deadline_ns - 80 * kMs <= 8333333 + 1);
  CHECK(release.deadline_ns > 80 * kMs);
}

// A simulated before-present loop: work varies 3-7 ms (sim-step frames are the
// heavy ones), the sleeper exits 0-40 us after the deadline. Present intervals
// stay within the exit jitter of the period and the average is exact.
void TestSimulatedLoop() {
  edf::FrameDeadlineSchedule schedule;
  uint32_t seed = 12345;
  auto random = [&](int64_t range) {
    seed = seed * 1664525u + 1013904223u;
    return int64_t(seed >> 8) % range;
  };
  int64_t now = 0, previous_release = -1, first_release = 0;
  int64_t worst = 0;
  const int frames = 1200;
  for (int frame = 0; frame < frames; ++frame) {
    const auto release = schedule.Next(now, 120);
    const int64_t released = std::max(now, release.deadline_ns) + random(40 * kUs);
    if (previous_release >= 0) worst = std::max(worst, std::abs((released - previous_release) - 8333333));
    else first_release = released;
    previous_release = released;
    const int64_t work = (frame % 2 ? 3 * kMs : 6 * kMs) + random(1 * kMs);
    now = released + work;
  }
  CHECK(worst < 50 * kUs);
  CHECK(schedule.rebases() == 0);
  const double average = double(previous_release - first_release) / (frames - 1);
  CHECK(std::abs(average - 8333333.3) < 100);
}

void TestSpinMargin() {
  edf::SpinMarginController margin(1 * kMs, 250 * kUs, 2 * kMs);
  CHECK(margin.margin_ns() == 1 * kMs);
  // Steady 100 us oversleep: decays toward 100 + 100 us guard, floored at 250.
  for (int i = 0; i < 400; ++i) margin.Observe(100 * kUs);
  CHECK(margin.margin_ns() == 250 * kUs);
  // One 700 us oversleep raises it at once (fast attack) ...
  margin.Observe(700 * kUs);
  CHECK(margin.margin_ns() == 800 * kUs);
  // ... and it decays back slowly: still above 500 us after 8 quiet waits.
  for (int i = 0; i < 8; ++i) margin.Observe(50 * kUs);
  CHECK(margin.margin_ns() > 500 * kUs && margin.margin_ns() < 800 * kUs);
  for (int i = 0; i < 400; ++i) margin.Observe(50 * kUs);
  CHECK(margin.margin_ns() == 250 * kUs);
  // A preemption far beyond the maximum clamps at the maximum.
  margin.Observe(10 * kMs);
  CHECK(margin.margin_ns() == 2 * kMs);
  // Negative observations count as zero; a lower floor lets it decay further.
  margin.SetMinimum(0);
  for (int i = 0; i < 600; ++i) margin.Observe(-5 * kUs);
  CHECK(margin.margin_ns() == edf::SpinMarginController::kGuardNs);
  // Raising the floor lifts the margin at once.
  margin.SetMinimum(300 * kUs);
  CHECK(margin.margin_ns() == 300 * kUs);
}

void TestPacerClock() {
  const int64_t a = edf::PacerClock::NowNs();
  const int64_t b = edf::PacerClock::NowNs();
  CHECK(b >= a);
#ifdef _WIN32
  const int64_t f = edf::PacerClock::Frequency();
  CHECK(f > 0);
  CHECK(edf::PacerClock::TicksToNs(f) == kSecond);
  CHECK(edf::PacerClock::NsToTicks(kSecond) == f);
  // Round trip: ticks for a deadline never land before it.
  for (int64_t ns : {int64_t(1), int64_t(8333333), int64_t(123456789012345)})
    CHECK(edf::PacerClock::TicksToNs(edf::PacerClock::NsToTicks(ns)) >= ns);
#endif
}

// The real primitive: never early, and on an idle machine well under a
// millisecond late. Bounds are loose so a loaded test machine does not fail:
// the median exit must be within 1 ms and the worst within 20 ms.
void TestRealSleeper() {
  const char* skip = std::getenv("EDF_FRAME_LIMITER_SKIP_TIMING");
  if (skip && *skip == '1') {
    std::cout << "frame limiter timing test skipped\n";
    return;
  }
  edf::PreciseSleeper sleeper;
  std::vector<int64_t> late;
  bool early = false;
  for (int i = 0; i < 40; ++i) {
    const int64_t deadline = edf::PacerClock::NowNs() + (i % 2 ? 4 * kMs : 1500 * kUs);
    const auto result = sleeper.WaitUntil(deadline);
    const int64_t exit = edf::PacerClock::NowNs();
    if (exit < deadline || result.late_ns < 0) early = true;
    late.push_back(result.late_ns);
  }
  // A deadline already passed returns at once.
  const auto passed = sleeper.WaitUntil(edf::PacerClock::NowNs() - 1 * kMs);
  CHECK(passed.late_ns >= 1 * kMs && !passed.timer_wait && passed.spin_ns == 0);
  std::sort(late.begin(), late.end());
  CHECK(!early);
  CHECK(late[late.size() / 2] < 1 * kMs);
  CHECK(late.back() < 20 * kMs);
  CHECK(sleeper.margin_ns() >= 250 * kUs && sleeper.margin_ns() <= 4 * kMs);
  std::cout << "frame limiter timing: timer=" << (sleeper.high_resolution() ? "high_resolution" : "standard")
            << " median_late_us=" << late[late.size() / 2] / 1000.0
            << " max_late_us=" << late.back() / 1000.0 << " margin_us=" << sleeper.margin_ns() / 1000.0 << '\n';
}
}  // namespace

int main() {
  TestFirstReleaseAndGrid();
  TestNoDrift();
  TestSmallOverrunKeepsGrid();
  TestHitchRebases();
  TestCapChanges();
  TestClockAnomaly();
  TestSimulatedLoop();
  TestSpinMargin();
  TestPacerClock();
  TestRealSleeper();
  if (failures) {
    std::cerr << failures << " frame limiter check(s) failed\n";
    return 1;
  }
  std::cout << "frame limiter tests passed\n";
  return 0;
}
