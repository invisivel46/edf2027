#include "native_scene_adapter.h"
#include "native_queued_scene.h"

namespace edf::native {
void NativeSceneAdapter::PublishGroupMaterial(uint32_t group,uint64_t revision,std::shared_ptr<const NativeSceneMaterialProgram> program,
    std::vector<NativeSceneMaterialInputs::Constant> constants) {
  if(!group || !revision || !program) throw std::runtime_error("invalid native material program publication");
  const auto previous=GroupMaterial(group,revision);
  if(previous && previous->program==program && previous->constants==constants) return;
  group_material_programs_[group]=std::make_shared<const NativeSceneGroupMaterial>(group,revision,std::move(program),std::move(constants));
}
std::shared_ptr<const NativeSceneGroupMaterial> NativeSceneAdapter::GroupMaterial(uint32_t group,uint64_t revision) const {
  const auto found=group_material_programs_.find(group);
  return found!=group_material_programs_.end() && found->second->revision==revision?found->second:nullptr;
}
void NativeSceneAdapter::PublishGroupGeometry(uint32_t group,uint64_t revision,
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry,std::optional<NativeSceneGeometrySource> setup) {
  if(!group || !revision || !geometry) throw std::runtime_error("invalid native scene geometry publication");
  const auto old=GroupGeometry(group,revision);
  if(old && old->geometry==geometry && old->setup==setup) return;
  group_geometry_[group]=std::make_shared<const NativeSceneGroupGeometry>(group,revision,std::move(geometry),std::move(setup));
}
std::shared_ptr<const NativeSceneGroupGeometry> NativeSceneAdapter::GroupGeometry(uint32_t group,uint64_t revision) const {
  const auto found=group_geometry_.find(group);
  return found!=group_geometry_.end() && found->second->revision==revision?found->second:nullptr;
}
void NativeSceneAdapter::PruneGroupGeometry(const NativeSceneSources& sources) {
  std::erase_if(group_material_programs_,[&](const auto& entry) {
    const auto* group=sources.FindGroup(entry.first);
    return !group || group->revision!=entry.second->revision;
  });
  std::erase_if(group_geometry_,[&](const auto& entry) {
    const auto* group=sources.FindGroup(entry.first);
    return !group || group->revision!=entry.second->revision;
  });
}
NativeSceneAdapter::Population NativeSceneAdapter::PopulateGroup(const NativeSceneSources& sources,uint32_t address,
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry,NativeSceneMaterialCapture capture,
    const std::function<bool(const NativeSceneSources::Part&)>& eligible) {
  Population result;
  const auto* group=sources.FindGroup(address);
  if(!group || !geometry || !capture.material || geometry->backend()!=capture.material->backend()) return result;
  // Callers resolve a fresh capture every frame. Intern it first so that an
  // unchanged material compares identical to the one recorded below; comparing
  // the fresh object re-populated every part of every group on every frame.
  capture.material=InternMaterial(std::move(capture.material));
  const auto prior=populated_groups_.find(address);
  if(prior!=populated_groups_.end() && prior->second.revision==group->revision &&
     prior->second.geometry.lock()==geometry && prior->second.material.lock()==capture.material) return result;
  size_t matrices=0; bool column=false;
  for(const auto& constant:capture.material->constants()) for(const auto& matrix:constant.matrices)
    if(matrix.source==NativeSceneMatrixSource::World) {
      if(constant.stage!=NativeBackendStage::Vertex) return result;
      ++matrices; column=matrix.column_major;
    }
  if(matrices!=1) return result;
  std::vector<uint32_t> owners;
  owners.reserve(group->parts.size());
  for(const auto& [instance,part]:group->parts) {
    ++result.examined;
    const auto* source=sources.Find(instance);
    if(source) owners.push_back(source->owner);
    const auto world=source?sources.WorldRegisters(*source,part.world_data):nullptr;
    if(!world || !eligible(part)) { ++result.rejected; continue; }
    capture.world=DecodeNativeQueuedWorld(*world,column);
    const bool missing=!objects_.contains(Key{source->owner,source->generation,source->lod,source->part});
    Observe(*source,geometry,capture);
    result.created+=missing;
  }
  // Rejected parts remain on their existing route until membership/assets are
  // replaced. World changes are delivered separately by UpdateWorld events.
  std::sort(owners.begin(),owners.end());
  owners.erase(std::unique(owners.begin(),owners.end()),owners.end());
  populated_groups_[address]={group->revision,geometry,capture.material,std::move(owners)};
  return result;
}
std::shared_ptr<const NativeSceneInstance> NativeScenePublication::Find(uint64_t id) const {
  const auto at=std::lower_bound(by_id.begin(),by_id.end(),id,
    [](const auto& instance,uint64_t key) { return instance->id<key; });
  return at!=by_id.end() && (*at)->id==id?*at:nullptr;
}
std::shared_ptr<const NativeSceneInstance> NativeScenePublication::Resolve(
    const NativeSceneSources::Source& source,
    const std::shared_ptr<const NativeIndexedMesh::RetainedDraw>& geometry,
    const NativeSceneMaterialCapture& capture) const {
  const auto found=by_source.find({source.owner,source.generation,source.lod,source.part});
  if(found==by_source.end() || !geometry || !capture.material ||
     found->second->object.geometry!=geometry || geometry->backend()!=capture.material->backend()) return {};
  const auto& retained=found->second;
  if(retained->object.material==capture.material && retained->object.world==capture.world &&
     retained->previous==capture.world) return retained;
  auto resolved=std::make_shared<NativeSceneInstance>(*retained);
  resolved->object.material=capture.material; resolved->object.world=capture.world;
  // The pass already chose this exact world sample. Do not interpolate from
  // an unrelated material capture or previous visibility observation.
  resolved->previous=capture.world;
  return resolved;
}
std::shared_ptr<const NativeScenePublication> NativeSceneAdapter::Publish(uint64_t tick,
    std::shared_ptr<const NativeSceneSources> sources,
    std::shared_ptr<const NativeSceneMembership::Publication> membership,
    NativeSceneTreePublications::Images trees) {
  auto publication=std::make_shared<NativeScenePublication>();
  publication->sources=std::move(sources);
  publication->membership=std::move(membership);
  publication->trees=std::move(trees);
  publication->snapshot=scene_.Publish(tick);
  publication->world_animations=world_animations_;
  publication->cameras=cameras_;
  for(const auto& [group,geometry]:group_geometry_) publication->group_geometry.push_back(geometry);
  for(const auto& [group,material]:group_material_programs_) publication->group_materials.push_back(material);
  publication->by_id=publication->snapshot->instances;
  std::sort(publication->by_id.begin(),publication->by_id.end(),
    [](const auto& a,const auto& b) { return a->id<b->id; });
  for(const auto& [source,id]:objects_)
    if(auto instance=publication->Find(id)) publication->by_source.emplace(source,std::move(instance));
  publication_.store(publication);
  return publication;
}
size_t NativeSceneAdapter::UpdateWorld(uint32_t owner,uint64_t generation,const NativeSceneSources::World& registers) {
  if(!generation) return 0;
  size_t updated=0;
  for(auto at=objects_.lower_bound(Key{owner,generation,0,0});
      at!=objects_.end() && at->first[0]==owner && at->first[1]==generation;++at) {
    auto object=scene_.SelectOne(at->second)->object;
    size_t matrices=0;
    bool supported=true,column_major=false;
    for(const auto& constant:object.material->constants()) for(const auto& matrix:constant.matrices)
      if(matrix.source==NativeSceneMatrixSource::World) {
        ++matrices;
        supported&=constant.stage==NativeBackendStage::Vertex;
        column_major=matrix.column_major;
      }
    if(!supported || matrices!=1) continue;
    object.world=DecodeNativeQueuedWorld(registers,column_major);
    scene_.Update(at->second,std::move(object));
    ++updated;
  }
  return updated;
}
uint64_t NativeSceneAdapter::Observe(const NativeSceneSources::Source& source,
    std::shared_ptr<NativeRenderBackend> backend,const NativeIndexedMesh& mesh,
    uint32_t first,uint32_t count,int32_t base,NativeSceneMaterialCapture capture) {
  if(!source.generation || !source.owner || !capture.material)
    throw std::runtime_error("native scene observation has no audited lifetime/material");
  return Observe(source,RetainGeometry(std::move(backend),mesh,first,count,base),std::move(capture));
}
std::shared_ptr<const NativeIndexedMesh::RetainedDraw> NativeSceneAdapter::RetainGeometry(
    std::shared_ptr<NativeRenderBackend> backend,const NativeIndexedMesh& mesh,
    uint32_t first,uint32_t count,int32_t base) {
  const GeometryKey geometry_key{uintptr_t(mesh.VertexStorage().get()),uintptr_t(mesh.IndexStorage().get()),
    mesh.input_layout().fingerprint(),first,count,uint32_t(base)};
  auto& weak_geometry=geometry_[geometry_key];
  auto geometry=weak_geometry.lock();
  if(geometry && geometry->backend()!=backend.get()) throw std::runtime_error("native geometry backend mismatch");
  if(!geometry) {
    geometry=std::make_shared<const NativeIndexedMesh::RetainedDraw>(mesh.RetainDraw(backend,first,count,base));
    weak_geometry=geometry;
  }
  return geometry;
}
std::shared_ptr<const NativeSceneMaterial> NativeSceneAdapter::InternMaterial(
    std::shared_ptr<const NativeSceneMaterial> material) {
  auto& candidates=materials_[material->fingerprint()];
  for(auto at=candidates.begin();at!=candidates.end();) {
    const auto existing=at->lock();
    if(!existing) { at=candidates.erase(at); continue; }
    if(existing==material || existing->Equivalent(*material)) return existing;
    ++at;
  }
  candidates.push_back(material);
  return material;
}
uint64_t NativeSceneAdapter::Observe(const NativeSceneSources::Source& source,
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry,NativeSceneMaterialCapture capture) {
  if(!source.generation || !source.owner || !geometry || !capture.material ||
     geometry->backend()!=capture.material->backend())
    throw std::runtime_error("native scene observation has no compatible retained assets/lifetime");
  if((++observations_&4095)==0) Prune();
  capture.material=InternMaterial(std::move(capture.material));
  NativeSceneObject object; object.geometry=std::move(geometry);
  object.material=std::move(capture.material); object.world=capture.world;
  const Key key{source.owner,source.generation,source.lod,source.part};
  const auto found=objects_.find(key);
  if(found!=objects_.end()) { scene_.Update(found->second,std::move(object)); return found->second; }
  const auto id=scene_.Create(std::move(object));
  try { objects_.emplace(key,id); } catch(...) { scene_.Remove(id); throw; }
  return id;
}
void NativeSceneAdapter::Retire(uint32_t owner) {
  // Model replacement can keep the same source addresses and group revision.
  std::erase_if(populated_groups_,[&](const auto& entry) {
    return std::binary_search(entry.second.owners.begin(),entry.second.owners.end(),owner);
  });
  bool removed=false;
  for(auto at=objects_.lower_bound(Key{owner,0,0,0});at!=objects_.end() && at->first[0]==owner;) {
    scene_.Remove(at->second); at=objects_.erase(at); removed=true;
  }
  if(removed) Prune();
}
void NativeSceneAdapter::Prune() {
  std::erase_if(populated_groups_,[](const auto& entry) {
    return entry.second.geometry.expired() || entry.second.material.expired();
  });
  std::erase_if(group_materials_,[](const auto& entry) { return entry.second.expired(); });
  std::erase_if(geometry_,[](const auto& entry) { return entry.second.expired(); });
  for(auto at=materials_.begin();at!=materials_.end();) {
    std::erase_if(at->second,[](const auto& material) { return material.expired(); });
    if(at->second.empty()) at=materials_.erase(at); else ++at;
  }
}
std::shared_ptr<const NativeSceneMaterial> NativeSceneAdapter::PreviousGroupMaterial(uint32_t group) const {
  const auto found=group_materials_.find(group);
  return found==group_materials_.end()?nullptr:found->second.lock();
}
void NativeSceneAdapter::RememberGroupMaterial(uint32_t group,const std::shared_ptr<const NativeSceneMaterial>& material) {
  if(!group || !material) return;
  // Hints do not own assets, and obsolete group addresses cannot grow this
  // index indefinitely even while older scene snapshots are still retained.
  if(group_materials_.size()>=16384 && !group_materials_.contains(group)) group_materials_.erase(group_materials_.begin());
  group_materials_[group]=material;
}
}
