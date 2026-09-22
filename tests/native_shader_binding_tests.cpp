#include "native_graphics/native_shader_binding.h"
#include "native_graphics/native_texture_binding.h"
#include <iostream>
#include <array>
#include <cstring>
using namespace edf::native;
namespace {
struct Reader {
  mutable std::array<uint8_t,65536> memory{};
  uint32_t watched_address=0xffffffff;
  mutable size_t watched_writes=0;
  uint32_t Add(uint32_t at,size_t size) const {
    if(size>memory.size() || at>memory.size()-size) throw std::runtime_error("range");
    return at+uint32_t(size);
  }
  const uint8_t* Bytes(uint32_t at,size_t size) const { Add(at,size); return memory.data()+at; }
  uint32_t Word(uint32_t at) const { const auto* p=Bytes(at,4); return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
  void StoreWord(uint32_t at,uint32_t value) const { Add(at,4); if(at==watched_address) ++watched_writes; for(unsigned i=0;i<4;++i) memory[at+i]=uint8_t(value>>(24-i*8)); }
  void StoreDoubleWord(uint32_t at,uint64_t value) const { StoreWord(at,uint32_t(value>>32)); StoreWord(at+4,uint32_t(value)); }
  void StoreByte(uint32_t at,uint8_t value) const { Add(at,1); memory[at]=value; }
};
void Require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
void Run() {
  Reader reader;
  constexpr uint32_t device=0x1000,shader=0x8000,data=shader+40+256;
  reader.StoreWord(shader+60,256);
  reader.StoreDoubleWord(data,0xf0);
  reader.StoreDoubleWord(data+8,1);
  const std::array<uint32_t,11> program{1,0xdeadbeef,0,(16u<<16)|2,0x11223344,0x55667788,0,(24u<<16)|2,0xffffff00,0x44,0};
  reader.StoreWord(data+24,program.size()*4);
  for(size_t i=0;i<program.size();++i) reader.StoreWord(data+32+uint32_t(i*4),program[i]);
  reader.StoreWord(device+1024+24,0xaabbccdd);
  reader.StoreDoubleWord(device+8,0xff);
  SetNativeShaderResource(reader,device,shader,true,[]{throw std::runtime_error("unexpected allocation"); return 0u;},[]{return 0u;});
  Require(reader.Word(device+12416)==shader,"pixel binding missing");
  Require(reader.Word(device+12)==0x0f,"shader constant mask not cleared");
  Require(reader.Word(device+16)==0x120000,"pixel/depth dirty bits changed");
  Require(reader.Word(device+28)==2,"shared defaults not dirtied");
  Require(reader.Word(device+1040)==0x11223344 && reader.Word(device+1044)==0x55667788 && reader.Word(device+1048)==0xaabbcc44,
    "shader copy/mask defaults decoded incorrectly");
  reader.StoreWord(device+10780,37);
  SetNativeShaderResource(reader,device,0,true,[]{return 0u;},[]{return 0u;});
  Require(reader.Word(shader+8)==37 && reader.Word(device+12416)==0,"shader retirement fence or unbind failed");
  reader.StoreByte(device+10810,0xff);
  SetNativeShaderResource(reader,device,0,false,[]{return 0u;},[]{return 0u;});
  Require(*reader.Bytes(device+10810,1)==0x7f && reader.Word(device+16)==0x1a0000,"vertex flags changed");
  reader.StoreWord(data+32+7*4,(24u<<16)|1);
  bool rejected=false;
  try { (void)ReadNativeShaderDefaults(reader,shader,true); } catch(const std::exception&) { rejected=true; }
  Require(rejected,"odd mask-pair extent accepted");
  // PPC 82149608 retires the previous resource before reading the new shader's
  // defaults. Here the retirement fence aliases the defaults' clear mask.
  // A snapshot taken before retirement incorrectly retains constant bit 5.
  Reader alias;
  constexpr uint32_t old_resource=data-4;
  alias.StoreWord(shader+60,256);
  alias.StoreWord(device+12416,old_resource);
  alias.StoreWord(device+10780,0x20);
  alias.StoreDoubleWord(device+8,0xff);
  SetNativeShaderResource(alias,device,shader,true,[]{return 0u;},[]{return 0u;});
  Require(alias.Word(device+12)==0xdf,"shader defaults sampled before retirement");
  // Invalid defaults are diagnosed after the original's binding/dirty writes.
  alias.StoreWord(data+24,3);
  alias.StoreWord(device+12416,0);
  alias.StoreDoubleWord(device+16,0);
  rejected=false;
  try { SetNativeShaderResource(alias,device,shader,true,[]{return 0u;},[]{return 0u;}); }
  catch(const std::exception&) { rejected=true; }
  Require(rejected && alias.Word(device+12416)==shader && alias.Word(device+16)==0x120000,
    "shader default failure reordered binding effects");
  Reader absent;
  absent.watched_address=device+8;
  SetNativeShaderResource(absent,device,shader,true,[]{return 0u;},[]{return 0u;});
  Require(absent.watched_writes==0,"absent shader defaults wrote the constant mask");
  absent.watched_address=device+16;
  SetNativeShaderResource(absent,device,0,true,[]{return 0u;},[]{return 0u;});
  Require(absent.watched_writes==2,"pixel dirty stores were combined");
  constexpr uint32_t texture=0xa000,slot=3,record=device+1024+slot*24;
  const std::array<uint32_t,6> old{0x003ffc00,0x800,0,0x7ff80000,0xa5000003,0x1ab};
  const std::array<uint32_t,6> header{0x80000001,0x10000,0x12345678,0x80001234,(2u<<2)|(12u<<6),0x20000};
  for(uint32_t i=0;i<6;++i) { reader.StoreWord(record+i*4,old[i]); reader.StoreWord(texture+28+i*4,header[i]); }
  reader.StoreByte(device+11678+slot,5); reader.StoreByte(device+11704+slot,9);
  reader.StoreDoubleWord(device+16,0x20);
  SetNativeTextureResource(reader,device,slot,texture,uint64_t(1)<<40,[]{return 0u;},[]{return 0u;});
  Require(reader.Word(device+12272+slot*4)==texture,"native texture binding missing");
  Require(reader.Word(record)==0x803ffc01 && reader.Word(record+4)==0x10800 && reader.Word(record+8)==0x12345678,
    "texture descriptor address/reserved bits changed");
  Require(reader.Word(record+12)==0xfff81234 && reader.Word(record+16)==0xa5000257 && reader.Word(record+20)==0x201ab,
    "texture filters/LOD clamps/mip reserved bits changed");
  const auto prior_descriptor=reader.Word(record);
  SetNativeTextureResource(reader,device,slot,0,uint64_t(1)<<40,[]{return 0u;},[]{return 0u;});
  Require(reader.Word(texture+8)==37 && reader.Word(device+12272+slot*4)==0 && reader.Word(record)==prior_descriptor,
    "texture unbind lost descriptor inheritance or retirement");
  for(uint32_t initialization_slot=0;initialization_slot<26;++initialization_slot)
    SetNativeTextureResource(reader,device,initialization_slot,0,0,[]{return 0u;},[]{return 0u;});
  Require(reader.Word(device+12272+25*4)==0,"last device texture slot was not initialized");
  rejected=false;
  try { SetNativeTextureResource(reader,device,26,0,0,[]{return 0u;},[]{return 0u;}); }
  catch(const std::exception&) { rejected=true; }
  Require(rejected,"out-of-device texture slot accepted");
}
}
int main() { try { Run(); std::cout<<"Native shader binding checks passed\n"; } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
