#pragma once
#include "native_graphics/native_bucket_dispatch.h"
#include "native_graphics/native_scene_visibility.h"
#include "native_graphics/native_transparent_items.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace edf::native {
// Native generation of the effect draws the guest issues from each effect's
// slot 4 when the bucket drain sub_821A3BA0 reaches it. The full-frame renderer
// skips the guest render helper, so nothing else produces them.
//
// Every operation is single precision in the guest's order, and written the way
// the recompiled C++ evaluates it: float(double op double), fmadds/fmsubs as
// float(std::fma(...)) of doubles. Constants are read from the guest image.

// Image constants (all lfs, single). Read once per walk.
struct NativeEffectConstantAddress {
  static constexpr uint32_t zero=0x820009A4,one=0x820008CC,half=0x820008D4,quarter=0x820021E4,
    three_quarters=0x820024A0,fifth=0x820028E4,twelfth=0x8200751C,five=0x82002738,web_scale=0x82007638,
    half_pi=0x820045EC,pi=0x82017668,three_half_pi=0x8201771C;
};
struct NativeEffectConstants {
  float zero=0,one=1,half=.5f,quarter=.25f,three_quarters=.75f,fifth=.2f,twelfth=1.f/12,five=5,web_scale=2.6f;
  float half_pi=0,pi=0,three_half_pi=0;
};
template<class Reader>
NativeEffectConstants ReadNativeEffectConstants(const Reader& r) {
  using A=NativeEffectConstantAddress;
  const auto f=[&](uint32_t at) { return std::bit_cast<float>(r.Word(at)); };
  return {f(A::zero),f(A::one),f(A::half),f(A::quarter),f(A::three_quarters),f(A::fifth),f(A::twelfth),
          f(A::five),f(A::web_scale),f(A::half_pi),f(A::pi),f(A::three_half_pi)};
}
// Globals. [0x8257C034] is the effect-shader object every slot 4 passes as r3
// to 821A7640/821A8628/821A8090; [0x8257C02C]+192 is the eye the ribbons face.
//
// The eye's one writer is sub_821A19F0(pool [8257C02C], view), whose one
// caller is the scene begin 821BE8D0 (r4 = scene+96: the view the pass camera
// holds). The full-frame renderer never runs 821BE8D0 (the render helper
// 821A5080 is skipped); its BeginView writes the pool natively instead
// (WriteNativeViewGlobals, native_view_globals.h, whose eye is this function).
// The native builders derive the eye from the pass camera's view
// (NativeEffectEyeFromView) and read +192 only for diagnostics
// (ReadNativeEffectGuestEye). The guest readers of +192 are
// 821A7E08 (the electric wire strip), 821A8090, 821A8360, 821A8628 and
// 821A88E8; 821A8360 is also called by the guest view listener 820D3FD0.
inline constexpr uint32_t kNativeEffectShaderGlobal=0x8257C034;
inline constexpr uint32_t kNativeEffectCameraGlobal=0x8257C02C;
inline constexpr uint32_t kNativeEffectEyeOffset=192;
// 821A19F0's lfs f0,5084(lis -32256): the factor the rotated translation is
// scaled by (-1.0 in the image).
inline constexpr uint32_t kNativeEffectEyeScale=0x820013DC;
// clEffectEtc02's slot 4 picks its blend from this byte: nonzero -> 1 (ONE/ONE).
inline constexpr uint32_t kNativeEffectEtc02BlendGlobal=0x82554BF0;
// clEffectEtc02's slot 4 decrements this word on every call (8217C4B0..C0) and
// its slot 3 (8217C3A8) kills the object once it is <= 0: a lifetime counted
// in draws. The native pass keeps the count, so this is its one guest write.
inline constexpr uint32_t kNativeEffectEtc02Lifetime=612;

inline float NativeFxAdd(float a,float b) { return float(double(a)+double(b)); }
inline float NativeFxSub(float a,float b) { return float(double(a)-double(b)); }
inline float NativeFxMul(float a,float b) { return float(double(a)*double(b)); }
inline float NativeFxDiv(float a,float b) { return float(double(a)/double(b)); }
inline float NativeFxSqrt(float a) { return float(std::sqrt(double(a))); }
// fmadds a*c+b and fmsubs a*c-b.
inline float NativeFxMadd(float a,float c,float b) { return float(std::fma(double(a),double(c),double(b))); }
inline float NativeFxMsub(float a,float c,float b) { return float(std::fma(double(a),double(c),-double(b))); }
using NativeFxVec3=std::array<float,3>;
using NativeFxVec4=std::array<float,4>;
// sub_821B0320: v scaled to `length`; a zero squared length stores the zero
// constant (a NaN one does not compare equal and takes the division).
inline NativeFxVec3 NativeFxSetLength(const NativeFxVec3& v,float length,const NativeEffectConstants& k) {
  float sum=NativeFxMul(v[1],v[1]);
  sum=NativeFxMadd(v[0],v[0],sum);
  sum=NativeFxMadd(v[2],v[2],sum);
  if(sum==k.zero) return {k.zero,k.zero,k.zero};
  const float scale=NativeFxDiv(length,NativeFxSqrt(sum));
  return {NativeFxMul(v[0],scale),NativeFxMul(v[1],scale),NativeFxMul(v[2],scale)};
}
// sub_821A19F0's eye, [pool+192..+204], from the row-vector view it is given
// (the 16 words at scene+96, as NativeScenePassCamera::view holds them):
//  - the 64 bytes are copied to the stack and 821C8750 transposes their upper
//    3x3 in place (1<->4, 2<->8, 6<->9);
//  - 821B0130(out, in = row 3 at +48..+56, untouched by the transpose, M') is
//    the row vector times the transposed 3x3, in its instruction order;
//  - x, y, z are each fmuls'd by [820013DC]; w is the view's +60, copied with
//    them by the 16-byte ld/std pair.
// Bit-identical to the guest's store for the same view words.
inline NativeFxVec4 NativeEffectEyeFromView(const std::array<uint32_t,16>& view,float scale) {
  const auto m=[&](size_t i) { return std::bit_cast<float>(view[i]); };
  // M' after 821C8750: M'[0]=M[0], M'[1]=M[4], M'[2]=M[8], M'[4]=M[1],
  // M'[5]=M[5], M'[6]=M[9], M'[8]=M[2], M'[9]=M[6], M'[10]=M[10].
  const float in0=m(12),in1=m(13),in2=m(14);
  float f8=NativeFxMul(m(6),in2);              // fmuls  f8,[M'+36],in2
  float f7=NativeFxMul(m(10),in2);             // fmuls  f7,[M'+40],in2
  const float f9=NativeFxMul(m(0),in0);        // fmuls  f9,[M'+0],in0
  f8=NativeFxMadd(m(5),in1,f8);                // fmadds f8,[M'+20],in1,f8
  f7=NativeFxMadd(m(9),in1,f7);                // fmadds f7,[M'+24],in1,f7
  const float y=NativeFxMadd(m(4),in0,f8);     // fmadds f11,[M'+4],in0,f8
  const float f13=NativeFxMadd(m(1),in1,f9);   // fmadds f13,[M'+16],in1,f9
  const float z=NativeFxMadd(m(8),in0,f7);     // fmadds f12,[M'+8],in0,f7
  const float x=NativeFxMadd(in2,m(2),f13);    // fmadds f0,in2,[M'+32],f13
  return {NativeFxMul(x,scale),NativeFxMul(y,scale),NativeFxMul(z,scale),m(15)};
}
template<class Reader> float ReadNativeFxFloat(const Reader& r,uint32_t at) { return std::bit_cast<float>(r.Word(at)); }
template<class Reader> NativeFxVec3 ReadNativeFxVec3(const Reader& r,uint32_t at) {
  return {ReadNativeFxFloat(r,at),ReadNativeFxFloat(r,r.Add(at,4)),ReadNativeFxFloat(r,r.Add(at,8))};
}
template<class Reader> NativeFxVec4 ReadNativeFxVec4(const Reader& r,uint32_t at) {
  return {ReadNativeFxFloat(r,at),ReadNativeFxFloat(r,r.Add(at,4)),ReadNativeFxFloat(r,r.Add(at,8)),ReadNativeFxFloat(r,r.Add(at,12))};
}

