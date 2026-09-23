#pragma once
#include "guest_block.h"
#include "native_material_render_state.h"
#include "native_model_hierarchy.h"
#include "native_model_pass.h"
#include "native_model_publication.h"
#include "native_render_instances.h"
#include "native_reuse.h"
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace edf::native {
// Native sky pass of the full-frame renderer: clSky (vtable 8200284C) without
// its slot 4, 820BB270 (edf2017_recomp.12.cpp:568), which per render does:
//   1. copy 16 bytes Word(context+16)+272 into this+272. context+16 is the
//      scene 821A5080 renders (its stack context at r1+80 stores the scene at
//      +16), and scene+224 is the 64-byte copy of the camera world scene+416
//      that 821CDDF8 makes after deriving the view, so +272 is the camera
//      world's translation row (x,y,z,w) as rendered (interpolated when the
//      821CDDF8 hook substitutes the pose). this+224 is the sky's own world:
//      rows 0-2 are set once by the constructor 820BB460 (identity 821C7AD8,
//      then the yaw/pitch of 821C7EE0 toward the construction-time camera x,0,z)
//      and never written again by clSky; row 3 is the camera translation.
//   2. 821C8C58(this+428,this+224): this+412 is the cl3D9_Model instance and
//      +428 (instance+16) its node tree: roots at +0, root count +8. Each
//      304-byte node runs 821D1688(node,parent): node+240 = 821C8198(node+176,
//      parent) (world = local x parent, row-vector 4x4), then its children
//      (+80, count +88) with parent = node+240. Nothing on this path writes a
//      local (+176), so the hierarchy is static for a bound model.
//   3. 821C9478(this+428,this+384): bone pointers at tree+12, count tree+20;
//      the pose vector (+4 begin, +8 end) is resized to the count (821C9400),
//      then pose[i] = node+240 when the byte tree+24 is zero, otherwise
//      821C8198(node+112, node+240) (inverse bind x world).
//   4. 821C9C20(this+412,this+384): the model draw, which the native model
//      layout (native_model_publication.h) and draw plan describe.
// So the pose is a pure function of the camera translation and static sky
// fields: nothing needs a guest call. The sky object comes from its
// constructor (the 820BB460 hook records it; the map loader 820B5718 keeps the
// single sky at owner+340 and releases the previous one first through
// 821C0ED8, which sets the dead byte +36); 821C0C00 skips it while the u16 at
// +64 is nonzero, as for every map effect.
struct NativeSkyObject {
  static constexpr uint32_t vtable=0x8200284C,constructor=0x820BB460,destructor=0x820BB3D8,render=0x820BB270;
  static constexpr uint32_t dead=36,mode=52,hidden=64,world=224,translation=272,pose_vector=384,instance=412,tree=428;
};
// The node tree and its walk (steps 2 and 3) are native_model_hierarchy.h's,
// shared with clBrokenObject (8211FAA8: root this+640, tree this+400).
using NativeSkyTree=NativeModelTree;
using NativeSkyNode=NativeModelTreeNode;
// Render context of 821A5080 (stack r1+80) and the scene fields the sky reads.
struct NativeSkyScene { static constexpr uint32_t context_scene=16,world=224,translation=272,camera=416; };
inline constexpr uint32_t kNativeSkyMaxNodes=kNativeModelHierarchyMaxNodes,kNativeSkyMaxDepth=kNativeModelHierarchyMaxDepth;
using NativeSkyMatrix=NativeGuestMatrix;

// 821C8198(out,a,b): out = a x b, row-vector (NativeGuestMatrixMultiply,
// native_render_instances.h, which documents the DPPS order).
inline NativeSkyMatrix NativeSkyMultiply(const NativeSkyMatrix& a,const NativeSkyMatrix& b) { return NativeGuestMatrixMultiply(a,b); }
template<class Reader>
NativeSkyMatrix ReadNativeSkyMatrix(const Reader& reader,uint32_t address) { return ReadNativeGuestMatrix(reader,address); }
// The sky's tree (this+428); a root's parent matrix is the sky world.
using NativeSkyHierarchy=NativeModelHierarchy;
template<class Reader>
NativeSkyHierarchy ReadNativeSkyHierarchy(const Reader& reader,uint32_t sky) {
  if(!sky || sky%4) throw std::runtime_error("invalid native sky object");
  return ReadNativeModelHierarchy(reader,reader.Add(sky,NativeSkyObject::tree));
}
class NativeSkyHierarchyCache : public NativeModelHierarchyCache {
 public:
  template<class Reader>
  const NativeSkyHierarchy& Acquire(const Reader& reader,uint32_t sky) {
    return NativeModelHierarchyCache::Acquire(reader,reader.Add(sky,NativeSkyObject::tree));
  }
};
// The camera translation row as 820BB270 copies it: the rendered camera
// world's row 3 (row-vector camera-to-world, NativeCameraPose::world),
// all four words bit for bit.
inline std::array<float,4> NativeSkyCameraTranslation(const std::array<float,16>& camera_world) {
  return {camera_world[12],camera_world[13],camera_world[14],camera_world[15]};
}
// Sky world this+224 after step 1: the static rows 0-2 and the camera row.
template<class Reader>
NativeSkyMatrix NativeSkyWorld(const Reader& reader,uint32_t sky,const std::array<float,4>& translation) {
  auto world=ReadNativeSkyMatrix(reader,reader.Add(sky,NativeSkyObject::world));
  for(size_t i=0;i<4;++i) world[12+i]=translation[i];
  return world;
}
// Steps 2 and 3: every node's +240 and the pose vector's entries (this+384).
using NativeSkyPose=NativeModelHierarchyPose;
inline NativeSkyPose ComputeNativeSkyPose(const NativeSkyHierarchy& hierarchy,const NativeSkyMatrix& world) {
  return ComputeNativeModelHierarchyPose(hierarchy,world);
}
// Material state operations of one pass record (the activation's list at
// +96: records of {state offset, value}), in activation order.
template<class Reader>
std::vector<std::array<uint32_t,2>> ReadNativeSkyPassStates(const Reader& reader,uint32_t pass) {
  const auto header=ReadGuestWords<3>(reader,reader.Add(pass,96));
  if(header[2]>4096) throw std::runtime_error("native sky pass state count");
  std::vector<std::array<uint32_t,2>> states;
  states.reserve(header[2]);
  for(uint32_t i=0;i<header[2];++i) {
    states.push_back(ReadGuestWords<2>(reader,reader.Add(header[0],i*8)));
    (void)NativeMaterialStateSetter(states.back()[0]); // An undecoded operation throws.
  }
  return states;
}
// Depth control as the chained render words hold it (RB_DEPTHCONTROL: bit 1
// z enable, set by op 0x28 only with a depth target bound; bit 2 z write,
// op 0x30; bits 4-6 z function, op 0x2c). A sky drawn with the test off, or
// with writes off and a far-depth vertex shader, both read from here: the
// ops decide, not an assumption about skies.
struct NativeSkyDepth {
  bool test=false,write=false;
  uint32_t function=0;
  bool operator==(const NativeSkyDepth&) const=default;
};
inline NativeSkyDepth NativeSkyDepthOf(const NativeMaterialRenderPass& pass) {
  const auto word=pass.words[1];
  return {(word&2u)!=0,(word&4u)!=0,(word>>4)&7u};
}
// The full frame's sky resolve appends z write off (op 0x30, value 0) after
// each pass's own state operations: the dome is drawn first, so it must never
// write depth (a written dome depth failed every world pixel beyond its
// radius). A pass-owned operation, the same for every sky draw.
inline constexpr std::array<std::array<uint32_t,2>,1> kNativeSkyNoDepthWrite{{{0x30,0}}};
// Which sky the frame draws. The constructor hook records the object before
// its body runs (only its address is kept); the destructor clears it when it
// is still the current one.
class NativeSkyRegistry {
 public:
  void Constructed(uint32_t sky) { current_.store(sky,std::memory_order_release); constructions_.fetch_add(1,std::memory_order_relaxed); }
  void Destroyed(uint32_t sky) { auto expected=sky; current_.compare_exchange_strong(expected,0,std::memory_order_acq_rel); }
  uint32_t Current() const { return current_.load(std::memory_order_acquire); }
  uint64_t constructions() const { return constructions_.load(std::memory_order_relaxed); }
 private:
  std::atomic<uint32_t> current_{0};
  std::atomic<uint64_t> constructions_{0};
};
// The process registry fed by the 820BB460/820BB3D8 hooks (native_full_frame_sky.cpp).
NativeSkyRegistry& NativeSkyObjects();

enum class NativeSkyStatus : uint32_t { Recorded, NoSky, NotSky, Released, Hidden, Declined };
// One draw of the sky: mesh/batch/pass as NativeModelDrawPlan orders them,
// g_mWorld as 821A17D8 stores it for the record, and the pass's render state
// chained from the frame's state at the sky, with its depth summary.
struct NativeSkyDraw {
  uint32_t mesh=0,batch=0,pass=0;
  const NativeModelBatchLayout* geometry=nullptr;
  std::array<uint32_t,16> world{};
  NativeMaterialRenderPass render;
  NativeSkyDepth depth;
};
struct NativeSkyFrameInputs {
  std::array<float,16> camera_world{};   // Rendered camera-to-world (scene+416 as 821CDDF8 consumed it).
  NativeMaterialRenderPass start;        // Pass state the sky's first pass chains from.
  uint32_t palette_limit=kNativeBonePaletteShaderBones;
};
struct NativeSkyRecord {
  NativeSkyStatus status=NativeSkyStatus::NoSky;
  uint32_t sky=0,draws=0;
  const char* reason=nullptr;
  NativeSkyPose pose;
  std::vector<std::array<uint32_t,kNativeBonePaletteFloats>> palette; // Skinned layouts only.
  NativeMaterialRenderPass end;          // State after the last pass.
};
// Per-renderer state: the hierarchy and the decoded layout, reused while the
// instance still names the same model, pose vector extent and buffers.
struct NativeSkyPassState {
  NativeSkyHierarchyCache hierarchy;
  std::optional<NativeModelLayout> layout;
  uint64_t decodes=0;
  void Reset() { hierarchy.Reset(); layout.reset(); }
};
template<class Reader,class Lookup>
const NativeModelLayout& AcquireNativeSkyLayout(const Reader& reader,NativeSkyPassState& state,uint32_t sky,const Lookup& lookup) {
  const auto instance=reader.Add(sky,NativeSkyObject::instance),vector=reader.Add(sky,NativeSkyObject::pose_vector);
  const auto identity=ReadGuestWords<2>(reader,instance);
  // Reuse off (native_reuse.h): decoded again every frame.
  bool same=NativeReuseAllowed() && state.layout && state.layout->instance==instance && state.layout->container==identity[0] &&
    state.layout->node==identity[1] && state.layout->pose_vector==vector &&
    state.layout->bones==ReadNativeModelPoseRange(reader,vector).count &&
    state.layout->skinned==(reader.Bytes(reader.Add(instance,12),1)[0]!=0);
  if(same) for(const auto& mesh:state.layout->meshes) for(const auto& batch:mesh.batches)
    if(lookup(batch.vertex.owner,NativeModelBuffers::Kind::Vertex)!=batch.vertex.generation ||
       lookup(batch.index.owner,NativeModelBuffers::Kind::Index)!=batch.index.generation) same=false;
  if(!same) { state.layout=DecodeNativeModelLayoutWith(reader,instance,vector,lookup); ++state.decodes; }
  return *state.layout;
}
// The sky pass for a full-frame renderer: finds the sky, poses it from the
// camera and hands each draw to sink(const NativeSkyDraw&) in guest order.
// lookup(owner,kind) is the NativeModelBuffers generation (0 when unknown).
// Guest memory is only read; nothing is recorded when anything declines.
template<class Reader,class Lookup,class Sink>
NativeSkyRecord RecordNativeSky(const Reader& reader,NativeSkyPassState& state,const NativeSkyFrameInputs& inputs,
    uint32_t sky,const Lookup& lookup,Sink&& sink) {
  NativeSkyRecord record; record.sky=sky; record.end=inputs.start;
  const auto decline=[&](NativeSkyStatus status,const char* reason) { record.status=status; record.reason=reason; return record; };
  if(!sky) return decline(NativeSkyStatus::NoSky,"no sky constructed");
  std::vector<NativeSkyDraw> draws;
  try {
    if(reader.Word(sky)!=NativeSkyObject::vtable) return decline(NativeSkyStatus::NotSky,"object is not a clSky");
    if(reader.Bytes(reader.Add(sky,NativeSkyObject::dead),1)[0]) return decline(NativeSkyStatus::Released,"sky released");
    const auto* hidden=reader.Bytes(reader.Add(sky,NativeSkyObject::hidden),2);
    if(hidden[0]|hidden[1]) return decline(NativeSkyStatus::Hidden,"sky hidden");
    const auto& hierarchy=state.hierarchy.Acquire(reader,sky);
    record.pose=ComputeNativeSkyPose(hierarchy,NativeSkyWorld(reader,sky,NativeSkyCameraTranslation(inputs.camera_world)));
    const auto& layout=AcquireNativeSkyLayout(reader,state,sky,lookup);
    if(layout.bones!=record.pose.palette.size()) return decline(NativeSkyStatus::Declined,"pose vector size differs from the bone table");
    for(const auto& mesh:layout.meshes) for(const auto& batch:mesh.batches)
      if(!batch.vertex.generation || !batch.index.generation) return decline(NativeSkyStatus::Declined,"sky buffers unpublished");
    if(layout.skinned) record.palette=NativeModelPaletteWords(record.pose.palette,inputs.palette_limit);
    // Every rigid record uploads its bone; palette records draw with the last upload (none: zero).
    const auto worlds=NativeModelWorldPlan(layout,record.pose.palette,std::array<uint8_t,64>{});
    auto cursor=inputs.start;
    for(const auto& draw:NativeModelDrawPlan(layout)) {
      for(const auto& [offset,value]:ReadNativeSkyPassStates(reader,draw.pass)) ApplyNativeMaterialState(cursor,offset,value);
      auto& out=draws.emplace_back();
      out.mesh=draw.mesh; out.batch=draw.batch; out.pass=draw.pass;
      out.geometry=&layout.meshes[draw.mesh].batches[draw.batch];
      for(size_t i=0;i<16;++i) out.world[i]=GuestBlockWord(worlds[draw.mesh].data()+i*4);
      out.render=cursor; out.depth=NativeSkyDepthOf(cursor);
    }
    record.end=cursor;
  } catch(const std::exception&) { return decline(NativeSkyStatus::Declined,"sky decode failed"); }
  for(const auto& draw:draws) sink(draw);
  record.status=NativeSkyStatus::Recorded; record.draws=uint32_t(draws.size());
  return record;
}
}
