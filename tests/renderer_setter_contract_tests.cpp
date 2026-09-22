// Unchanged generated bodies, real store macros, independent ordered-effect oracle.
#include "edf2017_pch.h"
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

struct SetterCase {
  const char* name;
  void (*fn)(PPCContext&, uint8_t*);
  uint8_t rule;
  int32_t value_disp;
  uint8_t value_width, value_source_reg;
  int32_t dirty_disp;
  uint64_t dirty_or_mask;
  uint8_t changed_count, changed_regs[3];
  bool holdout;
  uint8_t load_reg, merge_dest_reg, merge_source_reg, shift, mask_begin, mask_end;
  uint8_t first_shift, first_mb, first_me, second_shift, second_mb, second_me;
  int32_t li_immediate;
  uint8_t dirty_shift, dirty_mask_end;
};
static_assert(std::is_trivially_copyable_v<PPCContext>);
struct Write { uint32_t address; unsigned width; uint64_t value;
  bool operator==(const Write&) const = default;
};
static std::vector<Write> writes;
// These expand the real generated-header macros before instrumentation is enabled.
static void Real8(uint8_t* base, uint32_t address, uint8_t value) { REX_STORE_U8(address, value); }
static void Real32(uint8_t* base, uint32_t address, uint32_t value) { REX_STORE_U32(address, value); }
static void Real64(uint8_t* base, uint32_t address, uint64_t value) { REX_STORE_U64(address, value); }
static void Trace8(uint8_t* base, uint32_t a, uint8_t v) { writes.push_back({a,1,v}); Real8(base,a,v); }
static void Trace32(uint8_t* base, uint32_t a, uint32_t v) { writes.push_back({a,4,v}); Real32(base,a,v); }
static void Trace64(uint8_t* base, uint32_t a, uint64_t v) { writes.push_back({a,8,v}); Real64(base,a,v); }
#undef REX_STORE_U8
#undef REX_STORE_U32
#undef REX_STORE_U64
#define REX_STORE_U8(x,y) Trace8(base, (uint32_t)(x), (uint8_t)(y))
#define REX_STORE_U32(x,y) Trace32(base, (uint32_t)(x), (uint32_t)(y))
#define REX_STORE_U64(x,y) Trace64(base, (uint32_t)(x), (uint64_t)(y))
#include "renderer_setter_original_fixture.inc"

