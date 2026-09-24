#pragma once
// Native, bit-exact skeletal animation: the per-slot bone evaluation 821CE848
// (cl3D9_Animation, called only by Animation_Update 8210B4E8) and the hierarchy
// propagation 821D1688 (node+240 = node+176 x parent, then the children),
// transcribed statement for statement from the recompiled bodies. They are the
// same operations in the same order with the same roundings, so the results are
// the recompiled code's bits (a NaN's payload aside; its sign is not stable in the
// recompiled code either). The guest stack scratch the originals write (saved
// registers, 821CE848's r1+80 angle stash, 821C8198's staged rows) is not written.
//
// 821CE848(r3=slot), the 104-byte animation slot (Animation_Update walks them):
//   +12 records (96 bytes each), +20 record count, +24 key palette (0: no clip,
//   return), +32 time in frames, +36 end frame, +56 blend-active byte,
//   +60 blend length, +64 blend clock.
//   frame, frac: time < end ? (trunc(time), time - frame) : (trunc(time)-1, 1.0)
//   For each record: +0 bone struct B, +4 source channel, +16..+63 rows 0-2 of
//   the local rotation, +64..+72 translation, +80..+88 scale. The source channel
//   (DXA): +4 flags, +8/+12/+16 translation/rotation/scale divisors, +20 index
//   stream offset (from the channel), +24 indices per frame, +28..+36 constant
//   translation, +40..+48 constant Euler angles, +52..+60 constant scale. Flags:
//   1 translation keyed, 8 constant; 2 rotation keyed, 16 constant; 4 scale
//   keyed, 32 constant. A keyed group lerps two 6-byte int16 palette triples
//   (821CE5C8: fmadds, then times 1/divisor); a rotation is rebuilt from its
//   angles every call as rotX (821C7B20) then rotY (821C7D80) then rotZ
//   (821C7E30) with the guest sin/cos.
//   Blend (+56 set): w = +64/+60, clamped to 1.0 (which clears +56); every
//   record moves towards B's snapshot (B+16 rotation, B+64 translation, B+80
//   scale) by 1-w: translation and scale (snap-rec)*(1-w)+rec, the rotation by
//   821C8480 (fmadds lerp) then 821C84C8 (re-orthonormalised by cross products,
//   rows normalised by 821B0320).
//   Output: a record whose B+4 byte is set copies +16..+79 over *(B+0) (the
//   model node's local matrix, node+176) and scales its rows 0-2 by +80..+88.
//   All scalar code: the guest flush mode is off (the caller's hook turns it off
//   first, as the original's first FPU instruction does).
// 821D1688(r3=node,r4=parent): 821C8198(node+240,node+176,parent), then each of
//   the node+88 children (304 bytes each, from node+80) with node+240 as parent.
//   821C8198 is VMX (vmsum4fp128 = DPPS 0xFF) with the guest flush mode on: the
//   caller's hook turns it on before the first multiply, as the original does.
//
// Speed without new results: NativeSkeletalEvaluate builds four records'
// rotations at once, one per AVX2 lane with each lane's operations exactly the
// scalar ones (NativeSkeletalEvaluateScalar is the one-record reference), blends
// four records per lane group the same way, memoises the constant rotations by
// their angles' bits, and reads and writes whole 16-byte rows. The propagation's
// multiply for an aligned world does DPPS's multiplies and adds with mulps/addps
// in DPPS's order. Order across objects is untouched: all of it still runs in
// each object's own update, on the engine thread.
#include "native_render_instances.h"
#include <immintrin.h>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace edf::native {
struct NativeSkeletal {
  // Image constants the bodies load (lfs): 1.0 and 0.0.
  static constexpr uint32_t one_address=0x820008CCu,zero_address=0x820009A4u;
  // Animation slot.
  static constexpr uint32_t slot_records=12,slot_record_count=20,slot_palette=24,slot_time=32,slot_end=36,
    slot_blending=56,slot_blend_length=60,slot_blend_clock=64;
  // Record.
  static constexpr uint32_t record_size=96,record_bone=0,record_channel=4,record_rotation=16,record_translation=64,
    record_scale=80;
  // Source channel.
  static constexpr uint32_t channel_flags=4,channel_translation_divisor=8,channel_rotation_divisor=12,
    channel_scale_divisor=16,channel_stream=20,channel_stride=24,channel_translation=28,channel_angles=40,
    channel_scale=52;
  // Bone struct B.
  static constexpr uint32_t bone_local=0,bone_enabled=4,bone_rotation=16,bone_translation=64,bone_scale=80;
  // Model node.
  static constexpr uint32_t node_size=304,node_children=80,node_child_count=88,node_local=176,node_world=240;
};

