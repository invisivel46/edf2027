#pragma once
#include "native_full_frame_base_state.h"
#include "native_scene_adapter.h"
#include "native_scene_tree.h"
#include "native_scene_static_walk.h"
#include "native_static_world_cache.h"
#include "native_queued_scene.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
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
// There are no guest queues, device mirrors, eligibility against the device or
// handoffs. Every input is a published immutable generation - NativeScenePublication
// (sources, membership, trees, group order, geometry, material programs) - but
// the route words, read live for the objects culling keeps (NativeFullFrameLiveRoutes).

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
// sub_821C0C00's route words for one owner: vtable+16==820B2670 (direct), +52
// mode and +64 hidden. They have unhooked writers (inline stw/sth), so they
// are not published: the selection reads them live at render time, for the
// objects that survive tree and frustum culling only, as 820B4038 reads them
// in the guest's own render walk (the same race with the simulation).
struct NativeFullFrameStaticRoute {
  bool direct=false;
  uint32_t mode=0;
  uint16_t hidden=0;
  bool operator==(const NativeFullFrameStaticRoute&) const=default;
};
// Reads one owner's route words; nullopt when its header cannot be read.
using NativeFullFrameStaticRouteRead=std::function<std::optional<NativeFullFrameStaticRoute>(uint32_t owner)>;
// The production route reader over a reader (typically a page window): one
// 68-byte header read per object (+0 vtable, +52 mode, +64 hidden), and the
// vtable+16 slot only when the object would take the render slot (hidden 0,
// mode 0), in sub_821C0C00's order. Vtables are image data, never stored to,
// so each vtable's slot answer is kept for the reader's lifetime. The reader
// must outlive this. Not synchronized.
template<class Reader>
class NativeFullFrameLiveRoutes {
 public:
  explicit NativeFullFrameLiveRoutes(const Reader& reader):reader_(reader) {}
  std::optional<NativeFullFrameStaticRoute> operator()(uint32_t owner) const {
    try {
      const auto* header=reader_.Bytes(owner,68);
      const auto vtable=GuestBlockWord(header);
      NativeFullFrameStaticRoute route{false,GuestBlockWord(header+52),uint16_t(GuestBlockWord(header+64)>>16)};
      ++reads;
      if(route.hidden || route.mode) return route;
      if(const auto found=direct_.find(vtable);found!=direct_.end()) route.direct=found->second;
      else { route.direct=reader_.Word(reader_.Add(vtable,16))==kNativeStaticDirectRender; direct_.emplace(vtable,route.direct); ++slot_reads; }
      return route;
    } catch(const std::exception&) { ++failures; return std::nullopt; }
  }
  mutable uint64_t reads=0,slot_reads=0,failures=0;
 private:
  const Reader& reader_;
  mutable std::unordered_map<uint32_t,bool> direct_;
};
// Addresses of the published tree image only: every read is a captured byte,
// and anything else throws rather than reaching guest memory.
class NativeSceneTreeImageReader {
 public:
  explicit NativeSceneTreeImageReader(const NativeSceneTreeImage& image):image_(image) {}
  uint32_t Add(uint32_t address,size_t size) const { return uint32_t(address+size); }
  // image.Find(address,size): the region last located answers every address
  // in its [base,limit) exactly as Locate would (a node's center, radius and
  // extents are one region), so only a read outside it searches the map.
  const uint8_t* Bytes(uint32_t address,size_t size) const {
    ++reads;
    if(!last_.bytes || address<last_.base || address>=last_.limit) last_=image_.Locate(address);
    if(const auto* bytes=NativeSceneTreeImage::Within(last_,address,size)) return bytes;
    throw std::runtime_error("native full-frame tree read outside its published image");
  }
  uint32_t Word(uint32_t address) const { return GuestBlockWord(Bytes(address,4)); }
  mutable uint64_t reads=0;
 private:
  const NativeSceneTreeImage& image_;
  mutable NativeSceneTreeImage::Region last_;
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
    uint64_t route_reads=0,unrouted=0,not_direct=0,unpublished=0,culled_distance=0,culled_frustum=0,visible=0,missing_lod=0,undrawable=0;
    uint64_t selected=0,parts=0,unordered_groups=0,unordered_parts=0;
  };
  std::vector<NativeFullFrameStaticOwner> owners;
  // Every object selected natively, in walk order, with the LOD it chose.
  std::vector<Object> objects;
  Stats stats;
};
// Cross-frame scratch of SelectNativeFullFrameStaticWorld; it never changes
// a selection, only what one costs:
//  - each membership list's members pre-looked-up (visibility record, LOD
//    parts), rebuilt only when the list's published snapshot or the
//    candidates (NativeSceneSources::SameCandidates) move. The views point
//    into sources, which it retains, so they stay valid while the publication
//    moves on. Route words are never cached: they are read live, each frame,
//    for culling's survivors;
//  - the +48 visit marker as an epoch-stamped open-addressing set (no clear,
//    no allocation per frame);
//  - each world's group queues as flat arrays indexed by the group's first
//    position in the published order (rebuilt when the order moves).
// Not synchronized.
struct NativeFullFrameStaticSelectCache {
  struct Candidate {
    uint32_t owner=0;
    bool published=false;  // A visibility record with at least one LOD.
    NativeSceneSources::CandidateView view;
    std::array<bool,3> drawable{};  // Every part of the LOD has a group.
  };
  struct List {
    std::shared_ptr<const NativeSceneMembership::Snapshot> members;
    std::vector<Candidate> candidates;
    uint64_t used=0;
  };
  struct Queues {
    std::shared_ptr<const NativeSceneGroupOrder> order;
    std::unordered_map<uint32_t,uint32_t> slots;  // Group -> first position in order.
    std::vector<std::vector<uint32_t>> queues;    // Per position, in push order.
    std::vector<uint32_t> touched;                // Positions with a queue this frame.
    std::map<uint32_t,size_t> unordered;          // Groups outside the order -> parts.
    uint64_t used=0;
  };
  // The +48 marker: Insert is true once per key per Begin.
  class Seen {
   public:
    void Begin() {
      if(++epoch_==0) { std::fill(stamps_.begin(),stamps_.end(),0u); epoch_=1; }
      count_=0;
    }
    bool Insert(uint32_t key) {
      if((count_+1)*2>keys_.size()) Grow();
      const auto mask=keys_.size()-1;
      for(size_t at=Hash(key)&mask;;at=(at+1)&mask) {
        if(stamps_[at]!=epoch_) { stamps_[at]=epoch_; keys_[at]=key; ++count_; return true; }
        if(keys_[at]==key) return false;
      }
    }
   private:
    static size_t Hash(uint32_t key) { return size_t((uint64_t(key)*0x9E3779B97F4A7C15ull)>>32); }
    void Grow() {
      std::vector<uint32_t> keys((std::max)(keys_.size()*2,size_t(1024))),stamps(keys.size());
      const auto mask=keys.size()-1;
      for(size_t i=0;i<keys_.size();++i) if(stamps_[i]==epoch_) {
        size_t at=Hash(keys_[i])&mask;
        while(stamps[at]==epoch_) at=(at+1)&mask;
        stamps[at]=epoch_; keys[at]=keys_[i];
      }
      keys_=std::move(keys); stamps_=std::move(stamps);
    }
    std::vector<uint32_t> keys_,stamps_;
    uint32_t epoch_=0;
    size_t count_=0;
  };
  struct Stats { uint64_t list_hits=0,list_builds=0,invalidations=0; };
  std::shared_ptr<const NativeSceneSources> sources;  // The generation every List points into.
  std::unordered_map<uint32_t,List> lists;
  std::unordered_map<uint32_t,Queues> worlds;
  Seen seen;
  Stats stats;
  uint64_t pass=0;
};
// 821C61D8 + 820B4038 + 821C3BB8's group order over published data. Per world
// owner with a tree image: walk the tree with frustum classification from the
// camera; gather each accepted leaf list (node+120), then world+372, from the
// published membership; visit each owner once (the guest's +48 frame marker);
// cull by the published visibility record and pick the LOD
// (SelectNativeVisibility, shared with the 820B4038 hook); route by the words
// route() reads for the survivors only (hidden, then mode 0 with direct
// 820B2670 dispatch; buckets, virtual and unknown routes are other passes'
// objects; an unreadable header is unrouted); push each part of
// that LOD into its group. A part without a group makes the whole object
// undrawable here, as it sends the object back to the guest in the hook.
// Selections of a group outside the owner's published order are counted and
// dropped: 821C3BB8 never reaches them.
// cache, when given, is kept across frames (a call without one builds and
// drops its own); the selection is the same either way.
NativeFullFrameStaticSelection SelectNativeFullFrameStaticWorld(const NativeScenePublication& publication,
  const NativeFullFrameStaticCamera& camera,const NativeFullFrameStaticRouteRead& route,
  NativeFullFrameStaticSelectCache* cache=nullptr);

