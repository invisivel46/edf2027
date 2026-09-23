#include "native_graphics/native_full_frame_effects.h"
#include "native_graphics/native_transparent_items.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
using namespace edf::native;
namespace {
void Require(bool condition,const std::string& text) { if(!condition) throw std::runtime_error(text); }
// Sparse big-endian guest memory in 4 KiB pages; a read of an unmapped page throws.
struct Reader {
  mutable std::map<uint32_t,std::array<uint8_t,4096>> pages;
  uint32_t Add(uint32_t at,uint32_t offset) const { return at+offset; }
  const uint8_t* Bytes(uint32_t at,size_t size) const {
    if((at&4095)+size>4096) throw std::runtime_error("test range crosses a page");
    auto found=pages.find(at&~4095u);
    if(found==pages.end()) throw std::runtime_error("unmapped test read");
    return found->second.data()+(at&4095);
  }
  uint8_t* Map(uint32_t at) const { return pages[at&~4095u].data()+(at&4095); }
  uint32_t Word(uint32_t at) const { const auto* p=Bytes(at,4); return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
  uint8_t Byte(uint32_t at) const { return *Bytes(at,1); }
  void StoreWord(uint32_t at,uint32_t value) const { auto* p=Map(at); for(int i=0;i<4;++i) p[i]=uint8_t(value>>(24-i*8)); }
  void StoreByte(uint32_t at,uint8_t value) const { *Map(at)=value; }
  void StoreHalf(uint32_t at,uint16_t value) const { auto* p=Map(at); p[0]=uint8_t(value>>8); p[1]=uint8_t(value); }
  void StoreFloat(uint32_t at,float value) const { StoreWord(at,std::bit_cast<uint32_t>(value)); }
  std::vector<uint8_t> Read(uint32_t at,size_t size) const {
    std::vector<uint8_t> out(size);
    for(size_t i=0;i<size;++i) out[i]=Byte(at+uint32_t(i));
    return out;
  }
};
// --- The recompiled code's register semantics -------------------------------
double F(double x) { return double(float(x)); }
double lfs(const Reader& m,uint32_t at) { return double(std::bit_cast<float>(m.Word(at))); }
void stfs(const Reader& m,uint32_t at,double v) { m.StoreWord(at,std::bit_cast<uint32_t>(float(v))); }
void copy_words(const Reader& m,uint32_t dst,uint32_t src,uint32_t bytes) {
  std::vector<uint32_t> words; for(uint32_t i=0;i<bytes;i+=4) words.push_back(m.Word(src+i));
  for(uint32_t i=0;i<bytes;i+=4) m.StoreWord(dst+i,words[i/4]);
}
double fmadds(double a,double c,double b) { return F(std::fma(a,c,b)); }
double fmsubs(double a,double c,double b) { return F(std::fma(a,c,-b)); }
using A=NativeEffectConstantAddress;
constexpr uint32_t kEffect=0x40002460,kCamera=0x40001DC0;
constexpr uint32_t kStack=0x60000000,kScratch=0x68000000;

// sub_821B0320 (recomp.50.cpp:8100).
void T_SetLength(const Reader& m,uint32_t r3,double f1) {
  double f12=lfs(m,r3+4),f0=F(f12*f12),f13=lfs(m,r3),f11=lfs(m,r3+8),f10=lfs(m,A::zero);
  f0=fmadds(f13,f13,f0); f0=fmadds(f11,f11,f0);
  if(f0==f10) { stfs(m,r3+8,f10); stfs(m,r3+4,f10); stfs(m,r3,f10); return; }
  f0=F(std::sqrt(f0)); f0=F(f1/f0);
  f13=F(f13*f0); stfs(m,r3,f13); f13=F(f12*f0); stfs(m,r3+4,f13); f0=F(f11*f0); stfs(m,r3+8,f0);
}
// sub_821A7640 (recomp.2.cpp:7596), loc_821A7764..821A7898. Returns every
// DrawPrimitiveUP's vertices, concatenated; V is the r1+80 vertex area.
std::vector<uint8_t> T_Expand(const Reader& m,uint32_t r31,uint32_t r26,uint32_t V=kScratch) {
  std::vector<uint8_t> out;
  if(int32_t(r26)==0) return out;
  const double f28=lfs(m,A::three_half_pi),f29=lfs(m,A::half_pi),f30=lfs(m,A::one),f31=lfs(m,A::zero);
  do {
    uint32_t r9=int32_t(r26)>1000?1000:r26;
    r26-=r9;
    if(!r9) break;
    const double f6=lfs(m,A::pi);
    uint32_t r11=V+88,r10=r9;
    do {
      double f0=lfs(m,r31); --r10; double f13=lfs(m,r31+36);
      stfs(m,r11+44,f0); const double f5=F(f13+f29); stfs(m,r11,f0); stfs(m,r11-44,f0); stfs(m,r11-88,f0);
      f0=F(f13+f6); stfs(m,r11-68,f13); f13=F(f13+f28); stfs(m,r11+20,f0);
      const double f12=lfs(m,r31+32),f11=lfs(m,r31+16),f10=lfs(m,r31+20),f9=lfs(m,r31+24),f8=lfs(m,r31+4),f7=lfs(m,r31+8);
      f0=lfs(m,r31+28); r31+=48;
      stfs(m,r11-76,f31); stfs(m,r11-72,f31); stfs(m,r11-32,f30); stfs(m,r11-28,f31); stfs(m,r11+12,f30);
      stfs(m,r11+16,f30); stfs(m,r11+56,f31); stfs(m,r11+60,f30); stfs(m,r11-24,f5); stfs(m,r11+64,f13);
      for(int32_t o:{68,24,-20,-64}) stfs(m,r11+o,f12);
      for(int32_t o:{72,28,-16,-60}) stfs(m,r11+o,f11);
      for(int32_t o:{76,32,-12,-56}) stfs(m,r11+o,f10);
      for(int32_t o:{48,4,-40,-84}) stfs(m,r11+o,f8);
      for(int32_t o:{52,8,-36,-80}) stfs(m,r11+o,f7);
      for(int32_t o:{80,36,-8,-52}) stfs(m,r11+o,f9);
      for(int32_t o:{84,40,-4,-48}) stfs(m,r11+o,f0);
      r11+=176;
    } while(r10);
    const auto bytes=m.Read(V,r9*176);
    out.insert(out.end(),bytes.begin(),bytes.end());
  } while(r26);
  return out;
}
// sub_821A8628 (recomp.22.cpp:7790). r8 = colour, f1 = width, S = r1.
std::vector<uint8_t> T_Segments(const Reader& m,uint32_t r4,uint32_t r5,uint32_t r8,double f26,uint32_t S=kScratch) {
  if(int32_t(r5)<2) return {};
  int32_t r28=int32_t(r5)/2; if(r28>100) r28=100;
  for(uint32_t i=0;i<4;++i) stfs(m,S+96+i*4,lfs(m,r8+i*4));
  copy_words(m,S+112,m.Word(0x8257C02C)+192,16);
  const double f31=lfs(m,A::zero); stfs(m,S+88,f31); stfs(m,S+84,f31); stfs(m,S+80,f31);
  const double f30=lfs(m,A::one); stfs(m,S+92,f30);
  if(!r28) return {};
  const double f29=lfs(m,S+120); uint32_t r31=S+132; const double f28=lfs(m,S+116); uint32_t r30=r4+36;
  const double f27=lfs(m,S+112); int32_t r29=r28;
  do {
    double f12=lfs(m,r30-28),f0=lfs(m,r30-36); double f9=F(f12-f29); double f6=lfs(m,r30+4); double f11=F(f0-f27);
    double f13=lfs(m,r30-32); f12=F(f6-f12); double f8=lfs(m,r30); double f10=F(f13-f28); double f7=lfs(m,r30-4);
    f13=F(f8-f13); f0=F(f7-f0);
    f7=F(f12*f11); f8=F(f13*f9); f6=F(f10*f0);
    f0=fmsubs(f9,f0,f7); stfs(m,S+84,f0); f12=fmsubs(f12,f10,f8); stfs(m,S+80,f12); f0=fmsubs(f13,f11,f6); stfs(m,S+88,f0);
    T_SetLength(m,S+80,f26);
    f0=lfs(m,S+80); f11=lfs(m,r30-36); f10=lfs(m,r30-32); f9=lfs(m,r30-28); f6=F(f11+f0);
    f13=lfs(m,S+84); f12=lfs(m,S+88); f8=lfs(m,r30-16); f7=lfs(m,r30-4);
    const double f5=F(f10+f13),f4=F(f9+f12); f11=F(f11-f0); f10=F(f10-f13); f9=F(f9-f12);
    copy_words(m,r31+16,S+96,16); copy_words(m,r31+52,S+96,16);
    stfs(m,r31+12,f8); stfs(m,r31+48,f8); f8=F(f7-f0);
    stfs(m,r31+32,f11); stfs(m,r31+36,f10); stfs(m,r31+40,f9);
    f11=lfs(m,r30); f10=lfs(m,r30+16); f9=lfs(m,r30+4);
    stfs(m,r31+8,f31); stfs(m,r31+44,f30); stfs(m,r31-4,f6); stfs(m,r31,f5); stfs(m,r31+4,f4);
    f0=F(f7+f0); stfs(m,r31+68,f8); --r29; stfs(m,r31+104,f0);
    f0=F(f11-f13); f13=F(f11+f13); f11=F(f9-f12); f12=F(f9+f12);
    stfs(m,r31+80,f30); stfs(m,r31+116,f31); stfs(m,r31+72,f0);
    copy_words(m,r31+88,S+96,16); copy_words(m,r31+124,S+96,16);
    stfs(m,r31+76,f11); stfs(m,r31+108,f13); stfs(m,r31+112,f12); stfs(m,r31+84,f10); stfs(m,r31+120,f10);
    r30+=64; r31+=144;
  } while(r29);
  return m.Read(S+128,uint32_t(r28)*144);
}
// sub_821A8090 (recomp.45.cpp:7919). r29 = points, r27 = colour, f25 = width.
std::vector<uint8_t> T_Strip(const Reader& m,uint32_t r29,uint32_t r26,uint32_t r27,double f25,uint32_t S=kScratch) {
  if(int32_t(r26)<2) return {};
  if(int32_t(r26)>100) r26=100;
  double f0=lfs(m,r29),f13=lfs(m,r29+4); uint32_t r30=r29+8; double f12=lfs(m,r29+32),f11=lfs(m,r29+36);
  f0=F(f12-f0); f13=F(f11-f13); stfs(m,S+96,f0); stfs(m,S+100,f13);
  const double f30=lfs(m,A::zero); uint32_t r28=0; f0=lfs(m,r29+40);
  copy_words(m,S+160,m.Word(0x8257C02C)+192,16);
  f13=lfs(m,r30); f0=F(f0-f13); stfs(m,S+120,f30); stfs(m,S+116,f30); stfs(m,S+104,f0);
  stfs(m,S+88,f30); stfs(m,S+84,f30); stfs(m,S+80,f30);
  const double f29=lfs(m,A::one); stfs(m,S+92,f29);
  const double f28=lfs(m,S+168); const uint32_t r23=uint32_t(-40)-r29; const double f27=lfs(m,S+164),f26=lfs(m,S+160);
  const uint32_t r25=r26-1; stfs(m,S+140,f29); uint32_t r31=S+180; stfs(m,S+156,f29); const uint32_t r24=uint32_t(-8)-r29;
  const double f31=lfs(m,A::half);
  do {
    uint32_t r11=r30+r24; if(r28==r25) r11=r30+r23; r11+=r29;
    f11=lfs(m,S+96); f13=lfs(m,r11+32); f0=lfs(m,r11); f0=F(f13-f0); f12=lfs(m,r11+36); f13=lfs(m,r11+4);
    stfs(m,S+128,f0); double f10=lfs(m,r11+40); f11=F(f0+f11); stfs(m,S+144,f11); f0=F(f12-f13); f12=lfs(m,S+100);
    stfs(m,S+132,f0); f13=lfs(m,r11+8); f11=F(f11*f31); f0=F(f0+f12); stfs(m,S+148,f0); f0=F(f10-f13); f12=lfs(m,S+104);
    stfs(m,S+136,f0); f13=lfs(m,r11); f10=lfs(m,r11+8); f0=F(f0+f12); stfs(m,S+152,f0);
    copy_words(m,S+112,S+144,16);
    f12=lfs(m,r11+4); f0=F(f13-f26); f13=F(f12-f27); f12=F(f10-f28);
    copy_words(m,S+96,S+128,16);
    f10=lfs(m,S+116); double f9=lfs(m,S+120); f10=F(f10*f31); f9=F(f9*f31); stfs(m,S+116,f10); stfs(m,S+120,f9);
    double f6=F(f13*f11),f8=F(f12*f10),f7=F(f9*f0);
    f0=fmsubs(f10,f0,f6); stfs(m,S+88,f0); f13=fmsubs(f13,f9,f8); stfs(m,S+80,f13); f13=fmsubs(f12,f11,f7); stfs(m,S+84,f13);
    T_SetLength(m,S+80,f25);
    f0=lfs(m,r30-8); f8=lfs(m,S+80); f7=F(f8+f0); stfs(m,r31-4,f7); f13=lfs(m,r30-4); f0=F(f0-f8); f7=lfs(m,S+84);
    f6=F(f13+f7); stfs(m,r31,f6); f12=lfs(m,r30); f6=lfs(m,S+88); stfs(m,r31+32,f0); f0=F(f13-f7); stfs(m,r31+36,f0);
    const double f5=F(f12+f6); f11=lfs(m,r27); f0=F(f12-f6); f10=lfs(m,r27+4); f9=lfs(m,r27+8);
    stfs(m,r31+4,f5); stfs(m,r31+40,f0); ++r28; f0=lfs(m,r30+12); r30+=32; f13=lfs(m,r27+12);
    stfs(m,r31+52,f11); stfs(m,r31+16,f11); stfs(m,r31+56,f10); stfs(m,r31+20,f10); stfs(m,r31+60,f9); stfs(m,r31+24,f9);
    stfs(m,r31+8,f30); stfs(m,r31+44,f29); stfs(m,r31+12,f0); stfs(m,r31+64,f13); stfs(m,r31+28,f13); stfs(m,r31+48,f0);
    r31+=72;
  } while(r28!=r26);
  return m.Read(S+176,r26*72);
}
// One producer call as the transcriptions see it.
struct Call {
  bool ribbon=false; uint32_t primitive=13;
  std::vector<uint8_t> vertices;
  uint32_t texture=0; int32_t blend=0; uint32_t technique=0,depth_flag=0;
};
void Particles(std::vector<Call>& calls,const Reader& m,uint32_t records,uint32_t count,uint32_t texture,int32_t blend) {
  auto bytes=T_Expand(m,records,count);
  if(!bytes.empty()) calls.push_back({false,13,std::move(bytes),texture,blend,0,0});
}
// The stack pre-fill every fixed-count builder performs: position 0, +12 one,
// colour one, radius/angle untouched.
void Prefill(const Reader& m,uint32_t base,uint32_t records) {
  for(uint32_t i=0;i<records;++i) {
    const uint32_t r=base+i*48;
    for(uint32_t o:{0u,4u,8u}) stfs(m,r+o,lfs(m,A::zero));
    for(uint32_t o:{12u,16u,20u,24u,28u}) stfs(m,r+o,lfs(m,A::one));
    for(uint32_t o:{32u,36u,40u,44u}) m.StoreWord(r+o,0xCDCDCDCD);  // uninitialized stack
  }
}
// clAcidAmmo01 slot 4 (recomp.19.cpp:3396).
std::vector<Call> T_Acid(const Reader& m,uint32_t r31,uint32_t r1=kStack) {
  copy_words(m,r1+80,r31+896,16);
  const double f31=lfs(m,r31+676); T_SetLength(m,r1+80,F(f31*lfs(m,A::quarter)));
  Prefill(m,r1+96,8);
  copy_words(m,r1+96,r31+912,16); copy_words(m,r1+112,r31+656,16);
  const double f12=lfs(m,r1+84),f11=lfs(m,r1+80); stfs(m,r1+132,lfs(m,A::zero)); const double f13=lfs(m,r1+88);
  stfs(m,r1+128,f31); const double f0=lfs(m,A::three_quarters);
  uint32_t r11=r1+148;
  for(uint32_t r6=7;r6;--r6) {
    const uint32_t r8=r11-4,r10=r11-52; copy_words(m,r8,r10,48);
    double f10=lfs(m,r11+28); f10=F(f10*f0); stfs(m,r11+28,f10);
    f10=lfs(m,r11); double f9=lfs(m,r8); f10=F(f10-f12); stfs(m,r11,f10); f9=F(f9-f11);
    const double f8=lfs(m,r11+4); f10=F(f8-f13); stfs(m,r8,f9); stfs(m,r11+4,f10);
    r11+=48;
  }
  std::vector<Call> calls; Particles(calls,m,r1+96,8,m.Word(r31+880),0); return calls;
}
// clBeamAmmo01 slot 4 (recomp.10.cpp:3271).
std::vector<Call> T_Beam(const Reader& m,uint32_t r31,uint32_t r1=kStack) {
  copy_words(m,r1+80,r31+480,16);
  const double f31=lfs(m,r31+676); T_SetLength(m,r1+80,F(f31*lfs(m,A::quarter)));
  Prefill(m,r1+96,8);
  copy_words(m,r1+96,r31+544,16); copy_words(m,r1+112,r31+656,16);
  const double f0=lfs(m,r1+88),f12=lfs(m,r1+80); stfs(m,r1+132,lfs(m,A::zero)); const double f13=lfs(m,r1+84);
  stfs(m,r1+128,f31);
  uint32_t r11=r1+148;
  for(uint32_t r6=7;r6;--r6) {
    const uint32_t r8=r11-4,r10=r11-52; copy_words(m,r8,r10,48);
    double f11=lfs(m,r11); const double f10=lfs(m,r8); f11=F(f11+f13); stfs(m,r11,f11);
    f11=F(f12+f10); stfs(m,r8,f11); const uint32_t z=r11+4; r11+=48;
    f11=lfs(m,z); f11=F(f11+f0); stfs(m,z,f11);
  }
  std::vector<Call> calls; Particles(calls,m,r1+96,8,m.Word(r31+880),1); return calls;
}
// clRocketAmmo02 slot 4 (recomp.53.cpp:3345) and clSolidAmmo01 (recomp.40.cpp:3420).
std::vector<Call> T_Twelve(const Reader& m,uint32_t r31,bool solid,uint32_t r1=kStack) {
  copy_words(m,r1+112,r31+496,64);
  const double f31=lfs(m,r31+676);
  const double f30=F(f31*lfs(m,solid?A::half:A::fifth));
  copy_words(m,r1+80,r31+480,16); T_SetLength(m,r1+80,F(f31*lfs(m,A::quarter)));
  Prefill(m,r1+176,12);
  const double f12=lfs(m,r1+84),f11=lfs(m,r1+80),f10=F(f30-f31);
  copy_words(m,r1+176,r1+160,16); copy_words(m,r1+192,r31+656,16);
  stfs(m,r1+212,lfs(m,A::zero));
  const double f13=lfs(m,r1+88),f0=lfs(m,A::twelfth); stfs(m,r1+208,solid?f30:f31);
  uint32_t r11=r1+228; int32_t r6=solid?11:1;
  do {
    const uint32_t r8=r11-4,r10=r11-52; copy_words(m,r8,r10,48);
    double f9=double(int64_t(r6)); r6+=solid?-1:1;
    f9=F(f9); f9=F(f9*f0); f9=fmadds(f9,f10,f31); stfs(m,r11+28,f9);
    f9=lfs(m,r11); double f8=lfs(m,r8); f9=solid?F(f9+f12):F(f9-f12); const double f7=lfs(m,r11+4);
    f8=solid?F(f8+f11):F(f8-f11); stfs(m,r11,f9); f9=solid?F(f7+f13):F(f7-f13); stfs(m,r8,f8);
    const uint32_t z=r11+4; r11+=48; stfs(m,z,f9);
  } while(solid?r6!=0:r6!=12);
  std::vector<Call> calls; Particles(calls,m,r1+176,12,m.Word(r31+(solid?884:908)),1); return calls;
}
// clLaserAmmo01 slot 4 (recomp.37.cpp:3422).
std::vector<Call> T_Laser(const Reader& m,uint32_t r31,uint32_t r1=kStack) {
  const double f31=lfs(m,A::zero),f30=lfs(m,A::one);
  stfs(m,r1+152,f31); stfs(m,r1+148,f31); stfs(m,r1+144,f31); stfs(m,r1+184,f31); stfs(m,r1+180,f31);
  stfs(m,r1+156,f30); stfs(m,r1+176,f31); stfs(m,r1+188,f30);
  copy_words(m,r1+144,r31+944,16);
  const double width=F(lfs(m,r31+676)*lfs(m,r31+936)),half=lfs(m,A::half);
  stfs(m,r1+160,half); stfs(m,r1+164,half);
  copy_words(m,r1+176,r31+960,16);
  stfs(m,r1+192,half); stfs(m,r1+196,half);
  std::vector<Call> calls;
  auto ribbon=T_Segments(m,r1+144,2,r31+656,width);
  if(!ribbon.empty()) calls.push_back({true,13,std::move(ribbon),m.Word(r31+880),1,2,0});
  if(m.Byte(r31+976)) {
    double f0=lfs(m,r31+676),f13=lfs(m,r31+936); f13=F(f0*f13);
    const double five=lfs(m,A::five);
    for(uint32_t i=0;i<4;++i) stfs(m,r1+80+i*4,F(lfs(m,r31+656+i*4)*five));
    stfs(m,r1+104,f31); stfs(m,r1+100,f31); stfs(m,r1+96,f31); stfs(m,r1+108,f30);
    for(uint32_t o:{124u,120u,116u,112u}) stfs(m,r1+o,f30);
    f0=F(f13*five);
    copy_words(m,r1+96,r31+912,16); copy_words(m,r1+112,r1+80,16);
    stfs(m,r1+132,f31); stfs(m,r1+128,f0);
    Particles(calls,m,r1+96,1,m.Word(r31+880),1);
  }
  return calls;
}
// clWebAmmo01 slot 4 (recomp.73.cpp:3535).
std::vector<Call> T_Web(const Reader& m,uint32_t r31,uint32_t r1=kStack) {
  const double f31=lfs(m,A::one);
  for(uint32_t o:{80u,84u,88u,92u}) stfs(m,r1+o,f31);
  std::vector<Call> calls;
  auto ribbon=T_Strip(m,m.Word(r31+784),m.Word(r31+792),r1+80,F(lfs(m,r31+484)*lfs(m,r31+780)));
  if(!ribbon.empty()) calls.push_back({true,6,std::move(ribbon),m.Word(r31+796),0,2,0});
  const uint32_t r10=(m.Word(r31+792)<<5)+m.Word(r31+784)-32;
  const double f12=F(lfs(m,r31+484)*lfs(m,r31+780)),f0=lfs(m,A::zero);
  stfs(m,r1+104,f0); stfs(m,r1+100,f0); stfs(m,r1+96,f0);
  copy_words(m,r1+96,r10,16);
  for(uint32_t o:{112u,116u,120u,124u}) stfs(m,r1+o,f31);
  stfs(m,r1+132,f0); stfs(m,r1+128,F(f12*lfs(m,A::web_scale)));
  Particles(calls,m,r1+96,1,m.Word(r31+812),0);
  return calls;
}
// clEffectEtc02 slot 4 (recomp.9.cpp, 8217C4A0), including the +612 store.
std::vector<Call> T_Etc02(const Reader& m,uint32_t r3,uint32_t r1=kStack) {
  const uint32_t r11=m.Word(r3+612)-1; int32_t r6=int32_t(m.Word(r3+544)); m.StoreWord(r3+612,r11);
  if(r6>32) r6=32;
  if(r6>0) {
    const double f0=lfs(m,r3+604),f13=lfs(m,r3+592),f12=lfs(m,r3+596),f11=lfs(m,r3+600);
    uint32_t r10=m.Word(r3+640),r9=r1+96,w=r1+84; int32_t r8=r6;
    const double f10=lfs(m,r3+504),f9=lfs(m,r3+508),f8=lfs(m,r3+500),f7=lfs(m,r3+512);
    do {
      --r8;
      for(uint32_t v=0;v<4;++v) {
        for(uint32_t c=0;c<3;++c) stfs(m,w-4+v*36+c*4,lfs(m,r10+v*16+c*4));
        stfs(m,w+28+v*36,f0); stfs(m,w+16+v*36,f13); stfs(m,w+20+v*36,f12); stfs(m,w+24+v*36,f11);
      }
      r10+=64; w+=144;
      stfs(m,r9-4,f8); stfs(m,r9,f10); stfs(m,r9+32,f9); stfs(m,r9+36,f10);
      stfs(m,r9+68,f9); stfs(m,r9+72,f7); stfs(m,r9+104,f8); stfs(m,r9+108,f7);
      r9+=144;
    } while(r8);
  }
  std::vector<Call> calls;
  if(r6>0) calls.push_back({true,13,m.Read(r1+80,uint32_t(r6)*144),m.Word(r3+528),m.Byte(0x82554BF0)?1:0,2,0});
  return calls;
}
// clSpark01 slot 4 (recomp.71.cpp:3697): b 821A8090.
std::vector<Call> T_Spark01(const Reader& m,uint32_t r11) {
  std::vector<Call> calls;
  auto ribbon=T_Strip(m,m.Word(r11+544),m.Word(r11+556),r11+512,lfs(m,r11+440));
  if(!ribbon.empty()) calls.push_back({true,6,std::move(ribbon),m.Word(r11+528),1,2,0});
  return calls;
}
// clMuzzleFlash slot 4 (recomp.49.cpp:6665): two 821A8628 calls.
std::vector<Call> T_Muzzle(const Reader& m,uint32_t r31) {
  std::vector<Call> calls;
  const uint32_t r30=r31+480;
  uint32_t r11=m.Word(r31+384);
  auto first=T_Segments(m,m.Word(r31+464),m.Word(r31+424)<<1,r30,lfs(m,r11+20));
  if(!first.empty()) calls.push_back({true,13,std::move(first),m.Word(r31+396),1,2,0});
  r11=m.Word(r31+424)<<6;
  const uint32_t r4=m.Word(r31+384),r6=m.Word(r31+464);
  auto second=T_Segments(m,r6+r11,2,r30,lfs(m,r4+r11+20));
  if(!second.empty()) calls.push_back({true,13,std::move(second),m.Word(r31+396),1,2,0});
  return calls;
}
// sub_821A88E8 (recomp.25.cpp:7724), loc_821A8994. r4 = 48-byte points, r5 count, S = r1.
std::vector<uint8_t> T_ColourSegments(const Reader& m,uint32_t r4,uint32_t r5,uint32_t S=kScratch) {
  if(int32_t(r5)<2) return {};
  int32_t r28=int32_t(r5)>>1; if(int32_t(r5)<0 && (r5&1)) ++r28;  // srawi/addze
  if(r28>100) r28=100;
  copy_words(m,S+96,m.Word(0x8257C02C)+192,16);
  const double f31=lfs(m,A::zero); stfs(m,S+88,f31); stfs(m,S+84,f31); stfs(m,S+80,f31);
  const double f30=lfs(m,A::one); stfs(m,S+92,f30);
  if(!r28) return {};
  const double f29=lfs(m,S+104); uint32_t r31=S+116; const double f28=lfs(m,S+100); uint32_t r30=r4+52;
  const double f27=lfs(m,S+96); int32_t r29=r28;
  do {
    double f12=lfs(m,r30-44),f0=lfs(m,r30-52); double f9=F(f12-f29); double f6=lfs(m,r30+4); double f11=F(f0-f27);
    double f13=lfs(m,r30-48); f12=F(f6-f12); double f8=lfs(m,r30); double f10=F(f13-f28); f13=F(f8-f13);
    double f7=lfs(m,r30-4); f0=F(f7-f0); const double f1=lfs(m,r30-20);
    f7=F(f12*f11); f8=F(f13*f9); f6=F(f10*f0);
    f0=fmsubs(f9,f0,f7); stfs(m,S+84,f0); f12=fmsubs(f12,f10,f8); stfs(m,S+80,f12); f0=fmsubs(f13,f11,f6); stfs(m,S+88,f0);
    T_SetLength(m,S+80,f1);
    f0=lfs(m,S+80); f11=lfs(m,r30-52); --r29; f13=F(f11+f0); stfs(m,r31-4,f13);
    f13=lfs(m,S+84); f11=F(f11-f0); f10=lfs(m,r30-48); f12=F(f10+f13); stfs(m,r31+32,f11); stfs(m,r31,f12);
    f11=F(f10-f13); f12=lfs(m,S+88); f9=lfs(m,r30-44); stfs(m,r31+36,f11); f11=F(f9-f12); f7=lfs(m,r30-4);
    const double f2=F(f9+f12); stfs(m,r31+40,f11); f11=F(f7-f0); f6=lfs(m,r30); f0=F(f7+f0); stfs(m,r31+68,f11);
    f11=F(f6-f13); stfs(m,r31+104,f0); f0=F(f6+f13); const double f5=lfs(m,r30+4); stfs(m,r31+72,f11);
    f11=F(f5-f12); stfs(m,r31+108,f0); f0=F(f5+f12); f8=lfs(m,r30-12); stfs(m,r31+12,f8); stfs(m,r31+48,f8);
    stfs(m,r31+76,f11); stfs(m,r31+112,f0);
    const double f4=lfs(m,r30+36),f3=lfs(m,r30-36); f0=lfs(m,r30-32); f13=lfs(m,r30-28); f12=lfs(m,r30-24);
    f11=lfs(m,r30+12); f10=lfs(m,r30+16); f9=lfs(m,r30+20); f8=lfs(m,r30+24); r30+=96;
    stfs(m,r31+8,f31); stfs(m,r31+44,f30); stfs(m,r31+80,f30); stfs(m,r31+116,f31); stfs(m,r31+4,f2);
    stfs(m,r31+84,f4); stfs(m,r31+120,f4); stfs(m,r31+52,f3); stfs(m,r31+16,f3); stfs(m,r31+56,f0); stfs(m,r31+20,f0);
    stfs(m,r31+60,f13); stfs(m,r31+24,f13); stfs(m,r31+64,f12); stfs(m,r31+28,f12);
    stfs(m,r31+124,f11); stfs(m,r31+88,f11); stfs(m,r31+128,f10); stfs(m,r31+92,f10);
    stfs(m,r31+132,f9); stfs(m,r31+96,f9); stfs(m,r31+136,f8); stfs(m,r31+100,f8);
    r31+=144;
  } while(r29);
  return m.Read(S+112,uint32_t(r28)*144);
}
// sub_821A8360 (recomp.70.cpp:7619), loc_821A8458. r29 = 48-byte points, r27 count.
std::vector<uint8_t> T_ColourStrip(const Reader& m,uint32_t r29,uint32_t r27,uint32_t S=kScratch) {
  if(int32_t(r27)<2) return {};
  if(int32_t(r27)>100) r27=100;
  double f12=lfs(m,r29+48),f0=lfs(m,r29); const uint32_t r30_start=r29+4; f0=F(f12-f0); stfs(m,S+96,f0);
  uint32_t r28=0; double f11=lfs(m,r29+52),f13=lfs(m,r29+8); const double f30=lfs(m,A::zero);
  f0=lfs(m,r30_start); f12=lfs(m,r29+56); f0=F(f11-f0); stfs(m,S+100,f0); f0=F(f12-f13);
  stfs(m,S+120,f30); stfs(m,S+116,f30); stfs(m,S+104,f0);
  copy_words(m,S+160,m.Word(0x8257C02C)+192,16);
  stfs(m,S+88,f30); stfs(m,S+84,f30); stfs(m,S+80,f30);
  const double f29=lfs(m,A::one); stfs(m,S+92,f29);
  const double f28=lfs(m,S+168); const uint32_t r24=uint32_t(-52)-r29; const double f27=lfs(m,S+164),f26=lfs(m,S+160);
  const uint32_t r26=r27-1; stfs(m,S+140,f29); uint32_t r31=S+180; stfs(m,S+156,f29); const uint32_t r25=uint32_t(-4)-r29;
  const double f31=lfs(m,A::half);
  uint32_t r30=r30_start;
  do {
    uint32_t r11=r25+r30; if(r28==r26) r11=r24+r30; r11+=r29;
    f11=lfs(m,S+96); const double f1=lfs(m,r30+28); f13=lfs(m,r11+48); f0=lfs(m,r11); f0=F(f13-f0); f12=lfs(m,r11+52);
    f13=lfs(m,r11+4); stfs(m,S+128,f0); double f10=lfs(m,r11+56); f11=F(f0+f11); stfs(m,S+144,f11); f0=F(f12-f13);
    f12=lfs(m,S+100); stfs(m,S+132,f0); f13=lfs(m,r11+8); f11=F(f11*f31); f0=F(f0+f12); stfs(m,S+148,f0);
    f0=F(f10-f13); f12=lfs(m,S+104); stfs(m,S+136,f0); f13=lfs(m,r11); f10=lfs(m,r11+8); f0=F(f0+f12); stfs(m,S+152,f0);
    copy_words(m,S+112,S+144,16);
    f12=lfs(m,r11+4); f0=F(f13-f26); f13=F(f12-f27); f12=F(f10-f28);
    copy_words(m,S+96,S+128,16);
    f10=lfs(m,S+116); double f9=lfs(m,S+120); f10=F(f10*f31); f9=F(f9*f31); stfs(m,S+116,f10); stfs(m,S+120,f9);
    double f6=F(f13*f11),f8=F(f12*f10),f7=F(f9*f0);
    f0=fmsubs(f10,f0,f6); stfs(m,S+88,f0); f13=fmsubs(f13,f9,f8); stfs(m,S+80,f13); f13=fmsubs(f12,f11,f7); stfs(m,S+84,f13);
    T_SetLength(m,S+80,f1);
    f0=lfs(m,r30-4); f8=lfs(m,S+80); f7=F(f0+f8); stfs(m,r31-4,f7); f13=lfs(m,r30); f0=F(f0-f8); f7=lfs(m,S+84);
    f6=F(f13+f7); stfs(m,r31,f6); f12=lfs(m,r30+4); f6=lfs(m,S+88); stfs(m,r31+32,f0); f0=F(f13-f7); stfs(m,r31+36,f0);
    const double f5=F(f12+f6); f11=lfs(m,r30+12); f0=F(f12-f6); f10=lfs(m,r30+16); f9=lfs(m,r30+20);
    stfs(m,r31+4,f5); stfs(m,r31+40,f0); ++r28; f0=lfs(m,r30+24); f13=lfs(m,r30+36); r30+=48;
    stfs(m,r31+52,f11); stfs(m,r31+16,f11); stfs(m,r31+56,f10); stfs(m,r31+20,f10); stfs(m,r31+60,f9); stfs(m,r31+24,f9);
    stfs(m,r31+8,f30); stfs(m,r31+44,f29); stfs(m,r31+64,f0); stfs(m,r31+28,f0); stfs(m,r31+12,f13); stfs(m,r31+48,f13);
    r31+=72;
  } while(r28!=r27);
  return m.Read(S+176,r27*72);
}
// clSpark02 slot 4 (recomp.15.cpp:3685): b 821A88E8(r4 +536, r5 +544, r6 +472, r7 1, r8 0).
std::vector<Call> T_Spark02(const Reader& m,uint32_t r11) {
  std::vector<Call> calls;
  auto ribbon=T_ColourSegments(m,m.Word(r11+536),m.Word(r11+544));
  if(!ribbon.empty()) calls.push_back({true,13,std::move(ribbon),m.Word(r11+472),1,2,0});
  return calls;
}
// clSmokeLine slot 4 (recomp.78.cpp:3646): bltlr on +620 < 2 unsigned, then
// b 821A8360(r4 +604, r5 +620, r6 +392, r7 +424, r8 0).
std::vector<Call> T_SmokeLine(const Reader& m,uint32_t r11) {
  std::vector<Call> calls;
  const uint32_t r5=m.Word(r11+620);
  if(r5<2) return calls;
  auto ribbon=T_ColourStrip(m,m.Word(r11+604),r5);
  if(!ribbon.empty()) calls.push_back({true,6,std::move(ribbon),m.Word(r11+392),int32_t(m.Word(r11+424)),2,0});
  return calls;
}
// sub_8217EA40 (recomp.80.cpp:6377): one quad at r1+96, 821A7C70(r4 13, r6 1, r7 r5, r8 0, r9 0).
Call T_Etc01Entry(const Reader& m,uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r1) {
  stfs(m,r1+80,lfs(m,r6)); stfs(m,r1+84,lfs(m,r6+4)); stfs(m,r1+88,lfs(m,r6+8)); stfs(m,r1+92,lfs(m,r6+12));
  stfs(m,r1+96,lfs(m,r4)); stfs(m,r1+100,lfs(m,r4+4)); stfs(m,r1+104,lfs(m,r4+8));
  stfs(m,r1+132,lfs(m,r4+16)); stfs(m,r1+136,lfs(m,r4+20));
  const double f0=lfs(m,A::zero);
  stfs(m,r1+172,lfs(m,r4+36)); stfs(m,r1+140,lfs(m,r4+24)); stfs(m,r1+176,lfs(m,r4+40));
  stfs(m,r1+108,f0); stfs(m,r1+112,f0); stfs(m,r1+168,lfs(m,r4+32)); stfs(m,r1+148,f0);
  stfs(m,r1+204,lfs(m,r4+48)); stfs(m,r1+216,f0);
  const double f13=lfs(m,A::one);
  stfs(m,r1+144,f13); stfs(m,r1+180,f13); stfs(m,r1+184,f13);
  stfs(m,r1+208,lfs(m,r4+52)); stfs(m,r1+212,lfs(m,r4+56)); stfs(m,r1+220,f13);
  for(uint32_t o:{116u,152u,188u,224u}) copy_words(m,r1+o,r1+80,16);
  return {true,13,m.Read(r1+96,144),r5,0,2,0};
}
// clEffectEtc01 slot 4 (recomp.29.cpp:6570): 8217EA40 per entry while i < +612.
std::vector<Call> T_Etc01(const Reader& m,uint32_t r31,uint32_t r1=kStack) {
  std::vector<Call> calls;
  if(int32_t(m.Word(r31+612))<=0) return calls;
  const uint32_t r28=r31+656; uint32_t r30=0; int32_t r29=0;
  do {
    const uint32_t r11=m.Word(r31+704)+r30;
    calls.push_back(T_Etc01Entry(m,r11+64,m.Word(r31+596),r28,r1));
    ++r29; r30+=224;
  } while(r29<int32_t(m.Word(r31+612)));
  return calls;
}
std::vector<Call> Transcribe(const Reader& m,uint32_t object,NativeEffectClass type) {
  std::vector<Call> calls;
  const auto w=[&](uint32_t o) { return m.Word(object+o); };
  switch(type) {
    case NativeEffectClass::Particle01Limit: Particles(calls,m,w(396),w(404),w(424),int32_t(w(444))); break;
    case NativeEffectClass::Particle02: if(w(620)) Particles(calls,m,w(604),w(620),w(392),int32_t(w(420))); break;
    case NativeEffectClass::Glass: Particles(calls,m,w(700),w(684),w(596),0); break;
    case NativeEffectClass::RocketAmmo01: Particles(calls,m,w(896),w(924)<=w(888)?w(924):w(888),w(908),1); break;
    case NativeEffectClass::AcidAmmo01: return T_Acid(m,object);
    case NativeEffectClass::BeamAmmo01: return T_Beam(m,object);
    case NativeEffectClass::RocketAmmo02: return T_Twelve(m,object,false);
    case NativeEffectClass::SolidAmmo01: return T_Twelve(m,object,true);
    case NativeEffectClass::LaserAmmo01: return T_Laser(m,object);
    case NativeEffectClass::WebAmmo01: return T_Web(m,object);
    case NativeEffectClass::EffectEtc02: return T_Etc02(m,object);
    case NativeEffectClass::Spark01: return T_Spark01(m,object);
    case NativeEffectClass::MuzzleFlash: return T_Muzzle(m,object);
    case NativeEffectClass::Spark02: return T_Spark02(m,object);
    case NativeEffectClass::EffectEtc01: return T_Etc01(m,object);
    case NativeEffectClass::SmokeLine: return T_SmokeLine(m,object);
    default: break;
  }
  return calls;
}
// --- Fixture ------------------------------------------------------------------
std::mt19937 rng(20260922);
float Random(float lo,float hi) { return std::uniform_real_distribution<float>(lo,hi)(rng); }
Reader MakeMemory() {
  Reader m;
  const std::pair<uint32_t,float> constants[]={{A::zero,0.f},{A::one,1.f},{A::half,.5f},{A::quarter,.25f},
    {A::three_quarters,.75f},{A::fifth,.2f},{A::twelfth,0.0833333358f},{A::five,5.f},{A::web_scale,2.6f},
    {A::half_pi,1.57079637f},{A::pi,3.14159274f},{A::three_half_pi,4.71238899f}};
  for(const auto& [at,value]:constants) m.StoreFloat(at,value);
  m.StoreWord(kNativeEffectShaderGlobal,kEffect);
  m.StoreWord(kNativeEffectCameraGlobal,kCamera);
  m.StoreFloat(kCamera+192,Random(-500,500)); m.StoreFloat(kCamera+196,Random(-50,50)); m.StoreFloat(kCamera+200,Random(-500,500));
  m.StoreByte(0x82554BF0,1);
  m.Map(kStack); m.Map(kScratch);
  return m;
}
void FillFloats(const Reader& m,uint32_t at,uint32_t bytes,float lo,float hi) {
  for(uint32_t i=0;i<bytes;i+=4) m.StoreFloat(at+i,Random(lo,hi));
}
constexpr uint32_t kObject=0x50000000,kArray=0x52000000,kTables=0x51000000;
uint32_t Records(const Reader& m,uint32_t count,uint32_t at=kArray) {
  FillFloats(m,at,count*48,-300,300);
  return at;
}
void FillObject(const Reader& m,uint32_t object,uint32_t slot4) {
  FillFloats(m,object,4096,-40,40);
  const uint32_t table=kTables+((slot4>>2)&0xFF0);
  m.StoreWord(object,table); m.StoreWord(table+16,slot4);
}
void Compare(const std::vector<NativeEffectDraw>& draws,const std::vector<Call>& calls,const std::string& name) {
  Require(draws.size()==calls.size(),name+": draw count "+std::to_string(draws.size())+" vs "+std::to_string(calls.size()));
  for(size_t i=0;i<draws.size();++i) {
    const auto& d=draws[i]; const auto& c=calls[i];
    const auto what=name+" draw "+std::to_string(i);
    Require((d.kind!=NativeEffectDraw::Kind::Particles)==c.ribbon && d.primitive()==c.primitive,what+": kind");
    Require(d.texture==c.texture && d.blend==c.blend,what+": texture/blend");
    Require(d.effect==kEffect,what+": effect object");
    Require(c.ribbon?(d.technique==NativeEffectTechnique::Ribbon && d.sets_depth_write && d.depth_write==(c.depth_flag==1))
                    :(d.technique==NativeEffectTechnique::Particle && !d.sets_depth_write),what+": technique");
    const auto bytes=EncodeNativeEffectVertices(d,0,d.vertex_count());
    Require(bytes.size()==c.vertices.size(),what+": vertex bytes "+std::to_string(bytes.size())+" vs "+std::to_string(c.vertices.size()));
    for(size_t b=0;b<bytes.size();b+=4)
      Require(std::memcmp(&bytes[b],&c.vertices[b],4)==0,what+": word "+std::to_string(b/4)+" differs");
  }
}
void CheckClass(const Reader& m,uint32_t slot4,const std::string& name) {
  const auto type=ClassifyNativeEffect(slot4);
  Require(type!=NativeEffectClass::Unknown,name+": unclassified");
  const auto inputs=ReadNativeEffectInputs(m);
  const auto draws=BuildNativeEffectDraws(m,kObject,type,inputs);
  Compare(draws,Transcribe(m,kObject,type),name);
}
// --- Builders against their transcriptions ------------------------------------
void TestBuilders() {
  for(int round=0;round<24;++round) {
    auto m=MakeMemory();
    const auto tag=" (round "+std::to_string(round)+")";
    // Array classes.
    FillObject(m,kObject,0x8211D250);
    const uint32_t big=round==0?1200:round%5;  // 1200 = two DrawPrimitiveUP calls
    m.StoreWord(kObject+396,Records(m,big)); m.StoreWord(kObject+404,big);
    m.StoreWord(kObject+424,0x1000+round); m.StoreWord(kObject+444,uint32_t(round%3));
    CheckClass(m,0x8211D250,"clParticle01_Limit"+tag);
    FillObject(m,kObject,0x8211DB70);
    m.StoreWord(kObject+604,Records(m,7)); m.StoreWord(kObject+620,round%4==0?0:7);
    m.StoreWord(kObject+392,0x2000); m.StoreWord(kObject+420,1);
    CheckClass(m,0x8211DB70,"clParticle02"+tag);
    FillObject(m,kObject,0x8217D6E0);
    m.StoreWord(kObject+700,Records(m,5)); m.StoreWord(kObject+684,5); m.StoreWord(kObject+596,0x3000);
    CheckClass(m,0x8217D6E0,"clEffectGlass"+tag);
    FillObject(m,kObject,0x82119A10);
    m.StoreWord(kObject+896,Records(m,9)); m.StoreWord(kObject+924,uint32_t(round%12)); m.StoreWord(kObject+888,6);
    m.StoreWord(kObject+908,0x4000);
    CheckClass(m,0x82119A10,"clRocketAmmo01"+tag);
    // Fixed-count classes over random object fields.
    struct Fixed { uint32_t slot; const char* name; uint32_t texture; };
    const Fixed fixed[]={{0x82113308,"clAcidAmmo01",880},{0x82114A98,"clBeamAmmo01",880},
                         {0x8211A0E8,"clRocketAmmo02",908},{0x8211B088,"clSolidAmmo01",884}};
    for(const auto& f:fixed) {
      FillObject(m,kObject,f.slot);
      m.StoreWord(kObject+f.texture,0x5000);
      if(round==1) for(uint32_t o:{480u,484u,488u,896u,900u,904u}) m.StoreFloat(kObject+o,0.f);  // zero-length step
      CheckClass(m,f.slot,std::string(f.name)+tag);
    }
    FillObject(m,kObject,0x82117CD0);
    m.StoreWord(kObject+880,0x6000); m.StoreWord(kObject+976,round%2?0x01000000u:0u);
    if(round==2) for(uint32_t o:{960u,964u,968u}) m.StoreWord(kObject+o,m.Word(kObject+o-16));  // zero-length segment
    CheckClass(m,0x82117CD0,"clLaserAmmo01"+tag);
    FillObject(m,kObject,0x8211BB80);
    const uint32_t points=round==3?150:round==4?1:2+uint32_t(round)*3;  // clamp to 100; a single point skips the strip
    FillFloats(m,kArray,points*32,-200,200);
    m.StoreWord(kObject+784,kArray); m.StoreWord(kObject+792,points);
    m.StoreWord(kObject+796,0x7000); m.StoreWord(kObject+812,0x7100);
    CheckClass(m,0x8211BB80,"clWebAmmo01"+tag);
    FillObject(m,kObject,0x8217C4A0);
    const int32_t quads=round==5?-3:round==6?40:round%9;
    m.StoreWord(kObject+544,uint32_t(quads)); FillFloats(m,kArray,32*64,-200,200);
    m.StoreWord(kObject+640,kArray); m.StoreWord(kObject+528,0x8000); m.StoreWord(kObject+612,5);
    m.StoreByte(0x82554BF0,uint8_t(round%2));
    CheckClass(m,0x8217C4A0,"clEffectEtc02"+tag);
    FillObject(m,kObject,0x8211E7A0);
    const uint32_t sparks=round==3?150:round==4?1:round==5?0xFFFFFFFEu:2+uint32_t(round)*3;  // cap, too few, negative
    FillFloats(m,kArray,std::min<uint32_t>(sparks,150)*32,-200,200);
    m.StoreWord(kObject+544,kArray); m.StoreWord(kObject+556,sparks); m.StoreWord(kObject+528,0x9000);
    CheckClass(m,0x8211E7A0,"clSpark01"+tag);
    FillObject(m,kObject,0x821897A8);
    const uint32_t pairs=round==3?150:round==4?0:1+uint32_t(round%6);  // 150 pairs: the first call caps at 100
    constexpr uint32_t kWidths=0x53000000;
    FillFloats(m,kArray,(pairs+1)*64,-200,200); FillFloats(m,kWidths,(pairs+1)*64,0.5f,8.f);
    m.StoreWord(kObject+464,kArray); m.StoreWord(kObject+384,kWidths); m.StoreWord(kObject+424,pairs);
    m.StoreWord(kObject+396,0x9100);
    if(round==2) for(uint32_t o:{32u,36u,40u}) m.StoreWord(kArray+o,m.Word(kArray+o-32));  // zero-length segment
    CheckClass(m,0x821897A8,"clMuzzleFlash"+tag);
    FillObject(m,kObject,0x8252B718);
    CheckClass(m,0x8252B718,"blr slot 4"+tag);
    // clSpark02: 48-byte points, per-point colour and per-pair width.
    FillObject(m,kObject,0x8211F540);
    const uint32_t spark_points=round==3?250:round==4?1:round==5?0xFFFFFFFEu:round==7?0x80000001u:2+uint32_t(round)*3;
    FillFloats(m,kArray,(spark_points>250?2:spark_points)*48,-200,200);
    if(round==2) for(uint32_t o:{48u,52u,56u}) m.StoreWord(kArray+o,m.Word(kArray+o-48));  // zero-length segment
    if(round==8) for(uint32_t o:{32u,128u}) m.StoreFloat(kArray+o,0.f);                   // zero width
    m.StoreWord(kObject+536,kArray); m.StoreWord(kObject+544,spark_points); m.StoreWord(kObject+472,0xA000+round);
    CheckClass(m,0x8211F540,"clSpark02"+tag);
    // clEffectEtc01: one quad per entry.
    FillObject(m,kObject,0x8217ECB8);
    const int32_t entries=round==5?-2:round==6?40:round%5;
    FillFloats(m,kArray,uint32_t(std::max(entries,0))*224+64,-200,200);
    m.StoreWord(kObject+704,kArray); m.StoreWord(kObject+612,uint32_t(entries)); m.StoreWord(kObject+596,0xB000+round);
    CheckClass(m,0x8217ECB8,"clEffectEtc01"+tag);
    // clSmokeLine: 48-byte strip points; the blend word is the object's.
    FillObject(m,kObject,0x82121848);
    const uint32_t smoke_points=round==3?150:round==4?1:round==5?0xFFFFFFFEu:round==6?0:2+uint32_t(round)*3;
    FillFloats(m,kArray,std::min<uint32_t>(smoke_points,150)*48,-200,200);
    if(round==2) for(uint32_t o:{48u,52u,56u}) m.StoreWord(kArray+o,m.Word(kArray+o-48));
    m.StoreWord(kObject+604,kArray); m.StoreWord(kObject+620,smoke_points);
    m.StoreWord(kObject+392,0xC000+round); m.StoreWord(kObject+424,uint32_t(round%3));
    CheckClass(m,0x82121848,"clSmokeLine"+tag);
  }
  // The three classes that had no builder now classify; nothing is named unsupported.
  Require(ClassifyNativeEffect(0x8211F540)==NativeEffectClass::Spark02 && ClassifyNativeEffect(0x8217ECB8)==NativeEffectClass::EffectEtc01 &&
          ClassifyNativeEffect(0x82121848)==NativeEffectClass::SmokeLine,"clSpark02, clEffectEtc01 and clSmokeLine classify");
  Require(!NativeEffectSlotName(0x8211F540) && !NativeEffectSlotName(0x82000000),"no slot is named unsupported");
  {
    // Their slot 4s write nothing but the stack: the object is unchanged.
    auto m=MakeMemory();
    for(const uint32_t slot:{0x8211F540u,0x8217ECB8u,0x82121848u}) {
      FillObject(m,kObject,slot);
      FillFloats(m,kArray,40*224,-200,200);
      for(uint32_t o:{536u,604u,704u}) m.StoreWord(kObject+o,kArray);
      m.StoreWord(kObject+544,20); m.StoreWord(kObject+612,3); m.StoreWord(kObject+620,20);
      const auto before=m.Read(kObject,4096);
      NativeEffectItem item; item.object=kObject; item.slot4=slot; item.type=ClassifyNativeEffect(slot);
      item.draws=BuildNativeEffectDraws(m,kObject,item.type,ReadNativeEffectInputs(m));
      CommitNativeEffectDraw(m,item);
      Require(!item.draws.empty() && m.Read(kObject,4096)==before,"slot 4 leaves the object as it was");
    }
  }
  {
    // A corrupt clEffectEtc01 count is refused rather than walked.
    auto m=MakeMemory();
    FillObject(m,kObject,0x8217ECB8); m.StoreWord(kObject+612,kNativeEffectEtc01EntryLimit+1);
    bool threw=false;
    try { BuildNativeEffectDraws(m,kObject,NativeEffectClass::EffectEtc01,ReadNativeEffectInputs(m)); } catch(const std::exception&) { threw=true; }
    Require(threw,"clEffectEtc01 entry count limit");
  }
}
// --- Technique material and sampler list (821A7640/821A7C70) --------------------
void TestTechniqueMaterial() {
  auto m=MakeMemory();
  const std::pair<NativeEffectTechnique,uint32_t> techniques[]={{NativeEffectTechnique::Particle,244},
    {NativeEffectTechnique::ZParticle,288},{NativeEffectTechnique::Ribbon,188}};
  for(const auto& [technique,offset]:techniques) {
    const auto tag=" (technique +"+std::to_string(offset)+")";
    const uint32_t list=kEffect+NativeEffectSamplerListOffset(technique),entries=0x45000000+offset*16,records=0x46000000+offset*64;
    // lwz r3,16(r30) / lwz r3,204(r30): the material is the word at +16.
    m.StoreWord(kEffect+offset,0xBAD0BAD0); m.StoreWord(kEffect+offset+16,0x47000000+offset);
    Require(NativeEffectTechniqueMaterial(m,kEffect,technique)==0x47000000+offset,"material is [technique+16]"+tag);
    // 821BC4C8: each listed record gets the texture at +4; nothing else moves.
    m.StoreWord(list+4,entries); m.StoreWord(list+12,3);
    for(uint32_t i=0;i<3;++i) {
      m.StoreWord(entries+i*4,records+i*28);
      m.StoreWord(records+i*28,0x11110000+i); m.StoreWord(records+i*28+4,0xDEAD); m.StoreWord(records+i*28+8,i);
    }
    m.StoreWord(records+3*28+4,0xBEEF);  // past the count
    BindNativeEffectTexture(m,kEffect,technique,0x1234+offset);
    for(uint32_t i=0;i<3;++i)
      Require(m.Word(records+i*28+4)==0x1234+offset && m.Word(records+i*28)==0x11110000+i && m.Word(records+i*28+8)==i,
              "texture stored in record "+std::to_string(i)+tag);
    Require(m.Word(records+3*28+4)==0xBEEF,"only the listed records"+tag);
    BindNativeEffectTexture(m,kEffect,technique,0);  // unconditional, as the guest's
    Require(m.Word(records+4)==0,"a null texture is stored too"+tag);
    m.StoreWord(list+12,0); BindNativeEffectTexture(m,kEffect,technique,0x55);
    Require(m.Word(records+4)==0,"an empty list writes nothing"+tag);
    m.StoreWord(list+12,kNativeEffectSamplerListLimit+1);
    bool threw=false;
    try { BindNativeEffectTexture(m,kEffect,technique,0x55); } catch(const std::exception&) { threw=true; }
    Require(threw,"a corrupt list count is refused"+tag);
  }
  // Ribbons set blend/depth before the activation, particles the blend after.
  const auto k=ReadNativeEffectConstants(m);
  Require(!MakeNativeParticleDraw(kEffect,{},0,1,0,k).state_before_activation() &&
          !MakeNativeParticleDraw(kEffect,{},0,1,1,k).state_before_activation(),"particle blend after activation");
  Require(MakeNativeRibbonDraw(kEffect,NativeEffectDraw::Kind::RibbonQuads,{},0,1,1).state_before_activation(),"ribbon state before activation");
}
// --- Filed effect items reach their record callbacks in draw order -------------
void TestTransparentRecording() {
  std::vector<NativeEffectItem> items(4);
  const uint16_t keys[]={300,100,5000,300};
  for(uint32_t i=0;i<4;++i) { items[i].key=keys[i]; items[i].order=10+i; items[i].object=0x50000000+i; }
  std::vector<uint32_t> ran;
  const auto effects=NativeEffectTransparentItems(items,[&](NativeBackendRecorder&,const NativeEffectItem& item) { ran.push_back(item.object); });
  const auto merged=MergeNativeTransparentItems({effects});
  alignas(std::max_align_t) unsigned char storage[64]{};  // the callbacks never touch the recorder
  RecordNativeTransparentItems(merged,*reinterpret_cast<NativeBackendRecorder*>(storage));
  Require(ran==std::vector<uint32_t>{0x50000002,0x50000000,0x50000003},"filed effects record by key, then filing order; key 100 drops");
}
// --- The four-vertex expansion and the call split -----------------------------
void TestExpansion() {
  auto m=MakeMemory();
  const auto k=ReadNativeEffectConstants(m);
  NativeParticleRecord record{{1,2,3},{.1f,.2f,.3f,.4f},7.5f,.3f};
  const auto v=ExpandNativeParticleRecords(std::span<const NativeParticleRecord>(&record,1),k);
  Require(v.size()==4,"four vertices per record");
  const std::array<std::array<float,2>,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
  const float angles[4]={.3f,float(double(.3f)+double(1.57079637f)),float(double(.3f)+double(3.14159274f)),float(double(.3f)+double(4.71238899f))};
  for(uint32_t i=0;i<4;++i) {
    Require(v[i].position==record.position && v[i].colour==record.colour && v[i].radius==7.5f,"record copied to every vertex");
    Require(v[i].uv==uv[i],"UV order (0,0) (1,0) (1,1) (0,1)");
    Require(std::bit_cast<uint32_t>(v[i].angle)==std::bit_cast<uint32_t>(angles[i]),"angle + k*pi/2");
  }
  // 1000 records per DrawPrimitiveUP.
  const auto records=Records(m,2500);
  const auto draw=MakeNativeParticleDraw(kEffect,ReadNativeParticleArray(m,records,2500),1,0,0,k);
  const auto calls=NativeEffectDrawCalls(draw);
  Require(calls.size()==3 && calls[0]==std::pair<uint32_t,uint32_t>{0,4000} && calls[1]==std::pair<uint32_t,uint32_t>{4000,4000} &&
          calls[2]==std::pair<uint32_t,uint32_t>{8000,2000},"particle draws split at 1000 records");
  Require(EncodeNativeEffectVertices(draw,0,draw.vertex_count())==T_Expand(m,records,2500),"expansion matches 821A7640");
  bool refused=false;
  try { ReadNativeParticleArray(m,records,uint32_t(-1)); } catch(const std::exception&) { refused=true; }
  Require(refused,"a negative particle count is refused");
  Require(MakeNativeParticleDraw(kEffect,{},1,0,1,k).technique==NativeEffectTechnique::ZParticle,"r8 != 0 selects +288");
}
// --- Keys and sort order -------------------------------------------------------
// sub_821A3B80 then sub_821A3BA0 (recomp.2.cpp:7457 and the drain after it),
// with the draw call replaced by recording the object.
std::vector<uint32_t> T_BucketOrder(const Reader& m,uint32_t heads,const std::vector<uint32_t>& objects) {
  for(uint32_t i=0;i<554*4;i+=4) m.StoreWord(heads+i,0);
  for(const auto r4:objects) {
    const uint32_t r11=(uint32_t(m.Byte(r4+40))+42)*4;
    m.StoreWord(r4+60,m.Word(heads+r11)); m.StoreWord(heads+r11,r4);
  }
  uint32_t r8=heads+168;
  for(uint32_t r7=256;r7;--r7,r8+=4) {
    uint32_t r11=m.Word(r8);
    while(r11) {
      const uint32_t r9=(uint32_t(m.Byte(r11+41))+298)*4,r10=r11+60,r6=r11;
      r11=m.Word(r10); m.StoreWord(r10,m.Word(heads+r9)); m.StoreWord(heads+r9,r6);
    }
  }
  std::vector<uint32_t> drawn;
  uint32_t r30=heads+2212;
  for(int32_t r29=255;r29>0;--r29,r30-=4) for(uint32_t r31=m.Word(r30);r31;r31=m.Word(r31+60)) drawn.push_back(r31);
  return drawn;
}
void TestSortOrder() {
  for(int round=0;round<50;++round) {
    Reader m;
    const uint32_t heads=0x70000000,objects=0x71000000;
    std::vector<uint32_t> filed;
    std::vector<NativeTransparentItem> producers[3];
    std::vector<uint16_t> keys;
    const uint32_t count=1+uint32_t(rng()%300);
    for(uint32_t i=0;i<count;++i) {
      // Few distinct keys so ties are common, and some below 256.
      const uint16_t key=uint16_t(rng()%4==0?rng()%256:(rng()%8)*0x1234+(rng()%3));
      const uint32_t object=objects+i*64;
      m.StoreByte(object+40,uint8_t(key)); m.StoreByte(object+41,uint8_t(key>>8));
      filed.push_back(object); keys.push_back(key);
      producers[rng()%3].push_back({key,i,{}});
    }
    const auto expected=T_BucketOrder(m,heads,filed);
    const auto merged=MergeNativeTransparentItems({producers[0],producers[1],producers[2]});
    Require(merged.size()==expected.size(),"merged size matches the drain");
    for(size_t i=0;i<merged.size();++i)
      Require(filed[merged[i].order]==expected[i],"merged order matches sub_821A3BA0 at "+std::to_string(i));
  }
  // The native key with the transformed z is sub_821C0C00's with that z in context+40.
  Reader m;
  m.StoreWord(kNativeBucketMinimum,0); m.StoreFloat(kNativeBucketMaximum,65535.f); m.StoreFloat(kNativeBucketMode2Scale,65536.f);
  const uint32_t context=0x72000000,object=0x73000000;
  for(int i=0;i<2000;++i) {
    m.StoreFloat(context,Random(-2,2)); m.StoreFloat(context+4,Random(-50,50));
    const float z=Random(-3000,100); m.StoreFloat(context+40,z);
    m.StoreWord(object+52,1+uint32_t(i%2)); m.StoreFloat(object+56,Random(-80,80));
    Require(ComputeNativeBucketKeyForDepth(m,context,object,std::bit_cast<uint32_t>(z))==ComputeNativeBucketKey(m,context,object),
            "key for depth matches the context+40 key");
  }
}
// --- Enumeration: cull, skip conditions, keys, order ---------------------------
void TestCollection() {
  auto m=MakeMemory();
  m.StoreWord(kNativeBucketMinimum,0); m.StoreFloat(kNativeBucketMaximum,65535.f); m.StoreFloat(kNativeBucketMode2Scale,65536.f);
  const uint32_t context=0x74000000,camera=0x74001000,manager=0x75000000,list=manager+48,nodes=0x76000000;
  m.StoreFloat(context,-1.f); m.StoreFloat(context+4,0.f); m.StoreFloat(context+8,1.f); m.StoreWord(context+16,camera);
  for(uint32_t i=0;i<16;++i) m.StoreFloat(camera+96+i*4,i%5==0?1.f:0.f);    // identity
  for(uint32_t i=0;i<26;++i) m.StoreFloat(camera+288+i*4,0.f);
  for(uint32_t side=0;side<4;++side) m.StoreFloat(camera+288+(8+side*4+2)*4,1.f);  // distance = z
  m.StoreFloat(camera+288+24*4,-1e6f); m.StoreFloat(camera+288+25*4,1e6f);
  m.StoreWord(manager,NativeEffectList::manager_vtable);
  struct Spec { const char* name; uint32_t slot4; int32_t mode; uint16_t key; uint16_t hidden=0; float distance=1e9f; uint8_t byte36=0; };
  const Spec specs[]={
    {"A",0x8217D6E0,1,5000},{"C",0x8217D6E0,1,9000},{"B",0x8217D6E0,1,5000,0,1e9f,1},  // B: byte +36 set, still drawn
    {"D hidden",0x8217D6E0,1,8000,1},{"E undrawn",0x8217D6E0,1,100},{"F culled",0x8217D6E0,1,8000,0,10.f},
    {"G mode 0",0x8217D6E0,0,0},{"I etc02",0x8217C4A0,1,7000},{"J unknown",0x82000000,1,6000},{"K mode 3",0x8217D6E0,3,6000},
    {"L etc02 culled",0x8217C4A0,1,7000,0,10.f}};
  std::vector<uint32_t> objects;
  for(size_t i=0;i<std::size(specs);++i) {
    const auto& s=specs[i];
    const uint32_t object=kObject+uint32_t(i)*0x1000;
    FillObject(m,object,s.slot4);
    m.StoreWord(object+52,uint32_t(s.mode)); m.StoreFloat(object+56,float(s.key)/1000.f+0.0004f);
    m.StoreHalf(object+64,s.hidden); m.StoreByte(object+36,s.byte36); m.StoreFloat(object+76,s.distance);
    m.StoreFloat(object+288,0.f); m.StoreFloat(object+292,0.f); m.StoreFloat(object+296,-1000.f); m.StoreFloat(object+300,1.f);
    m.StoreFloat(object+352,1.f);
    m.StoreWord(object+700,Records(m,2,kArray+uint32_t(i)*0x1000)); m.StoreWord(object+684,2); m.StoreWord(object+596,0x100+uint32_t(i));
    m.StoreWord(object+544,3); m.StoreWord(object+640,kArray+uint32_t(i)*0x1000); m.StoreWord(object+612,5);
    objects.push_back(object);
  }
  objects.push_back(objects[0]);  // A again: the +48 stamp skips a second visit
  // Linked in list order; the end marker is zero.
  m.StoreWord(list,nodes); m.StoreWord(list+12,0);
  for(size_t i=0;i<objects.size();++i) {
    const uint32_t node=nodes+uint32_t(i)*16;
    m.StoreWord(node,i+1<objects.size()?node+16:0); m.StoreWord(node+4,i?node-16:list); m.StoreWord(node+8,objects[i]);
  }
  uint32_t order=40;
  std::unordered_set<uint32_t> visited;
  const auto out=CollectNativeEffectManager(m,manager,context,order,&visited);
  Require(out.visited==objects.size() && out.duplicates==1 && out.hidden==1 && out.culled==2 && out.undrawn_keys==1 &&
          out.unknown_modes==1 && out.unsupported==1 && out.unsupported_slots==std::vector<uint32_t>{0x82000000},"collection counters");
  Require(out.immediate.size()==1 && out.immediate[0].object==objects[6],"mode 0 runs at once");
  const uint32_t expect[]={objects[1],objects[7],objects[0],objects[2]};  // C 9000, I 7000, A 5000, B 5000
  Require(out.items.size()==4,"four filed effects");
  for(size_t i=0;i<4;++i) {
    const auto& item=out.items[i];
    Require(item.object==expect[i],"descending key, filing order on ties ("+std::to_string(i)+")");
    Require(item.key==ComputeNativeBucketKeyForDepth(m,context,item.object,std::bit_cast<uint32_t>(-1000.f)).key,"item key");
    Require(item.key>=256,"drawn key");
  }
  Require(out.items[2].order<out.items[3].order,"A filed before B");
  Require(out.items[0].draws.size()==1 && out.items[0].draws[0].vertex_count()==8,"glass builds its records");
  // clEffectEtc02: +612 drops once for the drawn object only.
  Require(m.Word(objects[7]+612)==4 && m.Word(objects[10]+612)==5,"clEffectEtc02 lifetime decremented once per draw");
  // Merged with a model producer sharing the filing counter.
  const auto effects=NativeEffectTransparentItems(out.items,[](NativeBackendRecorder&,const NativeEffectItem&) {});
  const std::vector<NativeTransparentItem> models{{7000,43,{}},{9000,50,{}},{200,47,{}}};
  const auto merged=MergeNativeTransparentItems({effects,models});
  const std::pair<uint16_t,uint32_t> sequence[]={{9000,41},{9000,50},{7000,43},{7000,44},{5000,40},{5000,42}};
  Require(merged.size()==std::size(sequence),"merged drops the undrawn model key");
  for(size_t i=0;i<merged.size();++i)
    Require(merged[i].key==sequence[i].first && merged[i].order==sequence[i].second && (merged[i].record || merged[i].order==43 || merged[i].order==50),
            "merged sequence "+std::to_string(i));
  // A, C, B, E (filed, never drawn), I and J (filed; no builder): six.
  Require(order==40+6,"one filing number per keyed object that reached sub_821C0C00");
}
}
// The recording activates a run of adjacent draws once when this holds: it
// must hold for draws that differ only in their vertices (clElectricWire's
// strips) and fail when any field the activation reads differs.
void TestSharedActivation() {
  const NativeEffectConstants k{};
  const auto strip=[](float x) {
    return MakeNativeColourStripDraw(0x4000,{{{x,1,2},0xFF404040u},{{x+1,1,2},0xFF404040u},{{x,2,2},0xFF404040u}},kNativeEffectBlendAlpha,1);
  };
  const auto a=strip(0),b=strip(5);
  Require(NativeEffectDrawsShareActivation(a,b) && NativeEffectDrawsShareActivation(a,a),"wire strips do not share their activation");
  const std::vector<std::function<void(NativeEffectDraw&)>> changes{
    [](NativeEffectDraw& d) { d.effect^=0x10; },
    [](NativeEffectDraw& d) { d.texture=0x1234; },
    [](NativeEffectDraw& d) { d.blend=kNativeEffectBlendAdditive; },
    [](NativeEffectDraw& d) { d.depth_write=!d.depth_write; },
    [](NativeEffectDraw& d) { d.sets_depth_write=!d.sets_depth_write; },
    [](NativeEffectDraw& d) { d.technique=NativeEffectTechnique::Ribbon; },
    [](NativeEffectDraw& d) { d.kind=NativeEffectDraw::Kind::RibbonStrip; }};
  for(size_t i=0;i<changes.size();++i) {
    auto changed=b; changes[i](changed);
    Require(!NativeEffectDrawsShareActivation(a,changed) && !NativeEffectDrawsShareActivation(changed,a),
      "draws with another activation field shared it ("+std::to_string(i)+")");
  }
  // Particles and ribbons share it by the same fields; their vertices never matter.
  const auto particles=MakeNativeParticleDraw(0x4000,{NativeParticleRecord{}},0x55,1,0,k);
  auto more=MakeNativeParticleDraw(0x4000,{NativeParticleRecord{},NativeParticleRecord{}},0x55,1,0,k);
  Require(NativeEffectDrawsShareActivation(particles,more),"particle draws of one technique do not share their activation");
  more.texture=0x56;
  Require(!NativeEffectDrawsShareActivation(particles,more),"particle draws with other textures shared their activation");
}
int main() {
  try {
    TestSharedActivation();
    TestExpansion();
    TestBuilders();
    TestTechniqueMaterial();
    TestTransparentRecording();
    TestSortOrder();
    TestCollection();
  } catch(const std::exception& error) {
    std::cerr<<"native full-frame effects test failed: "<<error.what()<<"\n";
    return 1;
  }
  std::cout<<"native full-frame effects tests passed\n";
  return 0;
}
