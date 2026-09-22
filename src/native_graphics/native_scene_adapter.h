#pragma once
#include "native_scene_bindings.h"
#include "native_scene_sources.h"
#include "native_scene_material.h"
#include "native_scene_geometry.h"
#include "native_scene_pass_inputs.h"
#include "native_scene_membership.h"
#include "native_scene_tree_publication.h"
#include <functional>

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
struct NativeScenePublication {
  std::shared_ptr<const NativeSceneSources> sources;
  std::shared_ptr<const NativeSceneMembership::Publication> membership;
  NativeSceneTreePublications::Images trees;
  std::shared_ptr<const NativeSceneSnapshot> snapshot;
  std::vector<std::shared_ptr<const NativeSceneInstance>> by_id;
  std::map<std::array<uint64_t,4>,std::shared_ptr<const NativeSceneInstance>> by_source;
  // Assets can exist before any material or visible object has been captured.
  std::vector<std::shared_ptr<const NativeSceneGroupGeometry>> group_geometry;
  std::vector<std::shared_ptr<const NativeSceneGroupMaterial>> group_materials;
  std::map<uint32_t,NativeScenePassAnimation> world_animations;
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
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> RetainGeometry(
    std::shared_ptr<NativeRenderBackend> backend,const NativeIndexedMesh& mesh,
    uint32_t first,uint32_t count,int32_t base=0);
  void PublishGroupGeometry(uint32_t group,uint64_t revision,
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry,
    std::optional<NativeSceneGeometrySource> setup={});
  std::shared_ptr<const NativeSceneGroupGeometry> GroupGeometry(uint32_t group,uint64_t revision) const;
  void RetireGroupGeometry(uint32_t group) { group_geometry_.erase(group); }
  void PruneGroupGeometry(const NativeSceneSources& sources);
  size_t geometry_groups() const { return group_geometry_.size(); }
  void PublishGroupMaterial(uint32_t group,uint64_t revision,std::shared_ptr<const NativeSceneMaterialProgram> program,
    std::vector<NativeSceneMaterialInputs::Constant> constants);
  std::shared_ptr<const NativeSceneGroupMaterial> GroupMaterial(uint32_t group,uint64_t revision) const;
  void RetireGroupMaterial(uint32_t group) { group_material_programs_.erase(group); }
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
  void Retire(uint32_t owner);
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
 private:
  using Key=std::array<uint64_t,4>;
  using GeometryKey=std::array<uint64_t,6>;
  NativeSceneDatabase scene_;
  std::shared_ptr<const NativeScenePassCameras> cameras_=std::make_shared<const NativeScenePassCameras>();
  std::map<uint32_t,NativeScenePassAnimation> world_animations_;
  std::shared_ptr<const WorldAnimations> world_animation_publication_=std::make_shared<const WorldAnimations>();
  std::atomic<std::shared_ptr<const NativeScenePublication>> publication_;
  std::map<Key,uint64_t> objects_;
  std::map<GeometryKey,std::weak_ptr<const NativeIndexedMesh::RetainedDraw>> geometry_;
  std::map<uint32_t,std::shared_ptr<const NativeSceneGroupGeometry>> group_geometry_;
  std::map<uint32_t,std::shared_ptr<const NativeSceneGroupMaterial>> group_material_programs_;
  std::map<uint64_t,std::vector<std::weak_ptr<const NativeSceneMaterial>>> materials_;
  std::map<uint32_t,std::weak_ptr<const NativeSceneMaterial>> group_materials_;
  struct PopulatedGroup {
    uint64_t revision=0;
    std::weak_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
    std::weak_ptr<const NativeSceneMaterial> material;
    std::vector<uint32_t> owners;
  };
  std::map<uint32_t,PopulatedGroup> populated_groups_;
  uint64_t observations_=0;
  void Prune();
  // Returns the retained material equivalent to this one, registering it if
  // there is none, so that equal materials share one object.
  std::shared_ptr<const NativeSceneMaterial> InternMaterial(std::shared_ptr<const NativeSceneMaterial> material);
};
}
