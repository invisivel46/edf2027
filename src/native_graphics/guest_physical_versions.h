#pragma once
#include <rex/system/xmemory.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>
#include "native_alias_watch.h"

namespace edf::native {
// Physical-buffer invalidation metadata, independent of any emulated GPU.
// Memory must outlive this object. Callbacks never acquire renderer locks.
// This tracks SDK notifications, NOT arbitrary host physical writes. A matching
// version is usable only with a separately established writer/lifetime contract.
// It also is not a lock against writes concurrent with a snapshot or GPU upload.
// Supplemental alias watches cover host-writable guest-unallocated aliases;
// direct host physical providers still require explicit notifications.
class GuestPhysicalVersions {
 public:
  static constexpr uint32_t kPageSize=4096,kPhysicalSize=0x20000000;
  explicit GuestPhysicalVersions(rex::memory::Memory& memory)
      : memory_(memory),pages_(std::make_unique<std::atomic<uint64_t>[]>(kPhysicalSize/kPageSize)),
        foreign_(std::make_unique<std::atomic<bool>[]>(kPhysicalSize/kPageSize)),
        aliases_(std::make_unique<NativeAliasWatch>(memory,
          [](void* context,uint32_t start,uint32_t length) {
            static_cast<GuestPhysicalVersions*>(context)->Invalidate(start,length);
          },this)) {
    handle_=memory_.RegisterPhysicalMemoryInvalidationCallback(
      [](void* context,uint32_t start,uint32_t length,bool) -> std::pair<uint32_t,uint32_t> {
        auto& self=*static_cast<GuestPhysicalVersions*>(context);
        self.Invalidate(start,length);
        // Do not ask the SDK to unwatch adjacent pages owned by other buffers.
        return {start,length};
      },this);
  }
  ~GuestPhysicalVersions() { aliases_.reset(); memory_.UnregisterPhysicalMemoryInvalidationCallback(handle_); }
  GuestPhysicalVersions(const GuestPhysicalVersions&)=delete;
  GuestPhysicalVersions& operator=(const GuestPhysicalVersions&)=delete;
  bool ClaimDrawingThread() {
    const auto current=CurrentThreadToken();
    uint64_t expected=0;
    return owner_.compare_exchange_strong(expected,current,std::memory_order_acq_rel) || expected==current;
  }
  bool HasForeignWrites(uint32_t physical,uint32_t length) const {
    Validate(physical,length);
    for(uint32_t page=physical/kPageSize;page<=(physical+length-1)/kPageSize;++page)
      if(foreign_[page].load(std::memory_order_acquire)) return true;
    return false;
  }
  void Arm(uint32_t physical,uint32_t length) {
    Validate(physical,length);
    memory_.EnablePhysicalMemoryAccessCallbacks(physical,length,true,false);
    aliases_->Arm(physical,length);
  }
  uint64_t Version(uint32_t physical,uint32_t length) const {
    Validate(physical,length);
    uint64_t newest=0;
    const uint32_t last=(physical+length-1)/kPageSize;
    for(uint32_t page=physical/kPageSize;page<=last;++page)
      newest=(std::max)(newest,pages_[page].load(std::memory_order_acquire));
    return newest;
  }
 private:
  void Invalidate(uint32_t start,uint32_t length) {
    const uint64_t end=(std::min)(uint64_t(start)+length,uint64_t(kPhysicalSize));
    if(start>=end) return;
    const bool foreign=CurrentThreadToken()!=owner_.load(std::memory_order_acquire);
    const auto version=serial_.fetch_add(1,std::memory_order_relaxed)+1;
    for(uint32_t page=start/kPageSize;page<=(end-1)/kPageSize;++page) {
      if(foreign) foreign_[page].store(true,std::memory_order_release);
      pages_[page].store(version,std::memory_order_release);
    }
  }
  static uint64_t CurrentThreadToken() {
    // Unique for each thread lifetime, unlike reusable OS thread IDs.
    static std::atomic<uint64_t> next{0};
    static thread_local const uint64_t token=next.fetch_add(1,std::memory_order_relaxed)+1;
    return token;
  }
  static void Validate(uint32_t physical,uint32_t length) {
    if(!length || physical>=kPhysicalSize || length>kPhysicalSize-physical)
      throw std::runtime_error("invalid physical invalidation range");
  }
  rex::memory::Memory& memory_;
  std::unique_ptr<std::atomic<uint64_t>[]> pages_;
  std::unique_ptr<std::atomic<bool>[]> foreign_;
  std::atomic<uint64_t> owner_{0};
  std::atomic<uint64_t> serial_{0};
  std::unique_ptr<NativeAliasWatch> aliases_;
  void* handle_=nullptr;
};
}
