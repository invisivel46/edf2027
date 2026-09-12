#pragma once
#include <cstddef>
#include <cstdint>

namespace edf::native {
struct GuestReadableRegion {
  uint64_t base = 0, size = 0;
  bool committed = false, readable = false;
};

// Query fresh metadata for every region intersecting the request. Never cache
// these results across guest calls. A false result requests OS validation.
template<class Query>
bool GuestRangeCommittedReadable(uint32_t address, size_t size, Query query) {
  constexpr uint64_t limit = 0x100000000ull;
  if (!address || !size || size > limit - address) return false;
  const uint64_t end = uint64_t(address) + size;
  uint64_t cursor = address;
  while (cursor < end) {
    const auto region = query(static_cast<uint32_t>(cursor));
    if (!region.committed || !region.readable || region.base > cursor ||
        region.base >= limit || !region.size || region.size > limit - region.base)
      return false;
    const uint64_t next = region.base + region.size;
    if (next <= cursor) return false;
    cursor = next;
  }
  return true;
}
}  // namespace edf::native
