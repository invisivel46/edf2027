#pragma once
#include "guest_block.h"
#include "native_model_buffers.h"
#include "native_model_pose_history.h"
#include "native_shared_vector.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
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
// The guest's pose bytes (64 big-endian bytes per matrix) against a decoded
// pose, bitwise as SameNativeModelPose, without decoding or allocating: an
// unchanged pose costs one pass over its bytes and keeps its shared copy.
inline bool SameNativeModelPoseBytes(const uint8_t* bytes,size_t count,std::span<const NativePoseMatrix> pose) {
  if(pose.size()!=count) return false;
  for(size_t bone=0;bone<count;++bone) for(size_t i=0;i<16;++i)
    if(GuestBlockWord(bytes+bone*64+i*4)!=std::bit_cast<uint32_t>(pose[bone][i])) return false;
  return true;
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
// A leaf lock: no callback or other lock is taken while it is held, and guest
// memory is never read under it.
//
// Frees race decodes: a layout is decoded without the lock and registered
// afterwards, and a pose vector is read without it and committed afterwards.
// Both run inside a CaptureScope; while any scope is open RetireAddress
// records every freed address with a free epoch, and Register / the pose
// commit drop what names an address freed after their scope began. With no
// scope open the free hook is two atomic loads unless the address hashes to a
// bucket that holds a key.
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
  // Open for the whole decode-then-register (or read-then-commit) window.
  class CaptureScope {
   public:
    explicit CaptureScope(NativeModelPublications& owner):owner_(owner),epoch_(owner.BeginCapture()) {}
    ~CaptureScope() { owner_.EndCapture(epoch_); }
    CaptureScope(const CaptureScope&)=delete;
    CaptureScope& operator=(const CaptureScope&)=delete;
    uint64_t epoch() const { return epoch_; }
   private:
    NativeModelPublications& owner_;
    uint64_t epoch_;
  };
  size_t size() const { return count_.load(std::memory_order_relaxed); }
  uint64_t captures() const { std::lock_guard lock(mutex_); return captures_; }
  uint64_t retirements() const { std::lock_guard lock(mutex_); return retirements_; }
  uint64_t pose_failures() const { std::lock_guard lock(mutex_); return pose_failures_; }
  uint64_t snapshots() const { std::lock_guard lock(mutex_); return snapshots_; }
  uint64_t stale_captures() const { std::lock_guard lock(mutex_); return stale_captures_; }
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
    if(rejected_.size()>=4096) { for(const auto& rejected:rejected_) Mark(rejected.first,-1); rejected_.clear(); }
    if(rejected_.insert_or_assign(instance,std::pair{node,pose_vector}).second) Mark(instance,1);
  }
  // Replaces any older generation of the same instance. Its pose is seeded
  // at the next publication even when simulation does not rebuild it. With a
  // capture scope, a layout whose instance, node or pose storage was freed
  // after the scope began is dropped (empty result) and nothing is replaced:
  // the registered generation, its pose and its render-dependence latch stay.
  Layout Register(NativeModelLayout layout,uint32_t storage=0,const CaptureScope* capture=nullptr) {
    if(!layout.instance || !layout.node || !layout.pose_vector) throw std::runtime_error("incomplete native model layout");
    std::lock_guard lock(mutex_);
    if(capture && (FreedSinceLocked(layout.instance,capture->epoch()) || FreedSinceLocked(layout.node,capture->epoch()) ||
       (storage && FreedSinceLocked(storage,capture->epoch())))) { ++stale_captures_; return {}; }
    RetireLocked(layout.instance,false);
    EraseRejectedLocked(layout.instance);
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
  // Lock-free unless a capture is in flight or the address hits a key bucket.
  size_t RetireAddress(uint32_t address) {
    if(!address) return 0;
    // Pairs with EndCapture: a scope seen closed has its keys marked.
    if(!capturing_.load(std::memory_order_seq_cst) && !marks_[Bucket(address)].load(std::memory_order_relaxed)) return 0;
    std::lock_guard lock(mutex_);
    if(capturing_.load(std::memory_order_relaxed)) freed_.emplace_back(address,++free_epoch_);
    EraseRejectedLocked(address);
    std::vector<uint32_t> instances;
    const auto [first,last]=keys_.equal_range(address);
    for(auto at=first;at!=last;++at) instances.push_back(at->second);
    size_t retired=0;
    for(const auto instance:instances) retired+=RetireLocked(instance,true);
    return retired;
  }
  // Engine thread, after the tick's dirty walk. Only vectors built this tick
  // (and first-sight seeds) are read; every other entry keeps its shared pose.
  // The vector list is taken under the lock, guest memory is read without it,
  // and the commit re-checks each instance's layout generation and the read
  // storage's free epoch, so a concurrent retire or free wins. Poses carry the
  // generation seen at collection, which the model pass gate and the
  // render-dependence latch (untouched here) key on.
  template<class Reader>
  std::shared_ptr<const PosePublication> PublishPoses(const Reader& reader,uint64_t tick,std::span<const uint32_t> dirty) {
    struct Read {
      uint32_t vector=0,storage=0;
      bool failed=false;
      std::vector<std::pair<uint32_t,uint64_t>> instances; // (instance, layout generation) when collected.
      std::vector<NativePoseMatrix> matrices;
    };
    const CaptureScope capture(*this);
    std::vector<Read> reads;
    {
      std::lock_guard lock(mutex_);
      std::vector<uint32_t> pending(dirty.begin(),dirty.end());
      pending.insert(pending.end(),seeds_.begin(),seeds_.end());
      seeds_.clear();
      std::sort(pending.begin(),pending.end());
      pending.erase(std::unique(pending.begin(),pending.end()),pending.end());
      for(const auto vector:pending) {
        const auto found=vectors_.find(vector);
        if(found==vectors_.end()) continue;
        auto& read=reads.emplace_back(); read.vector=vector;
        for(const auto instance:found->second) read.instances.emplace_back(instance,entries_.at(instance).layout.generation);
      }
    }
    for(auto& read:reads) {
      try { read.matrices=ReadNativeModelPose(reader,read.vector,&read.storage); }
      catch(const std::exception&) { read.failed=true; }
    }
    std::lock_guard lock(mutex_);
    const auto live=[&](uint32_t instance,uint64_t generation)->Entry* {
      const auto found=entries_.find(instance);
      return found!=entries_.end() && found->second.layout.generation==generation?&found->second:nullptr;
    };
    ++pose_generation_;
    for(const auto& read:reads) {
      // Storage freed while it was read: the copy is unusable; reseed next tick.
      const bool freed=!read.failed && read.storage && FreedSinceLocked(read.storage,capture.epoch());
      if(read.failed || freed) {
        ++pose_failures_;
        for(const auto& [instance,generation]:read.instances) if(live(instance,generation)) poses_.Erase(instance);
        if(freed && vectors_.contains(read.vector)) seeds_.insert(read.vector);
        continue;
      }
      ++snapshots_;
      for(const auto& [instance,generation]:read.instances) {
        auto* entry=live(instance,generation);
        if(!entry) continue; // Retired or re-registered while the pose was read.
        // The skeleton changed size: the captured bone checks no longer hold.
        if(read.matrices.size()!=entry->layout.layout->bones) { ++pose_failures_; RetireLocked(instance,true); continue; }
        if(read.storage!=entry->storage) {
          if(entry->storage) RemoveKey(entry->storage,instance);
          if(read.storage) AddKey(read.storage,instance);
          entry->storage=read.storage;
        }
        poses_.Set(instance,std::make_shared<const Pose>(Pose{instance,entry->layout.generation,pose_generation_,read.matrices}));
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
    for(auto& mark:marks_) mark.store(0,std::memory_order_relaxed);
    poses_=decltype(poses_){}; published_.reset();
    count_.store(0,std::memory_order_relaxed);
  }
 private:
  struct Entry { Layout layout; uint32_t storage=0; };
  static constexpr uint32_t kMarkBits=14;
  // Fibonacci hashing spreads aligned guest heap blocks over the buckets.
  static uint32_t Bucket(uint32_t address) { return uint32_t(address*0x9E3779B1u)>>(32-kMarkBits); }
  // Counting prefilter over keys_ and rejected_ instances: written under the
  // lock, read without it by the free hook.
  void Mark(uint32_t address,int delta) { marks_[Bucket(address)].fetch_add(uint32_t(delta),std::memory_order_relaxed); }
  uint64_t BeginCapture() {
    std::lock_guard lock(mutex_);
    capturing_.fetch_add(1,std::memory_order_seq_cst);
    active_.insert(free_epoch_);
    return free_epoch_;
  }
  void EndCapture(uint64_t epoch) {
    std::lock_guard lock(mutex_);
    active_.erase(active_.find(epoch));
    if(active_.empty()) freed_.clear();
    else { const auto oldest=*active_.begin(); std::erase_if(freed_,[&](const auto& freed) { return freed.second<=oldest; }); }
    capturing_.fetch_sub(1,std::memory_order_seq_cst);
  }
  bool FreedSinceLocked(uint32_t address,uint64_t epoch) const {
    return std::any_of(freed_.begin(),freed_.end(),[&](const auto& freed) { return freed.first==address && freed.second>epoch; });
  }
  void EraseRejectedLocked(uint32_t instance) { if(rejected_.erase(instance)) Mark(instance,-1); }
  void AddKey(uint32_t address,uint32_t instance) {
    const auto [first,last]=keys_.equal_range(address);
    for(auto at=first;at!=last;++at) if(at->second==instance) return;
    keys_.emplace(address,instance);
    Mark(address,1);
  }
  void RemoveKey(uint32_t address,uint32_t instance) {
    const auto [first,last]=keys_.equal_range(address);
    for(auto at=first;at!=last;++at) if(at->second==instance) { keys_.erase(at); Mark(address,-1); return; }
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
  std::atomic<uint32_t> capturing_{0};
  std::array<std::atomic<uint32_t>,size_t(1)<<kMarkBits> marks_{};
  std::unordered_map<uint32_t,Entry> entries_;
  std::unordered_multimap<uint32_t,uint32_t> keys_;
  std::unordered_map<uint32_t,std::vector<uint32_t>> vectors_;
  std::unordered_set<uint32_t> seeds_;
  std::unordered_map<uint32_t,std::pair<uint32_t,uint32_t>> rejected_;
  std::unordered_map<uint32_t,uint64_t> render_dependent_;
  std::multiset<uint64_t> active_;                  // Open capture scopes' start epochs.
  std::vector<std::pair<uint32_t,uint64_t>> freed_; // (address, free epoch) while a scope is open.
  NativeSharedMap<uint32_t,std::shared_ptr<const Pose>> poses_;
  std::shared_ptr<const PosePublication> published_;
  uint64_t generation_=0,pose_generation_=0,free_epoch_=0,captures_=0,retirements_=0,pose_failures_=0,snapshots_=0,stale_captures_=0;
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