// Guest float registers hold singles as doubles; every single-precision step
// rounds through float exactly as the recompiled code writes it.
namespace skeletal {
inline double F(double value) { return double(float(value)); }
inline double Fma(double a,double c,double b) { return F(std::fma(a,c,b)); }   // fmadds
inline double Fms(double a,double c,double b) { return F(std::fma(a,c,-b)); }  // fmsubs
// fctiwz, low word as stfiwx stores it.
inline uint32_t Fctiwz(double value) {
  const int64_t result=std::isnan(value)?int64_t(0x80000000u):value>=double(INT_MAX)?INT_MAX:
    int64_t(_mm_cvttsd_si32(_mm_load_sd(&value)));
  return uint32_t(result);
}
// Rows 0-2 of a 4x4 (12 floats, the w column included), as doubles of singles.
using Rows=std::array<double,12>;
// 821C7B20: the rotation about x replaces rows 0-2.
inline void RotationX(Rows& m,double angle,double one,double zero) {
  const double s=F(NativeGuestSin(angle));
  const double c=F(NativeGuestCos(angle));
  const double ns=-s;
  m={one,zero,zero,zero, zero,c,s,zero, zero,ns,c,zero};
}
// 821C7D80: columns 0 and 2 of rows 0-2, times the rotation about y.
inline void RotateY(Rows& m,double angle) {
  const double s=F(NativeGuestSin(angle));
  const double c=F(NativeGuestCos(angle));
  {
    const double x=m[0],z=m[2];
    const double xs=F(x*s),zs=F(z*s);
    m[2]=Fms(z,c,xs);
    m[0]=Fma(x,c,zs);
  }
  {
    const double x=m[4],xc=F(x*c),z=m[6],xs=F(x*s);
    const double nx=Fma(z,s,xc);
    m[6]=Fms(z,c,xs);
    m[4]=nx;
  }
  {
    const double x=m[8],xs=F(x*s),z=m[10],xc=F(x*c);
    m[10]=Fms(z,c,xs);
    m[8]=Fma(z,s,xc);
  }
}
// 821C7E30: columns 0 and 1 of rows 0-2, times the rotation about z.
inline void RotateZ(Rows& m,double angle) {
  const double s=F(NativeGuestSin(angle));
  const double c=F(NativeGuestCos(angle));
  for(size_t row=0;row<12;row+=4) {
    const double x=m[row],y=m[row+1];
    const double xs=F(x*s),ys=F(y*s);
    m[row+1]=Fma(y,c,xs);
    m[row]=Fms(x,c,ys);
  }
}
// NativeGuestSin/NativeGuestCos for up to four lanes at once (AVX2, FMA): the
// same double operations per lane, so the same bits. cos lanes (mask set) take
// the cos reduction (|x|+pi/2, quadrant n-0.5, no sign), sin lanes the sin one.
// fctid is vroundpd in the current rounding mode (nearbyint); where it would
// differ (NaN, |x| beyond 2^63) the lane's result is NaN either way, as is its
// quadrant parity (vcvttpd2dq), which only matters below the 2.2e8 limit.
inline __m256d NativeGuestSinCos4(__m256d x,__m256d cos_lanes) {
  using K=NativeGuestTrig;
  const __m256d sign_bit=_mm256_set1_pd(-0.0);
  const __m256d a=_mm256_andnot_pd(sign_bit,x);
  const __m256d argument=_mm256_blendv_pd(a,_mm256_add_pd(_mm256_set1_pd(K::half_pi),a),cos_lanes);
  const __m256d n=_mm256_round_pd(_mm256_mul_pd(_mm256_set1_pd(K::inv_pi),argument),_MM_FROUND_CUR_DIRECTION);
  const __m256d q=_mm256_blendv_pd(n,_mm256_sub_pd(n,_mm256_set1_pd(double(K::half))),cos_lanes);
  const __m128i quadrant=_mm256_cvttpd_epi32(n);
  const __m256i odd=_mm256_cmpeq_epi64(_mm256_cvtepi32_epi64(_mm_and_si128(quadrant,_mm_set1_epi32(1))),_mm256_set1_epi64x(1));
  __m256d r=_mm256_xor_pd(sign_bit,_mm256_fmadd_pd(_mm256_set1_pd(K::pi_high),q,_mm256_xor_pd(sign_bit,a)));
  r=_mm256_xor_pd(sign_bit,_mm256_fmadd_pd(_mm256_set1_pd(K::pi_low),q,_mm256_xor_pd(sign_bit,r)));
  const __m256d r2=_mm256_mul_pd(r,r);
  __m256d p=_mm256_fmadd_pd(_mm256_set1_pd(K::poly[7]),r2,_mm256_set1_pd(K::poly[6]));
  for(size_t i=6;i-->0;) p=_mm256_fmadd_pd(p,r2,_mm256_set1_pd(K::poly[i]));
  p=_mm256_fmadd_pd(p,r2,_mm256_set1_pd(K::unit));
  __m256d value=_mm256_mul_pd(p,r);
  value=_mm256_xor_pd(value,_mm256_and_pd(_mm256_castsi256_pd(odd),sign_bit));
  // sin: times +-1 by the sign of x (NaN: -1); cos: as is.
  const __m256d sign=_mm256_blendv_pd(_mm256_set1_pd(double(K::minus_one)),_mm256_set1_pd(double(K::one)),
    _mm256_cmp_pd(x,_mm256_setzero_pd(),_CMP_GE_OQ));
  value=_mm256_blendv_pd(_mm256_mul_pd(value,sign),value,cos_lanes);
  const __m256d beyond=_mm256_cmp_pd(_mm256_sub_pd(argument,_mm256_set1_pd(K::limit)),_mm256_setzero_pd(),_CMP_GE_OQ);
  value=_mm256_blendv_pd(value,_mm256_set1_pd(K::nan),beyond);
  // Zero: sin returns x itself (a signed zero), cos 1.0.
  const __m256d zero=_mm256_cmp_pd(a,_mm256_setzero_pd(),_CMP_EQ_OQ);
  const __m256d at_zero=_mm256_blendv_pd(x,_mm256_set1_pd(double(K::one)),cos_lanes);
  return _mm256_blendv_pd(value,at_zero,zero);
}
// The six results rotX/rotY/rotZ need, rounded to single as the builders do (frsp):
// s[k], c[k] = sin, cos of angle k.
inline void NativeGuestSinCos3(const double* angles,double* s,double* c) {
  const __m256d cos_lanes=_mm256_castsi256_pd(_mm256_setr_epi64x(0,0,0,-1));
  const __m256d first=NativeGuestSinCos4(_mm256_setr_pd(angles[0],angles[1],angles[2],angles[0]),cos_lanes);
  const __m256d second=NativeGuestSinCos4(_mm256_setr_pd(angles[1],angles[2],angles[1],angles[2]),
    _mm256_castsi256_pd(_mm256_set1_epi64x(-1)));
  alignas(32) double a[4],b[4];
  _mm256_store_pd(a,first); _mm256_store_pd(b,second);
  s[0]=F(a[0]); s[1]=F(a[1]); s[2]=F(a[2]); c[0]=F(a[3]); c[1]=F(b[0]); c[2]=F(b[1]);
}
// rotX(angles[0]) then rotY(angles[1]) then rotZ(angles[2]) (821C7B20, 821C7D80,
// 821C7E30) with the six trig values computed together.
inline void RotationXYZ(Rows& m,const double* angles,double one,double zero) {
  double s[3],c[3];
  NativeGuestSinCos3(angles,s,c);
  m={one,zero,zero,zero, zero,c[0],s[0],zero, zero,-s[0],c[0],zero};
  {
    const double x=m[0],z=m[2];
    const double xs=F(x*s[1]),zs=F(z*s[1]);
    m[2]=Fms(z,c[1],xs);
    m[0]=Fma(x,c[1],zs);
  }
  {
    const double x=m[4],xc=F(x*c[1]),z=m[6],xs=F(x*s[1]);
    const double nx=Fma(z,s[1],xc);
    m[6]=Fms(z,c[1],xs);
    m[4]=nx;
  }
  {
    const double x=m[8],xs=F(x*s[1]),z=m[10],xc=F(x*c[1]);
    m[10]=Fms(z,c[1],xs);
    m[8]=Fma(z,s[1],xc);
  }
  for(size_t row=0;row<12;row+=4) {
    const double x=m[row],y=m[row+1];
    const double xs=F(x*s[2]),ys=F(y*s[2]);
    m[row+1]=Fma(y,c[2],xs);
    m[row]=Fms(x,c[2],ys);
  }
}
// ---- Four records at once (NativeSkeletalEvaluate's batched rotations). ----
// Each lane is one record and carries exactly the scalar code's operations in its
// order: F is the same float round trip (cvtpd2ps/cvtps2pd round and widen as
// cvtsd2ss/cvtss2sd do, lane by lane, under the same MXCSR), Fma/Fms the same
// double fma then F. So each lane's bits are the scalar function's.
namespace lanes {
inline __m256d F(__m256d x) { return _mm256_cvtps_pd(_mm256_cvtpd_ps(x)); }
inline __m256d Fma(__m256d a,__m256d c,__m256d b) { return F(_mm256_fmadd_pd(a,c,b)); }  // fmadds
inline __m256d Fms(__m256d a,__m256d c,__m256d b) { return F(_mm256_fmsub_pd(a,c,b)); }  // fmsubs: a*c-b, one rounding
inline __m256d Sin(__m256d x) { return NativeGuestSinCos4(x,_mm256_setzero_pd()); }
inline __m256d Cos(__m256d x) { return NativeGuestSinCos4(x,_mm256_castsi256_pd(_mm256_set1_epi64x(-1))); }
// RotationXYZ per lane: m[k] holds element k of rows 0-2 for the four records.
inline void RotationXYZ(__m256d* m,__m256d ax,__m256d ay,__m256d az,__m256d one,__m256d zero) {
  const __m256d s0=F(Sin(ax)),s1=F(Sin(ay)),s2=F(Sin(az));
  const __m256d c0=F(Cos(ax)),c1=F(Cos(ay)),c2=F(Cos(az));
  m[0]=one; m[1]=zero; m[2]=zero; m[3]=zero;
  m[4]=zero; m[5]=c0; m[6]=s0; m[7]=zero;
  m[8]=zero; m[9]=_mm256_xor_pd(s0,_mm256_set1_pd(-0.0)); m[10]=c0; m[11]=zero;
  {
    const __m256d x=m[0],z=m[2];
    const __m256d xs=F(_mm256_mul_pd(x,s1)),zs=F(_mm256_mul_pd(z,s1));
    m[2]=Fms(z,c1,xs);
    m[0]=Fma(x,c1,zs);
  }
  {
    const __m256d x=m[4],xc=F(_mm256_mul_pd(x,c1)),z=m[6],xs=F(_mm256_mul_pd(x,s1));
    const __m256d nx=Fma(z,s1,xc);
    m[6]=Fms(z,c1,xs);
    m[4]=nx;
  }
  {
    const __m256d x=m[8],xs=F(_mm256_mul_pd(x,s1)),z=m[10],xc=F(_mm256_mul_pd(x,c1));
    m[10]=Fms(z,c1,xs);
    m[8]=Fma(z,s1,xc);
  }
  for(size_t row=0;row<12;row+=4) {
    const __m256d x=m[row],y=m[row+1];
    const __m256d xs=F(_mm256_mul_pd(x,s2)),ys=F(_mm256_mul_pd(y,s2));
    m[row+1]=Fma(y,c2,xs);
    m[row]=Fms(x,c2,ys);
  }
}
// 821B0320 per lane (skeletal::Normalize below): a lane whose squared length
// equals zero stores zeros.
inline void Normalize(__m256d* v,__m256d length,__m256d zero) {
  const __m256d y=v[1];
  __m256d squared=F(_mm256_mul_pd(y,y));
  const __m256d x=v[0],z=v[2];
  squared=Fma(x,x,squared);
  squared=Fma(z,z,squared);
  const __m256d is_zero=_mm256_cmp_pd(squared,zero,_CMP_EQ_OQ);
  const __m256d scale=F(_mm256_div_pd(length,F(_mm256_sqrt_pd(squared))));
  v[0]=_mm256_blendv_pd(F(_mm256_mul_pd(x,scale)),zero,is_zero);
  v[1]=_mm256_blendv_pd(F(_mm256_mul_pd(y,scale)),zero,is_zero);
  v[2]=_mm256_blendv_pd(F(_mm256_mul_pd(z,scale)),zero,is_zero);
}
// 821C84C8 per lane (skeletal::Orthonormalize below).
inline void Orthonormalize(__m256d* r0,__m256d* r1,__m256d* r2,__m256d one,__m256d zero) {
  const auto mul=[](__m256d a,__m256d b) { return F(_mm256_mul_pd(a,b)); };
  r0[0]=Fms(r2[2],r1[1],mul(r2[1],r1[2]));
  r0[1]=Fms(r1[2],r2[0],mul(r2[2],r1[0]));
  r0[2]=Fms(r2[1],r1[0],mul(r1[1],r2[0]));
  r1[0]=Fms(r2[1],r0[2],mul(r0[1],r2[2]));
  r1[1]=Fms(r0[0],r2[2],mul(r2[0],r0[2]));
  r1[2]=Fms(r2[0],r0[1],mul(r2[1],r0[0]));
  Normalize(r0,one,zero);
  Normalize(r1,one,zero);
  Normalize(r2,one,zero);
}
// NativeSkeletalKeyLerp's element k per lane: first/second are the int16 keys.
inline __m256d KeyLerp(__m128i first,__m128i second,__m256d frac,__m256d reciprocal) {
  const __m256d a=F(_mm256_cvtepi32_pd(first)),b=F(_mm256_cvtepi32_pd(second));
  const __m256d delta=F(_mm256_sub_pd(b,a));
  return F(_mm256_mul_pd(Fma(delta,frac,a),reciprocal));
}
}  // namespace lanes

// 821B0320(v,length): v scaled to length; a zero squared length stores zeros.
inline void Normalize(double* v,double length,double zero) {
  const double y=v[1];
  double squared=F(y*y);
  const double x=v[0],z=v[2];
  squared=Fma(x,x,squared);
  squared=Fma(z,z,squared);
  if(squared==zero) { v[2]=zero; v[1]=zero; v[0]=zero; return; }
  const double scale=F(length/F(std::sqrt(squared)));
  v[0]=F(x*scale); v[1]=F(y*scale); v[2]=F(z*scale);
}
// 821C84C8 on rows r0, r1, r2 (3 elements each).
inline void Orthonormalize(double* r0,double* r1,double* r2,double one,double zero) {
  r0[0]=Fms(r2[2],r1[1],F(r2[1]*r1[2]));
  r0[1]=Fms(r1[2],r2[0],F(r2[2]*r1[0]));
  r0[2]=Fms(r2[1],r1[0],F(r1[1]*r2[0]));
  r1[0]=Fms(r2[1],r0[2],F(r0[1]*r2[2]));
  r1[1]=Fms(r0[0],r2[2],F(r2[0]*r0[2]));
  r1[2]=Fms(r2[0],r0[1],F(r2[1]*r0[0]));
  Normalize(r0,one,zero);
  Normalize(r1,one,zero);
  Normalize(r2,one,zero);
}
}  // namespace skeletal

