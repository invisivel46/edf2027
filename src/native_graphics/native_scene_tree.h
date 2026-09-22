#pragma once
#include "native_scene_visibility.h"
#include <stdexcept>
#include <vector>

namespace edf::native {
template<class Reader,class Draw>
void DispatchNativeSceneGroups(const Reader& reader,uint32_t list,Draw draw) {
  const auto end=reader.Word(reader.Add(list,4));
  auto node=reader.Word(end);
  while(node!=end) {
    if(node==reader.Word(reader.Add(list,4))) throw std::runtime_error("native scene group iterator invalidated");
    draw(reader.Word(reader.Add(node,8)));
    if(node==reader.Word(reader.Add(list,4))) throw std::runtime_error("native scene group iterator invalidated");
    node=reader.Word(node);
  }
}
// 821C3BB8 walks owner+240 as a circular list with a sentinel at +4:
// node+0 is next, node+8 the group; the walk stops on returning to the sentinel.
template<class Reader>
void CaptureNativeSceneGroupOrder(const Reader& reader,uint32_t list,std::vector<uint32_t>& order) {
  order.clear();
  const auto end=reader.Word(reader.Add(list,4));
  if(!end) throw std::runtime_error("native scene group list has no sentinel");
  for(auto node=reader.Word(end);node!=end;node=reader.Word(node)) {
    if(!node || order.size()>=(1u<<20)) throw std::runtime_error("invalid native scene group list");
    order.push_back(reader.Word(reader.Add(node,8)));
  }
}
inline uint32_t NativeVisibilityAabb(const NativeSceneVisibilityView& view,
    const std::array<float,4>& center,const std::array<float,4>& extent) {
  std::array<uint32_t,6> outside{};
  uint32_t inside=0;
  for(uint32_t corner=0;corner<8;++corner) {
    std::array<float,3> point;
    for(size_t c=0;c<3;++c) point[c]=(corner&(1u<<c))?float(extent[c]+center[c]):float(center[c]-extent[c]);
    const auto& m=view.matrix;
    // 821B0258 is an affine XYZ transform with a different operation order
    // from the vec4 transform used for the sphere center.
    auto x=NativeVisibilityMadd(m[4],point[1],float(m[0]*point[0]));
    x=float(NativeVisibilityMadd(point[2],m[8],x)+m[12]);
    auto y=NativeVisibilityMadd(m[4+1],point[1],float(m[8+1]*point[2]));
    y=float(NativeVisibilityMadd(m[1],point[0],y)+m[13]);
    auto z=NativeVisibilityMadd(m[4+2],point[1],float(m[8+2]*point[2]));
    z=float(NativeVisibilityMadd(m[2],point[0],z)+m[14]);
    point={x,y,z};
    const auto& f=view.frustum;
    bool contained=true;
    if(z<f[24]) { ++outside[0]; contained=false; }
    else if(z>f[25]) { ++outside[1]; contained=false; }
    for(size_t pair=0;pair<2;++pair) {
      const size_t offset=8+pair*8;
      if(NativeVisibilityMadd(point[pair],f[offset+pair],float(z*f[offset+2]))>0) {
        ++outside[2+pair*2]; contained=false;
      } else if(NativeVisibilityMadd(f[offset+4+pair],point[pair],float(f[offset+6]*z))>0) {
        ++outside[3+pair*2]; contained=false;
      }
    }
    inside+=contained;
  }
  if(inside==8) return 1;
  for(const auto count:outside) if(count==8) return 0;
  return 2;
}

// Reads remain at callback boundaries until hierarchy publication is migrated.
// Classify returns outside/inside/intersecting, and visit consumes a leaf list.
template<class Reader,class Classify,class Visit>
void TraverseNativeSceneTree(const Reader& reader,uint32_t manager,Classify classify,Visit visit) {
  reader.StoreWord(reader.Add(manager,100),0);
  reader.StoreWord(reader.Add(manager,104),0);
  const auto levels=reader.Word(reader.Add(manager,52));
  const auto level_end=reader.Word(reader.Add(manager,56));
  if(!levels || level_end<levels || level_end-levels<32)
    throw std::runtime_error("invalid native scene tree levels");
  const auto roots=reader.Add(levels,16);
  auto root=reader.Word(reader.Add(roots,4));
  const auto end=reader.Word(reader.Add(roots,8));
  if(root>end || (end-root)%144) throw std::runtime_error("invalid native scene tree roots");
  const auto walk=[&](auto&& self,uint32_t node,bool accepted,uint32_t depth)->void {
    if(depth>128) throw std::runtime_error("native scene tree depth exceeds limit");
    if(!reader.Word(reader.Add(node,116))) return;
    if(!accepted) {
      const auto count=reader.Add(manager,100);
      reader.StoreWord(count,reader.Word(count)+1);
      const auto result=classify(node);
      if(!result) return;
      accepted=result==1;
    }
    const auto children=reader.Add(node,84);
    if(!reader.Word(children)) { visit(reader.Add(node,120)); return; }
    for(uint32_t child=0;child<8;++child)
      self(self,reader.Word(reader.Add(children,child*4)),accepted,depth+1);
  };
  while(root!=end) {
    if(root>=reader.Word(reader.Add(roots,8))) throw std::runtime_error("native scene root iterator invalidated");
    walk(walk,root,false,0);
    if(root>=reader.Word(reader.Add(roots,8))) throw std::runtime_error("native scene root iterator invalidated");
    root=reader.Add(root,144);
  }
}
}