// 821A7640's 48-byte record: +0..+8 position, +16..+28 RGBA, +32 radius, +36
// angle. +12 and +40/+44 are never read.
struct NativeParticleRecord {
  NativeFxVec3 position{};
  NativeFxVec4 colour{};
  float radius=0,angle=0;
};
// Vs_Particle vertex, 44 bytes: position, UV, (angle, radius), colour.
struct NativeParticleVertex {
  NativeFxVec3 position{};
  std::array<float,2> uv{};
  float angle=0,radius=0;
  NativeFxVec4 colour{};
};
// VS_3DTex/PS_Tex vertex, 36 bytes: position, UV, colour.
struct NativeRibbonVertex {
  NativeFxVec3 position{};
  std::array<float,2> uv{};
  NativeFxVec4 colour{};
};
// VS_3D/PS_Main vertex of 821A7B58, 16 bytes: position and a D3DCOLOR word.
struct NativeColourVertex {
  NativeFxVec3 position{};
  uint32_t colour=0;
};
static_assert(sizeof(NativeParticleVertex)==44 && sizeof(NativeRibbonVertex)==36 && sizeof(NativeColourVertex)==16);
inline constexpr uint32_t kNativeParticleRecordBytes=48,kNativeParticleRecordsPerCall=1000;
inline constexpr uint32_t kNativeRibbonPointBytes=32,kNativeRibbonPointLimit=100;
// The most vertices one clGrassMap call carries (whole quads; the immediate
// recording refuses more than 16384).
inline constexpr uint32_t kNativeGrassMapCallVertices=16384;
template<class Reader>
NativeParticleRecord ReadNativeParticleRecord(const Reader& r,uint32_t at) {
  return {ReadNativeFxVec3(r,at),ReadNativeFxVec4(r,r.Add(at,16)),ReadNativeFxFloat(r,r.Add(at,32)),ReadNativeFxFloat(r,r.Add(at,36))};
}
// 821A7640's inner loop: four vertices per record, UVs (0,0) (1,0) (1,1) (0,1)
// from the zero/one constants and the angle advanced by 0, pi/2, pi, 3pi/2.
// Implemented in native_full_frame_effects.cpp.
std::vector<NativeParticleVertex> ExpandNativeParticleRecords(std::span<const NativeParticleRecord> records,
                                                             const NativeEffectConstants& k);

// Ribbon input point, 32-byte stride: +0..+8 position, +20 the V coordinate.
struct NativeRibbonPoint {
  NativeFxVec3 position{};
  float v=0;
};
template<class Reader>
NativeRibbonPoint ReadNativeRibbonPoint(const Reader& r,uint32_t at) {
  return {ReadNativeFxVec3(r,at),ReadNativeFxFloat(r,r.Add(at,20))};
}
// sub_821A8628: independent segments from point pairs (count/2 of them, at most
// 100), each a camera-facing quad of `width`; primitive 13. Needs count >= 2.
std::vector<NativeRibbonVertex> BuildNativeRibbonSegments(std::span<const NativeRibbonPoint> points,
  const NativeFxVec4& colour,float width,const NativeFxVec3& eye,const NativeEffectConstants& k);
// sub_821A8090: a strip through up to 100 points, two vertices per point, the
// side taken from the averaged neighbouring directions; primitive 6.
std::vector<NativeRibbonVertex> BuildNativeRibbonStrip(std::span<const NativeRibbonPoint> points,
  const NativeFxVec4& colour,float width,const NativeFxVec3& eye,const NativeEffectConstants& k);
// The 48-byte point sub_821A88E8 and sub_821A8360 read: +0..+8 position,
// +16..+28 RGBA, +32 width, +40 V. +12, +36 and +44 are never read. Colour and
// width are per point where 821A8628/821A8090 take one of each per call.
struct NativeColourRibbonPoint {
  NativeFxVec3 position{};
  NativeFxVec4 colour{};
  float width=0,v=0;
};
inline constexpr uint32_t kNativeColourRibbonPointBytes=48;
template<class Reader>
NativeColourRibbonPoint ReadNativeColourRibbonPoint(const Reader& r,uint32_t at) {
  return {ReadNativeFxVec3(r,at),ReadNativeFxVec4(r,r.Add(at,16)),ReadNativeFxFloat(r,r.Add(at,32)),ReadNativeFxFloat(r,r.Add(at,40))};
}
// sub_821A88E8: 821A8628's segments (same side, order and UVs) with the pair's
// width from P0+32 and each vertex taking its own point's colour; primitive 13.
std::vector<NativeRibbonVertex> BuildNativeColourRibbonSegments(std::span<const NativeColourRibbonPoint> points,
  const NativeFxVec3& eye,const NativeEffectConstants& k);
// sub_821A8360: 821A8090's strip (same directions, side and order) with the
// width from P[i]+32 and the pair's colour from P[i]+16; primitive 6.
std::vector<NativeRibbonVertex> BuildNativeColourRibbonStrip(std::span<const NativeColourRibbonPoint> points,
  const NativeFxVec3& eye,const NativeEffectConstants& k);
// sub_821A7E08: the same strip through up to 100 points of 16-byte stride
// (+0..+8 position; +12 unread), with no UV and one D3DCOLOR word (r7) on every
// vertex, drawn through 821A7B58 as primitive 6 (r6 = 2*count-2 primitives).
// Its side math is 821A8090's statement for statement (D = P1-P0 first, the
// last point reusing P[n-2]..P[n-1], e = A - eye, the same fmsubs pairing and
// 821B0320), so the positions are BuildNativeRibbonStrip's. None below 2.
std::vector<NativeColourVertex> BuildNativeColourStrip(std::span<const NativeFxVec3> points,uint32_t colour,float width,
  const NativeFxVec3& eye,const NativeEffectConstants& k);