// Target formats and depth direction of the pass, and its base state: the one
// definition every full-frame scene pass shares (native_full_frame_base_state.h).
using NativeFullFrameStaticTargets=NativeFullFramePassTargets;
struct NativeFullFrameStaticPass {
  NativeFullFrameStaticTargets targets;
  NativeBackendViewport viewport;
  NativeBackendScissor scissor;
  int filtering=-1;
  bool operator==(const NativeFullFrameStaticPass& other) const {
    return targets==other.targets && !std::memcmp(&viewport,&other.viewport,sizeof(viewport)) &&
      !std::memcmp(&scissor,&other.scissor,sizeof(scissor)) && filtering==other.filtering;
  }
};
// Each group's state is the shared base state plus its own material's state
// operations - NOT chained from the previous group; see NativeFullFrameBaseState.
inline NativeSceneMaterialPassState NativeFullFrameStaticBaseState(const NativeFullFrameStaticTargets& targets) {
  return NativeFullFrameBaseState(targets);
}
inline constexpr const auto& kNativeFullFrameStaticBaseOperations=kNativeFullFrameBaseOperations;

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
// The same in place, on a copy of the group's published constants.
void ApplyNativeFullFrameStaticConstants(std::vector<NativeSceneMaterialInputs::Constant>& constants,
  const NativeFullFrameStaticCamera& camera);
