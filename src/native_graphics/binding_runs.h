#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace edf::native {
// Sorted slot map. Emit only adjacent declared slots; gaps must retain their
// context state. Explicit null values are emitted, never treated as gaps.
template<class Pointer, size_t Limit, class Slots, class Get, class Emit>
void EmitBindingRuns(const Slots& slots, Get get, Emit emit) {
  static_assert(Limit>0);
  for(const auto& [slot,value]:slots)
    if(slot>=Limit) throw std::runtime_error("native binding slot exceeds stage limit");
  std::array<Pointer,Limit> values;
  auto it=slots.begin();
  while(it!=slots.end()) {
    const uint32_t first=it->first;
    size_t count=0;
    do {
      values[count++]=get(it->second);
      ++it;
    } while(it!=slots.end() && it->first==first+count);
    emit(first,static_cast<uint32_t>(count),values.data());
  }
}
}
