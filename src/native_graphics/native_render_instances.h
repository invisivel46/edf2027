#pragma once
// Instanced model draws of the full-frame renderer: models a slot 4 draws once
// per computed world through 821C9DA8 instead of once with a pose vector
// through 821C9C20, and the guest math that produces those worlds.
//
// 821C9DA8(r3=instance,r4=world) (edf2017_recomp.34.cpp:8985) traps like
// 821C9C20 (null instance+0 container, instance+4 at the container's end
// node), then walks the 52-byte mesh records at node+44 (count node+52): a
// record whose byte +48 is zero uploads r4 through 821A17D8 and draws with
// 821B2C28; a record with +48 set is neither uploaded nor drawn. There is no
// pose vector, no palette path and instance+12 is never read.
//
// clUfoMother01_Dummy (vtable 820054D0, constructor 820EC2F8) slot 4 820EC180
// (edf2017_recomp.80.cpp:1951) first draws 821C9C20(this+1100,this+1144),
// then for each 20-byte record in [*(this+1220), +20*Word(this+1228)):
//   m = identity (1.0 [820008CC], 0.0 [820009A4])
//   821C7B20(m, rec+4)                 rows 0-2 = rotation about x
//   821C7D80(m, fmadds(rec+16, this+1216, rec+8))   m = m x rotation about y
//   row 3 = (m[8..10] * rec+12, 1.0)   translation along the rotated z row
//   821C8198(m, m, this+224)           m = m x world
//   m[8..10] *= 0.1f [820021F0]        the z row flattened after the world
//   821C9DA8(this+1172, m)             the MotherShip_sphere model
// The constructor fills the records (820EC518..820EC698, 6 rings): +0 the ring
// index (unread here), +4 the ring's x angle, +8 the instance's y angle
// j/count*2pi, +12 the radius ({390,385,380}[ring%3]*1.2) and +16 a signed
// per-ring spin rate. this+1216 is the spin phase: slot 3 820EBFB0, the
// per-update entity method, adds 0.01f [820028BC]; nothing else writes it.
// this+224 is the object's world. So every input is simulation state and no
// input is the camera: the worlds are a pure function of the tick's object
// fields, computed at registry tick time.
#include "guest_block.h"
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace edf::native {
using NativeGuestMatrix=std::array<float,16>;

// 821C8198(out,a,b): out = a x b, row-vector, as the recompiled body computes
// it. Each guest row is loaded byte-reversed, so host lane 0 is guest element
// 3; b is transposed with unpacks and every element is one DPPS (imm 0xFF) of
// a row and a column, whose hardware order is (l0+l1)+(l2+l3):
// (a.w*b3 + a.z*b2) + (a.y*b1 + a.x*b0). One product or sum per statement, so
// nothing contracts into a fused multiply-add. The body runs with the guest
// flush mode enabled (denormal inputs and results flush to zero); that is not
// modeled here.
inline float NativeGuestMatrixDot(const float* row,const NativeGuestMatrix& b,size_t column) {
  const float w=row[3]*b[12+column];
  const float z=row[2]*b[8+column];
  const float y=row[1]*b[4+column];
  const float x=row[0]*b[column];
  const float high=w+z;
  const float low=y+x;
  return high+low;
}
inline NativeGuestMatrix NativeGuestMatrixMultiply(const NativeGuestMatrix& a,const NativeGuestMatrix& b) {
  NativeGuestMatrix out{};
  for(size_t row=0;row<4;++row) for(size_t column=0;column<4;++column) out[row*4+column]=NativeGuestMatrixDot(a.data()+row*4,b,column);
  return out;
}

// The CRT sin/cos the rotation helpers call, from their constant table at
// 82556678 (+0..+112, doubles unless noted) plus 1.0 [82019BF0] and the
// out-of-range result NaN [82556D50]. These are image data, named by address.
struct NativeGuestTrig {
  static constexpr double half_pi=std::bit_cast<double>(0x3ff921fb54442d18ull);   // +0
  static constexpr double inv_pi=std::bit_cast<double>(0x3fd45f306dc9c883ull);    // +8
  static constexpr double limit=std::bit_cast<double>(0x41aa39de00000000ull);     // +16 (2.2e8)
  static constexpr float zero=std::bit_cast<float>(0x00000000u);                  // +24 (f32)
  static constexpr float one=std::bit_cast<float>(0x3f800000u);                   // +28 (f32)
  static constexpr float minus_one=std::bit_cast<float>(0xbf800000u);             // +32 (f32)
  static constexpr float half=std::bit_cast<float>(0x3f000000u);                  // +36 (f32)
  static constexpr double pi_high=std::bit_cast<double>(0x400921fb54400000ull);   // +40
  static constexpr double pi_low=std::bit_cast<double>(0x3de0b4611a600000ull);    // +48
  static constexpr std::array<double,8> poly{                                     // +56..+112
    std::bit_cast<double>(0xbfc5555555555555ull),std::bit_cast<double>(0x3f811111111110b0ull),
    std::bit_cast<double>(0xbf2a01a01a013e1aull),std::bit_cast<double>(0x3ec71de3a524f063ull),
    std::bit_cast<double>(0xbe5ae6454b5dc0abull),std::bit_cast<double>(0x3de6123c686ad430ull),
    std::bit_cast<double>(0xbd6ae420dc08499cull),std::bit_cast<double>(0x3ce880ff6993df95ull)};
  static constexpr double unit=std::bit_cast<double>(0x3ff0000000000000ull);      // 82019BF0
  static constexpr double nan=std::bit_cast<double>(0xfff8000000000000ull);       // 82556D50
};
// fctid / fctidz as the recompiled bodies convert (simde_mm_cvtsd_si64 /
// cvttsd_si64 behind the NaN and LLONG_MAX guards): NaN and any other
// out-of-range value give INT64_MIN; fctid rounds to nearest even (the
// default MXCSR mode the recompilation runs under).
inline int64_t NativeGuestFctid(double value) {
  if(std::isnan(value)) return INT64_MIN;
  if(value>double(LLONG_MAX)) return LLONG_MAX;
  if(!(value>=-9223372036854775808.0) || value>=9223372036854775808.0) return INT64_MIN;
  return int64_t(std::nearbyint(value));
}
inline int64_t NativeGuestFctidz(double value) {
  if(std::isnan(value)) return INT64_MIN;
  if(value>double(LLONG_MAX)) return LLONG_MAX;
  if(!(value>=-9223372036854775808.0) || value>=9223372036854775808.0) return INT64_MIN;
  return int64_t(value);
}
inline double NativeGuestNegate(double value) { return std::bit_cast<double>(std::bit_cast<uint64_t>(value)^0x8000000000000000ull); }
// The shared tail: r reduced, polynomial in r*r (fma chain from +112 down to
// +56, then 1.0), times r, negated for an odd quadrant.
inline double NativeGuestTrigPolynomial(double r,bool odd) {
  using K=NativeGuestTrig;
  const double r2=r*r;
  double p=std::fma(K::poly[7],r2,K::poly[6]);
  for(size_t i=6;i-->0;) p=std::fma(p,r2,K::poly[i]);
  p=std::fma(p,r2,K::unit);
  const double s=p*r;
  return odd?NativeGuestNegate(s):s;
}
// sub_821E9558 (edf2017_recomp.72.cpp:9055): sin.
inline double NativeGuestSin(double x) {
  using K=NativeGuestTrig;
  const double a=std::bit_cast<double>(std::bit_cast<uint64_t>(x)&~0x8000000000000000ull);
  const double sign=x>=0.0?double(K::one):double(K::minus_one);
  const double q=double(NativeGuestFctid(K::inv_pi*a));
  double r=-std::fma(K::pi_high,q,-a);
  const bool odd=NativeGuestFctidz(q)&1;
  r=-std::fma(K::pi_low,q,-r);
  const double s=NativeGuestTrigPolynomial(r,odd)*sign;
  const double result=a-K::limit>=0.0?K::nan:s;
  return std::bit_cast<uint64_t>(a)==0?x:result;
}
// sub_821E9630 (edf2017_recomp.0.cpp:9720): cos, as sin(|x|+pi/2) reduced
// about the half-integer quadrant n-0.5.
inline double NativeGuestCos(double x) {
  using K=NativeGuestTrig;
  const double a=std::bit_cast<double>(std::bit_cast<uint64_t>(x)&~0x8000000000000000ull);
  const double shifted=K::half_pi+a;
  const double n=double(NativeGuestFctid(K::inv_pi*shifted));
  const double q=n-double(K::half);
  const bool odd=NativeGuestFctidz(n)&1;
  double r=-std::fma(K::pi_high,q,-a);
  r=-std::fma(K::pi_low,q,-r);
  const double c=NativeGuestTrigPolynomial(r,odd);
  const double result=shifted-K::limit>=0.0?K::nan:c;
  return a==double(K::zero)?double(K::one):result;
}
// Single-precision steps as the guest rounds them (fmuls, fmadds/fmsubs).
inline float NativeGuestMul(float a,float b) { return float(double(a)*double(b)); }
inline float NativeGuestMadd(float a,float c,float b) { return float(std::fma(double(a),double(c),double(b))); }
inline float NativeGuestMsub(float a,float c,float b) { return float(std::fma(double(a),double(c),-double(b))); }
inline constexpr float kNativeGuestOne=1.0f,kNativeGuestZero=0.0f;  // [820008CC], [820009A4]
// sub_821C7B20 (edf2017_recomp.55.cpp:8684): rows 0-2 of m become the x
// rotation (1,0,0,0 / 0,c,s,0 / 0,-s,c,0); row 3 is not written.
inline void NativeGuestRotationX(NativeGuestMatrix& m,float angle) {
  const float s=float(NativeGuestSin(double(angle)));
  const float c=float(NativeGuestCos(double(angle)));
  m[0]=kNativeGuestOne; m[1]=kNativeGuestZero; m[2]=kNativeGuestZero; m[3]=kNativeGuestZero;
  m[4]=kNativeGuestZero; m[5]=c; m[6]=s; m[7]=kNativeGuestZero;
  m[8]=kNativeGuestZero; m[9]=-s; m[10]=c; m[11]=kNativeGuestZero;
}
// sub_821C7D80 (edf2017_recomp.50.cpp:8904): columns 0 and 2 of rows 0-2
// rotated about y in place (m x rotation y). The rows pair their products
// differently, and so round differently: row 0 fuses x*c, row 1 and row 2 z*s
// for the new x.
inline void NativeGuestRotateY(NativeGuestMatrix& m,float angle) {
  const float s=float(NativeGuestSin(double(angle)));
  const float c=float(NativeGuestCos(double(angle)));
  {
    const float x=m[0],z=m[2];
    const float xs=NativeGuestMul(x,s),zs=NativeGuestMul(z,s);
    m[2]=NativeGuestMsub(z,c,xs);
    m[0]=NativeGuestMadd(x,c,zs);
  }
  {
    const float x=m[4];
    const float xc=NativeGuestMul(x,c);
    const float z=m[6];
    const float xs=NativeGuestMul(x,s);
    const float nx=NativeGuestMadd(z,s,xc);
    m[6]=NativeGuestMsub(z,c,xs);
    m[4]=nx;
  }
  {
    const float x=m[8];
    const float xs=NativeGuestMul(x,s);
    const float z=m[10];
    const float xc=NativeGuestMul(x,c);
    m[10]=NativeGuestMsub(z,c,xs);
    m[8]=NativeGuestMadd(z,s,xc);
  }
}

// clUfoMother01_Dummy fields and the per-record world of 820EC180.
struct NativeMotherSpheres {
  static constexpr uint32_t vtable=0x820054D0u,constructor=0x820EC2F8u,render=0x820EC180u,update=0x820EBFB0u;
  static constexpr uint32_t world=224,instance=1172,phase=1216,records=1220,count=1228,stride=20;
  static constexpr uint32_t pitch=4,yaw=8,radius=12,spin=16;
  static constexpr uint32_t max_records=4096;
  static constexpr float flatten=0.1f;  // [820021F0] = 0x3DCCCCCD
};
struct NativeMotherSphereRecord { float pitch=0,yaw=0,radius=0,spin=0; };
inline NativeGuestMatrix NativeMotherSphereWorld(const NativeMotherSphereRecord& record,float phase,const NativeGuestMatrix& world) {
  NativeGuestMatrix m{kNativeGuestOne,kNativeGuestZero,kNativeGuestZero,kNativeGuestZero,
    kNativeGuestZero,kNativeGuestOne,kNativeGuestZero,kNativeGuestZero,
    kNativeGuestZero,kNativeGuestZero,kNativeGuestOne,kNativeGuestZero,
    kNativeGuestZero,kNativeGuestZero,kNativeGuestZero,kNativeGuestOne};
  NativeGuestRotationX(m,record.pitch);
  NativeGuestRotateY(m,NativeGuestMadd(record.spin,phase,record.yaw));
  // r1+80 = row 2 * rec+12 with r1+92 = 1.0 (stored once before the loop), copied over row 3.
  m[12]=NativeGuestMul(m[8],record.radius); m[13]=NativeGuestMul(m[9],record.radius);
  m[14]=NativeGuestMul(m[10],record.radius); m[15]=kNativeGuestOne;
  m=NativeGuestMatrixMultiply(m,world);
  for(size_t i=8;i<11;++i) m[i]=NativeGuestMul(m[i],NativeMotherSpheres::flatten);
  return m;
}
// Every sphere world 820EC180 would draw with now, in record order, into out
// (cleared first; its capacity is kept). Throws on an implausible table.
template<class Reader>
void ReadNativeMotherSphereWorlds(const Reader& reader,uint32_t object,std::vector<NativeGuestMatrix>& out) {
  using M=NativeMotherSpheres;
  out.clear();
  const auto begin=reader.Word(object+M::records),count=reader.Word(object+M::count);
  if(count>M::max_records || (count && (!begin || begin%4))) throw std::runtime_error("invalid native mother sphere records");
  if(!count) return;
  const auto phase=std::bit_cast<float>(reader.Word(object+M::phase));
  const auto* matrix=reader.Bytes(object+M::world,64);
  NativeGuestMatrix world{};
  for(size_t i=0;i<16;++i) world[i]=std::bit_cast<float>(GuestBlockWord(matrix+i*4));
  const auto* bytes=reader.Bytes(begin,size_t(count)*M::stride);
  const auto f=[&](uint32_t record,uint32_t offset) { return std::bit_cast<float>(GuestBlockWord(bytes+record*M::stride+offset)); };
  out.reserve(count);
  for(uint32_t i=0;i<count;++i)
    out.push_back(NativeMotherSphereWorld({f(i,M::pitch),f(i,M::yaw),f(i,M::radius),f(i,M::spin)},phase,world));
}
}
