#include "native_graphics/native_capture_policy.h"
#include <iostream>
#include <limits>

int main() {
  using edf::native::ShouldCaptureNativeOutput;
  int failures = 0;
  auto check = [&](bool ok) { if (!ok) ++failures; };
  for (uint64_t frame = 0; frame < 2000; ++frame) {
    const bool milestone = (frame >= 1 && frame <= 3) || frame == 30 || frame == 60 ||
                           frame == 120 || frame == 600 || frame == 1800;
    check(ShouldCaptureNativeOutput(frame, 0, 32, 0, 0) == milestone);
    check(ShouldCaptureNativeOutput(frame, 0, 32, -1, -1) == milestone);
    check(ShouldCaptureNativeOutput(frame, 0, 32, 300, 0) ==
          (milestone || (frame && frame % 300 == 0)));
    check(!ShouldCaptureNativeOutput(frame, 0, 0, 1, 600));
    check(!ShouldCaptureNativeOutput(frame, 0, -1, 1, 600));
    check(!ShouldCaptureNativeOutput(frame, 0, 32, 0, 600));
  }
  uint32_t captured = 0;
  for (uint64_t frame = 1; frame < 2000; ++frame) {
    const bool selected = ShouldCaptureNativeOutput(frame, captured, 64, 1, 600);
    check(selected == (frame >= 600 && frame < 664));
    if (selected) ++captured;
  }
  check(captured == 64);
  check(ShouldCaptureNativeOutput(607, 0, 32, 7, 600));
  check(!ShouldCaptureNativeOutput(606, 0, 32, 7, 600));
  check(ShouldCaptureNativeOutput(std::numeric_limits<uint64_t>::max(), 127, 1000, 1, 600));
  check(!ShouldCaptureNativeOutput(600, 128, 1000, 1, 600));
  // Output frame counting: guest frames by indexed draws since scene begin, a
  // full native frame (no guest indexed draws) by having drawn the scene.
  using edf::native::NativeOutputFrameCounts;
  using edf::native::NativeSceneDrew;
  check(NativeOutputFrameCounts(true, 10, 4, false));
  check(!NativeOutputFrameCounts(true, 4, 4, false));   // Menus/loading: no 3D, not counted.
  check(NativeOutputFrameCounts(true, 4, 4, true));     // Full frame with no guest indexed draws.
  check(!NativeOutputFrameCounts(false, 4, 4, true));   // An invalid output never counts.
  check(!NativeOutputFrameCounts(false, 10, 4, false));
  check(NativeSceneDrew(5, 4, false) && NativeSceneDrew(4, 4, true) && !NativeSceneDrew(4, 4, false));
  // A run of full frames advances the counter as guest frames do.
  {
    uint64_t frames = 0;
    for (int i = 0; i < 5; ++i) if (NativeOutputFrameCounts(true, 100, 100, true)) ++frames;
    check(frames == 5);
  }
  if (failures) std::cerr << failures << " capture policy failures\n";
  return failures ? 1 : 0;
}
