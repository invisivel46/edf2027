#include "native_graphics/native_model_skinning.h"
#include <bit>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using namespace edf::native;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> void Reject(F f,const char* message) {
  bool rejected=false; try { f(); } catch(const std::exception&) { rejected=true; } Require(rejected,message);
}
// Minimal guest context for a line-by-line transcription of the recompiled
// bodies. Guest memory is big-endian, as REX_LOAD_U32/REX_STORE_U32 see it.
std::vector<uint8_t>* g_memory=nullptr;
uint32_t REX_LOAD_U32(uint32_t at) {
  uint32_t value=0; for(unsigned i=0;i<4;++i) value=(value<<8)|g_memory->at(at+i); return value;
}
void REX_STORE_U32(uint32_t at,uint32_t value) { for(unsigned i=0;i<4;++i) g_memory->at(at+i)=uint8_t(value>>(24-i*8)); }
union Register { uint64_t u64; int64_t s64; uint32_t u32; };
struct FloatRegister { double f64=0; };
union Temp { uint32_t u32; float f32; };
struct ConditionRegister {
  bool lt=false,gt=false,eq=false;
  template<class T> void compare(T left,T right,int) { lt=left<right; gt=left>right; eq=left==right; }
};
struct Fpscr { void disableFlushMode() {} };
struct Context {
  Register r3{},r4{},r5{},r6{},r10{},r11{};
  FloatRegister f0{};
  ConditionRegister cr6{};
  Fpscr fpscr{};
  int xer=0;
};
// generated/default/edf2017_recomp.62.cpp:7812-7922, verbatim apart from the macros above.
void sub_821A1738(Context& ctx) {
	Temp temp{};
	// mr r10,r6
	ctx.r10.u64 = ctx.r6.u64;
	// cmplwi cr6,r4,0
	ctx.cr6.compare<uint32_t>(ctx.r4.u32, 0, ctx.xer);
	// beqlr cr6
	if (ctx.cr6.eq) return;
	// lwz r11,16(r4)
	ctx.r11.u64 = REX_LOAD_U32(ctx.r4.u32 + 16);
	// cmplw cr6,r10,r11
	ctx.cr6.compare<uint32_t>(ctx.r10.u32, ctx.r11.u32, ctx.xer);
	// ble cr6,0x821a1754
	if (!ctx.cr6.gt) goto loc_821A1754;
	// mr r10,r11
	ctx.r10.u64 = ctx.r11.u64;
loc_821A1754:
	// lwz r11,0(r4)
	ctx.r11.u64 = REX_LOAD_U32(ctx.r4.u32 + 0);
	// cmplwi cr6,r10,0
	ctx.cr6.compare<uint32_t>(ctx.r10.u32, 0, ctx.xer);
	// beqlr cr6
	if (ctx.cr6.eq) return;
loc_821A1760:
	// lfs f0,0(r5)
	ctx.fpscr.disableFlushMode();
	temp.u32 = REX_LOAD_U32(ctx.r5.u32 + 0);
	ctx.f0.f64 = double(temp.f32);
	// addi r10,r10,-1
	ctx.r10.s64 = ctx.r10.s64 + -1;
	// stfs f0,0(r11)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r11.u32 + 0, temp.u32);
	// lfs f0,16(r5)
	temp.u32 = REX_LOAD_U32(ctx.r5.u32 + 16);
	ctx.f0.f64 = double(temp.f32);
	// cmplwi cr6,r10,0
	ctx.cr6.compare<uint32_t>(ctx.r10.u32, 0, ctx.xer);
	// stfs f0,4(r11)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r11.u32 + 4, temp.u32);
	// lfs f0,32(r5)
	temp.u32 = REX_LOAD_U32(ctx.r5.u32 + 32);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,8(r11)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r11.u32 + 8, temp.u32);
	// lfs f0,48(r5)
	temp.u32 = REX_LOAD_U32(ctx.r5.u32 + 48);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,12(r11)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r11.u32 + 12, temp.u32);
	// lfs f0,4(r5)
	temp.u32 = REX_LOAD_U32(ctx.r5.u32 + 4);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,16(r11)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r11.u32 + 16, temp.u32);
	// lfs f0,20(r5)
	temp.u32 = REX_LOAD_U32(ctx.r5.u32 + 20);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,20(r11)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r11.u32 + 20, temp.u32);
	// lfs f0,36(r5)
	temp.u32 = REX_LOAD_U32(ctx.r5.u32 + 36);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,24(r11)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r11.u32 + 24, temp.u32);
	// lfs f0,52(r5)
	temp.u32 = REX_LOAD_U32(ctx.r5.u32 + 52);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,28(r11)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r11.u32 + 28, temp.u32);
	// lfs f0,8(r5)
	temp.u32 = REX_LOAD_U32(ctx.r5.u32 + 8);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,32(r11)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r11.u32 + 32, temp.u32);
	// lfs f0,24(r5)
	temp.u32 = REX_LOAD_U32(ctx.r5.u32 + 24);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,36(r11)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r11.u32 + 36, temp.u32);
	// lfs f0,40(r5)
	temp.u32 = REX_LOAD_U32(ctx.r5.u32 + 40);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,40(r11)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r11.u32 + 40, temp.u32);
	// lfs f0,56(r5)
	temp.u32 = REX_LOAD_U32(ctx.r5.u32 + 56);
	ctx.f0.f64 = double(temp.f32);
	// addi r5,r5,64
	ctx.r5.s64 = ctx.r5.s64 + 64;
	// stfs f0,44(r11)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r11.u32 + 44, temp.u32);
	// addi r11,r11,48
	ctx.r11.s64 = ctx.r11.s64 + 48;
	// bne cr6,0x821a1760
	if (!ctx.cr6.eq) goto loc_821A1760;
	// blr
	return;
}
// generated/default/edf2017_recomp.60.cpp:8547-8649, verbatim apart from the macros above.
void sub_821C8000(Context& ctx) {
	Temp temp{};
	// lfs f0,0(r4)
	ctx.fpscr.disableFlushMode();
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 0);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,0(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 0, temp.u32);
	// lfs f0,16(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 16);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,4(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 4, temp.u32);
	// lfs f0,32(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 32);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,8(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 8, temp.u32);
	// lfs f0,48(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 48);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,12(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 12, temp.u32);
	// lfs f0,4(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 4);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,16(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 16, temp.u32);
	// lfs f0,20(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 20);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,20(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 20, temp.u32);
	// lfs f0,36(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 36);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,24(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 24, temp.u32);
	// lfs f0,52(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 52);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,28(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 28, temp.u32);
	// lfs f0,8(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 8);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,32(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 32, temp.u32);
	// lfs f0,24(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 24);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,36(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 36, temp.u32);
	// lfs f0,40(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 40);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,40(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 40, temp.u32);
	// lfs f0,56(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 56);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,44(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 44, temp.u32);
	// lfs f0,12(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 12);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,48(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 48, temp.u32);
	// lfs f0,28(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 28);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,52(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 52, temp.u32);
	// lfs f0,44(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 44);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,56(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 56, temp.u32);
	// lfs f0,60(r4)
	temp.u32 = REX_LOAD_U32(ctx.r4.u32 + 60);
	ctx.f0.f64 = double(temp.f32);
	// stfs f0,60(r3)
	temp.f32 = float(ctx.f0.f64);
	REX_STORE_U32(ctx.r3.u32 + 60, temp.u32);
	// blr 
	return;
}
// generated/default/edf2017_recomp.3.cpp:7388-7403, verbatim apart from the macros above.
void sub_821A17D8(Context& ctx) {
	// mr r11,r4
	ctx.r11.u64 = ctx.r4.u64;
	// cmplwi cr6,r11,0
	ctx.cr6.compare<uint32_t>(ctx.r11.u32, 0, ctx.xer);
	// beqlr cr6
	if (ctx.cr6.eq) return;
	// lwz r3,0(r11)
	ctx.r3.u64 = REX_LOAD_U32(ctx.r11.u32 + 0);
	// mr r4,r5
	ctx.r4.u64 = ctx.r5.u64;
	// b 0x821c8000
	sub_821C8000(ctx);
}

