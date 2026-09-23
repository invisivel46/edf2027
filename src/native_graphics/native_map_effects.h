#pragma once
#include "native_graphics/native_full_frame_effects.h"
#include "native_graphics/native_render_instances.h"
#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace edf::native {
// clMapEffectManager (vtable 0x82002214) keeps an intrusive list header at
// +48. Its constructor sub_820B3620 clears +0/+4 through sub_821D1880 and then
// +8/+12; sub_821A1628 links nodes at the head (+0 next, +4 prev). Each node
// carries its scene object at +8. sub_820B35A0 (reached through the slot-2
// adapter sub_820B3610 with r4=manager+48, r5=context) reads the first node and
// the end marker once, loads the object before calling sub_821C0C00 and loads
// the next link only after that call returns: a callback that unlinks or
// relinks the current node redirects the walk, and a node inserted at the head
// is not visited. The native walk keeps exactly those read points.
struct NativeMapEffectList {
  static constexpr uint32_t manager_offset=48,first=0,end=12,next=0,object=8;
};
// Object fields sub_821C0C00 dispatches on: nonzero +64 (u16) skips the object,
// +52 selects slot 4 (mode 0) or the mode-1/2 bucket path (sub_821A3B80).
struct NativeMapEffectObject {
  static constexpr uint32_t mode=52,hidden=64,render_slot=16;
};
template<class Reader,class Call>
uint32_t WalkNativeMapEffects(const Reader& reader,uint32_t list,Call&& call) {
  auto node=reader.Word(reader.Add(list,NativeMapEffectList::first));   // lwz r31,0(r4)
  const auto end=reader.Word(reader.Add(list,NativeMapEffectList::end)); // lwz r30,12(r4)
  uint32_t visited=0;
  while(node!=end) {
    call(reader.Word(reader.Add(node,NativeMapEffectList::object)));     // lwz r3,8(r31)
    node=reader.Word(reader.Add(node,NativeMapEffectList::next));        // lwz r31,0(r31), after the call
    ++visited;
  }
  return visited;
}
// RTTI names (edf-analysis classes table) of the vtables whose slot 4 is a
// scene-object render method; anything else prints as its address. The
// map-effect list's real members are clSky, clElectricWire and clGrassMap;
// clEffectEtc01/02 and clEffectGlass belong to the "Effect" layer (the
// clEffectObjectManager list, NativeEffectList), so they are not named here.
inline const char* NativeMapEffectClassName(uint32_t vtable) {
  switch(vtable) {
    case 0x82002158: return "clMapArtifact_Base";
    case 0x8200225c: return "clMapObject_Base";
    case 0x820026bc: return "clBuilding";
    case 0x82002744: return "clElectricWire";
    case 0x82002760: return "clFieldParts";
    case 0x8200277c: return "clNameBoard";
    case 0x820027a4: return "clNameBoard_Board";
    case 0x820027c0: return "clNameBoard_Box";
    case 0x8200284c: return "clSky";
    case 0x82002868: return "clSmallBoard";
    case 0x82002884: return "clSmallEtc";
    case 0x820028a0: return "clSmallPole";
    case 0x820124dc: return "clGrassMap";
    case 0x82019a2c: return "clObject_Base@Sgs";
    default: return nullptr;
  }
}
struct NativeMapEffectClass {
  uint32_t vtable=0,mode=0,render=0;
  auto operator<=>(const NativeMapEffectClass&) const=default;
};
// Read-only tally of the objects the walk would visit. It follows only the
// links present before the original runs, so it never observes callback
// mutations; it exists to rank what the per-object port has to cover.
class NativeMapEffectCensus {
 public:
  struct Row { NativeMapEffectClass key; uint64_t count=0,hidden=0; double percent=0; };
  template<class Reader>
  uint32_t Record(const Reader& reader,uint32_t list,uint32_t limit=1u<<20) {
    auto node=reader.Word(reader.Add(list,NativeMapEffectList::first));
    const auto end=reader.Word(reader.Add(list,NativeMapEffectList::end));
    uint32_t visited=0;
    for(;node!=end;node=reader.Word(reader.Add(node,NativeMapEffectList::next))) {
      if(visited==limit) { ++truncated_; break; }
      const auto object=reader.Word(reader.Add(node,NativeMapEffectList::object));
      const auto table=reader.Word(object);
      const NativeMapEffectClass key{table,reader.Word(reader.Add(object,NativeMapEffectObject::mode)),
        reader.Word(reader.Add(table,NativeMapEffectObject::render_slot))};
      const auto* hidden=reader.Bytes(reader.Add(object,NativeMapEffectObject::hidden),2);
      auto& tally=counts_[key];
      ++tally.count; if(hidden[0]|hidden[1]) ++tally.hidden;
      ++visited;
    }
    ++walks_; objects_+=visited;
    return visited;
  }
  std::vector<Row> Top(size_t limit) const {
    std::vector<Row> rows; rows.reserve(counts_.size());
    for(const auto& [key,tally]:counts_)
      rows.push_back({key,tally.count,tally.hidden,objects_?100.0*double(tally.count)/double(objects_):0.0});
    // Count descending; equal counts keep key order so summaries are stable.
    std::stable_sort(rows.begin(),rows.end(),[](const Row& a,const Row& b) { return a.count>b.count; });
    if(rows.size()>limit) rows.resize(limit);
    return rows;
  }
  void Reset() { counts_.clear(); walks_=objects_=truncated_=0; }
  uint64_t walks() const { return walks_; }
  uint64_t objects() const { return objects_; }
  uint64_t truncated() const { return truncated_; }
  size_t classes() const { return counts_.size(); }
 private:
  struct Tally { uint64_t count=0,hidden=0; };
  std::map<NativeMapEffectClass,Tally> counts_;
  uint64_t walks_=0,objects_=0,truncated_=0;
};

