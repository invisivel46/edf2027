#pragma once
#include "guest_block.h"

namespace edf::native {
// Short-lived access to CPU scene data during pure native traversal. Page
// commitment/protection is checked on admission. No view may survive a guest
// callback that can allocate, free or change protection: call Invalidate first.
// Contents are never cached, and this is not a GPU resource/lifetime cache.
template<class Reader>
class NativeSceneCpuWindow {
 public:
  explicit NativeSceneCpuWindow(const Reader& reader):reader_(reader) {}
  void Invalidate() { pages_={}; next_=0; }
  uint32_t Add(uint32_t address,uint32_t offset) const { return reader_.Add(address,offset); }
  const uint8_t* Bytes(uint32_t address,size_t size) const { return Access(address,size,false); }
  const uint8_t* WritableBytes(uint32_t address,size_t size,size_t alignment) const {
    if(alignment!=4 || (address&3)) return reader_.WritableBytes(address,size,alignment);
    return Access(address,size,true);
  }
  uint32_t Word(uint32_t address) const { return GuestBlockWord(Bytes(address,4)); }
 private:
  struct Page { uint32_t address=0; const uint8_t* data=nullptr; bool writable=false; };
  const uint8_t* Access(uint32_t address,size_t size,bool writable) const {
    constexpr uint32_t page_size=4096;
    const uint32_t page=address&~(page_size-1),offset=address&(page_size-1);
    // Crossing pages and the null page use the backing validator directly.
    if(!size || !page || size>page_size-offset)
      return writable?reader_.WritableBytes(address,size,4):reader_.Bytes(address,size);
    for(auto& cached:pages_) if(cached.address==page && cached.data) {
      if(writable && !cached.writable) {
        cached.data=reader_.WritableBytes(page,page_size,4); cached.writable=true;
      }
      return cached.data+offset;
    }
    const auto* data=writable?reader_.WritableBytes(page,page_size,4):reader_.Bytes(page,page_size);
    pages_[next_]={page,data,writable}; next_=(next_+1)%pages_.size();
    return data+offset;
  }
  const Reader& reader_;
  mutable std::array<Page,16> pages_{};
  mutable size_t next_=0;
};
}
