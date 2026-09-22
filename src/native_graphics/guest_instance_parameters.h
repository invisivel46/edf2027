#pragma once
#include "guest_block.h"
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace edf::native {
struct InstanceParameter {
  uint32_t data, first, count;
  bool operator==(const InstanceParameter&) const=default;
};

// Retail 821D9600: the vector lives at instance+12, with begin/end at +4/+8.
// Reader must validate guest addresses and return big-endian words.
// Into a caller-owned vector, so the per-draw caller can reuse one: this ran
// two thousand times a frame with an allocation each.
template<class Reader>
void ReadInstanceParameters(const Reader& reader, uint32_t instance, std::vector<InstanceParameter>& result) {
  result.clear();
  const auto range=ReadGuestWords<2>(reader,reader.Add(instance,16));
  const auto begin=range[0],end=range[1];
  if(end<begin || (end-begin)%12 || (end-begin)/12>4096)
    throw std::runtime_error("invalid per-instance vertex parameter list");
  if(begin==end) return;
  const auto* records=reader.Bytes(begin,end-begin);
  result.reserve((end-begin)/12);
  for(size_t offset=0;offset<end-begin;offset+=12) {
    const auto* record=records+offset;
    InstanceParameter parameter{GuestBlockWord(record),GuestBlockWord(record+4),GuestBlockWord(record+8)};
    if(parameter.first>256 || parameter.count>256-parameter.first)
      throw std::runtime_error("invalid instance register range");
    result.push_back(parameter);
  }
}
template<class Reader>
std::vector<InstanceParameter> ReadInstanceParameters(const Reader& reader, uint32_t instance) {
  std::vector<InstanceParameter> result;
  ReadInstanceParameters(reader,instance,result);
  return result;
}
}  // namespace edf::native
