#include "native_graphics/native_bucket_dispatch.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <vector>
#if defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#endif
using namespace edf::native;
namespace {
void Require(bool condition,const char* text) { if(!condition) throw std::runtime_error(text); }
// Sparse big-endian guest memory; ranges never cross a 4 KiB page here.
struct Reader {
  mutable std::map<uint32_t,std::array<uint8_t,4096>> pages;
  uint32_t Add(uint32_t at,uint32_t offset) const {
    if(offset>UINT32_MAX-at) throw std::runtime_error("guest address overflow");
    return at+offset;
  }
  const uint8_t* Bytes(uint32_t at,size_t size) const {
    if((at&4095)+size>4096) throw std::runtime_error("test range crosses a page");
    auto found=pages.find(at&~4095u);
    if(found==pages.end()) throw std::runtime_error("unmapped test read");
    return found->second.data()+(at&4095);
  }
  uint8_t* Map(uint32_t at) const { return pages[at&~4095u].data()+(at&4095); }
  uint32_t Word(uint32_t at) const { const auto* p=Bytes(at,4); return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
  void StoreWord(uint32_t at,uint32_t value) const { auto* p=Map(at); for(int i=0;i<4;++i) p[i]=uint8_t(value>>(24-i*8)); }
  void StoreByte(uint32_t at,uint8_t value) const { *Map(at)=value; }
  void StoreHalf(uint32_t at,uint16_t value) const { auto* p=Map(at); p[0]=uint8_t(value>>8); p[1]=uint8_t(value); }
  void StoreFloat(uint32_t at,float value) const { StoreWord(at,std::bit_cast<uint32_t>(value)); }
  uint8_t Byte(uint32_t at) const { return *Bytes(at,1); }
};
constexpr uint32_t kContext=0x40001000,kObject=0x40002000,kHeads=0x40010000,kStack=0x40020000;
Reader MakeReader() {
  Reader r;
  r.StoreWord(kNativeBucketMinimum,0x00000000);
  r.StoreWord(kNativeBucketMaximum,0x477fff00);
  r.StoreWord(kNativeBucketMode2Scale,0x47800000);
  for(uint32_t i=0;i<512;++i) r.StoreWord(kHeads+i*4,0);
  return r;
}
void MakeObject(const Reader& r,uint32_t object,int32_t mode,float scale,uint16_t hidden=0) {
  for(uint32_t i=0;i<80;i+=4) r.StoreWord(object+i,0xCDCDCDCD);
  r.StoreWord(object,0x82001000); r.StoreWord(object+32,kHeads);
  r.StoreWord(object+52,uint32_t(mode)); r.StoreFloat(object+56,scale); r.StoreHalf(object+64,hidden);
}
void SetContext(const Reader& r,float c0,float c4,float c40) {
  r.StoreFloat(kContext,c0); r.StoreFloat(kContext+4,c4); r.StoreFloat(kContext+40,c40);
}
#if defined(_M_X64) || defined(__x86_64__)
int64_t RecompFctidz(double f0) {
  return std::isnan(f0)?int64_t(0x8000000000000000ULL):(f0>double(LLONG_MAX))?LLONG_MAX:_mm_cvttsd_si64(_mm_load_sd(&f0));
}
#else
int64_t RecompFctidz(double f0) { return NativeFctidz(f0); }
#endif
// Literal transcription of sub_821C0C00 (recomp.57.cpp:8481-8534) and
// sub_821A3B80 (recomp.2.cpp:7457) for mode 1/2 objects.
void TranscribedDispatch(const Reader& r,uint32_t r4,uint32_t r11) {
  auto load=[&](uint32_t at){ return double(std::bit_cast<float>(r.Word(at))); };
  const auto r10=int32_t(r.Word(r11+52));
  double f0=load(r4+40),f13=0,f12=0;
  // stfs of the register lfs just loaded: the round trip reproduces the word
  // (PPC keeps sNaN payloads; the compiler folds float(double(x)) to x).
  r.StoreWord(r11+44,r.Word(r4+40));
  if(r10==1) {
    f13=load(r4+0); f0=load(r4+40); f12=load(r4+4);
    f0=double(float(std::fma(f0,f13,f12)));
    f13=load(r11+56);
    f0=double(float(f0*f13));
  } else if(r10==2) {
    f13=load(r11+56); f0=load(0x82020000u-26080u);
    f0=double(float(f13*f0));
  } else throw std::runtime_error("transcription covers modes 1/2");
  f13=load(0x82000000u+2468u);
  bool clamp=f0<f13;
  if(!clamp) { f13=load(0x82020000u-29004u); clamp=f0>f13; }
  if(clamp) f0=f13;
  const auto converted=uint64_t(RecompFctidz(f0));
  for(int i=0;i<8;++i) r.StoreByte(kStack+80+i,uint8_t(converted>>(56-i*8)));
  const uint32_t r3=r.Word(r11+32);
  const uint32_t half=uint32_t(r.Byte(kStack+86))<<8|r.Byte(kStack+87);
  const uint64_t rot=uint64_t(half)|uint64_t(half)<<32;
  const uint32_t r9=uint32_t(std::rotl(rot,24))&0xFFFFFF;
  r.StoreByte(r11+40,uint8_t(half)); r.StoreByte(r11+41,uint8_t(r9));
  const uint32_t slot=uint32_t(std::rotl(uint64_t(r.Byte(r11+40)+42),2))&0xFFFFFFFC;
  r.StoreWord(r11+60,r.Word(slot+r3));
  r.StoreWord(slot+r3,r11);
}
struct Outcome { uint8_t low,high; uint32_t depth,link,head; uint16_t key; };
Outcome Observe(const Reader& r,uint32_t object) {
  const uint8_t low=r.Byte(object+40),high=r.Byte(object+41);
  return {low,high,r.Word(object+44),r.Word(object+60),r.Word(kHeads+(uint32_t(low)+42)*4),uint16_t(high<<8|low)};
}
// Runs native and transcription on identical memory; returns the native key.
uint16_t Check(int32_t mode,float scale,float c0,float c4,float c40,const char* text) {
  auto native=MakeReader(),expected=MakeReader();
  for(auto* r:{&native,&expected}) { MakeObject(*r,kObject,mode,scale); SetContext(*r,c0,c4,c40); }
  const auto plan=PlanNativeBucket(native,kContext,kObject);
  Require(native.Word(kObject+44)==0xCDCDCDCD,"planning stored into the object");
  const auto inserted=InsertNativeBucket(native,kContext,kObject);
  Require(inserted==plan,"insert diverged from its plan");
  Require(ComputeNativeBucketKey(expected,kContext,kObject)==plan.key,"key depends on bucket state");
  TranscribedDispatch(expected,kContext,kObject);
  const auto a=Observe(native,kObject),b=Observe(expected,kObject);
  if(a.low!=b.low || a.high!=b.high || a.depth!=b.depth || a.link!=b.link || a.head!=b.head || a.head!=kObject) {
    std::cerr<<text<<": native key="<<a.key<<" expected="<<b.key<<'\n'; Require(false,text);
  }
  Require(plan.key.key==a.key && plan.slot==kHeads+(uint32_t(plan.key.low)+42)*4,"reported plan disagrees with stores");
  return a.key;
}
void TestClassify() {
  auto r=MakeReader();
  const std::array<std::pair<int32_t,NativeBucketDispatch>,6> modes{{
    {0,NativeBucketDispatch::Mode0},{1,NativeBucketDispatch::Bucket},{2,NativeBucketDispatch::Bucket},
    {3,NativeBucketDispatch::Unknown},{-1,NativeBucketDispatch::Unknown},{0x100,NativeBucketDispatch::Unknown}}};
  for(const auto& [mode,kind]:modes) {
    MakeObject(r,kObject,mode,1.0f);
    Require(ClassifyNativeBucket(r,kObject)==kind,"sort mode classification");
    for(const uint16_t hidden:{uint16_t(1),uint16_t(0x100),uint16_t(0xFFFF)}) {
      r.StoreHalf(kObject+64,hidden);
      Require(ClassifyNativeBucket(r,kObject)==NativeBucketDispatch::Return,"hidden halfword must return first");
    }
  }
  MakeObject(r,kObject,1,1.0f); r.StoreByte(kObject+66,0xFF);
  Require(ClassifyNativeBucket(r,kObject)==NativeBucketDispatch::Bucket,"only the +64 halfword hides");
  MakeObject(r,kObject,3,1.0f);
  bool thrown=false;
  try { ComputeNativeBucketKey(r,kContext,kObject); } catch(const std::exception&) { thrown=true; }
  Require(thrown,"unknown modes must not synthesize a stack key");
}
void TestFctidz() {
  const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
  for(const double v:{0.0,-0.0,0.999,-0.999,65535.0,65535.99,-1.5,nan,-nan,inf,-inf,
      9223372036854775808.0,std::nextafter(9223372036854775808.0,inf),std::nextafter(9223372036854775808.0,0.0),
      -9223372036854775808.0,std::nextafter(-9223372036854775808.0,-inf),1e300,-1e300})
    Require(NativeFctidz(v)==RecompFctidz(v),"fctidz differs from the recomp conversion");
  Require(NativeFctidz(nan)==INT64_MIN && NativeFctidz(inf)==LLONG_MAX && NativeFctidz(-inf)==INT64_MIN,"fctidz saturation");
  Require(NativeFctidz(9223372036854775808.0)==INT64_MIN,"recomp converts exactly 2^63 via cvttsd2si");
  Require(NativeFctidz(-2.75)==-2 && NativeFctidz(2.75)==2,"fctidz truncates toward zero");
}
void TestKeys() {
  const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
  // Mode 1: (c40*c0+c4)*scale.
  Require(Check(1,1.0f,1.0f,0.0f,100.25f,"mode1 plain")==100,"mode1 plain key");
  Require(Check(1,1.0f,1.0f,0.0f,255.9f,"mode1 low byte")==255,"low byte");
  Require(Check(1,1.0f,1.0f,0.0f,256.0f,"mode1 high byte")==256,"high byte");
  Require(Check(1,2.0f,100.0f,5.0f,400.0f,"mode1 clamp high")==65535,"upper clamp"); // (400*100+5)*2=80010
  Require(Check(1,1.0f,1.0f,0.0f,65535.0f,"mode1 at maximum")==65535,"maximum passes");
  Require(Check(1,1.0f,1.0f,0.0f,65534.996f,"mode1 below maximum")==65534,"truncation below maximum");
  Require(Check(1,1.0f,1.0f,0.0f,-1.0f,"mode1 negative")==0,"negative depth");
  Require(Check(1,1.0f,1.0f,0.0f,-0.0f,"mode1 negative zero")==0,"negative zero");
  Require(Check(1,1.0f,1.0f,-0.5f,0.25f,"mode1 fraction below zero")==0,"negative fraction");
  Require(Check(1,1.0f,1.0f,0.0f,0.999f,"mode1 fraction")==0,"fraction truncates");
  Require(Check(1,nan,1.0f,0.0f,10.0f,"mode1 NaN scale")==0,"NaN depth converts to key 0");
  Require(Check(1,1.0f,nan,0.0f,10.0f,"mode1 NaN context")==0,"NaN context");
  Require(Check(1,1.0f,1.0f,0.0f,inf,"mode1 infinity")==65535,"infinite depth clamps");
  Require(Check(1,1.0f,1.0f,0.0f,-inf,"mode1 negative infinity")==0,"negative infinity clamps");
  Require(Check(1,inf,0.0f,0.0f,0.0f,"mode1 zero times infinity")==0,"0*inf is NaN, key 0");
  Require(Check(1,-2.0f,1.0f,0.0f,-4.0f,"mode1 negative product")==8,"sign product");
  Require(Check(1,1.0f,1.0f,0.0f,std::bit_cast<float>(0x00000001u),"mode1 denormal")==0,"denormal depth");
  // Mode 2: scale*65536 then clamp; the context only supplies +44.
  Require(Check(2,0.5f,7.0f,7.0f,7.0f,"mode2 half")==32768,"mode2 half scale");
  Require(Check(2,1.0f,0.0f,0.0f,0.0f,"mode2 one")==65535,"mode2 1.0 clamps");
  Require(Check(2,0.99999f,0.0f,0.0f,0.0f,"mode2 below one")==uint16_t(double(float(double(0.99999f)*65536.0))),"mode2 below one");
  Require(Check(2,-0.25f,0.0f,0.0f,0.0f,"mode2 negative")==0,"mode2 negative");
  Require(Check(2,nan,0.0f,0.0f,0.0f,"mode2 NaN")==0,"mode2 NaN");
  Require(Check(2,std::bit_cast<float>(0x7F800001u),0.0f,0.0f,0.0f,"mode2 signalling NaN")==0,"mode2 sNaN");
  // +44 copies context+40 bits, including NaN payloads.
  auto r=MakeReader(); MakeObject(r,kObject,2,0.5f); SetContext(r,0,0,0);
  r.StoreWord(kContext+40,0x7FC12345);
  Require(ComputeNativeBucketKey(r,kContext,kObject).depth_bits==0x7FC12345,"+44 must be context+40");
  // Constants come from the guest image, not the header.
  r.StoreWord(kNativeBucketMaximum,std::bit_cast<uint32_t>(1000.0f));
  Require(ComputeNativeBucketKey(r,kContext,kObject).key==1000,"maximum read from guest constant");
}
void TestRandomAgainstTranscription() {
  std::mt19937 random(0x821C0C00);
  const std::array<float,12> specials{0.0f,-0.0f,1.0f,-1.0f,65535.0f,65536.0f,0.5f,1e-40f,3e38f,
    std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
  const auto pick=[&](float span) {
    switch(random()%4) {
      case 0: return specials[random()%specials.size()];
      case 1: return std::bit_cast<float>(uint32_t(random()));
      default: return std::uniform_real_distribution<float>(-span,span)(random);
    }
  };
  for(int i=0;i<20000;++i) {
    const int32_t mode=1+int32_t(random()%2);
    Check(mode,pick(4.0f),pick(2000.0f),pick(4000.0f),pick(200.0f),"random transcription");
  }
}
void TestRepeatedInserts() {
  auto native=MakeReader(),expected=MakeReader();
  constexpr uint32_t objects[]={0x40003000,0x40003100,0x40003200,0x40003300};
  const float depths[]={10.5f,10.9f,300.0f,10.0f};
  std::vector<uint32_t> order;
  for(size_t i=0;i<4;++i) for(auto* r:{&native,&expected}) {
    MakeObject(*r,objects[i],1,1.0f); SetContext(*r,1.0f,0.0f,depths[i]);
    if(r==&native) InsertNativeBucket(*r,kContext,objects[i]); else TranscribedDispatch(*r,kContext,objects[i]);
  }
  for(uint32_t at=kHeads;at<kHeads+512*4;at+=4) Require(native.Word(at)==expected.Word(at),"bucket heads diverged");
  for(const auto object:objects) Require(native.Word(object+60)==expected.Word(object+60),"bucket links diverged");
  // Head insertion: bucket 10 lists the most recent object first.
  auto slot=kHeads+(10+42)*4;
  for(auto object=native.Word(slot);object;object=native.Word(object+60)) order.push_back(object);
  Require(order==std::vector<uint32_t>({objects[3],objects[1],objects[0]}),"head insert order");
  Require(native.Word(kHeads+(44+42)*4)==objects[2] && native.Byte(objects[2]+41)==1,"key 300 bucket byte/high byte");
  // Re-inserting an object already at the head self-links, as the original does.
  InsertNativeBucket(native,kContext,objects[3]); TranscribedDispatch(expected,kContext,objects[3]);
  Require(native.Word(objects[3]+60)==objects[3] && expected.Word(objects[3]+60)==objects[3] &&
    native.Word(slot)==objects[3],"repeated insert of the head object");
  // Key byte 255 uses the last head, heads+297*4.
  MakeObject(native,objects[0],2,1.0f);
  const auto plan=InsertNativeBucket(native,kContext,objects[0]);
  Require(plan.slot==kHeads+297*4 && plan.key.low==0xFF && plan.key.high==0xFF,"maximum key slot");
}
}
int main() {
  try {
    TestClassify(); TestFctidz(); TestKeys(); TestRandomAgainstTranscription(); TestRepeatedInserts();
    std::cout<<"Native bucket dispatch checks passed\n";
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