// Technique objects in the effect-shader object: 821A7640 r8==0 -> +244 with
// its texture into the sampler list at +272 (Ps_Particle), r8!=0 -> +288/+316
// (Ps_ZParticle); 821A7C70 -> +188/+216 (VS_3DTex/PS_Tex); 821A7B58 -> +160
// (VS_3D/PS_Main, lwz r3,176(r27)), untextured: it never calls 821BC4C8.
// Utility3DTexA is clGrassMap's (native_map_effects.h): its draw's `effect` is
// the cl3D9_Utility object at grass+1256 (constructed by 8218D8F0, techniques
// set up by 8218D4C8), and 8218D440 binds the technique at +172
// ("Utility_3DTexA", 821BE5D8/821BD5B0) with its sampler list at +200
// (821BC4C8(r3 +172, r4 +200, r5 texture)) and activates 821B94E8([+188]).
enum class NativeEffectTechnique : uint8_t { Particle, ZParticle, Ribbon, Solid, Utility3DTexA };
inline constexpr uint32_t NativeEffectTechniqueOffset(NativeEffectTechnique t) {
  return t==NativeEffectTechnique::Particle?244:t==NativeEffectTechnique::ZParticle?288:
         t==NativeEffectTechnique::Solid?160:t==NativeEffectTechnique::Utility3DTexA?172:188;
}
inline constexpr uint32_t NativeEffectSamplerListOffset(NativeEffectTechnique t) {
  return t==NativeEffectTechnique::Particle?272:t==NativeEffectTechnique::ZParticle?316:
         t==NativeEffectTechnique::Utility3DTexA?200:216;
}
// The technique object is not itself a material: 821A7640 activates
// 821B94E8([technique+16]) (lwz r3,16(r30), r30 = +244/+288) and 821A7C70
// 821B94E8([+204]) = [+188+16]. That word is the 112-byte material (pass
// record: +96/+104 state operations, +108 the shader pair) 821B8E48 reads.
inline constexpr uint32_t kNativeEffectTechniqueMaterial=16;
template<class Reader>
uint32_t NativeEffectTechniqueMaterial(const Reader& r,uint32_t effect,NativeEffectTechnique t) {
  return r.Word(r.Add(r.Add(effect,NativeEffectTechniqueOffset(t)),kNativeEffectTechniqueMaterial));
}
// sub_821BC4C8(r3 technique, r4 sampler list, r5 texture), which both
// producers call before the activation, unconditionally: the list's vector at
// +4 (begin +4, count +12) holds the material's local texture records, and
// each gets the texture at +4 (NativeMaterialTexture's handle word). It is
// the guest's own write, so the program read after it samples the draw's
// texture through the material's own sampler record.
inline constexpr uint32_t kNativeEffectSamplerListLimit=64;
template<class Reader>
void BindNativeEffectTexture(const Reader& r,uint32_t effect,NativeEffectTechnique t,uint32_t texture) {
  if(t==NativeEffectTechnique::Solid) return;  // 821A7B58 binds no texture.
  const auto list=r.Add(effect,NativeEffectSamplerListOffset(t));
  const auto begin=r.Word(r.Add(list,4)),count=r.Word(r.Add(list,12));
  if(count>kNativeEffectSamplerListLimit) throw std::runtime_error("native effect sampler list is too long");
  for(uint32_t i=0;i<count;++i) r.StoreWord(r.Add(r.Word(r.Add(begin,i*4)),4),texture);
}
// Blend (r7 of 821A7640, r8 of 821A7C70): 0 = SRCALPHA/INVSRCALPHA (6/7),
// 1 = ONE/ONE; any other value leaves the device's blend as it was. The
// values are the li r4 to 82135078 (state 0x48) and 82135108 (0x4c).
enum : int32_t { kNativeEffectBlendAlpha=0,kNativeEffectBlendAdditive=1 };
struct NativeEffectDraw {
  enum class Kind : uint8_t { Particles,RibbonQuads,RibbonStrip,ColourStrip };
  Kind kind=Kind::Particles;
  NativeEffectTechnique technique=NativeEffectTechnique::Particle;
  uint32_t effect=0,texture=0;
  int32_t blend=kNativeEffectBlendAlpha;
  // 821A7C70 only: sub_82135578 sets device+10420 bit 2 to (r9==1). The
  // particle expander does not touch it.
  bool sets_depth_write=false,depth_write=false;
  std::vector<NativeParticleRecord> records;
  std::vector<NativeParticleVertex> particle_vertices;
  std::vector<NativeRibbonVertex> ribbon_vertices;
  std::vector<NativeColourVertex> colour_vertices;
  uint32_t primitive() const { return kind==Kind::RibbonStrip || kind==Kind::ColourStrip?6:13; }
  // 821A7C70 and 821A7B58 set blend and depth write (82135078/82135108/
  // 82135578) before their 821B94E8, so the technique's own state operations
  // win over them; so does clGrassMap (8218D398 before every 8218D440);
  // 821A7640 sets its blend after the activation, so the draw's blend wins.
  bool state_before_activation() const {
    return technique==NativeEffectTechnique::Ribbon || technique==NativeEffectTechnique::Solid ||
           technique==NativeEffectTechnique::Utility3DTexA;
  }
  uint32_t stride() const { return kind==Kind::Particles?44:kind==Kind::ColourStrip?16:36; }
  uint32_t vertex_count() const {
    return uint32_t(kind==Kind::Particles?particle_vertices.size():
                    kind==Kind::ColourStrip?colour_vertices.size():ribbon_vertices.size());
  }
  bool empty() const { return vertex_count()==0; }
};
// Whether two draws activate identically: the host activation of an effect draw
// (the texture word 821BC4C8 stores, the technique's material and program, its
// constants and samplers, the blend and depth-write states around it and the
// immediate declaration) reads exactly these fields and never the vertices.
// Adjacent draws for which this holds are activated once (clElectricWire's
// strips: one per record, all alike).
inline bool NativeEffectDrawsShareActivation(const NativeEffectDraw& a,const NativeEffectDraw& b) {
  return a.kind==b.kind && a.technique==b.technique && a.effect==b.effect && a.texture==b.texture &&
    a.blend==b.blend && a.sets_depth_write==b.sets_depth_write && a.depth_write==b.depth_write;
}
// Vertex ranges of the guest's DrawPrimitiveUP calls for this draw: 821A7640
// issues one per 1000 records, the ribbons one each. (first, count) pairs.
std::vector<std::pair<uint32_t,uint32_t>> NativeEffectDrawCalls(const NativeEffectDraw& draw);
// Vertices [first, first+count) as the guest would have them in memory: 32-bit
// big-endian words in declaration order. This is what the immediate recording
// path (and the mesh's declaration-driven conversion) consumes.
std::vector<uint8_t> EncodeNativeEffectVertices(const NativeEffectDraw& draw,uint32_t first,uint32_t count);