// What one 821CE848 call did, for the hook's statistics.
struct NativeSkeletalEvalStats { uint32_t records=0,copies=0; bool blended=false; };

// M: guest memory, big-endian words. U8/U16/U32/U64(address), StoreU8/StoreU32/
// StoreU64(address,value); float helpers are built on U32/StoreU32.
template<class M>
inline double NativeSkeletalLoadFloat(const M& m,uint32_t address) { return double(std::bit_cast<float>(m.U32(address))); }
template<class M>
inline void NativeSkeletalStoreFloat(const M& m,uint32_t address,double value) { m.StoreU32(address,std::bit_cast<uint32_t>(float(value))); }

// 821CE5C8(slot,dest,a,b,frac,divisor): dest[k] = (a[k] + (b[k]-a[k])*frac) * (1/divisor), k=0..2.
template<class M>
inline void NativeSkeletalKeyLerp(const M& m,double* dest,uint32_t a,uint32_t b,double frac,double divisor,double one) {
  using namespace skeletal;
  const double reciprocal=F(one/divisor);
  for(uint32_t k=0;k<3;++k) {
    const double first=F(double(int64_t(int16_t(m.U16(a+k*2)))));
    const double second=F(double(int64_t(int16_t(m.U16(b+k*2)))));
    const double delta=F(second-first);
    dest[k]=F(Fma(delta,frac,first)*reciprocal);
  }
}

