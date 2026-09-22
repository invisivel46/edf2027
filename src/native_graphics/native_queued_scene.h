#pragma once
#include "guest_instance_parameters.h"
#include <cstring>
#include <span>
#include <bit>
#include <map>
#include <algorithm>

namespace edf::native {
// Frame-local visibility selections. Retail inserts at the head of each group;
// native queues retain insertion order and consume in reverse. Guest addresses
// identify only this frame's inputs, never persistent object lifetimes.
class NativeSceneQueues {
 public:
  bool enabled=true;
  bool Contains(uint32_t group) const { return Slot(group)!=nullptr; }
  bool empty() const { return groups_.empty(); }
  template<class Contains> bool Within(Contains&& contains) const {
    return std::ranges::all_of(groups_,[&](const auto& entry) { return contains(entry.first); });
  }
  void Push(uint32_t group,uint32_t instance) {
    if(!enabled || !group || !instance) throw std::runtime_error("invalid native scene queue append");
    if(auto* instances=Slot(group)) { instances->push_back(instance); return; }
    const auto at=groups_.try_emplace(group).first;
    last_.entry=&*at; at->second.push_back(instance);
  }
  std::vector<uint32_t> Take(uint32_t group) {
    const auto found=groups_.find(group);
    if(found==groups_.end()) return {};
    if(last_.entry==&*found) last_.entry=nullptr;
    auto result=std::move(found->second); groups_.erase(found);
    std::reverse(result.begin(),result.end()); return result;
  }
  template<class Reader>
  void Materialize(const Reader& reader) {
    for(const auto& [group,instances]:groups_) for(const auto instance:instances) {
      const auto head=reader.Word(reader.Add(group,4));
      reader.StoreWord(reader.Add(instance,4),head);
      reader.StoreWord(reader.Add(group,4),instance);
    }
    groups_.clear(); last_.entry=nullptr; enabled=false;
  }
 private:
  using Groups=std::map<uint32_t,std::vector<uint32_t>>;
  // The group the last lookup found: a selected object's check and its pushes
  // hit the same group in a row. Map nodes are stable until erased; a copy or
  // move of the queues starts without one.
  struct LastGroup {
    Groups::value_type* entry=nullptr;
    LastGroup()=default;
    LastGroup(const LastGroup&) {}
    LastGroup& operator=(const LastGroup&) { entry=nullptr; return *this; }
  };
  std::vector<uint32_t>* Slot(uint32_t group) const {
    if(last_.entry && last_.entry->first==group) return &last_.entry->second;
    const auto found=groups_.find(group);
    if(found==groups_.end()) return nullptr;
    last_.entry=&*found; return &found->second;
  }
  mutable Groups groups_;
  mutable LastGroup last_;
};
template<class Reader>
bool NativeQueuedConstantsClean(const Reader& reader,uint32_t device) {
  if(device&15) return false;
  const auto* dirty=reader.Bytes(device,40);
  for(size_t i=0;i<40;++i) if(dirty[i]) return false;
  return true;
}
inline std::array<float,16> DecodeNativeQueuedWorld(std::span<const uint8_t,64> registers,bool column_major) {
  std::array<float,16> result;
  for(size_t row=0;row<4;++row) for(size_t column=0;column<4;++column)
    result[row*4+column]=std::bit_cast<float>(GuestBlockWord(registers.data()+(column_major?column*4+row:row*4+column)*4));
  return result;
}
// CPU effects of 821D9600/82149248 followed by the native indexed CPU tail,
// restricted to the clean-state case. No guest callback may run between these
// operations. The first draw of a group establishes/consumes all other state.
template<class Reader>
bool NativeQueuedConstantsConsumed(const Reader& reader,uint32_t device,
                                   std::span<const InstanceParameter> parameters) {
  if(!NativeQueuedConstantsClean(reader,device)) return false;
  const uint64_t bank=reader.Add(device,1792),end=bank+4096;
  // Validate the whole operation before any write. Reject aliases: applying an
  // earlier override must not change bytes still needed by native bindings.
  for(const auto& p:parameters) {
    if(!p.count || p.first>256 || p.count>256-p.first) return false;
    const size_t size=size_t(p.count)*16;
    if(uint64_t(p.data)<end && uint64_t(p.data)+size>bank) return false;
    reader.Bytes(p.data,size);
    reader.WritableBytes(reader.Add(device,(112+p.first)*16),size,4);
  }
  for(const auto& p:parameters)
    std::memcpy(const_cast<uint8_t*>(reader.WritableBytes(reader.Add(device,(112+p.first)*16),size_t(p.count)*16,4)),
                reader.Bytes(p.data,size_t(p.count)*16),size_t(p.count)*16);
  // 82149248 ORs vertex dirty bits; the packet-free FE358 tail consumes them
  // and stores zero. No intervening callback observes that temporary mask.
  return true;
}

// Keep the retail queue order and read the next link after drawing, since a
// fallback callback can change it. Persistent identity belongs to the enclosing
// scene object; these links are visibility selections only.
template<class Reader,class Prepare,class Draw>
void VisitNativeQueuedScene(const Reader& reader,uint32_t group,Prepare&& prepare,Draw&& draw) {
  const auto end=reader.Word(reader.Add(group,8));
  auto at=reader.Word(reader.Add(group,4));
  if(at==end) return;
  prepare();
  size_t count=0;
  while(at!=end) {
    if(++count>1048576) throw std::runtime_error("native scene queue is cyclic or too large");
    draw(reader.Word(at));
    at=reader.Word(reader.Add(at,4));
  }
  reader.StoreWord(reader.Add(group,4),0);
}
}