// Whether NativeScenePassCamera::Apply rewrites this constant: the only
// constants whose pass value depends on the pass camera.
inline bool NativeFullFrameStaticCameraConstant(const NativeSceneMaterialInputs::Constant& constant) {
  return constant.global && (constant.name=="g_mProjection" || constant.name=="g_mView" ||
    constant.name=="g_mViewTranspose" || constant.name=="g_mViewProjection");
}

// One instanced draw: one group's geometry and material, every selected
// instance with its published world. instances are scene objects in draw
// order; the renderer instances adjacent objects that share geometry and
// material, which all of these do.
struct NativeFullFrameStaticDraw {
  uint32_t owner=0,group=0;
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  std::shared_ptr<const NativeSceneMaterial> material;
  NativeSceneView view;
  // Shared storage: a snapshot, or the next frame's unchanged draw, copies it in O(1).
  NativeSceneInstances instances;
  NativeSceneSnapshot Snapshot() const { return {0,instances}; }
};
struct NativeFullFrameStaticFrame {
  struct Stats {
    uint64_t groups=0,draws=0,instances=0,resolves=0,cache_hits=0,missing_group=0,missing_material=0,
      missing_geometry=0,scissor=0,declined=0,world_declines=0,retained_objects=0,fresh_objects=0,
      camera_only=0,reused_draws=0,reused_frame=0;
  };
  NativeFullFrameStaticSelection selection;
  std::vector<NativeFullFrameStaticDraw> draws;
  Stats stats;
};
// Cross-frame state: the per-group material cache and instance reuse. The
// material cache is keyed by the program pointer, geometry, pass base state
// and targets; never by guest state. A hit also requires this frame's pass
// constants (the published ones with camera and animation applied) to match
// but for the camera matrices the capture derives and the zeroed world (see
// NativeStaticWorldGroupCache::Current). Not synchronized.
//
// What a frame costs follows what moved since the last one (every result is
// the one a build from scratch returns, but for the ids of frame-local objects):
//  - Select always selects (the route words are read live, so no selection
//    outlives its frame), through selection_cache.
//  - BuildSelected returns the previous frame whole when the selection's
//    groups and instances equal the ones it was built from and its own inputs
//    (sources, group materials and geometry, by_source, pass camera,
//    animation, pass) are the same and no group was resolved since
//    (cache.stores).
//  - Per group, the pass constants persist (Memo): while the published group
//    material and the animation are the same objects only the camera
//    constants are rewritten, in place. Once Current accepted them for an
//    entry, and every camera constant is one it compares by shape alone (it
//    reaches the capture only as the derived camera), later frames skip
//    Current's compare and derive the camera alone.
//  - Per group, the instances are the previous frame's while the selected
//    instances, sources, by_source, geometry, material and world layout are
//    the same; a frame-local object then keeps its id across frames.
// resolve is taken to be the same function on every call.
class NativeFullFrameStaticWorld {
 public:
  using Cache=NativeStaticWorldGroupCache<NativeFullFrameStaticTargets,NativeFullFrameStaticMaterial>;
  // resolve(group material, geometry, base state, pass constants) returns the
  // group's material or throws; ResolveNativeFullFrameStaticMaterial is the
  // production resolver. Runs once per group per cache miss.
  template<class Resolve>
  const NativeFullFrameStaticFrame& Build(const NativeScenePublication& publication,const NativeFullFrameStaticCamera& camera,
      const NativeFullFrameStaticRouteRead& route,const NativeFullFrameStaticPass& pass,Resolve&& resolve) {
    Select(publication,camera,route);
    return BuildSelected(publication,camera,pass,std::forward<Resolve>(resolve));
  }
  // The two halves of Build, for timing them apart: BuildSelected draws the
  // selection the last Select made, with the same publication and camera.
  const NativeFullFrameStaticSelection& Select(const NativeScenePublication& publication,
    const NativeFullFrameStaticCamera& camera,const NativeFullFrameStaticRouteRead& route);
  template<class Resolve>
  const NativeFullFrameStaticFrame& BuildSelected(const NativeScenePublication& publication,
    const NativeFullFrameStaticCamera& camera,const NativeFullFrameStaticPass& pass,Resolve&& resolve);
  const NativeFullFrameStaticFrame& frame() const { return frame_; }
  Cache cache;
  NativeSceneInstanceReuse reuse;
  NativeFullFrameStaticSelectCache selection_cache;
 private:
  using Constants=std::vector<NativeSceneMaterialInputs::Constant>;
  using BySource=decltype(NativeScenePublication::by_source);
  struct Memo {
    // constants: published's constants with pass_camera and animation applied;
    // cameras: the indices NativeFullFrameStaticCameraConstant names.
    std::shared_ptr<const NativeSceneGroupMaterial> published;
    std::optional<NativeScenePassAnimation> animation;
    NativeScenePassCamera pass_camera,derived;
    Constants constants;
    std::vector<uint32_t> cameras;
    // Current accepted constants for entry (at stores); derived is the pass
    // camera entry's camera was last derived at.
    const Cache::Entry* entry=nullptr;
    uint64_t stores=0;
    bool accepted=false,camera_only=false;
    // The last instance build and what it depended on.
    std::vector<uint32_t> selected;
    std::shared_ptr<const NativeSceneSources> sources;
    BySource by_source;
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
    std::shared_ptr<const NativeSceneMaterial> material;
    uint32_t first=0;
    bool column_major=false,built=false;
    NativeSceneInstances instances;
    uint64_t retained=0,fresh=0,declined=0,used=0;
  };
  // NativeFullFrameStaticConstants(published,camera), kept in memo.
  static const Constants& PassConstants(Memo& memo,const std::shared_ptr<const NativeSceneGroupMaterial>& published,
      const NativeFullFrameStaticCamera& camera) {
    if(memo.published!=published || memo.animation!=camera.animation) {
      memo.published=nullptr; memo.accepted=false;
      memo.constants=published->constants;  // Assignment keeps each constant's storage.
      ApplyNativeFullFrameStaticConstants(memo.constants,camera);
      memo.cameras.clear();
      for(size_t i=0;i<memo.constants.size();++i)
        if(NativeFullFrameStaticCameraConstant(memo.constants[i])) memo.cameras.push_back(uint32_t(i));
      memo.published=published; memo.animation=camera.animation; memo.pass_camera=camera.pass;
    } else if(!(memo.pass_camera==camera.pass)) {
      // Only the camera constants depend on the pass camera.
      memo.published=nullptr;
      for(const auto i:memo.cameras) camera.pass.Apply(memo.constants[i]);
      memo.published=published; memo.pass_camera=camera.pass;
    }
    return memo.constants;
  }
  // Cache::Current(entry,memo.constants,...) with its compare skipped when
  // only constants it compares by shape moved since it accepted them.
  bool Current(Memo& memo,Cache::Entry& entry,const NativeFullFrameStaticCamera& camera,NativeFullFrameStaticFrame::Stats& stats) {
    auto& material=entry.material;
    if(memo.accepted && memo.camera_only && memo.entry==&entry && memo.stores==cache.stores) {
      // Current's result for these constants: the entry's camera is already
      // the one they derive when it is underived or derived at this camera.
      if(!entry.derived || memo.derived==camera.pass) { ++stats.camera_only; return true; }
      if(material.material) if(const auto view=NativeStaticCaptureCamera(*material.material,memo.constants)) {
        material.camera.view=view->view; material.camera.projection=view->projection;
        material.camera.view_projection=view->view_projection; memo.derived=camera.pass;
        ++stats.camera_only; return true;
      }
    }
    memo.accepted=false;
    if(!Cache::Current(entry,memo.constants,material.material.get(),material.camera)) return false;
    memo.accepted=true; memo.entry=&entry; memo.stores=cache.stores; memo.derived=camera.pass;
    memo.camera_only=std::ranges::all_of(memo.cameras,[&](uint32_t i) { return entry.derived && entry.camera[i]; });
    return true;
  }
  struct BuildInputs {
    std::shared_ptr<const NativeSceneSources> sources;
    decltype(NativeScenePublication::group_materials) materials;
    decltype(NativeScenePublication::group_geometry) geometry;
    BySource by_source;
    NativeScenePassCamera pass_camera;
    std::optional<NativeScenePassAnimation> animation;
    NativeFullFrameStaticPass pass;
    uint64_t stores=0;
    bool valid=false;
  };
  BuildInputs built_;
  bool same_selection_=false;  // Select's groups and instances are the ones built_ drew.
  NativeFullFrameStaticFrame frame_;
  std::unordered_map<uint32_t,Memo> memos_;
  uint64_t pass_=0;
  uint64_t next_id_=(uint64_t(3)<<61);
  static constexpr uint64_t kMemoAge=256;
};