// sub_821CE848 (edf2017_recomp.74.cpp:8866), one record at a time in the
// original's statement order: the reference NativeSkeletalEvaluate is checked
// against (and the edf_native_skeletal_batched=false route).
template<class M>
NativeSkeletalEvalStats NativeSkeletalEvaluateScalar(const M& m,uint32_t slot) {
  using namespace skeletal;
  using S=NativeSkeletal;
  NativeSkeletalEvalStats stats;
  const uint32_t palette=m.U32(slot+S::slot_palette);
  if(!palette) return stats;
  const double one=NativeSkeletalLoadFloat(m,S::one_address),zero=NativeSkeletalLoadFloat(m,S::zero_address);
  const double time=NativeSkeletalLoadFloat(m,slot+S::slot_time),end=NativeSkeletalLoadFloat(m,slot+S::slot_end);
  uint32_t frame;
  double frac;
  if(time<end) {
    frame=Fctiwz(time);
    frac=F(time-F(double(int64_t(int32_t(frame)))));
  } else {
    frame=Fctiwz(time)-1u;
    frac=one;
  }
  const uint32_t next=frame+1u;
  // r1+80..+88: the keyed angles, zeroed once per call.
  double angles[3]={zero,zero,zero};
  const uint32_t records=m.U32(slot+S::slot_records);
  const uint32_t count=m.U32(slot+S::slot_record_count);
  const uint32_t records_end=records+count*S::record_size;
  for(uint32_t record=records;record!=records_end;record+=S::record_size) {
    ++stats.records;
    const uint32_t channel=m.U32(record+S::record_channel);
    const uint32_t stride=m.U32(channel+S::channel_stride);
    const uint32_t stream=m.U32(channel+S::channel_stream);
    uint32_t key0=((stride*frame)<<1)+stream+channel;
    uint32_t key1=((next*stride)<<1)+stream+channel;
    const auto key_address=[&](uint32_t key) { return palette+uint32_t(int32_t(int16_t(m.U16(key))))*6u; };
    if(m.U32(channel+S::channel_flags)&1u) {
      double translation[3];
      NativeSkeletalKeyLerp(m,translation,key_address(key0),key_address(key1),frac,
        NativeSkeletalLoadFloat(m,channel+S::channel_translation_divisor),one);
      for(uint32_t k=0;k<3;++k) NativeSkeletalStoreFloat(m,record+S::record_translation+k*4,translation[k]);
      key0+=2; key1+=2;
    }
    if(m.U32(channel+S::channel_flags)&8u)
      for(uint32_t k=0;k<3;++k) NativeSkeletalStoreFloat(m,record+S::record_translation+k*4,
        NativeSkeletalLoadFloat(m,channel+S::channel_translation+k*4));
    if(m.U32(channel+S::channel_flags)&2u) {
      NativeSkeletalKeyLerp(m,angles,key_address(key0),key_address(key1),frac,
        NativeSkeletalLoadFloat(m,channel+S::channel_rotation_divisor),one);
      Rows rows;
      RotationXYZ(rows,angles,one,zero);
      for(uint32_t k=0;k<12;++k) NativeSkeletalStoreFloat(m,record+S::record_rotation+k*4,rows[k]);
      key0+=2; key1+=2;
    }
    if(m.U32(channel+S::channel_flags)&16u) {
      Rows rows;
      const double constant[3]{NativeSkeletalLoadFloat(m,channel+S::channel_angles),
        NativeSkeletalLoadFloat(m,channel+S::channel_angles+4),NativeSkeletalLoadFloat(m,channel+S::channel_angles+8)};
      RotationXYZ(rows,constant,one,zero);
      for(uint32_t k=0;k<12;++k) NativeSkeletalStoreFloat(m,record+S::record_rotation+k*4,rows[k]);
    }
    if(m.U32(channel+S::channel_flags)&4u) {
      double scale[3];
      NativeSkeletalKeyLerp(m,scale,key_address(key0),key_address(key1),frac,
        NativeSkeletalLoadFloat(m,channel+S::channel_scale_divisor),one);
      for(uint32_t k=0;k<3;++k) NativeSkeletalStoreFloat(m,record+S::record_scale+k*4,scale[k]);
    }
    if(m.U32(channel+S::channel_flags)&32u)
      for(uint32_t k=0;k<3;++k) NativeSkeletalStoreFloat(m,record+S::record_scale+k*4,
        NativeSkeletalLoadFloat(m,channel+S::channel_scale+k*4));
  }
  if(m.U8(slot+S::slot_blending)) {
    stats.blended=true;
    double w=F(NativeSkeletalLoadFloat(m,slot+S::slot_blend_clock)/NativeSkeletalLoadFloat(m,slot+S::slot_blend_length));
    if(!(w<one)) { w=one; m.StoreU8(slot+S::slot_blending,0); }
    const double keep=F(one-w);
    for(uint32_t record=m.U32(slot+S::slot_records);record!=records_end;record+=S::record_size) {
      const uint32_t bone=m.U32(record+S::record_bone);
      for(uint32_t k=0;k<3;++k) {
        const double current=NativeSkeletalLoadFloat(m,record+S::record_translation+k*4);
        const double snapshot=NativeSkeletalLoadFloat(m,bone+S::bone_translation+k*4);
        NativeSkeletalStoreFloat(m,record+S::record_translation+k*4,F(F(F(snapshot-current)*keep)+current));
      }
      // 821C8480(rec+16, B+16, 1-w), then 821C84C8(rec+16).
      double rows[3][3];
      for(uint32_t r=0;r<3;++r) for(uint32_t c=0;c<3;++c) {
        const uint32_t offset=r*16+c*4;
        const double a=NativeSkeletalLoadFloat(m,record+S::record_rotation+offset);
        const double b=NativeSkeletalLoadFloat(m,bone+S::bone_rotation+offset);
        rows[r][c]=Fma(F(b-a),keep,a);
      }
      Orthonormalize(rows[0],rows[1],rows[2],one,zero);
      for(uint32_t r=0;r<3;++r) for(uint32_t c=0;c<3;++c)
        NativeSkeletalStoreFloat(m,record+S::record_rotation+r*16+c*4,rows[r][c]);
      for(uint32_t k=0;k<3;++k) {
        const double current=NativeSkeletalLoadFloat(m,record+S::record_scale+k*4);
        const double snapshot=NativeSkeletalLoadFloat(m,bone+S::bone_scale+k*4);
        NativeSkeletalStoreFloat(m,record+S::record_scale+k*4,F(current+F(F(snapshot-current)*keep)));
      }
    }
  }
  for(uint32_t record=m.U32(slot+S::slot_records);record!=records_end;record+=S::record_size) {
    const uint32_t bone=m.U32(record+S::record_bone);
    if(!m.U8(bone+S::bone_enabled)) continue;
    ++stats.copies;
    const uint32_t local=m.U32(bone+S::bone_local);
    for(uint32_t k=0;k<64;k+=8) m.StoreU64(local+k,m.U64(record+S::record_rotation+k));
    for(uint32_t row=0;row<3;++row) {
      const double scale=NativeSkeletalLoadFloat(m,record+S::record_scale+row*4);
      for(uint32_t k=0;k<3;++k) {
        const uint32_t at=local+row*16+k*4;
        NativeSkeletalStoreFloat(m,at,F(NativeSkeletalLoadFloat(m,at)*scale));
      }
    }
  }
  return stats;
}

// ---- The batched evaluation. ----
// A memo of the constant rotations (channel flag 16). RotationXYZ's rows are a
// pure function of the three angles' single bits, given the image constants one
// (1.0f) and zero (+0.0f) and the MXCSR rounding and flush bits (round to nearest,
// FTZ and DAZ off): the evaluation uses the memo only under exactly those, the
// state every entry was computed in, so a hit stores the bits a computation would.
// Direct mapped by the source channel's address (the same clip data serves every
// object of a kind); the channel only picks the slot and the angle bits decide a
// hit. Entries hold the guest bytes of record+16..+63.
struct NativeSkeletalRotationCache {
  struct Entry { uint32_t channel=0,angles[3]{}; alignas(16) uint8_t rows[48]{}; };
  static constexpr uint32_t bits=10,size=1u<<bits;
  Entry entries[size];
  uint64_t hits=0,misses=0;
  static uint32_t Slot(uint32_t channel) { return (channel*0x9E3779B1u)>>(32-bits); }
  static bool Usable(uint32_t one_bits,uint32_t zero_bits,uint32_t csr) {
    return one_bits==0x3F800000u && zero_bits==0 && !(csr&0xE040u);
  }
};

