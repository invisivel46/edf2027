#pragma once
#include "guest_readable_range.h"
#include <rex/memory.h>

namespace edf::native {
inline bool GuestVirtualHeapCommittedWritable(rex::memory::BaseHeap& heap,uint32_t address,size_t size) {
  // Physical heaps can have host write watches not represented by allocation
  // protection. Only ordinary virtual CPU state may use this fast proof.
  if(heap.heap_type()!=rex::memory::HeapType::kGuestVirtual || !address || !size ||
     address<heap.heap_base() || size>0x100000000ull-address ||
     uint64_t(address)-heap.heap_base()+size>heap.heap_size() || !heap.page_size()) return false;
  return GuestRangeCommittedReadable(address,size,[&](uint32_t at) {
    const uint32_t page=heap.heap_base()+((at-heap.heap_base())/heap.page_size())*heap.page_size();
    rex::memory::HeapAllocationInfo info{};
    if(!heap.QueryRegionInfo(page,&info)) return GuestReadableRegion{};
    constexpr auto access=rex::memory::kMemoryProtectRead|rex::memory::kMemoryProtectWrite;
    return GuestReadableRegion{info.base_address,info.region_size,
      (info.state&rex::memory::kMemoryAllocationCommit)!=0,(info.protect&access)==access};
  });
}
inline bool GuestHeapCommittedReadable(rex::memory::BaseHeap& heap,uint32_t address,size_t size) {
  if(!address || !size || address<heap.heap_base() || size>0x100000000ull-address ||
     uint64_t(address)-heap.heap_base()+size>heap.heap_size() || !heap.page_size()) return false;
  return GuestRangeCommittedReadable(address,size,[&](uint32_t at) {
    // SDK region_size counts whole pages even for unaligned queries.
    const uint32_t page=heap.heap_base()+((at-heap.heap_base())/heap.page_size())*heap.page_size();
    rex::memory::HeapAllocationInfo info{};
    if(!heap.QueryRegionInfo(page,&info)) return GuestReadableRegion{};
    return GuestReadableRegion{info.base_address,info.region_size,
      (info.state&rex::memory::kMemoryAllocationCommit)!=0,
      (info.protect&rex::memory::kMemoryProtectRead)!=0};
  });
}
}  // namespace edf::native
