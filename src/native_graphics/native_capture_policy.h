#pragma once
#include <algorithm>
#include <cstdint>

namespace edf::native {
// Whether the scene since its begin (8219C7A8) drew 3D content: guest indexed
// draws counted from its begin, or a full native frame (edf_native_full_frame)
// that recorded its passes on it, which issues no guest indexed draw. Scene
// captures and the post-input captures ask this.
inline bool NativeSceneDrew(uint64_t indexed_submitted,uint64_t scene_indexed_start,bool full_frame) {
  return indexed_submitted>scene_indexed_start || full_frame;
}
// Whether a published ordinary output is an indexed output frame: the frame
// number output captures (ShouldCaptureNativeOutput) and the A/B alternation
// (AbSide) count. A full frame counts exactly as a guest frame with indexed
// draws does, so both work the same in either mode.
inline bool NativeOutputFrameCounts(bool output_valid,uint64_t indexed_submitted,uint64_t scene_indexed_start,bool full_frame) {
  return output_valid && NativeSceneDrew(indexed_submitted,scene_indexed_start,full_frame);
}
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
