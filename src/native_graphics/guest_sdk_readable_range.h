#pragma once
#include "guest_page_table.h"
#include "guest_readable_range.h"
#include <rex/memory.h>
#include <atomic>
#include <bit>
#include <vector>

namespace edf::native {
namespace detail {
// BaseHeap::QueryRegionInfo takes heap_mutex_ and scans page entries to the end
// of the region (to the end of the heap for a free one) on every call. The page
// table it reads is the SDK's own record of commitment and protection, updated
// under that mutex by every Alloc/AllocFixed/AllocRange/Decommit/Release/
// Protect/Reset/Restore, whoever calls them (guest imports, SDK threads, the
// GPU and audio systems). Reading the entries for the pages a request touches
// gives the same answer as the query without the lock or the scan. The vector
// is sized once in BaseHeap::Initialize and never reallocated, so its storage
// is stable for the Memory's lifetime.
struct SdkHeapPages : rex::memory::BaseHeap {
  static const std::vector<rex::memory::PageEntry>& Table(const rex::memory::BaseHeap& heap) {
    return heap.*(&SdkHeapPages::page_table_);
  }
};
inline GuestPageState SdkPageState(const rex::memory::PageEntry& entry) {
  // Writers hold heap_mutex_; this unlocked load sees the entry before or after
  // any concurrent change, exactly as a locked query just before or after it
  // would. state and current_protect lie in the entry's first eight bytes
  // under both MSVC and Itanium bit-field layout (the SDK's Save/Restore rely
  // on that too).
  rex::memory::PageEntry copy{};
  copy.qword=std::atomic_ref<uint64_t>(const_cast<uint64_t&>(entry.qword)).load(std::memory_order_acquire);
  return {(copy.state&rex::memory::kMemoryAllocationCommit)!=0,copy.current_protect};
}
inline bool SdkHeapPagesAdmit(const rex::memory::BaseHeap& heap,uint32_t address,size_t size,uint32_t required) {
  const uint32_t page_size=heap.page_size();
  if(!page_size || (page_size&(page_size-1))) return false;
  const auto& table=SdkHeapPages::Table(heap);
  return GuestPagesAdmit(heap.heap_base(),heap.heap_size(),uint32_t(std::countr_zero(page_size)),table.size(),
    address,size,required,[&](uint32_t index) { return SdkPageState(table[index]); });
}
}  // namespace detail

inline bool GuestVirtualHeapCommittedWritable(rex::memory::BaseHeap& heap,uint32_t address,size_t size) {
  // Physical heaps can have host write watches not represented by allocation
  // protection. Only ordinary virtual CPU state may use this fast proof.
  if(heap.heap_type()!=rex::memory::HeapType::kGuestVirtual) return false;
  return detail::SdkHeapPagesAdmit(heap,address,size,rex::memory::kMemoryProtectRead|rex::memory::kMemoryProtectWrite);
}
inline bool GuestHeapCommittedReadable(rex::memory::BaseHeap& heap,uint32_t address,size_t size) {
  return detail::SdkHeapPagesAdmit(heap,address,size,rex::memory::kMemoryProtectRead);
}
}  // namespace edf::native
