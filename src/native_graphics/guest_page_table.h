#pragma once
#include <cstddef>
#include <cstdint>

namespace edf::native {
struct GuestPageState {
  bool committed=false;
  uint32_t protect=0;  // rex::memory::MemoryProtectFlag bits
};

// Admits [address,address+size) only if it lies inside one heap and every page
// it touches is committed with all `required` protect bits. `page(index)`
// returns the current state of page `index` (heap-relative) straight from the
// authoritative page table; nothing is remembered between calls, so there is
// nothing to invalidate. Cost is one lookup per page touched, independent of
// the size of the allocation the pages belong to. False requests the caller's
// slower validation, never a read.
template<class Page>
bool GuestPagesAdmit(uint32_t heap_base,uint64_t heap_size,uint32_t page_shift,uint64_t page_count,
                     uint32_t address,size_t size,uint32_t required,Page page) {
  if(!address || !size || page_shift>31 || size>0x100000000ull-address || address<heap_base) return false;
  const uint64_t first=uint64_t(address)-heap_base,end=first+size;
  if(end>heap_size) return false;
  const uint64_t last_page=(end-1)>>page_shift;
  if(last_page>=page_count) return false;
  for(uint64_t index=first>>page_shift;index<=last_page;++index) {
    const GuestPageState state=page(uint32_t(index));
    if(!state.committed || (state.protect&required)!=required) return false;
  }
  return true;
}
}  // namespace edf::native
