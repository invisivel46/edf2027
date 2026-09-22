#pragma once
#include "guest_block.h"
#include "native_material_render_state.h"
#include "native_model_pass.h"
#include "native_model_publication.h"
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
struct NativeSkyTree { static constexpr uint32_t roots=0,root_count=8,bones=12,bone_count=20,bind=24; };
struct NativeSkyNode { static constexpr uint32_t size=304,children=80,child_count=88,inverse_bind=112,local=176,world=240; };
// Render context of 821A5080 (stack r1+80) and the scene fields the sky reads.
struct NativeSkyScene { static constexpr uint32_t context_scene=16,world=224,translation=272,camera=416; };
inline constexpr uint32_t kNativeSkyMaxNodes=4096,kNativeSkyMaxDepth=256;
using NativeSkyMatrix=std::array<float,16>;

// 821C8198(out,a,b): out = a x b, row-vector, as the recompiled body computes
// it. Each guest row is loaded byte-reversed, so host lane 0 is guest element
// 3; b is transposed with unpacks and every element is one DPPS (imm 0xFF) of
// a row and a column, whose hardware order is (l0+l1)+(l2+l3):
// (a.w*b3 + a.z*b2) + (a.y*b1 + a.x*b0). One product or sum per statement, so
// nothing contracts into a fused multiply-add. The body runs with the guest
// flush mode enabled (denormal inputs and results flush to zero); that is not
// modeled here.
inline float NativeSkyDot(const float* row,const NativeSkyMatrix& b,size_t column) {
  const float w=row[3]*b[12+column];
  const float z=row[2]*b[8+column];
  const float y=row[1]*b[4+column];
  const float x=row[0]*b[column];
  const float high=w+z;
  const float low=y+x;
  return high+low;
}
inline NativeSkyMatrix NativeSkyMultiply(const NativeSkyMatrix& a,const NativeSkyMatrix& b) {
  NativeSkyMatrix out{};
  for(size_t row=0;row<4;++row) for(size_t column=0;column<4;++column) out[row*4+column]=NativeSkyDot(a.data()+row*4,b,column);
  return out;
}
template<class Reader>
NativeSkyMatrix ReadNativeSkyMatrix(const Reader& reader,uint32_t address) {
  const auto* bytes=reader.Bytes(address,64);
  NativeSkyMatrix out{};
  for(size_t i=0;i<16;++i) out[i]=std::bit_cast<float>(GuestBlockWord(bytes+i*4));
  return out;
}
// The node tree of one bound model, in the guest's walk order (roots, each
// followed depth first by its children). parent is an index into nodes, or -1
// for a root, whose parent matrix is the sky world. Locals and inverse binds
// are the guest's bytes as floats.
struct NativeSkyHierarchy {
  struct Node { uint32_t address=0; int32_t parent=-1; NativeSkyMatrix local{},inverse_bind{}; };
  uint32_t sky=0,tree=0,roots=0,root_count=0,bone_table=0,bone_count=0;
  bool bind=false;
  std::vector<Node> nodes;
  std::vector<uint32_t> bones; // Node index per pose entry.
};
// Reads the tree the way 821C8C58/821D1688/821C9478 walk it. A cycle, an
// oversized or unaligned table, or a bone outside the walked nodes (the guest
// would copy a stale +240) throws.
template<class Reader>
NativeSkyHierarchy ReadNativeSkyHierarchy(const Reader& reader,uint32_t sky) {
  if(!sky || sky%4) throw std::runtime_error("invalid native sky object");
  NativeSkyHierarchy result; result.sky=sky;
  result.tree=reader.Add(sky,NativeSkyObject::tree);
  const auto header=ReadGuestWords<6>(reader,result.tree);
  result.roots=header[0]; result.root_count=header[2]; result.bone_table=header[3]; result.bone_count=header[5];
  result.bind=reader.Bytes(reader.Add(result.tree,NativeSkyTree::bind),1)[0]!=0;
  std::unordered_map<uint32_t,uint32_t> index;
  struct Pending { uint32_t table=0,count=0,next=0; int32_t parent=-1; };
  const auto table=[&](uint32_t address,uint32_t count) {
    if(count>kNativeSkyMaxNodes || (count && (!address || address%4))) throw std::runtime_error("invalid native sky node table");
    if(count) reader.Bytes(address,size_t(count)*NativeSkyNode::size);
  };
  table(result.roots,result.root_count);
  std::vector<Pending> stack{{result.roots,result.root_count,0,-1}};
  while(!stack.empty()) {
    auto& top=stack.back();
    if(top.next==top.count) { stack.pop_back(); continue; }
    const auto address=top.table+top.next++*NativeSkyNode::size;
    const auto parent=top.parent;
    if(result.nodes.size()>=kNativeSkyMaxNodes || !index.emplace(address,uint32_t(result.nodes.size())).second)
      throw std::runtime_error("native sky node tree is cyclic or too large");
    auto& node=result.nodes.emplace_back();
    node.address=address; node.parent=parent;
    node.local=ReadNativeSkyMatrix(reader,address+NativeSkyNode::local);
    node.inverse_bind=ReadNativeSkyMatrix(reader,address+NativeSkyNode::inverse_bind);
    const auto children=reader.Word(address+NativeSkyNode::children),count=reader.Word(address+NativeSkyNode::child_count);
    table(children,count);
    if(count) {
      if(stack.size()>=kNativeSkyMaxDepth) throw std::runtime_error("native sky node tree too deep");
      stack.push_back({children,count,0,int32_t(result.nodes.size()-1)});
    }
  }
  if(result.bone_count>kNativeModelMaxBones || (result.bone_count && (!result.bone_table || result.bone_table%4)))
    throw std::runtime_error("invalid native sky bone table");
  if(result.bone_count) {
    const auto* bytes=reader.Bytes(result.bone_table,size_t(result.bone_count)*4);
    result.bones.reserve(result.bone_count);
    for(uint32_t bone=0;bone<result.bone_count;++bone) {
      const auto found=index.find(GuestBlockWord(bytes+bone*4));
      if(found==index.end()) throw std::runtime_error("native sky bone is not a walked node");
      result.bones.push_back(found->second);
    }
  }
  return result;
}
// Cached hierarchy: rebuilt when the sky, its tree header or bind byte change,
// and every verify_interval acquisitions the node bytes are re-read and
// compared, so a local that did change after all is caught (and counted).
class NativeSkyHierarchyCache {
 public:
  uint32_t verify_interval=120;
  template<class Reader>
  const NativeSkyHierarchy& Acquire(const Reader& reader,uint32_t sky) {
    const auto tree=reader.Add(sky,NativeSkyObject::tree);
    const auto header=ReadGuestWords<6>(reader,tree);
    const bool bind=reader.Bytes(reader.Add(tree,NativeSkyTree::bind),1)[0]!=0;
    const bool same=cached_ && cached_->sky==sky && cached_->roots==header[0] && cached_->root_count==header[2] &&
      cached_->bone_table==header[3] && cached_->bone_count==header[5] && cached_->bind==bind;
    if(same && (!verify_interval || ++uses_<verify_interval)) return *cached_;
    auto fresh=ReadNativeSkyHierarchy(reader,sky);
    if(same) {
      ++verifications_;
      if(Same(*cached_,fresh)) { uses_=0; return *cached_; }
      ++changes_;
    }
    cached_=std::move(fresh); uses_=0; ++builds_;
    return *cached_;
  }
  void Reset() { cached_.reset(); uses_=0; }
  uint64_t builds() const { return builds_; }
  uint64_t verifications() const { return verifications_; }
  uint64_t changes() const { return changes_; }
 private:
  static bool Same(const NativeSkyHierarchy& a,const NativeSkyHierarchy& b) {
    if(a.nodes.size()!=b.nodes.size() || a.bones!=b.bones) return false;
    for(size_t i=0;i<a.nodes.size();++i) {
      const auto& x=a.nodes[i],&y=b.nodes[i];
      if(x.address!=y.address || x.parent!=y.parent ||
         std::bit_cast<std::array<uint32_t,16>>(x.local)!=std::bit_cast<std::array<uint32_t,16>>(y.local) ||
         std::bit_cast<std::array<uint32_t,16>>(x.inverse_bind)!=std::bit_cast<std::array<uint32_t,16>>(y.inverse_bind)) return false;
    }
    return true;
  }
  std::optional<NativeSkyHierarchy> cached_;
  uint32_t uses_=0;
  uint64_t builds_=0,verifications_=0,changes_=0;
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
// Steps 2 and 3: every node's +240 and the pose vector's entries.
struct NativeSkyPose {
  NativeSkyMatrix world{};
  std::vector<NativeSkyMatrix> nodes;       // node+240, in hierarchy order.
  std::vector<NativePoseMatrix> palette;    // Pose vector entries (this+384).
};
inline NativeSkyPose ComputeNativeSkyPose(const NativeSkyHierarchy& hierarchy,const NativeSkyMatrix& world) {
  NativeSkyPose pose; pose.world=world;
  pose.nodes.resize(hierarchy.nodes.size());
  // Parents precede children in walk order, so one forward pass suffices.
  for(size_t i=0;i<hierarchy.nodes.size();++i) {
    const auto& node=hierarchy.nodes[i];
    pose.nodes[i]=NativeSkyMultiply(node.local,node.parent<0?world:pose.nodes[size_t(node.parent)]);
  }
  pose.palette.reserve(hierarchy.bones.size());
  for(const auto bone:hierarchy.bones)
    pose.palette.push_back(hierarchy.bind?NativeSkyMultiply(hierarchy.nodes[bone].inverse_bind,pose.nodes[bone]):pose.nodes[bone]);
  return pose;
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
  bool same=state.layout && state.layout->instance==instance && state.layout->container==identity[0] &&
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
