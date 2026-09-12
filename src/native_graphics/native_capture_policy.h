#pragma once
#include <algorithm>
#include <cstdint>

namespace edf::native {
// A positive start selects only periodic captures, relative to that frame.
// This prevents startup milestone captures from exhausting a later burst.
inline bool ShouldCaptureNativeOutput(uint64_t frame, uint32_t attempted,
                                     int32_t limit, int32_t interval, int32_t start) {
  if (!frame || attempted >= uint32_t(std::clamp(limit, 0, 128))) return false;
  if (start > 0) {
    return frame >= uint32_t(start) && interval > 0 &&
           (frame - uint32_t(start)) % uint32_t(interval) == 0;
  }
  return frame <= 3 || frame == 30 || frame == 60 || frame == 120 ||
         frame == 600 || frame == 1800 ||
         (interval > 0 && frame % uint32_t(interval) == 0);
}
}
