#pragma once
#include <array>
#include <cstdint>

namespace edf::native {
// One shader stage in one recording scope. Reflection order may change when
// switching shaders; the register is the identity of the binding.
//
// What was sent is remembered as (owner, version): the binding object the
// image came from and the version its bytes had, which the object counts
// itself. Comparing the bytes was the previous test, a memcmp per register
// per draw; the version answers the same question - has this changed since it
// was sent - for the price of two integers.
class NativeConstantCache {
 public:
  bool Matches(uint32_t slot,const void* owner,uint64_t version) const {
    if(slot>=slots_.size()) return false;
    const auto& sent=slots_[slot];
    return sent.valid && sent.owner==owner && sent.version==version;
  }
  void Store(uint32_t slot,const void* owner,uint64_t version) {
    slots_.at(slot)={owner,version,true};
    complete_={};
  }
  // Valid only after every reflected buffer has been checked/sent. Any
  // individual binding replaces this certificate, including shader switches.
  bool MatchesComplete(const void* owner,uint64_t version) const {
    return complete_.valid && complete_.owner==owner && complete_.version==version;
  }
  void StoreComplete(const void* owner,uint64_t version) { complete_={owner,version,true}; }
 private:
  struct Sent { const void* owner=nullptr; uint64_t version=0; bool valid=false; };
  // Both recorders support the fixed b0..b13 constant-buffer register range.
  std::array<Sent,14> slots_{};
  Sent complete_{};
};
}