inline NativeEffectDraw MakeNativeParticleDraw(uint32_t effect,std::vector<NativeParticleRecord> records,
    uint32_t texture,int32_t blend,uint32_t technique,const NativeEffectConstants& k) {
  NativeEffectDraw draw;
  draw.kind=NativeEffectDraw::Kind::Particles;
  draw.technique=technique?NativeEffectTechnique::ZParticle:NativeEffectTechnique::Particle;  // cmpwi r30,0
  draw.effect=effect; draw.texture=texture; draw.blend=blend;
  draw.records=std::move(records);
  draw.particle_vertices=ExpandNativeParticleRecords(draw.records,k);
  return draw;
}
// 821A7C70 r7 texture, r8 blend, r9 depth-write flag.
inline NativeEffectDraw MakeNativeRibbonDraw(uint32_t effect,NativeEffectDraw::Kind kind,
    std::vector<NativeRibbonVertex> vertices,uint32_t texture,int32_t blend,uint32_t depth_flag) {
  NativeEffectDraw draw;
  draw.kind=kind; draw.technique=NativeEffectTechnique::Ribbon;
  draw.effect=effect; draw.texture=texture; draw.blend=blend;
  draw.sets_depth_write=true; draw.depth_write=depth_flag==1;
  draw.ribbon_vertices=std::move(vertices);
  return draw;
}
// 821A7B58 r7 blend, r8 depth-write flag (no texture).
inline NativeEffectDraw MakeNativeColourStripDraw(uint32_t effect,std::vector<NativeColourVertex> vertices,
    int32_t blend,uint32_t depth_flag) {
  NativeEffectDraw draw;
  draw.kind=NativeEffectDraw::Kind::ColourStrip; draw.technique=NativeEffectTechnique::Solid;
  draw.effect=effect; draw.blend=blend;
  draw.sets_depth_write=true; draw.depth_write=depth_flag==1;
  draw.colour_vertices=std::move(vertices);
  return draw;
}
// 821A7640 compares its count signed against 0 and 1000; a negative count
// would run the inner loop to 2^32, so it is refused rather than reproduced.
template<class Reader>
std::vector<NativeParticleRecord> ReadNativeParticleArray(const Reader& r,uint32_t array,uint32_t count_word) {
  const auto count=int32_t(count_word);
  if(count<0 || count>(1<<20)) throw std::runtime_error("native effect particle count out of range");
  std::vector<NativeParticleRecord> records;
  records.reserve(size_t(count));
  for(int32_t i=0;i<count;++i) records.push_back(ReadNativeParticleRecord(r,r.Add(array,uint32_t(i)*kNativeParticleRecordBytes)));
  return records;
}

// The effect classes, keyed by their slot 4 (vtable+16).
enum class NativeEffectClass : uint8_t {
  Unknown,Particle01Limit,Particle02,Glass,RocketAmmo01,AcidAmmo01,BeamAmmo01,RocketAmmo02,SolidAmmo01,LaserAmmo01,WebAmmo01,
  EffectEtc02,Spark01,MuzzleFlash,Empty,Spark02,EffectEtc01,SmokeLine,
  // clGrassMap: a map effect (clMapEffectManager's list, native_map_effects.h),
  // filed as an item of this type; never classified from an effect list.
  GrassMap
};
// The classes that register with the "Effect" manager (their constructors
// pass the string at 0x82004460 to 821C0AE0) and whose slot 4 no effect
// builder covers. The full frame never calls these slot 4s. Audit of each
// slot 4 and everything it calls, for state that simulation reads (lifetimes,
// counters, flags):
//   clBombAmmo01     82007394 821152D0  b 821C9C20 (model draw)
//   clCentryGun01    820073C4 82115AE8  b 821C9C20
//   clGrenadeAmmo01  8200740C 82117298  b 821C9C20
//   clMissileAmmo01  82007478 82118648  b 821C9C20
//   clBrokenPiece    820077F4 82120168  b 821C9C20
//   clShellCase01    82014D4C 8218A658  b 821C9C20
//     821C9C20 stores only the draw's shader constants: 821A1738 and
//     821C8000 (via 821A17D8) write matrices into the constant blocks it is
//     handed, 821B2C28 sets device state and draws. None is a simulation field.
//     These six are model classes: the models pass draws them from the render
//     registry (native_render_registry.cpp, Rigid).
//   clBrokenObject   820077D8 8211FAA8  +712 = +708, then 821C8C58/821C9478
//     (pose) and 821C9C20. Its slot 3 8211FAF8 releases the object once
//     +708 - +712 > 10, so +712 is simulation state: the models pass stores
//     it natively for every object whose slot 4 the guest would call
//     (NativeFullFrameBrokenObjects). A copy, so a repeated render is a no-op
//     and it needs no per-tick gate.
//   clIKTest         82007050 8210E220  debug lines through 821A8CF0 ->
//   clDrawTestObject 82020578 821E5558  821A7B58 (device state and a draw);
//     every other store is to the stack. Test objects.
// The other members are built: the blr slot 4 8252B718 (clAcidAmmo02,
// clFireAmmo01, clPlasmaAmmo01, clMuzzleSmoke01, clFriendPeople_Generator)
// is Empty, clParticle01 shares clParticle01_Limit's 8211D250, and of the
// builders only clEffectEtc02 writes simulation state (+612,
// CommitNativeEffectDraw). So no class left unbuilt here advances state that
// the full frame drops.
struct NativeEffectUnbuiltSlot {
  uint32_t slot4;
  const char* name;
  bool model;  // Drawn by the models pass (the render registry).
};
inline constexpr NativeEffectUnbuiltSlot kNativeEffectUnbuiltSlots[]{
  {0x8210E220,"clIKTest",false},{0x821152D0,"clBombAmmo01",true},{0x82115AE8,"clCentryGun01",true},
  {0x82117298,"clGrenadeAmmo01",true},{0x82118648,"clMissileAmmo01",true},{0x8211FAA8,"clBrokenObject",true},
  {0x82120168,"clBrokenPiece",true},{0x8218A658,"clShellCase01",true},{0x821E5558,"clDrawTestObject",false}};
inline const NativeEffectUnbuiltSlot* FindNativeEffectUnbuiltSlot(uint32_t slot4) {
  for(const auto& slot:kNativeEffectUnbuiltSlots) if(slot.slot4==slot4) return &slot;
  return nullptr;
}
// A known effect-manager class's slot 4 that has no effect builder, named for
// the log; null otherwise.
inline const char* NativeEffectSlotName(uint32_t slot4) {
  const auto* slot=FindNativeEffectUnbuiltSlot(slot4);
  return slot?slot->name:nullptr;
}
inline NativeEffectClass ClassifyNativeEffect(uint32_t slot4) {
  switch(slot4) {
    case 0x8211D250: return NativeEffectClass::Particle01Limit;  // clParticle01_Limit, vtable 0x82004F6C
    case 0x8211DB70: return NativeEffectClass::Particle02;       // clParticle02, 0x82007784
    case 0x8217D6E0: return NativeEffectClass::Glass;            // clEffectGlass, 0x82012CC8
    case 0x82119A10: return NativeEffectClass::RocketAmmo01;     // clRocketAmmo01, 0x820074F4
    case 0x82113308: return NativeEffectClass::AcidAmmo01;       // clAcidAmmo01, 0x820072F0
    case 0x82114A98: return NativeEffectClass::BeamAmmo01;       // clBeamAmmo01, 0x82007368
    case 0x8211A0E8: return NativeEffectClass::RocketAmmo02;     // clRocketAmmo02, 0x82007524
    case 0x8211B088: return NativeEffectClass::SolidAmmo01;      // clSolidAmmo01, 0x82007614
    case 0x82117CD0: return NativeEffectClass::LaserAmmo01;      // clLaserAmmo01, 0x82007438
    case 0x8211BB80: return NativeEffectClass::WebAmmo01;        // clWebAmmo01, 0x82007670
    case 0x8217C4A0: return NativeEffectClass::EffectEtc02;      // clEffectEtc02, 0x82012C78
    case 0x8211E7A0: return NativeEffectClass::Spark01;          // clSpark01, strip via 821A8090
    case 0x821897A8: return NativeEffectClass::MuzzleFlash;      // clMuzzleFlash, segments via 821A8628
    case 0x8252B718: return NativeEffectClass::Empty;            // a bare blr: nothing to draw
    case 0x8211F540: return NativeEffectClass::Spark02;          // clSpark02, 0x820077BC, segments via 821A88E8
    case 0x8217ECB8: return NativeEffectClass::EffectEtc01;      // clEffectEtc01, 0x82012CEC, quads via 8217EA40
    case 0x82121848: return NativeEffectClass::SmokeLine;        // clSmokeLine, 0x82007940, strip via 821A8360
    default: return NativeEffectClass::Unknown;
  }
}
struct NativeEffectInputs {
  uint32_t effect=0;
  NativeFxVec3 eye{};
  NativeEffectConstants k{};
};
// The inputs for the view whose row-vector view matrix is `view` (the pass
// camera's view: the words 821BE8D0 hands 821A19F0). The eye is derived from
// it, never read from [8257C02C]+192, which the full-frame renderer leaves as
// the last guest-rendered view wrote it.
template<class Reader>
NativeEffectInputs ReadNativeEffectInputs(const Reader& r,const std::array<uint32_t,16>& view) {
  NativeEffectInputs in;
  in.effect=r.Word(kNativeEffectShaderGlobal);
  const auto eye=NativeEffectEyeFromView(view,ReadNativeFxFloat(r,kNativeEffectEyeScale));
  in.eye={eye[0],eye[1],eye[2]};
  in.k=ReadNativeEffectConstants(r);
  return in;
}
// What [8257C02C]+192..+204 holds now: diagnostics only (the stale-eye count).
template<class Reader>
NativeFxVec4 ReadNativeEffectGuestEye(const Reader& r) {
  return ReadNativeFxVec4(r,r.Add(r.Word(kNativeEffectCameraGlobal),kNativeEffectEyeOffset));
}

