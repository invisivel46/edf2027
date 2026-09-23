#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace edf {

struct FrameStats {
  std::atomic<double> fps{0.0};
  std::atomic<double> average_ms{0.0};
  std::atomic<double> minimum_ms{0.0};
  std::atomic<double> maximum_ms{0.0};
  std::atomic<double> low_1_percent_ms{0.0};
  std::atomic<uint32_t> total_frames{0};

  // Performance overlay (perf_overlay.h), fed only while it is shown. The swap hook
  // (input_hooks.cpp) writes one present-to-present time per frame into the ring and the
  // engine thread's busy time for that frame: the interval minus the heartbeat's wait for
  // the next simulation tick (engine_wait_ns, added by guest_shader_bridge.cpp) and the
  // render cap's wait.
  static constexpr uint32_t kHistory = 240;
  std::array<std::atomic<float>, kHistory> frame_ms{};
  std::atomic<uint32_t> frame_count{0};  // frames written; the newest is at (count - 1) % kHistory
  std::atomic<float> cpu_ms{0.0f};       // smoothed
  std::atomic<uint64_t> engine_wait_ns{0};
  // GPU frame time from edf_native_gpu_timings' report window (average over its frames);
  // gpu_windows counts reports, so 0 means no GPU number yet.
  std::atomic<float> gpu_ms{0.0f};
  std::atomic<uint64_t> gpu_windows{0};
};

FrameStats& CurrentFrameStats();

}  // namespace edf
