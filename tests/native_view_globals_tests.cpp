// The view globals the full frame writes for its guest listeners
// (native_view_globals.h) against the guest writers 821A17F8/821A19F0 as
// 821BE8D0 calls them, transcribed from the recompiled bodies statement for
// statement and run on synthetic guest memory for the same camera.
#include "native_graphics/native_view_globals.h"
#include "native_graphics/native_full_frame_effects.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

using namespace edf::native;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
// Sparse big-endian guest memory: 4 KiB pages created on first touch, so the
// image constants and the pool global can sit at their real addresses.
struct Memory {
  mutable std::map<uint32_t,std::unique_ptr<std::array<uint8_t,4096>>> pages;
  uint8_t* Page(uint32_t at) const {
    auto& page=pages[at>>12];
    if(!page) page=std::make_unique<std::array<uint8_t,4096>>();
    return page->data();
  }
  uint32_t Add(uint32_t address,uint32_t offset) const { return address+offset; }
  const uint8_t* Bytes(uint32_t at,size_t size) const {
    if(!at || (at&4095)+size>4096) throw std::runtime_error("view globals test range");
    return Page(at)+(at&4095);
  }
  uint32_t Word(uint32_t at) const {
    const auto* p=Bytes(at,4);
    return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
  }
  void StoreWord(uint32_t at,uint32_t value) const {
    auto* p=const_cast<uint8_t*>(Bytes(at,4));
    for(unsigned i=0;i<4;++i) p[i]=uint8_t(value>>(24-i*8));
  }
  float Float(uint32_t at) const { return std::bit_cast<float>(Word(at)); }
  void StoreFloat(uint32_t at,float value) const { StoreWord(at,std::bit_cast<uint32_t>(value)); }
  uint64_t Load64(uint32_t at) const { return (uint64_t(Word(at))<<32)|Word(at+4); }
  void Store64(uint32_t at,uint64_t value) const { StoreWord(at,uint32_t(value>>32)); StoreWord(at+4,uint32_t(value)); }
};