// The points 821A8628 reads for `count` (r5): none below 2 (signed), else
// srawi/addze count/2 pairs, capped at 100.
template<class Reader>
std::vector<NativeRibbonPoint> ReadNativeRibbonSegmentPoints(const Reader& r,uint32_t array,uint32_t count) {
  std::vector<NativeRibbonPoint> points;
  if(int32_t(count)<2) return points;
  const auto used=std::min<uint32_t>(uint32_t(int32_t(count)/2),kNativeRibbonPointLimit)*2;
  points.reserve(used);
  for(uint32_t i=0;i<used;++i) points.push_back(ReadNativeRibbonPoint(r,r.Add(array,i*kNativeRibbonPointBytes)));
  return points;
}
// The points 821A8090 reads for `count` (r5): none below 2 (signed), else up to 100.
template<class Reader>
std::vector<NativeRibbonPoint> ReadNativeRibbonStripPoints(const Reader& r,uint32_t array,uint32_t count) {
  std::vector<NativeRibbonPoint> points;
  if(int32_t(count)<2) return points;
  const auto used=std::min<uint32_t>(count,kNativeRibbonPointLimit);
  points.reserve(used);
  for(uint32_t i=0;i<used;++i) points.push_back(ReadNativeRibbonPoint(r,r.Add(array,i*kNativeRibbonPointBytes)));
  return points;
}
// The points 821A88E8 reads for `count` (r5): as 821A8628's, 48 bytes apart.
template<class Reader>
std::vector<NativeColourRibbonPoint> ReadNativeColourRibbonSegmentPoints(const Reader& r,uint32_t array,uint32_t count) {
  std::vector<NativeColourRibbonPoint> points;
  if(int32_t(count)<2) return points;
  const auto used=std::min<uint32_t>(uint32_t(int32_t(count)/2),kNativeRibbonPointLimit)*2;
  points.reserve(used);
  for(uint32_t i=0;i<used;++i) points.push_back(ReadNativeColourRibbonPoint(r,r.Add(array,i*kNativeColourRibbonPointBytes)));
  return points;
}
// The points 821A8360 reads for `count` (r5): as 821A8090's, 48 bytes apart.
template<class Reader>
std::vector<NativeColourRibbonPoint> ReadNativeColourRibbonStripPoints(const Reader& r,uint32_t array,uint32_t count) {
  std::vector<NativeColourRibbonPoint> points;
  if(int32_t(count)<2) return points;
  const auto used=std::min<uint32_t>(count,kNativeRibbonPointLimit);
  points.reserve(used);
  for(uint32_t i=0;i<used;++i) points.push_back(ReadNativeColourRibbonPoint(r,r.Add(array,i*kNativeColourRibbonPointBytes)));
  return points;
}
// clEffectEtc01's slot 4 (8217ECB8) calls 8217EA40 once per entry while the
// index is below the signed count at +612; the entries are 224 bytes apart
// from [+704], each quad's four points 16 bytes apart from entry+64. The
// limit is the native pass's own refusal, not the guest's.
inline constexpr uint32_t kNativeEffectEtc01Count=612,kNativeEffectEtc01Entries=704,kNativeEffectEtc01EntryBytes=224,
  kNativeEffectEtc01QuadOffset=64,kNativeEffectEtc01EntryLimit=1u<<16;