// 16 guest bytes in memory order at any address (M::Row/M::StoreRow take any
// address here); a row straddling 0xE0000000 (the host view jumps there) goes
// byte by byte.
template<class M>
inline __m128i NativeSkeletalLoadBytes(const M& m,uint32_t address) {
  if(address-0xDFFFFFF1u<15u) {
    alignas(16) uint8_t bytes[16];
    for(uint32_t i=0;i<16;++i) bytes[i]=m.U8(address+i);
    return _mm_load_si128(reinterpret_cast<const __m128i*>(bytes));
  }
  return _mm_loadu_si128(reinterpret_cast<const __m128i*>(m.Row(address)));
}
template<class M>
inline void NativeSkeletalStoreBytes(const M& m,uint32_t address,__m128i value) {
  if(address-0xDFFFFFF1u<15u) {
    alignas(16) uint8_t bytes[16];
    _mm_store_si128(reinterpret_cast<__m128i*>(bytes),value);
    for(uint32_t i=0;i<16;++i) m.StoreU8(address+i,bytes[i]);
    return;
  }
  m.StoreRow(address,value);
}
// A guest structure [address, address+size): through one host pointer when M
// offers it (M::Host: null for a range crossing 0xE0000000), else access by
// access through M. The same bytes either way.
template<class M>
struct NativeSkeletalBytes {
  const M& m;
  uint32_t address;
  uint8_t* host=nullptr;
  NativeSkeletalBytes(const M& memory,uint32_t at,uint32_t size):m(memory),address(at) {
    if constexpr(requires { memory.Host(at,size); }) host=memory.Host(at,size);
  }
  uint32_t U32(uint32_t offset) const {
    if(!host) return m.U32(address+offset);
    uint32_t v; std::memcpy(&v,host+offset,4); return std::byteswap(v);
  }
  uint8_t U8(uint32_t offset) const { return host?host[offset]:m.U8(address+offset); }
  __m128i Load(uint32_t offset) const {
    return host?_mm_loadu_si128(reinterpret_cast<const __m128i*>(host+offset)):NativeSkeletalLoadBytes(m,address+offset);
  }
  void Store(uint32_t offset,__m128i value) const {
    if(host) _mm_storeu_si128(reinterpret_cast<__m128i*>(host+offset),value);
    else NativeSkeletalStoreBytes(m,address+offset,value);
  }
};
// A cache hint for the line holding a guest address, when M offers one
// (M::Prefetch; never faults, no effect on memory).
template<class M>
inline void NativeSkeletalPrefetch(const M& m,uint32_t address) {
  if constexpr(requires { m.Prefetch(address); }) m.Prefetch(address);
}
// Big-endian words <-> host floats, four at a time.
inline __m128i NativeSkeletalSwapWords(__m128i v) {
  return _mm_shuffle_epi8(v,_mm_setr_epi8(3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12));
}

// Up to 64 rotations the record loop queued: keyed (flag 2: the two int16
// palette triples, the divisor) or constant (flag 16 whose memo missed: the
// angles), built four records at a time.
struct NativeSkeletalRotationBatch {
  static constexpr uint32_t capacity=64;
  alignas(32) int32_t first[3][capacity],second[3][capacity];
  alignas(32) double divisor[capacity],constant[3][capacity];
  alignas(32) int64_t is_constant[capacity];
  uint32_t record[capacity];
  uint8_t* record_host[capacity];   // NativeSkeletalBytes::host of the record
  NativeSkeletalRotationCache::Entry* fill[capacity];
  uint32_t count=0;
};

template<class M>
void NativeSkeletalFlushRotations(const M& m,NativeSkeletalRotationBatch& batch,double frac,double one,double zero) {
  using S=NativeSkeletal;
  const uint32_t count=batch.count;
  if(!count) return;
  // Lanes past the end: harmless inputs, never stored.
  for(uint32_t j=count;j<((count+3u)&~3u);++j) {
    for(uint32_t k=0;k<3;++k) { batch.first[k][j]=batch.second[k][j]=0; batch.constant[k][j]=zero; }
    batch.divisor[j]=one; batch.is_constant[j]=-1; batch.record[j]=0; batch.record_host[j]=nullptr; batch.fill[j]=nullptr;
  }
  const __m256d vfrac=_mm256_set1_pd(frac),vone=_mm256_set1_pd(one),vzero=_mm256_set1_pd(zero);
  for(uint32_t j=0;j<count;j+=4) {
    const __m256d reciprocal=skeletal::lanes::F(_mm256_div_pd(vone,_mm256_load_pd(batch.divisor+j)));
    const __m256d constant=_mm256_castsi256_pd(_mm256_load_si256(reinterpret_cast<const __m256i*>(batch.is_constant+j)));
    __m256d angle[3];
    for(uint32_t k=0;k<3;++k) {
      const __m256d keyed=skeletal::lanes::KeyLerp(_mm_load_si128(reinterpret_cast<const __m128i*>(batch.first[k]+j)),
        _mm_load_si128(reinterpret_cast<const __m128i*>(batch.second[k]+j)),vfrac,reciprocal);
      angle[k]=_mm256_blendv_pd(keyed,_mm256_load_pd(batch.constant[k]+j),constant);
    }
    __m256d rows[12];
    skeletal::lanes::RotationXYZ(rows,angle[0],angle[1],angle[2],vone,vzero);
    // Element-major (4 records per vector) to record-major rows of 4 floats.
    __m128 r[12];
    for(uint32_t e=0;e<12;++e) r[e]=_mm256_cvtpd_ps(rows[e]);
    for(uint32_t row=0;row<3;++row) _MM_TRANSPOSE4_PS(r[row*4],r[row*4+1],r[row*4+2],r[row*4+3]);
    const uint32_t lanes_used=count-j<4u?count-j:4u;
    for(uint32_t lane=0;lane<lanes_used;++lane) {
      const uint32_t record=batch.record[j+lane];
      uint8_t* const host=batch.record_host[j+lane];
      NativeSkeletalRotationCache::Entry* fill=batch.fill[j+lane];
      for(uint32_t row=0;row<3;++row) {
        const __m128i bytes=NativeSkeletalSwapWords(_mm_castps_si128(r[row*4+lane]));
        if(host) _mm_storeu_si128(reinterpret_cast<__m128i*>(host+S::record_rotation+row*16),bytes);
        else NativeSkeletalStoreBytes(m,record+S::record_rotation+row*16,bytes);
        if(fill) _mm_store_si128(reinterpret_cast<__m128i*>(fill->rows+row*16),bytes);
      }
    }
  }
  batch.count=0;
}

