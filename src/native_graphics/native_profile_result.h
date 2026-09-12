#pragma once
#include <algorithm>
#include <bit>
#include <optional>

namespace edf::native {
inline double NativeProfileResult(std::optional<double> ratio,float scale,float minimum) {
  if(!ratio) return double(std::bit_cast<float>(0x7fffffffu));
  const float value=float(*ratio)*scale;
  return double((std::max)(minimum,(std::min)(scale,value)));
}
}
