#pragma once
#include "guest_block.h"
#include <bit>
#include <cmath>

namespace edf::native {
// World-space center (vec4) followed by three oriented half axes, not a world
// matrix. This is the retail static bound produced by 821B2B00.
struct NativeSceneVisibility {
  std::array<float,16> box{};
  float radius=0,distance=0;
  uint32_t lod_count=0;
  std::array<float,2> lod_thresholds{};
  bool operator==(const NativeSceneVisibility&) const=default;
};
struct NativeSceneVisibilityView {
  std::array<float,16> matrix{};
  std::array<float,26> frustum{};
  float depth_scale=0;
};
template<size_t N>
bool NativeVisibilityBitsEqual(const std::array<float,N>& a,const std::array<float,N>& b) {
  // IEEE equality rejects even identical NaNs. Audits compare the actual
  // transformed register bits, including NaN payloads and signed zero.
  for(size_t i=0;i<N;++i) if(std::bit_cast<uint32_t>(a[i])!=std::bit_cast<uint32_t>(b[i])) return false;
  return true;
}
inline float NativeVisibilityMadd(float a,float b,float c) {
  // Match the recompiled single-precision PPC operation, including its double
  // intermediate. Do not let a compiler contract adjacent operations instead.
  return float(std::fma(double(a),double(b),double(c)));
}
inline std::array<float,4> NativeVisibilityTransform(const std::array<float,4>& point,
    const std::array<float,16>& matrix) {
  std::array<float,4> result;
  for(size_t c=0;c<4;++c) {
    float value=float(matrix[8+c]*point[2]);
    if(c==0) {
      value=NativeVisibilityMadd(point[0],matrix[0],value);
      value=NativeVisibilityMadd(matrix[4],point[1],value);
      result[c]=NativeVisibilityMadd(matrix[12],point[3],value);
      continue;
    }
    value=NativeVisibilityMadd(matrix[4+c],point[1],value);
    value=NativeVisibilityMadd(matrix[12+c],point[3],value);
    result[c]=NativeVisibilityMadd(matrix[c],point[0],value);
  }
  return result;
}
// 0 outside, 1 inside, 2 intersecting. The camera stores two depth limits and
// four normalized side-plane coefficients. Boundary comparisons match 821C3070.
inline uint32_t NativeVisibilitySphere(const NativeSceneVisibilityView& view,
    const std::array<float,4>& center,float radius) {
  const auto& f=view.frustum;
  bool intersects=false;
  if(center[2]<float(f[24]+radius)) {
    if(center[2]<float(f[24]-radius)) return 0;
    intersects=true;
  }
  if(center[2]>float(f[25]-radius)) {
    if(center[2]>float(f[25]+radius)) return 0;
    intersects=true;
  }
  for(size_t side=0;side<4;++side) {
    const size_t offset=8+side*4,axis=side<2?0:1;
    const float distance=NativeVisibilityMadd(f[offset+axis],center[axis],float(f[offset+2]*center[2]));
    if(distance>-radius) {
      if(distance>radius) return 0;
      intersects=true;
    }
  }
  return intersects?2:1;
}
inline uint32_t NativeVisibilityBox(const NativeSceneVisibilityView& view,const std::array<float,16>& box) {
  std::array<uint32_t,6> outside{};
  uint32_t inside=0;
  for(uint32_t corner=0;corner<8;++corner) {
    std::array<float,4> point{0,0,0,1};
    for(size_t c=0;c<3;++c) {
      point[c]=(corner&1)?float(box[c]-box[4+c]):float(box[c]+box[4+c]);
      point[c]=(corner&2)?float(point[c]-box[8+c]):float(point[c]+box[8+c]);
      point[c]=(corner&4)?float(point[c]-box[12+c]):float(point[c]+box[12+c]);
    }
    point=NativeVisibilityTransform(point,view.matrix);
    const auto& f=view.frustum;
    bool contained=true;
    if(point[2]<f[24]) { ++outside[0]; contained=false; }
    else if(point[2]>f[25]) { ++outside[1]; contained=false; }
    for(size_t pair=0;pair<2;++pair) {
      const size_t offset=8+pair*8;
      if(NativeVisibilityMadd(f[offset+pair],point[pair],float(f[offset+2]*point[2]))>0) {
        ++outside[2+pair*2]; contained=false;
      } else if(NativeVisibilityMadd(f[offset+4+pair],point[pair],float(f[offset+6]*point[2]))>0) {
        ++outside[3+pair*2]; contained=false;
      }
    }
    inside+=contained;
  }
  if(inside==8) return 1;
  for(const auto count:outside) if(count==8) return 0;
  return 2;
}
inline uint32_t NativeVisibilityLod(const NativeSceneVisibility& object,float depth) {
  uint32_t lod=0;
  for(uint32_t next=1;next<object.lod_count && next<=2;++next)
    if(depth>object.lod_thresholds[next-1]) lod=next;
  return lod;
}
// One static object's per-frame visibility and LOD, as 820B4038 decides it
// (the native path of its hook): the box center in view space, the view
// depth, the distance cull, sphere then box classification, and the LOD the
// depth selects. The hook and the full-frame static world both call these, so
// the two cannot drift. The center is its own step because the hook's audit
// may replace it with the original's before the rest runs.
inline std::array<float,4> NativeVisibilityCenter(const NativeSceneVisibilityView& view,const NativeSceneVisibility& object) {
  return NativeVisibilityTransform({object.box[0],object.box[1],object.box[2],object.box[3]},view.matrix);
}
struct NativeVisibilitySelection {
  float depth=0;
  bool in_range=false;      // Not beyond the object's cull distance.
  uint32_t sphere=0,box=0;  // Classifications (0 outside); computed only in range.
  uint32_t lod=0;
  bool visible() const { return in_range && box!=0; }
};
inline NativeVisibilitySelection SelectNativeVisibility(const NativeSceneVisibilityView& view,
    const NativeSceneVisibility& object,const std::array<float,4>& center) {
  NativeVisibilitySelection result;
  result.depth=-float(center[2]*view.depth_scale);
  result.in_range=!(result.depth>object.distance);
  if(result.in_range) {
    result.sphere=NativeVisibilitySphere(view,center,object.radius);
    result.box=result.sphere==2?NativeVisibilityBox(view,object.box):result.sphere;
  }
  result.lod=NativeVisibilityLod(object,result.depth);
  return result;
}
template<size_t N,class Reader>
std::array<float,N> ReadNativeVisibilityFloats(const Reader& reader,uint32_t address) {
  const auto words=ReadGuestWords<N>(reader,address);
  std::array<float,N> values;
  for(size_t i=0;i<N;++i) values[i]=std::bit_cast<float>(words[i]);
  return values;
}
template<class Reader>
NativeSceneVisibility ReadNativeSceneVisibility(const Reader& reader,uint32_t owner,bool static_lods) {
  NativeSceneVisibility result;
  result.box=ReadNativeVisibilityFloats<16>(reader,reader.Add(owner,288));
  result.radius=std::bit_cast<float>(reader.Word(reader.Add(owner,352)));
  result.distance=std::bit_cast<float>(reader.Word(reader.Add(owner,76)));
  if(static_lods) {
    result.lod_count=reader.Word(reader.Add(owner,404));
    if(result.lod_count>3) throw std::runtime_error("invalid native visibility LOD count");
    for(uint32_t lod=1;lod<result.lod_count;++lod)
      result.lod_thresholds[lod-1]=std::bit_cast<float>(reader.Word(reader.Add(owner,448+lod*44)));
  }
  return result;
}
template<class Reader>
NativeSceneVisibilityView ReadNativeSceneVisibilityView(const Reader& reader,uint32_t context) {
  NativeSceneVisibilityView result;
  const auto camera=reader.Word(reader.Add(context,16));
  result.matrix=ReadNativeVisibilityFloats<16>(reader,reader.Add(camera,96));
  result.frustum=ReadNativeVisibilityFloats<26>(reader,reader.Add(camera,288));
  result.depth_scale=std::bit_cast<float>(reader.Word(reader.Add(context,8)));
  return result;
}
}
