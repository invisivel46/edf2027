#pragma once
#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>

namespace edf::native {
struct NativePoolBlock { uint32_t address,bytes; };
// 821D3748 receives the chunk at r3 and the block iterator pair in r4.
// Its sole audited caller holds the pool critical section. Decode before the
// original marks the node free or coalesces it; no model wrapper is required.
template<class Reader> NativePoolBlock ReadNativePoolBlock(const Reader& reader,uint32_t chunk,uint64_t iterator) {
  const auto list=uint32_t(iterator>>32),node=uint32_t(iterator);
  if(!list || list!=reader.Add(chunk,8) || !node || node==reader.Word(reader.Add(list,4)))
    throw std::runtime_error("invalid native pool block iterator");
  const auto address=reader.Word(reader.Add(node,8)),bytes=reader.Word(reader.Add(node,12));
  if((bytes && !address) || uint64_t(address)+bytes>0x100000000ull)
    throw std::runtime_error("invalid native pool block extent");
  return {address,bytes};
}
// 821D3A40 entry r3 names the chunk-list descriptor (pool+28). The list
// sentinel is at +4; nodes link through +0 and hold backing address at +8.
// Collect before retail detaches the list. The caller owns list synchronization.
template<class Reader> std::vector<uint32_t> ReadNativePoolBackings(const Reader& reader,uint32_t list) {
  const auto sentinel=reader.Word(reader.Add(list,4));
  if(!sentinel) throw std::runtime_error("null native pool list sentinel");
  std::set<uint32_t> visited;
  std::vector<uint32_t> result;
  for(auto node=reader.Word(sentinel);node!=sentinel;node=reader.Word(node)) {
    if(!node || !visited.insert(node).second)
      throw std::runtime_error("invalid native pool backing list");
    const auto address=reader.Word(reader.Add(node,8));
    if(address) result.push_back(address);
  }
  return result;
}
}
