#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <vector>
#include <memory>
#include <optional>
#include "guest_block.h"
#include "native_scene_visibility.h"

namespace edf::native {
// Constructor/destructor events establish lifetime. Observation cannot create
// an owner: a guest address alone is not a persistent scene identity.
class NativeSceneSources {
 public:
  struct Part {
    uint32_t instance=0,lod=0,part=0,world_data=0,group=0;
    bool operator==(const Part&) const=default;
  };
  struct Source {
    uint64_t generation=0;
    uint32_t owner=0,lod=0,part=0,world_data=0;
    bool operator==(const Source&) const=default;
  };
  struct Group {
    uint64_t revision=0;
    std::map<uint32_t,Part> parts;
  };
  const Group* FindGroup(uint32_t group) const {
    const auto found=groups_.find(group);
    return found==groups_.end()?nullptr:&found->second;
  }
  const auto& Groups() const { return groups_; }
  uint64_t Born(uint32_t owner) {
    if(!owner || next_==UINT64_MAX) throw std::runtime_error("invalid native scene source lifetime");
    Retire(owner);
    owners_.emplace(owner,Owner{next_++,{}});
    return owners_.at(owner).generation;
  }
  bool Retire(uint32_t owner) {
    const auto found=owners_.find(owner);
    if(found==owners_.end()) return false;
    for(const auto& part:found->second.parts) { RemoveGroupPart(part); parts_.erase(part.instance); }
    owners_.erase(found); return true;
  }
  bool Observe(uint32_t owner,std::span<const Part> parts) {
    const auto found=owners_.find(owner);
    if(found==owners_.end()) return false;
    if(std::equal(parts.begin(),parts.end(),found->second.parts.begin(),found->second.parts.end())) return true;
    std::map<uint32_t,Source> replacements;
    std::array<std::vector<Part>,3> lod_parts;
    for(const auto& part:parts) {
      if(part.lod>=3) throw std::runtime_error("invalid native static LOD");
      lod_parts[part.lod].push_back(part);
      if(!part.instance || !replacements.emplace(part.instance,Source{found->second.generation,owner,part.lod,part.part,part.world_data}).second)
        throw std::runtime_error("duplicate native scene source part");
      const auto existing=parts_.find(part.instance);
      if(existing!=parts_.end() && existing->second.owner!=owner)
        throw std::runtime_error("native scene part has two live owners");
    }
    std::vector<Part> retained(parts.begin(),parts.end());
    for(const auto& old:found->second.parts) { RemoveGroupPart(old); parts_.erase(old.instance); }
    parts_.merge(replacements);
    for(const auto& part:retained) if(part.group) {
      auto& group=groups_[part.group];
      group.parts.emplace(part.instance,part); group.revision=++group_revision_;
    }
    found->second.parts=std::move(retained);
    found->second.lod_parts=std::move(lod_parts);
    return true;
  }
  const Source* Find(uint32_t instance) const {
    const auto found=parts_.find(instance); return found==parts_.end()?nullptr:&found->second;
  }
  bool HasOwner(uint32_t owner) const { return owners_.contains(owner); }
  uint64_t Generation(uint32_t owner) const {
    const auto found=owners_.find(owner);
    return found==owners_.end()?0:found->second.generation;
  }
  std::optional<std::span<const Part>> LodParts(uint32_t descriptor) const {
    for(uint32_t lod=0;lod<3;++lod) {
      const uint32_t offset=408+lod*44;
      if(descriptor<offset) continue;
      const auto found=owners_.find(descriptor-offset);
      if(found!=owners_.end()) return found->second.lod_parts[lod];
    }
    return {};
  }
  using World=std::array<uint8_t,64>;
  bool PublishVisibility(uint32_t owner,const NativeSceneVisibility& visibility) {
    const auto found=owners_.find(owner);
    if(found==owners_.end()) return false;
    if(!found->second.visibility || *found->second.visibility!=visibility)
      found->second.visibility=std::make_shared<const NativeSceneVisibility>(visibility);
    return true;
  }
  std::shared_ptr<const NativeSceneVisibility> Visibility(uint32_t owner) const {
    const auto found=owners_.find(owner);
    return found==owners_.end()?nullptr:found->second.visibility;
  }
  bool PublishWorld(uint32_t owner,const World& registers) {
    const auto found=owners_.find(owner);
    if(found==owners_.end()) return false;
    if(!found->second.world || *found->second.world!=registers)
      found->second.world=std::make_shared<const World>(registers);
    return true;
  }
  std::shared_ptr<const World> WorldRegisters(const Source& source,uint32_t data) const {
    if(!data || source.world_data!=data) return {};
    const auto found=owners_.find(source.owner);
    if(found==owners_.end() || found->second.generation!=source.generation) return {};
    return found->second.world;
  }
  size_t owners() const { return owners_.size(); }
  size_t parts() const { return parts_.size(); }
 private:
  struct Owner {
    uint64_t generation; std::vector<Part> parts; std::shared_ptr<const World> world;
    std::array<std::vector<Part>,3> lod_parts;
    std::shared_ptr<const NativeSceneVisibility> visibility;
  };
  std::map<uint32_t,Owner> owners_;
  std::map<uint32_t,Source> parts_;
  std::map<uint32_t,Group> groups_;
  uint64_t group_revision_=0;
  void RemoveGroupPart(const Part& part) {
    if(!part.group) return;
    const auto found=groups_.find(part.group);
    if(found==groups_.end()) return;
    found->second.parts.erase(part.instance);
    if(found->second.parts.empty()) groups_.erase(found);
    else found->second.revision=++group_revision_;
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
    for(uint32_t at=range[0],part=0;at<range[1];at+=28,++part)
      parts.push_back({at,lod,part,world_data,reader.Word(reader.Add(at,8))});
  }
  return parts;
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