namespace {
constexpr size_t Size = 0x6000;
using Memory = std::array<uint8_t, Size>;
void Require(bool b, const std::string& text) { if (!b) throw std::runtime_error(text); }
uint64_t& Reg(PPCContext& c, unsigned r) {
  switch(r) { case 3:return c.r3.u64; case 4:return c.r4.u64; case 10:return c.r10.u64;
    case 11:return c.r11.u64; case 12:return c.r12.u64;
    default:throw std::runtime_error("Unsupported oracle register"); }
}
uint32_t Address(const PPCContext& c, int32_t disp, unsigned width) {
  uint32_t a=c.r3.u32+uint32_t(disp);
  Require(a % width == 0 && uint64_t(a)+width <= Size, "Outside ordinary aligned mapped memory");
  return a;
}
uint64_t Load(const Memory& m, uint32_t a, unsigned width) {
  uint64_t v=0; for(unsigned i=0;i<width;++i) v=v*256+m[a+i]; return v;
}
void Store(Memory& m, uint32_t a, unsigned width, uint64_t v) {
  for(unsigned i=0;i<width;++i) m[a+i]=uint8_t(v >> (8*(width-1-i)));
}
uint64_t Merge(uint64_t dest,uint32_t source,unsigned sh,unsigned begin,unsigned end) {
  for(unsigned bit=0;bit<32;++bit) {
    unsigned ppc=31-bit;
    bool selected=begin<=end ? ppc>=begin&&ppc<=end : ppc>=begin||ppc<=end;
    if(selected) { uint64_t mask=uint64_t{1}<<bit; dest&=~mask;
      if((source>>((bit+32-sh)%32))&1) dest|=mask; }
  }
  return dest;
}
void Precondition(const SetterCase& s,const PPCContext& c) {
  Require(s.value_width==1||s.value_width==4,"Unexpected scalar width in fixture");
  Address(c,s.value_disp,s.value_width);
  if(s.dirty_or_mask) Address(c,s.dirty_disp,8);
}
// PPC64 word masks use positions 32+MB through 32+ME. A wrapping
// interval includes the high half, whose bits repeat the rotated word.
uint64_t RotateWord(uint32_t source,unsigned sh,unsigned mb,unsigned me) {
  uint64_t result=0;
  for(unsigned bit=0;bit<64;++bit) {
    unsigned ppc=63-bit,begin=32+mb,end=32+me;
    bool selected=begin<=end ? ppc>=begin&&ppc<=end : ppc>=begin||ppc<=end;
    if(selected&&((source>>((bit+32-sh)%32))&1)) result|=uint64_t{1}<<bit;
  }
  return result;
}
uint64_t RotateDouble(uint64_t source,unsigned sh,unsigned me) {
  uint64_t result=0;
  for(unsigned bit=0;bit<64;++bit)
    if(63-bit<=me && ((source>>((bit+64-sh)%64))&1)) result|=uint64_t{1}<<bit;
  return result;
}
std::vector<Write> Expected(const SetterCase& s,PPCContext& c,Memory& m) {
  Precondition(s,c);
  auto a=Address(c,s.value_disp,s.value_width);
  if(s.rule==4) {
    c.r10.u64=RotateWord(uint32_t(Load(m,a,4)),s.second_shift,s.second_mb,s.second_me);
    c.r11.u64=RotateWord(c.r4.u32,s.first_shift,s.first_mb,s.first_me)|c.r10.u64;
    c.r12.u64=RotateDouble(uint64_t(int64_t(s.li_immediate)),s.dirty_shift,s.dirty_mask_end);
    Require(c.r12.u64==s.dirty_or_mask,"Dirty mask fixture/oracle disagreement");
  } else if(s.load_reg) {
    Reg(c,s.load_reg)=Load(m,a,4);
    Reg(c,s.merge_dest_reg)=Merge(Reg(c,s.merge_dest_reg),uint32_t(Reg(c,s.merge_source_reg)),
                                s.shift,s.mask_begin,s.mask_end);
  }
  uint64_t value=Reg(c,s.value_source_reg);
  if(s.value_width<8) value &= (uint64_t{1}<<(s.value_width*8))-1;
  Store(m,a,s.value_width,value);
  std::vector<Write> result{{a,s.value_width,value}};
  if(s.dirty_or_mask) {
    a=Address(c,s.dirty_disp,8);
    c.r11.u64=Load(m,a,8)|(s.rule==4 ? c.r12.u64 : s.dirty_or_mask);
    Store(m,a,8,c.r11.u64); result.push_back({a,8,c.r11.u64});
  }
  return result;
}
bool Matches(const PPCContext& a,const PPCContext& e,const Memory& m,const Memory& expected,
             const std::vector<Write>& effects) {
  return std::memcmp(&a,&e,sizeof(a))==0 && m==expected && writes==effects;
}
uint32_t Next(uint32_t& r) { r^=r<<13;r^=r>>17;r^=r<<5;return r; }
}

