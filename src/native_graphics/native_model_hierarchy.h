#pragma once
// The node hierarchy walk a slot 4 runs before 821C9C20 when it builds its own
// pose at render time (clSky 820BB270, clBrokenObject 8211FAA8), natively:
//   821C8C58(tree,root): tree is the cl3D9_Model instance + 16: roots at +0,
//     root count +8. Each 304-byte node runs 821D1688(node,parent): node+240 =
//     821C8198(node+176,parent) (world = local x parent, row-vector 4x4), then
//     its children (+80, count +88) with parent = node+240. Nothing on this
//     path writes a local (+176), so the hierarchy is static for a bound model.
//   821C9478(tree,vector): bone pointers at tree+12, count tree+20; the pose
//     vector (+4 begin, +8 end) is resized to the count (821C9400), then
//     pose[i] = node+240 when the byte tree+24 is zero, otherwise
//     821C8198(node+112, node+240) (inverse bind x world).
// The products are NativeGuestMatrixMultiply (native_render_instances.h),
// bit for bit. So the pose vector is a pure function of the root matrix and
// the tree: sized to tree+20 entries whatever the guest vector holds now.
#include "guest_block.h"
#include "native_model_publication.h"
#include "native_render_instances.h"
#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace edf::native {
struct NativeModelTree { static constexpr uint32_t roots=0,root_count=8,bones=12,bone_count=20,bind=24,instance_offset=16; };
struct NativeModelTreeNode { static constexpr uint32_t size=304,children=80,child_count=88,inverse_bind=112,local=176,world=240; };
inline constexpr uint32_t kNativeModelHierarchyMaxNodes=4096,kNativeModelHierarchyMaxDepth=256;
template<class Reader>
NativeGuestMatrix ReadNativeGuestMatrix(const Reader& reader,uint32_t address) {
  const auto* bytes=reader.Bytes(address,64);
  NativeGuestMatrix out{};
  for(size_t i=0;i<16;++i) out[i]=std::bit_cast<float>(GuestBlockWord(bytes+i*4));
  return out;
}
// The node tree of one bound model, in the guest's walk order (roots, each
// followed depth first by its children). parent is an index into nodes, or -1
// for a root, whose parent matrix is the root the caller passes. Locals and
// inverse binds are the guest's bytes as floats.
struct NativeModelHierarchy {
  struct Node { uint32_t address=0; int32_t parent=-1; NativeGuestMatrix local{},inverse_bind{}; };
  uint32_t tree=0,roots=0,root_count=0,bone_table=0,bone_count=0;
  bool bind=false;
  std::vector<Node> nodes;
  std::vector<uint32_t> bones; // Node index per pose entry.
};
// Reads the tree the way 821C8C58/821D1688/821C9478 walk it. A cycle, an
// oversized or unaligned table, or a bone outside the walked nodes (the guest
// would copy a stale +240) throws.
template<class Reader>
NativeModelHierarchy ReadNativeModelHierarchy(const Reader& reader,uint32_t tree) {
  if(!tree || tree%4) throw std::runtime_error("invalid native model tree");
  NativeModelHierarchy result; result.tree=tree;
  const auto header=ReadGuestWords<6>(reader,tree);
  result.roots=header[0]; result.root_count=header[2]; result.bone_table=header[3]; result.bone_count=header[5];
  result.bind=reader.Bytes(reader.Add(tree,NativeModelTree::bind),1)[0]!=0;
  std::unordered_map<uint32_t,uint32_t> index;
  struct Pending { uint32_t table=0,count=0,next=0; int32_t parent=-1; };
  const auto table=[&](uint32_t address,uint32_t count) {
    if(count>kNativeModelHierarchyMaxNodes || (count && (!address || address%4))) throw std::runtime_error("invalid native model node table");
    if(count) reader.Bytes(address,size_t(count)*NativeModelTreeNode::size);
  };
  table(result.roots,result.root_count);
  std::vector<Pending> stack{{result.roots,result.root_count,0,-1}};
  while(!stack.empty()) {
    auto& top=stack.back();
    if(top.next==top.count) { stack.pop_back(); continue; }
    const auto address=top.table+top.next++*NativeModelTreeNode::size;
    const auto parent=top.parent;
    if(result.nodes.size()>=kNativeModelHierarchyMaxNodes || !index.emplace(address,uint32_t(result.nodes.size())).second)
      throw std::runtime_error("native model node tree is cyclic or too large");
    auto& node=result.nodes.emplace_back();
    node.address=address; node.parent=parent;
    node.local=ReadNativeGuestMatrix(reader,address+NativeModelTreeNode::local);
    node.inverse_bind=ReadNativeGuestMatrix(reader,address+NativeModelTreeNode::inverse_bind);
    const auto children=reader.Word(address+NativeModelTreeNode::children),count=reader.Word(address+NativeModelTreeNode::child_count);
    table(children,count);
    if(count) {
      if(stack.size()>=kNativeModelHierarchyMaxDepth) throw std::runtime_error("native model node tree too deep");
      stack.push_back({children,count,0,int32_t(result.nodes.size()-1)});
    }
  }
  if(result.bone_count>kNativeModelMaxBones || (result.bone_count && (!result.bone_table || result.bone_table%4)))
    throw std::runtime_error("invalid native model bone table");
  if(result.bone_count) {
    const auto* bytes=reader.Bytes(result.bone_table,size_t(result.bone_count)*4);
    result.bones.reserve(result.bone_count);
    for(uint32_t bone=0;bone<result.bone_count;++bone) {
      const auto found=index.find(GuestBlockWord(bytes+bone*4));
      if(found==index.end()) throw std::runtime_error("native model bone is not a walked node");
      result.bones.push_back(found->second);
    }
  }
  return result;
}
// Cached hierarchy of one tree: rebuilt when the tree address, its header or
// bind byte change, and every verify_interval acquisitions the node bytes are
// re-read and compared, so a local that did change after all is caught (and
// counted). builds() moves whenever the returned hierarchy may differ.
class NativeModelHierarchyCache {
 public:
  uint32_t verify_interval=120;
  template<class Reader>
  const NativeModelHierarchy& Acquire(const Reader& reader,uint32_t tree) {
    const auto header=ReadGuestWords<6>(reader,tree);
    const bool bind=reader.Bytes(reader.Add(tree,NativeModelTree::bind),1)[0]!=0;
    const bool same=cached_ && cached_->tree==tree && cached_->roots==header[0] && cached_->root_count==header[2] &&
      cached_->bone_table==header[3] && cached_->bone_count==header[5] && cached_->bind==bind;
    if(same && (!verify_interval || ++uses_<verify_interval)) return *cached_;
    auto fresh=ReadNativeModelHierarchy(reader,tree);
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
  static bool Same(const NativeModelHierarchy& a,const NativeModelHierarchy& b) {
    if(a.nodes.size()!=b.nodes.size() || a.bones!=b.bones) return false;
    for(size_t i=0;i<a.nodes.size();++i) {
      const auto& x=a.nodes[i],&y=b.nodes[i];
      if(x.address!=y.address || x.parent!=y.parent ||
         std::bit_cast<std::array<uint32_t,16>>(x.local)!=std::bit_cast<std::array<uint32_t,16>>(y.local) ||
         std::bit_cast<std::array<uint32_t,16>>(x.inverse_bind)!=std::bit_cast<std::array<uint32_t,16>>(y.inverse_bind)) return false;
    }
    return true;
  }
  std::optional<NativeModelHierarchy> cached_;
  uint32_t uses_=0;
  uint64_t builds_=0,verifications_=0,changes_=0;
};
// 821C8C58 then 821C9478 from `root`: every node's +240 and the pose vector's entries.
struct NativeModelHierarchyPose {
  NativeGuestMatrix world{};
  std::vector<NativeGuestMatrix> nodes;     // node+240, in hierarchy order.
  std::vector<NativePoseMatrix> palette;    // Pose vector entries.
};
inline NativeModelHierarchyPose ComputeNativeModelHierarchyPose(const NativeModelHierarchy& hierarchy,const NativeGuestMatrix& root) {
  NativeModelHierarchyPose pose; pose.world=root;
  pose.nodes.resize(hierarchy.nodes.size());
  // Parents precede children in walk order, so one forward pass suffices.
  for(size_t i=0;i<hierarchy.nodes.size();++i) {
    const auto& node=hierarchy.nodes[i];
    pose.nodes[i]=NativeGuestMatrixMultiply(node.local,node.parent<0?root:pose.nodes[size_t(node.parent)]);
  }
  pose.palette.reserve(hierarchy.bones.size());
  for(const auto bone:hierarchy.bones)
    pose.palette.push_back(hierarchy.bind?NativeGuestMatrixMultiply(hierarchy.nodes[bone].inverse_bind,pose.nodes[bone]):pose.nodes[bone]);
  return pose;
}
}