// Per-class builders. Each returns the draws its slot 4 issues, in order.
namespace native_effect_builders {
// clAcidAmmo01 (82113308) and clBeamAmmo01 (82114A98) share the shape: eight
// records copied forward from record 0, each moved by one step.
template<class Reader>
NativeFxVec3 Step(const Reader& r,uint32_t object,uint32_t direction,float size,const NativeEffectConstants& k) {
  // ld/std of the 16 bytes, then sub_821B0320 with f1 = size*0.25.
  return NativeFxSetLength(ReadNativeFxVec3(r,r.Add(object,direction)),NativeFxMul(size,k.quarter),k);
}
template<class Reader>
NativeParticleRecord Head(const Reader& r,uint32_t object,uint32_t position,float radius,const NativeEffectConstants& k) {
  // Position copied from `position`, colour from +656, the given radius, and
  // the angle is the zero constant (stfs f13).
  return {ReadNativeFxVec3(r,r.Add(object,position)),ReadNativeFxVec4(r,r.Add(object,656)),radius,k.zero};
}
template<class Reader>
std::vector<NativeParticleRecord> Acid(const Reader& r,uint32_t object,const NativeEffectConstants& k) {
  const float size=ReadNativeFxFloat(r,r.Add(object,676));
  const auto step=Step(r,object,896,size,k);
  std::vector<NativeParticleRecord> records{Head(r,object,912,size,k)};
  for(uint32_t i=1;i<8;++i) {
    auto next=records.back();                                  // 48-byte copy of the previous record
    next.radius=NativeFxMul(next.radius,k.three_quarters);
    next.position[1]=NativeFxSub(next.position[1],step[1]);
    next.position[0]=NativeFxSub(next.position[0],step[0]);
    next.position[2]=NativeFxSub(next.position[2],step[2]);
    records.push_back(next);
  }
  return records;
}
template<class Reader>
std::vector<NativeParticleRecord> Beam(const Reader& r,uint32_t object,const NativeEffectConstants& k) {
  const float size=ReadNativeFxFloat(r,r.Add(object,676));
  const auto step=Step(r,object,480,size,k);
  std::vector<NativeParticleRecord> records{Head(r,object,544,size,k)};
  for(uint32_t i=1;i<8;++i) {
    auto next=records.back();
    next.position[1]=NativeFxAdd(next.position[1],step[1]);   // fadds f11,f11,f13
    next.position[0]=NativeFxAdd(step[0],next.position[0]);   // fadds f11,f12,f10
    next.position[2]=NativeFxAdd(next.position[2],step[2]);
    records.push_back(next);
  }
  return records;
}
// clRocketAmmo02 (8211A0E8): twelve records back from the head at +544 (the
// +496 block's second 48), radius_i = fmadds(float(i)*(1/12), 0.2s - s, s).
template<class Reader>
std::vector<NativeParticleRecord> Rocket02(const Reader& r,uint32_t object,const NativeEffectConstants& k) {
  const float size=ReadNativeFxFloat(r,r.Add(object,676));
  const float tail=NativeFxMul(size,k.fifth);
  const auto step=Step(r,object,480,size,k);
  const float span=NativeFxSub(tail,size);
  std::vector<NativeParticleRecord> records{Head(r,object,544,size,k)};
  for(int32_t i=1;i<12;++i) {
    auto next=records.back();
    next.radius=NativeFxMadd(NativeFxMul(float(double(i)),k.twelfth),span,size);  // fcfid, frsp, fmuls, fmadds
    next.position[1]=NativeFxSub(next.position[1],step[1]);
    next.position[0]=NativeFxSub(next.position[0],step[0]);
    next.position[2]=NativeFxSub(next.position[2],step[2]);
    records.push_back(next);
  }
  return records;
}
// clSolidAmmo01 (8211B088): record 0 has radius 0.5s; then n = 11..1 with
// radius fmadds(float(n)*(1/12), 0.5s - s, s), each moved forward by one step.
template<class Reader>
std::vector<NativeParticleRecord> Solid(const Reader& r,uint32_t object,const NativeEffectConstants& k) {
  const float size=ReadNativeFxFloat(r,r.Add(object,676));
  const float head=NativeFxMul(size,k.half);
  const auto step=Step(r,object,480,size,k);
  const float span=NativeFxSub(head,size);
  std::vector<NativeParticleRecord> records{Head(r,object,544,head,k)};
  for(int32_t n=11;n>0;--n) {
    auto next=records.back();
    next.radius=NativeFxMadd(NativeFxMul(float(double(n)),k.twelfth),span,size);
    next.position[1]=NativeFxAdd(next.position[1],step[1]);
    next.position[0]=NativeFxAdd(next.position[0],step[0]);
    next.position[2]=NativeFxAdd(next.position[2],step[2]);
    records.push_back(next);
  }
  return records;
}
}  // namespace native_effect_builders

