#pragma once
#include <rex/system/xmemory.h>
#include <rex/thread/mutex.h>
#include <algorithm>
#include <stdexcept>
#include <optional>

namespace edf::native {
struct NativePhysicalWriteExtent { uint32_t address,bytes; bool all=false; };
// Mapping for a completed write, not permission to dereference guest memory.
// Unknown/nonphysical writers are outside this physical registry. A malformed
// extent starting in a physical heap must invalidate conservatively, not vanish.
inline std::optional<NativePhysicalWriteExtent> MapCompletedNativePhysicalWrite(
    rex::memory::Memory& memory,uint32_t destination,uint32_t bytes) {
  if(!bytes || destination<0xa0000000u) return std::nullopt;
  auto* heap=memory.LookupHeap(destination);
  if(!heap || heap->heap_type()!=rex::memory::HeapType::kGuestPhysical) return std::nullopt;
  const uint64_t end=uint64_t(destination)+bytes;
  const auto physical=memory.GetPhysicalAddress(destination);
  if(end>0x100000000ull || heap!=memory.LookupHeap(uint32_t(end-1)) ||
     uint64_t(physical)+bytes>0x20000000ull ||
     memory.GetPhysicalAddress(uint32_t(end-1))!=uint64_t(physical)+bytes-1)
    return NativePhysicalWriteExtent{UINT32_MAX,1,true};
  return NativePhysicalWriteExtent{physical,bytes,false};
}
// Completed native providers may bypass all virtual aliases. Deliver completion
// to any armed SDK alias, not just the virtual address passed to the provider.
// Call after writing; this is not a lock against an in-progress native write.
inline void NotifyPhysicalProviderWrite(rex::memory::Memory& memory,uint32_t physical,uint32_t bytes) {
  if(!bytes) return;
  if(physical>=0x20000000u || bytes>0x20000000u-physical)
    throw std::runtime_error("invalid completed physical provider extent");
  auto lock=rex::thread::global_critical_region::AcquireDirect();
  for(uint32_t base:{0xa0000000u,0xc0000000u,0xe0000000u}) {
    auto* heap=memory.LookupHeap(base);
    const uint64_t offset=memory.GetPhysicalAddress(base);
    const auto first=(std::max)(uint64_t(physical),offset);
    const auto end=(std::min)(uint64_t(physical)+bytes,offset+heap->heap_size());
    if(first>=end) continue;
    memory.TriggerPhysicalMemoryCallbacks(rex::thread::global_critical_region::AcquireDirect(),
      base+uint32_t(first-offset),uint32_t(end-first),true,true);
  }
}
}
