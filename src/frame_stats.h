#pragma once

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
};

FrameStats& CurrentFrameStats();

}  // namespace edf