// ---- Transcription of the guest bodies, operating on guest memory. ----
// lfs/stfs through a double register, as the recompiled bodies do.
double Lfs(const Memory& m,uint32_t at) { return double(m.Float(at)); }
void Stfs(const Memory& m,uint32_t at,double value) { m.StoreFloat(at,float(value)); }
double Fmuls(double a,double b) { return double(float(a*b)); }
double Fmadds(double a,double c,double b) { return double(float(std::fma(a,c,b))); }
// sub_821C8000 (edf2017_recomp.60.cpp:8547): dst+4*k <- src at the transposed offset.
void Guest821C8000(const Memory& m,uint32_t r3,uint32_t r4) {
  constexpr std::array<uint32_t,16> from{0,16,32,48,4,20,36,52,8,24,40,56,12,28,44,60};
  for(uint32_t k=0;k<16;++k) Stfs(m,r3+k*4,Lfs(m,r4+from[k]));
}
// sub_821A16D8(r3 pool, r4 value, r5 source, r6 rows) (edf2017_recomp.28.cpp:7522).
void Guest821A16D8(const Memory& m,uint32_t r4,uint32_t r5,uint32_t r6) {
  uint32_t r10=r6;
  if(r4==0) return;                           // cmplwi cr6,r4,0; beqlr
  const uint32_t r11=m.Word(r4+8);
  if(!(r10<=r11)) r10=r11;                    // cmplw; ble; mr r10,r11
  if(r10==0) return;
  for(uint32_t off=0;r10;off+=16,--r10) {
    const uint32_t r8=m.Word(r4)+off;
    m.Store64(r8,m.Load64(r5+off));
    m.Store64(r8+8,m.Load64(r5+off+8));
  }
}
// sub_821C8750 (edf2017_recomp.52.cpp:8669).
void Guest821C8750(const Memory& m,uint32_t r3) {
  double f0=Lfs(m,r3+16),f13=Lfs(m,r3+4); Stfs(m,r3+16,f13); Stfs(m,r3+4,f0);
  f0=Lfs(m,r3+32); f13=Lfs(m,r3+8); Stfs(m,r3+32,f13); Stfs(m,r3+8,f0);
  f0=Lfs(m,r3+36); f13=Lfs(m,r3+24); Stfs(m,r3+36,f13); Stfs(m,r3+24,f0);
}
// sub_821B0130 (edf2017_recomp.28.cpp:7973).
void Guest821B0130(const Memory& m,uint32_t r3,uint32_t r4,uint32_t r5) {
  double f0=Lfs(m,r4+8);
  double f8=Lfs(m,r5+36); f8=Fmuls(f8,f0);
  double f13=Lfs(m,r4+4);
  double f7=Lfs(m,r5+40);
  const double f6=Lfs(m,r5+20);
  f7=Fmuls(f7,f0);
  double f12=Lfs(m,r4+0);
  double f9=Lfs(m,r5+0); f9=Fmuls(f9,f12);
  double f11=Lfs(m,r5+4);
  const double f5=Lfs(m,r5+24);
  double f10=Lfs(m,r5+8);
  f8=Fmadds(f6,f13,f8);
  f7=Fmadds(f5,f13,f7);
  f11=Fmadds(f11,f12,f8);
  f8=Lfs(m,r5+16);
  f13=Fmadds(f8,f13,f9);
  f12=Fmadds(f10,f12,f7);
  f10=Lfs(m,r5+32);
  Stfs(m,r3+4,f11);
  Stfs(m,r3+8,f12);
  f0=Fmadds(f0,f10,f13);
  Stfs(m,r3+0,f0);
}
// A 128-bit register as simde sees it after the byte-reversing VectorMaskL
// load: lane i is guest element 3-i (as in native_full_frame_sky_tests.cpp).
using Lanes=std::array<float,4>;
Lanes Load(const Memory& m,uint32_t at) { return {m.Float(at+12),m.Float(at+8),m.Float(at+4),m.Float(at)}; }
void Store(const Memory& m,uint32_t at,const Lanes& v) { for(unsigned i=0;i<4;++i) m.StoreFloat(at+i*4,v[3-i]); }
Lanes Hi(const Lanes& a,const Lanes& b) { return {a[2],b[2],a[3],b[3]}; } // simde_mm_unpackhi_epi32
Lanes Lo(const Lanes& a,const Lanes& b) { return {a[0],b[0],a[1],b[1]}; } // simde_mm_unpacklo_epi32
// _mm_dp_ps(a,b,0xFF): (t0+t1)+(t2+t3) broadcast, one rounding per operation.
Lanes Dp(const Lanes& a,const Lanes& b) {
  const float t0=a[0]*b[0]; const float t1=a[1]*b[1]; const float t2=a[2]*b[2]; const float t3=a[3]*b[3];
  const float l=t0+t1; const float h=t2+t3; const float s=l+h;
  return {s,s,s,s};
}
// sub_821C8198 (edf2017_recomp.79.cpp:8666), statement for statement.
void Guest821C8198(const Memory& m,uint32_t r3,uint32_t r4,uint32_t r5) {
  Lanes v10=Load(m,r5),v0=Load(m,r4),v9=Load(m,r5+16),v7=Load(m,r5+48),v8=Load(m,r5+32);
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
  Store(m,r3,v0); Store(m,r3+16,s48); Store(m,r3+32,s32); Store(m,r3+48,s16);
}
void Copy64Bytes(const Memory& m,uint32_t to,uint32_t from) { for(uint32_t i=0;i<64;i+=8) m.Store64(to+i,m.Load64(from+i)); }
// sub_821A17F8 (edf2017_recomp.47.cpp:7593).
void Guest821A17F8(const Memory& m,uint32_t r3,uint32_t r4) {
  const uint32_t r11=m.Word(r3+56);
  if(r11) Guest821C8000(m,m.Word(r11),r4);
  Copy64Bytes(m,r3+64,r4);
}
// sub_821A19F0 (edf2017_recomp.37.cpp:7582); r1 is the frame (stwu -240).
void Guest821A19F0(const Memory& m,uint32_t r3,uint32_t r4,uint32_t r1) {
  const uint32_t r31=r3,r30=r4;
  Guest821A16D8(m,m.Word(r31+44),r30,4);
  if(const auto r11=m.Word(r31+48)) Guest821C8000(m,m.Word(r11),r30);
  if(const auto r11=m.Word(r31+40)) Guest821C8000(m,m.Word(r11),r30);
  Copy64Bytes(m,r31+128,r30);
  Copy64Bytes(m,r1+144,r30);
  Guest821C8750(m,r1+144);
  Guest821B0130(m,r1+192,r1+192,r1+144);
  const double minus_one=Lfs(m,0x820013DC);
  Stfs(m,r1+192,Fmuls(Lfs(m,r1+192),minus_one));
  Stfs(m,r1+196,Fmuls(Lfs(m,r1+196),minus_one));
  Stfs(m,r1+200,Fmuls(Lfs(m,r1+200),minus_one));
  const double one=Lfs(m,0x820008CC),zero=Lfs(m,0x820009A4);
  for(const uint32_t at:{80u,100u,120u,140u}) Stfs(m,r1+at,one);
  const uint64_t eye0=m.Load64(r1+192),eye1=m.Load64(r1+200);
  for(const uint32_t at:{84u,88u,92u,96u}) Stfs(m,r1+at,zero);
  m.Store64(r31+192,eye0);
  for(const uint32_t at:{104u,108u,112u,116u,124u,128u,132u,136u}) Stfs(m,r1+at,zero);
  m.Store64(r31+200,eye1);
  Guest821C8198(m,r1+80,r30,r31+64);
  if(const auto r11=m.Word(r31+52)) Guest821C8000(m,m.Word(r11),r1+80);
}
// 821BE8D0's tail (821BE9A4..BC).
void Guest821BE8D0Globals(const Memory& m,uint32_t scene,uint32_t stack) {
  Guest821A17F8(m,m.Word(0x8257C02C),scene+32);
  Guest821A19F0(m,m.Word(0x8257C02C),scene+96,stack);
}

