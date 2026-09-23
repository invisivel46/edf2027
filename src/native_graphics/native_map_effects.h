#pragma once
#include "native_graphics/native_full_frame_effects.h"
#include "native_graphics/native_render_instances.h"
#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
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
// clGrassMap. The native walk only reads: no guest callback runs, so no link
// can change during it, and its order is the list's.
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
// The ten points of one record as 820B8D28's inner loop leaves them at r1+144.
inline std::array<NativeFxVec3,NativeElectricWire::points> NativeElectricWirePoints(const NativeFxVec3& a,const NativeFxVec3& b,
    float phase,float offset,const NativeEffectConstants& k,const NativeElectricWireConstants& w) {
  const float sx=NativeFxMul(NativeFxSub(b[0],a[0]),w.ninth);   // f26
  const float sy=NativeFxMul(NativeFxSub(b[1],a[1]),w.ninth);   // f25
  const float sz=NativeFxMul(NativeFxSub(b[2],a[2]),w.ninth);   // f24
  float px=a[0],py=a[1],pz=a[2],angle=k.zero;                  // f29 f28 f27 f31
  std::array<NativeFxVec3,NativeElectricWire::points> out{};
  for(auto& point:out) {
    point={px,py,pz};                                           // the 16-byte copy of r1+112
    const float s=float(NativeGuestSin(double(angle)));         // frsp f30
    float t=float(NativeGuestSin(double(NativeFxAdd(phase,offset))));
    px=NativeFxAdd(sx,px);
    point[1]=NativeFxSub(point[1],s);
    py=NativeFxAdd(py,sy); pz=NativeFxAdd(pz,sz);
    angle=NativeFxAdd(angle,w.angle_step);
    t=NativeFxMul(t,k.half); t=NativeFxMul(t,s);
    point[0]=NativeFxAdd(point[0],t);
    point[2]=NativeFxAdd(t,point[2]);
  }
  return out;
}
struct NativeElectricWireStats { uint32_t records=0,disabled=0,distant=0,culled=0,drawn=0; };
// Every draw 820B8D28 issues for `wire` in the view whose scene is `scene`
// (Word(context+16)), in record order. Throws on an implausible vector.
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
  std::vector<NativeEffectDraw> draws;
  for(uint32_t record=begin;record!=end;record+=W::stride) {
    ++count.records;
    if(!r.Bytes(r.Add(record,W::enabled),1)[0]) { ++count.disabled; continue; }
    const auto a=ReadNativeFxVec3(r,r.Add(record,W::a)),b=ReadNativeFxVec3(r,r.Add(record,W::b));
    const auto va=NativeWireTransform(a,matrix);
    float sum=NativeFxMul(va[1],va[1]);
    sum=NativeFxMadd(va[2],va[2],sum); sum=NativeFxMadd(va[0],va[0],sum);
    if(sum>w.distance) {
      const auto vb=NativeWireTransform(b,matrix);
      sum=NativeFxMul(vb[1],vb[1]);
      sum=NativeFxMadd(vb[0],vb[0],sum); sum=NativeFxMadd(vb[2],vb[2],sum);
      if(sum>w.distance) { ++count.distant; continue; }
    }
    const auto centre=NativeWireTransform(ReadNativeFxVec3(r,r.Add(record,W::centre)),matrix);
    if(!NativeWireSphereVisible(frustum,centre,ReadNativeFxFloat(r,r.Add(record,W::radius)))) { ++count.culled; continue; }
    const auto points=NativeElectricWirePoints(a,b,phase,ReadNativeFxFloat(r,r.Add(record,W::offset)),in.k,w);
    auto vertices=BuildNativeColourStrip(points,W::colour,w.width,in.eye,in.k);
    if(vertices.empty()) continue;
    draws.push_back(MakeNativeColourStripDraw(in.effect,std::move(vertices),W::blend,W::depth_flag));
    ++count.drawn;
  }
  return draws;
}

// clGrassMap (vtable 820124DC): NOT PORTED. Mission 1 has none. This is the
// specification a port follows; until then the full frame declares it
// unsupported (logged once per object class, nothing drawn). Its mode (+52)
// is 2, so 821C0C00 files it with key bias*65536 and the bucket drain
// 821A3BA0 reaches its slot 4 in key order (a transparent item).
//  slot 4 sub_82172698(this, context): +1168 = 0; returns unless bytes +1164
//   and +1248 are set. Copies the camera world Word(context+16)+416 (64 bytes)
//   to the stack, stores context at +1228 and the camera translation row at
//   +1232..+1244. reach = max(fctiwz(+1156 * (1/+1112)), fctiwz((1/+1108) *
//   +1156)) + +1200 (sub-cells per cell). Camera cell: cx = fctiwz((+1232 -
//   +1124) * (1/+1108)) / +1200, cy = fctiwz((+1240 - +1128) * (1/+1112)) /
//   +1200 (divw); rings = (reach-1) / +1200. 8218D380 (82017728 state set 3
//   on [8257BFB4]) and 8218D398(this+1256, 0, 0) (depth write off), then
//   82171F58(cx, cy), then for ring = 1..rings the cells of the ring's four
//   sides in the order of the two inner loops, 8218D430 (82017728 restore),
//   and +1172 = max(+1172, +1168) (blades drawn; peak).
//  cell sub_82171F58(this, x, y): nothing unless 0 <= x < +1208, 0 <= y <
//   +1212 and slot = Word(+1176)[y*+1208 + x] >= 0. The cell's bound from its
//   height pair +1216+slot*8 (lo, hi) and the grid origin (+1124, +1128) +
//   (x, y) * (+1140, +1144) + (+1148, +1152): centre +288/+292/+296, +300 =
//   1.0, half axes +304/+324/+344, radius +352 (sqrt of the summed squares);
//   82171BD8 culls it (radius + +1156). Per-type vertex cursors from the 15
//   12-byte lists at +920 and counts (zeroed) on the stack. Then +1200 x +1200
//   sub-cells, the u16 pairs at Word(+1188) + slot*+1200^2*4: the first
//   halfword's top 12 bits pick the blade (0: none), its type the byte table
//   at 82554480 (8 bytes per entry: height, width), the low nibbles a signed
//   jitter; the blade's centre +288.. is rebuilt (0.0625 [82009654] per
//   jitter step, 0.0666667 [82004FF8], 1.73205 [820124D0] for the radius) and
//   82171BD8 tests it; 821B03C8 gives the camera distance, faded as
//   (d - 0.75*+1156) / (0.25*+1156) [820024A0/820021E4] (> 1.0 skips); the
//   type's colour +396+type*16 (RGBA) with alpha scaled by 1 - fade; the angle
//   (type * 0.418879 [820124CC]) through sin/cos (821E9558/821E9630) spans
//   four 36-byte VS_3DTex vertices (position, UV, colour) appended to the
//   type's list; its count += 2 primitives; +1168 += 1. Last, for each of the
//   15 types with a count: 8218D440(this+1256, Word(this+668+type*16)) (the
//   texture into the sampler list +1256+172/+200 by 821BC4C8, 821B94E8 of
//   [+1256+188], declaration Word(+1256+64)+28) and 8218D3F0(this+1256, 13,
//   list, count): 821FD8F8 with stride 36 (DrawPrimitiveUP, quads).
// A port needs GrassMap.bin's grid as loaded (+1176/+1188/+1216), a bucket
// item per object and the VS_3DTex quad path the ribbons already use.
enum class NativeGrassMapSupport : uint8_t { Unsupported };
inline constexpr NativeGrassMapSupport kNativeGrassMapSupport=NativeGrassMapSupport::Unsupported;
}
