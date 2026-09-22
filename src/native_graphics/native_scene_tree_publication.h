#pragma once
#include "guest_block.h"
#include <array>
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
  using Regions=std::map<uint32_t,std::vector<uint8_t>>;
  // Shared so a restamp for a newer epoch never copies or mutates captured bytes.
  std::shared_ptr<const Regions> regions;
  // The region Find answers from for every address in [base,limit): the last
  // one starting at or below it, and where the next one starts.
  struct Region { uint32_t base=0; uint64_t limit=0; const std::vector<uint8_t>* bytes=nullptr; };
  Region Locate(uint32_t address) const {
    if(!regions) return {};
    auto next=regions->upper_bound(address);
    if(next==regions->begin()) return {};
    const uint64_t limit=next==regions->end()?(uint64_t(1)<<32):next->first;
    --next;
    return {next->first,limit,&next->second};
  }
  static const uint8_t* Within(const Region& region,uint32_t address,size_t bytes) {
    if(!region.bytes || address<region.base || address>=region.limit) return nullptr;
    const auto offset=uint64_t(address)-region.base;
    if(offset>region.bytes->size() || bytes>region.bytes->size()-offset) return nullptr;
    return region.bytes->data()+offset;
  }
  const uint8_t* Find(uint32_t address,size_t bytes) const { return Within(Locate(address),address,bytes); }
};
template<class Reader>
std::shared_ptr<NativeSceneTreeImage> CaptureNativeSceneTree(const Reader& reader,uint32_t owner) {
  auto image=std::make_shared<NativeSceneTreeImage>(); image->owner=owner;
  auto regions=std::make_shared<NativeSceneTreeImage::Regions>();
  const auto copy=[&](uint32_t at,size_t size) {
    const auto* bytes=reader.Bytes(at,size);
    regions->emplace(at,std::vector<uint8_t>(bytes,bytes+size));
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
  image->regions=std::move(regions);
  return image;
}
// Every byte the capture reads lies in a copied region, so equal regions imply
// an identical recapture. Hooked mutations only bump the epoch; unhooked
// writers (see the audit) are caught here.
template<class Reader>
bool NativeSceneTreeUnchanged(const Reader& reader,const NativeSceneTreeImage& image) {
  if(!image.regions) return false;
  for(const auto& [at,bytes]:*image.regions)
    if(std::memcmp(reader.Bytes(at,bytes.size()),bytes.data(),bytes.size())) return false;
  return true;
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
  uint64_t Epoch() const { return epoch_.load(std::memory_order_acquire); }
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
  // Recaptures only when the tree bytes differ from the owner's last image;
  // an unchanged tree keeps its image, or is restamped for a newer epoch.
  template<class Reader> bool Publish(const Reader& reader,uint32_t owner) {
    const auto epoch=epoch_.load(std::memory_order_acquire);
    std::shared_ptr<const NativeSceneTreeImage> previous;
    {
      std::lock_guard lock(mutex_);
      const auto found=images_.find(owner);
      if(found!=images_.end()) previous=found->second;
    }
    std::shared_ptr<NativeSceneTreeImage> image;
    if(previous && NativeSceneTreeUnchanged(reader,*previous)) {
      if(previous->epoch==epoch) { ++reuses_; return Current(*previous); }
      image=std::make_shared<NativeSceneTreeImage>(*previous); ++reuses_;
    } else { image=CaptureNativeSceneTree(reader,owner); ++captures_; }
    image->epoch=epoch;
    std::lock_guard lock(mutex_);
    if(!Current(*image)) return false;
    images_[owner]=std::move(image); return true;
  }
  uint64_t captures() const { return captures_.load(std::memory_order_relaxed); }
  uint64_t reuses() const { return reuses_.load(std::memory_order_relaxed); }
  std::shared_ptr<const NativeSceneTreeImage> Acquire(uint32_t owner) const {
    std::lock_guard lock(mutex_);
    const auto found=images_.find(owner);
    return found!=images_.end() && Current(*found->second)?found->second:nullptr;
  }
 private:
  std::atomic<uint64_t> epoch_{1},captures_{0},reuses_{0};
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
    if(image_ && publications_.Current(*image_)) if(const auto* bytes=Owned(address,size)) {
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
  // image_->Find through the last two regions it answered from: a node's
  // reads (+32 bounds, +64 radius, +84 children, +116 occupancy) fall in two
  // captured regions, so most reads skip the map search. A hit is exactly
  // Find's answer: the address lies below the next region's start.
  const uint8_t* Owned(uint32_t address,size_t size) const {
    for(const auto& hit:hits_)
      if(hit.bytes && address>=hit.base && address<hit.limit) return NativeSceneTreeImage::Within(hit,address,size);
    const auto region=image_->Locate(address);
    if(region.bytes) { hits_[next_hit_]=region; next_hit_^=1; }
    return NativeSceneTreeImage::Within(region,address,size);
  }
  const Reader& backing_;
  const NativeSceneTreePublications& publications_;
  std::shared_ptr<const NativeSceneTreeImage> image_;
  bool audit_=false;
  mutable std::array<NativeSceneTreeImage::Region,2> hits_{};
  mutable size_t next_hit_=0;
};
}
