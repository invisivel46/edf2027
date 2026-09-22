#pragma once
#include "guest_block.h"
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace edf::native {
struct NativeSceneTreeImage {
  uint64_t epoch=0;
  uint32_t owner=0;
  size_t nodes=0;
  std::map<uint32_t,std::vector<uint8_t>> regions;
  const uint8_t* Find(uint32_t address,size_t bytes) const {
    auto found=regions.upper_bound(address);
    if(found==regions.begin()) return nullptr;
    --found;
    const auto offset=uint64_t(address)-found->first;
    if(offset>found->second.size() || bytes>found->second.size()-offset) return nullptr;
    return found->second.data()+offset;
  }
};
template<class Reader>
std::shared_ptr<NativeSceneTreeImage> CaptureNativeSceneTree(const Reader& reader,uint32_t owner) {
  auto image=std::make_shared<NativeSceneTreeImage>(); image->owner=owner;
  const auto copy=[&](uint32_t at,size_t size) {
    const auto* bytes=reader.Bytes(at,size);
    image->regions.emplace(at,std::vector<uint8_t>(bytes,bytes+size));
  };
  const auto levels=reader.Word(reader.Add(owner,52));
  const auto level_end=reader.Word(reader.Add(owner,56));
  if(!levels || level_end<levels || level_end-levels<32) throw std::runtime_error("tree publication has no root level");
  const auto begin=reader.Word(reader.Add(levels,20)),end=reader.Word(reader.Add(levels,24));
  if(begin>end || (end-begin)%144) throw std::runtime_error("invalid tree publication root extent");
  copy(reader.Add(owner,52),8); copy(reader.Add(levels,20),8);
  std::set<uint32_t> visited;
  const auto capture=[&](auto&& self,uint32_t node,uint32_t depth)->void {
    if(!node || depth>128 || visited.size()>=1000000 || !visited.insert(node).second)
      throw std::runtime_error("invalid tree publication topology");
    ++image->nodes;
    if(!reader.Word(reader.Add(node,116))) { copy(reader.Add(node,116),4); return; }
    copy(reader.Add(node,32),36); // Center, half-extents and radius.
    copy(reader.Add(node,84),36); // Eight children and occupancy.
    if(reader.Word(reader.Add(node,84))) for(uint32_t child=0;child<8;++child)
      self(self,reader.Word(reader.Add(node,84+child*4)),depth+1);
  };
  for(auto node=begin;node!=end;node=reader.Add(node,144)) capture(capture,node,0);
  return image;
}
class NativeSceneTreePublications {
 public:
  using Images=std::map<uint32_t,std::shared_ptr<const NativeSceneTreeImage>>;
  Images AcquireAll() const {
    std::lock_guard lock(mutex_);
    Images result;
    for(const auto& [owner,image]:images_) if(Current(*image)) result.emplace(owner,image);
    return result;
  }
  void Invalidate() { epoch_.fetch_add(1,std::memory_order_acq_rel); }
  void Retire(uint32_t owner) {
    Invalidate(); std::lock_guard lock(mutex_); images_.erase(owner);
  }
  bool Current(const NativeSceneTreeImage& image) const { return image.epoch==epoch_.load(std::memory_order_acquire); }
  std::vector<uint32_t> Owners() const {
    std::lock_guard lock(mutex_);
    std::vector<uint32_t> owners;
    for(const auto& [owner,image]:images_) owners.push_back(owner);
    return owners;
  }
  template<class Reader> bool Publish(const Reader& reader,uint32_t owner) {
    const auto epoch=epoch_.load(std::memory_order_acquire);
    auto image=CaptureNativeSceneTree(reader,owner); image->epoch=epoch;
    std::lock_guard lock(mutex_);
    if(!Current(*image)) return false;
    images_[owner]=std::move(image); return true;
  }
  std::shared_ptr<const NativeSceneTreeImage> Acquire(uint32_t owner) const {
    std::lock_guard lock(mutex_);
    const auto found=images_.find(owner);
    return found!=images_.end() && Current(*found->second)?found->second:nullptr;
  }
 private:
  std::atomic<uint64_t> epoch_{1};
  mutable std::mutex mutex_;
  std::map<uint32_t,std::shared_ptr<const NativeSceneTreeImage>> images_;
};
template<class Reader>
class NativeSceneTreeReader {
 public:
  NativeSceneTreeReader(const Reader& backing,const NativeSceneTreePublications& publications,
      std::shared_ptr<const NativeSceneTreeImage> image,bool audit)
      : backing_(backing),publications_(publications),image_(std::move(image)),audit_(audit) {}
  uint32_t Add(uint32_t address,size_t size) const { return backing_.Add(address,size); }
  const uint8_t* Bytes(uint32_t address,size_t size) const {
    if(image_ && publications_.Current(*image_)) if(const auto* bytes=image_->Find(address,size)) {
      if(audit_ && std::memcmp(bytes,backing_.Bytes(address,size),size))
        throw std::runtime_error("native tree publication differs from live hierarchy");
      ++owned_reads; return bytes;
    }
    ++live_reads; return backing_.Bytes(address,size);
  }
  uint32_t Word(uint32_t address) const { return GuestBlockWord(Bytes(address,4)); }
  void StoreWord(uint32_t address,uint32_t value) const { backing_.StoreWord(address,value); }
  mutable uint64_t owned_reads=0,live_reads=0;
 private:
  const Reader& backing_;
  const NativeSceneTreePublications& publications_;
  std::shared_ptr<const NativeSceneTreeImage> image_;
  bool audit_=false;
};
}
