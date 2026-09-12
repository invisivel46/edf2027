#pragma once
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace edf::native {
// Sequential, non-indexed strip -> big-endian uint16 triangle-list indices.
// Preserve alternating winding and every triangle, including degenerate ones.
inline std::vector<uint8_t> TriangleStripIndices16(uint32_t vertices) {
  if(vertices<3 || vertices>65536) throw std::runtime_error("invalid native triangle strip size");
  std::vector<uint8_t> result; result.reserve(size_t(vertices-2)*6);
  auto emit=[&](uint32_t index){result.push_back(uint8_t(index>>8));result.push_back(uint8_t(index));};
  for(uint32_t i=0;i<vertices-2;++i) {
    emit(i+(i&1)); emit(i+1-(i&1)); emit(i+2);
  }
  return result;
}
}  // namespace edf::native