// ---- Synthetic pool and scene. ----
constexpr uint32_t kPool=0x40001DC0,kValues=0x40010000,kData=0x40020000,kScene=0x40030000,kStack=0x7FFE0000;
using Matrix=std::array<float,16>;
// Row-vector rigid transform (rotation rows then translation).
Matrix Rigid(float yaw,float pitch,float roll,float x,float y,float z) {
  const float cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch),cr=std::cos(roll),sr=std::sin(roll);
  const Matrix r{cy*cr+sy*sp*sr,cp*sr,-sy*cr+cy*sp*sr,0, -cy*sr+sy*sp*cr,cp*cr,sy*sr+cy*sp*cr,0, sy*cp,-sp,cy*cp,0, 0,0,0,1};
  Matrix world=r; world[12]=x; world[13]=y; world[14]=z;
  return world;
}
// The view as a camera update produces it: the inverse of a rigid world.
Matrix ViewOf(const Matrix& world) {
  Matrix view{};
  for(int r=0;r<3;++r) for(int c=0;c<3;++c) view[r*4+c]=world[c*4+r];
  for(int c=0;c<3;++c) view[12+c]=-(world[12]*view[c]+world[13]*view[4+c]+world[14]*view[8+c]);
  view[15]=1;
  return view;
}
Matrix Perspective(float fov,float aspect,float near_plane,float far_plane,bool reversed) {
  const float h=1/std::tan(fov*0.5f),w=h/aspect;
  const float q=reversed?near_plane/(near_plane-far_plane):far_plane/(far_plane-near_plane);
  return {w,0,0,0, 0,h,0,0, 0,0,reversed?-q:q,1, 0,0,reversed?q*far_plane:-q*near_plane,0};
}
struct Setup { uint32_t view_transpose_count=16; bool null_projection=false,null_view=false,null_view_projection=false; };
void BuildMemory(const Memory& m,const Setup& setup,const Matrix& projection,const Matrix& view) {
  m.StoreFloat(0x820013DC,-1.0f); m.StoreFloat(0x820008CC,1.0f); m.StoreFloat(0x820009A4,0.0f);
  m.StoreWord(0x8257C02C,kPool);
  // Value records: +40 g_mView, +44 g_mViewTranspose, +48 g_mViewInverseTranspose,
  // +52 g_mViewProjection, +56 g_mProjection (821A37E8), each {data, _, count}.
  for(uint32_t i=0;i<5;++i) {
    const uint32_t offset=40+i*4,record=kValues+i*16,data=kData+i*0x100;
    const bool null=(offset==56 && setup.null_projection) || (offset==40 && setup.null_view) ||
                    (offset==52 && setup.null_view_projection);
    m.StoreWord(kPool+offset,null?0:record);
    m.StoreWord(record,data);
    m.StoreWord(record+8,offset==44?setup.view_transpose_count:4);
    for(uint32_t w=0;w<64;++w) m.StoreWord(data+w*4,0xDEAD0000u+i*64+w);  // stale contents
  }
  for(uint32_t w=0;w<64;++w) m.StoreWord(kPool+64+w*4,0xBEEF0000u+w);
  for(uint32_t i=0;i<16;++i) { m.StoreFloat(kScene+32+i*4,projection[i]); m.StoreFloat(kScene+96+i*4,view[i]); }
}
NativeViewWords Words(const Matrix& m) { NativeViewWords w{}; for(size_t i=0;i<16;++i) w[i]=std::bit_cast<uint32_t>(m[i]); return w; }
// Every page but the guest stack's.
bool SameMemory(const Memory& a,const Memory& b) {
  for(const auto& [index,page]:a.pages) {
    if((index<<12)>=kStack && (index<<12)<kStack+0x10000) continue;
    const auto other=b.pages.find(index);
    if(other==b.pages.end()) { for(const auto byte:*page) if(byte) return false; continue; }
    if(*page!=*other->second) return false;
  }
  for(const auto& [index,page]:b.pages) if(!a.pages.contains(index)) {
    if((index<<12)>=kStack && (index<<12)<kStack+0x10000) continue;
    for(const auto byte:*page) if(byte) return false;
  }
  return true;
}
void MatchesGuest(const Matrix& projection,const Matrix& view,const Setup& setup,const char* what) {
  const Memory native,guest;
  BuildMemory(native,setup,projection,view);
  BuildMemory(guest,setup,projection,view);
  Require(WriteNativeViewGlobals(native,Words(projection),Words(view)),"native writer found the pool");
  Guest821BE8D0Globals(guest,kScene,kStack+0x8000);
  if(!SameMemory(native,guest)) {
    std::cerr<<"mismatch: "<<what<<"\n";
    throw std::runtime_error("native view globals differ from the guest writers");
  }
}

