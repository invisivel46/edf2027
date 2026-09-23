#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <vector>
#include <memory>
#include <optional>
#include "guest_block.h"
#include "guest_instance_parameters.h"
#include "native_scene_visibility.h"
#include "native_shared_vector.h"

namespace edf::native {
// Constructor/destructor events establish lifetime. Observation cannot create
// an owner: a guest address alone is not a persistent scene identity.
class NativeSceneSources {
 public:
  // One immutable generation supplies membership, LOD, world and visibility
  // together. Repeated acquisitions share storage until a producer event, and
  // every generation shares the chunks that later events did not touch.
  std::shared_ptr<const NativeSceneSources> AcquireSnapshot() const {
    if(!snapshot_) {
      auto result=std::make_shared<NativeSceneSources>();
      result->owners_=owners_; result->parts_=parts_; result->groups_=groups_;
      result->group_revision_=group_revision_; result->next_=next_;
      result->candidate_revision_=candidate_revision_; result->lineage_.value=lineage_.value;
      snapshot_=std::move(result);
    }
    return snapshot_;
  }
  struct Part {
    uint32_t instance=0,lod=0,part=0,world_data=0,group=0;
    std::optional<uint32_t> world_first;
    bool operator==(const Part&) const=default;
  };
  struct Source {
    uint64_t generation=0;
    uint32_t owner=0,lod=0,part=0,world_data=0;
    std::optional<uint32_t> world_first;
    bool operator==(const Source&) const=default;
  };
  struct Group {
    uint64_t revision=0;
    NativeSharedMap<uint32_t,Part> parts;
  };
  using GroupMap=NativeSharedMap<uint32_t,Group>;
  const Group* FindGroup(uint32_t group) const { return groups_.Find(group); }
  const GroupMap& Groups() const { return groups_; }
  // Advances whenever any group is added, changed or erased.
  uint64_t GroupRevision() const { return group_revision_; }
  // Advances whenever FindCandidate's answer for any owner can change (birth,
  // retirement, changed parts or visibility). Copied into every snapshot, so
  // equal revisions mean equal candidates. World registers do not move it.
  uint64_t CandidateRevision() const { return candidate_revision_; }
  // Whether every FindCandidate(View) answer here equals other's: snapshots of
  // one producer (a copy is a new lineage) at the same candidate revision.
  bool SameCandidates(const NativeSceneSources& other) const {
    return lineage_.value==other.lineage_.value && candidate_revision_==other.candidate_revision_;
  }
  // fixed: a single-record owner (clRock, NativeSceneFixedRecord) whose parts
  // are its one record's, filed as LOD 0 and found by LodParts(owner+396).
  uint64_t Born(uint32_t owner,bool fixed=false) {
    if(!owner || next_==UINT64_MAX) throw std::runtime_error("invalid native scene source lifetime");
    Retire(owner);
    snapshot_.reset(); ++candidate_revision_;
    const auto generation=next_;
    owners_.Set(owner,Owner{generation,std::make_shared<const Parts>(),{},{},fixed});
    ++next_;
    return generation;
  }
  bool Retire(uint32_t owner) {
    const auto* found=owners_.Find(owner);
    if(!found) return false;
    snapshot_.reset(); ++candidate_revision_;
    const auto parts=found->parts;
    for(const auto& part:parts->all) { RemoveGroupPart(part); parts_.Erase(part.instance); }
    owners_.Erase(owner); return true;
  }
  bool Observe(uint32_t owner,std::span<const Part> parts) {
    const auto* found=owners_.Find(owner);
    if(!found) return false;
    const auto old=found->parts;
    const auto generation=found->generation;
    if(std::equal(parts.begin(),parts.end(),old->all.begin(),old->all.end())) return true;
    std::map<uint32_t,Source> replacements;
    auto next=std::make_shared<Parts>();
    for(const auto& part:parts) {
      if(part.lod>=3) throw std::runtime_error("invalid native static LOD");
      next->lod[part.lod].push_back(part);
      if(!part.instance || !replacements.emplace(part.instance,Source{generation,owner,part.lod,part.part,part.world_data,part.world_first}).second)
        throw std::runtime_error("duplicate native scene source part");
      const auto* existing=parts_.Find(part.instance);
      if(existing && existing->owner!=owner)
        throw std::runtime_error("native scene part has two live owners");
    }
    next->all.assign(parts.begin(),parts.end());
    snapshot_.reset(); ++candidate_revision_;
    for(const auto& part:old->all) { RemoveGroupPart(part); parts_.Erase(part.instance); }
    for(auto& [instance,source]:replacements) parts_.Set(instance,std::move(source));
    for(const auto& part:next->all) if(part.group) {
      auto* group=groups_.Mutable(part.group);
      if(!group) { groups_.Set(part.group,Group{}); group=groups_.Mutable(part.group); }
      if(!group->parts.contains(part.instance)) group->parts.Set(part.instance,part);
      group->revision=++group_revision_;
    }
    owners_.Mutable(owner)->parts=std::move(next);
    return true;
  }
  const Source* Find(uint32_t instance) const { return parts_.Find(instance); }
  bool HasOwner(uint32_t owner) const { return owners_.contains(owner); }
  bool Fixed(uint32_t owner) const {
    const auto* found=owners_.Find(owner);
    return found && found->fixed;
  }
  uint64_t Generation(uint32_t owner) const {
    const auto* found=owners_.Find(owner);
    return found?found->generation:0;
  }
  // The parts 821BEE68 publishes for `descriptor`: LOD record lod of an LOD
  // owner (owner+408+lod*44, 820B2670), or a fixed owner's one record
  // (owner+396, 820BAF90). The owner kind decides, so neither can alias the other.
  std::optional<std::span<const Part>> LodParts(uint32_t descriptor) const {
    for(uint32_t lod=0;lod<3;++lod) {
      const uint32_t offset=408+lod*44;
      if(descriptor<offset) continue;
      if(const auto* found=owners_.Find(descriptor-offset);found && !found->fixed) return std::span<const Part>(found->parts->lod[lod]);
    }
    if(descriptor>=kFixedRecord)
      if(const auto* found=owners_.Find(descriptor-kFixedRecord);found && found->fixed) return std::span<const Part>(found->parts->lod[0]);
    return {};
  }
  static constexpr uint32_t kFixedRecord=396;
  // Everything the per-frame visibility walk needs from one owner, found with
  // one lookup: its bounds and each LOD's parts. Retains that membership, so
  // the spans stay valid after a later producer event replaces the owner.
  struct Candidate {
    bool registered=false,fixed=false;
    std::shared_ptr<const NativeSceneVisibility> visibility;
    std::shared_ptr<const void> retained;
    std::array<std::span<const Part>,3> lods;
    // As LodParts(owner+408+lod*44): absent for an unregistered owner or LOD.
    std::optional<std::span<const Part>> Lod(uint32_t lod) const {
      if(!registered || lod>=lods.size()) return {};
      return lods[lod];
    }
  };
  Candidate FindCandidate(uint32_t owner) const {
    Candidate result;
    const auto* found=owners_.Find(owner);
    if(!found) return result;
    result.registered=true; result.fixed=found->fixed; result.visibility=found->visibility; result.retained=found->parts;
    for(size_t lod=0;lod<3;++lod) result.lods[lod]=found->parts->lod[lod];
    return result;
  }
  // A Candidate's answer without its retaining copies (no reference-count
  // traffic per object): valid while what it was taken from is alive and
  // unchanged, i.e. an immutable published generation or a Candidate the
  // caller keeps.
  struct CandidateView {
    bool registered=false,fixed=false;
    const NativeSceneVisibility* visibility=nullptr;
    std::array<std::span<const Part>,3> lods;
    std::optional<std::span<const Part>> Lod(uint32_t lod) const {
      if(!registered || lod>=lods.size()) return {};
      return lods[lod];
    }
  };
  static CandidateView View(const Candidate& candidate) {
    return {candidate.registered,candidate.fixed,candidate.visibility.get(),candidate.lods};
  }
  CandidateView FindCandidateView(uint32_t owner) const {
    CandidateView result;
    const auto* found=owners_.Find(owner);
    if(!found) return result;
    result.registered=true; result.fixed=found->fixed; result.visibility=found->visibility.get();
    for(size_t lod=0;lod<3;++lod) result.lods[lod]=found->parts->lod[lod];
    return result;
  }
  using World=std::array<uint8_t,64>;
  bool PublishVisibility(uint32_t owner,const NativeSceneVisibility& visibility) {
    const auto* found=owners_.Find(owner);
    if(!found) return false;
    if(!found->visibility || *found->visibility!=visibility) {
      auto published=std::make_shared<const NativeSceneVisibility>(visibility);
      snapshot_.reset(); ++candidate_revision_;
      owners_.Mutable(owner)->visibility=std::move(published);
    }
    return true;
  }
  std::shared_ptr<const NativeSceneVisibility> Visibility(uint32_t owner) const {
    const auto* found=owners_.Find(owner);
    return found?found->visibility:nullptr;
  }
  bool PublishWorld(uint32_t owner,const World& registers) {
    const auto* found=owners_.Find(owner);
    if(!found) return false;
    if(!found->world || *found->world!=registers) {
      auto published=std::make_shared<const World>(registers);
      snapshot_.reset();
      owners_.Mutable(owner)->world=std::move(published);
    }
    return true;
  }
  std::shared_ptr<const World> WorldRegisters(const Source& source,uint32_t data) const {
    if(!data || source.world_data!=data) return {};
    const auto* found=owners_.Find(source.owner);
    if(!found || found->generation!=source.generation) return {};
    return found->world;
  }
  size_t owners() const { return owners_.size(); }
  size_t parts() const { return parts_.size(); }
 private:
  mutable std::shared_ptr<const NativeSceneSources> snapshot_;
  // Shared by every generation until the owner's membership is replaced.
  struct Parts {
    std::vector<Part> all;
    std::array<std::vector<Part>,3> lod;
  };
  struct Owner {
    uint64_t generation=0;
    std::shared_ptr<const Parts> parts;
    std::shared_ptr<const World> world;
    std::shared_ptr<const NativeSceneVisibility> visibility;
    bool fixed=false;
  };
  NativeSharedMap<uint32_t,Owner> owners_;
  NativeSharedMap<uint32_t,Source> parts_;
  GroupMap groups_;
  uint64_t group_revision_=0,candidate_revision_=0;
  // One producer's identity, shared only by its snapshots: a copied or
  // assigned object takes a fresh one, as its revisions no longer follow ours.
  struct Lineage {
    uint64_t value=Next();
    Lineage()=default;
    Lineage(const Lineage&):value(Next()) {}
    Lineage& operator=(const Lineage&) { value=Next(); return *this; }
    static uint64_t Next() { static std::atomic<uint64_t> next{1}; return next.fetch_add(1,std::memory_order_relaxed); }
  } lineage_;
  void RemoveGroupPart(const Part& part) {
    if(!part.group) return;
    auto* group=groups_.Mutable(part.group);
    if(!group) return;
    group->parts.Erase(part.instance);
    if(group->parts.empty()) { groups_.Erase(part.group); ++group_revision_; }
    else group->revision=++group_revision_;
  }
  uint64_t next_=1;
};

// Static-object construction/model replacement owns these LOD vectors. Read
// them at those events, never while selecting visibility for every frame.
template<class Reader>
std::vector<NativeSceneSources::Part> ReadNativeStaticSceneParts(const Reader& reader,uint32_t owner) {
  const auto lods=reader.Word(reader.Add(owner,404));
  if(lods>3) throw std::runtime_error("invalid static world LOD count");
  std::vector<NativeSceneSources::Part> parts;
  for(uint32_t lod=0;lod<lods;++lod) {
    const auto range=ReadGuestWords<2>(reader,reader.Add(owner,412+lod*44));
    if(range[1]<range[0] || (range[1]-range[0])%28 || (range[1]-range[0])/28>8192)
      throw std::runtime_error("invalid static world instance vector");
    if(range[0]!=range[1]) reader.Bytes(range[0],range[1]-range[0]);
    const auto parameter=reader.Word(reader.Add(owner,436+lod*44));
    const auto world_data=parameter?reader.Word(reader.Add(parameter,4)):0;
    for(uint32_t at=range[0],part=0;at<range[1];at+=28,++part) {
      NativeSceneSources::Part value{at,lod,part,world_data,reader.Word(reader.Add(at,8))};
      const auto parameters=ReadInstanceParameters(reader,at);
      if(world_data && parameters.size()==1 && parameters[0].count==4 && parameters[0].data==world_data)
        value.world_first=parameters[0].first;
      parts.push_back(std::move(value));
    }
  }
  return parts;
}
// clRock (vtable 82002830, constructor 820BAF98, 448 bytes) is not a
// clMapArtifact_Base: its slot 4 820BAF90 is `addi r3,r3,396; b 821BEE68`, one
// model record at +396 (initialized by 821BFEB8, loaded by 821C07B8, its
// bound computed by 821BEF10 into +288 and its world registers written once by
// 821BEDF0 from +224) and no LOD choice. The record has the LOD record's
// layout: instance vector at +4/+8 (28-byte instances), parameter at +28.
// Its slot 2 (the map artifacts' world update, 820B2DF8) is the empty
// 8252B718, so what the constructor leaves is what every render draws.
struct NativeSceneFixedRecord {
  static constexpr uint32_t vtable=0x82002830,render=0x820BAF90,constructor=0x820BAF98,destructor=0x820BB208;
  static constexpr uint32_t record=NativeSceneSources::kFixedRecord,instances=record+4,parameter=record+28;
};
template<class Reader>
std::vector<NativeSceneSources::Part> ReadNativeFixedSceneParts(const Reader& reader,uint32_t owner) {
  using F=NativeSceneFixedRecord;
  const auto range=ReadGuestWords<2>(reader,reader.Add(owner,F::instances));
  if(range[1]<range[0] || (range[1]-range[0])%28 || (range[1]-range[0])/28>8192)
    throw std::runtime_error("invalid fixed static instance vector");
  if(range[0]!=range[1]) reader.Bytes(range[0],range[1]-range[0]);
  const auto parameter=reader.Word(reader.Add(owner,F::parameter));
  const auto world_data=parameter?reader.Word(reader.Add(parameter,4)):0;
  std::vector<NativeSceneSources::Part> parts;
  for(uint32_t at=range[0],part=0;at<range[1];at+=28,++part) {
    NativeSceneSources::Part value{at,0,part,world_data,reader.Word(reader.Add(at,8))};
    const auto parameters=ReadInstanceParameters(reader,at);
    if(world_data && parameters.size()==1 && parameters[0].count==4 && parameters[0].data==world_data)
      value.world_first=parameters[0].first;
    parts.push_back(std::move(value));
  }
  return parts;
}
// The owner's parts by its kind (Born's fixed).
template<class Reader>
std::vector<NativeSceneSources::Part> ReadNativeSceneOwnerParts(const Reader& reader,uint32_t owner,bool fixed) {
  return fixed?ReadNativeFixedSceneParts(reader,owner):ReadNativeStaticSceneParts(reader,owner);
}
// 821BEDF0/821BED40 transpose the object's row-major matrix into the LOD's
// float4 register stream. Keep the exact encoded bytes and immutable ownership.
template<class Reader>
NativeSceneSources::World ReadNativeStaticWorld(const Reader& reader,uint32_t owner) {
  const auto* matrix=reader.Bytes(reader.Add(owner,224),64);
  NativeSceneSources::World registers;
  for(size_t row=0;row<4;++row) for(size_t col=0;col<4;++col)
    std::memcpy(registers.data()+(col*4+row)*4,matrix+(row*4+col)*4,4);
  return registers;
}
}
