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
  if (failures) std::cerr << failures << " capture policy failures\n";
  return failures ? 1 : 0;
}