// sub_821CE848 (edf2017_recomp.74.cpp:8866), the same results as
// NativeSkeletalEvaluateScalar with the work reordered where nothing can observe
// it: each record's rotation is built later than its translation and scale (the
// three write disjoint parts of the record, and nothing in the loop reads a
// record), four records' rotations at a time (lanes), a constant rotation comes
// from the memo when it holds, and a group the original writes twice (flags 1
// and 8, 2 and 16, 4 and 32) is written once with the second value. Records
// never overlap the clip data they read (a record is heap, a clip is the
// model's file image). The blend and the final copy to the node locals run
// afterwards in record order as before (the copy with 16-byte rows).
template<class M>
NativeSkeletalEvalStats NativeSkeletalEvaluate(const M& m,uint32_t slot,NativeSkeletalRotationCache* cache=nullptr) {
  using namespace skeletal;
  using S=NativeSkeletal;
  NativeSkeletalEvalStats stats;
  const uint32_t palette=m.U32(slot+S::slot_palette);
  if(!palette) return stats;
  const uint32_t one_bits=m.U32(S::one_address),zero_bits=m.U32(S::zero_address);
  const double one=double(std::bit_cast<float>(one_bits)),zero=double(std::bit_cast<float>(zero_bits));
  if(cache && !NativeSkeletalRotationCache::Usable(one_bits,zero_bits,_mm_getcsr())) cache=nullptr;
  const double time=NativeSkeletalLoadFloat(m,slot+S::slot_time),end=NativeSkeletalLoadFloat(m,slot+S::slot_end);
  uint32_t frame;
  double frac;
  if(time<end) {
    frame=Fctiwz(time);
    frac=F(time-F(double(int64_t(int32_t(frame)))));
  } else {
    frame=Fctiwz(time)-1u;
    frac=one;
  }
  const uint32_t next=frame+1u;
  const uint32_t records=m.U32(slot+S::slot_records);
  const uint32_t count=m.U32(slot+S::slot_record_count);
  const uint32_t records_end=records+count*S::record_size;
  const auto key_address=[&](uint32_t key) { return palette+uint32_t(int32_t(int16_t(m.U16(key))))*6u; };
  // A translation or scale (3 words) goes into its 16-byte row with the row's
  // fourth word written back as it is, so the copy's 16-byte loads below read
  // one 16-byte store (store forwarding). Singles in, singles out, through
  // double as lfs/stfs are (a signalling NaN comes out quiet, as there).
  const auto store3=[&](const NativeSkeletalBytes<M>& rec,uint32_t offset,__m128 values) {
    const __m128i row=rec.Load(offset);
    const __m128i bytes=NativeSkeletalSwapWords(_mm_castps_si128(_mm256_cvtpd_ps(_mm256_cvtps_pd(values))));
    rec.Store(offset,_mm_blend_epi32(bytes,row,0x8));
  };
  const auto lerp3=[&](const NativeSkeletalBytes<M>& rec,uint32_t offset,uint32_t a,uint32_t b,double divisor) {
    double v[3];
    NativeSkeletalKeyLerp(m,v,a,b,frac,divisor,one);
    store3(rec,offset,_mm_setr_ps(float(v[0]),float(v[1]),float(v[2]),0.0f));
  };
  stats.records=count;
  NativeSkeletalRotationBatch batch;
  for(uint32_t record=records;record!=records_end;record+=S::record_size) {
    const NativeSkeletalBytes<M> rec(m,record,S::record_size);
    // The copy at the end reads each record's bone struct and then its node
    // local, lines a step old: ask for the bone now and, eight records on (its
    // line in by then), for the local.
    NativeSkeletalPrefetch(m,rec.U32(S::record_bone));
    if(record-records>=8*S::record_size) {
      const uint32_t earlier_bone=m.U32(record-8*S::record_size+S::record_bone);
      const uint32_t local=m.U32(earlier_bone+S::bone_local);
      NativeSkeletalPrefetch(m,local); NativeSkeletalPrefetch(m,local+63);
    }
    const uint32_t channel=rec.U32(S::record_channel);
    // The 64-byte source channel as host words: c0 = +0..+12 (name, flags,
    // translation and rotation divisors), c1 = +16..+28 (scale divisor, stream,
    // stride, translation x), c2 = +32..+44, c3 = +48..+60.
    const NativeSkeletalBytes<M> source(m,channel,64);
    const __m128i c0=NativeSkeletalSwapWords(source.Load(0));
    const __m128i c1=NativeSkeletalSwapWords(source.Load(16));
    const __m128i c2=NativeSkeletalSwapWords(source.Load(32));
    const __m128i c3=NativeSkeletalSwapWords(source.Load(48));
    const uint32_t flags=uint32_t(_mm_extract_epi32(c0,1));
    const uint32_t stream=uint32_t(_mm_extract_epi32(c1,1));
    const uint32_t stride=uint32_t(_mm_extract_epi32(c1,2));
    const auto word_float=[](__m128i v,int lane) { return double(std::bit_cast<float>(uint32_t(
      lane==0?_mm_cvtsi128_si32(v):lane==1?_mm_extract_epi32(v,1):lane==2?_mm_extract_epi32(v,2):_mm_extract_epi32(v,3)))); };
    uint32_t key0=((stride*frame)<<1)+stream+channel;
    uint32_t key1=((next*stride)<<1)+stream+channel;
    if(flags&8u) {
      store3(rec,S::record_translation,_mm_castsi128_ps(_mm_alignr_epi8(c2,c1,12)));   // +28..+36
    } else if(flags&1u) {
      lerp3(rec,S::record_translation,key_address(key0),key_address(key1),word_float(c0,2));
    }
    if(flags&1u) { key0+=2; key1+=2; }
    if(flags&16u) {
      const __m128i angles=_mm_alignr_epi8(c3,c2,8);   // +40..+48
      const uint32_t a=uint32_t(_mm_cvtsi128_si32(angles)),b=uint32_t(_mm_extract_epi32(angles,1)),
        c=uint32_t(_mm_extract_epi32(angles,2));
      NativeSkeletalRotationCache::Entry* fill=nullptr;
      if(cache) {
        auto& entry=cache->entries[NativeSkeletalRotationCache::Slot(channel)];
        if(entry.channel==channel && entry.angles[0]==a && entry.angles[1]==b && entry.angles[2]==c) {
          ++cache->hits;
          for(uint32_t row=0;row<3;++row)
            rec.Store(S::record_rotation+row*16,_mm_load_si128(reinterpret_cast<const __m128i*>(entry.rows+row*16)));
        } else {
          ++cache->misses;
          // Untagged until the rows are in (the batch is built right below).
          entry.channel=0; entry.angles[0]=a; entry.angles[1]=b; entry.angles[2]=c;
          fill=&entry;
        }
      }
      if(!cache || fill) {
        const uint32_t j=batch.count++;
        batch.record[j]=record; batch.record_host[j]=rec.host; batch.fill[j]=fill; batch.is_constant[j]=-1;
        batch.constant[0][j]=double(std::bit_cast<float>(a));
        batch.constant[1][j]=double(std::bit_cast<float>(b));
        batch.constant[2][j]=double(std::bit_cast<float>(c));
        batch.divisor[j]=one;
        for(uint32_t k=0;k<3;++k) batch.first[k][j]=batch.second[k][j]=0;
        if(fill) { NativeSkeletalFlushRotations(m,batch,frac,one,zero); fill->channel=channel; }
      }
    } else if(flags&2u) {
      const uint32_t j=batch.count++;
      batch.record[j]=record; batch.record_host[j]=rec.host; batch.fill[j]=nullptr; batch.is_constant[j]=0;
      const uint32_t a=key_address(key0),b=key_address(key1);
      for(uint32_t k=0;k<3;++k) {
        batch.first[k][j]=int16_t(m.U16(a+k*2));
        batch.second[k][j]=int16_t(m.U16(b+k*2));
      }
      batch.divisor[j]=word_float(c0,3);
      for(uint32_t k=0;k<3;++k) batch.constant[k][j]=zero;
    }
    if(flags&2u) { key0+=2; key1+=2; }
    if(flags&32u) {
      store3(rec,S::record_scale,_mm_castsi128_ps(_mm_srli_si128(c3,4)));   // +52..+60
    } else if(flags&4u) {
      lerp3(rec,S::record_scale,key_address(key0),key_address(key1),word_float(c1,0));
    }
    if(batch.count==NativeSkeletalRotationBatch::capacity) NativeSkeletalFlushRotations(m,batch,frac,one,zero);
  }
  NativeSkeletalFlushRotations(m,batch,frac,one,zero);
  if(m.U8(slot+S::slot_blending)) {
    stats.blended=true;
    double w=F(NativeSkeletalLoadFloat(m,slot+S::slot_blend_clock)/NativeSkeletalLoadFloat(m,slot+S::slot_blend_length));
    if(!(w<one)) { w=one; m.StoreU8(slot+S::slot_blending,0); }
    const double keep=F(one-w);
    // Four records at a time, one per lane, each with the scalar blend's
    // operations (the records are disjoint and the snapshots only read). Rows
    // are loaded and stored whole: the words the blend does not write (each
    // row's fourth) go back as they were.
    using namespace skeletal::lanes;
    const __m256d vkeep=_mm256_set1_pd(keep),vone=_mm256_set1_pd(one),vzero=_mm256_set1_pd(zero);
    const auto wide=[](__m128 v) { return _mm256_cvtps_pd(v); };
    for(uint32_t first=records;first!=records_end;) {
      uint32_t record[4],bone[4],n=0;
      for(;n<4 && first!=records_end;++n,first+=S::record_size) { record[n]=first; bone[n]=m.U32(first+S::record_bone); }
      for(uint32_t lane=n;lane<4;++lane) { record[lane]=record[0]; bone[lane]=bone[0]; }
      // Element-major: v[k] holds element k of the four records' rows.
      const auto load=[&](const uint32_t* base,uint32_t offset,__m128* v) {
        for(uint32_t lane=0;lane<4;++lane)
          v[lane]=_mm_castsi128_ps(NativeSkeletalSwapWords(NativeSkeletalLoadBytes(m,base[lane]+offset)));
        _MM_TRANSPOSE4_PS(v[0],v[1],v[2],v[3]);
      };
      const auto store=[&](uint32_t offset,__m128* v) {
        _MM_TRANSPOSE4_PS(v[0],v[1],v[2],v[3]);
        for(uint32_t lane=0;lane<n;++lane)
          NativeSkeletalStoreBytes(m,record[lane]+offset,NativeSkeletalSwapWords(_mm_castps_si128(v[lane])));
      };
      __m128 t[4],snap_t[4];
      load(record,S::record_translation,t); load(bone,S::bone_translation,snap_t);
      for(uint32_t k=0;k<3;++k) {
        const __m256d current=wide(t[k]),snapshot=wide(snap_t[k]);
        t[k]=_mm256_cvtpd_ps(F(_mm256_add_pd(F(_mm256_mul_pd(F(_mm256_sub_pd(snapshot,current)),vkeep)),current)));
      }
      // 821C8480(rec+16, B+16, 1-w), then 821C84C8(rec+16).
      __m128 r[3][4],snap_r[3][4];
      __m256d rows[3][3];
      for(uint32_t row=0;row<3;++row) {
        load(record,S::record_rotation+row*16,r[row]); load(bone,S::bone_rotation+row*16,snap_r[row]);
        for(uint32_t c=0;c<3;++c) {
          const __m256d a=wide(r[row][c]),b=wide(snap_r[row][c]);
          rows[row][c]=Fma(F(_mm256_sub_pd(b,a)),vkeep,a);
        }
      }
      Orthonormalize(rows[0],rows[1],rows[2],vone,vzero);
      for(uint32_t row=0;row<3;++row) for(uint32_t c=0;c<3;++c) r[row][c]=_mm256_cvtpd_ps(rows[row][c]);
      __m128 s[4],snap_s[4];
      load(record,S::record_scale,s); load(bone,S::bone_scale,snap_s);
      for(uint32_t k=0;k<3;++k) {
        const __m256d current=wide(s[k]),snapshot=wide(snap_s[k]);
        s[k]=_mm256_cvtpd_ps(F(_mm256_add_pd(current,F(_mm256_mul_pd(F(_mm256_sub_pd(snapshot,current)),vkeep)))));
      }
      store(S::record_translation,t);
      for(uint32_t row=0;row<3;++row) store(S::record_rotation+row*16,r[row]);
      store(S::record_scale,s);
    }
  }
  // The copy: rows 0-2 of the rotation times the scale (elements 0-2; a float
  // multiply is the rounding of the exact product, as F(x*s) of two singles),
  // their w words and row 3 (translation, +76) as they are.
  uint32_t copies=0;
  for(uint32_t record=records;record!=records_end;record+=S::record_size) {
    const NativeSkeletalBytes<M> rec(m,record,S::record_size);
    const NativeSkeletalBytes<M> bone(m,rec.U32(S::record_bone),8);
    if(!bone.U8(S::bone_enabled)) continue;
    ++copies;
    const NativeSkeletalBytes<M> local(m,bone.U32(S::bone_local),64);
    const __m128 scale=_mm_castsi128_ps(NativeSkeletalSwapWords(rec.Load(S::record_scale)));
    const __m128i row3=rec.Load(S::record_translation);
    __m128i out[3];
    for(uint32_t row=0;row<3;++row) {
      const __m128 v=_mm_castsi128_ps(NativeSkeletalSwapWords(rec.Load(S::record_rotation+row*16)));
      const __m128 s=row==0?_mm_shuffle_ps(scale,scale,0x00):row==1?_mm_shuffle_ps(scale,scale,0x55):_mm_shuffle_ps(scale,scale,0xAA);
      out[row]=NativeSkeletalSwapWords(_mm_castps_si128(_mm_blend_ps(_mm_mul_ps(v,s),v,0x8)));
    }
    for(uint32_t row=0;row<3;++row) local.Store(row*16,out[row]);
    local.Store(48,row3);
  }
  stats.copies=copies;
  return stats;
}

