#pragma once
#include <bit>
#include <cstdint>

namespace edf::native {
// Equivalent to count repetitions of cursor=(cursor+1)&mask, including masks
// that are not 2^n-1. No packet storage or per-word loop is needed.
constexpr uint32_t AdvanceNativeRingCursor(uint32_t cursor,uint32_t count,uint32_t mask) {
  if(!count) return cursor;
  const auto bits=std::countr_one(mask);
  if(bits==32) return cursor+count;
  const uint32_t low=(uint32_t(1)<<bits)-1u;
  const uint32_t first=(cursor+1u)&mask;
  return (first&~low)|((first+count-1u)&low);
}
}