template<class Resolve>
const NativeFullFrameStaticFrame& NativeFullFrameStaticWorld::BuildSelected(const NativeScenePublication& publication,
    const NativeFullFrameStaticCamera& camera,const NativeFullFrameStaticPass& pass,Resolve&& resolve) {
  auto& in=built_;
  if(same_selection_ && in.valid && in.sources==publication.sources && in.materials.Shares(publication.group_materials) &&
     in.geometry.Shares(publication.group_geometry) && in.by_source.Shares(publication.by_source) &&
     in.pass_camera==camera.pass && in.animation==camera.animation && in.pass==pass && in.stores==cache.stores) {
    frame_.stats.reused_frame=1;
    return frame_;
  }
  in.valid=false;
  frame_.draws.clear(); frame_.stats={};
  ++pass_;
  auto& stats=frame_.stats;
  const auto base=NativeFullFrameStaticBaseState(pass.targets);
  const auto* sources=publication.sources.get();
  const auto report=[&](const char* reason) {
    ++stats.declined;
    (void)reason;
  };
  for(const auto& owner:frame_.selection.owners) for(const auto& selected:owner.groups) {
    ++stats.groups;
    const auto* membership=sources?sources->FindGroup(selected.group):nullptr;
    if(!membership) { ++stats.missing_group; continue; }
    // Both are kept sorted and unique by group (NativeSceneAdapter publishes them so).
    const auto* material=publication.group_materials.Find(selected.group,NativeSceneGroupKey{});
    if(material && (*material)->revision!=membership->revision) material=nullptr;
    const auto* geometry=publication.group_geometry.Find(selected.group,NativeSceneGroupKey{});
    if(geometry && (*geometry)->revision!=membership->revision) geometry=nullptr;
    if(!material || !(*material)->program) { ++stats.missing_material; continue; }
    if(!geometry) { ++stats.missing_geometry; continue; }
    const auto& group=**material;
    const auto& program=*group.program;
    // Scissor enable's rectangle is not a pass input (as in the world pass).
    if(!program.CanDeferCpuActivation()) { ++stats.scissor; continue; }
    const auto first=program.inputs.WorldRegisterFirst();
    if(!first) { report("world register"); continue; }
    auto& memo=memos_[selected.group];
    memo.used=pass_;
    Cache::Key key{.group=selected.group,.vertex=program.inputs.vertex,.pixel=program.inputs.pixel,
      .program=group.program,.setup=(*geometry)->setup.value_or(NativeSceneGeometrySource{}),
      .geometry=(*geometry)->geometry,.backend=program.backend,.pass=base,.view=pass.targets,.filtering=pass.filtering};
    const NativeFullFrameStaticMaterial* resolved=nullptr;
    try {
      const auto& constants=PassConstants(memo,*material,camera);
      auto* entry=cache.Candidate(key);
      if(entry && Current(memo,*entry,camera,stats)) {
        resolved=&entry->material; ++stats.cache_hits; ++cache.hits;
      } else {
        ++cache.misses; ++stats.resolves;
        NativeFullFrameStaticMaterial fresh=resolve(group,(*geometry)->geometry,base,std::span<const NativeSceneMaterialInputs::Constant>(constants));
        const auto captured=fresh.material;
        const auto view=fresh.camera;
        // No guest eligibility here: empty reads and witness. The published
        // group material is recorded as an observation only.
        auto& stored=cache.Store(std::move(key),constants,NativeRecordedReads{},NativeStaticEligibilityWitness{},
          base.After(program),std::move(fresh),captured.get(),view,Cache::Observed{0,*material});
        resolved=&stored.material;
      }
    } catch(const std::exception& error) { report(error.what()); continue; }
    NativeFullFrameStaticDraw draw;
    draw.owner=owner.owner; draw.group=selected.group; draw.geometry=(*geometry)->geometry; draw.material=resolved->material;
    draw.view=resolved->camera;
    draw.view.viewport=pass.viewport; draw.view.scissor=pass.scissor; draw.view.scissor_enabled=resolved->render[5]!=0;
    if(memo.built && memo.selected==selected.instances && memo.sources==publication.sources &&
       memo.by_source.Shares(publication.by_source) && memo.geometry==draw.geometry && memo.material==draw.material &&
       memo.column_major==resolved->world_column_major && memo.first==*first) {
      draw.instances=memo.instances; ++stats.reused_draws;
    } else {
      memo.built=false; memo.retained=memo.fresh=memo.declined=0;
      for(const auto instance:selected.instances) {
        // The instance's parameters must be its world alone, in the program's
        // g_mWorld registers (recorded at the source's lifetime event).
        const auto* source=sources->Find(instance);
        if(!source || !source->world_data || !source->world_first || *source->world_first!=*first) { ++memo.declined; continue; }
        const auto world=sources->WorldRegisters(*source,source->world_data);
        if(!world) { ++memo.declined; continue; }
        const auto matrix=DecodeNativeQueuedWorld(*world,resolved->world_column_major);
        auto object=publication.Resolve(*source,draw.geometry,draw.material,matrix,&reuse);
        if(object) ++memo.retained;
        else {
          // No retained lifetime yet (never observed): a frame-local object.
          auto fresh=std::make_shared<NativeSceneInstance>();
          fresh->id=++next_id_; fresh->changed_tick=UINT64_MAX;
          fresh->object.geometry=draw.geometry; fresh->object.material=draw.material;
          fresh->object.world=matrix; fresh->previous=matrix;
          object=std::move(fresh); ++memo.fresh;
        }
        draw.instances.push_back(std::move(object));
      }
      memo.selected=selected.instances; memo.sources=publication.sources; memo.by_source=publication.by_source;
      memo.geometry=draw.geometry; memo.material=draw.material; memo.column_major=resolved->world_column_major;
      memo.first=*first; memo.instances=draw.instances; memo.built=true;
    }
    stats.world_declines+=memo.declined; stats.retained_objects+=memo.retained; stats.fresh_objects+=memo.fresh;
    if(draw.instances.empty()) continue;
    stats.instances+=draw.instances.size(); ++stats.draws;
    frame_.draws.push_back(std::move(draw));
  }
  cache.EndPass(); reuse.EndPass();
  if(!(pass_%kMemoAge)) std::erase_if(memos_,[&](const auto& item) { return pass_-item.second.used>kMemoAge; });
  in={publication.sources,publication.group_materials,publication.group_geometry,publication.by_source,
    camera.pass,camera.animation,pass,cache.stores,true};
  return frame_;
}
}