// ---- The full frame's native map-effect walk ----
// The helper's world list (owner+44, end word owner+56, nodes {+0 next,
// +4 prev, +8 object}) holds clMapEffectManager (vtable 82002214), whose slot 2
// sub_820B3610 walks its +48 list with sub_820B35A0: no culling, every node,
// 821C0C00 per object (u16 +64 nonzero skips; +52 == 0 calls slot 4 inside
// the walk, 1/2 file a bucket key). Its members are clSky, clElectricWire and
// clGrassMap (PlanNativeMapEffects routes each). The native walk only reads: no
// guest callback runs, so no link can change during it, and its order is the
// list's.
inline constexpr uint32_t kNativeMapEffectManagerVtable=0x82002214;
struct NativeWorldList { static constexpr uint32_t head=44,end=56,next=0,object=8,limit=4096; };
// The first object on the helper's world list whose vtable is `vtable`, or 0.
template<class Reader>
uint32_t FindNativeWorldListObject(const Reader& r,uint32_t owner,uint32_t vtable) {
  const auto end=r.Word(r.Add(owner,NativeWorldList::end));
  uint32_t guard=0;
  for(auto node=r.Word(r.Add(owner,NativeWorldList::head));node!=end;node=r.Word(r.Add(node,NativeWorldList::next))) {
    if(++guard>NativeWorldList::limit) throw std::runtime_error("native world list does not terminate");
    const auto object=r.Word(r.Add(node,NativeWorldList::object));
    if(object && r.Word(object)==vtable) return object;
  }
  return 0;
}
enum class NativeMapEffectKind : uint8_t { Sky, ElectricWire, GrassMap, Other };
inline constexpr NativeMapEffectKind ClassifyNativeMapEffect(uint32_t vtable) {
  return vtable==0x8200284Cu?NativeMapEffectKind::Sky:vtable==0x82002744u?NativeMapEffectKind::ElectricWire:
         vtable==0x820124DCu?NativeMapEffectKind::GrassMap:NativeMapEffectKind::Other;
}
struct NativeMapEffectMember {
  uint32_t object=0,vtable=0,render=0;
  int32_t mode=0;
  bool hidden=false;
  NativeMapEffectKind kind=NativeMapEffectKind::Other;
};
// The list's objects in walk order with 821C0C00's route words (read-only).
template<class Reader>
std::vector<NativeMapEffectMember> CollectNativeMapEffectMembers(const Reader& r,uint32_t manager,uint32_t limit=1u<<16) {
  if(r.Word(manager)!=kNativeMapEffectManagerVtable) throw std::runtime_error("not a clMapEffectManager");
  const auto list=r.Add(manager,NativeMapEffectList::manager_offset);
  std::vector<NativeMapEffectMember> members;
  const auto end=r.Word(r.Add(list,NativeMapEffectList::end));
  for(auto node=r.Word(r.Add(list,NativeMapEffectList::first));node!=end;node=r.Word(r.Add(node,NativeMapEffectList::next))) {
    if(members.size()>=limit) throw std::runtime_error("native map-effect list does not terminate");
    auto& member=members.emplace_back();
    member.object=r.Word(r.Add(node,NativeMapEffectList::object));
    member.vtable=r.Word(member.object);
    member.render=r.Word(r.Add(member.vtable,NativeMapEffectObject::render_slot));
    const auto* hidden=r.Bytes(r.Add(member.object,NativeMapEffectObject::hidden),2);
    member.hidden=(hidden[0]|hidden[1])!=0;
    member.mode=int32_t(r.Word(r.Add(member.object,NativeMapEffectObject::mode)));
    member.kind=ClassifyNativeMapEffect(member.vtable);
  }
  return members;
}