// sub_821C8198 (edf2017_recomp.79.cpp:8666): out = a x b with the recompiled
// body's own vector operations (the same byte-reversed lanes, unpacks and DPPS),
// so the guest flush mode must already be on. lvx128 reads the 16-byte aligned
// rows below each address; stvlx writes 16-(out&15) bytes of each row.
// M::Row(address) returns the 16 guest bytes at a 16-byte aligned address and
// M::StoreRow(address,bytes) stores 16 bytes there (memory order).
template<class M>
// With stack!=0 (the caller's r1) it also makes the original's guest stack
// writes, in its order: r3 (out) at r1+20 between the first two row loads and
// the rest, rows 1-3 staged at r1-48/-32/-16 (16-byte aligned) and read back
// from there before their stores, so it is the whole memory effect of a call.
inline void NativeSkeletalMultiply(const M& m,uint32_t out,uint32_t a,uint32_t b,uint32_t stack=0) {
  const __m128i reverse=_mm_setr_epi8(15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0);
  const auto load=[&](uint32_t address) {
    return _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(m.Row(address&~0xFu))),reverse);
  };
  const auto hi=[](__m128i x,__m128i y) { return _mm_unpackhi_epi32(x,y); };
  const auto lo=[](__m128i x,__m128i y) { return _mm_unpacklo_epi32(x,y); };
  const auto dp=[](__m128i x,__m128i y) {
    return _mm_castps_si128(_mm_dp_ps(_mm_castsi128_ps(x),_mm_castsi128_ps(y),0xFF));
  };
  __m128i v10=load(b),v0=load(a);
  if(stack) m.StoreU32(stack+20,out);   // stw r3,20(r1)
  __m128i v9=load(b+16),v7=load(b+48),v8=load(b+32);
  __m128i v5=hi(v7,v9);             // vmrghw v5,v9,v7
  __m128i v6=hi(v8,v10);            // vmrghw v6,v10,v8
  __m128i v13=load(a+16);
  __m128i v4=lo(v8,v10);            // vmrglw v4,v10,v8
  __m128i v12=load(a+32);
  v7=lo(v7,v9);                     // vmrglw v7,v9,v7
  __m128i v11=load(a+48);
  v9=lo(v5,v6);                     // vmrglw v9,v6,v5
  v10=hi(v5,v6);                    // vmrghw v10,v6,v5
  v8=hi(v7,v4);                     // vmrghw v8,v4,v7
  v7=lo(v7,v4);                     // vmrglw v7,v4,v7
  const __m128i v1=dp(v13,v9);
  v5=dp(v13,v10);
  const __m128i v2=dp(v13,v8);
  v13=dp(v13,v7);
  v6=dp(v0,v10);
  const __m128i v3=dp(v0,v8);
  const __m128i v31=dp(v12,v10),v30=dp(v12,v8),v29=dp(v12,v9);
  v12=dp(v12,v7);
  v4=dp(v0,v9);
  v0=dp(v0,v7);
  const __m128i v28=dp(v11,v7);
  v10=dp(v11,v10);
  v8=dp(v11,v8);
  v9=dp(v11,v9);
  v7=hi(v2,v5);                     // vmrghw v7,v5,v2
  v13=hi(v13,v1);                   // vmrghw v13,v1,v13
  v11=hi(v3,v6);                    // vmrghw v11,v6,v3
  v13=hi(v13,v7);                   // vmrghw v13,v7,v13
  v6=hi(v30,v31);                   // vmrghw v6,v31,v30
  v12=hi(v12,v29);                  // vmrghw v12,v29,v12
  v0=hi(v0,v4);                     // vmrghw v0,v4,v0
  const __m128i row1=v13;           // stvx v13 -> r1-48
  v13=hi(v12,v6);                   // vmrghw v13,v6,v12
  v0=hi(v0,v11);                    // vmrghw v0,v11,v0
  v10=hi(v8,v10);                   // vmrghw v10,v10,v8
  v11=hi(v28,v9);                   // vmrghw v11,v9,v28
  const __m128i row2=v13;           // stvx v13 -> r1-32
  v13=hi(v11,v10);                  // vmrghw v13,v10,v11
  const __m128i row3=v13;           // stvx v13 -> r1-16
  if(stack) {
    const uint32_t staged[3]{(stack-48)&~0xFu,(stack-32)&~0xFu,(stack-16)&~0xFu};
    const __m128i values[3]{row1,row2,row3};
    for(uint32_t k=0;k<3;++k) m.StoreRow(staged[k],_mm_shuffle_epi8(values[k],reverse));
    if(!(out&0xFu)) {
      // Whole rows: stvlx of a reloaded staged row writes its bytes as they are.
      m.StoreRow(out,_mm_shuffle_epi8(v0,reverse));
      for(uint32_t k=0;k<3;++k)
        m.StoreRow(out+16*(k+1),_mm_loadu_si128(reinterpret_cast<const __m128i*>(m.Row(staged[k]))));
      return;
    }
    const uint32_t bytes=16u-(out&0xFu);
    const auto store=[&](uint32_t at,__m128i value) {
      alignas(16) uint8_t lanes[16];
      _mm_store_si128(reinterpret_cast<__m128i*>(lanes),value);
      for(uint32_t i=0;i<bytes;++i) m.StoreU8(at+i,lanes[15-i]);
    };
    store(out,v0);
    for(uint32_t k=0;k<3;++k) store(out+16*(k+1),load(staged[k]));
    return;
  }
  const __m128i rows[4]{v0,row1,row2,row3};
  if(!(out&0xFu)) {
    // Aligned (every node world): all 16 bytes of each row, guest order.
    for(uint32_t row=0;row<4;++row) m.StoreRow(out+row*16,_mm_shuffle_epi8(rows[row],reverse));
    return;
  }
  const uint32_t bytes=16u-(out&0xFu);
  for(uint32_t row=0;row<4;++row) {
    alignas(16) uint8_t lanes[16];
    _mm_store_si128(reinterpret_cast<__m128i*>(lanes),rows[row]);
    const uint32_t at=out+row*16;
    for(uint32_t i=0;i<bytes;++i) m.StoreU8(at+i,lanes[15-i]);
  }
}

