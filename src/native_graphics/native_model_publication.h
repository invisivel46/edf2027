#pragma once
#include "guest_block.h"
#include "native_model_buffers.h"
#include "native_model_pose_history.h"
#include "native_shared_vector.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace edf::native {
// Data half of a native model pass. Offsets follow the generated guest code:
// 821C9C20(r3=instance,r4=pose vector) traps on a null instance+0 container or
// on instance+4 equal to the container's end node (+4). instance+12 (byte)
// selects the palette path (821A1738); otherwise every record uploads
// pose[rec+44] through 821A17D8. On the palette path only records whose byte
// rec+48 is zero upload their bone. Mesh records are 52 bytes at node+44, count
// at node+52. 821B2C28(r3=record) walks 148-byte batches in [rec+4,rec+8):
// stream owner batch+4 (stride batch+60 = owner+56), declaration iterator
// batch+72/+76 (trap on end node, declaration = *(node+28)), index owner
// batch+84, index count batch+140 drawn as floor(n/3)*3 triangles-list indices,
// material *(batch+0) with pass count +24 and 112-byte pass records at *(+16).
// Pose vectors keep begin/end at +4/+8 with 64-byte row-major matrices.
inline constexpr uint32_t kNativeModelMaxMeshes=4096,kNativeModelMaxBatches=1024,
  kNativeModelMaxPasses=64,kNativeModelMaxBones=1024,kNativeModelMaxIndices=0x01fffffeu;