// clElectricWire (vtable 82002744), slot 4 sub_820B8D28, per 96-byte record of
// the vector at this+384 (begin +388, end +392, the end word read once; a
// record past it traps):
//   - byte +88 zero skips;
//   - ends A = +32 and B = +48 through the view matrix Word(context+16)+96
//     (821B0258); when |A|^2 > 90000 [82002710] and |B|^2 > 90000 (each sum in
//     its own fmadds order) the record is skipped;
//   - the centre +64 through the same matrix must pass 821C2FD0 against the
//     frustum Word(context+16)+288 with radius +80;
//   - ten points: P starts as the 16 bytes at +32 and steps (B-A)*(1/9)
//     [82002714]; point i is P before its step, with y -= s and x,z += t*s,
//     where s = sin(angle), the angle a running fadds from 0.0 by 0.349066
//     [82002718], and t = sin(this+400 + rec+84) * 0.5 [820008D4] (sin is
//     821E9558, each result frsp);
//   - 821A7E08(r3 [8257C034], r4 the points, r5 10, f1 0.03 [8200271C],
//     r7 0xFF404040, r8 0, r9 1): BuildNativeColourStrip, then 821A7B58 with
//     blend 0 (SRCALPHA/INVSRCALPHA) and depth write on (r8 == 1).
// Its mode (+52) is 0: 821C0C00 runs slot 4 inside the map-effect walk, so the
// draws are immediate, in list order. Reads only; nothing is written.
struct NativeElectricWire {
  static constexpr uint32_t vtable=0x82002744,render=0x820B8D28;
  static constexpr uint32_t records=388,records_end=392,phase=400,stride=96,max_records=4096;
  static constexpr uint32_t a=32,b=48,centre=64,radius=80,offset=84,enabled=88,points=10;
  static constexpr uint32_t colour=0xFF404040u,depth_flag=1;
  static constexpr int32_t blend=kNativeEffectBlendAlpha;
};
struct NativeElectricWireConstantAddress {
  static constexpr uint32_t distance=0x82002710,ninth=0x82002714,angle_step=0x82002718,width=0x8200271C;
};
struct NativeElectricWireConstants { float distance=90000.f,ninth=1.f/9,angle_step=0.349066f,width=0.03f; };
template<class Reader>
NativeElectricWireConstants ReadNativeElectricWireConstants(const Reader& r) {
  using A=NativeElectricWireConstantAddress;
  const auto f=[&](uint32_t at) { return std::bit_cast<float>(r.Word(at)); };
  return {f(A::distance),f(A::ninth),f(A::angle_step),f(A::width)};
}
// sub_821B0258(out, p, m): p (x,y,z) through the row-vector matrix m, in its
// product order.
inline NativeFxVec3 NativeWireTransform(const NativeFxVec3& p,const std::array<float,16>& m) {
  const float x=p[0],y=p[1],z=p[2];
  float f8=NativeFxMul(m[9],z),f7=NativeFxMul(m[10],z);
  const float f9=NativeFxMul(m[0],x);
  f8=NativeFxMadd(m[5],y,f8); f7=NativeFxMadd(m[6],y,f7);
  const float f11=NativeFxMadd(m[1],x,f8),f10=NativeFxMadd(m[2],x,f7);
  const float f13=NativeFxMadd(m[4],y,f9);
  return {NativeFxAdd(NativeFxMadd(z,m[8],f13),m[12]),NativeFxAdd(f11,m[13]),NativeFxAdd(f10,m[14])};
}
// sub_821C2FD0(frustum, centre, radius): the depth pair (+96/+100), then four
// planes (+32/+40 and +48/+56 on x, +68/+72 and +84/+88 on y). An unordered
// comparison does not reject (fcmpu, then bge/bgt).
inline bool NativeWireSphereVisible(const std::array<float,26>& f,const NativeFxVec3& c,float radius) {
  if(c[2]<NativeFxSub(f[24],radius)) return false;
  if(c[2]>NativeFxAdd(f[25],radius)) return false;
  if(NativeFxMadd(f[8],c[0],NativeFxMul(f[10],c[2]))>radius) return false;
  if(NativeFxMadd(f[12],c[0],NativeFxMul(f[14],c[2]))>radius) return false;
  if(NativeFxMadd(f[17],c[1],NativeFxMul(f[18],c[2]))>radius) return false;
  if(NativeFxMadd(f[21],c[1],NativeFxMul(f[22],c[2]))>radius) return false;
  return true;
}
// The sag of each of the ten points: frsp(sin(angle)) with the angle a running
// fadds from 0.0 by the step. It depends on image constants alone, so one table
// serves every record of every wire (the guest recomputes it per point).
using NativeElectricWireSines=std::array<float,NativeElectricWire::points>;
inline NativeElectricWireSines NativeElectricWireSag(const NativeEffectConstants& k,const NativeElectricWireConstants& w) {
  NativeElectricWireSines sines{};
  float angle=k.zero;                                           // f31
  for(auto& s:sines) {
    s=float(NativeGuestSin(double(angle)));                     // frsp f30
    angle=NativeFxAdd(angle,w.angle_step);
  }
  return sines;
}
// The ten points of one record as 820B8D28's inner loop leaves them at r1+144.
// sines is NativeElectricWireSag(k, w); the swing sin(phase + offset), which
// the guest recomputes at every point from the same two floats, is taken once.
// The same bits as recomputing both per point, except the sign of a NaN (a NaN
// phase or step), which NativeGuestSin's host fma does not fix either way.
inline std::array<NativeFxVec3,NativeElectricWire::points> NativeElectricWirePoints(const NativeFxVec3& a,const NativeFxVec3& b,
    float phase,float offset,const NativeEffectConstants& k,const NativeElectricWireConstants& w,const NativeElectricWireSines& sines) {
  const float sx=NativeFxMul(NativeFxSub(b[0],a[0]),w.ninth);   // f26
  const float sy=NativeFxMul(NativeFxSub(b[1],a[1]),w.ninth);   // f25
  const float sz=NativeFxMul(NativeFxSub(b[2],a[2]),w.ninth);   // f24
  float px=a[0],py=a[1],pz=a[2];                               // f29 f28 f27
  const float swing=float(NativeGuestSin(double(NativeFxAdd(phase,offset))));
  std::array<NativeFxVec3,NativeElectricWire::points> out{};
  for(size_t i=0;i<out.size();++i) {
    auto& point=out[i];
    point={px,py,pz};                                           // the 16-byte copy of r1+112
    const float s=sines[i];
    px=NativeFxAdd(sx,px);
    point[1]=NativeFxSub(point[1],s);
    py=NativeFxAdd(py,sy); pz=NativeFxAdd(pz,sz);
    float t=NativeFxMul(swing,k.half); t=NativeFxMul(t,s);
    point[0]=NativeFxAdd(point[0],t);
    point[2]=NativeFxAdd(t,point[2]);
  }
  return out;
}
inline std::array<NativeFxVec3,NativeElectricWire::points> NativeElectricWirePoints(const NativeFxVec3& a,const NativeFxVec3& b,
    float phase,float offset,const NativeEffectConstants& k,const NativeElectricWireConstants& w) {
  return NativeElectricWirePoints(a,b,phase,offset,k,w,NativeElectricWireSag(k,w));
}
struct NativeElectricWireStats { uint32_t records=0,disabled=0,distant=0,culled=0,drawn=0; };
// Every draw 820B8D28 issues for `wire` in the view whose scene is `scene`
// (Word(context+16)), in record order. Throws on an implausible vector.
// scene+96 and +288 are the camera the pass draws with, unlocked included:
// only 821CDDF8 writes them, and the frustum builder 821C26C0 has no other
// caller. The hook runs it at 821A4EB0 on the interpolated pose, 821A4DE8
// then publishes the pass camera by copying +32/+96/+160 (so its view is
// +96's bytes), and nothing writes them again before the helper that draws
// this frame is joined. The frustum is in view space (fov +480, aspect
// +496/+500), so +288 is the one for that camera.
template<class Reader>
std::vector<NativeEffectDraw> BuildNativeElectricWireDraws(const Reader& r,uint32_t wire,uint32_t scene,
    const NativeEffectInputs& in,NativeElectricWireStats* stats=nullptr) {
  using W=NativeElectricWire;
  NativeElectricWireStats local;
  auto& count=stats?*stats:local;
  const auto w=ReadNativeElectricWireConstants(r);
  const auto matrix=ReadNativeVisibilityFloats<16>(r,r.Add(scene,96));
  const auto frustum=ReadNativeVisibilityFloats<26>(r,r.Add(scene,288));
  const auto begin=r.Word(r.Add(wire,W::records)),end=r.Word(r.Add(wire,W::records_end));
  if(begin>end || (end-begin)%W::stride || (end-begin)/W::stride>W::max_records)
    throw std::runtime_error("invalid native electric wire records");
  const float phase=ReadNativeFxFloat(r,r.Add(wire,W::phase));
  const auto sines=NativeElectricWireSag(in.k,w);
  std::vector<NativeEffectDraw> draws;
  // The record vector is read as one block (the fields are its bytes; an
  // unreadable vector throws before any draw, as a trap on any record loses
  // the whole wire's draws anyway).
  const auto* bytes=end!=begin?r.Bytes(begin,end-begin):nullptr;
  const auto field=[](const uint8_t* at) {
    return std::bit_cast<float>(uint32_t(at[0])<<24|uint32_t(at[1])<<16|uint32_t(at[2])<<8|uint32_t(at[3]));
  };
  const auto vec3=[&](const uint8_t* at) { return NativeFxVec3{field(at),field(at+4),field(at+8)}; };
  for(uint32_t index=0;index<(end-begin)/W::stride;++index) {
    const auto* record=bytes+size_t(index)*W::stride;
    ++count.records;
    if(!record[W::enabled]) { ++count.disabled; continue; }
    const auto a=vec3(record+W::a),b=vec3(record+W::b);
    const auto va=NativeWireTransform(a,matrix);
    float sum=NativeFxMul(va[1],va[1]);
    sum=NativeFxMadd(va[2],va[2],sum); sum=NativeFxMadd(va[0],va[0],sum);
    if(sum>w.distance) {
      const auto vb=NativeWireTransform(b,matrix);
      sum=NativeFxMul(vb[1],vb[1]);
      sum=NativeFxMadd(vb[0],vb[0],sum); sum=NativeFxMadd(vb[2],vb[2],sum);
      if(sum>w.distance) { ++count.distant; continue; }
    }
    const auto centre=NativeWireTransform(vec3(record+W::centre),matrix);
    if(!NativeWireSphereVisible(frustum,centre,field(record+W::radius))) { ++count.culled; continue; }
    const auto points=NativeElectricWirePoints(a,b,phase,field(record+W::offset),in.k,w,sines);
    auto vertices=BuildNativeColourStrip(points,W::colour,w.width,in.eye,in.k);
    if(vertices.empty()) continue;
    draws.push_back(MakeNativeColourStripDraw(in.effect,std::move(vertices),W::blend,W::depth_flag));
    ++count.drawn;
  }
  return draws;
}

