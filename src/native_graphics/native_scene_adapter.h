#pragma once
#include "native_scene_bindings.h"
#include "native_scene_sources.h"
#include "native_scene_material.h"
#include <functional>

namespace edf::native {
struct NativeSceneGroupGeometry {
  uint32_t group=0;
  uint64_t revision=0;
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
};
struct NativeSceneGroupMaterial {
  uint32_t group=0;
  uint64_t revision=0;
  std::shared_ptr<const NativeSceneMaterialProgram> program;
};
struct NativeScenePublication {
  std::shared_ptr<const NativeSceneSnapshot> snapshot;
  std::vector<std::shared_ptr<const NativeSceneInstance>> by_id;
  // Assets can exist before any material or visible object has been captured.
  std::vector<std::shared_ptr<const NativeSceneGroupGeometry>> group_geometry;
  std::vector<std::shared_ptr<const NativeSceneGroupMaterial>> group_materials;
  std::shared_ptr<const NativeSceneInstance> Find(uint64_t id) const;
};
// Persistent native objects keyed by audited guest lifetimes. Weak asset indexes
// deduplicate allocations without keeping a retired level alive.
class NativeSceneAdapter {
 public:
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> RetainGeometry(
    std::shared_ptr<NativeRenderBackend> backend,const NativeIndexedMesh& mesh,
    uint32_t first,uint32_t count,int32_t base=0);
  void PublishGroupGeometry(uint32_t group,uint64_t revision,
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry);
  std::shared_ptr<const NativeSceneGroupGeometry> GroupGeometry(uint32_t group,uint64_t revision) const;
  void RetireGroupGeometry(uint32_t group) { group_geometry_.erase(group); }
  void PruneGroupGeometry(const NativeSceneSources& sources);
  size_t geometry_groups() const { return group_geometry_.size(); }
  void PublishGroupMaterial(uint32_t group,uint64_t revision,std::shared_ptr<const NativeSceneMaterialProgram> program);
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
  std::shared_ptr<const NativeScenePublication> Publish(uint64_t tick);
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
};
}
