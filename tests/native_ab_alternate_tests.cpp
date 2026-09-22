#include "native_graphics/native_ab_alternate.h"
#include <iostream>

int main() {
  using edf::native::AbSide;
  int failures = 0;
  auto check = [&](bool ok) { if (!ok) ++failures; };
  for (uint64_t frame = 0; frame < 100; ++frame) {
    check(AbSide(frame, 0, 0));
    check(AbSide(frame, 600, -1));
    check(AbSide(frame, 0, 1) == (frame % 2 == 1));
    check(AbSide(frame, -5, 1) == (frame % 2 == 1));
    check(AbSide(frame, 0, 3) == ((frame / 3) % 2 == 1));
  }
  check(!AbSide(599, 600, 1));
  check(!AbSide(600, 600, 1));
  check(AbSide(601, 600, 1));
  check(!AbSide(601, 600, 2));
  check(AbSide(602, 600, 2) && AbSide(603, 600, 2) && !AbSide(604, 600, 2));
  check(edf::native::NativeAbNativeSide());
  {
    edf::native::NativeAbSideLatch outer(false);
    check(!edf::native::NativeAbNativeSide());
    { edf::native::NativeAbSideLatch inner(true); check(edf::native::NativeAbNativeSide()); }
    check(!edf::native::NativeAbNativeSide());
  }
  check(edf::native::NativeAbNativeSide());
  if (failures) std::cerr << failures << " native A/B alternate checks failed\n";
  return failures ? 1 : 0;
}