// NativeSkeletalMultiply without the stack writes, for a 16-byte aligned out:
// each output element is the DPPS 0xFF sum over its row of a and column of b,
// ((l0+l1)+(l2+l3)) with lane l the guest element 3-l, so
// (a3*b3c + a2*b2c) + (a1*b1c + a0*b0c): the same single-precision multiplies
// and adds in the same order under the same MXCSR, done for the four columns at
// once with mulps/addps instead of sixteen DPPS and their unpacks.
template<class M>
inline void NativeSkeletalMultiplyRows(const M& m,uint32_t out,uint32_t a,uint32_t b) {
  const auto load=[&](uint32_t address) {   // lvx128: the aligned row, as host floats in guest order
    return _mm_castsi128_ps(NativeSkeletalSwapWords(_mm_loadu_si128(reinterpret_cast<const __m128i*>(m.Row(address&~0xFu)))));
  };
  const __m128 b0=load(b),b1=load(b+16),b2=load(b+32),b3=load(b+48);
  __m128 rows[4];
  for(uint32_t row=0;row<4;++row) {
    const __m128 x=load(a+row*16);
    const __m128 t0=_mm_mul_ps(_mm_shuffle_ps(x,x,0x00),b0),t1=_mm_mul_ps(_mm_shuffle_ps(x,x,0x55),b1);
    const __m128 t2=_mm_mul_ps(_mm_shuffle_ps(x,x,0xAA),b2),t3=_mm_mul_ps(_mm_shuffle_ps(x,x,0xFF),b3);
    rows[row]=_mm_add_ps(_mm_add_ps(t3,t2),_mm_add_ps(t1,t0));
  }
  for(uint32_t row=0;row<4;++row) m.StoreRow(out+row*16,NativeSkeletalSwapWords(_mm_castps_si128(rows[row])));
}

// sub_821D1688 (edf2017_recomp.52.cpp:8930): the node's world, then its children's.
// Returns the nodes written.
template<class M>
uint32_t NativeSkeletalPropagate(const M& m,uint32_t node,uint32_t parent) {
  using S=NativeSkeletal;
  if(!((node+S::node_world)&0xFu)) NativeSkeletalMultiplyRows(m,node+S::node_world,node+S::node_local,parent);
  else NativeSkeletalMultiply(m,node+S::node_world,node+S::node_local,parent);
  uint32_t nodes=1;
  const uint32_t count=m.U32(node+S::node_child_count);
  for(uint32_t i=0,offset=0;i<count;++i,offset+=S::node_size)
    nodes+=NativeSkeletalPropagate(m,m.U32(node+S::node_children)+offset,node+S::node_world);
  return nodes;
}
}  // namespace edf::native
