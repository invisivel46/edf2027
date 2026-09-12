#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <span>
#include <stdexcept>
#include <cstdint>
#include <string_view>
namespace edf::native {
// Classify once when publishing a native parameter binding plan.
inline bool IsNativeCanvasXY(uint64_t source,std::string_view entry,
                             std::string_view parameter,size_t group) {
  return group<2 && source==0xc885203e230fe745ull &&
    (entry=="VS_2D" || entry=="VS_2DTex") &&
    (parameter=="_g_DX2DScale" || parameter=="_g_DX2DOffset");
}
// Host-owned copy: scale clip-space XY without changing Z/W or guest memory.
inline std::array<uint8_t,16> ScaleNativeCanvasXY(std::span<const uint8_t> input,float x,float y) {
  if(input.size()!=16 || !std::isfinite(x) || !std::isfinite(y) || x<=0 || y<=0)
    throw std::runtime_error("invalid native canvas constant");
  std::array<uint8_t,16> output{};
  for(size_t i=0;i<16;++i) output[i]=input[i];
  for(size_t lane=0;lane<2;++lane) {
    const float scale=lane?y:x;
    if(scale==1) continue;
    uint32_t word=0;
    for(size_t b=0;b<4;++b) word=(word<<8)|input[lane*4+b];
    word=std::bit_cast<uint32_t>(std::bit_cast<float>(word)*scale);
    for(size_t b=0;b<4;++b) output[lane*4+b]=uint8_t(word>>(24-b*8));
  }
  return output;
}
}
