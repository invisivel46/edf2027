#include "native_graphics/native_full_frame_effects.h"
#include <cstring>

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
// sub_821A8628 and sub_821A88E8 (the same arithmetic, the same order). Per
// pair: e = P0 - eye, d = P1 - P0, side = SetLength((dz*ey - dy*ez, ez*dx -
// dz*ex, dy*ex - ey*dx), width), each component one fmsubs of a separately
// rounded product. Quad P0+s, P0-s, P1-s, P1+s with U 0,1,1,0 (the zero/one
// constants) and each point's V. `colour(p)` is the colour a vertex of p gets
// and `width(p0)` the pair's width.
template<class Point,class Colour,class Width>
std::vector<NativeRibbonVertex> Segments(std::span<const Point> points,Colour colour,Width width,
    const NativeFxVec3& eye,const NativeEffectConstants& k) {
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
    side=NativeFxSetLength(side,width(p0),k);
    const auto plus=[&](const NativeFxVec3& p) {
      return NativeFxVec3{NativeFxAdd(p[0],side[0]),NativeFxAdd(p[1],side[1]),NativeFxAdd(p[2],side[2])};
    };
    const auto minus=[&](const NativeFxVec3& p) {
      return NativeFxVec3{NativeFxSub(p[0],side[0]),NativeFxSub(p[1],side[1]),NativeFxSub(p[2],side[2])};
    };
    vertices.push_back({plus(p0.position),{k.zero,p0.v},colour(p0)});
    vertices.push_back({minus(p0.position),{k.one,p0.v},colour(p0)});
    vertices.push_back({minus(p1.position),{k.one,p1.v},colour(p1)});
    vertices.push_back({plus(p1.position),{k.zero,p1.v},colour(p1)});
  }
  return vertices;
}
// sub_821A8090 and sub_821A8360 (the same arithmetic, the same order). D
// starts as P1 - P0. For point i the segment is A = P[i], B = P[i+1], except
// the last point, which reuses P[n-2]..P[n-1]. C = B - A, m = (C + D) * 0.5
// per component (the x product is formed before the others, the stored copy
// is unscaled - it is m that is used), D = C, e = A - eye and side =
// SetLength((ey*mz - ez*my, ez*mx - mz*ex, my*ex - ey*mx), width(P[i])). The
// pair is P[i]+s (U 0) and P[i]-s (U 1), both with P[i]'s V and colour(P[i]).
template<class Point,class Colour,class Width>
std::vector<NativeRibbonVertex> Strip(std::span<const Point> points,Colour colour,Width width,
    const NativeFxVec3& eye,const NativeEffectConstants& k) {
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
    side=NativeFxSetLength(side,width(points[i]),k);
    const auto& p=points[i].position;
    vertices.push_back({{NativeFxAdd(side[0],p[0]),NativeFxAdd(p[1],side[1]),NativeFxAdd(p[2],side[2])},{k.zero,points[i].v},colour(points[i])});
    vertices.push_back({{NativeFxSub(p[0],side[0]),NativeFxSub(p[1],side[1]),NativeFxSub(p[2],side[2])},{k.one,points[i].v},colour(points[i])});
  }
  return vertices;
}
NativeFxVec4 PointColour(const NativeColourRibbonPoint& point) { return point.colour; }
float PointWidth(const NativeColourRibbonPoint& point) { return point.width; }
}
// sub_821A8628: one colour and width per call, V from each point's +20.
std::vector<NativeRibbonVertex> BuildNativeRibbonSegments(std::span<const NativeRibbonPoint> points,
    const NativeFxVec4& colour,float width,const NativeFxVec3& eye,const NativeEffectConstants& k) {
  return Segments(points,[&](const NativeRibbonPoint&) { return colour; },[&](const NativeRibbonPoint&) { return width; },eye,k);
}
// sub_821A8090: one colour and width per call, V from P[i]+20.
std::vector<NativeRibbonVertex> BuildNativeRibbonStrip(std::span<const NativeRibbonPoint> points,
    const NativeFxVec4& colour,float width,const NativeFxVec3& eye,const NativeEffectConstants& k) {
  return Strip(points,[&](const NativeRibbonPoint&) { return colour; },[&](const NativeRibbonPoint&) { return width; },eye,k);
}
// sub_821A88E8 (loc_821A8994): f1 = lfs P0+32 per pair; the vertices of P0
// copy P0+16..+28 and those of P1 P1+16..+28; V from P0+40 and P1+40.
std::vector<NativeRibbonVertex> BuildNativeColourRibbonSegments(std::span<const NativeColourRibbonPoint> points,
    const NativeFxVec3& eye,const NativeEffectConstants& k) {
  return Segments(points,PointColour,PointWidth,eye,k);
}
// sub_821A8360 (loc_821A8458): f1 = lfs P[i]+32 (28(r30), r30 = P[i]+4), even
// for the last point, whose segment is P[n-2]..P[n-1]; colour P[i]+16..+28 on
// both vertices, V P[i]+40.
std::vector<NativeRibbonVertex> BuildNativeColourRibbonStrip(std::span<const NativeColourRibbonPoint> points,
    const NativeFxVec3& eye,const NativeEffectConstants& k) {
  return Strip(points,PointColour,PointWidth,eye,k);
}
std::vector<NativeColourVertex> BuildNativeColourStrip(std::span<const NativeFxVec3> points,uint32_t colour,float width,
    const NativeFxVec3& eye,const NativeEffectConstants& k) {
  std::vector<NativeColourVertex> vertices;
  if(points.size()<2) return vertices;                                   // cmpwi r27,2; blt
  const size_t count=std::min<size_t>(points.size(),kNativeRibbonPointLimit);  // cmpwi r27,100; ble / li r27,100
  std::vector<NativeRibbonPoint> strip;
  strip.reserve(count);
  for(size_t i=0;i<count;++i) strip.push_back({points[i],k.zero});
  const auto ribbon=BuildNativeRibbonStrip(strip,{},width,eye,k);
  vertices.reserve(ribbon.size());
  for(const auto& vertex:ribbon) vertices.push_back({vertex.position,colour});  // stw r26,8(r31) / 24(r31)
  return vertices;
}
std::vector<std::pair<uint32_t,uint32_t>> NativeEffectDrawCalls(const NativeEffectDraw& draw) {
  std::vector<std::pair<uint32_t,uint32_t>> calls;
  const auto total=draw.vertex_count();
  if(!total) return calls;
  // clGrassMap's 8218D3F0 draws a cell's whole list of one blade type in one
  // DrawPrimitiveUP; its quads are independent, so ranges of whole quads under
  // the immediate path's vertex limit draw the same pixels in the same order.
  if(draw.technique==NativeEffectTechnique::Utility3DTexA) {
    for(uint32_t first=0;first<total;first+=kNativeGrassMapCallVertices)
      calls.push_back({first,std::min(kNativeGrassMapCallVertices,total-first)});
    return calls;
  }
  if(draw.kind!=NativeEffectDraw::Kind::Particles) { calls.push_back({0,total}); return calls; }
  constexpr uint32_t chunk=kNativeParticleRecordsPerCall*4;
  for(uint32_t first=0;first<total;first+=chunk) calls.push_back({first,std::min(chunk,total-first)});
  return calls;
}
std::vector<uint8_t> EncodeNativeEffectVertices(const NativeEffectDraw& draw,uint32_t first,uint32_t count) {
  if(uint64_t(first)+count>draw.vertex_count()) throw std::runtime_error("native effect vertex range out of bounds");
  // Big-endian words in declaration order, written in place: this runs for
  // every effect vertex of every frame.
  std::vector<uint8_t> bytes(size_t(count)*draw.stride());
  auto* out=bytes.data();
  const auto word=[&](uint32_t value) { value=std::byteswap(value); std::memcpy(out,&value,4); out+=4; };
  const auto put=[&](float value) { word(std::bit_cast<uint32_t>(value)); };
  for(uint32_t i=first;i<first+count;++i) {
    if(draw.kind==NativeEffectDraw::Kind::Particles) {
      const auto& v=draw.particle_vertices[i];
      for(const float f:v.position) put(f);
      for(const float f:v.uv) put(f);
      put(v.angle); put(v.radius);
      for(const float f:v.colour) put(f);
    } else if(draw.kind==NativeEffectDraw::Kind::ColourStrip) {
      const auto& v=draw.colour_vertices[i];
      for(const float f:v.position) put(f);
      word(v.colour);
    } else {
      const auto& v=draw.ribbon_vertices[i];
      for(const float f:v.position) put(f);
      for(const float f:v.uv) put(f);
      for(const float f:v.colour) put(f);
    }
  }
  if(out!=bytes.data()+bytes.size()) throw std::runtime_error("native effect vertex encoding does not fill its stride");
  return bytes;
}
void EncodeNativeEffectDrawCalls(NativeEffectDraw& draw) {
  draw.encoded_calls.clear();
  for(const auto& [first,count]:NativeEffectDrawCalls(draw)) draw.encoded_calls.push_back(EncodeNativeEffectVertices(draw,first,count));
}
}