// ---- clGrassMap (vtable 820124DC), slot 4 sub_82172698 ----
// Its constructor 82172F78 sets mode 2 (821C0AF8: +52 = 2) and +56 = 1.0
// (821C0B00), so 821C0C00 files it with key fctidz(min(1.0 * 65536
// [82019A20], 65535)) = 65535 and the drain 821A3BA0 reaches its slot 4 among
// the first drawn keys, in filing order on a tie (NativeTransparentItem). What
// it reads is GrassMap.bin as 82172F78 loaded it:
//  +1108/+1112 sub-cell size (x, z: +1132/+1136 over the counts +1100/+1104),
//  +1124/+1128 the grid origin, +1140/+1144 the cell size (+1200 sub-cells a
//  side), +1148/+1152 half a cell, +1156 the reach (62.0 [820124D4] before the
//  load), +1176 the +1208 x +1212 grid of int32 slots (< 0: no grass), +1188
//  the u16 pairs (+1200^2 per slot), +1216 a (lo, hi) height pair per slot,
//  +396+t*16 blade type t's RGBA floats (1,1,1,0.5 by default), +668+i*16
//  list i's texture word (the holders 8210AB08 builds at +652+t*16), +920+i*12
//  the 15 vertex vectors (begin, end, capacity; 82171CF0 reserves +1200^2*120
//  each and 82172F78 writes every quad's UVs (0,1) (1,1) (1,0) (0,0) once:
//  nothing writes them again), and the blade table at 82554480 (.data, 8
//  bytes per type: base and top size, written by 82172F78 for types 1..15).
//
// slot 4 sub_82172698(this, context): +1168 = 0; nothing unless bytes +1164
//  and +1248 are set. The camera world Word(context+16)+416 is copied to the
//  stack, context stored at +1228 and the world's translation row (+464..
//  +476) at +1232..+1244. With ix = 1/+1108 and iz = 1/+1112 (fdivs from
//  1.0 [820008CC]): extent = +1200 + max(fctiwz(+1156 * iz), fctiwz(ix * +1156))
//  (cmpw/bge), cx = fctiwz((+1232 - +1124) * ix) / +1200, cz = fctiwz((+1240 -
//  +1128) * iz) / +1200, rings = (extent - 1) / +1200 (divw; twllei traps a zero
//  +1200, twlgei INT_MIN / -1). 8218D380 pushes three render states (8219CE18
//  on [8257BFB4], ids at 82017728) and 8218D398(this+1256, 0, 0) sets
//  SRCALPHA/INVSRCALPHA (8218D318 r4 0: 82135078 6, 82135108 7) and depth
//  write off (82135578 r4 = r5 == 1). Then 82171F58(cx, cz), the rings
//  (WalkNativeGrassMapRings), 8218D430 (8219C9D8 pops the three states) and
//  +1172 = max(+1172, +1168).
// cell sub_82171F58: BuildNativeGrassMapDraws, statement for statement.
// cull sub_82171BD8(this, f1 limit): the bound +288..+300 (w = 1.0) through
//  821B0198 (NativeVisibilityTransform) into context+32 with the view matrix
//  Word(context+16)+96; out when -(context+8 * z) > limit (fcmpu/ble: a NaN
//  goes on), else 821C2FD0 (NativeWireSphereVisible) against
//  Word(context+16)+288 with the radius +352; r3 = result != 0.
// distance sub_821B03C8(v): y*y, fmadds x, fmadds z; the zero constant
//  [820009A4] when the sum equals it, else fsqrts.
// draw per list i = type-1 with a count: 8218D440(this+1256, Word(+668+i*16))
//  (821BC4C8 the texture into the Utility_3DTexA technique's sampler list
//  +1256+200, 821B94E8([+1256+188]), 82149A90 the declaration Word(Word(
//  +1256+64)+28), trapping on Word(+1256+60) == 0 or Word(+1256+64) ==
//  Word(Word(+1256+60)+4)); 8218D3F0(this+1256, 13, Word(+920+i*12), count):
//  821FD8F8 with stride 36 and count * [82008888+13*8] + [+4] vertices (4 per
//  quad). The declaration is 821D34F0's from 825558FC - position FLOAT3 +0,
//  texcoord FLOAT2 +12, colour FLOAT4 +20 - which is the VS_3DTex layout the
//  ribbons' immediate path already uses.
// The camera the native build takes (NativeGrassMap::camera_translation) is
// not +416 but the scene's +224, the 64-byte copy of +416 that 821CDDF8 makes
// in the same call that derives the view +96 and the frustum +288 this build
// culls with (the sky reads the same copy, NativeSkyScene). The two are the
// same bytes whenever +416 still holds what 821CDDF8 consumed. They differ in
// two cases. Unlocked, the 821CDDF8 hook derives +96/+224/+288 from the
// interpolated pose and then puts the tick pose back into +416, so +416
// would centre the rings and the blades' distance fade on the tick camera
// while the cull and the draw use the interpolated one. And the helper runs
// beside the next step (821D58E8 releases it before 821A4BA0; 821D5800 joins
// it after), whose camera update writes +416, so a live +416 read races with
// the simulation. That is also true of the guest's own read, which can
// therefore see the next tick's position. +224 is written only by 821CDDF8
// (at 821A4EB0, after the join, and by the scene constructor 821CE168), so
// it is the camera of the frame being drawn.
//
// The native build only reads. The guest's own writes are left out: the
// counters +1168/+1172 (read by nothing but 82172698), +1228 and +1232..
// +1244, the bound +288..+352, context+32..+44 (821B0198's output; the drain
// has filed every key by then) and the positions and colours in the +920
// vectors (each draw's bytes are built here instead).
struct NativeGrassMap {
  static constexpr uint32_t vtable=0x820124DC,render=0x82172698;
  static constexpr uint32_t colours=396,textures=668,lists=920,sub_x=1108,sub_z=1112,origin_x=1124,origin_z=1128,
    cell_x=1140,cell_z=1144,half_x=1148,half_z=1152,reach=1156,loaded=1164,slots=1176,blades=1188,subcells=1200,
    width=1208,depth=1212,heights=1216,enabled=1248,utility=1256;
  static constexpr uint32_t types=15,colour_stride=16,texture_stride=16,list_stride=12,vertex_bytes=36,uv=12,
    quad_primitive=13,depth_flag=0;
  static constexpr int32_t blend=kNativeEffectBlendAlpha;
  // .data blade sizes (8 bytes per type) and 8218D3F0's per-primitive vertex
  // table (8 bytes per primitive type: per primitive, plus).
  static constexpr uint32_t blade_table=0x82554480,primitive_table=0x82008888;
  // Word(context+16)+224 row 3: the rendered copy of the +416 world whose
  // row 3 (+464) 82172698 reads (see above).
  static constexpr uint32_t camera_world=416,rendered_camera_world=224;
  static constexpr uint32_t camera_translation=rendered_camera_world+48;
  static constexpr uint32_t declarations=60,declaration=64;   // in the Utility object (8218D440's traps)
  // The native pass's own refusals, not the guest's.
  static constexpr int32_t max_rings=256,max_subcells=256;
};
struct NativeGrassMapConstantAddress {
  static constexpr uint32_t zero=0x820009A4,one=0x820008CC,half=0x820008D4,quarter=0x820021E4,three_quarters=0x820024A0,
    fifteenth=0x82004FF8,sixteenth=0x82009654,angle=0x820124CC,root3=0x820124D0;
};
struct NativeGrassMapConstants {
  float zero=0,one=1,half=.5f,quarter=.25f,three_quarters=.75f,fifteenth=1.f/15,sixteenth=.0625f,angle=0.418879f,root3=1.73205f;
};
template<class Reader>
NativeGrassMapConstants ReadNativeGrassMapConstants(const Reader& r) {
  using A=NativeGrassMapConstantAddress;
  const auto f=[&](uint32_t at) { return std::bit_cast<float>(r.Word(at)); };
  return {f(A::zero),f(A::one),f(A::half),f(A::quarter),f(A::three_quarters),f(A::fifteenth),f(A::sixteenth),f(A::angle),f(A::root3)};
}
// The recompiled fctiwz: NaN -> 0x80000000, >= INT_MAX -> INT_MAX, else
// cvttsd2si (0x80000000 when the truncation does not fit).
inline int32_t NativeGuestFctiwz(double value) {
  if(std::isnan(value)) return INT32_MIN;
  if(value>=double(INT32_MAX)) return INT32_MAX;
  if(!(value>-2147483649.0)) return INT32_MIN;
  return int32_t(value);
}
// divw behind 82172698's traps: twllei on a zero divisor, twlgei on
// INT_MIN / -1 (andc of the divisor with rotlwi(dividend,1)-1 equal to -1).
inline int32_t NativeGrassMapDivw(int32_t dividend,int32_t divisor) {
  if(divisor==0) throw std::runtime_error("native grass map: zero sub-cell count (twllei)");
  if(dividend==INT32_MIN && divisor==-1) throw std::runtime_error("native grass map: divw overflow (twlgei)");
  return dividend/divisor;
}
// 82172698's cells after the camera's own, as its two inner loops visit them
// (32-bit wrapping registers as the guest keeps them): for ring = 1..rings
// (r22; r30 = 1 - ring, r25 = cx + ring) the pairs (x, cz - ring), (x, cz +
// ring) for x = cx - ring .. cx + ring, then (cx - ring, z), (cx + ring, z)
// for z = cz - ring + 1 .. cz + ring - 1. cell(x, z) as 82171F58's (r4, r5).
template<class Cell>
void WalkNativeGrassMapRings(int32_t cx,int32_t cz,int32_t rings,Cell&& cell) {
  cell(cx,cz);                                                   // bl 82171F58 (r4 r29, r5 r28)
  uint32_t r22=1;                                                // li r22,1
  if(rings<1) return;                                            // cmpwi r20,1; blt
  const uint32_t x=uint32_t(cx),z=uint32_t(cz);
  uint32_t r30=0,r25=x+1;                                        // li r30,0 (at entry); addi r25,r29,1
  const uint32_t r24=z-1,r23=x-1,r21=z-x,r18=uint32_t(-1)-x;     // addi r24; addi r23; subf r21; subfic r18
  do {
    if(!(int32_t(r30-1)>int32_t(r22))) {                         // addi r11,r30,-1; cmpw r11,r22; bgt
      uint32_t r27=r24+r30,r29=r23+r30,r28=(r22-r30)+2;
      const uint32_t r26=r21+r25;
      do {
        cell(int32_t(r29),int32_t(r27));
        cell(int32_t(r29),int32_t(r26));
        --r28; ++r29;
      } while(r28!=0);                                           // cmplwi r28,0; bne
    }
    const uint32_t r10=r18+r25;                                  // add r10,r18,r25
    if(!(int32_t(r30)>int32_t(r10))) {                           // cmpw r30,r10; bgt
      const uint32_t r27=r23+r30;
      uint32_t r29=(r24+r30)+1,r28=(r10-r30)+1;
      do {
        cell(int32_t(r27),int32_t(r29));
        cell(int32_t(r25),int32_t(r29));
        --r28; ++r29;
      } while(r28!=0);
    }
    ++r22; --r30; ++r25;
  } while(!(int32_t(r22)>rings));                                // cmpw r22,r20; ble
}
struct NativeGrassMapStats {
  uint32_t objects=0,skipped=0,cells=0,outside=0,empty=0,culled=0,blades=0,blade_culled=0,faded=0,drawn=0,draws=0;
};
// One list's draw: 8218D398's blend and depth write, then the Utility_3DTexA
// activation with the list's texture, quads of 36-byte vertices.
inline NativeEffectDraw MakeNativeGrassMapDraw(uint32_t utility,std::vector<NativeRibbonVertex> vertices,uint32_t texture) {
  NativeEffectDraw draw;
  draw.kind=NativeEffectDraw::Kind::RibbonQuads; draw.technique=NativeEffectTechnique::Utility3DTexA;
  draw.effect=utility; draw.texture=texture; draw.blend=NativeGrassMap::blend;
  draw.sets_depth_write=true; draw.depth_write=NativeGrassMap::depth_flag==1;
  draw.ribbon_vertices=std::move(vertices);
  return draw;
}
// Every draw 82172698 issues for `grass` with the guest frame context
// `context` (+8 depth scale, +16 the view's scene), in the guest's order:
// cells in ring order, and in each cell the lists 0..14 that got blades.
// `cells` (optional) receives every 82171F58 call's (x, z). Throws where the
// guest traps and on the native refusals.
template<class Reader>
std::vector<NativeEffectDraw> BuildNativeGrassMapDraws(const Reader& r,uint32_t grass,uint32_t context,
    NativeGrassMapStats* stats=nullptr,std::vector<std::array<int32_t,2>>* cells=nullptr) {
  using G=NativeGrassMap;
  NativeGrassMapStats local;
  auto& count=stats?*stats:local;
  ++count.objects;
  const auto word=[&](uint32_t offset) { return r.Word(r.Add(grass,offset)); };
  const auto single=[&](uint32_t offset) { return ReadNativeFxFloat(r,r.Add(grass,offset)); };
  // lbz 1164; stw 0 to +1168; beq / lbz 1248; beq.
  if(!r.Bytes(r.Add(grass,G::loaded),1)[0] || !r.Bytes(r.Add(grass,G::enabled),1)[0]) { ++count.skipped; return {}; }
  const auto k=ReadNativeGrassMapConstants(r);
  const auto view=ReadNativeSceneVisibilityView(r,context);     // 82171BD8 through Word(+1228) = context
  // +1232/+1236/+1240, from the rendered copy (scene+272, not +464): the
  // camera the view and frustum above were derived from.
  const auto camera=ReadNativeFxVec3(r,r.Add(r.Word(r.Add(context,16)),G::camera_translation));
  const float sub_x=single(G::sub_x),sub_z=single(G::sub_z),reach=single(G::reach);
  const float origin_x=single(G::origin_x),origin_z=single(G::origin_z);
  const float inv_z=NativeFxDiv(k.one,sub_z);                   // fdivs f12,f0,f13
  const float inv_x=NativeFxDiv(k.one,sub_x);                   // fdivs f0,f0,f11
  const int32_t along_z=NativeGuestFctiwz(NativeFxMul(reach,inv_z));   // fmuls f11,f13,f12 -> r1+84
  const int32_t along_x=NativeGuestFctiwz(NativeFxMul(inv_x,reach));   // fmuls f13,f0,f13 -> r1+80
  const int32_t n=int32_t(word(G::subcells));
  const uint32_t extent=uint32_t(n)+uint32_t(along_x<along_z?along_z:along_x);  // cmpw r10,r11; bge; add r10,r11,r10
  const int32_t px=NativeGuestFctiwz(NativeFxMul(NativeFxSub(camera[0],origin_x),inv_x));  // fsubs; fmuls f0,f13,f0
  const int32_t pz=NativeGuestFctiwz(NativeFxMul(NativeFxSub(camera[2],origin_z),inv_z));  // fsubs; fmuls f13,f11,f12
  const int32_t rings=NativeGrassMapDivw(int32_t(extent-1),n);     // divw r20
  const int32_t cx=NativeGrassMapDivw(px,n),cz=NativeGrassMapDivw(pz,n);  // divw r29, r28
  if(rings>G::max_rings) throw std::runtime_error("native grass map: ring count out of range");
  if(n>G::max_subcells) throw std::runtime_error("native grass map: sub-cell count out of range");
  const float cell_x=single(G::cell_x),cell_z=single(G::cell_z),half_x=single(G::half_x),half_z=single(G::half_z);
  const uint32_t width=word(G::width),depth=word(G::depth),slots=word(G::slots),heights=word(G::heights),blades=word(G::blades);
  const uint32_t utility=r.Add(grass,G::utility);
  // Per list i: the vector's begin (the cursor every cell restarts from), its
  // texture; per type t = 1..15: its colour words and (base, top) sizes.
  std::array<uint32_t,G::types> list_begin{},textures{};
  std::array<std::array<uint32_t,4>,G::types+1> colours{};
  std::array<std::array<float,2>,G::types+1> sizes{};
  for(uint32_t i=0;i<G::types;++i) {
    list_begin[i]=word(G::lists+i*G::list_stride);
    textures[i]=word(G::textures+i*G::texture_stride);
    const uint32_t type=i+1;
    for(uint32_t c=0;c<4;++c) colours[type][c]=word(G::colours+type*G::colour_stride+c*4);
    sizes[type]={ReadNativeFxFloat(r,G::blade_table+type*8),ReadNativeFxFloat(r,G::blade_table+type*8+4)};
  }
  // 8218D3F0: vertices = count * [82008888 + 13*8] + [+4] (mullw, add).
  const uint32_t per_primitive=r.Word(G::primitive_table+G::quad_primitive*8),plus=r.Word(G::primitive_table+G::quad_primitive*8+4);
  bool checked_declaration=false;
  // 82171BD8(limit) on the bound (x, y, z, 1.0) with `radius`.
  const auto visible=[&](float x,float y,float z,float radius,float limit) {
    const auto c=NativeVisibilityTransform({x,y,z,k.one},view.matrix);   // 821B0198 into context+32
    const float depth_z=-NativeFxMul(view.depth_scale,c[2]);            // lfs +8(ctx), lfs +40(ctx); fmuls; fneg
    if(depth_z>limit) return false;                                     // fcmpu; ble
    return NativeWireSphereVisible(view.frustum,{c[0],c[1],c[2]},radius);  // 821C2FD0(scene+288, ctx+32, +352)
  };
  std::vector<NativeEffectDraw> draws;
  const auto cell=[&](int32_t x,int32_t z) {
    ++count.cells;
    if(cells) cells->push_back({x,z});
    // cmpwi r4,0 blt; cmpw r4,+1208 bge; cmpwi r29,0 blt; cmpw r29,+1212 bge.
    if(x<0 || x>=int32_t(width) || z<0 || z>=int32_t(depth)) { ++count.outside; return; }
    const int32_t slot=int32_t(r.Word(r.Add(slots,(width*uint32_t(z)+uint32_t(x))<<2)));  // mullw; add; rlwinm; lwzx
    if(slot<0) { ++count.empty; return; }
    const uint32_t pair=r.Add(heights,uint32_t(slot)<<3);                // +1216 + slot*8
    const float lo=ReadNativeFxFloat(r,pair),hi=ReadNativeFxFloat(r,r.Add(pair,4));
    const float fz=float(double(z)),fx=float(double(x));                  // extsw; std; lfd; fcfid; frsp (f8, f16)
    const float bz=NativeFxAdd(NativeFxMadd(cell_z,fz,half_z),origin_z);  // fmadds f0,f0,f8,f12; fadds f12,f0,f9 -> +296
    const float bx=NativeFxAdd(NativeFxMadd(cell_x,fx,half_x),origin_x);  // fmadds f11,f11,f16,f7; fadds f12,f11,f5 -> +288
    const float half_y=NativeFxMul(NativeFxSub(hi,lo),k.half);           // fsubs f0,f10,f13; fmuls f0,f0,f26 -> +324
    const float by=NativeFxAdd(lo,half_y);                                // fadds f13,f13,f0 -> +292
    float sum=NativeFxMul(half_x,half_x);                                 // fmuls f10,f12,f12 (+304 = +1148, +344 = +1152)
    sum=NativeFxMadd(half_z,half_z,sum);
    sum=NativeFxMadd(half_y,half_y,sum);
    const float radius=NativeFxSqrt(sum);                                 // fsqrts -> +352
    if(!visible(bx,by,bz,radius,NativeFxAdd(reach,radius))) { ++count.culled; return; }  // fadds f1,f11,f0
    // The stack's 15 quad counts (r1+180..) and cursors (r1+244.., from each
    // vector's begin): per cell, so every list restarts at its first vertex.
    std::array<uint32_t,G::types> quads{};
    std::array<std::vector<NativeRibbonVertex>,G::types> lists;
    const uint32_t base=r.Add(blades,(uint32_t(n)*uint32_t(n)*uint32_t(slot))<<2);  // mullw r4; mullw r9; rlwinm; add
    float row_z=NativeFxMadd(float(double(int32_t(uint32_t(n)*uint32_t(z)))),sub_z,origin_z);  // f19
    if(n>0) {
      const auto* data=r.Bytes(base,size_t(n)*size_t(n)*4);
      for(int32_t row=0;row<n;++row) {
        float col_x=NativeFxMadd(NativeFxMul(float(double(n)),sub_x),fx,origin_x);  // fmuls f13,f13,f12; fmadds f23,f13,f16,f0
        for(int32_t col=0;col<n;++col,col_x=NativeFxAdd(sub_x,col_x)) {             // fadds f23,f12,f23
          const auto* at=data+(size_t(row)*size_t(n)+size_t(col))*4;                 // r29 += 4 per sub-cell
          const uint32_t hw=uint32_t(at[0])<<8|at[1];                                // lhz r10,0(r29)
          const uint32_t type=hw>>12;                                                // rlwinm r30,r10,20,12,31
          if(!type) continue;
          ++count.blades;
          const float a=float(double((hw>>8)&15));                                   // rlwinm r9,r10,24,28,31; fcfid f13
          const float height=float(double(int16_t(uint16_t(uint32_t(at[2])<<8|at[3]))));  // lhz 2; extsh; fcfid f9
          const int32_t jx=int32_t(hw<<24)>>28,jz=int32_t(hw<<28)>>28;               // rlwinm 24,0,7 / 28,0,3; srawi 28
          const float base_size=sizes[type][0],top_size=sizes[type][1];              // lfsx f0 / f10 at 82554480 + type*8
          const float spread=NativeFxMul(NativeFxSub(top_size,base_size),a);         // fsubs f10,f10,f0; fmuls f10,f10,f13
          const float angle=NativeFxMul(a,k.angle);                                  // fmuls f28,f13,f22
          const float ground=NativeFxMul(height,k.sixteenth);                        // fmuls f31,f9,f24
          const float size=NativeFxMadd(spread,k.fifteenth,base_size);               // fmadds f27,f10,f21,f0
          const float dz=NativeFxMul(float(double(jz)),sub_z);                       // fmuls f13,f8,f11
          const float dx=NativeFxMul(float(double(jx)),sub_x);                       // fmuls f0,f9,f12
          const float z_at=NativeFxMadd(dz,k.sixteenth,row_z);                       // fmadds f29,f13,f24,f19 -> +296
          const float x_at=NativeFxMadd(dx,k.sixteenth,col_x);                       // fmadds f30,f0,f24,f23 -> +288
          const float half=NativeFxMul(size,k.half);                                 // fmuls f0,f27,f26 -> +304/+324/+344
          const float y_at=NativeFxAdd(half,ground);                                 // fadds f13,f0,f31 -> +292
          const float blade_radius=NativeFxMul(half,k.root3);                        // fmuls f13,f0,f20 -> +352
          if(!visible(x_at,y_at,z_at,blade_radius,reach)) { ++count.blade_culled; continue; }  // f1 = +1156
          // 821B03C8(r1+128): (f30 - +1232, f31 - +1236, f29 - +1240, 1.0).
          const float vx=NativeFxSub(x_at,camera[0]),vy=NativeFxSub(ground,camera[1]),vz=NativeFxSub(z_at,camera[2]);
          float length=NativeFxMul(vy,vy);
          length=NativeFxMadd(vx,vx,length);
          length=NativeFxMadd(vz,vz,length);
          const float distance=length==k.zero?k.zero:NativeFxSqrt(length);
          const float quarter=NativeFxMul(reach,k.quarter);                          // fmuls f13,f0,f17
          float fade=NativeFxDiv(-NativeFxMsub(reach,k.three_quarters,distance),quarter);  // fnmsubs f0,f0,f18,f1; fdivs
          if(fade>k.one) { ++count.faded; continue; }                                // fcmpu f0,f25; bgt
          fade=NativeFxSub(k.one,fade);                                              // fsubs f0,f25,f0
          if(fade>k.one) fade=k.one;                                                 // ble; fmr f0,f25
          const auto& colour=colours[type];                                          // +396 + type*16 -> r1+96
          const float alpha=NativeFxMul(std::bit_cast<float>(colour[3]),fade);       // lfs f13,108(r1); fmuls f0,f13,f0
          const NativeFxVec4 rgba{std::bit_cast<float>(colour[0]),std::bit_cast<float>(colour[1]),std::bit_cast<float>(colour[2]),alpha};
          const float s=NativeFxMul(NativeFxMul(float(NativeGuestSin(double(angle))),size),k.half);  // 821E9558; frsp; fmuls f0,f0,f27; fmuls f28,f0,f26
          const float c=NativeFxMul(NativeFxMul(float(NativeGuestCos(double(angle))),size),k.half);  // 821E9630; frsp f13,f1; fmuls f13,f13,f27; f26
          const float top=NativeFxAdd(size,ground);                                  // fadds f0,f27,f31
          const float x0=NativeFxSub(x_at,s),x1=NativeFxAdd(s,x_at);                 // fsubs f12,f30,f28; fadds f11,f28,f30
          const float z0=NativeFxSub(z_at,c),z1=NativeFxAdd(c,z_at);                 // fsubs f10,f29,f13; fadds f9,f13,f29
          const float z2=NativeFxSub(z_at,s),x2=NativeFxSub(x_at,c);                 // fsubs f10,f29,f28; fsubs f12,f30,f13
          const float z3=NativeFxAdd(s,z_at),x3=NativeFxAdd(c,x_at);                 // fadds f11,f28,f29; fadds f13,f13,f30
          auto& list=lists[type-1];                                                  // cursor r1+240 + type*4
          // Eight 36-byte vertices from the cursor, the UV words untouched
          // (filled from the vector below), the colour copy on every one.
          for(const auto& p:{NativeFxVec3{x0,top,z0},NativeFxVec3{x1,top,z1},NativeFxVec3{x1,ground,z1},NativeFxVec3{x0,ground,z0},
                             NativeFxVec3{x2,top,z3},NativeFxVec3{x3,top,z2},NativeFxVec3{x3,ground,z2},NativeFxVec3{x2,ground,z3}})
            list.push_back({p,{},rgba});
          quads[type-1]+=2;                                                          // count r1+176 + type*4, addi 2
          ++count.drawn;                                                             // +1168 += 1
        }
        row_z=NativeFxAdd(sub_z,row_z);                                              // fadds f19,f0,f19
      }
    }
    // For each list with a count (r25 from r1+180, r29 from +668, r28 from +920).
    for(uint32_t i=0;i<G::types;++i) {
      if(!quads[i]) continue;
      auto& vertices=lists[i];
      if(uint32_t(int32_t(per_primitive)*int32_t(quads[i]))+plus!=vertices.size())
        throw std::runtime_error("native grass map: quad list vertex count differs from 8218D3F0's");
      if(!checked_declaration) {                                                     // 8218D440's twi 31 traps
        const auto list=r.Word(r.Add(utility,G::declarations));
        if(!list || r.Word(r.Add(utility,G::declaration))==r.Word(r.Add(list,4)))
          throw std::runtime_error("native grass map: Utility declaration missing (8218D440 traps)");
        checked_declaration=true;
      }
      const auto* bytes=r.Bytes(list_begin[i],vertices.size()*G::vertex_bytes);
      const auto uv=[&](size_t at) {
        return std::bit_cast<float>(uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3]);
      };
      for(size_t v=0;v<vertices.size();++v)
        vertices[v].uv={uv(v*G::vertex_bytes+G::uv),uv(v*G::vertex_bytes+G::uv+4)};
      draws.push_back(MakeNativeGrassMapDraw(utility,std::move(vertices),textures[i]));
      ++count.draws;
    }
  };
  WalkNativeGrassMapRings(cx,cz,rings,cell);
  return draws;
}

