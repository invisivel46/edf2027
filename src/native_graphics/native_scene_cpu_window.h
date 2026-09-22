#pragma once
#include "guest_block.h"
#include <atomic>
#include <bit>

namespace edf::native {
// Short-lived access to CPU scene data during pure native traversal. Page
// commitment/protection is checked on admission. No view may survive a guest
// callback that can allocate, free or change protection: call Invalidate first,
// or pass `epoch` (a per-thread guest call count) and every admission is
// dropped on the first access after it moves. Contents are never cached, and
// this is not a GPU resource/lifetime cache.
template<class Reader>
class NativeSceneCpuWindow {
 public:
  explicit NativeSceneCpuWindow(const Reader& reader,const uint64_t* epoch=nullptr)
      :reader_(reader),epoch_(epoch),seen_(epoch?*epoch:0) {}
  void Invalidate() const { pages_={}; }
  uint32_t Add(uint32_t address,uint32_t offset) const { return reader_.Add(address,offset); }
  const uint8_t* Bytes(uint32_t address,size_t size) const { return Access(address,size,false); }
  const uint8_t* WritableBytes(uint32_t address,size_t size,size_t alignment) const {
    if(alignment!=4 || (address&3)) return reader_.WritableBytes(address,size,alignment);
    return Access(address,size,true);
  }
  uint32_t Word(uint32_t address) const { return GuestBlockWord(Bytes(address,4)); }
  // As GuestReader::StoreWord: one interlocked big-endian word store.
  void StoreWord(uint32_t address,uint32_t value) const {
    auto* data=const_cast<uint8_t*>(WritableBytes(address,4,4));
    std::atomic_ref<uint32_t>(*reinterpret_cast<uint32_t*>(data)).exchange(std::byteswap(value));
  }
 private:
  struct Page { uint32_t address=0; const uint8_t* data=nullptr; bool writable=false; };
  static constexpr uint32_t kPageSize=4096;
  const uint8_t* Access(uint32_t address,size_t size,bool writable) const {
    if(epoch_ && *epoch_!=seen_) { pages_={}; seen_=*epoch_; }
    const uint32_t page=address&~(kPageSize-1),offset=address&(kPageSize-1);
    // Crossing pages and the null page use the backing validator directly.
    if(!size || !page || size>kPageSize-offset)
      return writable?reader_.WritableBytes(address,size,4):reader_.Bytes(address,size);
    // Direct-mapped by page number: a walk streams through owner pages while
    // the context, camera, stack and vtable pages stay admitted.
    auto& cached=pages_[(page/kPageSize)%pages_.size()];
    if(cached.data && cached.address==page) {
      if(writable && !cached.writable) {
        cached.data=reader_.WritableBytes(page,kPageSize,4); cached.writable=true;
      }
      return cached.data+offset;
    }
    const auto* data=writable?reader_.WritableBytes(page,kPageSize,4):reader_.Bytes(page,kPageSize);
    cached={page,data,writable};
    return data+offset;
  }
  const Reader& reader_;
  const uint64_t* epoch_=nullptr;
  mutable uint64_t seen_=0;
  mutable std::array<Page,64> pages_{};
};
}