inline uint32_t NativeModelAddress(uint32_t base,uint64_t offset) {
  if(!base || uint64_t(base)+offset>0xffffffffull) throw std::runtime_error("native model address overflow");
  return uint32_t(base+offset);
}
struct NativeModelBufferIdentity {
  uint32_t owner=0;
  uint64_t generation=0; // NativeModelBuffers lifetime generation; 0 when unpublished.
  bool operator==(const NativeModelBufferIdentity&) const=default;
};
struct NativeModelBatchLayout {
  uint32_t address=0,material=0,declaration=0,stride=0,index_count=0,draw_count=0;
  NativeModelBufferIdentity vertex,index;
  std::vector<uint32_t> passes; // Material pass record addresses, in draw order.
  bool operator==(const NativeModelBatchLayout&) const=default;
};
struct NativeModelMeshLayout {
  uint32_t address=0,bone=0;
  bool skinned=false,uploads_bone=false;
  std::vector<NativeModelBatchLayout> batches;
  bool operator==(const NativeModelMeshLayout&) const=default;
};
// Immutable once registered. Identity, not a certificate that guest bytes stay put.
struct NativeModelLayout {
  uint32_t instance=0,container=0,node=0,pose_vector=0,bones=0;
  bool skinned=false;
  std::vector<NativeModelMeshLayout> meshes;
  size_t Batches() const { size_t count=0; for(const auto& mesh:meshes) count+=mesh.batches.size(); return count; }
  bool operator==(const NativeModelLayout&) const=default;
};
struct NativeModelPoseRange { uint32_t begin=0,count=0; };
template<class Reader>
NativeModelPoseRange ReadNativeModelPoseRange(const Reader& reader,uint32_t vector) {
  if(!vector || vector%4) throw std::runtime_error("invalid native model pose vector");
  const auto begin=reader.Word(NativeModelAddress(vector,4)),end=reader.Word(NativeModelAddress(vector,8));
  if(!begin) {
    if(end) throw std::runtime_error("native model pose vector has an end without storage");
    return {};
  }
  if(begin%4 || end<begin || (end-begin)%64 || (end-begin)/64>kNativeModelMaxBones)
    throw std::runtime_error("invalid native model pose extent");
  return {begin,(end-begin)/64};
}
template<class Reader>
std::vector<NativePoseMatrix> ReadNativeModelPose(const Reader& reader,uint32_t vector,uint32_t* storage=nullptr) {
  const auto range=ReadNativeModelPoseRange(reader,vector);
  std::vector<NativePoseMatrix> matrices(range.count);
  if(range.count) {
    const auto* bytes=reader.Bytes(range.begin,size_t(range.count)*64);
    for(size_t bone=0;bone<matrices.size();++bone) for(size_t i=0;i<16;++i)
      matrices[bone][i]=std::bit_cast<float>(GuestBlockWord(bytes+bone*64+i*4));
  }
  if(storage) *storage=range.begin;
  return matrices;
}
// Bitwise: NaN payloads and signed zeros are part of the published source.
inline bool SameNativeModelPose(std::span<const NativePoseMatrix> a,std::span<const NativePoseMatrix> b) {
  return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y) {
    return std::bit_cast<std::array<uint32_t,16>>(x)==std::bit_cast<std::array<uint32_t,16>>(y);
  });
}
template<class Reader,class Lookup>
NativeModelBatchLayout DecodeNativeModelBatch(const Reader& reader,uint32_t address,const Lookup& lookup) {
  NativeModelBatchLayout batch; batch.address=address;
  NativeModelAddress(address,148);
  batch.material=reader.Word(address);
  if(!batch.material || batch.material%4) throw std::runtime_error("native model batch has no material");
  // The guest compares the pass count as signed zero and then counts down.
  const auto passes=reader.Word(NativeModelAddress(batch.material,24));
  if(passes>kNativeModelMaxPasses) throw std::runtime_error("native model material pass count out of range");
  const auto records=reader.Word(NativeModelAddress(batch.material,16));
  if(passes && (!records || records%4)) throw std::runtime_error("native model material has no pass records");
  if(passes) reader.Bytes(records,size_t(passes)*112);
  batch.passes.reserve(passes);
  for(uint32_t pass=0;pass<passes;++pass) batch.passes.push_back(records+pass*112);
  batch.stride=reader.Word(address+60);
  if(!batch.stride || batch.stride>2048 || batch.stride%4) throw std::runtime_error("invalid native model batch stride");
  const auto container=reader.Word(address+72),node=reader.Word(address+76);
  if(!container || !node || node==reader.Word(NativeModelAddress(container,4)))
    throw std::runtime_error("native model batch declaration iterator is at its end");
  batch.declaration=reader.Word(NativeModelAddress(node,28));
  if(!batch.declaration) throw std::runtime_error("native model batch has no declaration");
  batch.index_count=reader.Word(address+140);
  if(batch.index_count>kNativeModelMaxIndices) throw std::runtime_error("native model batch index count out of range");
  batch.draw_count=batch.index_count/3*3;
  batch.vertex={address+4,lookup(address+4,NativeModelBuffers::Kind::Vertex)};
  batch.index={address+84,lookup(address+84,NativeModelBuffers::Kind::Index)};
  return batch;
}
// lookup(owner,kind) returns the NativeModelBuffers generation, or 0.
template<class Reader,class Lookup>
NativeModelLayout DecodeNativeModelLayoutWith(const Reader& reader,uint32_t instance,uint32_t pose_vector,const Lookup& lookup) {
  if(!instance || instance%4) throw std::runtime_error("invalid native model instance");
  NativeModelLayout layout; layout.instance=instance; layout.pose_vector=pose_vector;
  layout.container=reader.Word(instance);
  layout.node=reader.Word(NativeModelAddress(instance,4));
  if(!layout.container || !layout.node || layout.node%4 || layout.node==reader.Word(NativeModelAddress(layout.container,4)))
    throw std::runtime_error("native model instance names no model resource");
  layout.skinned=reader.Bytes(NativeModelAddress(instance,12),1)[0]!=0;
  layout.bones=ReadNativeModelPoseRange(reader,pose_vector).count;
  if(layout.skinned && !layout.bones) throw std::runtime_error("native skinned model has an empty pose");
  const auto records=reader.Word(NativeModelAddress(layout.node,44)),count=reader.Word(NativeModelAddress(layout.node,52));
  if(count>kNativeModelMaxMeshes || (count && (!records || records%4)))
    throw std::runtime_error("invalid native model mesh table");
  if(count) reader.Bytes(records,size_t(count)*52);
  layout.meshes.reserve(count);
  for(uint32_t index=0;index<count;++index) {
    auto& mesh=layout.meshes.emplace_back();
    mesh.address=records+index*52;
    mesh.bone=reader.Word(mesh.address+44);
    mesh.skinned=reader.Bytes(mesh.address+48,1)[0]!=0;
    mesh.uploads_bone=!layout.skinned || !mesh.skinned;
    if(mesh.uploads_bone && mesh.bone>=layout.bones) throw std::runtime_error("native model mesh bone outside its pose");
    const auto first=reader.Word(mesh.address+4),last=reader.Word(mesh.address+8);
    if(!first) { if(last) throw std::runtime_error("native model mesh batches have an end without storage"); continue; }
    if(first%4 || last<first || (last-first)%148 || (last-first)/148>kNativeModelMaxBatches)
      throw std::runtime_error("invalid native model mesh batch extent");
    mesh.batches.reserve((last-first)/148);
    for(uint32_t batch=first;batch!=last;batch+=148) mesh.batches.push_back(DecodeNativeModelBatch(reader,batch,lookup));
  }
  return layout;
}
template<class Reader>
NativeModelLayout DecodeNativeModelLayout(const Reader& reader,uint32_t instance,uint32_t pose_vector,const NativeModelBuffers* buffers) {
  return DecodeNativeModelLayoutWith(reader,instance,pose_vector,[&](uint32_t owner,NativeModelBuffers::Kind kind)->uint64_t {
    const auto* found=buffers?buffers->Find(owner,kind):nullptr;
    return found?found->generation:0;
  });
}
// Per-(instance, generation) layouts plus an immutable per-tick pose map.
// A leaf lock: no callback or other lock is taken while it is held.
class NativeModelPublications {
 public:
  struct Layout {
    uint64_t generation=0;
    std::shared_ptr<const NativeModelLayout> layout;
    explicit operator bool() const { return bool(layout); }
  };
  struct Pose {
    uint32_t instance=0;
    uint64_t layout_generation=0,pose_generation=0;
    std::vector<NativePoseMatrix> matrices;
  };
  struct PosePublication {
    uint64_t generation=0,tick=0;
    NativeSharedMap<uint32_t,std::shared_ptr<const Pose>> poses;
    const Pose* Find(uint32_t instance) const { const auto* found=poses.Find(instance); return found?found->get():nullptr; }
  };
  size_t size() const { return count_.load(std::memory_order_relaxed); }
  uint64_t captures() const { std::lock_guard lock(mutex_); return captures_; }
  uint64_t retirements() const { std::lock_guard lock(mutex_); return retirements_; }
  uint64_t pose_failures() const { std::lock_guard lock(mutex_); return pose_failures_; }
  uint64_t snapshots() const { std::lock_guard lock(mutex_); return snapshots_; }
  Layout Find(uint32_t instance) const {
    std::lock_guard lock(mutex_);
    const auto found=entries_.find(instance);
    return found==entries_.end()?Layout{}:found->second.layout;
  }
  // First-sight test: the registered layout still names this model and pose vector.
  bool Current(uint32_t instance,uint32_t container,uint32_t node,uint32_t pose_vector) const {
    std::lock_guard lock(mutex_);
    const auto found=entries_.find(instance);
    if(found==entries_.end()) return false;
    const auto& layout=*found->second.layout.layout;
    return layout.container==container && layout.node==node && layout.pose_vector==pose_vector;
  }
  // A decode failure is remembered per identity so draws do not re-decode it.
  bool Rejected(uint32_t instance,uint32_t node,uint32_t pose_vector) const {
    std::lock_guard lock(mutex_);
    const auto found=rejected_.find(instance);
    return found!=rejected_.end() && found->second==std::pair{node,pose_vector};
  }
  void Reject(uint32_t instance,uint32_t node,uint32_t pose_vector) {
    std::lock_guard lock(mutex_);
    if(rejected_.size()>=4096) rejected_.clear();
    rejected_.insert_or_assign(instance,std::pair{node,pose_vector});
  }
  // Replaces any older generation of the same instance. Its pose is seeded
  // at the next publication even when simulation does not rebuild it.
  Layout Register(NativeModelLayout layout,uint32_t storage=0) {
    if(!layout.instance || !layout.node || !layout.pose_vector) throw std::runtime_error("incomplete native model layout");
    std::lock_guard lock(mutex_);
    RetireLocked(layout.instance,false);
    rejected_.erase(layout.instance);
    const auto instance=layout.instance,node=layout.node,vector=layout.pose_vector;
    Entry entry{{++generation_,std::make_shared<const NativeModelLayout>(std::move(layout))},storage};
    AddKey(instance,instance); AddKey(node,instance);
    if(storage) AddKey(storage,instance);
    vectors_[vector].push_back(instance);
    seeds_.insert(vector);
    const auto result=entry.layout;
    entries_.emplace(instance,std::move(entry));
    count_.store(entries_.size(),std::memory_order_relaxed);
    ++captures_;
    return result;
  }
  bool Retire(uint32_t instance) { std::lock_guard lock(mutex_); return RetireLocked(instance,true); }
  // A pose that changes between renders of one tick (camera-facing
  // attachments) is not what a tick publication can carry. Latched per layout
  // generation by the pose audit or the interpolation history; a new
  // generation starts clean.
  void MarkRenderDependent(uint32_t instance,uint64_t generation) {
    std::lock_guard lock(mutex_);
    const auto found=entries_.find(instance);
    if(found!=entries_.end() && found->second.layout.generation==generation) render_dependent_.insert_or_assign(instance,generation);
  }
  bool RenderDependent(uint32_t instance,uint64_t generation) const {
    std::lock_guard lock(mutex_);
    const auto found=render_dependent_.find(instance);
    return found!=render_dependent_.end() && found->second==generation;
  }
  // Deallocation (820B2510): the freed block was an instance, its model node
  // or the pose storage it was captured with. Vector growth (821C8F10) frees
  // the old storage the same way; a later draw captures a new generation.
  size_t RetireAddress(uint32_t address) {
    if(!count_.load(std::memory_order_relaxed) || !address) return 0;
    std::lock_guard lock(mutex_);
    rejected_.erase(address);
    std::vector<uint32_t> instances;
    const auto [first,last]=keys_.equal_range(address);
    for(auto at=first;at!=last;++at) instances.push_back(at->second);
    size_t retired=0;
    for(const auto instance:instances) retired+=RetireLocked(instance,true);
    return retired;
  }
  // Engine thread, after the tick's dirty walk. Only vectors built this tick
  // (and first-sight seeds) are read; every other entry keeps its shared pose.
  template<class Reader>
  std::shared_ptr<const PosePublication> PublishPoses(const Reader& reader,uint64_t tick,std::span<const uint32_t> dirty) {
    std::lock_guard lock(mutex_);
    std::vector<uint32_t> pending(dirty.begin(),dirty.end());
    pending.insert(pending.end(),seeds_.begin(),seeds_.end());
    seeds_.clear();
    std::sort(pending.begin(),pending.end());
    pending.erase(std::unique(pending.begin(),pending.end()),pending.end());
    ++pose_generation_;
    for(const auto vector:pending) {
      const auto found=vectors_.find(vector);
      if(found==vectors_.end()) continue;
      const auto instances=found->second;
      std::vector<NativePoseMatrix> matrices;
      uint32_t storage=0;
      try { matrices=ReadNativeModelPose(reader,vector,&storage); }
      catch(const std::exception&) {
        ++pose_failures_;
        for(const auto instance:instances) poses_.Erase(instance);
        continue;
      }
      ++snapshots_;
      for(const auto instance:instances) {
        auto& entry=entries_.at(instance);
        // The skeleton changed size: the captured bone checks no longer hold.
        if(matrices.size()!=entry.layout.layout->bones) { ++pose_failures_; RetireLocked(instance,true); continue; }
        if(storage!=entry.storage) {
          if(entry.storage) RemoveKey(entry.storage,instance);
          if(storage) AddKey(storage,instance);
          entry.storage=storage;
        }
        poses_.Set(instance,std::make_shared<const Pose>(Pose{instance,entry.layout.generation,pose_generation_,matrices}));
      }
    }
    auto publication=std::make_shared<PosePublication>();
    publication->generation=pose_generation_; publication->tick=tick; publication->poses=poses_;
    published_=std::move(publication);
    return published_;
  }
  std::shared_ptr<const PosePublication> AcquirePoses() const { std::lock_guard lock(mutex_); return published_; }
  void Clear() {
    std::lock_guard lock(mutex_);
    retirements_+=entries_.size();
    entries_.clear(); keys_.clear(); vectors_.clear(); seeds_.clear(); rejected_.clear(); render_dependent_.clear();
    poses_=decltype(poses_){}; published_.reset();
    count_.store(0,std::memory_order_relaxed);
  }
 private:
  struct Entry { Layout layout; uint32_t storage=0; };
  void AddKey(uint32_t address,uint32_t instance) {
    const auto [first,last]=keys_.equal_range(address);
    for(auto at=first;at!=last;++at) if(at->second==instance) return;
    keys_.emplace(address,instance);
  }
  void RemoveKey(uint32_t address,uint32_t instance) {
    const auto [first,last]=keys_.equal_range(address);
    for(auto at=first;at!=last;++at) if(at->second==instance) { keys_.erase(at); return; }
  }
  bool RetireLocked(uint32_t instance,bool count) {
    const auto found=entries_.find(instance);
    if(found==entries_.end()) return false;
    const auto& layout=*found->second.layout.layout;
    RemoveKey(instance,instance); RemoveKey(layout.node,instance);
    if(found->second.storage) RemoveKey(found->second.storage,instance);
    if(const auto vector=vectors_.find(layout.pose_vector);vector!=vectors_.end()) {
      std::erase(vector->second,instance);
      if(vector->second.empty()) { seeds_.erase(vector->first); vectors_.erase(vector); }
    }
    poses_.Erase(instance);
    render_dependent_.erase(instance);
    entries_.erase(found);
    count_.store(entries_.size(),std::memory_order_relaxed);
    if(count) ++retirements_;
    return true;
  }
  mutable std::mutex mutex_;
  std::atomic<size_t> count_{0};
  std::unordered_map<uint32_t,Entry> entries_;
  std::unordered_multimap<uint32_t,uint32_t> keys_;
  std::unordered_map<uint32_t,std::vector<uint32_t>> vectors_;
  std::unordered_set<uint32_t> seeds_;
  std::unordered_map<uint32_t,std::pair<uint32_t,uint32_t>> rejected_;
  std::unordered_map<uint32_t,uint64_t> render_dependent_;
  NativeSharedMap<uint32_t,std::shared_ptr<const Pose>> poses_;
  std::shared_ptr<const PosePublication> published_;
  uint64_t generation_=0,pose_generation_=0,captures_=0,retirements_=0,pose_failures_=0,snapshots_=0;
};
struct NativeModelAudit {
  bool decoded=true,layout_mismatch=false,published=false,pose_mismatch=false;
};
// Development check at model draw entry: the published layout must still
// decode identically, and a pose published for this generation must equal
// the live vector bit for bit.
template<class Reader,class Lookup>
NativeModelAudit AuditNativeModelPublication(const Reader& reader,const NativeModelPublications::Layout& published,
    const NativeModelPublications::PosePublication* poses,uint32_t pose_vector,const Lookup& lookup) {
  NativeModelAudit audit;
  if(!published) return audit;
  const auto& layout=*published.layout;
  try { audit.layout_mismatch=DecodeNativeModelLayoutWith(reader,layout.instance,pose_vector,lookup)!=layout; }
  catch(const std::exception&) { audit.decoded=false; audit.layout_mismatch=true; }
  const auto* pose=poses?poses->Find(layout.instance):nullptr;
  if(!pose || pose->layout_generation!=published.generation) return audit;
  audit.published=true;
  try { audit.pose_mismatch=!SameNativeModelPose(pose->matrices,ReadNativeModelPose(reader,pose_vector)); }
  catch(const std::exception&) { audit.pose_mismatch=true; }
  return audit;
}
}