int main(int argc,char** argv) {
  auto start=std::chrono::steady_clock::now();
  try {
    Require(argc<=2,"usage: setter-contract-tests [report.json]");
    Require(std::size(kSetterCases)>0,"Empty setter fixture");
    Require(Merge(0xdeadbeef00000000ull,0x80000000,1,31,31)==0xdeadbeef00000001ull,"Merge high-half oracle");
    Require(Merge(0,0xffffffff,0,29,3)==0xf0000007,"Wrap mask oracle");
    Require(RotateWord(0x12345678,0,28,23)==0x1234567812345608ull,"PPC64 wrapping word mask");
    Require(RotateWord(0x80000001,4,0,27)==0x10,"PPC64 nonwrapping word mask");
    Require(RotateDouble(1,45,63)==0x200000000000ull,"Doubleword dirty mask oracle");
    std::vector<uint32_t> values{0,0xffffffff,0x80000001,0x01234567,0x89abcdef,0xa5a5a5a5};
    for(unsigned b=0;b<32;++b) { values.push_back(1u<<b);values.push_back(~(1u<<b)); }
    uint32_t random=0x20260923;for(unsigned i=0;i<128;++i) values.push_back(Next(random));
    size_t invocations=0,controls=0,holdout=0,alias_checks=0;
    alignas(32) Memory memory{},expected_memory{};
    for(const auto& s:kSetterCases) {
      if(s.holdout) ++holdout;
      for(uint32_t receiver:{0u,0x400u,0x1400u}) for(uint32_t value:values) {
        PPCContext actual,expected;
        std::memset(&actual,uint8_t(Next(random)),sizeof(actual));
        actual.r3.u64=0xdeadbeef00000000ull|receiver;
        actual.r4.u64=(uint64_t(Next(random))<<32)|value;
        for(auto& b:memory) b=uint8_t(Next(random));
        // Vary both old value bits and dirty flags, including already-set flags.
        Precondition(s,actual); expected_memory=memory;
        std::memcpy(&expected,&actual,sizeof(actual));
        auto effects=Expected(s,expected,expected_memory);
        writes.clear();s.fn(actual,memory.data());
        Require(Matches(actual,expected,memory,expected_memory,effects),std::string(s.name)+" mismatch");
        ++invocations;
      }
      PPCContext actual,expected;
      std::memset(&actual,0x5a,sizeof(actual));actual.r3.u64=0x400;actual.r4.u64=0x89abcdef01234567;
      memory.fill(0x96);
      if(s.dirty_or_mask) Store(memory,Address(actual,s.dirty_disp,8),8,0x0123456789abcde0ull);
      expected_memory=memory;std::memcpy(&expected,&actual,sizeof(actual));
      auto effects=Expected(s,expected,expected_memory);writes.clear();s.fn(actual,memory.data());
      Require(Matches(actual,expected,memory,expected_memory,effects),"Control baseline");
      actual.r12.u64^=1;Require(!Matches(actual,expected,memory,expected_memory,effects),"Register mutation");++controls;
      actual.r12.u64^=1;memory[0x20]^=1;
      Require(!Matches(actual,expected,memory,expected_memory,effects),"Extra write mutation");++controls;memory=expected_memory;
      if(s.rule==4) {
        actual.r10.u64^=uint64_t{1}<<32;
        Require(!Matches(actual,expected,memory,expected_memory,effects),"Lost high-half clobber mutation");++controls;
        actual.r10.u64^=uint64_t{1}<<32;
      }
      memory[effects[0].address]^=1;
      Require(!Matches(actual,expected,memory,expected_memory,effects),"Wrong value mutation");++controls;memory=expected_memory;
      // Byte-swap a real multi-byte write while leaving the rest of memory intact.
      const auto& wide=effects.back();
      Require(wide.width>1,"Control requires a multi-byte write");
      for(unsigned i=0;i<wide.width;++i) memory[wide.address+i]=expected_memory[wide.address+wide.width-1-i];
      Require(memory!=expected_memory,"Endian control lacks distinguishing bytes");
      Require(!Matches(actual,expected,memory,expected_memory,effects),"Endian mutation");++controls;memory=expected_memory;
      writes[0].width++;
      Require(!Matches(actual,expected,memory,expected_memory,effects),"Wrong width mutation");++controls;writes=effects;
      writes.pop_back();Require(!Matches(actual,expected,memory,expected_memory,effects),"Missing write mutation");++controls;writes=effects;
      if(effects.size()>1) { std::swap(writes[0],writes[1]);
        Require(!Matches(actual,expected,memory,expected_memory,effects),"Write order mutation");++controls;writes=effects; }
      actual.r3.u64=0xa0000000;
      bool rejected=false;try{Precondition(s,actual);}catch(const std::runtime_error&){rejected=true;}
      Require(rejected,"Unmapped precondition");++controls;
      if(s.value_width>1||s.dirty_or_mask) {
        actual.r3.u64=1;rejected=false;try{Precondition(s,actual);}catch(const std::runtime_error&){rejected=true;}
        Require(rejected,"Unaligned precondition");++controls;
      }
      // Exact accepted bodies may have disjoint offsets. These synthetic oracle
      // checks explicitly test sequential alias semantics, not nonexistent guest paths.
      if(s.dirty_or_mask) {
        auto alias=s;alias.value_disp=alias.dirty_disp;
        std::memset(&actual,0,sizeof(actual));actual.r3.u64=0x400;actual.r4.u64=0x12345678;
        memory.fill(0);auto events=Expected(alias,actual,memory);
        uint64_t first=events[0].value << (8*(8-alias.value_width));
        Require(events[1].value==(first|alias.dirty_or_mask),"Sequential overlapping-write oracle");++alias_checks;
      }
    }
    double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::string report="{\n  \"passed\": true,\n  \"functions\": "+std::to_string(std::size(kSetterCases))+
      ",\n  \"invocations\": "+std::to_string(invocations)+",\n  \"negative_controls\": "+std::to_string(controls)+
      ",\n  \"holdout_functions\": "+std::to_string(holdout)+",\n  \"synthetic_alias_oracle_checks\": "+std::to_string(alias_checks)+
      ",\n  \"seconds\": "+std::to_string(seconds)+
      ",\n  \"scope\": \"Instrumented unchanged generated guest bodies using real stores; independent state, memory and ordered-write oracle; ordinary memory only; no native equivalence\"\n}\n";
    if(argc==2){std::ofstream f(argv[1]);f<<report;Require(bool(f),"Report write failed");}
    std::cout<<report;return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
