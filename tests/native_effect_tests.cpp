#include "native_graphics/effect.h"
#include "native_graphics/binding_runs.h"
#include "native_graphics/mesh_key.h"
#include <map>
#include "native_graphics/guest_instance_parameters.h"
#include "native_graphics/guest_parameter_records.h"
#include "native_graphics/native_material_parameters.h"
#include "native_graphics/guest_draw_state.h"
#include "native_graphics/guest_readable_range.h"
#include "native_graphics/triangle_strip.h"
#include "native_graphics/guest_fence.h"
#include "native_graphics/native_constant_ownership.h"
#include "native_graphics/native_contract_ledger.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <thread>

namespace {
int failures = 0;
struct ParameterReader {
  std::vector<uint8_t> memory=std::vector<uint8_t>(512);
  mutable size_t reads=0;
  uint32_t minimum_read_address=0;
  uint32_t Add(uint32_t at,uint32_t size) const {
    if(size>UINT32_MAX-at) throw std::runtime_error("address overflow");
    return at+size;
  }
  const uint8_t* Bytes(uint32_t at,size_t size) const {
    ++reads;
    if(at<minimum_read_address) throw std::runtime_error("forbidden guest-state read");
    if(!at || at>memory.size() || size>memory.size()-at) throw std::runtime_error("unreadable range");
    return memory.data()+at;
  }
  std::string String(uint32_t at,size_t limit) const {
    const auto* text=Bytes(at,1);
    for(size_t i=0;i<limit && i<memory.size()-at;++i)
      if(!text[i]) return {reinterpret_cast<const char*>(text),i};
    throw std::runtime_error("unterminated name");
  }
  void Put(uint32_t at,uint32_t value) {
    for(size_t i=0;i<4;++i) memory.at(at+i)=uint8_t(value>>(24-i*8));
  }
};
void Check(bool ok, const char* name) {
  if (!ok) { ++failures; std::cerr << name << '\n'; }
}
template<class F> void Reject(F function, const char* name) {
  try { function(); Check(false, name); } catch (const std::runtime_error&) {}
}
void Word(std::vector<uint8_t>& data, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) data.at(at + i) = static_cast<uint8_t>(value >> (8 * i));
}
void String(std::vector<uint8_t>& data, size_t at, const char* value) {
  do { data.at(at++) = *value; } while (*value++);
}
std::vector<uint8_t> Fixture() {
  std::vector<uint8_t> data(256);
  String(data, 0, "DXSL"); Word(data, 4, 200);
  Word(data, 8, 1); Word(data, 12, 24); Word(data, 16, 2); Word(data, 20, 80);
  Word(data, 24, 96); Word(data, 28, 1); Word(data, 32, 12);
  // One pass at 36, with two independent states at 64.
  Word(data, 40, 2); Word(data, 44, 28);
  Word(data, 48, 104); Word(data, 52, 114);
  Word(data, 56, 124); Word(data, 60, 134);
  Word(data, 64, 0); Word(data, 68, 1); Word(data, 72, 4); Word(data, 76, 1);
  Word(data, 80, 0); Word(data, 84, 60); Word(data, 88, 70);
  Word(data, 92, 1); Word(data, 96, 68); Word(data, 100, 78);
  String(data, 120, "Main"); String(data, 140, "VS"); String(data, 150, "vs_2_0");
  String(data, 160, "PS"); String(data, 170, "ps_2_0"); String(data, 200, "source");
  return data;
}
}
int main() {
  {
    const edf::native::NativeMeshKeyLess less;
    std::array<uint32_t,5> a{},b{};
    Check(!less(a,a),"mesh key equality is not less");
    for(unsigned lane=0;lane<5;++lane)
      for(uint32_t x:{0u,1u,255u,256u,0x80000000u,0xffffffffu})
        for(uint32_t y:{0u,1u,255u,256u,0x80000000u,0xffffffffu}) {
          a.fill(123); b.fill(123); a[lane]=x; b[lane]=y;
          Check(less(a,b)==(a<b),"mesh key unsigned lexicographic order differs");
          if(lane<4) {a[lane+1]=0xffffffff; b[lane+1]=0;
            Check(less(a,b)==(a<b),"mesh key suffix overrides first difference");}
        }
  }
  {
    int a=1,b=2;
    std::map<uint32_t,int*> slots{{0,&a},{1,nullptr},{3,&b},{15,&a}};
    std::vector<uint32_t> starts,counts;
    std::array<int*,16> context; context.fill(&b);
    auto emit=[&](uint32_t first,uint32_t count,int* const* values) {
      starts.push_back(first); counts.push_back(count);
      std::copy_n(values,count,context.begin()+first);
    };
    edf::native::EmitBindingRuns<int*,16>(slots,[](int* p){return p;},emit);
    Check(starts==std::vector<uint32_t>{0,3,15} && counts==std::vector<uint32_t>{2,1,1},
          "binding runs preserve sparse slot boundaries");
    Check(context[0]==&a && !context[1] && context[2]==&b && context[15]==&a,
          "binding runs emit nulls and preserve gaps");
    starts.clear(); counts.clear(); slots.clear();
    edf::native::EmitBindingRuns<int*,16>(slots,[](int* p){return p;},emit);
    Check(starts.empty(),"empty binding set emits no calls");
    slots={{0,&a},{16,&b}};
    Reject([&]{edf::native::EmitBindingRuns<int*,16>(slots,[](int* p){return p;},emit);},
           "binding runs reject out-of-range slots");
    Check(starts.empty(),"invalid binding set rejected before emission");
    slots.clear(); for(uint32_t i=0;i<16;++i) slots.emplace(i,&a);
    edf::native::EmitBindingRuns<int*,16>(slots,[](int* p){return p;},emit);
    Check(starts==std::vector<uint32_t>{0} && counts==std::vector<uint32_t>{16},
          "full binding run uses one call");
  }
  Check(edf::native::TriangleStripIndices16(4)==std::vector<uint8_t>{0,0,0,1,0,2,0,2,0,1,0,3},
    "triangle strip alternating winding");
  for(uint32_t vertices:{3u,4u,5u,20u,16384u,65536u}) {
    const auto indices=edf::native::TriangleStripIndices16(vertices);
    Check(indices.size()==size_t(vertices-2)*6,"triangle strip exact triangle count");
    for(uint32_t i=0;i<vertices-2;++i) {
      const auto at=size_t(i)*6;
      const uint32_t a=uint32_t(indices[at])*256+indices[at+1];
      const uint32_t b=uint32_t(indices[at+2])*256+indices[at+3];
      const uint32_t c=uint32_t(indices[at+4])*256+indices[at+5];
      Check(a==(i%2?i+1:i) && b==(i%2?i:i+1) && c==i+2,"triangle strip indices and parity");
    }
  }
  for(uint32_t count:{0u,1u,2u,65537u,UINT32_MAX})
    Reject([&]{edf::native::TriangleStripIndices16(count);},"invalid triangle strip bound");
  using edf::native::GuestReadableRegion;
  using edf::native::GuestRangeCommittedReadable;
  std::array<GuestReadableRegion,3> regions{{{0x1000,0x1000,true,true},
    {0x2000,0x1000,true,true},{0x3000,0x1000,true,true}}};
  size_t region_queries=0;
  auto query_region=[&](uint32_t at) {
    ++region_queries;
    for(const auto& region:regions)
      if(at>=region.base && at-region.base<region.size) return region;
    return GuestReadableRegion{};
  };
  Check(GuestRangeCommittedReadable(0x1fff,0x1002,query_region) && region_queries==3,
    "unaligned read validates every intersected region");
  regions[1].committed=false;
  Check(!GuestRangeCommittedReadable(0x1fff,0x1002,query_region),
    "decommitted middle region rejects stale readable protection");
  regions[1].committed=true; regions[1].readable=false;
  Check(!GuestRangeCommittedReadable(0x1fff,0x1002,query_region),"unreadable middle region");
  regions[1].readable=true;
  Check(GuestRangeCommittedReadable(0x1fff,1,query_region),"last byte before region boundary");
  Check(!GuestRangeCommittedReadable(0,1,query_region) &&
    !GuestRangeCommittedReadable(0x1000,0,query_region) &&
    !GuestRangeCommittedReadable(UINT32_MAX,2,query_region),"invalid readable ranges");
  Check(GuestRangeCommittedReadable(UINT32_MAX,1,[](uint32_t) {
    return GuestReadableRegion{0xfffff000,0x1000,true,true};
  }),"last guest byte uses nonwrapping end");
  for(auto malformed:std::array<GuestReadableRegion,4>{{{0x2000,0x1000,true,true},
      {0x1000,0,true,true},{0,0x1000,true,true},{0x1000,0x100000000ull,true,true}}})
    Check(!GuestRangeCommittedReadable(0x1000,1,[&](uint32_t){return malformed;}),
      "malformed region must not advance or overrun guest range");
  ParameterReader window_backing;
  window_backing.Put(64,0x12345678);
  const edf::native::GuestReadWindow window(window_backing,64,32);
  Check(window_backing.reads==1,"window validates containing block once");
  Check(window.Word(64)==0x12345678 && window.Bytes(92,4)==window_backing.memory.data()+92,
    "window includes first and last words");
  Check(window_backing.reads==1,"contained window reads do not requery mappings");
  window_backing.Put(64,0xabcdef01);
  Check(window.Word(64)==0xabcdef01,"window does not cache guest values");
  window.Bytes(60,8); window.Bytes(92,8); window.Bytes(128,4);
  Check(window_backing.reads==4,"crossing and external reads retain backing validation");
  window_backing.memory[256]='x'; window_backing.memory[257]=0;
  Check(window.String(256,2)=="x" && window_backing.reads==5,"window delegates bounded strings");
  Reject([&]{window.String(256,1);},"window retains unterminated string rejection");
  Reject([&]{window.Bytes(508,8);},"external window read bounds");
  Reject([&]{window.Bytes(UINT32_MAX,8);},"window read address overflow");
  Reject([&]{edf::native::GuestReadWindow bad(window_backing,0,4);},"null window");
  Reject([&]{edf::native::GuestReadWindow bad(window_backing,UINT32_MAX,4);},"window extent overflow");
  Reject([&]{edf::native::GuestReadWindow bad(window_backing,508,8);},"window backing truncation");
  ParameterReader material_backing;
  const edf::native::GuestReadWindow material(material_backing,32,112);
  for(uint32_t stage:{0u,36u}) for(bool global:{false,true})
    Check(edf::native::ReadNamedParameters(material,32,stage,global).empty(),"windowed empty material constants");
  for(bool global:{false,true})
    Check(edf::native::ReadTextureParameters(material,32,global).empty(),"windowed empty material textures");
  Check(material_backing.reads==1,"six material vectors share one validation");
  const std::array<uint32_t,6> font_decl{0,0x2c23a5,0,8,0x2c23a5,0x50000};
  Check(edf::native::IsFontVertexDeclaration(2,font_decl),"font declaration");
  for(uint32_t padding=0;padding<256;++padding) {
    auto padded=font_decl; padded[2]|=padding; padded[5]|=255-padding;
    Check(edf::native::IsFontVertexDeclaration(2,padded),"font declaration padding");
  }
  Check(!edf::native::IsFontVertexDeclaration(1,font_decl),"font element count");
  for(size_t word=0;word<6;++word) for(uint32_t bit=0;bit<32;++bit) {
    if((word==2 || word==5) && bit<8) continue;
    auto changed=font_decl; changed[word]^=uint32_t(1)<<bit;
    Check(!edf::native::IsFontVertexDeclaration(2,changed),"font semantic mutation");
  }
  using Ownership=edf::native::NativeConstantOwnership;
  Check(!Ownership::OwnsMainStatePackets(4096,0x821fd4fc),"main state unowned outside draw");
  {
    Ownership owner(4096);
    for(uint32_t caller:{0x821fd4fcu,0x821fe3f4u}) {
      Check(Ownership::OwnsMainStatePackets(4096,caller),"main state exact caller/device");
      Check(!Ownership::OwnsMainStatePackets(8192,caller) && !Ownership::OwnsMainStatePackets(4096,caller+4),
        "main state rejects other caller/device");
    }
    { Ownership masked(0); Check(!Ownership::OwnsMainStatePackets(4096,0x821fd4fc),"main state nested masking"); }
    Check(Ownership::OwnsMainStatePackets(4096,0x821fd4fc),"main state scope restored");
  }
  Check(!Ownership::OwnsShaderUpload(4096,0x8213efb8),"shader upload unowned outside draw");
  {
    Ownership owner(4096);
    for(uint32_t caller:{0x8213efb8u,0x8213f070u}) {
      Check(Ownership::OwnsShaderUpload(4096,caller),"shader upload exact caller/device");
      Check(!Ownership::OwnsShaderUpload(8192,caller) && !Ownership::OwnsShaderUpload(4096,caller+4),
        "shader upload rejects other caller/device");
    }
    { Ownership masked(0); Check(!Ownership::OwnsShaderUpload(4096,0x8213efb8),"shader upload nested masking"); }
    Check(Ownership::OwnsShaderUpload(4096,0x8213efb8),"shader upload scope restored");
  }
  Check(!Ownership::OwnsShaderOutputPatch(4096,0x8213ea50),"shader output unowned outside draw");
  {
    Ownership owner(4096);
    Check(Ownership::OwnsShaderOutputPatch(4096,0x8213ea50),"shader output exact parent device/caller");
    Check(!Ownership::OwnsShaderOutputPatch(8192,0x8213ea50) &&
      !Ownership::OwnsShaderOutputPatch(4096,0x8213ea54),"shader output wrong parent device/caller");
    { Ownership masked(0); Check(!Ownership::OwnsShaderOutputPatch(4096,0x8213ea50),"shader output nested masking"); }
    Check(Ownership::OwnsShaderOutputPatch(4096,0x8213ea50),"shader output scope restored");
  }
  Check(!Ownership::OwnsShaderMicrocodePatch(4096+12256,0x8213ec30),"shader patch unowned outside draw");
  {
    Ownership owner(4096);
    for(uint32_t caller:{0x8213ec30u,0x8213ea8cu}) {
      Check(Ownership::OwnsShaderMicrocodePatch(4096+12256,caller),"shader patch exact stride table/caller");
      Check(!Ownership::OwnsShaderMicrocodePatch(8192+12256,caller) &&
        !Ownership::OwnsShaderMicrocodePatch(4096+12256,caller+4),"shader patch wrong table/caller");
    }
    { Ownership masked(0); Check(!Ownership::OwnsShaderMicrocodePatch(4096+12256,0x8213ec30),"shader patch nested masking"); }
    Check(Ownership::OwnsShaderMicrocodePatch(4096+12256,0x8213ec30),"shader patch scope restored");
    { Ownership overflow(UINT32_MAX-100); Check(!Ownership::OwnsShaderMicrocodePatch(12155,0x8213ec30),"shader patch address wrap rejected"); }
  }
  Check(!Ownership::OwnsDerivedStatePackets(4096,0x8213f368),"derived packets unowned outside draw");
  {
    Ownership owner(4096);
    Check(Ownership::OwnsDerivedStatePackets(4096,0x8213f368),"derived packets exact caller/device");
    Check(!Ownership::OwnsDerivedStatePackets(8192,0x8213f368) &&
      !Ownership::OwnsDerivedStatePackets(4096,0x8213f36c),"derived packets reject wrong caller/device");
    { Ownership masked(0); Check(!Ownership::OwnsDerivedStatePackets(4096,0x8213f368),"derived packets nested masking"); }
    Check(Ownership::OwnsDerivedStatePackets(4096,0x8213f368),"derived packets restored scope");
  }
  Check(!Ownership::OwnsShaderLoadPackets(4096,0x8213edc8),"shader-load packets unowned outside draw");
  {
    Ownership owner(4096);
    for(uint32_t caller:{0x8213edc8u,0x8213ef3cu,0x8213f04cu}) {
      Check(Ownership::OwnsShaderLoadPackets(4096,caller),"shader-load exact caller");
      Check(!Ownership::OwnsShaderLoadPackets(8192,caller) && !Ownership::OwnsShaderLoadPackets(4096,caller+4),
        "shader-load rejects other device/caller");
    }
    { Ownership masked(0); Check(!Ownership::OwnsShaderLoadPackets(4096,0x8213edc8),"shader-load nested masking"); }
    Check(Ownership::OwnsShaderLoadPackets(4096,0x8213edc8),"shader-load scope restored");
  }
  Check(!Ownership::OwnsSpecialRenderPacket(4096,0x821fe424,0x100,0),"special render unowned outside draw");
  {
    Ownership owner(4096);
    for(uint32_t caller:{0x821fe424u,0x821fd52cu}) {
      Check(Ownership::OwnsSpecialRenderPacket(4096,caller,UINT64_MAX,0),"special render native caller");
      Check(!Ownership::OwnsSpecialRenderPacket(8192,caller,0x100,0) &&
        !Ownership::OwnsSpecialRenderPacket(4096,caller,0x100,1) &&
        !Ownership::OwnsSpecialRenderPacket(4096,caller,~uint64_t(0x100),0),"special render device/mode/dirty bounds");
    }
    Check(!Ownership::OwnsSpecialRenderPacket(4096,0x821fe428,0x100,0),"special render unrelated caller");
    { Ownership disabled(0); Check(!Ownership::OwnsSpecialRenderPacket(4096,0x821fe424,0x100,0),"special render nested disabled"); }
    Check(Ownership::OwnsSpecialRenderPacket(4096,0x821fe424,0x100,0),"special render outer scope restored");
  }
  Check(!Ownership::OwnsVectorStatePackets(4096,0xfc00000000000000ull),"vector state unowned outside draw");
  {
    Ownership owner(4096);
    for(unsigned bit=0;bit<64;++bit)
      Check(Ownership::OwnsVectorStatePackets(4096,uint64_t(1)<<bit)==(bit>=58),"vector-state mask bounds");
    Check(Ownership::OwnsVectorStatePackets(4096,0xfc00000000000000ull) &&
      !Ownership::OwnsVectorStatePackets(8192,0xfc00000000000000ull) &&
      !Ownership::OwnsVectorStatePackets(4096,0xfc00000000000001ull) &&
      !Ownership::OwnsVectorStatePackets(4096,0),"vector-state exact device and nonempty mask");
    { Ownership masked(0); Check(!Ownership::OwnsVectorStatePackets(4096,0xfc00000000000000ull),"vector-state nested mask"); }
    Check(Ownership::OwnsVectorStatePackets(4096,0xfc00000000000000ull),"vector-state restored scope");
    { Ownership overflow(UINT32_MAX-10270); Check(!Ownership::OwnsVectorStatePackets(UINT32_MAX-10270,0x8000000000000000ull),"vector-state control-word overflow"); }
  }
  Check(!Ownership::OwnsImmediateAllocation(4096,0x821fd6e8,44,16),"immediate allocation unowned outside draw");
  {
    Ownership immediate(4096,176);
    Check(Ownership::OwnsImmediateAllocation(4096,0x821fd6e8,44,16),"exact native immediate allocation");
    Check(!Ownership::OwnsImmediateAllocation(8192,0x821fd6e8,44,16) &&
      !Ownership::OwnsImmediateAllocation(4096,0x821fd6e4,44,16) &&
      !Ownership::OwnsImmediateAllocation(4096,0x821fd6e8,43,16) &&
      !Ownership::OwnsImmediateAllocation(4096,0x821fd6e8,44,4),"immediate allocation exact caller/device/extent/alignment");
    for(uint64_t bytes:{0ull,1ull,175ull,128ull*1024*1024+4,0x1000000b0ull}) {
      Ownership invalid(4096,bytes);
      Check(!Ownership::OwnsImmediateAllocation(4096,0x821fd6e8,44,16),"invalid immediate extent cannot wrap");
    }
    { Ownership indexed(4096); Check(!Ownership::OwnsImmediateAllocation(4096,0x821fd6e8,44,16),"indexed scope masks immediate ownership"); }
    { Ownership disabled(0,176); Check(!Ownership::OwnsImmediateAllocation(4096,0x821fd6e8,44,16),"unsubmitted nested scope masks immediate ownership"); }
    bool isolated=false;
    std::thread other([&] { isolated=!Ownership::OwnsImmediateAllocation(4096,0x821fd6e8,44,16); });
    other.join(); Check(isolated,"immediate allocation ownership is thread local");
    try { Ownership nested(8192,80); throw std::runtime_error("unwind immediate ownership"); }
    catch(const std::runtime_error&) {}
    Check(Ownership::OwnsImmediateAllocation(4096,0x821fd6e8,44,16),"immediate ownership restored after nested scopes");
  }
  Check(!Ownership::OwnsImmediateAllocation(4096,0x821fd6e8,44,16),"immediate allocation ownership ended");
  Check(!Ownership::OwnsFetchWords(4096,0x8000000000000000ull),"fetch ownership absent outside draw");
  {
    Ownership owner(4096);
    for(unsigned bit=0;bit<64;++bit)
      Check(Ownership::OwnsFetchWords(4096,uint64_t(1)<<bit)==(bit>=32),"fetch mask bounds");
    Check(Ownership::OwnsFetchWords(4096,0xffffffff00000000ull) &&
      !Ownership::OwnsFetchWords(4096,0xffffffff00000001ull) &&
      !Ownership::OwnsFetchWords(4096,0) && !Ownership::OwnsFetchWords(8192,0x8000000000000000ull),
      "fetch ownership rejects mixed, empty and other-device masks");
    { Ownership disabled(0); Check(!Ownership::OwnsFetchWords(4096,0x8000000000000000ull),"fetch nested disabled scope"); }
    Check(Ownership::OwnsFetchWords(4096,0x8000000000000000ull),"fetch outer scope restored");
    { Ownership overflow(UINT32_MAX); Check(!Ownership::OwnsFetchWords(UINT32_MAX,0x8000000000000000ull),"fetch source overflow"); }
  }
  Check(!Ownership::Owns(4096,16384,4096+1792),"constant ownership absent outside draw");
  Check(!Ownership::OwnsRenderWords(4096,8704,14516,0xe400000000000000ull),
    "render ownership absent outside native draw");
  {
    Ownership overflowing(UINT32_MAX);
    Check(!Ownership::OwnsRenderWords(UINT32_MAX,8704,10419,0x8000000000000000ull),
      "render source address overflow");
    Ownership disabled(0);
    Check(!Ownership::OwnsRenderWords(UINT32_MAX,8704,10419,0x8000000000000000ull),
      "nonnative nested draw masks render ownership");
  }
  {
    Ownership outer(4096);
    bool thread_isolated=false;
    std::thread other_thread([&] {
      const bool absent=!Ownership::Owns(4096,16384,5888) &&
        !Ownership::OwnsRenderWords(4096,8704,14516,0x8000000000000000ull) &&
        !Ownership::OwnsFetchWords(4096,0x8000000000000000ull);
      Ownership other(8192);
      thread_isolated=absent && Ownership::Owns(8192,16384,9984) &&
        !Ownership::Owns(4096,16384,5888) && Ownership::OwnsFetchWords(8192,0x8000000000000000ull) &&
        !Ownership::OwnsFetchWords(4096,0x8000000000000000ull);
    });
    other_thread.join();
    Check(thread_isolated && Ownership::Owns(4096,16384,5888),"draw ownership is thread-local");
    constexpr uint64_t render_bits=0xfff0000000000000ull;
    Check(Ownership::OwnsRenderWords(4096,8704,14516,render_bits),"owned render word combination");
    for(unsigned bit=0;bit<64;++bit) {
      const auto mask=uint64_t(1)<<bit;
      Check(Ownership::OwnsRenderWords(4096,8704,14516,mask)==bool(mask&render_bits),
        "exact render word ownership");
      if(!(mask&render_bits)) Check(!Ownership::OwnsRenderWords(4096,8704,14516,render_bits|mask),
        "mixed render word mask retains packet encoder");
    }
    Check(!Ownership::OwnsRenderWords(4096,8704,14516,0) &&
      !Ownership::OwnsRenderWords(4096,8576,14516,render_bits) &&
      !Ownership::OwnsRenderWords(4096,8704,14520,render_bits) &&
      !Ownership::OwnsRenderWords(8192,8704,18612,render_bits),"render ownership bounds");
    Check(Ownership::Owns(4096,16384,4096+1792) && Ownership::Owns(4096,17408,4096+5888),
      "native device owns exact vertex and pixel constant sources");
    Check(!Ownership::Owns(8192,16384,8192+1792) && !Ownership::Owns(4096,17408,4096+1792) &&
      !Ownership::Owns(4096,16384,4096+1796) && !Ownership::Owns(4096,0,4096+1792),
      "constant ownership rejects other devices, banks and source offsets");
    { Ownership disabled(0); Check(!Ownership::Owns(4096,16384,5888),"unsupported nested draw masks outer ownership"); }
    Check(Ownership::Owns(4096,16384,5888),"outer ownership restored after disabled scope");
    try {
      Ownership nested(8192);
      Check(Ownership::Owns(8192,17408,8192+5888) && !Ownership::Owns(4096,16384,5888),
        "nested native draw owns only its device");
      throw std::runtime_error("test unwind");
    } catch(const std::runtime_error&) {}
    Check(Ownership::Owns(4096,16384,5888) && Ownership::OwnsFetchWords(4096,0x8000000000000000ull),
      "ownership restored after exception");
    { Ownership overflow(UINT32_MAX-100); Check(!Ownership::Owns(UINT32_MAX-100,16384,1691) &&
        !Ownership::Owns(UINT32_MAX-100,17408,5787),"wrapped constant addresses rejected"); }
  }
  Check(!Ownership::Owns(4096,16384,5888),"ownership ends after draw");
  {
    struct Bank { uint32_t number,offset,words; };
    constexpr Bank banks[]{{18688,9984,40},{8192,10240,16},{8448,10316,21},{8576,10400,5},
      {8704,10420,12},{8832,10468,21},{8960,10552,38},{9088,10704,8}};
    Ownership owner(4096);
    for(const auto& bank:banks) {
      const auto mask=UINT64_MAX<<(64-bank.words);
      Check(Ownership::OwnsRenderWords(4096,bank.number,4096+bank.offset,mask),"complete state bank owned");
      for(unsigned bit=0;bit<64;++bit)
        Check(Ownership::OwnsRenderWords(4096,bank.number,4096+bank.offset,uint64_t(1)<<bit)==
          (bit>=64-bank.words),"state bank exact extent");
      Check(!Ownership::OwnsRenderWords(4096,bank.number,4096+bank.offset+4,mask) &&
        !Ownership::OwnsRenderWords(4096,bank.number,4096+bank.offset,mask|1),"state bank rejects mismatched source/mask");
      const auto wrapped=UINT32_MAX-bank.offset;
      Ownership overflow(wrapped);
      Check(!Ownership::OwnsRenderWords(wrapped,bank.number,UINT32_MAX,mask),"state bank end overflow");
    }
  }
  using edf::native::EvaluateNativeWait;
  using edf::native::NativeWaitResult;
  Check(EvaluateNativeWait(7,0,{}, {},true,true)==NativeWaitResult::Complete,"zero wait needs no event");
  Check(EvaluateNativeWait(7,5,5,{},false,false)==NativeWaitResult::Pending,"unknown completion is unfinished");
  Check(EvaluateNativeWait(7,5,{}, {},false,false)==NativeWaitResult::Unsubmitted,"missing event must not wait forever");
  Check(EvaluateNativeWait(9,7,5,5,false,false)==NativeWaitResult::Unsubmitted,"target beyond submitted event");
  Check(EvaluateNativeWait(7,5,5,3,false,true)==NativeWaitResult::TimedOut,"pending timeout is not completion");
  Check(EvaluateNativeWait(7,5,5,{},true,false)==NativeWaitResult::Cancelled,"unknown completion can be cancelled");
  Check(EvaluateNativeWait(7,5,5,5,true,true)==NativeWaitResult::Complete,"known completion takes precedence");
  Check(EvaluateNativeWait(3,1,1,0xffffffffu,false,false)==NativeWaitResult::Pending,"wrapped native pending wait");
  Check(EvaluateNativeWait(3,1,0xffffffffu,0xffffffffu,false,false)==NativeWaitResult::Unsubmitted,"wrapped missing event");
  Check(!edf::native::GuestFencePending(7,0,1),"zero fence target must not wait");
  Check(edf::native::GuestFencePending(7,5,3),"outstanding fence target");
  Check(!edf::native::GuestFencePending(7,5,5),"completed fence target");
  Check(!edf::native::GuestFencePending(7,3,5),"older fence target");
  Check(edf::native::GuestFencePending(3,1,0xffffffffu),"outstanding wrapped fence target");
  Check(!edf::native::GuestFencePending(3,0xffffffffu,1),"completed wrapped fence target");
  for(uint32_t origin:{1u,99u,0xfffffff1u,0xfffffffdu}) {
    for(uint32_t completed_index=0;completed_index<=12;++completed_index) {
      for(uint32_t target_index=0;target_index<=12;++target_index) {
        const uint32_t target=origin+target_index*2;
        Check(edf::native::GuestFencePending(origin+24,target,origin+completed_index*2)==
          (target_index>completed_index),"fence predicate must preserve ordered values across wrap");
      }
    }
  }
  const std::array<uint32_t,6> full_scene_view{0,0,1280,720,0,0x3f800000};
  std::array<uint8_t,32> cpu_state_bytes;
  cpu_state_bytes.fill(0xa5);
  edf::native::StoreGuestCpuWords(std::span<uint8_t>(cpu_state_bytes).subspan(4,24),full_scene_view);
  for(size_t i=0;i<6;++i)
    Check(edf::native::GuestBlockWord(cpu_state_bytes.data()+4+i*4)==full_scene_view[i],"ordinary CPU viewport stores preserve big endian words");
  Check(cpu_state_bytes[3]==0xa5 && cpu_state_bytes[28]==0xa5,"ordinary CPU state store preserves neighboring fields");
  const auto cpu_before=cpu_state_bytes;
  Reject([&]{edf::native::StoreGuestCpuWords(std::span<uint8_t>(cpu_state_bytes).subspan(4,23),full_scene_view);},"reject partial CPU state destination");
  Check(cpu_state_bytes==cpu_before,"rejected CPU state store does not mutate bytes");
  edf::native::StoreGuestCpuWords(std::span<uint8_t>(cpu_state_bytes).subspan(8,8),std::array<uint32_t,2>{0x81234567,0x89abcdff});
  Check(edf::native::GuestBlockWord(cpu_state_bytes.data()+8)==0x81234567 &&
    edf::native::GuestBlockWord(cpu_state_bytes.data()+12)==0x89abcdff,"CPU dirty mask stores preserve both64-bit halves");
  const auto full_scene=edf::native::MakeNativeSceneViewportCpuState(full_scene_view,1280,720,
    {0,0,640,736},false,{0x80008000,0x80008000});
  Check(full_scene.viewport==full_scene_view,"untiled viewport retains full1280 width, not640 tile width");
  Check(full_scene.transform==std::array<float,6>{640,640,-360,360,1,0},"native full scene viewport transform");
  Check(full_scene.packed_scissor==std::array<uint32_t,2>{0x80008000,0x82d08500},
    "disabled scissor uses native extent and preserves unrelated packed bits");
  const auto clipped_scene=edf::native::MakeNativeSceneViewportCpuState(full_scene_view,1280,720,
    {uint32_t(-10),20,600,800},true,{0,0});
  Check(clipped_scene.packed_scissor==std::array<uint32_t,2>{0x00140000,0x02d00258},
    "native scene scissor intersects signed rectangle with full viewport");
  Reject([&]{edf::native::MakeNativeSceneViewportCpuState(full_scene_view,640,720,{},{},{});},
    "native scene rejects extent mismatch");
  auto reversed_scene_view=full_scene_view;
  const auto auto_scene=edf::native::MakeNativeSceneViewportCpuState({0,0,0x7fffffff,0x7fffffff,0,0x3f800000},
    1280,720,{},false,{},false);
  Check(auto_scene.viewport==full_scene_view,"automatic viewport clamps to native surface, not tile extent");
  const auto partial_scene=edf::native::MakeNativeSceneViewportCpuState({100,200,UINT32_MAX,UINT32_MAX,0,0x3f800000},
    1280,720,{},false,{},false);
  Check(partial_scene.viewport[2]==1180 && partial_scene.viewport[3]==520 &&
    partial_scene.transform==std::array<float,6>{590,690,-260,460,1,0},
    "native subviewport clamps without unsigned extent overflow and retains origin");
  reversed_scene_view[4]=0x3f800000; reversed_scene_view[5]=0;
  const auto reversed_scene=edf::native::MakeNativeSceneViewportCpuState(reversed_scene_view,1280,720,{},false,{});
  Check(reversed_scene.transform[4]==-1 && reversed_scene.transform[5]==1,"native viewport preserves reversed depth");
  ParameterReader draw;
  draw.memory.resize(13000);
  const std::array<float,4> blend_color{.125f,.25f,.5f,1.f};
  for(uint32_t i=0;i<4;++i) draw.Put(32+10336+i*4,std::bit_cast<uint32_t>(blend_color[i]));
  draw.reads=0;
  Check(edf::native::ReadBlendFactor(draw,32)==blend_color && draw.reads==1,
        "blend factor channel order/endian conversion in one validated block");
  draw.Put(32+10336,0x3f400000);
  Check(edf::native::ReadBlendFactor(draw,32)[0]==.75f,"blend factor updates remain live");
  Reject([&]{edf::native::ReadBlendFactor(draw,UINT32_MAX);},"blend factor address overflow");
  draw.memory.resize(32+10351);
  Reject([&]{edf::native::ReadBlendFactor(draw,32);},"truncated blend factor block");
  draw.memory.resize(13000);
  for(uint32_t offset=10332;offset<12424;offset+=4) draw.Put(32+offset,0x81000000u+offset);
  const auto original_render=edf::native::ReadRenderStateWords(draw,32);
  const auto original_viewport=edf::native::ReadViewportWords(draw,32);
  draw.minimum_read_address=32+12376;
  draw.reads=0;
  const auto owned_viewport=edf::native::ReadViewportWords(draw,32,false);
  Check(draw.reads==1 && owned_viewport.words==original_viewport.words && !owned_viewport.scissor_enabled,
    "owned viewport must not reread guest scissor enable");
  Reject([&]{edf::native::ReadViewportWords(draw,32);},"legacy viewport touches forbidden scissor word");
  draw.minimum_read_address=0;
  draw.reads=0;
  const auto xui=edf::native::ReadXuiDeviceWords(draw,32);
  Check(draw.reads==1,"XUI device uses one validated block");
  Check(xui.render==original_render && xui.viewport.words==original_viewport.words &&
    xui.viewport.scissor_enabled==original_viewport.scissor_enabled,"XUI batched draw state matches individual readers");
  Check(xui.surface==0x81000000u+12168 && xui.texture==0x81000000u+12272,
    "XUI surface/texture offsets and endian conversion");
  draw.minimum_read_address=32+12168;
  auto owned_render=original_render;
  owned_render[0]^=0x80; owned_render[5]=0;
  draw.reads=0;
  const auto owned_xui=edf::native::ReadXuiDeviceWords(draw,32,owned_render);
  Check(draw.reads==1 && owned_xui.render==owned_render &&
    owned_xui.viewport.words==xui.viewport.words && !owned_xui.viewport.scissor_enabled &&
    owned_xui.surface==xui.surface && owned_xui.texture==xui.texture,
    "native XUI path must use owned render/scissor words and only read later device fields");
  Reject([&]{edf::native::ReadXuiDeviceWords(draw,32);},"legacy XUI negative control touches forbidden words");
  draw.minimum_read_address=0;
  draw.Put(32+11536,0xdeadbeef);
  draw.memory.resize(32+12416); // Old shader slots are deliberately inaccessible.
  const auto native_identity_xui=edf::native::ReadXuiDeviceWords(draw,32);
  Check(native_identity_xui.render==xui.render && native_identity_xui.viewport.words==xui.viewport.words &&
    native_identity_xui.surface==xui.surface && native_identity_xui.texture==xui.texture,
    "UI snapshot retained guest shader/declaration identity dependency");
  draw.Put(32+12272,123); draw.Put(32+11584,0);
  const auto changed_xui=edf::native::ReadXuiDeviceWords(draw,32);
  Check(changed_xui.texture==123 && !changed_xui.viewport.scissor_enabled && changed_xui.render[5]==0,
    "XUI device snapshot is refreshed between draws");
  Reject([&]{edf::native::ReadXuiDeviceWords(draw,UINT32_MAX);},"XUI block address overflow");
  draw.memory.resize(32+12415);
  Reject([&]{edf::native::ReadXuiDeviceWords(draw,32);},"truncated XUI device block");
  draw.memory.resize(13000);
  draw.reads=0;
  draw.Put(32+12416,0x81234567); draw.Put(32+12420,0x89abcdef);
  const auto shaders=edf::native::ReadShaderPair(draw,32);
  Check(shaders.pixel==0x81234567 && shaders.vertex==0x89abcdef && draw.reads==1,
    "shader pair endian/order from one validated block");
  draw.Put(32+12420,0x12345678);
  Check(edf::native::ReadShaderPair(draw,32).vertex==0x12345678,"shader pair remains live across draws");
  Reject([&]{edf::native::ReadShaderPair(draw,UINT32_MAX);},"shader pair address overflow");
  draw.memory.resize(32+12423);
  Reject([&]{edf::native::ReadShaderPair(draw,32);},"truncated shader pair");
  draw.memory.resize(13000);
  draw.reads=0;
  for(uint32_t i=0;i<10;++i) draw.Put(32+12376+i*4,0x81020300+i);
  draw.Put(32+11584,2);
  const auto viewport_words=edf::native::ReadViewportWords(draw,32);
  Check(viewport_words.scissor_enabled && draw.reads==2,"viewport requires two validated blocks");
  for(uint32_t i=0;i<10;++i)
    Check(viewport_words.words[i]==0x81020300+i,"viewport/scissor word offset and bit preservation");
  draw.Put(32+12392,0x3f800000); draw.Put(32+11584,0);
  const auto updated_viewport=edf::native::ReadViewportWords(draw,32);
  Check(!updated_viewport.scissor_enabled && updated_viewport.words[4]==0x3f800000,
    "viewport state remains live");
  for(uint32_t i=0;i<6;++i) draw.Put(32+10420+i*4,0x11223300+i);
  draw.Put(32+10332,0xfff5); draw.Put(32+11584,0x80);
  draw.reads=0;
  const auto render_words=edf::native::ReadRenderStateWords(draw,32);
  Check(render_words==std::array<uint32_t,6>{0x11223301,0x11223300,0x11223305,0x11223302,5,1} &&
    draw.reads==3,"render-state ordering, mask and boolean from three validated blocks");
  draw.Put(32+10424,0); draw.Put(32+10332,0); draw.Put(32+11584,0);
  const auto updated_render=edf::native::ReadRenderStateWords(draw,32);
  Check(updated_render[0]==0 && updated_render[4]==0 && updated_render[5]==0,"render state remains live");
  Reject([&]{edf::native::ReadViewportWords(draw,UINT32_MAX);},"viewport address overflow");
  Reject([&]{edf::native::ReadRenderStateWords(draw,UINT32_MAX);},"render-state address overflow");
  draw.memory.resize(32+12412);
  Reject([&]{edf::native::ReadViewportWords(draw,32);},"truncated viewport/scissor block");
  draw.memory.resize(32+10440);
  Reject([&]{edf::native::ReadRenderStateWords(draw,32);},"truncated render-state block");
  draw.memory.resize(13000);
  // PA_SU_VTX_CNTL 4 is the retail default written by 8214EFF8/82132F48:
  // Direct3D 9 pixel centres, round-to-even, 1/16th vertex quantization.
  const auto retail_centers=edf::native::DecodeGuestVertexCenters(4);
  Check(retail_centers.integer_centers && retail_centers.rounding==2 && retail_centers.quantization==0 &&
    edf::native::GuestPixelCenterOffset(4)==.5f,"retail vertex-centre word decodes to a half-pixel offset");
  const auto opengl_centers=edf::native::DecodeGuestVertexCenters(5);
  Check(!opengl_centers.integer_centers && opengl_centers.rounding==2 &&
    edf::native::GuestPixelCenterOffset(5)==0.f,"kOGLHalf centres take no offset");
  const auto packed_centers=edf::native::DecodeGuestVertexCenters(0xfffffffeu);
  Check(packed_centers.integer_centers && packed_centers.rounding==3 && packed_centers.quantization==7 &&
    edf::native::GuestPixelCenterOffset(0xfffffffeu)==.5f,
    "rounding/quantization fields are separated from unrelated upper bits");
  draw.Put(32+10560,4);
  draw.reads=0;
  Check(edf::native::ReadVertexCenterWord(draw,32)==4 && draw.reads==1,
    "vertex-centre word offset and endian conversion in one validated block");
  draw.Put(32+10560,0x00000005);
  Check(edf::native::ReadVertexCenterWord(draw,32)==5,"vertex centres remain live between draws");
  Reject([&]{edf::native::ReadVertexCenterWord(draw,UINT32_MAX);},"vertex-centre address overflow");
  draw.memory.resize(32+10563);
  Reject([&]{edf::native::ReadVertexCenterWord(draw,32);},"truncated vertex-centre word");
  draw.memory.resize(13000);
  ParameterReader textures;
  textures.memory.resize(2048);
  textures.Put(104,256); textures.Put(112,2);
  textures.Put(256,400); textures.Put(260,600); textures.Put(264,3);
  textures.Put(284,410); textures.Put(288,604); textures.Put(292,99);
  String(textures.memory,400,"local"); String(textures.memory,410,"unused");
  auto texture_records=edf::native::ReadTextureParameters(textures,32,false);
  Check(texture_records.size()==2 && texture_records[0].name=="local" &&
    texture_records[0].handle==600 && texture_records[0].slot==3 &&
    texture_records[1].slot==99 && textures.reads==4,"batched local texture records and unused slot");
  textures.Put(116,320); textures.Put(124,2);
  textures.Put(320,500); textures.Put(324,4);
  textures.Put(328,700); textures.Put(332,15);
  String(textures.memory,476,"short"); textures.Put(492,5); textures.Put(496,15); textures.Put(528,608);
  textures.Put(676,800); textures.Put(692,16); textures.Put(696,64); textures.Put(728,612);
  String(textures.memory,800,"global_long_name");
  textures.reads=0;
  texture_records=edf::native::ReadTextureParameters(textures,32,true);
  Check(texture_records.size()==2 && texture_records[0].name=="short" &&
    texture_records[0].handle==608 && texture_records[0].slot==4 &&
    texture_records[1].name=="global_long_name" && texture_records[1].handle==612 &&
    texture_records[1].slot==15 && textures.reads==8,"inline and heap global texture keys");
  textures.Put(728,616); textures.Put(676,850); String(textures.memory,850,"changed_tex_name");
  texture_records=edf::native::ReadTextureParameters(textures,32,true);
  Check(texture_records[1].handle==616 && texture_records[1].name=="changed_tex_name",
    "global texture handles and key pointers remain live");
  textures.Put(492,6);
  Reject([&]{edf::native::ReadTextureParameters(textures,32,true);},"global texture length mismatch");
  textures.Put(492,256);
  Reject([&]{edf::native::ReadTextureParameters(textures,32,true);},"oversized global texture key");
  textures.Put(492,5); textures.Put(496,4);
  Reject([&]{edf::native::ReadTextureParameters(textures,32,true);},"global key exceeds capacity");
  textures.Put(320,39);
  Reject([&]{edf::native::ReadTextureParameters(textures,32,true);},"global texture node underflow");
  textures.Put(112,4097);
  Reject([&]{edf::native::ReadTextureParameters(textures,32,false);},"oversized texture table");
  textures.Put(112,2); textures.Put(104,2040);
  Reject([&]{edf::native::ReadTextureParameters(textures,32,false);},"truncated texture table");
  textures.Put(112,0); textures.Put(104,0);
  Check(edf::native::ReadTextureParameters(textures,32,false).empty(),"empty texture vector");
  const uint32_t sampler_address=32+1024+15*24;
  for(uint32_t i=0;i<6;++i) textures.Put(sampler_address+i*4,0x01020300+i);
  textures.reads=0;
  const auto sampler_words=edf::native::ReadSamplerWords(textures,32,15);
  Check(sampler_words==std::array<uint32_t,4>{0x01020300,0x01020303,0x01020304,0x01020305} &&
    textures.reads==1,"single validated sampler block with correct word offsets");
  textures.Put(sampler_address+16,7);
  Check(edf::native::ReadSamplerWords(textures,32,15)[2]==7,"sampler state remains live");
  Reject([&]{edf::native::ReadSamplerWords(textures,32,16);},"invalid used sampler slot");
  Reject([&]{edf::native::ReadSamplerWords(textures,UINT32_MAX,0);},"sampler address overflow");
  Reject([&]{edf::native::ReadSamplerWords(textures,1008,0);},"truncated sampler block");
  ParameterReader named;
  {
    ParameterReader source; source.memory.resize(2048);
    source.Put(44,512); source.Put(52,1);
    source.Put(56,544); source.Put(64,1);
    source.Put(512,700); source.Put(516,800); source.Put(520,4); source.Put(524,8);
    source.Put(544,600); source.Put(548,700); source.Put(552,3); source.Put(556,12);
    source.Put(600,900); source.Put(608,4); String(source.memory,700,"owned");
    source.Put(104,1000); source.Put(112,1);
    source.Put(116,1040); source.Put(124,1);
    source.Put(1000,1100); source.Put(1004,0x1234); source.Put(1008,2);
    String(source.memory,1100,"local");
    source.Put(1040,1240); source.Put(1044,3);
    String(source.memory,1216,"global"); source.Put(1232,6); source.Put(1236,15);
    source.Put(1268,0x5678);
    edf::native::NativeMaterialParameters owners;
    owners.Publish(source,32); const auto first=owners.Get(32);
    Check((*first)[0].size()==1 && (*first)[1].size()==1 && (*first)[2].empty() &&
      (*first)[3].empty(),"native material stage groups");
    Check(first->textures[0].size()==1 && first->textures[1].size()==1 &&
      first->textures[0][0].name=="local" && first->textures[1][0].name=="global",
      "native local/global texture names");
    source.Put(1004,0x2345); source.Put(1008,7); source.Put(1268,0x6789); source.Put(1044,9);
    String(source.memory,1100,"other"); String(source.memory,1216,"change");
    const auto local_texture=first->textures[0][0].ReadValue(source,false);
    const auto global_texture=first->textures[1][0].ReadValue(source,true);
    Check(first->textures[0][0].name=="local" && first->textures[1][0].name=="global" &&
      local_texture.handle==0x2345 && local_texture.slot==7 &&
      global_texture.handle==0x6789 && global_texture.slot==9,"owned texture names/live handles and slots");
    source.Put(112,4097);
    Reject([&]{owners.Publish(source,32);},"invalid native texture replacement");
    Check(owners.Get(32)==first,"failed texture publication preserves material generation");
    source.Put(112,1);
    source.Put(516,816); source.Put(600,916); source.Put(608,5);
    String(source.memory,700,"other");
    Check((*first)[0][0].name=="owned" && (*first)[0][0].first==8 &&
      (*first)[0][0].ReadValue(source,false).data==816 &&
      (*first)[1][0].ReadValue(source,true).data==916 &&
      (*first)[1][0].ReadValue(source,true).available==5,"owned metadata/live values");
    source.Put(52,4097);
    Reject([&]{owners.Publish(source,32);},"invalid native material replacement");
    Check(owners.Get(32)==first,"failed material publication preserves old generation");
    source.Put(52,1); owners.Publish(source,32);
    Check(owners.Get(32)!=first && (*owners.Get(32))[0][0].name=="other",
      "material republish replaces metadata generation");
    owners.Publish(source,144); owners.Publish(source,145);
    owners.RetireArray(32,2);
    Reject([&]{owners.Get(32);},"material first array element retired");
    Reject([&]{owners.Get(144);},"material second array element retired");
    Check(owners.Get(145)!=nullptr && (*first)[0][0].name=="owned",
      "unaligned owner and retained generation survive retirement");
    owners.Retire(145); Reject([&]{owners.Get(145);},"scalar material retirement");
    Reject([&]{owners.RetireArray(UINT32_MAX-3,1);},"material extent overflow");
    source.Put(544,2044);
    Reject([&]{(*first)[1][0].ReadValue(source,true);},"truncated live material vector");
    source.Put(1040,39);
    Reject([&]{first->textures[1][0].ReadValue(source,true);},"invalid live texture node");
    source.Put(1040,2040);
    Reject([&]{first->textures[1][0].ReadValue(source,true);},"truncated live texture value");
  }
  named.Put(44,96); named.Put(52,1); // instance32 local vector
  named.Put(56,128); named.Put(64,1); // global vector
  named.Put(96,240); named.Put(100,400); named.Put(104,4); named.Put(108,8);
  named.Put(128,160); named.Put(132,240); named.Put(136,3); named.Put(140,12);
  named.Put(160,416); named.Put(168,4);
  String(named.memory,240,"value");
  auto local=edf::native::ReadNamedParameters(named,32,0,false);
  Check(local.size()==1 && local[0].name=="value" && local[0].data==400 &&
    local[0].registers==4 && local[0].first==8 && named.reads==3,"batched local parameter reads");
  named.reads=0;
  auto global=edf::native::ReadNamedParameters(named,32,0,true);
  Check(global.size()==1 && global[0].data==416 && global[0].registers==3 &&
    global[0].available==4 && global[0].first==12 && named.reads==4,"batched global parameter reads");
  named.Put(160,448); named.Put(168,5); String(named.memory,240,"new");
  global=edf::native::ReadNamedParameters(named,32,0,true);
  Check(global[0].data==448 && global[0].available==5 && global[0].name=="new",
    "global pointers, lengths and names must not be cached");
  Check(edf::native::ReadNamedParameters(named,32,36,false).empty(),"empty pixel parameters");
  Reject([&]{edf::native::ReadNamedParameters(named,32,12,false);},"invalid parameter stage");
  named.Put(52,4097);
  Reject([&]{edf::native::ReadNamedParameters(named,32,0,false);},"oversized named parameter table");
  named.Put(52,1); named.Put(44,508);
  Reject([&]{edf::native::ReadNamedParameters(named,32,0,false);},"truncated named parameter table");
  named.Put(44,96); named.Put(104,4097);
  Reject([&]{edf::native::ReadNamedParameters(named,32,0,false);},"oversized named register span");
  named.Put(128,508);
  Reject([&]{edf::native::ReadNamedParameters(named,32,0,true);},"truncated live global vector");
  struct InstanceReader {
    std::vector<uint32_t> words=std::vector<uint32_t>(128);
    uint32_t Add(uint32_t address,uint32_t offset) const {
      if(offset>UINT32_MAX-address) throw std::runtime_error("fixture address overflow");
      return address+offset;
    }
    mutable std::vector<uint8_t> encoded;
    mutable size_t reads=0;
    const uint8_t* Bytes(uint32_t address,size_t size) const {
      ++reads;
      if(!address || address>words.size()*4 || size>words.size()*4-address)
        throw std::runtime_error("fixture invalid read");
      encoded.resize(words.size()*4);
      for(size_t i=0;i<words.size();++i) for(size_t b=0;b<4;++b)
        encoded[i*4+b]=uint8_t(words[i]>>(24-b*8));
      return encoded.data()+address;
    }
  } instance_reader;
  constexpr uint32_t object=32, records=128;
  instance_reader.words[(object+4)/4]=400; // Deliberately invalid at the former wrong offsets.
  instance_reader.words[(object+8)/4]=4;
  instance_reader.words[(object+16)/4]=records;
  instance_reader.words[(object+20)/4]=records+24;
  instance_reader.words[records/4]=256;
  instance_reader.words[records/4+1]=3;
  instance_reader.words[records/4+2]=3;
  instance_reader.words[records/4+3]=304;
  instance_reader.words[records/4+4]=255;
  instance_reader.words[records/4+5]=1;
  const auto parameters=edf::native::ReadInstanceParameters(instance_reader,object);
  Check(instance_reader.reads==2,"instance list validates two blocks");
  Check(parameters.size()==2 && parameters[0].data==256 && parameters[0].first==3 &&
    parameters[0].count==3 && parameters[1].first==255 && parameters[1].count==1,
    "embedded instance vector offsets and records");
  instance_reader.words[records/4+5]=2;
  Reject([&]{edf::native::ReadInstanceParameters(instance_reader,object);},"instance register overflow");
  instance_reader.words[(object+20)/4]=records+1;
  Reject([&]{edf::native::ReadInstanceParameters(instance_reader,object);},"instance partial record");
  instance_reader.words[(object+20)/4]=records-12;
  Reject([&]{edf::native::ReadInstanceParameters(instance_reader,object);},"instance reversed range");
  instance_reader.words[(object+20)/4]=records+4097*12;
  Reject([&]{edf::native::ReadInstanceParameters(instance_reader,object);},"instance oversized list");
  instance_reader.words[(object+20)/4]=records;
  Check(edf::native::ReadInstanceParameters(instance_reader,object).empty(),"empty instance list");
  instance_reader.words[(object+20)/4]=records+12;
  instance_reader.words[records/4]=320;
  Check(edf::native::ReadInstanceParameters(instance_reader,object)[0].data==320,
    "instance records remain live across reads");
  instance_reader.words[(object+16)/4]=508;
  instance_reader.words[(object+20)/4]=520;
  Reject([&]{edf::native::ReadInstanceParameters(instance_reader,object);},"truncated instance record block");
  Reject([&]{edf::native::ReadInstanceParameters(instance_reader,UINT32_MAX);},"instance header address overflow");
  using namespace edf::native;
  Check(EffectSourceFingerprint("")==0xcbf29ce484222325ull,"empty source fingerprint");
  Check(EffectSourceFingerprint("hello")==0xa430d84680aabd0bull,"stable source fingerprint");
  Check(EffectSourceFingerprint("hello")!=EffectSourceFingerprint("Hello"),"source identity ignores changes");
  const std::vector<uint8_t> literal{'S','G','S','L',3,0,0,0,7,'a','b','c'};
  Check(DecodeSourceAsset(literal) == std::vector<uint8_t>({'a','b','c'}), "SGSL literals");
  const std::vector<uint8_t> overlap{'S','G','S','L',6,0,0,0,1,'a',0xee,0xf2};
  Check(DecodeSourceAsset(overlap) == std::vector<uint8_t>(6, 'a'), "overlapping ring copy");
  const std::vector<uint8_t> zeros{'S','G','S','L',3,0,0,0,0,0,0};
  Check(DecodeSourceAsset(zeros) == std::vector<uint8_t>(3, 0), "zero-initialized ring reference");
  for (size_t i = 4; i < literal.size(); ++i)
    Reject([&] { DecodeSourceAsset(std::span(literal).first(i)); }, "truncated literal accepted");
  auto invalid = overlap; invalid[4] = 5;
  Reject([&] { DecodeSourceAsset(invalid); }, "output overrun accepted");
  invalid = literal; invalid[7] = 0xff;
  Reject([&] { DecodeSourceAsset(invalid); }, "oversized allocation accepted");
  const auto data = Fixture();
  const auto effect = ParseEffect(data);
  Check(effect.source == "source" && effect.entries.size() == 2, "effect header/entries");
  Check(effect.techniques.size() == 1 && effect.techniques[0].name == "Main", "record-relative technique name");
  const auto& pass = effect.techniques[0].passes[0];
  Check(pass.vertex == "VS" && pass.pixel == "PS", "record-relative pass entry names");
  Check(pass.states.size() == 2 && pass.states[0].id == 0 && pass.states[1].id == 4 &&
        pass.states[0].value == 1 && pass.states[1].value == 1, "independent blend and depth write");
  auto guest = data;
  // Fixture's numeric records occupy [0,104); its strings start at 120.
  for (size_t at = 0; at < 104; at += 4) std::reverse(guest.begin() + at, guest.begin() + at + 4);
  const auto imported = ParseGuestEffect(guest);
  Check(imported.source == effect.source && imported.entries[1].name == "PS" &&
        imported.techniques[0].passes[0].states[1].value == 1, "guest endian conversion");
  Reject([&] { ParseGuestEffect(data); }, "disc bytes accepted as converted guest bytes");
  for (size_t i = 0; i < 207; ++i)
    Reject([&] { ParseEffect(std::span(data).first(i)); }, "truncated effect accepted");
  invalid = data; Word(invalid, 12, 0xfffffff0);
  Reject([&] { ParseEffect(invalid); }, "invalid array reference accepted");
  invalid = data; Word(invalid, 16, 0xffffffff);
  Reject([&] { ParseEffect(invalid); }, "invalid count accepted");
  invalid = data; Word(invalid, 80, 2);
  Reject([&] { ParseEffect(invalid); }, "invalid stage accepted");
  invalid = data; Word(invalid, 48, 134);
  Reject([&] { ParseEffect(invalid); }, "unresolved shader entry accepted");
  {
    using namespace edf::native;
    // Coverage identity is the contract, not the caller: a hundred draws of one
    // unsupported layout is one gap, and two layouts from one caller is two.
    NativeContractLedger ledger(3);
    NativeContract mesh;
    mesh.path=NativeContractPath::Indexed; mesh.vertex_source=1; mesh.pixel_source=2;
    const std::vector<uint8_t> declaration_bytes{1,2,3};
    mesh.declaration=HashNativeDeclaration(declaration_bytes); mesh.topology=4;
    const auto other_caller=mesh; // Callers are not part of identity at all.
    Check(ledger.RecordRejected(mesh,"unsupported semantic"),"first rejection was not distinct");
    Check(!ledger.RecordRejected(other_caller,"unsupported semantic"),
      "the same contract from another draw counted as a new gap");
    auto other_layout=mesh;
    const std::vector<uint8_t> other_bytes{9};
    other_layout.declaration=HashNativeDeclaration(other_bytes);
    Check(ledger.RecordRejected(other_layout,"unsupported stride"),"a different declaration was folded away");
    Check(ledger.counters().rejected==3 && ledger.counters().distinct_rejected==2,
      "rejected draw and distinct contract counts were not separated");
    Check(ledger.counters().rejected_by_path[size_t(NativeContractPath::Indexed)]==3,
      "rejections were not attributed to their draw path");
    Check(!ledger.clean(),"a ledger holding rejections reported clean coverage");
    const auto rejections=ledger.Rejections();
    Check(rejections.size()==2 && rejections[0].draws+rejections[1].draws==3,
      "retained rejections lost their draw counts");
    // Reaching the limit must be reported, never silently dropped.
    auto third=mesh; third.topology=7;
    Check(ledger.RecordRejected(third,"third"),"limit rejected a contract that still fits");
    auto fourth=mesh; fourth.topology=8;
    Check(!ledger.RecordRejected(fourth,"fourth") && ledger.counters().omitted_rejected==1,
      "a contract past the limit was dropped without being counted");
    Check(ledger.distinct_rejected()==3,"limit did not bound retained contracts");
    NativeContractLedger submitted(2);
    NativeContract drawn; drawn.vertex_source=5;
    Check(submitted.RecordSubmitted(drawn) && !submitted.RecordSubmitted(drawn) &&
      submitted.counters().submitted==2 && submitted.counters().distinct_submitted==1,
      "submitted contracts were not deduplicated");
    Check(submitted.clean(),"a ledger with no rejections reported unclean coverage");
    const std::vector<uint8_t> forward{1,2},reversed{2,1};
    Check(HashNativeDeclaration({})!=0,"empty declaration hash collided with unresolved");
    Check(HashNativeDeclaration(forward)!=HashNativeDeclaration(reversed),
      "declaration hash ignored byte order");
    Check(NativeContractPathName(NativeContractPath::Immediate)=="immediate" &&
      NativeContractPathName(NativeContractPath::Output)=="output","contract path names");
  }
  std::cout << "native effect tests: " << failures << " failures\n";
  return failures ? 1 : 0;
}
