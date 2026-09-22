#pragma once
#include "native_scene_bindings.h"
#include "native_scene_sources.h"
#include "native_scene_material.h"
#include "native_scene_geometry.h"
#include "native_scene_pass_inputs.h"
#include "native_scene_membership.h"
#include "native_scene_tree_publication.h"
#include <functional>
#include <set>

namespace edf::native {
struct NativeSceneGroupGeometry {
  uint32_t group=0;
  uint64_t revision=0;
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  std::optional<NativeSceneGeometrySource> setup;
};
struct NativeSceneGroupMaterial {
  uint32_t group=0;
  uint64_t revision=0;
  std::shared_ptr<const NativeSceneMaterialProgram> program;
  std::vector<NativeSceneMaterialInputs::Constant> constants;
};
// Walk order of one world owner's owner+240 group list (821C3BB8).
using NativeSceneGroupOrder=std::vector<uint32_t>;
using NativeSceneGroupOrders=NativeSharedMap<uint32_t,std::shared_ptr<const NativeSceneGroupOrder>>;
struct NativeSceneGroupOrderAudit { uint64_t checks=0,mismatches=0,missing=0; };
struct NativeSceneGroupKey {
  template<class T> uint32_t operator()(const std::shared_ptr<const T>& value) const { return value->group; }
};
// Every index shares the chunks no tick has changed with the previous
// publication; building one costs what changed, and none is written once built.
struct NativeScenePublication {
  std::shared_ptr<const NativeSceneSources> sources;
  std::shared_ptr<const NativeSceneMembership::Publication> membership;
  NativeSceneTreePublications::Images trees;
  std::shared_ptr<const NativeSceneSnapshot> snapshot;
  NativeSceneInstances by_id;
  NativeSharedMap<std::array<uint64_t,4>,std::shared_ptr<const NativeSceneInstance>> by_source;
  // Assets can exist before any material or visible object has been captured.
  // Ordered by group address.
  NativeSharedVector<std::shared_ptr<const NativeSceneGroupGeometry>> group_geometry;
  NativeSharedVector<std::shared_ptr<const NativeSceneGroupMaterial>> group_materials;
  std::map<uint32_t,NativeScenePassAnimation> world_animations;
  NativeSceneGroupOrders group_order;
  std::shared_ptr<const NativeScenePassCameras> cameras;
  std::shared_ptr<const NativeSceneInstance> Find(uint64_t id) const;
  // Select a retained lifetime and apply pass-resolved material/world without
  // observing, updating or consulting the producer's current scene database.
  std::shared_ptr<const NativeSceneInstance> Resolve(const NativeSceneSources::Source& source,
    const std::shared_ptr<const NativeIndexedMesh::RetainedDraw>& geometry,
    const NativeSceneMaterialCapture& capture) const;
};
// Persistent native objects keyed by audited guest lifetimes. Weak asset indexes
// deduplicate allocations without keeping a retired level alive.
class NativeSceneAdapter {
 public:
  void PublishCameras(NativeScenePassCameras cameras) {
    if(*cameras_!=cameras) cameras_=std::make_shared<const NativeScenePassCameras>(std::move(cameras));
  }
  std::shared_ptr<const NativeScenePassCameras> AcquireCameras() const { return cameras_; }
  using WorldAnimations=std::map<uint32_t,NativeScenePassAnimation>;
  void PublishWorldAnimation(uint32_t owner,NativeScenePassAnimation value) {
    const auto old=world_animations_.find(owner);
    if(old!=world_animations_.end() && old->second==value) return;
    world_animations_[owner]=value;
    world_animation_publication_=std::make_shared<const WorldAnimations>(world_animations_);
  }
  void RetireWorldAnimation(uint32_t owner) {
    if(world_animations_.erase(owner))
      world_animation_publication_=std::make_shared<const WorldAnimations>(world_animations_);
  }
  // Animation writers can advance between scene publications. Acquire their
  // immutable generation at render entry under the same producer/reader lock.
  std::shared_ptr<const WorldAnimations> AcquireWorldAnimations() const { return world_animation_publication_; }
  // Keeps the previous immutable order when the walk is unchanged; returns
  // whether a new order was published. Cost is linear in the list length.
  bool PublishGroupOrder(uint32_t owner,std::span<const uint32_t> order) {
    const auto* slot=group_orders_.Find(owner);
    if(slot && *slot && std::ranges::equal(**slot,order)) return false;
    group_orders_.Set(owner,std::make_shared<const NativeSceneGroupOrder>(order.begin(),order.end()));
    return true;
  }
  void RetireGroupOrder(uint32_t owner) { group_orders_.Erase(owner); }
  std::shared_ptr<const NativeSceneGroupOrder> GroupOrder(uint32_t owner) const {
    const auto* found=group_orders_.Find(owner);
    return found?*found:nullptr;
  }
  // Compares the latest published order with a live walk of the same list.
  bool AuditGroupOrder(uint32_t owner,std::span<const uint32_t> live) {
    ++group_order_audit_.checks;
    const auto order=GroupOrder(owner);
    if(!order) { ++group_order_audit_.missing; return false; }
    if(std::ranges::equal(*order,live)) return true;
    ++group_order_audit_.mismatches; return false;
  }
  const NativeSceneGroupOrderAudit& group_order_audit() const { return group_order_audit_; }
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> RetainGeometry(
    std::shared_ptr<NativeRenderBackend> backend,const NativeIndexedMesh& mesh,
    uint32_t first,uint32_t count,int32_t base=0);
  void PublishGroupGeometry(uint32_t group,uint64_t revision,
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry,
    std::optional<NativeSceneGeometrySource> setup={});
  std::shared_ptr<const NativeSceneGroupGeometry> GroupGeometry(uint32_t group,uint64_t revision) const;
  void RetireGroupGeometry(uint32_t group) { group_geometry_.Erase(group,NativeSceneGroupKey{}); }
  void PruneGroupGeometry(const NativeSceneSources& sources);
  size_t geometry_groups() const { return group_geometry_.size(); }
  void PublishGroupMaterial(uint32_t group,uint64_t revision,std::shared_ptr<const NativeSceneMaterialProgram> program,
    std::vector<NativeSceneMaterialInputs::Constant> constants);
  std::shared_ptr<const NativeSceneGroupMaterial> GroupMaterial(uint32_t group,uint64_t revision) const;
  void RetireGroupMaterial(uint32_t group) { group_material_programs_.Erase(group,NativeSceneGroupKey{}); }
  size_t material_groups() const { return group_material_programs_.size(); }
  struct Population { size_t examined=0,created=0,rejected=0; };
  // Populate registered parts even if visibility has never selected them.
  // The caller certifies that the shared group capture has no instance-specific
  // overrides except the complete world matrix; eligibility validates each part.
  Population PopulateGroup(const NativeSceneSources& sources,uint32_t group,
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry,
    NativeSceneMaterialCapture capture,
    const std::function<bool(const NativeSceneSources::Part&)>& eligible);
  uint64_t Observe(const NativeSceneSources::Source& source,
    std::shared_ptr<NativeRenderBackend> backend,const NativeIndexedMesh& mesh,
    uint32_t first,uint32_t count,int32_t base,NativeSceneMaterialCapture capture);
  uint64_t Observe(const NativeSceneSources::Source& source,
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry,NativeSceneMaterialCapture capture);
  // Cost follows the owner's own groups and objects, not the whole scene.
  void Retire(uint32_t owner);
  bool HasOwner(uint32_t owner) const {
    const auto at=objects_.lower_bound(Key{owner,0,0,0});
    return (at!=objects_.end() && at->first[0]==owner) || owner_groups_.contains(owner);
  }
  size_t retire_checks() const { return retire_checks_; }
  size_t populated_groups() const { return populated_groups_.size(); }
  size_t UpdateWorld(uint32_t owner,uint64_t generation,const NativeSceneSources::World& registers);
  std::shared_ptr<const NativeScenePublication> Publish(uint64_t tick,
    std::shared_ptr<const NativeSceneSources> sources={},
    std::shared_ptr<const NativeSceneMembership::Publication> membership={},
    NativeSceneTreePublications::Images trees={});
  std::shared_ptr<const NativeScenePublication> AcquirePublication() const { return publication_.load(); }
  // Lookup hint only. CaptureNativeSceneMaterial must validate live bindings
  // before reuse; a recycled guest group address never establishes identity.
  std::shared_ptr<const NativeSceneMaterial> PreviousGroupMaterial(uint32_t group) const;
  void RememberGroupMaterial(uint32_t group,const std::shared_ptr<const NativeSceneMaterial>& material);
  NativeSceneSnapshot Select(std::span<const uint64_t> ids) { return scene_.Select(ids); }
  std::shared_ptr<const NativeSceneInstance> SelectOne(uint64_t id) { return scene_.SelectOne(id); }
  size_t objects() const { return objects_.size(); }
  // Returns the retained material equivalent to this one, registering it if
  // there is none, so that equal materials share one object.
  std::shared_ptr<const NativeSceneMaterial> InternMaterial(std::shared_ptr<const NativeSceneMaterial> material);
 private:
  using Key=std::array<uint64_t,4>;
  using GeometryKey=std::array<uint64_t,6>;
  NativeSceneDatabase scene_;
  std::shared_ptr<const NativeScenePassCameras> cameras_=std::make_shared<const NativeScenePassCameras>();
  std::map<uint32_t,NativeScenePassAnimation> world_animations_;
  std::shared_ptr<const WorldAnimations> world_animation_publication_=std::make_shared<const WorldAnimations>();
  NativeSceneGroupOrders group_orders_;
  NativeSceneGroupOrderAudit group_order_audit_;
  std::atomic<std::shared_ptr<const NativeScenePublication>> publication_;
  std::map<Key,uint64_t> objects_;
  // by_source as last published, and the keys whose object may differ from it.
  NativeSharedMap<Key,std::shared_ptr<const NativeSceneInstance>> by_source_;
  std::set<Key> changed_sources_;
  void SourceChanged(const Key& key) { changed_sources_.insert(key); }
  void SourceRemoved(const Key& key) { if(by_source_.contains(key)) changed_sources_.insert(key); else changed_sources_.erase(key); }
  std::map<GeometryKey,std::weak_ptr<const NativeIndexedMesh::RetainedDraw>> geometry_;
  NativeSharedVector<std::shared_ptr<const NativeSceneGroupGeometry>> group_geometry_;
  NativeSharedVector<std::shared_ptr<const NativeSceneGroupMaterial>> group_material_programs_;
  std::map<uint64_t,std::vector<std::weak_ptr<const NativeSceneMaterial>>> materials_;
  std::map<uint32_t,std::weak_ptr<const NativeSceneMaterial>> group_materials_;
  struct PopulatedGroup {
    uint64_t revision=0;
    std::weak_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
    std::weak_ptr<const NativeSceneMaterial> material;
    std::vector<uint32_t> owners;
    // Parts the live eligibility check turned down, and unchanged visits since.
    size_t rejected=0;
    uint32_t skips=0;
  };
  std::map<uint32_t,PopulatedGroup> populated_groups_;
  // Owner -> groups populated with it. Entries may be stale (group since
  // repopulated or pruned); Retire verifies against the group's owners.
  std::map<uint32_t,std::set<uint32_t>> owner_groups_;
  uint64_t observations_=0;
  size_t retired_=0,retire_checks_=0;
  void Prune();
};
}
