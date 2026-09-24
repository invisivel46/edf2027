// native_skeletal.h against literal transcriptions of the recompiled bodies
// (821CE848 with 821CE5C8, 821C7B20, 821C7D80, 821C7E30, 821C8480, 821C84C8,
// 821B0320; 821D1688 with 821C8198), statement for statement in guest register
// form, over synthetic big-endian guest memory. The transcriptions write the
// guest stack scratch the originals write (in a stack area the comparison
// skips); everything else must match bit for bit, a NaN matching any NaN.
// 821CE848 is checked three ways: one record at a time
// (NativeSkeletalEvaluateScalar), batched (NativeSkeletalEvaluate, four records'
// rotations per AVX2 lane group) and batched with the constant-rotation memo,
// filled and then hit.
#include "native_graphics/native_skeletal.h"
#include <immintrin.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

using namespace edf::native;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }

constexpr uint32_t kImage=0x82000000u;  // one page mapped at the image base for the two constants
struct Memory {
  std::vector<uint8_t>& heap;    // [0, heap.size())
  std::vector<uint8_t>& image;   // [kImage, kImage+image.size())
  uint8_t* At(uint32_t address,uint32_t size) const {
    if(address>=kImage && address-kImage+size<=image.size()) return image.data()+(address-kImage);
    if(address+uint64_t(size)<=heap.size()) return heap.data()+address;
    throw std::runtime_error("guest address out of range");
  }
  uint8_t U8(uint32_t a) const { return *At(a,1); }
  uint16_t U16(uint32_t a) const { const auto* p=At(a,2); return uint16_t(p[0]<<8|p[1]); }
  uint32_t U32(uint32_t a) const { const auto* p=At(a,4); return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
  uint64_t U64(uint32_t a) const { return uint64_t(U32(a))<<32|U32(a+4); }
  void StoreU8(uint32_t a,uint8_t v) const { *At(a,1)=v; }
  void StoreU16(uint32_t a,uint16_t v) const { auto* p=At(a,2); p[0]=uint8_t(v>>8); p[1]=uint8_t(v); }
  void StoreU32(uint32_t a,uint32_t v) const { auto* p=At(a,4); for(int i=0;i<4;++i) p[i]=uint8_t(v>>(24-8*i)); }
  void StoreU64(uint32_t a,uint64_t v) const { StoreU32(a,uint32_t(v>>32)); StoreU32(a+4,uint32_t(v)); }
  const uint8_t* Row(uint32_t a) const { return At(a,16); }
  void StoreRow(uint32_t a,__m128i bytes) const { _mm_storeu_si128(reinterpret_cast<__m128i*>(At(a,16)),bytes); }
  // A structure's bytes through one pointer (NativeSkeletalBytes).
  uint8_t* Host(uint32_t a,uint32_t size) const { return At(a,size); }
};
// The same memory without Host: NativeSkeletalBytes goes access by access.
struct PlainMemory {
  const Memory& m;
  uint8_t U8(uint32_t a) const { return m.U8(a); }
  uint16_t U16(uint32_t a) const { return m.U16(a); }
  uint32_t U32(uint32_t a) const { return m.U32(a); }
  uint64_t U64(uint32_t a) const { return m.U64(a); }
  void StoreU8(uint32_t a,uint8_t v) const { m.StoreU8(a,v); }
  void StoreU32(uint32_t a,uint32_t v) const { m.StoreU32(a,v); }
  void StoreU64(uint32_t a,uint64_t v) const { m.StoreU64(a,v); }
  const uint8_t* Row(uint32_t a) const { return m.Row(a); }
  void StoreRow(uint32_t a,__m128i bytes) const { m.StoreRow(a,bytes); }
};

// ---- Guest register form. ----
double F(double x) { return double(float(x)); }
double Lfs(const Memory& m,uint32_t a) { return double(std::bit_cast<float>(m.U32(a))); }
void Stfs(const Memory& m,uint32_t a,double v) { m.StoreU32(a,std::bit_cast<uint32_t>(float(v))); }
double Lfd(const Memory& m,uint32_t a) { return std::bit_cast<double>(m.U64(a)); }
uint64_t Rot1(uint64_t r) { return uint64_t(uint32_t(r)<<1); }  // rlwinm rX,rY,1,0,30 on the low word

// sub_821CE5C8 (edf2017_recomp.17.cpp:8929). r1 is the caller's stack pointer.
void Guest821CE5C8(const Memory& m,uint32_t r1,uint32_t r4,uint32_t r5,uint32_t r6,double f1,double f2) {
  double f0=Lfs(m,0x820008CC);
  f0=F(f0/f2);
  for(uint32_t k=0;k<3;++k) {
    const int64_t a=int16_t(m.U16(r5+k*2)),b=int16_t(m.U16(r6+k*2));
    m.StoreU64(r1-16,uint64_t(a)); m.StoreU64(r1-8,uint64_t(b));
    double f13=double(std::bit_cast<int64_t>(m.U64(r1-16)));
    double f12=double(std::bit_cast<int64_t>(m.U64(r1-8)));
    f13=F(f13); f12=F(f12);
    f12=F(f12-f13);
    f13=F(std::fma(f12,f1,f13));
    f13=F(f13*f0);
    Stfs(m,r4+k*4,f13);
  }
}
// sub_821C7B20 (edf2017_recomp.55.cpp:8684).
void Guest821C7B20(const Memory& m,uint32_t r3,double f1) {
  const double f31=F(NativeGuestSin(f1));
  const double f13=F(NativeGuestCos(f1));
  const double f12=std::bit_cast<double>(std::bit_cast<uint64_t>(f31)^0x8000000000000000ull);
  double f0=Lfs(m,0x820008CC);
  Stfs(m,r3+0,f0);
  f0=Lfs(m,0x820009A4);
  for(uint32_t o:{4u,8u,12u,16u}) Stfs(m,r3+o,f0);
  Stfs(m,r3+20,f13); Stfs(m,r3+24,f31); Stfs(m,r3+28,f0); Stfs(m,r3+32,f0);
  Stfs(m,r3+36,f12); Stfs(m,r3+40,f13); Stfs(m,r3+44,f0);
}
// sub_821C7D80 (edf2017_recomp.50.cpp:8904).
void Guest821C7D80(const Memory& m,uint32_t r31,double f1) {
  const double f31=F(NativeGuestSin(f1));
  const double c=NativeGuestCos(f1);
  double f13=Lfs(m,r31+0);
  double f0=F(c);
  double f12=Lfs(m,r31+8);
  double f11=F(f13*f31);
  double f10=F(f12*f31);
  f12=F(std::fma(f12,f0,-f11)); Stfs(m,r31+8,f12);
  f13=F(std::fma(f13,f0,f10)); Stfs(m,r31+0,f13);
  f13=Lfs(m,r31+16);
  f11=F(f13*f0);
  f12=Lfs(m,r31+24);
  f10=F(f13*f31);
  f13=F(std::fma(f12,f31,f11));
  f12=F(std::fma(f12,f0,-f10));
  Stfs(m,r31+24,f12); Stfs(m,r31+16,f13);
  f13=Lfs(m,r31+32);
  f10=F(f13*f31);
  f12=Lfs(m,r31+40);
  f11=F(f13*f0);
  f0=F(std::fma(f12,f0,-f10)); Stfs(m,r31+40,f0);
  f13=F(std::fma(f12,f31,f11)); Stfs(m,r31+32,f13);
}
// sub_821C7E30 (edf2017_recomp.19.cpp:8961).
void Guest821C7E30(const Memory& m,uint32_t r31,double f1) {
  const double f31=F(NativeGuestSin(f1));
  const double c=NativeGuestCos(f1);
  double f13=Lfs(m,r31+0);
  double f0=F(c);
  double f12=Lfs(m,r31+4);
  double f11=F(f13*f31);
  double f10=F(f12*f31);
  f12=F(std::fma(f12,f0,f11)); Stfs(m,r31+4,f12);
  f13=F(std::fma(f13,f0,-f10)); Stfs(m,r31+0,f13);
  f13=Lfs(m,r31+16); f12=Lfs(m,r31+20);
  f10=F(f13*f31); f11=F(f12*f31);
  f12=F(std::fma(f12,f0,f10)); Stfs(m,r31+20,f12);
  f13=F(std::fma(f13,f0,-f11)); Stfs(m,r31+16,f13);
  f12=Lfs(m,r31+36); f13=Lfs(m,r31+32);
  f11=F(f12*f31); f10=F(f13*f31);
  f13=F(std::fma(f13,f0,-f11));
  f0=F(std::fma(f12,f0,f10));
  Stfs(m,r31+36,f0); Stfs(m,r31+32,f13);
}
// sub_821C8480 (edf2017_recomp.62.cpp:9063).
void Guest821C8480(const Memory& m,uint32_t r3,uint32_t r4,double f1) {
  const uint32_t r9=r4-r3;
  for(uint32_t r8=3;r8;--r8,r3+=16) {
    uint32_t r11=r3;
    for(uint32_t r10=3;r10;--r10,r11+=4) {
      const double f13=Lfs(m,r11);
      double f0=Lfs(m,r9+r11);
      f0=F(f0-f13);
      f0=F(std::fma(f0,f1,f13));
      Stfs(m,r11,f0);
    }
  }
}
// sub_821B0320 (edf2017_recomp.50.cpp:8100).
void Guest821B0320(const Memory& m,uint32_t r3,double f1) {
  const double f12=Lfs(m,r3+4);
  double f0=F(f12*f12);
  double f13=Lfs(m,r3+0);
  const double f11=Lfs(m,r3+8);
  const double f10=Lfs(m,0x820009A4);
  f0=F(std::fma(f13,f13,f0));
  f0=F(std::fma(f11,f11,f0));
  if(f0==f10) { Stfs(m,r3+8,f10); Stfs(m,r3+4,f10); Stfs(m,r3+0,f10); return; }
  f0=F(std::sqrt(f0));
  f0=F(f1/f0);
  Stfs(m,r3+0,F(f13*f0)); Stfs(m,r3+4,F(f12*f0)); Stfs(m,r3+8,F(f11*f0));
}
// sub_821C84C8 (edf2017_recomp.56.cpp:8854).
void Guest821C84C8(const Memory& m,uint32_t r3) {
  const uint32_t r31=r3+32,r30=r3+16;
  double f0=Lfs(m,r31+4),f13=Lfs(m,r30+8);
  f0=F(f0*f13);
  double f12=Lfs(m,r31+8); f13=Lfs(m,r30+4);
  const double f31=Lfs(m,0x820008CC);
  f0=F(std::fma(f12,f13,-f0)); Stfs(m,r3+0,f0);
  f13=Lfs(m,r30+0); f0=Lfs(m,r31+8); f0=F(f0*f13);
  f12=Lfs(m,r31+0); f13=Lfs(m,r30+8);
  f0=F(std::fma(f13,f12,-f0)); Stfs(m,r3+4,f0);
  f13=Lfs(m,r31+0); f0=Lfs(m,r30+4); f0=F(f0*f13);
  f12=Lfs(m,r30+0); f13=Lfs(m,r31+4);
  const double f11=Lfs(m,r3+4);
  f0=F(std::fma(f13,f12,-f0)); Stfs(m,r3+8,f0);
  f13=Lfs(m,r31+8); f13=F(f11*f13);
  f12=Lfs(m,r31+4);
  f0=F(std::fma(f12,f0,-f13)); Stfs(m,r30+0,f0);
  f13=Lfs(m,r3+8); f0=Lfs(m,r31+0); f0=F(f0*f13);
  f12=Lfs(m,r31+8); f13=Lfs(m,r3+0);
  f0=F(std::fma(f13,f12,-f0)); Stfs(m,r30+4,f0);
  f13=Lfs(m,r3+0); f0=Lfs(m,r31+4); f0=F(f0*f13);
  f12=Lfs(m,r3+4); f13=Lfs(m,r31+0);
  f0=F(std::fma(f13,f12,-f0)); Stfs(m,r30+8,f0);
  Guest821B0320(m,r3,f31);
  Guest821B0320(m,r30,f31);
  Guest821B0320(m,r31,f31);
}
uint32_t Fctiwz(double v) {
  const int64_t r=std::isnan(v)?int64_t(0x80000000u):(v>=double(INT_MAX))?INT_MAX:_mm_cvttsd_si32(_mm_load_sd(&v));
  return uint32_t(r);
}
// sub_821CE848 (edf2017_recomp.74.cpp:8866). r1 is the stack pointer after stwu -208.
void Guest821CE848(const Memory& m,uint32_t r27,uint32_t r1) {
  if(!m.U32(r27+24)) return;
  double f0=Lfs(m,r27+32),f13=Lfs(m,r27+36);
  const double f30=Lfs(m,0x820008CC);
  double f31;
  uint64_t r23;
  if(f0<f13) {
    m.StoreU32(r1+80,Fctiwz(f0));
    r23=m.U32(r1+80);
    m.StoreU64(r1+80,uint64_t(int64_t(int32_t(uint32_t(r23)))));
    f13=double(std::bit_cast<int64_t>(m.U64(r1+80)));
    f13=F(f13);
    f31=F(f0-f13);
  } else {
    m.StoreU32(r1+80,Fctiwz(f0));
    f31=f30;
    r23=uint64_t(int64_t(m.U32(r1+80))+-1);
  }
  uint64_t r25=m.U32(r27+12);
  f0=Lfs(m,0x820009A4);
  uint64_t r11=m.U32(r27+20);
  Stfs(m,r1+88,f0); Stfs(m,r1+84,f0); Stfs(m,r1+80,f0);
  const uint64_t r9=Rot1(r11);
  r11=r11+r9;
  r11=uint64_t(uint32_t(r11)<<5);
  const uint32_t r22=uint32_t(r11+r25);
  if(uint32_t(r25)!=r22) {
    const uint64_t r24=r23+1;
    uint64_t r28=r25+16;
    do {
      const uint32_t r26=uint32_t(r28+48);
      const uint32_t r31=m.U32(r26-60);
      const int32_t stride=int32_t(m.U32(r31+24)),stream=int32_t(m.U32(r31+20));
      uint64_t a=uint64_t(int64_t(stride)*int64_t(int32_t(r23)));
      uint64_t b=uint64_t(int64_t(int32_t(r24))*int64_t(stride));
      a=Rot1(a); b=Rot1(b);
      a+=uint32_t(stream); b+=uint32_t(stream);
      uint32_t r30=uint32_t(a+r31),r29=uint32_t(b+r31);
      const auto keyed=[&](uint32_t r4,uint32_t divisor_offset) {
        int64_t i1=int16_t(m.U16(r29)),i0=int16_t(m.U16(r30));
        const uint32_t r10=m.U32(r27+24);
        const double f2=Lfs(m,r31+divisor_offset);
        const uint64_t r7=Rot1(uint64_t(i1)),r8=Rot1(uint64_t(i0));
        const uint64_t s1=uint64_t(i1)+r7,s0=uint64_t(i0)+r8;
        const uint32_t r6=uint32_t(Rot1(s1)+r10),r5=uint32_t(Rot1(s0)+r10);
        Guest821CE5C8(m,r1,r4,r5,r6,f31,f2);
      };
      if(m.U32(r31+4)&1) { keyed(r26,8); r30+=2; r29+=2; }
      if(m.U32(r31+4)&8) {
        Stfs(m,r26,Lfs(m,r31+28)); Stfs(m,uint32_t(r25)+68,Lfs(m,r31+32)); Stfs(m,uint32_t(r25)+72,Lfs(m,r31+36));
      }
      if(m.U32(r31+4)&2) {
        keyed(r1+80,12);
        Guest821C7B20(m,uint32_t(r28),Lfs(m,r1+80));
        Guest821C7D80(m,uint32_t(r28),Lfs(m,r1+84));
        Guest821C7E30(m,uint32_t(r28),Lfs(m,r1+88));
        r30+=2; r29+=2;
      }
      if(m.U32(r31+4)&16) {
        Guest821C7B20(m,uint32_t(r28),Lfs(m,r31+40));
        Guest821C7D80(m,uint32_t(r28),Lfs(m,r31+44));
        Guest821C7E30(m,uint32_t(r28),Lfs(m,r31+48));
      }
      if(m.U32(r31+4)&4) keyed(uint32_t(r28)+64,16);
      if(m.U32(r31+4)&32) {
        Stfs(m,uint32_t(r28)+64,Lfs(m,r31+52)); Stfs(m,uint32_t(r28)+68,Lfs(m,r31+56)); Stfs(m,uint32_t(r28)+72,Lfs(m,r31+60));
      }
      r25+=96; r28+=96;
    } while(uint32_t(r25)!=r22);
  }
  if(m.U8(r27+56)) {
    f0=Lfs(m,r27+64);
    uint32_t r30=m.U32(r27+12);
    f13=Lfs(m,r27+60);
    f0=F(f0/f13);
    if(!(f0<f30)) { f0=f30; m.StoreU8(r27+56,0); }
    f31=F(f30-f0);
    if(r30!=r22) {
      uint32_t r31=r30+72;
      do {
        const uint32_t b=m.U32(r30);
        double t0=Lfs(m,r31-8),t1=Lfs(m,r31-4),t2=Lfs(m,r31);
        const double f10=t0,f9=t1,f11=t2;
        double f8=Lfs(m,b+64);
        const double f7=Lfs(m,b+68);
        t0=F(f8-t0);
        f8=Lfs(m,b+72);
        t1=F(f7-t1); t2=F(f8-t2);
        t0=F(t0*f31); t1=F(t1*f31); t2=F(t2*f31);
        Stfs(m,r31-8,F(t0+f10)); Stfs(m,r31-4,F(t1+f9)); Stfs(m,r31,F(t2+f11));
        Guest821C8480(m,r31-56,b+16,f31);
        Guest821C84C8(m,r31-56);
        const uint32_t bb=m.U32(r30);
        const uint32_t r10=r31+8,r9=r31+12,r8=r31+16;
        r30+=96;
        double s11=Lfs(m,bb+80),s0=Lfs(m,r10);
        const double s10=Lfs(m,bb+84);
        s0=F(s11-s0);
        double s13=Lfs(m,r9);
        s11=Lfs(m,bb+88);
        s13=F(s10-s13);
        double s12=Lfs(m,r8);
        s12=F(s11-s12);
        const double c10=Lfs(m,r10),c11=Lfs(m,r9),c9=Lfs(m,r8);
        s0=F(s0*f31); s13=F(s13*f31); s12=F(s12*f31);
        Stfs(m,r10,F(c10+s0)); Stfs(m,r9,F(s13+c11)); Stfs(m,r8,F(s12+c9));
        r31+=96;
      } while(r30!=r22);
    }
  }
  uint32_t r3=m.U32(r27+12);
  if(r3==r22) return;
  uint32_t r8=r3+32;
  do {
    uint32_t b=m.U32(r3);
    if(m.U8(b+4)) {
      const uint32_t r7=r8-16,r6=r8+16,r5=r8+32;
      const uint32_t d=m.U32(b);
      const uint32_t r10=d+16,r9=d+32,r4=d+48;
      m.StoreU64(d,m.U64(r7)); m.StoreU64(d+8,m.U64(r7+8));
      m.StoreU64(r10,m.U64(r8)); m.StoreU64(r10+8,m.U64(r8+8));
      m.StoreU64(r9,m.U64(r6)); m.StoreU64(r9+8,m.U64(r6+8));
      m.StoreU64(r4,m.U64(r5)); m.StoreU64(r4+8,m.U64(r5+8));
      double s=Lfs(m,r8+48);
      Stfs(m,d,F(Lfs(m,d)*s)); { const double y=F(Lfs(m,d+4)*s); const double z=F(Lfs(m,d+8)*s); Stfs(m,d+4,y); Stfs(m,d+8,z); }
      s=Lfs(m,r8+52);
      Stfs(m,r10,F(Lfs(m,r10)*s)); { const double y=F(Lfs(m,r10+4)*s); const double z=F(Lfs(m,r10+8)*s); Stfs(m,r10+4,y); Stfs(m,r10+8,z); }
      s=Lfs(m,r8+56);
      { const double x=Lfs(m,r9),y=Lfs(m,r9+4),z=Lfs(m,r9+8); Stfs(m,r9,F(x*s)); Stfs(m,r9+4,F(s*y)); Stfs(m,r9+8,F(s*z)); }
    }
    r3+=96; r8+=96;
  } while(r3!=r22);
}

// sub_821C8198 as lanes (a byte-reversed lvx128 load puts guest element 3-i
// in lane i), with DPPS as its documented order of single roundings
// ((l0+l1)+(l2+l3)) in scalar code: under the same MXCSR (the test sets the
// flush mode for these) this checks the native DPPS path independently.
using Lanes=std::array<float,4>;
Lanes Load(const Memory& m,uint32_t at) {
  at&=~0xFu;
  return {std::bit_cast<float>(m.U32(at+12)),std::bit_cast<float>(m.U32(at+8)),std::bit_cast<float>(m.U32(at+4)),std::bit_cast<float>(m.U32(at))};
}
Lanes Hi(const Lanes& a,const Lanes& b) { return {a[2],b[2],a[3],b[3]}; }
Lanes Lo(const Lanes& a,const Lanes& b) { return {a[0],b[0],a[1],b[1]}; }
Lanes Dp(const Lanes& a,const Lanes& b) {
  volatile float t0=a[0]*b[0],t1=a[1]*b[1],t2=a[2]*b[2],t3=a[3]*b[3];
  volatile float l=t0+t1,h=t2+t3;
  const float s=l+h;
  return {s,s,s,s};
}
void Stvlx(const Memory& m,uint32_t ea,const Lanes& v) {
  uint8_t bytes[16];
  std::memcpy(bytes,v.data(),16);
  for(uint32_t i=0;i<16-(ea&0xF);++i) m.StoreU8(ea+i,bytes[15-i]);
}
void StoreLanes(const Memory& m,uint32_t ea,const Lanes& v) { ea&=~0xFu; for(unsigned i=0;i<4;++i) m.StoreU32(ea+i*4,std::bit_cast<uint32_t>(v[3-i])); }
// r1: 0 skips the stack writes (as the propagation walk, whose frames do not exist).
void Guest821C8198(const Memory& m,uint32_t r3,uint32_t r4,uint32_t r5,uint32_t r1=0) {
  Lanes v10=Load(m,r5),v0=Load(m,r4);
  if(r1) m.StoreU32(r1+20,r3);
  Lanes v9=Load(m,r5+16),v7=Load(m,r5+48),v8=Load(m,r5+32);
  Lanes v5=Hi(v7,v9),v6=Hi(v8,v10);
  Lanes v13=Load(m,r4+16);
  Lanes v4=Lo(v8,v10);
  Lanes v12=Load(m,r4+32);
  v7=Lo(v7,v9);
  Lanes v11=Load(m,r4+48);
  v9=Lo(v5,v6); v10=Hi(v5,v6); v8=Hi(v7,v4); v7=Lo(v7,v4);
  Lanes v1=Dp(v13,v9); v5=Dp(v13,v10); Lanes v2=Dp(v13,v8); v13=Dp(v13,v7);
  v6=Dp(v0,v10); Lanes v3=Dp(v0,v8); Lanes v31=Dp(v12,v10),v30=Dp(v12,v8),v29=Dp(v12,v9); v12=Dp(v12,v7);
  v4=Dp(v0,v9); v0=Dp(v0,v7); Lanes v28=Dp(v11,v7); v10=Dp(v11,v10); v8=Dp(v11,v8); v9=Dp(v11,v9);
  v7=Hi(v2,v5); v13=Hi(v13,v1); v11=Hi(v3,v6); v13=Hi(v13,v7); v6=Hi(v30,v31); v12=Hi(v12,v29); v0=Hi(v0,v4);
  const Lanes s48=v13;
  v13=Hi(v12,v6); v0=Hi(v0,v11); v10=Hi(v8,v10); v11=Hi(v28,v9);
  const Lanes s32=v13;
  v13=Hi(v11,v10);
  const Lanes s16=v13;
  if(r1) {
    StoreLanes(m,r1-48,s48); StoreLanes(m,r1-32,s32); StoreLanes(m,r1-16,s16);
    Stvlx(m,r3,v0);
    Stvlx(m,r3+16,Load(m,r1-48)); Stvlx(m,r3+32,Load(m,r1-32)); Stvlx(m,r3+48,Load(m,r1-16));
    return;
  }
  Stvlx(m,r3,v0); Stvlx(m,r3+16,s48); Stvlx(m,r3+32,s32); Stvlx(m,r3+48,s16);
}
void Guest821D1688(const Memory& m,uint32_t node,uint32_t parent) {
  Guest821C8198(m,node+240,node+176,parent);
  const uint32_t count=m.U32(node+88);
  for(uint32_t i=0,off=0;i<count;++i,off+=304) Guest821D1688(m,m.U32(node+80)+off,node+240);
}

// ---- Synthetic data. ----
bool IsNan(uint32_t bits) { return (bits&0x7F800000u)==0x7F800000u && (bits&0x007FFFFFu); }
// Word-wise (guest words; NaN matches NaN) over [begin,end), skipping [skip_begin,skip_end).
void RequireSame(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b,uint32_t skip_begin,uint32_t skip_end,const char* what) {
  Require(a.size()==b.size(),"size");
  for(uint32_t i=0;i+4<=a.size();i+=4) {
    if(i>=skip_begin && i<skip_end) continue;
    const uint32_t x=uint32_t(a[i])<<24|uint32_t(a[i+1])<<16|uint32_t(a[i+2])<<8|a[i+3];
    const uint32_t y=uint32_t(b[i])<<24|uint32_t(b[i+1])<<16|uint32_t(b[i+2])<<8|b[i+3];
    if(x!=y && !(IsNan(x) && IsNan(y))) {
      std::cerr<<what<<": word at "<<std::hex<<i<<" native "<<x<<" guest "<<y<<std::dec<<"\n";
      throw std::runtime_error(what);
    }
  }
}
std::vector<uint8_t> Image(float one=1.0f,float zero=0.0f) {
  std::vector<uint8_t> image(0x1000,0);
  const auto put=[&](uint32_t a,float v) { const uint32_t b=std::bit_cast<uint32_t>(v); for(int i=0;i<4;++i) image[a-kImage+i]=uint8_t(b>>(24-8*i)); };
  put(0x820008CC,one); put(0x820009A4,zero);
  return image;
}
struct Random {
  std::mt19937 engine;
  explicit Random(uint32_t seed):engine(seed) {}
  uint32_t U(uint32_t n) { return std::uniform_int_distribution<uint32_t>(0,n-1)(engine); }
  float Uniform(float a,float b) { return std::uniform_real_distribution<float>(a,b)(engine); }
  // Mostly ordinary values, sometimes the awkward ones.
  float Awkward(float scale) {
    switch(U(40)) {
      case 0: return std::numeric_limits<float>::quiet_NaN();
      case 1: return std::numeric_limits<float>::infinity();
      case 2: return -std::numeric_limits<float>::infinity();
      case 3: return std::bit_cast<float>(0x00000001u+U(0x7FFFFF));  // denormal
      case 4: return -std::bit_cast<float>(0x00000001u+U(0x7FFFFF));
      case 5: return 0.0f;
      case 6: return -0.0f;
      case 7: return Uniform(-3e8f,3e8f);                               // beyond the trig limit
      case 8: return float(U(64))*1.5707963f;                           // multiples of pi/2
      case 9: return Uniform(-1e-30f,1e-30f);
      default: return Uniform(-scale,scale);
    }
  }
};

constexpr uint32_t kHeap=0xD0000,kTreeHeap=0x40000;
constexpr uint32_t kSlot=0x1000,kStackTop=0x3000,kStackBottom=0x2000,kPalette=0x10000,kChannels=0x20000,
  kRecords=0x40000,kBones=0x60000,kLocals=0x80000,kStreams=0xA0000;

// One random slot: records, channels, bones, palette, streams.
void BuildSlot(const Memory& m,Random& r,uint32_t records,bool clip_end_case) {
  const uint32_t frames=2+r.U(40);
  m.StoreU32(kSlot+24,r.U(10)?kPalette:0);
  m.StoreU32(kSlot+12,kRecords);
  m.StoreU32(kSlot+20,records);
  float time=r.Uniform(0,float(frames-1));
  float end=float(frames-1);
  if(clip_end_case) time=end+float(r.U(3))*0.5f;
  if(r.U(20)==0) time=r.Awkward(float(frames));
  if(r.U(30)==0) end=r.Awkward(float(frames));
  // Keep the frame index inside the streams unless the time is deliberately odd.
  if(!(std::isfinite(time) && time>=0 && time<float(frames))) {
    if(r.U(2)) time=float(r.U(frames-1));
  }
  m.StoreU32(kSlot+32,std::bit_cast<uint32_t>(time));
  m.StoreU32(kSlot+36,std::bit_cast<uint32_t>(end));
  const bool blending=r.U(3)==0;
  m.StoreU8(kSlot+56,blending);
  float length=float(1+r.U(20)),clock=r.Uniform(0,length*1.3f);
  if(r.U(15)==0) length=r.Awkward(10);
  if(r.U(15)==0) clock=r.Awkward(10);
  m.StoreU32(kSlot+60,std::bit_cast<uint32_t>(length));
  m.StoreU32(kSlot+64,std::bit_cast<uint32_t>(clock));
  for(uint32_t i=0;i<256*3;++i) {
    int16_t v=int16_t(int32_t(r.U(65536))-32768);
    if(r.U(4)==0) v=int16_t(int32_t(r.U(200))-100);
    m.StoreU16(kPalette+i*2,uint16_t(v));
  }
  static constexpr uint32_t common[]{56,42,35,49,42,42,56,35};
  const uint32_t bones=records?1+r.U(records):1;
  for(uint32_t i=0;i<bones;++i) {
    const uint32_t b=kBones+i*96;
    m.StoreU32(b+0,kLocals+i*64);
    m.StoreU8(b+4,r.U(8)!=0);
    for(uint32_t k=16;k<96;k+=4) m.StoreU32(b+k,std::bit_cast<uint32_t>(r.U(6)?r.Uniform(-2,2):r.Awkward(4)));
    for(uint32_t k=0;k<64;k+=4) m.StoreU32(kLocals+i*64+k,std::bit_cast<uint32_t>(r.Uniform(-9,9)));
  }
  for(uint32_t i=0;i<records;++i) {
    const uint32_t rec=kRecords+i*96,ch=kChannels+i*64;
    m.StoreU32(rec+0,kBones+r.U(bones)*96);  // bones shared by several records sometimes
    m.StoreU32(rec+4,ch);
    for(uint32_t k=8;k<96;k+=4) m.StoreU32(rec+k,std::bit_cast<uint32_t>(r.U(10)?r.Uniform(-2,2):r.Awkward(3)));
    const uint32_t flags=r.U(4)?common[r.U(8)]:r.U(64);
    const uint32_t keyed=(flags&1)+((flags>>1)&1)+((flags>>2)&1);
    const uint32_t stride=keyed?keyed:uint32_t(r.U(2));
    m.StoreU32(ch+4,flags);
    for(uint32_t k:{8u,12u,16u}) m.StoreU32(ch+k,std::bit_cast<uint32_t>(r.U(12)?float(1+r.U(4096)):r.Awkward(100)));
    const uint32_t stream=kStreams+i*1024;
    m.StoreU32(ch+20,stream-ch);
    m.StoreU32(ch+24,stride);
    for(uint32_t k=0;k<stride*(frames+1);++k) m.StoreU16(stream+k*2,uint16_t(r.U(256)));
    for(uint32_t k:{28u,32u,36u,52u,56u,60u}) m.StoreU32(ch+k,std::bit_cast<uint32_t>(r.U(10)?r.Uniform(-50,50):r.Awkward(50)));
    for(uint32_t k:{40u,44u,48u}) m.StoreU32(ch+k,std::bit_cast<uint32_t>(r.Awkward(7)));
  }
}
bool FrameInStreams(const Memory& m) {
  const float time=std::bit_cast<float>(m.U32(kSlot+32)),end=std::bit_cast<float>(m.U32(kSlot+36));
  const int32_t frame=int32_t(time<end?skeletal::Fctiwz(time):skeletal::Fctiwz(time)-1u);
  return frame>=0 && frame<41;
}

void EvaluationMatchesGuest() {
  Random r(1234);
  uint64_t cases=0,records_total=0,blends=0,skipped=0,big=0,odd_constants=0;
  // One memo for the whole run: the channels sit at the same addresses in every
  // slot with new angles each time, so its entries are met again with other
  // angles (a stale entry must miss) as well as with the same ones.
  static NativeSkeletalRotationCache cache;
  for(uint32_t iteration=0;iteration<30000;++iteration) {
    // Sometimes other image constants (the memo must stand aside then).
    float one=1.0f,zero=0.0f;
    if(r.U(25)==0) { one=r.U(2)?2.0f:-1.0f; ++odd_constants; }
    if(r.U(25)==0) { zero=r.U(2)?-0.0f:0.5f; ++odd_constants; }
    std::vector<uint8_t> heap(kHeap,0),image=Image(one,zero);
    const Memory m{heap,image};
    // Now and then more records than one rotation batch holds (64).
    const uint32_t records=r.U(40)==0?60+r.U(90):r.U(12);
    big+=records>64;
    BuildSlot(m,r,records,r.U(6)==0);
    if(!FrameInStreams(m) && m.U32(kSlot+24) && records) { ++skipped; continue; }  // would read off the synthetic streams
    const std::vector<uint8_t> initial=heap;
    std::vector<uint8_t> heap_guest=heap,image_guest=image;
    const Memory g{heap_guest,image_guest};
    Guest821CE848(g,kSlot,kStackTop-208);
    NativeSkeletalEvaluateScalar(m,kSlot);
    RequireSame(heap,heap_guest,kStackBottom,kStackTop,"821CE848 (scalar)");
    heap=initial;
    NativeSkeletalEvaluate(m,kSlot);
    RequireSame(heap,heap_guest,kStackBottom,kStackTop,"821CE848 (batched)");
    heap=initial;
    NativeSkeletalEvaluate(PlainMemory{m},kSlot);
    RequireSame(heap,heap_guest,kStackBottom,kStackTop,"821CE848 (batched, access by access)");
    for(int pass=0;pass<2;++pass) {  // fills the memo, then hits it
      heap=initial;
      NativeSkeletalEvaluate(m,kSlot,&cache);
      RequireSame(heap,heap_guest,kStackBottom,kStackTop,pass?"821CE848 (memo hit)":"821CE848 (memo fill)");
    }
    ++cases; records_total+=records; blends+=m.U8(kSlot+56)||g.U8(kSlot+56)?1:0;
  }
  Require(cache.hits>10000 && cache.misses>10000,"the memo was not exercised");
  std::cout<<"821CE848: "<<cases<<" slots, "<<records_total<<" records match, scalar, batched and memoised ("
    <<big<<" slots over one batch, "<<odd_constants<<" odd image constants, memo "<<cache.hits<<" hits "
    <<cache.misses<<" misses; "<<skipped<<" skipped)\n";
}

// The memo answers only for the same three angle bits under the state its
// entries were filled in: after a fill, the same slot is evaluated again with
// one constant rotation's angle changed (each of the three in turn), with other
// image constants, and under round-up; each must match the guest (no stale hit).
void MemoStandsAside() {
  Random r(4321);
  NativeSkeletalRotationCache cache;
  const uint32_t csr=_mm_getcsr();
  uint32_t checked[6]{};
  for(uint32_t iteration=0;iteration<6000;++iteration) {
    std::vector<uint8_t> heap(kHeap,0),image=Image();
    const Memory m{heap,image};
    const uint32_t records=1+r.U(12);
    BuildSlot(m,r,records,false);
    if(!FrameInStreams(m) || !m.U32(kSlot+24)) continue;
    // Constant rotations only, so the memo decides every record's rows.
    for(uint32_t i=0;i<records;++i) m.StoreU32(kChannels+i*64+4,r.U(2)?56u:16u);
    const std::vector<uint8_t> initial=heap;
    NativeSkeletalEvaluate(m,kSlot,&cache);   // fill
    const uint32_t variant=iteration%6;
    heap=initial;
    std::vector<uint8_t> image_now=image;
    const Memory now{heap,image_now};
    if(variant<3) {
      // One angle of one channel, one ulp or a sign away.
      const uint32_t at=kChannels+r.U(records)*64+40+variant*4;
      const uint32_t bits=m.U32(at);
      now.StoreU32(at,r.U(2)?bits^0x80000000u:bits+1u);
    } else if(variant==3) {
      image_now=Image(2.0f,0.0f);
    } else if(variant==4) {
      image_now=Image(1.0f,-0.0f);
    }
    std::vector<uint8_t> heap_guest=heap,image_guest=image_now;
    const Memory g{heap_guest,image_guest};
    if(variant==5) _mm_setcsr((csr&~0x6000u)|0x4000u);
    Guest821CE848(g,kSlot,kStackTop-208);
    NativeSkeletalEvaluate(now,kSlot,&cache);
    _mm_setcsr(csr);
    static const char* what[6]{"821CE848 (memo, angle x changed)","821CE848 (memo, angle y changed)",
      "821CE848 (memo, angle z changed)","821CE848 (memo, one=2)","821CE848 (memo, zero=-0)","821CE848 (memo, round up)"};
    RequireSame(heap,heap_guest,kStackBottom,kStackTop,what[variant]);
    ++checked[variant];
  }
  std::cout<<"memo stands aside: "<<checked[0]<<"/"<<checked[1]<<"/"<<checked[2]<<" slots with an angle changed, "
    <<checked[3]<<"/"<<checked[4]<<" with other constants, "<<checked[5]<<" under round-up match\n";
}

// The four-lane rotation build against the one-record build, lane by lane.
void LaneRotationsMatchScalar() {
  Random r(17);
  uint64_t rotations=0;
  for(uint32_t i=0;i<400000;++i) {
    alignas(32) double angle[3][4];
    for(auto& axis:angle) for(double& a:axis) a=double(r.U(4)?r.Awkward(8):std::bit_cast<float>(uint32_t(r.engine())));
    const double one=r.U(50)?1.0:double(r.Awkward(2)),zero=r.U(50)?0.0:double(r.Awkward(2));
    __m256d rows[12];
    skeletal::lanes::RotationXYZ(rows,_mm256_load_pd(angle[0]),_mm256_load_pd(angle[1]),_mm256_load_pd(angle[2]),
      _mm256_set1_pd(one),_mm256_set1_pd(zero));
    alignas(32) double out[12][4];
    for(int k=0;k<12;++k) _mm256_store_pd(out[k],rows[k]);
    for(int lane=0;lane<4;++lane) {
      const double angles[3]{angle[0][lane],angle[1][lane],angle[2][lane]};
      skeletal::Rows expected;
      skeletal::RotationXYZ(expected,angles,one,zero);
      for(int k=0;k<12;++k) {
        const uint64_t a=std::bit_cast<uint64_t>(expected[k]),b=std::bit_cast<uint64_t>(out[k][lane]);
        Require(a==b || (std::isnan(expected[k]) && std::isnan(out[k][lane])),"lane rotation differs");
      }
      ++rotations;
    }
  }
  std::cout<<"lane rotations: "<<rotations<<" match\n";
}

// Rows the batched evaluation loads and stores whole: one crossing 0xE0000000
// (where the game's host view of guest memory jumps) goes byte by byte.
struct SplitMemory {
  std::vector<uint8_t>& bytes;   // [0xDFFFFF00, +0x200)
  static constexpr uint32_t base=0xDFFFFF00u;
  uint8_t* At(uint32_t a,uint32_t size) const {
    if(a<base || a-base+uint64_t(size)>bytes.size()) throw std::runtime_error("split address out of range");
    return bytes.data()+(a-base);
  }
  uint8_t U8(uint32_t a) const { return *At(a,1); }
  void StoreU8(uint32_t a,uint8_t v) const { *At(a,1)=v; }
  const uint8_t* Row(uint32_t a) const {
    if(a<0xE0000000u && a+16u>0xE0000000u) throw std::runtime_error("a whole row across 0xE0000000");
    return At(a,16);
  }
  void StoreRow(uint32_t a,__m128i v) const {
    if(a<0xE0000000u && a+16u>0xE0000000u) throw std::runtime_error("a whole row across 0xE0000000");
    _mm_storeu_si128(reinterpret_cast<__m128i*>(At(a,16)),v);
  }
};
void RowsAcrossTheViewBoundary() {
  std::vector<uint8_t> bytes(0x200);
  for(size_t i=0;i<bytes.size();++i) bytes[i]=uint8_t(i*7+3);
  const SplitMemory m{bytes};
  for(uint32_t a=0xDFFFFFE0u;a<=0xE0000010u;++a) {
    const __m128i v=NativeSkeletalLoadBytes(m,a);
    alignas(16) uint8_t got[16];
    _mm_store_si128(reinterpret_cast<__m128i*>(got),v);
    for(uint32_t i=0;i<16;++i) Require(got[i]==m.U8(a+i),"row load across the boundary");
    const __m128i w=_mm_add_epi8(v,_mm_set1_epi8(1));
    NativeSkeletalStoreBytes(m,a,w);
    for(uint32_t i=0;i<16;++i) Require(m.U8(a+i)==uint8_t(got[i]+1),"row store across the boundary");
  }
  std::cout<<"rows across 0xE0000000: byte by byte\n";
}

// Trees: random shapes, awkward values, some unaligned outputs (partial stvlx).
void PropagationMatchesGuest(bool flush) {
  Random r(flush?99:98);
  const uint32_t csr=_mm_getcsr();
  if(flush) _mm_setcsr(csr|0x8040u); else _mm_setcsr(csr&~0x8040u);
  uint64_t nodes_total=0;
  for(uint32_t iteration=0;iteration<4000;++iteration) {
    std::vector<uint8_t> heap(kTreeHeap,0),image=Image();
    const Memory m{heap,image};
    uint32_t next=0x10000;
    const uint32_t misalign=r.U(4)==0?(r.U(4)*4):0;
    uint32_t count=0;
    // Build a tree breadth first: each node gets 0-3 children in one array.
    std::vector<uint32_t> queue{next};
    const uint32_t root=next; next+=304+misalign;
    while(!queue.empty() && count<60) {
      const uint32_t node=queue.front(); queue.erase(queue.begin()); ++count;
      for(uint32_t k=0;k<304;k+=4) if(k<240) m.StoreU32(node+k,std::bit_cast<uint32_t>(r.U(8)?r.Uniform(-4,4):r.Awkward(4)));
      const uint32_t children=r.U(4);
      m.StoreU32(node+80,next); m.StoreU32(node+88,children);
      for(uint32_t c=0;c<children;++c) queue.push_back(next+c*304);
      next+=children*304+16;
    }
    for(uint32_t node:queue) { m.StoreU32(node+80,0); m.StoreU32(node+88,0); }
    const uint32_t parent=0x8000+misalign;
    for(uint32_t k=0;k<64;k+=4) m.StoreU32(parent+k,std::bit_cast<uint32_t>(r.U(8)?r.Uniform(-100,100):r.Awkward(10)));
    std::vector<uint8_t> heap_guest=heap,image_guest=image;
    const Memory g{heap_guest,image_guest};
    nodes_total+=NativeSkeletalPropagate(m,root,parent);
    Guest821D1688(g,root,parent);
    RequireSame(heap,heap_guest,0,0,flush?"821D1688 (flush)":"821D1688");
  }
  _mm_setcsr(csr);
  std::cout<<"821D1688"<<(flush?" (flush on)":"")<<": "<<nodes_total<<" nodes match\n";
}

// 821C8198 as a whole call (its stack writes included), with out, a and b
// anywhere: unaligned, overlapping each other, overlapping the stack slots.
void MultiplyCallMatchesGuest() {
  Random r(21);
  const uint32_t csr=_mm_getcsr();
  _mm_setcsr(csr|0x8040u);
  for(uint32_t i=0;i<200000;++i) {
    std::vector<uint8_t> heap(0x2000,0),image=Image();
    const Memory m{heap,image};
    for(uint32_t k=0x100;k<0x1F00;k+=4) m.StoreU32(k,std::bit_cast<uint32_t>(r.U(8)?r.Uniform(-50,50):r.Awkward(10)));
    const uint32_t r1=0x1000+r.U(16)*16+(r.U(8)==0?r.U(4)*4:0);
    const auto pick=[&] { return (r.U(6)==0?r1-64+r.U(128):0x200+r.U(0xC00))&~3u; };  // floats are word aligned
    const uint32_t out=pick(),a=pick(),b=pick();
    std::vector<uint8_t> heap_guest=heap,image_guest=image;
    const Memory g{heap_guest,image_guest};
    NativeSkeletalMultiply(m,out,a,b,r1);
    Guest821C8198(g,out,a,b,r1);
    RequireSame(heap,heap_guest,0,0,"821C8198 call");
  }
  _mm_setcsr(csr);
  std::cout<<"821C8198 with stack writes: 200000 calls match\n";
}
// The propagation's multiply (mulps/addps in the DPPS order) against the DPPS
// transcription of 821C8198, flush mode off and on, over ordinary, awkward and
// denormal-heavy rows (a NaN matches any NaN).
void MultiplyRowsMatchDpps() {
  Random r(23);
  const uint32_t csr=_mm_getcsr();
  uint64_t calls=0;
  for(int flush=0;flush<2;++flush) {
    _mm_setcsr(flush?(csr|0x8040u):(csr&~0x8040u));
    for(uint32_t i=0;i<300000;++i) {
      std::vector<uint8_t> heap(0x400),image=Image();
      const Memory m{heap,image};
      const uint32_t kind=r.U(4);
      for(uint32_t k=0;k<0x200;k+=4) {
        float v;
        switch(kind) {
          case 0: v=r.Uniform(-4,4); break;
          case 1: v=r.Awkward(8); break;
          case 2: v=r.U(2)?std::bit_cast<float>(r.U(0x01000000u)|(r.U(2)<<31)):r.Uniform(-1e-19f,1e-19f); break;  // tiny and denormal
          default: v=std::bit_cast<float>(uint32_t(r.engine())); break;
        }
        m.StoreU32(k,std::bit_cast<uint32_t>(v));
      }
      const uint32_t a=r.U(8)*16+(r.U(4)==0?r.U(4)*4:0),b=0x100+r.U(8)*16+(r.U(4)==0?r.U(4)*4:0),out=0x300;
      std::vector<uint8_t> heap_dpps=heap,image_dpps=image;
      const Memory d{heap_dpps,image_dpps};
      NativeSkeletalMultiplyRows(m,out,a,b);
      NativeSkeletalMultiply(d,out,a,b);
      RequireSame(heap,heap_dpps,0,0,flush?"821C8198 rows (flush on)":"821C8198 rows");
      ++calls;
    }
  }
  _mm_setcsr(csr);
  std::cout<<"propagation multiply against DPPS: "<<calls<<" calls match\n";
}
// The helpers other native code already uses must agree with this file's.
void RotationsMatchRenderHelpers() {
  Random r(7);
  for(uint32_t i=0;i<200000;++i) {
    const float x=r.Awkward(10),y=r.Awkward(10);
    NativeGuestMatrix helper{};
    NativeGuestRotationX(helper,x);
    NativeGuestRotateY(helper,y);
    skeletal::Rows rows;
    skeletal::RotationX(rows,double(x),1.0,0.0);
    skeletal::RotateY(rows,double(y));
    for(size_t k=0;k<12;++k) {
      const uint32_t a=std::bit_cast<uint32_t>(helper[k]),b=std::bit_cast<uint32_t>(float(rows[k]));
      Require(a==b || (IsNan(a) && IsNan(b)),"rotation helpers differ");
    }
  }
  std::cout<<"rotX/rotY agree with native_render_instances.h\n";
}
// The batched sin/cos against NativeGuestSin/NativeGuestCos (the transcriptions
// the render registry tests check against the recompiled bodies), bit for bit.
void BatchedTrigMatchesScalar() {
  Random r(11);
  uint64_t values=0;
  const auto check=[&](float angle) {
    const double x[3]{double(angle),double(-angle),double(angle*0.5f)};
    double s[3],c[3];
    skeletal::NativeGuestSinCos3(x,s,c);
    for(int k=0;k<3;++k) {
      const uint32_t es=std::bit_cast<uint32_t>(float(NativeGuestSin(x[k]))),ec=std::bit_cast<uint32_t>(float(NativeGuestCos(x[k])));
      const uint32_t gs=std::bit_cast<uint32_t>(float(s[k])),gc=std::bit_cast<uint32_t>(float(c[k]));
      if(!(es==gs || (IsNan(es) && IsNan(gs))) || !(ec==gc || (IsNan(ec) && IsNan(gc)))) {
        std::cerr<<"angle bits "<<std::hex<<std::bit_cast<uint32_t>(float(x[k]))<<" sin "<<es<<"/"<<gs<<" cos "<<ec<<"/"<<gc<<std::dec<<"\n";
        throw std::runtime_error("batched sin/cos differ");
      }
    }
    values+=3;
  };
  for(uint32_t i=0;i<2000000;++i) check(r.Awkward(20));
  for(uint32_t i=0;i<2000000;++i) check(std::bit_cast<float>(uint32_t(r.engine())));  // every exponent
  for(float k=-64;k<=64;k+=0.5f) check(k*1.5707963267948966f);
  std::cout<<"batched sin/cos: "<<values<<" values match\n";
}
}  // namespace

int main() {
  try {
    RotationsMatchRenderHelpers();
    BatchedTrigMatchesScalar();
    LaneRotationsMatchScalar();
    RowsAcrossTheViewBoundary();
    EvaluationMatchesGuest();
    MemoStandsAside();
    MultiplyRowsMatchDpps();
    PropagationMatchesGuest(false);
    PropagationMatchesGuest(true);
    MultiplyCallMatchesGuest();
  } catch(const std::exception& error) {
    std::cerr<<"FAILED: "<<error.what()<<"\n";
    return 1;
  }
  std::cout<<"native skeletal tests passed\n";
  return 0;
}