// ---- The map-effect walk as the sky pass records it ----
// 820B35A0's objects in list order, each routed as 821C0C00 routes it: the
// sky (its own pass, RecordNativeSky) where the list holds the current one,
// mode-0 slot 4s drawn inside the walk (clElectricWire's strips, a mode-0
// clGrassMap's quads), and mode-1/2 objects filed into the buckets for the
// drain (clGrassMap: mode 2, key 65535). Hidden objects (u16 +64) do nothing;
// anything else is unsupported (listed, not drawn).
struct NativeMapEffectSegment {
  bool sky=false;                          // draw the sky here, else `draws` in order
  std::vector<NativeEffectDraw> draws;
};
struct NativeMapEffectPlan {
  std::vector<NativeMapEffectSegment> segments;   // walk order
  std::vector<NativeEffectItem> filed;            // drawn keys; `order` counts every filing of this walk from 0
  uint32_t filings=0,undrawn_keys=0;
  std::vector<NativeMapEffectMember> unsupported;
  NativeElectricWireStats wires;
  NativeGrassMapStats grass;
};
// `fail(what)` receives each object's build error; that object draws nothing.
template<class Reader,class Fail>
NativeMapEffectPlan PlanNativeMapEffects(const Reader& r,const std::vector<NativeMapEffectMember>& members,uint32_t sky,
    uint32_t scene,uint32_t context,const std::array<uint32_t,16>& eye_view,Fail&& fail) {
  NativeMapEffectPlan plan;
  std::vector<NativeEffectDraw> pending;
  std::optional<NativeEffectInputs> inputs;
  const auto flush=[&] {
    if(pending.empty()) return;
    plan.segments.push_back({false,std::move(pending)});
    pending.clear();
  };
  for(const auto& member:members) {
    if(member.kind==NativeMapEffectKind::Sky) {
      // The sky pass reads its own hidden word (RecordNativeSky).
      if(member.object!=sky) continue;
      flush();
      plan.segments.push_back({true,{}});
      continue;
    }
    if(member.hidden) continue;  // 821C0C00: lhz 64 nonzero returns.
    try {
      if(member.kind==NativeMapEffectKind::ElectricWire && member.mode==0) {
        if(!inputs) inputs=ReadNativeEffectInputs(r,eye_view);
        for(auto& draw:BuildNativeElectricWireDraws(r,member.object,scene,*inputs,&plan.wires)) pending.push_back(std::move(draw));
        continue;
      }
      // Mode 1's key would read context+40, which this walk never sets for
      // the object (no transform runs before 821C0C00 here): unsupported.
      if(member.kind==NativeMapEffectKind::GrassMap && (member.mode==0 || member.mode==2)) {
        if(!context) throw std::runtime_error("native grass map needs the guest frame context");
        if(member.mode==0) {
          for(auto& draw:BuildNativeGrassMapDraws(r,member.object,context,&plan.grass)) pending.push_back(std::move(draw));
          continue;
        }
        NativeEffectItem item;
        item.key=ComputeNativeBucketKey(r,context,member.object).key;   // mode 2: +56 * 65536, clamped
        item.order=plan.filings++;
        item.object=member.object; item.slot4=member.render; item.type=NativeEffectClass::GrassMap;
        // 821A3BA0 never draws bucket 0: slot 4 is not called for a key < 256.
        if(!NativeTransparentKeyDrawn(item.key)) { ++plan.undrawn_keys; continue; }
        item.draws=BuildNativeGrassMapDraws(r,member.object,context,&plan.grass);
        if(!item.draws.empty()) plan.filed.push_back(std::move(item));
        continue;
      }
    } catch(const std::exception& error) { fail(error.what()); continue; }
    plan.unsupported.push_back(member);
  }
  flush();
  return plan;
}
// Where the map effects' filings fall in the pass's one filing sequence
// (NativeTransparentItem::order). The helper's world list (owner+44) runs
// each manager's slot 2 in list order, so clMapEffectManager's filings come
// before clEffectObjectManager's exactly when it is earlier on that list (or
// the effects manager is absent). Models are taken as filed before both
// (their registry is unordered; see NativeFullFrameModelsShared).
template<class Reader>
int32_t NativeWorldListIndex(const Reader& r,uint32_t owner,uint32_t vtable) {
  const auto end=r.Word(r.Add(owner,NativeWorldList::end));
  int32_t index=0;
  for(auto node=r.Word(r.Add(owner,NativeWorldList::head));node!=end;node=r.Word(r.Add(node,NativeWorldList::next)),++index) {
    if(uint32_t(index)>=NativeWorldList::limit) throw std::runtime_error("native world list does not terminate");
    const auto object=r.Word(r.Add(node,NativeWorldList::object));
    if(object && r.Word(object)==vtable) return index;
  }
  return -1;
}
template<class Reader>
bool NativeMapEffectsFiledBeforeEffects(const Reader& r,uint32_t owner) {
  const auto map=NativeWorldListIndex(r,owner,kNativeMapEffectManagerVtable);
  const auto effects=NativeWorldListIndex(r,owner,NativeEffectList::manager_vtable);  // clEffectObjectManager 820072D4
  return effects<0 || (map>=0 && map<effects);
}
// The first filing order of each producer: models from 0, then the map
// effects and the effects in world-list order.
struct NativeFilingBases { uint32_t map_effects=0,effects=0; };
inline NativeFilingBases NativeMapEffectFilingBases(bool map_first,uint32_t models,uint32_t map_filings,uint32_t effect_filings) {
  return map_first?NativeFilingBases{models,models+map_filings}:NativeFilingBases{models+effect_filings,models};
}
}
