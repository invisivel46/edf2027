#pragma once
#include <cstdint>
#include <map>
#include <stdexcept>
#include <optional>
#include <algorithm>
#include <vector>
#include <memory>
#include "native_buffer_writes.h"

namespace edf::native {
class NativeIndexBuffer;
class NativeVertexBuffer;
// CPU-facing model buffer contract, independent of Xbox fetch descriptors.
// Metadata is not a promise that the pointed-to contents are immutable.
class NativeModelBuffers {
 public:
  explicit NativeModelBuffers(NativeBufferWrites* writes=nullptr):writes_(writes) {}
  NativeModelBuffers(const NativeModelBuffers&)=delete;
  NativeModelBuffers& operator=(const NativeModelBuffers&)=delete;
  ~NativeModelBuffers() {
    if(writes_) for(const auto& [owner,buffer]:buffers_) writes_->Unsubscribe(owner);
  }
  enum class Kind { Vertex, Index };
  struct Buffer { Kind kind; uint32_t address,bytes,stride; uint64_t generation,notified_updates; std::optional<uint32_t> physical;
    std::shared_ptr<const NativeIndexBuffer> index_storage;
    std::shared_ptr<NativeVertexBuffer> vertex_storage;
    std::shared_ptr<const std::vector<uint8_t>> vertex_contents;
    std::shared_ptr<const std::vector<uint8_t>> index_contents;
  };
  void Publish(uint32_t owner,Kind kind,uint32_t address,uint32_t stride,uint32_t count,
               std::optional<uint32_t> physical={},std::shared_ptr<const NativeIndexBuffer> index_storage={}) {
    const uint64_t bytes=uint64_t(stride)*count;
    if(!owner || (kind==Kind::Vertex && (!stride || stride>2048 || stride%4)) ||
       (kind==Kind::Index && stride!=2 && stride!=4) || bytes>0x03fffffcu ||
       (bytes && (!address || address%4)) || uint64_t(address)+bytes>0x100000000ull)
      throw std::runtime_error("invalid native model buffer metadata");
    if(physical && (*physical>=0x20000000u || bytes>0x20000000u-*physical))
      throw std::runtime_error("invalid native model physical extent");
    if(index_storage && kind!=Kind::Index) throw std::runtime_error("index storage on vertex owner");
    buffers_.insert_or_assign(owner,Buffer{kind,address,static_cast<uint32_t>(bytes),stride,++generation_,0,physical,std::move(index_storage)});
    index_dirty_=true;
    if(writes_) writes_->Subscribe(owner,physical,static_cast<uint32_t>(bytes));
  }
  const Buffer* Find(uint32_t owner,Kind kind) const {
    const auto found=buffers_.find(owner);
    return found!=buffers_.end() && found->second.kind==kind?&found->second:nullptr;
  }
  template<class Visitor> void VisitIndexStorage(Visitor visitor) const {
    for(const auto& [owner,buffer]:buffers_) if(buffer.index_storage) visitor(owner,*buffer.index_storage);
  }
  template<class Visitor> void VisitVertexStorage(Visitor visitor) const {
    for(const auto& [owner,buffer]:buffers_) if(buffer.vertex_storage) visitor(owner,*buffer.vertex_storage);
  }
  bool RetainVertexStorage(uint32_t owner,uint64_t generation,std::shared_ptr<NativeVertexBuffer> storage) {
    const auto found=buffers_.find(owner);
    if(found==buffers_.end() || found->second.kind!=Kind::Vertex || found->second.generation!=generation) return false;
    found->second.vertex_storage=std::move(storage); return true;
  }
  bool RetainVertexContents(uint32_t owner,uint64_t generation,
      std::shared_ptr<const std::vector<uint8_t>> contents,
      std::optional<NativeBufferWrites::ObservedVersion> expected={}) {
    const auto found=buffers_.find(owner);
    if(found==buffers_.end() || found->second.kind!=Kind::Vertex ||
       found->second.generation!=generation || !contents || contents->size()!=found->second.bytes) return false;
    if(expected) return writes_ && writes_->CommitObserved(owner,*expected,[&] {
      found->second.vertex_contents=std::move(contents);
    });
    // Physical owners require an observation token; this still does not certify
    // unreported stores or provide exclusion against concurrent guest writes.
    if(found->second.physical) return false;
    found->second.vertex_contents=std::move(contents); return true;
  }
  // Retain CPU index ownership independently of GPU-object construction.
  bool RetainIndexContents(uint32_t owner,uint64_t generation,
      std::shared_ptr<const std::vector<uint8_t>> contents,NativeBufferWrites::ObservedVersion expected) {
    const auto found=buffers_.find(owner);
    if(!writes_ || found==buffers_.end() || found->second.kind!=Kind::Index ||
       found->second.generation!=generation || !contents || contents->size()!=found->second.bytes) return false;
    return writes_->CommitObserved(owner,expected,[&] { found->second.index_contents=std::move(contents); });
  }
  // Diagnostic query against currently published owners, without invalidation.
  template<class Visitor> void VisitPhysicalOverlaps(uint32_t address,uint32_t bytes,Visitor visitor) {
    if(!bytes || address>=0x20000000u || bytes>0x20000000u-address) return;
    RebuildIndex();
    auto at=std::lower_bound(intervals_.begin(),intervals_.end(),uint64_t(address)+bytes,
      [](const Interval& interval,uint64_t end) { return interval.begin<end; });
    while(at!=intervals_.begin()) {
      --at;
      if(at->prefix_end<=address) break;
      if(at->end>address) visitor(at->owner,buffers_.at(at->owner));
    }
  }
  // Called after live source validation, under the same registry lock as
  // invalidation. A lifetime generation is not a content-version certificate.
  bool RetainIndexStorage(uint32_t owner,uint64_t generation,std::shared_ptr<const NativeIndexBuffer> storage) {
    const auto found=buffers_.find(owner);
    if(found==buffers_.end() || found->second.kind!=Kind::Index || found->second.generation!=generation) return false;
    found->second.index_storage=std::move(storage); return true;
  }
  bool CommitObservedIndex(uint32_t owner,uint64_t generation,NativeBufferWrites::ObservedVersion expected,
                           std::shared_ptr<const NativeIndexBuffer> storage) {
    const auto found=buffers_.find(owner);
    if(!writes_ || found==buffers_.end() || found->second.kind!=Kind::Index || found->second.generation!=generation) return false;
    return writes_->CommitObserved(owner,expected,[&] { found->second.index_storage=std::move(storage); });
  }
  bool CommitObservedVertex(uint32_t owner,uint64_t generation,NativeBufferWrites::ObservedVersion expected,
                            std::shared_ptr<NativeVertexBuffer> storage) {
    const auto found=buffers_.find(owner);
    if(!writes_ || found==buffers_.end() || found->second.kind!=Kind::Vertex || found->second.generation!=generation) return false;
    return writes_->CommitObserved(owner,expected,[&] { found->second.vertex_storage=std::move(storage); });
  }
  void Retire(uint32_t owner) {
    if(buffers_.erase(owner)) {
      index_dirty_=true;
      if(writes_) writes_->Unsubscribe(owner);
    }
  }
  struct GeometryOwner { uint32_t owner; uint64_t generation; NativeBufferWrites::ObservedVersion version; };
  // Registry lock held. Publish both GPU conversions and the canonical full VB
  // snapshot under one queue handshake, or leave all three attachments alone.
  bool CommitObservedGeometry(GeometryOwner vertex,GeometryOwner index,
      std::shared_ptr<NativeVertexBuffer> vertex_storage,
      std::shared_ptr<const std::vector<uint8_t>> vertex_contents,
      std::shared_ptr<const NativeIndexBuffer> index_storage,
      std::shared_ptr<const std::vector<uint8_t>> index_contents) {
    const auto vb=buffers_.find(vertex.owner),ib=buffers_.find(index.owner);
    if(!writes_ || vb==buffers_.end() || ib==buffers_.end() ||
       vb->second.kind!=Kind::Vertex || ib->second.kind!=Kind::Index ||
       vb->second.generation!=vertex.generation || ib->second.generation!=index.generation ||
       !vb->second.physical || !ib->second.physical || !vertex_storage || !index_storage ||
       !vertex_contents || vertex_contents->size()!=vb->second.bytes ||
       !index_contents || index_contents->size()!=ib->second.bytes) return false;
    return writes_->CommitObservedSet(std::array<NativeBufferWrites::ObservedOwner,2>{{
      {vertex.owner,vertex.version},{index.owner,index.version}}},[&] {
      vb->second.vertex_storage=std::move(vertex_storage);
      vb->second.vertex_contents=std::move(vertex_contents);
      ib->second.index_storage=std::move(index_storage);
      ib->second.index_contents=std::move(index_contents);
    });
  }
  // Counts observed completed producer updates only, not arbitrary guest stores.
  bool NotifyUpdate(uint32_t owner) {
    const auto found=buffers_.find(owner);
    if(found==buffers_.end()) return false;
    ++found->second.notified_updates;
    found->second.index_storage.reset();
    found->second.index_contents.reset();
    found->second.vertex_storage.reset();
    found->second.vertex_contents.reset();
    return true;
  }
  // An explicit update may also change other owners of the same physical bytes.
  // Conservatively cover the entire published extent, including nested unlocks;
  // this is invalidation, not a certificate of transaction completion.
  template<class RetireMesh> bool NotifyUpdateAliases(uint32_t owner,RetireMesh retire_mesh) {
    const auto found=buffers_.find(owner);
    if(found==buffers_.end()) return false;
    const auto& buffer=found->second;
    if(buffer.physical && buffer.bytes) {
      if(writes_) writes_->InvalidateObservedRange(*buffer.physical,buffer.bytes);
      NativeBufferWrites::Batch batch;
      batch.ranges[0]={*buffer.physical,buffer.bytes}; batch.count=1;
      ApplyWrites(batch,retire_mesh);
    } else {
      NotifyUpdate(owner); retire_mesh(owner);
    }
    return true;
  }
  template<class RetireMesh> void ApplyWrites(const NativeBufferWrites::Batch& batch,RetireMesh retire_mesh) {
    if(batch.all) {
      for(auto& [owner,buffer]:buffers_) { ++buffer.notified_updates; buffer.index_storage.reset(); buffer.index_contents.reset(); buffer.vertex_storage.reset(); buffer.vertex_contents.reset(); retire_mesh(owner); }
      return;
    }
    if(batch.pages) {
      for(auto& [owner,buffer]:buffers_) if(buffer.physical && buffer.bytes) {
        const auto first=*buffer.physical/NativeBufferWrites::kPageBytes;
        const auto last=(*buffer.physical+buffer.bytes-1)/NativeBufferWrites::kPageBytes;
        for(uint32_t page=first;page<=last;++page) if(batch.pages->test(page)) {
          ++buffer.notified_updates; buffer.index_storage.reset(); buffer.index_contents.reset(); buffer.vertex_storage.reset(); buffer.vertex_contents.reset(); retire_mesh(owner); break;
        }
      }
      return;
    }
    if(!batch.count) return;
    RebuildIndex();
    std::vector<uint32_t> affected;
    for(size_t i=0;i<batch.count;++i) {
      const auto& range=batch.ranges[i];
      if(!range.bytes) continue;
      auto at=std::lower_bound(intervals_.begin(),intervals_.end(),uint64_t(range.address)+range.bytes,
        [](const Interval& interval,uint64_t end) { return interval.begin<end; });
      while(at!=intervals_.begin()) {
        --at;
        // Prefix maximum makes this valid even with nested/aliased resources.
        if(at->prefix_end<=range.address) break;
        if(at->end>range.address) affected.push_back(at->owner);
      }
    }
    std::sort(affected.begin(),affected.end());
    affected.erase(std::unique(affected.begin(),affected.end()),affected.end());
    for(const auto owner:affected) { auto& buffer=buffers_.at(owner); ++buffer.notified_updates; buffer.index_storage.reset(); buffer.index_contents.reset(); buffer.vertex_storage.reset(); buffer.vertex_contents.reset(); retire_mesh(owner); }
  }
  // Model creators place the allocator record at owner+32. Generic allocator
  // release may bypass the higher-level model destructor. Only retire records
  // we actually own; unrelated allocations must not invalidate mesh handles.
  uint32_t RetireAllocation(uint32_t allocation_record) {
    if(allocation_record<32) return 0;
    const auto owner=allocation_record-32;
    if(!buffers_.contains(owner)) return 0;
    Retire(owner); return owner;
  }
  // Registry lock held, before the allocator can reuse the released block.
  // Retire every published alias, not only the wrapper embedding this record.
  template<class RetireMesh> uint32_t RetireAllocationAliases(uint32_t allocation_record,RetireMesh retire_mesh) {
    if(allocation_record<32) return 0;
    return RetireBackingAliases(allocation_record-32,std::move(retire_mesh));
  }
  template<class RetireMesh> uint32_t RetireBackingAliases(uint32_t owner,RetireMesh retire_mesh) {
    const auto found=buffers_.find(owner);
    if(found==buffers_.end()) return 0;
    std::vector<uint32_t> affected;
    const auto& buffer=found->second;
    if(buffer.physical && buffer.bytes)
      VisitPhysicalOverlaps(*buffer.physical,buffer.bytes,[&](uint32_t alias,const Buffer&) {
        affected.push_back(alias);
      });
    else affected.push_back(owner);
    // Finish allocation/query work before changing any ownership. Invalidation
    // callbacks must not reenter this registry or publish/release guest objects.
    for(const auto alias:affected) { Retire(alias); retire_mesh(alias); }
    return owner;
  }
  // Explicit whole-backing release; unlike owner retirement this does not
  // require a surviving model wrapper naming the allocation.
  template<class RetireMesh> void RetirePhysicalRange(uint32_t address,uint32_t bytes,RetireMesh retire_mesh) {
    if(!bytes) return;
    if(address>=0x20000000u || bytes>0x20000000u-address)
      throw std::runtime_error("invalid native backing retirement extent");
    std::vector<uint32_t> affected;
    VisitPhysicalOverlaps(address,bytes,[&](uint32_t owner,const Buffer&) { affected.push_back(owner); });
    for(const auto owner:affected) { Retire(owner); retire_mesh(owner); }
  }
 private:
  NativeBufferWrites* writes_;
  struct Interval { uint64_t begin,end,prefix_end; uint32_t owner; };
  void RebuildIndex() {
    if(!index_dirty_) return;
    intervals_.clear();
    for(const auto& [owner,buffer]:buffers_) if(buffer.physical && buffer.bytes)
      intervals_.push_back({*buffer.physical,uint64_t(*buffer.physical)+buffer.bytes,0,owner});
    std::sort(intervals_.begin(),intervals_.end(),[](const Interval& a,const Interval& b) { return a.begin<b.begin; });
    uint64_t end=0;
    for(auto& interval:intervals_) { end=(std::max)(end,interval.end); interval.prefix_end=end; }
    index_dirty_=false;
  }
  std::vector<Interval> intervals_;
  bool index_dirty_=true;
  std::map<uint32_t,Buffer> buffers_;
  uint64_t generation_=0;
};
}
