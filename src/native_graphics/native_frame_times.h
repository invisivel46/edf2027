#pragma once
#include <atomic>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace edf::native {
// Present-to-present frame times (edf_native_frame_times): the distribution
// over a window, and the frames that stood out, each named by what else
// happened in it.
//
// A frame is a spike when it took longer than spike_ms, or longer than
// spike_ratio times the median of the median_window frames before it (once
// there are median_minimum of them). The median excludes the frame itself, so
// one long frame cannot raise its own bar.
//
// A window closes after report_frames frames or report_ms of frame time,
// whichever comes first; frame time rather than a clock, so the recorder is
// deterministic under test and a loading screen at 5 fps still reports every
// few seconds. The window's percentiles are nearest-rank over its samples; its
// histogram (NativeFrameTimeBucket) is what lets a log reader merge windows
// into percentiles over a whole mission phase, which percentiles cannot do.
struct NativeFrameTimeWindow {
  uint64_t frames=0,spikes=0;
  double span_ms=0,mean_ms=0,p50_ms=0,p90_ms=0,p99_ms=0,p999_ms=0,max_ms=0;
  std::vector<uint32_t> histogram;  // Counts per NativeFrameTimeBucket.
};
// Histogram buckets: 0.25 ms below 50 ms, 1 ms to 100 ms, 10 ms to 1 s, then
// one overflow bucket.
inline constexpr size_t kNativeFrameTimeBuckets=200+50+90+1;
size_t NativeFrameTimeBucket(double ms);
double NativeFrameTimeBucketLower(size_t bucket);
// "8.00:120,8.25:300": lower bound and count of each non-empty bucket.
std::string FormatNativeFrameTimeHistogram(std::span<const uint32_t> histogram);

class NativeFrameTimeRecorder {
 public:
  struct Options {
    uint32_t report_frames=600;
    double report_ms=5000;
    uint32_t median_window=120,median_minimum=30;
    double spike_ratio=2.0,spike_ms=25.0;
  };
  struct Sample {
    uint64_t frame=0;  // 1-based count of recorded frames.
    double ms=0,median_ms=0;  // median_ms is 0 until median_minimum frames.
    bool over_limit=false,over_median=false;
    bool spike() const { return over_limit || over_median; }
    const char* reason() const {
      return over_limit && over_median ? "limit+median" : over_limit ? "limit"
           : over_median ? "median" : "none";
    }
  };
  NativeFrameTimeRecorder() : NativeFrameTimeRecorder(Options{}) {}
  explicit NativeFrameTimeRecorder(Options options);
  Sample Record(double ms);
  // Moves the closed window into `out` and starts the next; false while the
  // window is still open (or, with `force`, empty).
  bool TakeReport(NativeFrameTimeWindow& out,bool force=false);
  uint64_t frames() const { return frames_; }

 private:
  Options options_;
  uint64_t frames_=0;
  std::vector<double> recent_,scratch_;  // Ring of the last median_window frames.
  size_t recent_next_=0;
  std::vector<double> window_;
  double window_ms_=0;
  uint64_t window_spikes_=0;
  std::vector<uint32_t> histogram_;
};

// Counters that attribute a spike: sampled once per present, each spike line
// names how much every one moved during that frame. The names are fixed for a
// run, so every spike line has the same fields in the same order.
struct NativeFrameCounter {
  const char* name;
  uint64_t value;
};
class NativeFrameCounterDeltas {
 public:
  // Takes this frame's values; the deltas are against the previous call (all
  // zero on the first). A counter that went backwards (a backend was
  // recreated) reads as zero.
  void Update(std::span<const NativeFrameCounter> counters);
  // The last Update's deltas as "name=delta name=delta".
  std::string Describe() const;
  std::span<const uint64_t> deltas() const { return deltas_; }
 private:
  std::vector<const char*> names_;
  std::vector<uint64_t> previous_,deltas_;
};

// Process-wide event counts the bridge has no other count of, bumped where
// they happen and read by the frame-time recorder. Relaxed: they are
// attribution, not synchronization.
struct NativeFrameEventCounters {
  std::atomic<uint64_t> shader_compiles{0};  // CompileNativeShader calls.
  std::atomic<uint64_t> pass_declines{0};    // Full-frame passes or items declined.
  std::atomic<uint64_t> post_fallbacks{0};   // Native post failed; guest finish stage ran.
};
inline NativeFrameEventCounters& FrameEventCounters() {
  static NativeFrameEventCounters counters;
  return counters;
}
}  // namespace edf::native
