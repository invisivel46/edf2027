#include "native_graphics/native_full_frame_effects.h"

namespace edf::native {
// sub_821A7640, loc_821A778C. f31 = zero, f30 = one, f29 = pi/2, f6 = pi,
// f28 = 3pi/2. Vertex 0 takes the record's angle as loaded (stfs f13 before the
// fadds), the others fadds of it. Position, colour and radius are lfs/stfs.
std::vector<NativeParticleVertex> ExpandNativeParticleRecords(std::span<const NativeParticleRecord> records,
                                                             const NativeEffectConstants& k) {
  std::vector<NativeParticleVertex> vertices;
  vertices.reserve(records.size()*4);
  for(const auto& record:records) {
    const float angles[4]={record.angle,NativeFxAdd(record.angle,k.half_pi),NativeFxAdd(record.angle,k.pi),
                           NativeFxAdd(record.angle,k.three_half_pi)};
    const std::array<float,2> uvs[4]={{k.zero,k.zero},{k.one,k.zero},{k.one,k.one},{k.zero,k.one}};
    for(uint32_t v=0;v<4;++v) vertices.push_back({record.position,uvs[v],angles[v],record.radius,record.colour});
  }
  return vertices;
}
namespace {
NativeFxVec3 Difference(const NativeFxVec3& a,const NativeFxVec3& b) {
  return {NativeFxSub(a[0],b[0]),NativeFxSub(a[1],b[1]),NativeFxSub(a[2],b[2])};
}
}
// sub_821A8628. Per pair: e = P0 - eye, d = P1 - P0, side = SetLength(
// (dz*ey - dy*ez, ez*dx - dz*ex, dy*ex - ey*dx), width), each component one
// fmsubs of a separately rounded product. Quad P0+s, P0-s, P1-s, P1+s with U
// 0,1,1,0 (the zero/one constants) and V from each point's +20.
std::vector<NativeRibbonVertex> BuildNativeRibbonSegments(std::span<const NativeRibbonPoint> points,
    const NativeFxVec4& colour,float width,const NativeFxVec3& eye,const NativeEffectConstants& k) {
  std::vector<NativeRibbonVertex> vertices;
  if(points.size()<2) return vertices;                                   // cmpwi r5,2; blt
  const size_t segments=std::min<size_t>(points.size()/2,kNativeRibbonPointLimit);  // srawi/addze, cap 100
  vertices.reserve(segments*4);
  for(size_t s=0;s<segments;++s) {
    const auto& p0=points[s*2];
    const auto& p1=points[s*2+1];
    const auto e=Difference(p0.position,eye);
    const auto d=Difference(p1.position,p0.position);
    const float f7=NativeFxMul(d[2],e[0]),f8=NativeFxMul(d[1],e[2]),f6=NativeFxMul(e[1],d[0]);
    NativeFxVec3 side;
    side[1]=NativeFxMsub(e[2],d[0],f7);
    side[0]=NativeFxMsub(d[2],e[1],f8);
    side[2]=NativeFxMsub(d[1],e[0],f6);
    side=NativeFxSetLength(side,width,k);
    const auto plus=[&](const NativeFxVec3& p) {
      return NativeFxVec3{NativeFxAdd(p[0],side[0]),NativeFxAdd(p[1],side[1]),NativeFxAdd(p[2],side[2])};
    };
    const auto minus=[&](const NativeFxVec3& p) {
      return NativeFxVec3{NativeFxSub(p[0],side[0]),NativeFxSub(p[1],side[1]),NativeFxSub(p[2],side[2])};
    };
    vertices.push_back({plus(p0.position),{k.zero,p0.v},colour});
    vertices.push_back({minus(p0.position),{k.one,p0.v},colour});
    vertices.push_back({minus(p1.position),{k.one,p1.v},colour});
    vertices.push_back({plus(p1.position),{k.zero,p1.v},colour});
  }
  return vertices;
}
// sub_821A8090. D starts as P1 - P0. For point i the segment is A = P[i],
// B = P[i+1], except the last point, which reuses P[n-2]..P[n-1]. C = B - A,
// m = (C + D) * 0.5 per component (the x product is formed before the others,
// the stored copy is unscaled - it is m that is used), D = C, e = A - eye and
// side = SetLength((ey*mz - ez*my, ez*mx - mz*ex, my*ex - ey*mx), width). The
// pair is P[i]+s (U 0) and P[i]-s (U 1), V from P[i]+20.
std::vector<NativeRibbonVertex> BuildNativeRibbonStrip(std::span<const NativeRibbonPoint> points,
    const NativeFxVec4& colour,float width,const NativeFxVec3& eye,const NativeEffectConstants& k) {
  std::vector<NativeRibbonVertex> vertices;
  if(points.size()<2) return vertices;
  const size_t count=std::min<size_t>(points.size(),kNativeRibbonPointLimit);
  vertices.reserve(count*2);
  auto previous=Difference(points[1].position,points[0].position);
  for(size_t i=0;i<count;++i) {
    const auto& a=points[i==count-1?i-1:i].position;
    const auto& b=points[i==count-1?i:i+1].position;
    const auto current=Difference(b,a);
    const NativeFxVec3 sum{NativeFxAdd(current[0],previous[0]),NativeFxAdd(current[1],previous[1]),NativeFxAdd(current[2],previous[2])};
    const NativeFxVec3 m{NativeFxMul(sum[0],k.half),NativeFxMul(sum[1],k.half),NativeFxMul(sum[2],k.half)};
    previous=current;
    const auto e=Difference(a,eye);
    const float f6=NativeFxMul(e[1],m[0]),f8=NativeFxMul(e[2],m[1]),f7=NativeFxMul(m[2],e[0]);
    NativeFxVec3 side;
    side[2]=NativeFxMsub(m[1],e[0],f6);
    side[0]=NativeFxMsub(e[1],m[2],f8);
    side[1]=NativeFxMsub(e[2],m[0],f7);
    side=NativeFxSetLength(side,width,k);
    const auto& p=points[i].position;
    vertices.push_back({{NativeFxAdd(side[0],p[0]),NativeFxAdd(p[1],side[1]),NativeFxAdd(p[2],side[2])},{k.zero,points[i].v},colour});
    vertices.push_back({{NativeFxSub(p[0],side[0]),NativeFxSub(p[1],side[1]),NativeFxSub(p[2],side[2])},{k.one,points[i].v},colour});
  }
  return vertices;
}
std::vector<std::pair<uint32_t,uint32_t>> NativeEffectDrawCalls(const NativeEffectDraw& draw) {
  std::vector<std::pair<uint32_t,uint32_t>> calls;
  const auto total=draw.vertex_count();
  if(!total) return calls;
  if(draw.kind!=NativeEffectDraw::Kind::Particles) { calls.push_back({0,total}); return calls; }
  constexpr uint32_t chunk=kNativeParticleRecordsPerCall*4;
  for(uint32_t first=0;first<total;first+=chunk) calls.push_back({first,std::min(chunk,total-first)});
  return calls;
}
std::vector<uint8_t> EncodeNativeEffectVertices(const NativeEffectDraw& draw,uint32_t first,uint32_t count) {
  if(uint64_t(first)+count>draw.vertex_count()) throw std::runtime_error("native effect vertex range out of bounds");
  std::vector<uint8_t> bytes;
  bytes.reserve(size_t(count)*draw.stride());
  const auto put=[&](float value) {
    const auto word=std::bit_cast<uint32_t>(value);
    for(int shift=24;shift>=0;shift-=8) bytes.push_back(uint8_t(word>>shift));
  };
  for(uint32_t i=first;i<first+count;++i) {
    if(draw.kind==NativeEffectDraw::Kind::Particles) {
      const auto& v=draw.particle_vertices[i];
      for(const float f:v.position) put(f);
      for(const float f:v.uv) put(f);
      put(v.angle); put(v.radius);
      for(const float f:v.colour) put(f);
    } else {
      const auto& v=draw.ribbon_vertices[i];
      for(const float f:v.position) put(f);
      for(const float f:v.uv) put(f);
      for(const float f:v.colour) put(f);
    }
  }
  return bytes;
}
}