template<class Reader>
std::vector<NativeEffectDraw> BuildNativeEffectDraws(const Reader& r,uint32_t object,NativeEffectClass type,
                                                     const NativeEffectInputs& in) {
  namespace b=native_effect_builders;
  const auto& k=in.k;
  const auto word=[&](uint32_t offset) { return r.Word(r.Add(object,offset)); };
  const auto single=[&](uint32_t offset) { return ReadNativeFxFloat(r,r.Add(object,offset)); };
  std::vector<NativeEffectDraw> draws;
  const auto particles=[&](std::vector<NativeParticleRecord> records,uint32_t texture,int32_t blend) {
    draws.push_back(MakeNativeParticleDraw(in.effect,std::move(records),texture,blend,0,k));
  };
  switch(type) {
    case NativeEffectClass::Particle01Limit:   // r4 +396, r5 +404, r6 +424, r7 +444, r8 0
      particles(ReadNativeParticleArray(r,word(396),word(404)),word(424),int32_t(word(444)));
      break;
    case NativeEffectClass::Particle02:        // beqlr on +620 == 0
      if(word(620)) particles(ReadNativeParticleArray(r,word(604),word(620)),word(392),int32_t(word(420)));
      break;
    case NativeEffectClass::Glass:             // r5 +684, r4 +700, r6 +596, r7 0
      particles(ReadNativeParticleArray(r,word(700),word(684)),word(596),kNativeEffectBlendAlpha);
      break;
    case NativeEffectClass::RocketAmmo01: {    // r5 = +924 unless it is (unsigned) above +888
      const auto count=word(924),limit=word(888);
      particles(ReadNativeParticleArray(r,word(896),count>limit?limit:count),word(908),kNativeEffectBlendAdditive);
      break;
    }
    case NativeEffectClass::AcidAmmo01: particles(b::Acid(r,object,k),word(880),kNativeEffectBlendAlpha); break;
    case NativeEffectClass::BeamAmmo01: particles(b::Beam(r,object,k),word(880),kNativeEffectBlendAdditive); break;
    case NativeEffectClass::RocketAmmo02: particles(b::Rocket02(r,object,k),word(908),kNativeEffectBlendAdditive); break;
    case NativeEffectClass::SolidAmmo01: particles(b::Solid(r,object,k),word(884),kNativeEffectBlendAdditive); break;
    case NativeEffectClass::LaserAmmo01: {
      // 821A8628(r4 = two stack points, r5 2, r7 +880, r8 &+656, r9 1, r10 0,
      // f1 = +676 * +936). Each point: 16 bytes from +944/+960, +16/+20 = 0.5.
      const float width=NativeFxMul(single(676),single(936));
      const std::array<NativeRibbonPoint,2> points{{{ReadNativeFxVec3(r,r.Add(object,944)),k.half},
                                                   {ReadNativeFxVec3(r,r.Add(object,960)),k.half}}};
      const auto colour=ReadNativeFxVec4(r,r.Add(object,656));
      draws.push_back(MakeNativeRibbonDraw(in.effect,NativeEffectDraw::Kind::RibbonQuads,
        BuildNativeRibbonSegments(points,colour,width,in.eye,k),word(880),1,0));
      if(r.Bytes(r.Add(object,976),1)[0]) {
        // One record: position +912, colour +656 * 5, radius (+676 * +936) * 5.
        NativeParticleRecord head{ReadNativeFxVec3(r,r.Add(object,912)),{},k.zero,k.zero};
        for(size_t i=0;i<4;++i) head.colour[i]=NativeFxMul(colour[i],k.five);
        head.radius=NativeFxMul(NativeFxMul(single(676),single(936)),k.five);
        particles({head},word(880),kNativeEffectBlendAdditive);
      }
      break;
    }
    case NativeEffectClass::WebAmmo01: {
      // 821A8090(r4 +784, r5 +792, r7 +796, r8 colour 1.0 x4, r9 0, r10 0,
      // f1 = +484 * +780), then one record at the LAST point: array + count*32
      // - 32, read even when the count is below 2 (the strip is then skipped).
      const auto array=word(784),count=word(792);
      const NativeFxVec4 white{k.one,k.one,k.one,k.one};
      if(int32_t(count)>=2)
        draws.push_back(MakeNativeRibbonDraw(in.effect,NativeEffectDraw::Kind::RibbonStrip,
          BuildNativeRibbonStrip(ReadNativeRibbonStripPoints(r,array,count),white,NativeFxMul(single(484),single(780)),in.eye,k),
          word(796),0,0));
      const uint32_t last=array+(count<<5)-32;                  // rlwinm r10,r10,5,0,26; add; addi -32
      NativeParticleRecord head{ReadNativeFxVec3(r,last),white,k.zero,k.zero};
      head.radius=NativeFxMul(NativeFxMul(single(484),single(780)),k.web_scale);
      particles({head},word(812),kNativeEffectBlendAlpha);
      break;
    }
    case NativeEffectClass::EffectEtc02: {
      // 8217C4A0: min(+544, 32) quads (signed; none when <= 0) from the 64-byte
      // blocks at [+640], four points 16 bytes apart. UVs (+500,+504) (+508,+504)
      // (+508,+512) (+500,+512), colour +592..+604 on all four; 821A7C70 with
      // r4 13, r7 +528, r8 = byte [0x82554BF0] != 0, r9 0. The +612 decrement
      // is CommitNativeEffectDraw's, not a builder's.
      auto quads=int32_t(word(544));
      if(quads>32) quads=32;
      std::vector<NativeRibbonVertex> vertices;
      if(quads>0) {
        const NativeFxVec4 colour=ReadNativeFxVec4(r,r.Add(object,592));
        const float u0=single(500),v0=single(504),u1=single(508),v1=single(512);
        const std::array<std::array<float,2>,4> uv{{{u0,v0},{u1,v0},{u1,v1},{u0,v1}}};
        const auto points=word(640);
        vertices.reserve(size_t(quads)*4);
        for(int32_t q=0;q<quads;++q) for(uint32_t v=0;v<4;++v)
          vertices.push_back({ReadNativeFxVec3(r,r.Add(points,uint32_t(q)*64+v*16)),uv[v],colour});
      }
      const int32_t blend=r.Bytes(kNativeEffectEtc02BlendGlobal,1)[0]?kNativeEffectBlendAdditive:kNativeEffectBlendAlpha;
      draws.push_back(MakeNativeRibbonDraw(in.effect,NativeEffectDraw::Kind::RibbonQuads,std::move(vertices),word(528),blend,0));
      break;
    }
    case NativeEffectClass::Spark01:
      // 8211E7A0: b 821A8090(r4 +544, r5 +556, r7 +528, r8 &+512, r9 1, r10 0, f1 +440).
      draws.push_back(MakeNativeRibbonDraw(in.effect,NativeEffectDraw::Kind::RibbonStrip,
        BuildNativeRibbonStrip(ReadNativeRibbonStripPoints(r,word(544),word(556)),ReadNativeFxVec4(r,r.Add(object,512)),
          single(440),in.eye,k),word(528),kNativeEffectBlendAdditive,0));
      break;
    case NativeEffectClass::MuzzleFlash: {
      // 821897A8: two 821A8628 calls, both r7 +396, r8 &+480, r9 1, r10 0. The
      // first r4 +464, r5 = +424 << 1, f1 = [[+384]+20]; the second the two
      // points after those, r4 = +464 + (+424 << 6), r5 2, f1 the same offset
      // that far past [+384].
      const auto points=word(464),widths=word(384),pairs=word(424),texture=word(396);
      const auto colour=ReadNativeFxVec4(r,r.Add(object,480));
      const uint32_t tail=pairs<<6;                                // rlwinm r11,r11,6,0,25
      const auto segments=[&](uint32_t array,uint32_t count,float width) {
        draws.push_back(MakeNativeRibbonDraw(in.effect,NativeEffectDraw::Kind::RibbonQuads,
          BuildNativeRibbonSegments(ReadNativeRibbonSegmentPoints(r,array,count),colour,width,in.eye,k),
          texture,kNativeEffectBlendAdditive,0));
      };
      segments(points,pairs<<1,ReadNativeFxFloat(r,r.Add(widths,20)));
      segments(points+tail,2,ReadNativeFxFloat(r,widths+tail+20));   // add r4,r6,r11; add r31,r4,r11
      break;
    }
    case NativeEffectClass::Spark02:
      // 8211F540: b 821A88E8(r4 +536, r5 +544, r6 texture +472, r7 1, r8 0).
      draws.push_back(MakeNativeRibbonDraw(in.effect,NativeEffectDraw::Kind::RibbonQuads,
        BuildNativeColourRibbonSegments(ReadNativeColourRibbonSegmentPoints(r,word(536),word(544)),in.eye,k),
        word(472),kNativeEffectBlendAdditive,0));
      break;
    case NativeEffectClass::EffectEtc01: {
      // 8217ECB8: per entry i < +612 (cmpw, re-read each pass; nothing writes
      // it), 8217EA40(r4 [+704] + i*224 + 64, r5 +596, r6 &+656), which builds
      // one quad on the stack - positions r4+0/+16/+32/+48, UVs (0,0) (1,0)
      // (1,1) (0,1) from the zero/one constants, colour r6 on all four - and
      // calls 821A7C70(r4 13, r6 1, r7 r5, r8 0, r9 0): one draw per entry.
      const auto entries=int32_t(word(kNativeEffectEtc01Count));
      if(entries>int32_t(kNativeEffectEtc01EntryLimit)) throw std::runtime_error("native clEffectEtc01 entry count out of range");
      const auto colour=ReadNativeFxVec4(r,r.Add(object,656));
      const std::array<std::array<float,2>,4> uv{{{k.zero,k.zero},{k.one,k.zero},{k.one,k.one},{k.zero,k.one}}};
      for(int32_t i=0;i<entries;++i) {
        const auto quad=word(kNativeEffectEtc01Entries)+uint32_t(i)*kNativeEffectEtc01EntryBytes+kNativeEffectEtc01QuadOffset;
        std::vector<NativeRibbonVertex> vertices;
        vertices.reserve(4);
        for(uint32_t v=0;v<4;++v) vertices.push_back({ReadNativeFxVec3(r,r.Add(quad,v*16)),uv[v],colour});
        draws.push_back(MakeNativeRibbonDraw(in.effect,NativeEffectDraw::Kind::RibbonQuads,std::move(vertices),
          word(596),kNativeEffectBlendAlpha,0));
      }
      break;
    }
    case NativeEffectClass::SmokeLine: {
      // 82121848: bltlr on +620 < 2 (unsigned), then b 821A8360(r4 +604, r5
      // +620, r6 texture +392, r7 blend +424, r8 0); 821A8360 then refuses a
      // signed count below 2 as well.
      const auto count=word(620);
      if(count>=2)
        draws.push_back(MakeNativeRibbonDraw(in.effect,NativeEffectDraw::Kind::RibbonStrip,
          BuildNativeColourRibbonStrip(ReadNativeColourRibbonStripPoints(r,word(604),count),in.eye,k),
          word(392),int32_t(word(424)),0));
      break;
    }
    case NativeEffectClass::Empty: break;
    case NativeEffectClass::GrassMap: throw std::runtime_error("clGrassMap is a map effect (BuildNativeGrassMapDraws)");
    case NativeEffectClass::Unknown: throw std::runtime_error("unsupported native effect class");
  }
  std::erase_if(draws,[](const NativeEffectDraw& draw) { return draw.empty(); });
  return draws;
}