constexpr uint32_t kDescriptor=0x100,kPose=0x1000,kScratch=0x8000,kMemory=0x10000;
constexpr uint8_t kStale=0xA5;
std::vector<uint8_t> NewMemory() { return std::vector<uint8_t>(kMemory,kStale); }
// Deterministic non-orthogonal matrices (shear, non-uniform scale, projective
// last column). Bone 0 carries special values: -0, denormals, infinity, quiet NaN.
std::vector<NativePoseMatrix> Poses(uint32_t bones) {
  std::vector<NativePoseMatrix> poses(bones);
  uint32_t state=0x1234567u;
  for(auto& m:poses) for(auto& value:m) {
    state=state*1664525u+1013904223u;
    value=float(int32_t(state>>8)-0x800000)/float(1+(state&0xff));
  }
  if(bones) {
    poses[0]={1.0f,0.75f,-0.0f,0.125f, 0.3f,2.5f,std::numeric_limits<float>::denorm_min(),-1e-40f,
              -1.25f,std::numeric_limits<float>::infinity(),0.5f,std::numeric_limits<float>::quiet_NaN(),
              12.0f,-7.5f,3.25f,std::numeric_limits<float>::max()};
  }
  return poses;
}
void StorePoses(std::vector<uint8_t>& memory,const std::vector<NativePoseMatrix>& poses) {
  for(size_t bone=0;bone<poses.size();++bone) for(size_t i=0;i<16;++i)
    StoreNativeGuestFloat(memory.data()+kPose+bone*64+i*4,poses[bone][i]);
}
// Mirrors the caller at 821C9D14: r4=descriptor, r5=pose begin, r6=pose count.
std::vector<uint8_t> GuestPalette(const std::vector<NativePoseMatrix>& poses,uint32_t limit,bool null_descriptor=false) {
  auto memory=NewMemory();
  StorePoses(memory,poses);
  g_memory=&memory;
  REX_STORE_U32(kDescriptor+0,kScratch); REX_STORE_U32(kDescriptor+16,limit);
  Context ctx; ctx.r4.u64=null_descriptor?0:kDescriptor; ctx.r5.u64=kPose; ctx.r6.u64=uint32_t(poses.size());
  sub_821A1738(ctx);
  g_memory=nullptr;
  return memory;
}
uint32_t NativeWord(float value) { return std::bit_cast<uint32_t>(value); }
uint32_t GuestWord(const uint8_t* at) { return uint32_t(at[0])<<24|uint32_t(at[1])<<16|uint32_t(at[2])<<8|at[3]; }
void ComparePalette(uint32_t bones,uint32_t limit,const char* message) {
  const auto poses=Poses(bones);
  const auto guest=GuestPalette(poses,limit);
  const auto expected=NativeBonePaletteCount(bones,limit);
  // Guest bytes: the whole scratch window, including the untouched stale tail.
  const uint32_t window=bones+4;
  std::vector<uint8_t> packed(size_t(window)*kNativeBonePaletteBytes,kStale);
  Require(PackGuestBonePalette(poses,packed,limit)==expected,message);
  Require(std::memcmp(packed.data(),guest.data()+kScratch,packed.size())==0,message);
  // Native floats: same element order, host words equal to the guest words.
  std::vector<float> native(size_t(window)*kNativeBonePaletteFloats,-3.0f);
  Require(PackNativeBonePalette(poses,native,limit)==expected,message);
  for(size_t i=0;i<native.size();++i) {
    const auto want=i<size_t(expected)*kNativeBonePaletteFloats?GuestWord(guest.data()+kScratch+i*4):NativeWord(-3.0f);
    Require(NativeWord(native[i])==want,message);
  }
  // Source matrices are never written.
  auto source=NewMemory(); StorePoses(source,poses);
  Require(std::memcmp(source.data()+kPose,guest.data()+kPose,size_t(bones)*64)==0,message);
  // Exact-size destinations are enough; one bone short is rejected.
  std::vector<float> exact(size_t(expected)*kNativeBonePaletteFloats);
  Require(PackNativeBonePalette(poses,exact,limit)==expected,message);
  if(expected) {
    std::vector<float> small(size_t(expected-1)*kNativeBonePaletteFloats+11);
    Reject([&]{ PackNativeBonePalette(poses,small,limit); },message);
    std::vector<uint8_t> small_bytes(size_t(expected)*kNativeBonePaletteBytes-1);
    Reject([&]{ PackGuestBonePalette(poses,small_bytes,limit); },message);
  }
}
void PaletteMatchesGuest() {
  ComparePalette(1,kNativeBonePaletteShaderBones,"one bone palette");
  ComparePalette(68,kNativeBonePaletteShaderBones,"68 bone palette");
  ComparePalette(80,kNativeBonePaletteShaderBones,"palette past the limit");
  ComparePalette(200,kNativeBonePaletteShaderBones,"palette far past the limit");
  ComparePalette(5,3,"small descriptor limit");
  ComparePalette(5,0,"zero descriptor limit");
  ComparePalette(0,kNativeBonePaletteShaderBones,"empty pose");
  ComparePalette(3,0xffffffffu,"unbounded descriptor limit");
  // Past the limit: bone 68 onwards stays stale in guest scratch.
  const auto guest=GuestPalette(Poses(80),68);
  for(size_t i=68*48;i<80*48;++i) Require(guest[kScratch+i]==kStale,"bones past the limit were written");
  // Null descriptor returns before any write.
  const auto untouched=GuestPalette(Poses(4),68,true);
  for(size_t i=0;i<4*48;++i) Require(untouched[kScratch+i]==kStale,"null descriptor wrote scratch");
}
void PaletteLayout() {
  NativePoseMatrix m{};
  for(size_t i=0;i<16;++i) m[i]=float(i);
  std::array<float,12> out{};
  Require(PackNativeBonePalette(std::span<const NativePoseMatrix>(&m,1),out)==1,"layout count");
  const std::array<float,12> expected{0,4,8,12, 1,5,9,13, 2,6,10,14};
  Require(out==expected,"palette rows are source columns 0..2");
  const auto rigid=NativeRigidWorld(m);
  const NativePoseMatrix transposed{0,4,8,12, 1,5,9,13, 2,6,10,14, 3,7,11,15};
  Require(rigid==transposed,"rigid world is the full transpose");
}
void RigidMatchesGuest() {
  const auto poses=Poses(68);
  for(const uint32_t bone:{0u,1u,17u,67u}) {
    auto memory=NewMemory();
    StorePoses(memory,poses);
    g_memory=&memory;
    REX_STORE_U32(kDescriptor+0,kScratch);
    Context ctx; ctx.r4.u64=kDescriptor; ctx.r5.u64=kPose+bone*64;
    sub_821A17D8(ctx);
    g_memory=nullptr;
    const auto packed=GuestRigidWorld(poses[bone]);
    Require(std::memcmp(packed.data(),memory.data()+kScratch,packed.size())==0,"rigid guest bytes");
    Require(memory[kScratch+64]==kStale && memory[kScratch-1]==kStale,"rigid upload wrote past 64 bytes");
    const auto native=NativeRigidWorld(poses,bone);
    for(size_t i=0;i<16;++i) Require(NativeWord(native[i])==GuestWord(memory.data()+kScratch+i*4),"rigid native words");
  }
  auto memory=NewMemory();
  StorePoses(memory,poses);
  g_memory=&memory;
  Context ctx; ctx.r4.u64=0; ctx.r5.u64=kPose;
  sub_821A17D8(ctx);
  g_memory=nullptr;
  for(size_t i=0;i<64;++i) Require(memory[kScratch+i]==kStale,"null rigid descriptor wrote scratch");
  Reject([&]{ NativeRigidWorld(poses,68); },"rigid bone past the pose");
}
}
int main() {
  try {
    PaletteLayout();
    PaletteMatchesGuest();
    RigidMatchesGuest();
  } catch(const std::exception& error) {
    std::cerr<<"native model skinning test failed: "<<error.what()<<"\n";
    return 1;
  }
  std::cout<<"native model skinning tests passed\n";
  return 0;
}
