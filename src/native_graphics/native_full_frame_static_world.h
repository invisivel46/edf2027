#pragma once
#include "native_scene_adapter.h"
#include "native_scene_tree.h"
#include "native_scene_static_walk.h"
#include "native_static_world_cache.h"
#include "native_queued_scene.h"
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace edf::native {
// The full-frame renderer's StaticWorld pass: the static opaque world's draw
// list as a function of published data and the pass camera alone. It replaces
// the guest render helper's path for these objects end to end:
//   821C61D8 octree walk      -> WalkNativeSceneTree over the published tree image
//   820B4038 per-list gather  -> SelectNativeVisibility on published visibility records
//   native_scene_queues       -> a frame-local selection (group -> instances)
//   821C3BB8 group walk       -> the published group_order
//   821D96D8 group draw       -> one resolved material per group, instanced draws
// There are no guest reads, guest queues, device mirrors, eligibility against
// the device or handoffs. Every input is a published immutable generation:
// NativeScenePublication (sources, membership, trees, group order, geometry,
// material programs) plus the route words the step publishes per owner.

// The camera one full frame renders the static world with.
//  visibility: camera+96 view matrix, camera+288 frustum and context+8 depth
//              scale, the values 821C61D8/820B4038 read (ReadNativeSceneVisibilityView).
//  pass:       the published camera (scene+32/+96/+160) the materials' camera
//              globals are replaced with (NativeScenePassCamera::Apply).
//  animation:  the published world animation constants, when a material uses them.
struct NativeFullFrameStaticCamera {
  NativeSceneVisibilityView visibility;
  NativeScenePassCamera pass;
  std::optional<NativeScenePassAnimation> animation;
};
// sub_821C0C00's route words for one owner, as of the simulation step:
// vtable+16==820B2670 (direct), +52 mode and +64 hidden. These are not in the
// scene publication yet; the step's static walk plans hold them (the full
// frame must publish them with the step: CollectNativeFullFrameStaticRoutes).
struct NativeFullFrameStaticRoute {
  bool direct=false;
  uint32_t mode=0;
  uint16_t hidden=0;
  bool operator==(const NativeFullFrameStaticRoute&) const=default;
};
using NativeFullFrameStaticRoutes=std::unordered_map<uint32_t,NativeFullFrameStaticRoute>;
inline void CollectNativeFullFrameStaticRoutes(const NativeStaticWalkList& plan,NativeFullFrameStaticRoutes& routes) {
  for(const auto& member:plan.members) routes[member.owner]={member.direct,member.mode,member.hidden};
}
// Addresses of the published tree image only: every read is a captured byte,
// and anything else throws rather than reaching guest memory.
class NativeSceneTreeImageReader {
 public:
  explicit NativeSceneTreeImageReader(const NativeSceneTreeImage& image):image_(image) {}
  uint32_t Add(uint32_t address,size_t size) const { return uint32_t(address+size); }
  const uint8_t* Bytes(uint32_t address,size_t size) const {
    ++reads;
    if(const auto* bytes=image_.Find(address,size)) return bytes;
    throw std::runtime_error("native full-frame tree read outside its published image");
  }
  uint32_t Word(uint32_t address) const { return GuestBlockWord(Bytes(address,4)); }
  mutable uint64_t reads=0;
 private:
  const NativeSceneTreeImage& image_;
};

// One world owner's frame selection, in the order 821C3BB8 draws it: groups in
// the published group order, each group's instances in queue order (the guest
// links each selection at the group's head, so the last selected draws first).
struct NativeFullFrameStaticGroup {
  uint32_t group=0;
  std::vector<uint32_t> instances;
};
struct NativeFullFrameStaticOwner {
  uint32_t owner=0;
  std::vector<NativeFullFrameStaticGroup> groups;
};
struct NativeFullFrameStaticSelection {
  struct Object { uint32_t owner=0,lod=0; bool operator==(const Object&) const=default; };
  struct Stats {
    uint64_t worlds=0,missing_orders=0,nodes_classified=0,tree_reads=0,lists=0,missing_lists=0,members=0,duplicates=0;
    uint64_t unrouted=0,not_direct=0,unpublished=0,culled_distance=0,culled_frustum=0,visible=0,missing_lod=0,undrawable=0;
    uint64_t selected=0,parts=0,unordered_groups=0,unordered_parts=0;
  };
  std::vector<NativeFullFrameStaticOwner> owners;
  // Every object selected natively, in walk order, with the LOD it chose.
  std::vector<Object> objects;
  Stats stats;
};
// 821C61D8 + 820B4038 + 821C3BB8's group order over published data. Per world
// owner with a tree image: walk the tree with frustum classification from the
// camera; gather each accepted leaf list (node+120), then world+372, from the
// published membership; visit each owner once (the guest's +48 frame marker);
// route by the published words (hidden, then mode 0 with direct 820B2670
// dispatch; buckets, virtual and unknown routes are other passes' objects);
// cull by the published visibility record and pick the LOD
// (SelectNativeVisibility, shared with the 820B4038 hook); push each part of
// that LOD into its group. A part without a group makes the whole object
// undrawable here, as it sends the object back to the guest in the hook.
// Selections of a group outside the owner's published order are counted and
// dropped: 821C3BB8 never reaches them.
NativeFullFrameStaticSelection SelectNativeFullFrameStaticWorld(const NativeScenePublication& publication,
  const NativeFullFrameStaticCamera& camera,const NativeFullFrameStaticRoutes& routes);

// Target formats and depth direction of the pass (the pipeline inputs a
// resolved material depends on besides its program and base state).
struct NativeFullFrameStaticTargets {
  uint32_t count=1;
  std::array<uint32_t,8> rtv_format{};
  uint32_t dsv_format=0,samples=1;
  bool reverse_depth=false;
  bool operator==(const NativeFullFrameStaticTargets&) const=default;
};
struct NativeFullFrameStaticPass {
  NativeFullFrameStaticTargets targets;
  NativeBackendViewport viewport;
  NativeBackendScissor scissor;
  int filtering=-1;
};
// The pass's base render and sampler state. Each group's state is this plus
// its own material's state operations - NOT chained from the previous group
// and never read from the guest device mirrors.
//
// Why: in the guest, a group inherits whatever the previous group (or pass)
// left on the device and 821B8E48 applies only the operations its material
// lists. Chaining in walk order would make one group's state depend on which
// groups were visible before it this frame, and chaining from the mirrors
// needs guest state. The static opaque world is drawn right after scene begin
// (821BE8D0), which forwards the scene's viewport and clears, uploads the
// camera and calls 82135530(device,1) - state operation 0x28, depth enable -
// and sets nothing else. The rest is the Xbox 360 D3D device default state,
// written here as the operations that produce it:
//   0x28=1 depth test on (scene begin)     0x30=1 depth write on
//   0x2c=3 depth func LESS_EQUAL           0x38=6 cull D3DCULL_CCW (back faces, CW front)
//   0x3c=0 blend off (words[0]=0x10001)    0x48=1/0x4c=0 ONE/ZERO
//   0xd4..0xe0=15 color writes on          alpha test, stencil, scissor off
// Samplers start from the default record: every slot a material samples is
// written by its own sampler operations (filters, LOD range, bias); addressing
// stays as the texture's. A material that relied on an inherited state it does
// not set shows up as a group-local difference in the image A/B against the
// guest path, which is what validates this choice.
NativeSceneMaterialPassState NativeFullFrameStaticBaseState(const NativeFullFrameStaticTargets& targets);
// The operations above, in order, for tests and audits.
inline constexpr std::array<std::array<uint32_t,2>,12> kNativeFullFrameStaticBaseOperations{{
  {0x28,1},{0x30,1},{0x2c,3},{0x38,6},{0x3c,0},{0x48,1},{0x4c,0},{0x60,0},{0xd4,15},{0xd8,15},{0xdc,15},{0xe0,15}}};

// One group's material under the pass base state: everything its instances
// share. camera holds only the matrices (NativeStaticCaptureCamera's).
struct NativeFullFrameStaticMaterial {
  std::shared_ptr<const NativeSceneMaterial> material;
  NativeSceneView camera;
  bool world_column_major=false;
  RenderStateWords render{};
};
// NativeSceneMaterialProgram::Resolve with the pass base state and targets,
// interned when intern is given, validated to bind exactly one vertex
// g_mWorld. Throws on anything the pass cannot draw.
NativeFullFrameStaticMaterial ResolveNativeFullFrameStaticMaterial(const NativeSceneMaterialProgram& program,
  const NativeIndexedMesh::RetainedDraw& geometry,const NativeFullFrameStaticPass& pass,
  const NativeSceneMaterialPassState& base,std::span<const NativeSceneMaterialInputs::Constant> constants,
  const std::function<std::shared_ptr<const NativeSceneMaterial>(std::shared_ptr<const NativeSceneMaterial>)>& intern={});
// The production resolver for NativeFullFrameStaticWorld::Build; pass must
// outlive it. intern is typically the adapter's InternMaterial.
inline auto NativeFullFrameStaticResolver(const NativeFullFrameStaticPass& pass,
    std::function<std::shared_ptr<const NativeSceneMaterial>(std::shared_ptr<const NativeSceneMaterial>)> intern={}) {
  return [&pass,intern=std::move(intern)](const NativeSceneGroupMaterial& group,
      const std::shared_ptr<const NativeIndexedMesh::RetainedDraw>& geometry,const NativeSceneMaterialPassState& base,
      std::span<const NativeSceneMaterialInputs::Constant> constants) {
    if(!group.program || !geometry) throw std::runtime_error("native full-frame static group has no program or geometry");
    return ResolveNativeFullFrameStaticMaterial(*group.program,*geometry,pass,base,constants,intern);
  };
}
// The group's published constants with the pass camera and animation applied
// (the pass-owned globals); throws when a material needs a missing animation.
std::vector<NativeSceneMaterialInputs::Constant> NativeFullFrameStaticConstants(const NativeSceneGroupMaterial& group,
  const NativeFullFrameStaticCamera& camera);

// One instanced draw: one group's geometry and material, every selected
// instance with its published world. instances are scene objects in draw
// order; the renderer instances adjacent objects that share geometry and
// material, which all of these do.
struct NativeFullFrameStaticDraw {
  uint32_t owner=0,group=0;
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  std::shared_ptr<const NativeSceneMaterial> material;
  NativeSceneView view;
  std::vector<std::shared_ptr<const NativeSceneInstance>> instances;
  NativeSceneSnapshot Snapshot() const { return {0,NativeSceneInstances(instances)}; }
};
struct NativeFullFrameStaticFrame {
  struct Stats {
    uint64_t groups=0,draws=0,instances=0,resolves=0,cache_hits=0,missing_group=0,missing_material=0,
      missing_geometry=0,scissor=0,declined=0,world_declines=0,retained_objects=0,fresh_objects=0;
  };
  NativeFullFrameStaticSelection selection;
  std::vector<NativeFullFrameStaticDraw> draws;
  Stats stats;
};
// Cross-frame state: the per-group material cache and instance reuse. The
// material cache is keyed by the published group material (so its program
// pointer and published constants), the program, geometry, pass base state
// and targets; never by guest state. A hit also requires this frame's pass
// constants to match but for the camera matrices the capture derives (see
// NativeStaticWorldGroupCache::Current). Not synchronized.
class NativeFullFrameStaticWorld {
 public:
  using Cache=NativeStaticWorldGroupCache<NativeFullFrameStaticTargets,NativeFullFrameStaticMaterial>;
  // resolve(group material, geometry, base state, pass constants) returns the
  // group's material or throws; ResolveNativeFullFrameStaticMaterial is the
  // production resolver. Runs once per group per cache miss.
  template<class Resolve>
  NativeFullFrameStaticFrame Build(const NativeScenePublication& publication,const NativeFullFrameStaticCamera& camera,
      const NativeFullFrameStaticRoutes& routes,const NativeFullFrameStaticPass& pass,Resolve&& resolve);
  Cache cache;
  NativeSceneInstanceReuse reuse;
 private:
  uint64_t next_id_=(uint64_t(3)<<61);
};

template<class Resolve>
NativeFullFrameStaticFrame NativeFullFrameStaticWorld::Build(const NativeScenePublication& publication,
    const NativeFullFrameStaticCamera& camera,const NativeFullFrameStaticRoutes& routes,
    const NativeFullFrameStaticPass& pass,Resolve&& resolve) {
  NativeFullFrameStaticFrame frame;
  frame.selection=SelectNativeFullFrameStaticWorld(publication,camera,routes);
  auto& stats=frame.stats;
  const auto base=NativeFullFrameStaticBaseState(pass.targets);
  const auto* sources=publication.sources.get();
  const auto report=[&](const char* reason) {
    ++stats.declined;
    (void)reason;
  };
  for(const auto& owner:frame.selection.owners) for(const auto& selected:owner.groups) {
    ++stats.groups;
    const auto* membership=sources?sources->FindGroup(selected.group):nullptr;
    if(!membership) { ++stats.missing_group; continue; }
    std::shared_ptr<const NativeSceneGroupMaterial> material;
    for(const auto& published:publication.group_materials)
      if(published->group==selected.group && published->revision==membership->revision) { material=published; break; }
    std::shared_ptr<const NativeSceneGroupGeometry> geometry;
    for(const auto& published:publication.group_geometry)
      if(published->group==selected.group && published->revision==membership->revision) { geometry=published; break; }
    if(!material || !material->program) { ++stats.missing_material; continue; }
    if(!geometry) { ++stats.missing_geometry; continue; }
    const auto& program=*material->program;
    // Scissor enable's rectangle is not a pass input (as in the world pass).
    if(!program.CanDeferCpuActivation()) { ++stats.scissor; continue; }
    const auto first=program.inputs.WorldRegisterFirst();
    if(!first) { report("world register"); continue; }
    std::vector<NativeSceneMaterialInputs::Constant> constants;
    Cache::Key key{.group=selected.group,.vertex=program.inputs.vertex,.pixel=program.inputs.pixel,
      .material=material,.program=material->program,.setup=geometry->setup.value_or(NativeSceneGeometrySource{}),
      .geometry=geometry->geometry,.backend=program.backend,.pass=base,.view=pass.targets,.filtering=pass.filtering};
    const NativeFullFrameStaticMaterial* resolved=nullptr;
    try {
      constants=NativeFullFrameStaticConstants(*material,camera);
      auto* entry=cache.Candidate(key);
      if(entry && Cache::Current(*entry,constants,entry->material.material.get(),entry->material.camera)) {
        resolved=&entry->material; ++stats.cache_hits; ++cache.hits;
      } else {
        ++cache.misses; ++stats.resolves;
        NativeFullFrameStaticMaterial fresh=resolve(*material,geometry->geometry,base,std::span<const NativeSceneMaterialInputs::Constant>(constants));
        const auto captured=fresh.material;
        const auto view=fresh.camera;
        resolved=&cache.Store(std::move(key),constants,{},base.After(program),std::move(fresh),captured.get(),view).material;
      }
    } catch(const std::exception& error) { report(error.what()); continue; }
    NativeFullFrameStaticDraw draw;
    draw.owner=owner.owner; draw.group=selected.group; draw.geometry=geometry->geometry; draw.material=resolved->material;
    draw.view=resolved->camera;
    draw.view.viewport=pass.viewport; draw.view.scissor=pass.scissor; draw.view.scissor_enabled=resolved->render[5]!=0;
    draw.instances.reserve(selected.instances.size());
    for(const auto instance:selected.instances) {
      // The instance's parameters must be its world alone, in the program's
      // g_mWorld registers (recorded at the source's lifetime event).
      const auto* source=sources->Find(instance);
      if(!source || !source->world_data || !source->world_first || *source->world_first!=*first) { ++stats.world_declines; continue; }
      const auto world=sources->WorldRegisters(*source,source->world_data);
      if(!world) { ++stats.world_declines; continue; }
      const auto matrix=DecodeNativeQueuedWorld(*world,resolved->world_column_major);
      auto object=publication.Resolve(*source,draw.geometry,draw.material,matrix,&reuse);
      if(object) ++stats.retained_objects;
      else {
        // No retained lifetime yet (never observed): a frame-local object.
        auto fresh=std::make_shared<NativeSceneInstance>();
        fresh->id=++next_id_; fresh->changed_tick=UINT64_MAX;
        fresh->object.geometry=draw.geometry; fresh->object.material=draw.material;
        fresh->object.world=matrix; fresh->previous=matrix;
        object=std::move(fresh); ++stats.fresh_objects;
      }
      draw.instances.push_back(std::move(object));
    }
    if(draw.instances.empty()) continue;
    stats.instances+=draw.instances.size(); ++stats.draws;
    frame.draws.push_back(std::move(draw));
  }
  cache.EndPass(); reuse.EndPass();
  return frame;
}
}