// One effect object filed into the transparent pass.
struct NativeEffectItem {
  uint16_t key=0;
  uint32_t order=0,object=0,slot4=0;
  NativeEffectClass type=NativeEffectClass::Unknown;
  std::vector<NativeEffectDraw> draws;
};
// The guest state a slot 4 call changes besides the device: clEffectEtc02's
// lifetime, lwz/addi -1/stw (wrapping), once per call, drawn or not (a count
// <= 0 still reaches the store). Run once for every object whose slot 4 the
// guest would have called this pass - each item CollectNativeEffects returns -
// and never for a culled, hidden, duplicate or undrawn-key object. The other
// builders' slot 4s store only to their stack (clSpark02 8211F540/821A88E8,
// clEffectEtc01 8217ECB8/8217EA40 and clSmokeLine 82121848/821A8360 included).
template<class Reader>
void CommitNativeEffectDraw(const Reader& r,const NativeEffectItem& item) {
  if(item.type!=NativeEffectClass::EffectEtc02) return;
  const auto at=r.Add(item.object,kNativeEffectEtc02Lifetime);
  r.StoreWord(at,r.Word(at)-1);
}
struct NativeEffectCollection {
  std::vector<NativeEffectItem> items;      // key >= 256, descending, filing order on ties
  std::vector<NativeEffectItem> immediate;  // mode 0: slot 4 runs inside the walk, before the drain
  std::vector<uint32_t> unsupported_slots;  // slot 4 of objects no builder covers
  uint32_t visited=0,duplicates=0,culled=0,hidden=0,undrawn_keys=0,unknown_modes=0,unsupported=0;
  uint32_t held=0;  // clEffectEtc02 lifetimes a render-only frame did not commit (commit false)
};
// clEffectObjectManager (vtable 0x820072D4) keeps its objects on the intrusive
// list at +48; its slot 2 (sub_820D4850, shared with clGameBossObject_Manager)
// walks it with sub_820B4038, the same culled walk the models use.
struct NativeEffectList {
  static constexpr uint32_t manager_vtable=0x820072D4,manager_offset=48,first=0,end=12,next=0,object=8;
};

// sub_820B4038 over one list, then sub_821C0C00 per surviving object. The one
// guest write is CommitNativeEffectDraw's (clEffectEtc02 +612); the walk's +48
// stamp, context+32..+44 and the bucket links are not written:
//  - object+48 == context+12 skips an object already visited this pass (the
//    walk stores the stamp); `visited`, shared by every walk of the pass,
//    stands in for the stamp;
//  - centre +288 through the camera matrix; -(z * context+8) > +76 culls; the
//    sphere test with +352 and, when it intersects, the box at +288 (the model
//    path's NativeVisibilitySphere/Box);
//  - u16 +64 != 0 skips; +52 == 0 runs slot 4 at once; 1/2 file a key computed
//    as sub_821C0C00 does with the transformed z as context+40.
// No byte +36 test exists on this path: clGameObject_Manager::slot1 reads +36,
// but that is the update walk, not the render walk.
// `order` is the pass-wide filing counter (see NativeTransparentItem).
// `eye_view` is the pass camera's view (ReadNativeEffectInputs).
// `commit` false withholds every CommitNativeEffectDraw (counted in `held`):
// the caller passes NativeFrameInputs::tick_frame, so in the unlocked loop the
// +612 lifetime, which the guest counts in draws, still counts once per
// simulation tick (NativeTickGate) instead of once per render. The draws are
// built from the same state either way; locked frames always commit.
template<class Reader>
NativeEffectCollection CollectNativeEffects(const Reader& r,uint32_t list,uint32_t context,const std::array<uint32_t,16>& eye_view,
                                            uint32_t& order,std::unordered_set<uint32_t>* visited=nullptr,bool commit=true) {
  NativeEffectCollection out;
  const auto view=ReadNativeSceneVisibilityView(r,context);
  const auto in=ReadNativeEffectInputs(r,eye_view);
  auto node=r.Word(r.Add(list,NativeEffectList::first));
  const auto end=r.Word(r.Add(list,NativeEffectList::end));
  for(uint32_t guard=0;node!=end;node=r.Word(r.Add(node,NativeEffectList::next))) {
    if(++guard>(1u<<20)) throw std::runtime_error("native effect list does not terminate");
    const auto object=r.Word(r.Add(node,NativeEffectList::object));
    ++out.visited;
    if(visited && !visited->insert(object).second) { ++out.duplicates; continue; }
    const auto bound=ReadNativeSceneVisibility(r,object,false);
    const auto center=NativeVisibilityTransform({bound.box[0],bound.box[1],bound.box[2],bound.box[3]},view.matrix);
    const float depth=-float(center[2]*view.depth_scale);
    auto visibility=depth>bound.distance?0u:NativeVisibilitySphere(view,center,bound.radius);
    if(visibility==2) visibility=NativeVisibilityBox(view,bound.box);
    if(!visibility) { ++out.culled; continue; }
    const auto hidden=r.Bytes(r.Add(object,64),2);
    if(hidden[0] || hidden[1]) { ++out.hidden; continue; }
    const auto mode=int32_t(r.Word(r.Add(object,52)));
    if(mode!=0 && mode!=1 && mode!=2) { ++out.unknown_modes; continue; }  // guest clamps an uninitialized float
    NativeEffectItem item;
    item.object=object;
    item.slot4=r.Word(r.Add(r.Word(object),16));
    item.type=ClassifyNativeEffect(item.slot4);
    if(mode) {
      item.key=ComputeNativeBucketKeyForDepth(r,context,object,std::bit_cast<uint32_t>(center[2])).key;
      item.order=order++;
    }
    if(item.type==NativeEffectClass::Unknown) {
      ++out.unsupported;
      if(std::find(out.unsupported_slots.begin(),out.unsupported_slots.end(),item.slot4)==out.unsupported_slots.end())
        out.unsupported_slots.push_back(item.slot4);
      continue;
    }
    if(mode && !NativeTransparentKeyDrawn(item.key)) { ++out.undrawn_keys; continue; }
    item.draws=BuildNativeEffectDraws(r,object,item.type,in);
    if(commit) CommitNativeEffectDraw(r,item);
    else if(item.type==NativeEffectClass::EffectEtc02) ++out.held;
    (mode?out.items:out.immediate).push_back(std::move(item));
  }
  std::stable_sort(out.items.begin(),out.items.end(),[](const NativeEffectItem& a,const NativeEffectItem& b) {
    return a.key!=b.key?a.key>b.key:a.order<b.order;
  });
  return out;
}
// The collection's filed items as transparent-pass items, for merging with the
// models' and map effects'. `record(recorder, item)` records one item's draws
// in order, each over NativeEffectDrawCalls with EncodeNativeEffectVertices.
template<class Record>
std::vector<NativeTransparentItem> NativeEffectTransparentItems(std::vector<NativeEffectItem> items,Record record) {
  std::vector<NativeTransparentItem> out;
  out.reserve(items.size());
  for(auto& item:items) {
    const auto shared=std::make_shared<const NativeEffectItem>(std::move(item));
    out.push_back({shared->key,shared->order,[shared,record](NativeBackendRecorder& recorder) { record(recorder,*shared); }});
  }
  return out;
}
template<class Reader>
NativeEffectCollection CollectNativeEffectManager(const Reader& r,uint32_t manager,uint32_t context,
                                                  const std::array<uint32_t,16>& eye_view,uint32_t& order,
                                                  std::unordered_set<uint32_t>* visited=nullptr,bool commit=true) {
  if(r.Word(manager)!=NativeEffectList::manager_vtable) throw std::runtime_error("not a clEffectObjectManager");
  return CollectNativeEffects(r,r.Add(manager,NativeEffectList::manager_offset),context,eye_view,order,visited,commit);
}
}
