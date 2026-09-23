#pragma once
#include "native_bucket_dispatch.h"
#include "native_full_frame_base_state.h"
#include "native_full_frame_model_cache.h"
#include "native_model_pass.h"
#include "native_render_entry.h"
#include "native_render_motion.h"
#include "native_scene_adapter.h"
#include "native_scene_visibility.h"
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace edf::native {
// The full-frame renderer's Models pass (kNativeFramePassOrder "models", and
// the model half of "transparent"): every model the renderable registry
// published, drawn from the registry snapshot and the pass camera alone. It
// replaces, for registry objects, end to end:
//   820B4038 per-object gather -> ClassifyNativeFullFrameModel (visibility)
//   821C0C00 route + key       -> mode 0 opaque list, modes 1/2 bucket key
//   821A3BA0 bucket traversal  -> the transparent list, key descending
//   8210AE48 / 820B2670 LOD    -> SelectNativeFullFrameModelLod
//   821C9C20 model draw        -> the layout's records/batches/passes with the
//                                 snapshot pose (821A17D8 rigid world,
//                                 821A1738 bone palette), blended from the
//                                 previous tick's in unlocked mode
//                                 (NativeFullFrameModelPass::motion)
//   820DB268 face / 820DE790   -> one item per published attachment
//     weapons                     (NativeRenderEntry::attachments) after the
//                                 LOD model, with its own layout and pose
//   821C9DA8 world draw        -> one item per published instanced world
//                                 (NativeRenderEntry::instanced), every drawn
//                                 record with that world; identical draws of
//                                 the instances share one material and are
//                                 adjacent in the opaque order, so the scene
//                                 renderer draws them instanced
//   821A1730 before a draw  -> the item's per-object constants (g_Highlight,
//                                 g_Time, g_Scroll; NativeRenderEntry::constants)
//                                 over the pool state the guest's earlier slot
//                                 4s left (NativeFullFrameModelPoolCarry), bound
//                                 over the pass constants' globals of those
//                                 names (NativeFullFrameModelObjectConstants)
// No guest function is called, no device state is read or handed off and no
// guest-mirror eligibility is assessed. Each draw's render state is the pass
// base state plus its own material's state operations (see
// NativeFullFrameModelBaseState), never state chained from an earlier draw.

// The camera of one view. The published NativeScenePassCamera carries only the
// material matrices; visibility needs the float view matrix (camera+96, the
// same words as pass.view), the frustum (camera+288, 26 floats) and the depth
// scale (camera+400, which 821A5080 copies to context+8), and the modes-1
// bucket key needs context+0/+4 (Word(8201711C)/Word(82017120), which 821A5080
// stores per view). Until the camera publication carries them the frame host
// fills this from its view (ReadNativeSceneVisibilityView's fields).
struct NativeFullFrameModelCamera {
  NativeSceneVisibilityView visibility;
  NativeScenePassCamera pass;
  std::optional<NativeScenePassAnimation> animation;
  float key_scale=0,key_offset=0;
};

// 820B4038 / 821C0C00 for one entry, in the order the frame evaluates them.
enum class NativeFullFrameModelCull : uint8_t { Visible, Hidden, Mode, Distance, Frustum, Box };
struct NativeFullFrameModelVisibility {
  NativeFullFrameModelCull cull=NativeFullFrameModelCull::Visible;
  std::array<float,4> centre{};  // View-space centre (what the guest stores at context+32).
  float depth=0;                 // d=-float(z*depth_scale), the LOD and cull distance.
  uint32_t sphere=0,box=0;       // 821C3070 / 821C33E8 results (box only after a partial sphere).
  explicit operator bool() const { return cull==NativeFullFrameModelCull::Visible; }
};
// Hidden (obj+64) and an unsupported sort mode (not 0/1/2) reject first; then
// the centre is transformed by the view matrix, d > cull distance culls, and
// the sphere test with the radius classifies; a partial sphere (2) runs the
// box test on centre+axes, which rejects only when every corner is outside one
// plane. Boundary comparisons are NativeVisibilitySphere/Box's (821C3070/821C33E8).
NativeFullFrameModelVisibility ClassifyNativeFullFrameModel(const NativeRenderEntry& entry,const NativeSceneVisibilityView& view);
// The model index into entry.models, stateless, or none when the choice has
// no model. None: models[0]. Character (8210AE48): thresholds[i] selects
// models[i+1]; the last i with d > thresholds[i] wins, else models[0].
// FieldParts (820B2670, NativeVisibilityLod): thresholds[i] is LOD record i
// (obj+448+44i) and selects models[i]; the last i >= 1 with d > thresholds[i]
// wins, else models[0]; thresholds[0] is never compared.
std::optional<uint32_t> SelectNativeFullFrameModelLod(const NativeRenderEntry& entry,float depth);
// ComputeNativeBucketKey from values: mode 1 float(fma(z,scale,offset))*bias,
// mode 2 bias*65536, clamped to [0,65535] and converted as fctidz (NaN -> 0).
// z is the view-space centre z (context+40). Throws for any other mode.
inline uint16_t NativeFullFrameModelKey(int32_t mode,float view_z,float sort_bias,float scale,float offset) {
  double depth;
  if(mode==1) {
    depth=double(float(std::fma(double(view_z),double(scale),double(offset))));
    depth=double(float(depth*double(sort_bias)));
  } else if(mode==2) depth=double(float(double(sort_bias)*65536.0));
  else throw std::runtime_error("native full-frame model key for an unsupported sort mode");
  if(depth<0.0) depth=0.0;
  else if(depth>65535.0) depth=65535.0;
  return uint16_t(uint64_t(NativeFctidz(depth)));
}

// One model the frame draws: the LOD model chosen, with attachment >= 0
// entry->attachments[attachment] (face, weapon), or with instanced >= 0 the
// world `world` of entry->instanced[instanced], and the entry's sort data
// (attachments and instances share their object's visibility, key and route,
// as 820DEA08 and 820EC180 draw them inside the one slot-4 call). entry points
// into the snapshot, which must outlive the plan.
struct NativeFullFrameModelItem {
  const NativeRenderEntry* entry=nullptr;
  uint32_t model=0;
  float depth=0,view_z=0;
  uint16_t key=0;
  bool transparent=false;
  int32_t instanced=-1;
  uint32_t world=0;
  int32_t attachment=-1;
};
inline const std::shared_ptr<const NativeModelLayout>& NativeFullFrameModelItemLayoutObject(const NativeFullFrameModelItem& item) {
  if(item.instanced>=0) return item.entry->instanced[size_t(item.instanced)].model.layout;
  if(item.attachment>=0) return item.entry->attachments[size_t(item.attachment)].model.layout;
  return item.entry->models[item.model].layout;
}
inline const NativeModelLayout& NativeFullFrameModelItemLayout(const NativeFullFrameModelItem& item) {
  return *NativeFullFrameModelItemLayoutObject(item);
}
// The pose vector an item draws from and its motion: the entry's pose, its
// attachment's, or its instanced set's worlds (of which it draws one).
inline std::pair<const NativeRenderPose*,const NativeRenderPoseMotion*> NativeFullFrameModelItemPose(const NativeFullFrameModelItem& item) {
  if(item.instanced>=0) {
    const auto& set=item.entry->instanced[size_t(item.instanced)];
    return {&set.worlds,&set.motion};
  }
  if(item.attachment>=0) {
    const auto& attachment=item.entry->attachments[size_t(item.attachment)];
    return {&attachment.pose,&attachment.motion};
  }
  return {&item.entry->pose,&item.entry->motion};
}
// The per-object constants in effect at an item's draw: its attachment's, or
// the entry's (the LOD model and the instanced sets drawn after it).
inline const NativeRenderConstants& NativeFullFrameModelItemConstants(const NativeFullFrameModelItem& item) {
  if(item.attachment>=0) return item.entry->attachments[size_t(item.attachment)].constants;
  return item.entry->constants;
}
// The guest's gather order for one view: the objects 820B4038 visits, first
// visit only (its obj+48 stamp skips a second), in the order 821A5080 calls
// each world-list manager's slot 2 (owner+44 list):
//  - 820D4850 (clGameObject_Manager, clGameBossObject_Manager,
//    clEffectObjectManager): 820B4038 over the list at manager+48;
//  - 820B4310 (clMapObjectManager): 820B4038 over the list at manager+372,
//    then the octree walk 821C61D8 -> 821C5FC8 / 821C56C0, which gathers the
//    cells' lists (node+120) depth first. The octree is not walked here (tens
//    of thousands of static map objects); its objects are `unlisted`;
//  - any other slot 2 gathers nothing the registry draws.
// Objects are gathered in list order, 820B4038 calling 821C0C00 per object as
// it goes. An object in no walked list (the octree's) is gathered at position
// `unlisted` (before objects[unlisted]; the octree walk comes after the list
// at manager+372), several of them in snapshot order. Empty: no guest order
// known (every entry unlisted, i.e. snapshot order).
struct NativeFullFrameModelGather {
  std::vector<uint32_t> objects;
  uint32_t unlisted=UINT32_MAX;  // UINT32_MAX: after every listed object.
};
inline constexpr uint32_t kNativeGatherWorldList=44,kNativeGatherObjectList=48,kNativeGatherMapList=372,
  kNativeGatherObjectSlot2=0x820D4850u,kNativeGatherMapSlot2=0x820B4310u;
// The walk 821A5080 makes of owner+44 (nodes {+0 next, +8 manager}, end at
// list+12) and each manager's gather list (WalkNativeRenderList's layout), as
// NativeFullFrameModelGather describes. Throws on a list that does not end.
template<class Reader,class Visit>
void WalkNativeFullFrameGatherList(const Reader& reader,uint32_t list,Visit&& visit,uint32_t limit) {
  const auto end=reader.Word(list+12);
  uint32_t count=0;
  for(uint32_t node=reader.Word(list);node!=end;node=reader.Word(node)) {
    if(!node || ++count>limit) throw std::runtime_error("native full-frame gather list does not reach its end");
    visit(reader.Word(node+8));
  }
}
template<class Reader>
NativeFullFrameModelGather ReadNativeFullFrameModelGather(const Reader& reader,uint32_t owner) {
  NativeFullFrameModelGather gather;
  std::unordered_set<uint32_t> seen;
  const auto visit=[&](uint32_t object) { if(object && seen.insert(object).second) gather.objects.push_back(object); };
  WalkNativeFullFrameGatherList(reader,owner+kNativeGatherWorldList,[&](uint32_t manager) {
    if(!manager) return;
    const auto slot2=reader.Word(reader.Word(manager)+8);
    if(slot2==kNativeGatherObjectSlot2) WalkNativeFullFrameGatherList(reader,manager+kNativeGatherObjectList,visit,1u<<17);
    else if(slot2==kNativeGatherMapSlot2) {
      WalkNativeFullFrameGatherList(reader,manager+kNativeGatherMapList,visit,1u<<17);
      if(gather.unlisted==UINT32_MAX) gather.unlisted=uint32_t(gather.objects.size());
    }
  },256);
  return gather;
}
struct NativeFullFrameModelPlan {
  struct Stats {
    uint64_t entries=0,hidden=0,mode=0,distance=0,frustum=0,box=0,no_model=0,no_pose=0,bucket_zero=0,opaque=0,transparent=0,
      instances=0,no_instanced=0,other_pass=0,attachments=0,no_attachment=0,
      calls=0,unlisted=0;  // Slot-4 calls (`calls`); entries the gather order does not list.
  };
  std::vector<NativeFullFrameModelItem> opaque;       // Gather order.
  std::vector<NativeFullFrameModelItem> transparent;  // Draw order: key descending, ties in gather order.
  // Every entry whose slot 4 the guest calls this view, in call order: mode 0
  // as it is gathered (821C0C00 calls it at once), then the filed ones in
  // 821A3BA0's drain order (key descending, ties in gather order), drawn by
  // the native pass or not (no model, pose or layout yet). What the slot-4
  // bodies store into the effect pool follows this order.
  std::vector<const NativeRenderEntry*> calls;
  Stats stats;
};
// clBrokenObject (vtable 820077D8): slot 4 8211FAA8 copies obj+708 into
// obj+712 before it poses and draws; its slot 3 8211FAF8 releases the object
// (821C0ED8) once obj+708 - obj+712 > 10, so a frame that skips the store
// kills the debris early.
struct NativeBrokenObject { static constexpr uint32_t vtable=0x820077D8u,render=0x8211FAA8u,update=0x8211FAF8u,counter=708,drawn=712; };
// Whether the guest calls the entry's slot 4 this view: 820B4038's
// visibility (ClassifyNativeFullFrameModel), then 821C0C00's route: mode 0
// calls it at once, modes 1/2 file it under key (821A3B80) and 821A3BA0
// calls it unless the key's high byte is 0. Independent of the model, pose
// and layout the native draw needs.
inline bool NativeFullFrameModelDispatched(const NativeFullFrameModelVisibility& visibility,const NativeRenderEntry& entry,
    const NativeFullFrameModelCamera& camera) {
  if(!visibility) return false;
  return entry.mode==0 || NativeFullFrameModelKey(entry.mode,visibility.centre[2],entry.sort_bias,camera.key_scale,camera.key_offset)>=256;
}
// Every clBrokenObject entry whose slot 4 the guest would call this view, in
// snapshot order, whether or not it is drawn (no layout or pose yet, a
// declined or failed draw, no targets): the frame host stores +712 = +708 for
// each, which is all of 8211FAA8 that simulation reads.
std::vector<const NativeRenderEntry*> NativeFullFrameBrokenObjects(const NativeRenderRegistrySnapshot& snapshot,
  const NativeFullFrameModelCamera& camera);
// Visibility, LOD and routing for every entry: its posed LOD model, then each
// posed attachment in guest order (the face, then the weapons), then each
// instanced world in record order. Transparents follow 821A3BA0:
// buckets by high key byte 255 down to 1, each by low byte descending, equal
// keys in gather order. High bucket 0 (key < 256) is never traversed there,
// so those entries are dropped (stats.bucket_zero). The registry snapshot is
// ordered by object; entries are taken in `gather` order (unlisted ones at
// its `unlisted` position, among themselves in snapshot order).
NativeFullFrameModelPlan PlanNativeFullFrameModels(const NativeRenderRegistrySnapshot& snapshot,const NativeFullFrameModelCamera& camera,
  const NativeFullFrameModelGather& gather={});

// The shared effect pool as the model slot 4s leave it (821A1730 ->
// 821A16D8: one float4 over a named pool value, sticky until the next store).
// In the guest a draw reads the value the last store before it left, in slot-4
// call order, whichever object made it: a class that stores nothing draws
// with the previous writer's (C_PowerLoader's powerloader.Dxm is c_Mech01,
// which reads g_Highlight and g_Time; UFO, alien-tank and mothership classes
// write them), and a frame starts with what the previous one left.
//  state: name -> the value the pool holds, in first-store order; a name the
//    carry never saw is not in it (such a draw keeps the published value, the
//    live guest pool's).
// Apply merges one store set (NativeRenderConstants: a name's value replaced
// in place, a new name appended), as ReadNativeRenderConstant does.
void ApplyNativeFullFrameModelPool(std::vector<NativeRenderObjectConstant>& state,const NativeRenderConstants& stores);
// What each call of `plan.calls` finds in the pool (before its own stores),
// and what the view leaves: every entry's stores, then each attachment's (the
// parts' stores, after the entry's, in slot-4 order), applied in call order
// from `start`. before[entry] is shared while unchanged (pointer equality is
// value equality); null while the state is empty.
struct NativeFullFrameModelPoolCarry {
  std::unordered_map<const NativeRenderEntry*,NativeRenderConstants> before;
  std::vector<NativeRenderObjectConstant> end;
};
NativeFullFrameModelPoolCarry CarryNativeFullFrameModelPool(const NativeFullFrameModelPlan& plan,
  std::span<const NativeRenderObjectConstant> start);
// The constants in effect at an item's draw: the pool before its entry's slot
// 4 (`before`) with the item's own stores (NativeFullFrameModelItemConstants)
// applied over it.
std::vector<NativeRenderObjectConstant> NativeFullFrameModelEffectiveConstants(const NativeRenderConstants& before,
  const NativeRenderConstants& own);

// The per-object constants 821C9C20 uploads, from the snapshot pose.
//  worlds[mesh]: the g_mWorld registers each record draws with
//    (NativeModelWorldRegisters: the NativeRigidWorld transpose as big-endian
//    words). A rigid layout uploads pose[rec+44] for every record; a skinned
//    one only for records with rec+48 clear, and any other record sees the last
//    uploaded bone or, before any, the identity (the guest would see the
//    previous object's world; nothing native can reproduce that).
//  palette: skinned only, PackNativeBonePalette(pose,limit): bones*12 floats.
// The pool constants in effect (NativeFullFrameModelEffectiveConstants) are
// not among them: they are bound per draw, and only where the draw's material
// reads one (NativeFullFrameModelDrawState::bound).
struct NativeFullFrameModelConstants {
  std::vector<std::array<uint8_t,64>> worlds;
  std::vector<float> palette;
  uint32_t bones=0;
  bool skinned=false;
};
// The pass constants an item's per-object constants replace, in pass order:
// each global whose name is one of theirs, with its first register (16
// bytes) the object's value, as 821A16D8 stores one float4 over the pool
// value that global reads; the rest of the global keeps its published bytes.
// Empty when the material reads none of them (a draw of another shader).
std::vector<NativeSceneMaterialInputs::Constant> NativeFullFrameModelObjectConstants(
  std::span<const NativeSceneMaterialInputs::Constant> pass,std::span<const NativeRenderObjectConstant> objects);
// The same over the pass constants at `slots` only (indexes of the globals
// whose names the pool carry knows, NativeFullFrameModelRowState::object_slots).
std::vector<NativeSceneMaterialInputs::Constant> NativeFullFrameModelObjectConstants(
  std::span<const NativeSceneMaterialInputs::Constant> pass,std::span<const uint32_t> slots,
  std::span<const NativeRenderObjectConstant> objects);
NativeFullFrameModelConstants NativeFullFrameModelConstantsFor(const NativeModelLayout& layout,
  std::span<const NativePoseMatrix> pose,uint32_t palette_limit);
// 821C9DA8's constants: every record uploads the one world (records with
// rec+48 set are not drawn and keep no batches in a single-world layout).
NativeFullFrameModelConstants NativeFullFrameModelInstancedConstants(const NativeModelLayout& layout,const NativePoseMatrix& world);
// Replaces every vertex global g_mWorldArray's registers with the palette over
// zeroed registers of the same extent (the shader's reflected capacity).
// False when one is not a vertex global or cannot hold the palette.
bool BindNativeFullFrameModelPalette(std::vector<NativeSceneMaterialInputs::Constant>& constants,std::span<const float> palette);
// The same for one g_mWorldArray constant (whatever its name).
bool BindNativeFullFrameModelPalette(NativeSceneMaterialInputs::Constant& constant,std::span<const float> palette);

// One draw of one item: record, batch and pass of the item's layout;
// index is its position in NativeModelDrawPlan, pass_index its position in the
// batch's pass list.
struct NativeFullFrameModelDrawRef {
  uint32_t item=0,index=0,pass_index=0;
  NativeModelDraw draw;
};
// Draw order of a list. Transparent: items in list order, each item's draws in
// guest order (record, batch, pass). Opaque: stably ordered by (pass index,
// batch, pass record), so draws of the same model resource and material are
// adjacent across entries and the renderer instances them, while every entry
// still draws its pass n before its pass n+1. Opaque records of one entry are
// thus not drawn in record order; depth testing makes that unobservable
// except for blended passes of mode-0 objects.
std::vector<NativeFullFrameModelDrawRef> OrderNativeFullFrameModelDraws(std::span<const NativeFullFrameModelItem> items,bool opaque);

// Target formats and depth direction of the pass: the definition every
// full-frame scene pass shares (native_full_frame_base_state.h).
using NativeFullFrameModelTargets=NativeFullFramePassTargets;
struct NativeFullFrameModelPass {
  NativeFullFrameModelTargets targets;
  NativeBackendViewport viewport;
  NativeBackendScissor scissor;
  int filtering=-1;
  // Word(descriptor+16) of the 821A1738 descriptor *(*(8257C02C)+36): the
  // runtime palette clamp, read once per frame by the host.
  uint32_t palette_limit=kNativeBonePaletteShaderBones;
  // The frame's motion budget (NativeFrameInputs::motion): poses blend at its
  // fraction when it interpolates (NativeRenderPoseBlender).
  NativeFrameMotion motion;
  // The guest's gather order this view (ReadNativeFullFrameModelGather):
  // the slot-4 call order and the filing order of equal keys.
  NativeFullFrameModelGather gather;
  // The pool carry (NativeFullFrameModelPoolCarry) across views and frames:
  //  tick_frame: NativeFrameInputs::tick_frame. A frame that is not (an
  //    unlocked render-only frame) starts from the pool its tick's advancing
  //    frame started from and keeps nothing, so the pool advances once per
  //    tick, as the guest's one render per tick advances it;
  //  view: the view's index in the frame (a later view starts from the
  //    previous view's end);
  //  guest_frames: renders whose slot 4s were the guest's (the guest helper
  //    or frame dispatch: A/B alternate frames). When it moved, the guest
  //    pool holds what those stores left, and every name the carry knows is
  //    taken again from `pool`;
  //  pool: the guest pool's float4 of a name (the first register, as guest
  //    bytes), or nullopt when the name is not registered or unreadable.
  bool tick_frame=true;
  uint32_t view=0;
  uint64_t guest_frames=0;
  std::function<std::optional<std::array<uint8_t,16>>(const std::string&)> pool;
};
// The explicit base state every model draw starts from, opaque and transparent
// alike: the shared full-frame base state (NativeFullFrameBaseState, the same
// operations as the static world and sky passes). A draw's state is this plus
// its own material's state operations; a transparent material's blend,
// depth-write and alpha operations are among those, so it keeps them.
inline constexpr const auto& kNativeFullFrameModelBaseOperations=kNativeFullFrameBaseOperations;
inline NativeSceneMaterialPassState NativeFullFrameModelBaseState(const NativeFullFrameModelTargets& targets) {
  return NativeFullFrameBaseState(targets);
}

// Published per-pass-record inputs. Neither may call guest code. program is
// the model pass program cache (Bridge::model_pass_loads, built by
// BuildNativeSceneMaterialLocked): the program and constant values of one
// 112-byte pass record. geometry is the retained batch geometry under the
// pass's vertex shader (Bridge::model_geometry_loads). intern is the adapter's
// InternMaterial. Missing results skip the whole entry. exclusive, when set,
// runs each material resolve (the backend's pipeline and sampler caches) or
// capture against a cached pipeline half, with its intern, in one call: the
// bridge wraps it in a short hold of its locks, so Build itself runs off them.
// program and geometry take their own holds. generation is the providers' change signal, asked once per Build (inside
// its own hold when exclusive is set): at one generation program must be a
// function of its pass record and geometry of its batch value and pass
// record. Build asks program once per pass record and geometry once per
// (pass record, batch value) per generation, keeps the answers per (pass
// record, batch, layout) (NativeFullFrameModelSourceTable) and per draw state,
// and asks again only for draw states new at that generation. It must advance
// whenever either provider could return something else (a rebuilt program,
// refreshed constant values, reloaded geometry). Without it every draw
// fetches every frame. Missing answers are never kept. Cache rows themselves
// are Build's own state and take no hold.
struct NativeFullFrameModelSources {
  std::function<std::shared_ptr<const NativeSceneGroupMaterial>(uint32_t pass)> program;
  std::function<std::shared_ptr<const NativeIndexedMesh::RetainedDraw>(const NativeModelBatchLayout&,uint32_t pass)> geometry;
  std::function<std::shared_ptr<const NativeSceneMaterial>(std::shared_ptr<const NativeSceneMaterial>)> intern;
  std::function<void(const std::function<void()>&)> exclusive;
  std::function<uint64_t()> generation;
  // Diagnostic, empty by default: every draw of an item state kept at the
  // current generation (not re-sourced this Build) is handed here with its
  // batch, pass record and the sources it holds, for the host to compare with
  // a fresh fetch (edf_native_model_source_audit).
  std::function<void(const NativeModelBatchLayout&,uint32_t pass,const std::pair<std::shared_ptr<const NativeSceneGroupMaterial>,
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw>>& sources)> audit;
};
// Build's stages, reported as each begins (the host's sub-phase timings):
// visibility (plan), programs (program and geometry sources of every drawn
// item), resolve (materials and scene objects); Done ends the last.
enum class NativeFullFrameModelPhase : uint8_t { Visibility, Programs, Resolve, Done };
// The pipeline half of one resolve, which no constant reaches: the pipeline,
// the sampler objects in program texture order, the blend factor and whether
// the resolved state enables scissor. capture is the rigid draw's interned
// capture; palette is a skinned row's capture of its pass constants, from
// which each draw derives its own with its palette bound (With).
struct NativeFullFrameModelResolve {
  NativeBackendPipeline* pipeline=nullptr;
  std::vector<NativeBackendSampler*> samplers;
  std::optional<std::array<float,4>> blend_factor;
  bool scissor=false;
  NativeSceneMaterialCapture capture;
  std::shared_ptr<NativeScenePaletteCapture> palette;
};
using NativeFullFrameModelSourcePair=std::pair<std::shared_ptr<const NativeSceneGroupMaterial>,
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw>>;
// One NativeSceneRenderer::Render call: adjacent draws sharing a view. The
// snapshot holds the objects (and so geometry and materials) until the frame
// is dropped, which must not happen before the recording is submitted.
struct NativeFullFrameModelBatch {
  NativeSceneView view;
  NativeSceneSnapshot snapshot;
  bool transparent=false;
  // Transparent batches only (one item each): the item's bucket key and its
  // filing order among the plan's transparents (its index there), for
  // MergeNativeTransparentItems with the other producers.
  uint16_t key=0;
  uint32_t order=0;
};
struct NativeFullFrameModelFrame {
  struct Stats {
    // resolves: full program.Resolve calls (cache misses); captures: captures
    // against a cached pipeline half (rigid constant changes, a skinned row's
    // pass constants moving); cache_hits: material rows (rigid or skinned)
    // whose cached resolve served this frame; memo_hits: rigid draws after
    // their row's first this frame; palettes: skinned draws derived this
    // frame from their row's capture (NativeScenePaletteCapture::With);
    // source_hits/fetches: the program and geometry side table; programs /
    // geometries: provider calls behind its fetches (once per pass record, and
    // per pass record and batch value, per source generation).
    // Persistence (NativeFullFrameModels' draw states): reused, draws whose
    // scene object was carried from an earlier frame; derived, draws whose
    // object was made this frame (palettes included); sourced, items whose
    // draws and sources were gathered this frame (new, relaid out, or a moved
    // source generation); rows, material rows evaluated (once per row per
    // frame); camera_rows, those whose pass constants took only the camera in
    // place (the published constants and animation they read unchanged).
    uint64_t items=0,drawn=0,draws=0,resolves=0,memo_hits=0,missing_program=0,missing_geometry=0,
      scissor=0,palette=0,failed=0,cache_hits=0,captures=0,palettes=0,source_hits=0,source_fetches=0,
      programs=0,geometries=0,reused=0,derived=0,sourced=0,rows=0,camera_rows=0,
      blended=0,  // Drawn items whose pose or world is blended this frame (NativeRenderPoseBlender).
      object_constants=0,  // Draw objects made this frame with per-object constants bound (a capture each).
      carried=0,           // Of those, draws binding a value another slot 4 stored (the pool carry).
      reseeds=0;           // Pool names taken again from the guest pool (guest_frames moved).
  };
  NativeFullFrameModelPlan plan;
  std::vector<NativeFullFrameModelBatch> batches;  // Opaque, then transparent.
  Stats stats;
};
// Persistent per-object draw state of NativeFullFrameModels. One item state
// per (object, registry generation, LOD model or instanced set and world):
// the layout's draws in guest order with their program and geometry, the
// constants the item's pose (or instanced world) makes, and each draw's scene
// object with the material row result it was derived from. A registry entry
// is immutable and its pose, layout and world vectors are shared objects, so
// an unchanged pose pointer is an unchanged pose; a changed one is compared
// by value before anything derived from it is dropped.
struct NativeFullFrameModelDrawState {
  NativeModelDraw draw;
  uint32_t pass_index=0,batch=0;  // Position in its batch's pass list; the batch address (opaque order).
  NativeFullFrameModelSourcePair source;
  // The row result the object was made from: a skinned row's palette capture
  // or a rigid row's interned material. Held, so its address is never reused.
  std::shared_ptr<const void> made_from;
  std::shared_ptr<const NativeSceneInstance> object;
  // The pool constants the object was made with bound (the row's globals the
  // carry knows, NativeFullFrameModelObjectConstants over the item's
  // effective constants); empty when its material reads none. Another set
  // makes the object again.
  std::vector<NativeSceneMaterialInputs::Constant> bound;
};
struct NativeFullFrameModelItemState {
  std::shared_ptr<const NativeModelLayout> layout;
  NativeRenderPose pose;  // The entry's pose, its attachment's, or the instanced set's worlds.
  // The blend the constants were made with (NativeRenderBlendOf): the previous
  // pose and fraction, or no previous pose when they are the pose's own.
  NativeRenderBlend blend;
  uint32_t world=0,palette_limit=0;
  uint64_t generation=kNativeFullFrameModelUnversioned;  // Source generation of the draws' sources.
  bool sourced=false,valued=false;
  std::vector<NativeFullFrameModelDrawState> draws;
  NativeFullFrameModelConstants values;
  uint64_t used=0;
};
// One material row's per-frame work, kept across frames: its pass constants
// (the published constants with the pass camera and animation applied), the
// g_mWorldArray constants a skinned draw binds its palette into, and this
// frame's result (the capture, palette capture, view and scissor), which
// every draw of the row shares. program and geometry are the row key's
// identities and are held; group is the published material the constants
// were copied from.
struct NativeFullFrameModelRowState {
  std::shared_ptr<const NativeSceneMaterialProgram> program;
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  std::shared_ptr<const NativeSceneGroupMaterial> group;
  std::vector<NativeSceneMaterialInputs::Constant> constants,palette_constants;
  NativeScenePassCamera camera;
  std::optional<NativeScenePassAnimation> animation;
  bool animated=false,deferrable=false,same_backend=false;
  uint64_t frame=0,used=0,draws=0;  // draws: rigid draws served this frame (memo_hits past the first).
  bool failed=false;
  std::string error;
  NativeSceneMaterialCapture capture;
  std::shared_ptr<NativeScenePaletteCapture> palette;
  // A rigid row's pipeline half this frame, for the draws that capture their
  // per-object constants against it (NativeFullFrameModelObjectConstants).
  NativeBackendPipeline* pipeline=nullptr;
  std::vector<NativeBackendSampler*> samplers;
  std::optional<std::array<float,4>> blend_factor;
  NativeSceneView view;
  bool scissor=false;
  // Indexes into constants of the globals whose names the pool carry knows
  // (at names version object_names), which a draw's pool constants bind.
  std::vector<uint32_t> object_slots;
  uint64_t object_names=0;
};
// Cross-frame state: object ids, the program/geometry side table, the
// material cache, the material rows and the draw states. Materials are
// interned through the sources; rigid ones are cached across frames per (pass
// record, program, geometry, base state, targets, filtering) with the
// constants they were captured from, and shared by every draw of the row, so
// identical entries share one material object and instance with their own
// worlds. Skinned materials carry their palette, so they never share: a
// skinned row keeps, with the same constants rule, one capture of its pass
// constants (any palette), and each draw derives its material from it with
// only its palette's registers rebound (NativeScenePaletteCapture), which is
// what a full capture of the palette-bound constants makes.
//
// In unlocked mode an item's pose blends from its previous tick's
// (NativeFullFrameModelPass::motion, NativeRenderPoseBlender): its constants
// are then keyed by the blend inputs too (previous pose, fraction), so a
// moving item's are made again each frame, while a stationary item, a pose of
// an earlier tick, alpha 1 or interpolation off keep the pose's own.
//
// Per frame, Build touches:
// - visibility, LOD and routing of every entry (PlanNativeFullFrameModels);
// - each material row once: its pass constants take the camera in place
//   (rebuilt only when the published material or a read animation moved),
//   are compared with the cached row (Current), which derives the camera;
// - the draw states of drawn items: their draws and sources only when the item
//   is new, relaid out or the source generation moved (providers asked once
//   per pass record, and per pass record and batch value, per generation);
//   their constants only when the pose (or world) pointer or palette limit
//   moved; a draw's scene object is made again only when those constants
//   moved by value, its sources changed, its row's result is another object
//   (a recapture, a new resolve) or the pool constants it binds moved (only a
//   draw whose material reads one binds any). Every other draw carries last
//   frame's object, whose material, geometry and world are what a fresh
//   build makes; only the batch views (the row cameras) are per frame.
// The output is a fresh instance's for the same inputs, except that carried
// objects keep their ids. Not synchronized.
class NativeFullFrameModels {
 public:
  // Everything is resolved before anything is recorded. An entry any of whose
  // draws cannot be resolved is skipped whole (no partial objects). phase,
  // when given, is told as each stage begins and once when all end.
  NativeFullFrameModelFrame Build(const NativeRenderRegistrySnapshot& snapshot,const NativeFullFrameModelCamera& camera,
    const NativeFullFrameModelPass& pass,const NativeFullFrameModelSources& sources,
    const std::function<void(NativeFullFrameModelPhase)>& phase={});
  // The caller has bound the pass's render targets (the frame host's BeginView).
  static NativeSceneRenderStatistics Record(NativeRenderBackend& backend,NativeSceneRenderer& renderer,
    const NativeFullFrameModelFrame& frame);
  const NativeFullFrameModelSourceTable<NativeFullFrameModelSourcePair>& source_table() const { return sources_; }
  const NativeFullFrameModelMaterialCache<NativeFullFrameModelResolve>& material_cache() const { return materials_; }
  size_t item_states() const { return items_.size(); }
  // The pose source of every drawn item (Build's hook point for pose blending).
  const NativeRenderPoseBlender& poses() const { return poses_; }
  size_t row_states() const { return rows_.size(); }
  // (object, registry generation, instanced set or -1, LOD model or world);
  // an attachment's is (object, generation, -2, its pose vector address).
  using ItemKey=std::tuple<uint32_t,uint64_t,int32_t,uint32_t>;
  using RowKey=std::tuple<uint32_t,const void*,const void*,bool>;  // (pass record, program, geometry, skinned)
 private:
  struct ItemKeyHash {
    size_t operator()(const ItemKey& key) const {
      const auto mix=[](uint64_t x) { x^=x>>33; x*=0xff51afd7ed558ccdull; x^=x>>33; return x; };
      return size_t(mix(uint64_t(std::get<0>(key))<<32^uint64_t(uint32_t(std::get<2>(key)))<<16^std::get<3>(key))^
        mix(std::get<1>(key)));
    }
  };
  // Rows and item states unused this long are dropped; past the limits, all.
  static constexpr uint64_t kStateAge=64;
  static constexpr size_t kItemLimit=16384,kRowLimit=4096;
  uint64_t next_id_=(uint64_t(5)<<60),frame_=0;
  NativeFullFrameModelSourceTable<NativeFullFrameModelSourcePair> sources_;
  NativeFullFrameModelMaterialCache<NativeFullFrameModelResolve> materials_;
  std::unordered_map<ItemKey,NativeFullFrameModelItemState,ItemKeyHash> items_;
  NativeRenderPoseBlender poses_;
  std::map<RowKey,NativeFullFrameModelRowState> rows_;
  // The providers' answers at one source generation: a program per pass
  // record, a geometry per pass record and batch value. Found ones only.
  uint64_t provided_generation_=kNativeFullFrameModelUnversioned;
  std::unordered_map<uint32_t,std::shared_ptr<const NativeSceneGroupMaterial>> provided_programs_;
  std::map<std::pair<uint32_t,uint32_t>,std::vector<std::pair<NativeModelBatchLayout,
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw>>>> provided_geometry_;
  // The pool carry (NativeFullFrameModelPass::tick_frame, view, guest_frames):
  // committed, what the last advancing frame's last view left; tick_start,
  // what that frame started from; view_end, what the last view left. names,
  // every name the carry has seen, in first-store order (names_version_
  // counts its growth, for the rows' object_slots).
  std::vector<NativeRenderObjectConstant> pool_committed_,pool_tick_start_,pool_view_end_;
  std::vector<std::string> pool_names_;
  uint64_t pool_names_version_=1,pool_guest_frames_=UINT64_MAX;
};
// Build then Record. Returns the frame, which the caller keeps until submission.
NativeFullFrameModelFrame RecordNativeModels(NativeFullFrameModels& models,const NativeRenderRegistrySnapshot& snapshot,
  const NativeFullFrameModelCamera& camera,const NativeFullFrameModelPass& pass,const NativeFullFrameModelSources& sources,
  NativeRenderBackend& backend,NativeSceneRenderer& renderer,NativeSceneRenderStatistics* statistics=nullptr);
}
