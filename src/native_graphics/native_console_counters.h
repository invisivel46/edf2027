#pragma once
#include <atomic>
#include <cstdint>

namespace edf::native {
// The latest full frame's renderer counts, for the console's "stats" (src/console).
// Written by the passes once per frame (relaxed stores), read whenever asked; never
// used for rendering decisions.
struct NativeConsoleCounters {
  std::atomic<uint32_t> effects_visited{0};  // effect objects the Effects pass walked
  std::atomic<uint32_t> effects_filed{0};    // of those, filed for drawing (after culling)
  std::atomic<uint32_t> effects_drawn{0};    // immediate-mode effect draws recorded
  std::atomic<uint64_t> effects_frames{0};
};
inline NativeConsoleCounters& ConsoleCounters() {
  static NativeConsoleCounters counters;
  return counters;
}
}  // namespace edf::native