void CamerasMatchGuest() {
  const Matrix projection=Perspective(0.9f,16.f/9.f,0.5f,4000.f,false);
  MatchesGuest(projection,ViewOf(Rigid(0,0,0,0,0,0)),{},"identity camera");
  MatchesGuest(projection,ViewOf(Rigid(0.7f,-0.3f,0.05f,1234.5f,-87.25f,40961.125f)),{},"rotated, translated");
  MatchesGuest(Perspective(1.3f,4.f/3.f,0.1f,100000.f,true),ViewOf(Rigid(-2.9f,1.2f,-0.4f,-5.5f,300.f,-7.75f)),{},"reversed depth");
  // The record variations 821A17F8/821A19F0 branch on.
  MatchesGuest(projection,ViewOf(Rigid(0.2f,0.1f,0,10,20,30)),{2},"g_mViewTranspose with two float4s");
  MatchesGuest(projection,ViewOf(Rigid(0.2f,0.1f,0,10,20,30)),{0},"g_mViewTranspose with none");
  MatchesGuest(projection,ViewOf(Rigid(0.2f,0.1f,0,10,20,30)),{4,true,true,true},"null value records");
  // Arbitrary finite matrices: every word of both inputs exercised.
  std::mt19937 random(0x821BE8D0u);
  std::uniform_real_distribution<float> value(-2000.f,2000.f);
  for(int i=0;i<500;++i) {
    Matrix p{},v{};
    for(auto& x:p) x=value(random);
    for(auto& x:v) x=value(random);
    MatchesGuest(p,v,{uint32_t(i%6)},"random matrices");
  }
}
void EyeIsTheCameraAndFeedsTheEffects() {
  const auto world=Rigid(0.7f,-0.3f,0.05f,1234.5f,-87.25f,4096.125f);
  // One evaluation of each matrix, so every consumer sees the same words.
  const auto view_matrix=ViewOf(world);
  const auto projection_matrix=Perspective(0.9f,16.f/9.f,0.5f,4000.f,false);
  const auto view=Words(view_matrix);
  const Memory m;
  BuildMemory(m,{},projection_matrix,view_matrix);
  Require(WriteNativeViewGlobals(m,Words(projection_matrix),view),"pool written");
  // pool+192 is -(t x R^T) of the view: the camera's position.
  for(uint32_t i=0;i<3;++i) Require(std::fabs(m.Float(kPool+192+i*4)-world[12+i])<0.01f,"eye is the camera position");
  Require(m.Float(kPool+204)==1.0f,"eye w is the view's w");
  // The native effects pass derives its ribbon eye from the pass camera; after
  // the write the guest's +192 is that same eye, bit for bit (its
  // stale_guest_eye diagnostic compares exactly these).
  const auto inputs=ReadNativeEffectInputs(m,view);
  const auto guest=ReadNativeEffectGuestEye(m);
  for(uint32_t i=0;i<3;++i) {
    Require(std::bit_cast<uint32_t>(inputs.eye[i])==m.Word(kPool+192+i*4),"effects eye is pool+192");
    Require(std::bit_cast<uint32_t>(guest[i])==std::bit_cast<uint32_t>(inputs.eye[i]),"no stale guest eye");
  }
  // g_mView is the transposed view, g_mViewTranspose the view as is.
  for(uint32_t r=0;r<4;++r) for(uint32_t c=0;c<4;++c) {
    Require(m.Word(kData+0*0x100+(r*4+c)*4)==view[c*4+r],"g_mView = transpose(V)");
    Require(m.Word(kData+1*0x100+(r*4+c)*4)==view[r*4+c],"g_mViewTranspose = V");
  }
}
void NullPoolWritesNothing() {
  const Memory m;
  m.StoreWord(0x8257C02C,0);
  Require(!WriteNativeViewGlobals(m,Words(Matrix{}),Words(Matrix{})),"a null pool is reported");
  Require(m.pages.size()==1,"a null pool touches only the global's page");
}
}  // namespace

int main() {
  try {
    CamerasMatchGuest();
    EyeIsTheCameraAndFeedsTheEffects();
    NullPoolWritesNothing();
  } catch(const std::exception& error) {
    std::cerr<<"FAILED: "<<error.what()<<"\n";
    return 1;
  }
  std::cout<<"native view globals tests passed\n";
  return 0;
}
