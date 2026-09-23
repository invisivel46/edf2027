#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace edf::native {
// Lock-free "may this guest address be tracked?" test for hot guest hooks
// (821A1628/821A1678 list links, visibility and world updates), so that the
// ones naming nothing a bridge structure tracks skip the bridge lock.
//
// Contract: Add(a) before (or under the same lock as) the structure starts
// tracking a; then MayContain(a) is true for every later query (no false
// negatives). Bits are never cleared, so an address the structure stopped
// tracking, or a colliding one, answers true and takes the locked path as
// before: false positives cost only the lock. Two bits per address in a
// 4 Mi-bit table (512 KiB): ~0.2% false positives at 100k distinct addresses.
class NativeAddressFilter {
 public:
  static constexpr size_t kBits=size_t(1)<<22;
  void Add(uint32_t address) {
    for(const auto bit:Bits(address)) words_[bit>>6].fetch_or(uint64_t(1)<<(bit&63),std::memory_order_release);
  }
  bool MayContain(uint32_t address) const {
    for(const auto bit:Bits(address))
      if(!(words_[bit>>6].load(std::memory_order_acquire)&(uint64_t(1)<<(bit&63)))) return false;
    return true;
  }
  static std::array<uint32_t,2> Bits(uint32_t address) {
    const auto mixed=(uint64_t(address)+1)*0x9E3779B97F4A7C15ull;
    return {uint32_t(mixed>>42),uint32_t((mixed>>10)&(kBits-1))};
  }
 private:
  std::array<std::atomic<uint64_t>,kBits/64> words_{};
};
static_assert((NativeAddressFilter::kBits&(NativeAddressFilter::kBits-1))==0 && NativeAddressFilter::kBits==(size_t(1)<<22),
  "the filter's first index takes the top 22 bits of the mix");
}
