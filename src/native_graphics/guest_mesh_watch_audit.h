#pragma once
#include "guest_physical_versions.h"
#include <map>
#include <span>
#include <vector>
#include <bitset>
#include <optional>

namespace edf::native {
// Shadow-only: never authorizes skipping a mesh comparison or an upload.
// Call Check under the renderer's submission lock; Memory must outlive us.
class GuestMeshWatchAudit {
 public:
  struct Counters { uint64_t checked=0,unsupported=0,stable=0,invalidated=0,missed=0,resets=0,excluded=0,foreign=0; };
  explicit GuestMeshWatchAudit(rex::memory::Memory& memory):memory_(memory),versions_(memory) {}
  // An unknown provider extent invalidates the ownership contract for the
  // entire audit lifetime. Keep rendering comparisons, never resume eligibility.
  void Disable() noexcept { disabled_=true; records_.clear(); used_=0; }
  // Never clear on release: an address reused from an untracked provider remains
  // ineligible for this audit's lifetime. False positives are safe, not misses.
  void ExcludePhysical(uint32_t physical,uint32_t length) {
    if(!length || physical>=GuestPhysicalVersions::kPhysicalSize ||
       length>GuestPhysicalVersions::kPhysicalSize-physical)
      throw std::runtime_error("invalid untracked physical writer range");
    for(uint32_t page=physical/4096;page<=(physical+length-1)/4096;++page)
      excluded_.set(page);
  }
  struct Observation {
    uint32_t physical,length;
    uint64_t version;
    const GuestMeshWatchAudit* audit;
  };
  // Arm before the independently guarded byte capture. No payload is read here.
  std::optional<Observation> Begin(uint32_t address,size_t length) {
    ++counts_.checked;
    if(disabled_) { ++counts_.unsupported; return {}; }
    if(!versions_.ClaimDrawingThread()) { ++counts_.unsupported; return {}; }
    const uint64_t end=uint64_t(address)+length;
    auto* heap=memory_.LookupHeap(address);
    if(!length || length>kBudget || end>0x100000000ull || !heap ||
       heap!=memory_.LookupHeap(uint32_t(end-1)) ||
       heap->heap_type()!=rex::memory::HeapType::kGuestPhysical ||
       heap->QueryRangeAccess(address,uint32_t(end-1))!=rex::memory::PageAccess::kReadWrite) {
      ++counts_.unsupported; return {};
    }
    const auto physical=memory_.GetPhysicalAddress(address);
    if(physical>=GuestPhysicalVersions::kPhysicalSize ||
       length>GuestPhysicalVersions::kPhysicalSize-physical) { ++counts_.unsupported; return {}; }
    for(uint32_t page=physical/4096;page<=(physical+length-1)/4096;++page)
      if(excluded_.test(page)) { ++counts_.excluded; return {}; }
    const auto key=std::pair{physical,uint32_t(length)};
    auto found=records_.find(key);
    if(versions_.HasForeignWrites(physical,uint32_t(length))) {
      if(found!=records_.end()) { used_-=found->second.bytes.size(); records_.erase(found); }
      ++counts_.foreign; return {};
    }
    versions_.Arm(physical,uint32_t(length));
    return Observation{physical,uint32_t(length),versions_.Version(physical,uint32_t(length)),this};
  }
  // Caller supplies immutable bytes captured after Begin. A rejected capture
  // may abandon its token. Never relabel an old snapshot with a newer version.
  void Finish(const Observation& observation,std::span<const uint8_t> bytes) {
    if(observation.audit!=this || bytes.size()!=observation.length)
      throw std::runtime_error("invalid mesh audit observation");
    if(disabled_ || !versions_.ClaimDrawingThread()) { ++counts_.unsupported; return; }
    const auto physical=observation.physical,length=observation.length;
    for(uint32_t page=physical/4096;page<=(physical+length-1)/4096;++page)
      if(excluded_.test(page)) { ++counts_.excluded; return; }
    const auto key=std::pair{physical,length};
    auto found=records_.find(key);
    const auto discard=[&] {
      if(found!=records_.end()) { used_-=found->second.bytes.size(); records_.erase(found); }
    };
    if(versions_.HasForeignWrites(physical,length)) { discard(); ++counts_.foreign; return; }
    const auto version=versions_.Version(physical,length);
    if(version!=observation.version) { discard(); ++counts_.invalidated; return; }
    if(found!=records_.end()) {
      const auto& record=found->second;
      const bool equal=std::equal(record.bytes.begin(),record.bytes.end(),bytes.begin());
      const auto after=versions_.Version(physical,length);
      if(record.reliable && record.version==version && after==version &&
         !versions_.HasForeignWrites(physical,length)) {
        if(equal) { ++counts_.stable; return; }
        ++counts_.missed;
      } else ++counts_.invalidated;
      used_-=record.bytes.size(); records_.erase(found);
    }
    if(records_.size()>=1024 || used_>kBudget-bytes.size()) {
      records_.clear(); used_=0; ++counts_.resets;
    }
    Record record{{bytes.begin(),bytes.end()},observation.version,false};
    record.reliable=observation.version==versions_.Version(physical,length) && !versions_.HasForeignWrites(physical,length);
    records_.emplace(key,std::move(record)); used_+=bytes.size();
  }
  // Single-threaded legacy/test convenience; production uses guarded snapshots.
  void Check(uint32_t address,std::span<const uint8_t> bytes) {
    if(const auto observation=Begin(address,bytes.size())) Finish(*observation,bytes);
  }
  const Counters& counters() const { return counts_; }
 private:
  struct Record { std::vector<uint8_t> bytes; uint64_t version; bool reliable; };
  static constexpr size_t kBudget=64*1024*1024;
  rex::memory::Memory& memory_;
  GuestPhysicalVersions versions_;
  std::map<std::pair<uint32_t,uint32_t>,Record> records_;
  size_t used_=0;
  bool disabled_=false;
  Counters counts_;
  std::bitset<GuestPhysicalVersions::kPhysicalSize/4096> excluded_;
};
}
