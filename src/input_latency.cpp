// EDF2027 - input-to-photon latency: the switch, the trace and its report (input_latency.h).
#include "input_latency.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(edf_low_latency, false, "EDF2027",
                    "Low-latency presentation: the D3D12 presenter waits for the display just in time on its own "
                    "thread and shows the newest frame; with VSync on the renderer stays at most one frame ahead of "
                    "the display, with one frame of GPU work in flight; mouse aim takes the newest motion. Applies live");
REXCVAR_DEFINE_BOOL(edf_native_input_latency_trace, false, "EDF2027",
                    "Trace input-to-photon latency of keyboard and mouse input and log percentiles per stage every "
                    "5 s (\"Input latency:\" lines, tools/latency-report.py). Diagnostic; applies live");
REXCVAR_DEFINE_STRING(edf_native_input_latency_csv, "", "EDF2027",
                      "Also write every traced input to this CSV file (one row per event, nanosecond times); empty "
                      "disables it. Diagnostic");
REXCVAR_DECLARE(bool, edf_native_vsync);

namespace edf::latency {
namespace {

constexpr int64_t kReportPeriodNs = 5'000'000'000;

struct Trace {
  std::mutex mutex;
  Tracker tracker;
  UiBlockAccumulator ui_block;
  std::vector<Sample> window;
  int64_t window_begin = 0;
  int64_t last_dxgi_photon = 0;  // DXGI statistics are preferred to the waitable estimate
  std::atomic<uint64_t> tick{0};
  std::ofstream csv;
  bool csv_opened = false;
  std::atomic<bool> was_enabled{false};
};
Trace& Get() {
  static Trace* trace = new Trace;  // leaked: guest threads may report during static destruction
  return *trace;
}

void WriteCsv(Trace& trace, const std::vector<Sample>& samples) {
  if (!trace.csv_opened) {
    trace.csv_opened = true;
    const std::string path = REXCVAR_GET(edf_native_input_latency_csv);
    if (path.empty()) return;
    trace.csv.open(path, std::ios::out | std::ios::trunc);
    if (!trace.csv) {
      REXLOG_WARN("Input latency: cannot open {}", path);
      return;
    }
    trace.csv << "kind,input_ns,tick,tick_ns,record_ns,sequence,submit_ns,acquire_ns,present_id,present_ns,photon_ns,"
                 "photon_estimated,low_latency,vsync\n";
  }
  if (!trace.csv.is_open()) return;
  const int low = LowLatency() ? 1 : 0, vsync = REXCVAR_GET(edf_native_vsync) ? 1 : 0;
  for (const Sample& s : samples)
    trace.csv << kInputKindNames[size_t(s.kind)] << ',' << s.input << ',' << s.tick << ',' << s.tick_time << ','
              << s.record << ',' << s.sequence << ',' << s.submit << ',' << s.acquire << ',' << s.present_id << ','
              << s.present << ',' << s.photon << ',' << (s.photon_estimated ? 1 : 0) << ',' << low << ',' << vsync
              << '\n';
  trace.csv.flush();
}

// Caller holds the mutex. Collects finished samples and logs a window every 5 s.
void Collect(Trace& trace, int64_t now) {
  auto done = trace.tracker.TakeCompleted();
  if (!done.empty()) {
    WriteCsv(trace, done);
    trace.window.insert(trace.window.end(), done.begin(), done.end());
  }
  if (!trace.window_begin) trace.window_begin = now;
  const int64_t span = now - trace.window_begin;
  if (span < kReportPeriodNs) return;
  size_t estimated = 0;
  for (const Sample& s : trace.window) estimated += s.photon_estimated ? 1 : 0;
  const bool low = LowLatency(), vsync = REXCVAR_GET(edf_native_vsync);
  for (size_t kind = 0; kind < size_t(InputKind::kCount); ++kind) {
    bool any = false;
    for (const Sample& s : trace.window) any = any || size_t(s.kind) == kind;
    if (!any && kind != 0) continue;  // mouse is always reported, so an idle window shows up
    REXLOG_INFO("{} span_ms={:.0f} in_flight={}",
                FormatReport(InputKind(kind), trace.window, estimated, trace.ui_block.BlockedPercent(span),
                             trace.ui_block.ExpectedDelayMs(span), low, vsync, trace.tracker.expired(),
                             trace.tracker.discarded(), trace.tracker.overflow()),
                span / 1e6, trace.tracker.in_flight());
  }
  trace.window.clear();
  trace.tracker.ResetCounters();
  trace.ui_block.Reset();
  trace.window_begin = now;
}

// Locks the trace when it is on. Turning it off drops everything in flight, so turning it
// back on later starts clean instead of pairing old inputs with new frames.
template <class F>
void WithTrace(F&& f) {
  if (!Enabled()) {
    auto& trace = Get();
    if (trace.was_enabled.load(std::memory_order_relaxed)) {
      std::lock_guard lock(trace.mutex);
      if (trace.was_enabled.load(std::memory_order_relaxed)) {
        trace.tracker = Tracker{};
        trace.window.clear();
        trace.ui_block.Reset();
        trace.window_begin = 0;
        trace.last_dxgi_photon = 0;
        trace.was_enabled.store(false, std::memory_order_relaxed);
      }
    }
    return;
  }
  auto& trace = Get();
  std::lock_guard lock(trace.mutex);
  trace.was_enabled.store(true, std::memory_order_relaxed);
  f(trace);
}

}  // namespace

bool LowLatency() { return REXCVAR_GET(edf_low_latency); }
bool Enabled() { return REXCVAR_GET(edf_native_input_latency_trace); }
int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void OnInput(InputKind kind) {
  WithTrace([&](Trace& trace) { trace.tracker.Input(kind, NowNs()); });
}
void OnInputConsumed() {
  WithTrace([&](Trace& trace) {
    trace.tracker.Consumed(NowNs(), trace.tick.load(std::memory_order_relaxed));
  });
}
void OnInputDiscarded() {
  WithTrace([&](Trace& trace) { trace.tracker.Discarded(NowNs()); });
}
void OnHeartbeat(uint64_t tick) {
  if (Enabled()) Get().tick.store(tick, std::memory_order_relaxed);
}
void OnFrameRecorded(uint64_t tick) {
  WithTrace([&](Trace& trace) { trace.tracker.FrameRecorded(tick, NowNs()); });
}
void OnFrameSubmitted(uint64_t sequence) {
  WithTrace([&](Trace& trace) { trace.tracker.FrameSubmitted(sequence, NowNs()); });
}
void OnFrameAcquired(uint64_t sequence) {
  WithTrace([&](Trace& trace) { trace.tracker.FrameAcquired(sequence, NowNs()); });
}
void OnFramePresented(uint64_t present_id, int64_t paint_begin, int64_t present_end) {
  WithTrace([&](Trace& trace) {
    trace.ui_block.Add(paint_begin, present_end);
    trace.tracker.FramePresented(present_id, present_end);
    Collect(trace, present_end);
  });
}
void OnPhotonQpc(uint64_t present_id, int64_t sync_qpc) {
  WithTrace([&](Trace& trace) {
#ifdef _WIN32
    static const int64_t frequency = [] {
      LARGE_INTEGER value{};
      QueryPerformanceFrequency(&value);
      return int64_t(value.QuadPart);
    }();
    const int64_t t = QpcToNs(sync_qpc, frequency);
#else
    const int64_t t = sync_qpc;
#endif
    trace.tracker.Photon(present_id, t);
    trace.last_dxgi_photon = NowNs();
    Collect(trace, trace.last_dxgi_photon);
  });
}
void OnDisplaySignal(int64_t t) {
  WithTrace([&](Trace& trace) {
    if (trace.last_dxgi_photon && t - trace.last_dxgi_photon < 1'000'000'000) return;
    trace.tracker.DisplaySignal(t);
    Collect(trace, NowNs());
  });
}

namespace {
struct Refresh {
  std::mutex mutex;
  RefreshPeriodEstimator estimator;
  std::atomic<int64_t> period{0};
};
Refresh& GetRefresh() {
  static Refresh* refresh = new Refresh;
  return *refresh;
}
}  // namespace
void OnDisplayFlip(int64_t t) {
  auto& refresh = GetRefresh();
  {
    std::lock_guard lock(refresh.mutex);
    refresh.estimator.Flip(t);
    refresh.period.store(refresh.estimator.PeriodNs(), std::memory_order_relaxed);
  }
  OnDisplaySignal(t);
}
int64_t DisplayRefreshNs() { return GetRefresh().period.load(std::memory_order_relaxed); }

}  // namespace edf::latency
