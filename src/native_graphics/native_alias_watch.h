#pragma once
#include <rex/system/xmemory.h>
#include <rex/thread/mutex.h>
#include <Windows.h>
#include <algorithm>
#include <map>
#include <vector>
#include <stdexcept>

namespace edf::native {
// Complements SDK watches for host-writable, guest-unallocated physical aliases.
// Memory must outlive every watcher. Callback is pre-write, allocation-free and
// must not enter the renderer. All registry/protection operations use the SDK's
// recursive memory lock, including fault handling and lifetime transitions.
class NativeAliasWatch {
 public:
  using Notify=void(*)(void*,uint32_t,uint32_t);
  NativeAliasWatch(rex::memory::Memory& memory,Notify notify,void* context)
      :memory_(memory),notify_(notify),context_(context) {
    auto lock=rex::thread::global_critical_region::AcquireDirect();
    if(!handler_) {
      handler_=AddVectoredExceptionHandler(1,Fault);
      if(!handler_) throw std::runtime_error("cannot install native alias watch");
    }
    ++users_;
  }
  ~NativeAliasWatch() {
    auto lock=rex::thread::global_critical_region::AcquireDirect();
    for(auto at=pages_.begin();at!=pages_.end();) {
      auto& page=at->second;
      std::erase(page.observers,this);
      if(!page.observers.empty()) { ++at; continue; }
      // Do not override a real guest read-only protection applied after arming.
      const auto access=page.memory->LookupHeap(page.guest)->QueryRangeAccess(page.guest,page.guest+4095);
      if(access!=rex::memory::PageAccess::kReadOnly) Restore(at->first,page);
      at=pages_.erase(at);
    }
    if(!--users_) { RemoveVectoredExceptionHandler(handler_); handler_=nullptr; }
  }
  NativeAliasWatch(const NativeAliasWatch&)=delete;
  NativeAliasWatch& operator=(const NativeAliasWatch&)=delete;
  void Arm(uint32_t physical,uint32_t length) {
    if(!length || physical>=0x20000000u || length>0x20000000u-physical)
      throw std::runtime_error("invalid native alias watch range");
    auto lock=rex::thread::global_critical_region::AcquireDirect();
    const uint64_t end=(uint64_t(physical)+length+4095)&~uint64_t(4095);
    for(uint32_t base:{0xa0000000u,0xc0000000u,0xe0000000u}) {
      auto* heap=memory_.LookupHeap(base);
      const auto offset=memory_.GetPhysicalAddress(base);
      for(uint64_t page=physical&~4095u;page<end;page+=4096) {
        if(page<offset || page+4096>uint64_t(offset)+heap->heap_size()) continue;
        const uint32_t guest=base+uint32_t(page-offset);
        // Allocated writable aliases belong to the SDK. Read-only guest pages
        // must retain their genuine protection even if another alias is writable.
        if(heap->QueryRangeAccess(guest,guest+4095)!=rex::memory::PageAccess::kNoAccess) continue;
        const auto host=reinterpret_cast<uintptr_t>(memory_.TranslateVirtual(guest));
        if(host&4095) throw std::runtime_error("unaligned native alias watch mapping");
        auto found=pages_.find(host);
        if(found!=pages_.end()) {
          auto& observers=found->second.observers;
          MEMORY_BASIC_INFORMATION current{};
          if(!VirtualQuery(reinterpret_cast<void*>(host),&current,sizeof(current)) || current.State!=MEM_COMMIT ||
             (current.Protect!=PAGE_READONLY && current.Protect!=PAGE_READWRITE)) {
            for(auto* observer:observers) observer->notify_(observer->context_,uint32_t(page),4096);
            pages_.erase(found);
            continue;
          }
          if(current.Protect==PAGE_READWRITE) {
            // An external protection transition may have opened the alias.
            // Invalidate old snapshots and really re-arm, not merely retain
            // a stale registry entry claiming that the page is protected.
            for(auto* observer:observers) observer->notify_(observer->context_,uint32_t(page),4096);
            DWORD old=0;
            if(!VirtualProtect(reinterpret_cast<void*>(host),4096,PAGE_READONLY,&old))
              throw std::runtime_error("cannot re-arm native alias watch");
          }
          if(std::find(observers.begin(),observers.end(),this)==observers.end()) observers.push_back(this);
          continue;
        }
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void*>(host),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
           info.Protect!=PAGE_READWRITE) continue;
        auto inserted=pages_.emplace(host,Page{&memory_,guest,uint32_t(page),{this}}).first;
        DWORD old=0;
        if(!VirtualProtect(reinterpret_cast<void*>(host),4096,PAGE_READONLY,&old)) {
          pages_.erase(inserted);
          throw std::runtime_error("cannot arm native alias watch");
        }
      }
    }
  }
 private:
  struct Page { rex::memory::Memory* memory; uint32_t guest,physical; std::vector<NativeAliasWatch*> observers; };
  static bool Restore(uintptr_t host,const Page& page) {
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<void*>(host),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
       info.Protect!=PAGE_READONLY) return false;
    // A formerly unallocated alias may now have a legitimate SDK watch. Clear
    // and notify that watch before restoring its writable guest protection.
    page.memory->TriggerPhysicalMemoryCallbacks(rex::thread::global_critical_region::AcquireDirect(),
      page.guest,4096,true,true);
    DWORD old=0;
    return VirtualProtect(reinterpret_cast<void*>(host),4096,PAGE_READWRITE,&old)!=0;
  }
  static LONG CALLBACK Fault(EXCEPTION_POINTERS* exception) {
    const auto* record=exception->ExceptionRecord;
    if(record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || record->NumberParameters<2 ||
       record->ExceptionInformation[0]!=1) return EXCEPTION_CONTINUE_SEARCH;
    auto lock=rex::thread::global_critical_region::AcquireDirect();
    const uintptr_t host=record->ExceptionInformation[1]&~uintptr_t(4095);
    const auto found=pages_.find(host);
    if(found==pages_.end()) return EXCEPTION_CONTINUE_SEARCH;
    auto& page=found->second;
    const auto access=page.memory->LookupHeap(page.guest)->QueryRangeAccess(page.guest,page.guest+4095);
    if(access==rex::memory::PageAccess::kReadOnly) return EXCEPTION_CONTINUE_SEARCH;
    for(auto* observer:page.observers) observer->notify_(observer->context_,page.physical,4096);
    if(!Restore(host,page)) return EXCEPTION_CONTINUE_SEARCH;
    pages_.erase(found); // One shot; later snapshots explicitly re-arm.
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  rex::memory::Memory& memory_;
  Notify notify_;
  void* context_;
  inline static std::map<uintptr_t,Page> pages_;
  inline static void* handler_=nullptr;
  inline static size_t users_=0;
};
}
