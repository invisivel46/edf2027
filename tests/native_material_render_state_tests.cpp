#include "native_graphics/native_material_render_state.h"
#include "native_graphics/native_material_cpu_program.h"
#include "native_graphics/guest_block.h"
#include <iostream>

using namespace edf::native;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
// A flat guest arena with the GuestReader contract the activation relies on:
// every Bytes/WritableBytes call is one validation (counted), stores are
// big-endian words through WritableBytes. The material colour-scale constant
// the render pass reader checks lives outside the arena, as in the game.
struct ArenaReader {
  std::vector<uint8_t> memory=std::vector<uint8_t>(0x10000);
  std::array<uint8_t,4> color_scale{0x3b,0x80,0x80,0x81};
  uint32_t read_only_begin=0,read_only_end=0;
  mutable size_t validations=0;
  uint32_t Add(uint32_t address,uint32_t offset) const {
    if(offset>UINT32_MAX-address) throw std::runtime_error("arena address overflow");
    return address+offset;
  }
  const uint8_t* Bytes(uint32_t address,size_t size) const {
    ++validations;
    if(address==0x8200964c && size<=4) return color_scale.data();
    if(!address || address>memory.size() || size>memory.size()-address) throw std::runtime_error("arena read outside memory");
    return memory.data()+address;
  }
  uint32_t Word(uint32_t address) const { return GuestBlockWord(Bytes(address,4)); }
  uint64_t DoubleWord(uint32_t address) const {
    const auto* p=Bytes(address,8); return (uint64_t(GuestBlockWord(p))<<32)|GuestBlockWord(p+4);
  }
  std::string String(uint32_t,size_t) const { throw std::runtime_error("arena has no strings"); }
  const uint8_t* WritableBytes(uint32_t address,size_t size,size_t alignment) const {
    if(!size || (alignment!=4 && alignment!=8) || address%alignment) throw std::runtime_error("arena write alignment");
    const auto* bytes=Bytes(address,size);
    if(address<read_only_end && uint64_t(address)+size>read_only_begin) throw std::runtime_error("arena write to read-only bytes");
    return bytes;
  }
  void StoreWord(uint32_t address,uint32_t value) const {
    auto* p=const_cast<uint8_t*>(WritableBytes(address,4,4));
    for(int i=0;i<4;++i) p[i]=uint8_t(value>>(24-i*8));
  }
  void StoreDoubleWord(uint32_t address,uint64_t value) const {
    auto* p=const_cast<uint8_t*>(WritableBytes(address,8,8));
    for(int i=0;i<8;++i) p[i]=uint8_t(value>>(56-i*8));
  }
  void StoreByte(uint32_t address,uint8_t value) const {
    const auto aligned=address&~3u;
    const_cast<uint8_t*>(WritableBytes(aligned,4,4))[address-aligned]=value;
  }
  void Put(uint32_t address,uint32_t value) { for(int i=0;i<4;++i) memory.at(address+i)=uint8_t(value>>(24-i*8)); }
  uint32_t Get(uint32_t address) const { return GuestBlockWord(memory.data()+address); }
};
template<class F> bool Throws(F&& function) {
  try { function(); } catch(const std::exception&) { return true; }
  return false;
}
// The live window's contract: one validation up front, then plain loads and
// big-endian exchanges on the live bytes; the reader for anything else.
void RunWritableWindow() {
  ArenaReader arena;
  arena.Put(0x1000,0x11223344); arena.Put(0x1004,0x55667788);
  const GuestWritableWindow window(arena,0x1000,0x100);
  Require(window.proven() && arena.validations==1,"writable window validates its block once");
  Require(window.Word(0x1000)==0x11223344 && window.DoubleWord(0x1000)==0x1122334455667788ull &&
    window.Bytes(0x10fc,4)==arena.memory.data()+0x10fc && arena.validations==1,"window reads do not revalidate");
  window.StoreWord(0x1008,0xa1b2c3d4);
  window.StoreDoubleWord(0x1010,0x0102030405060708ull);
  window.StoreByte(0x1019,0xee);
  Require(arena.Get(0x1008)==0xa1b2c3d4 && arena.Get(0x1010)==0x01020304 && arena.Get(0x1014)==0x05060708 &&
    arena.memory[0x1019]==0xee && arena.validations==1,"window stores are big-endian and unvalidated");
  arena.Put(0x1020,0xcafef00d);
  Require(window.Word(0x1020)==0xcafef00d,"window reads live bytes, not a copy");
  Require(window.WritableBytes(0x1040,16,4)==arena.memory.data()+0x1040 &&
    window.WritableBytes(0x1048,8,8)==arena.memory.data()+0x1048 && arena.validations==1,
    "window hands out its own writable bytes");
  window.Word(0x10fe); window.Word(0x2000); window.StoreWord(0x2000,1);
  Require(arena.validations==1+3 && arena.Get(0x2000)==1,"crossing and outside accesses go to the reader");
  Require(Throws([&] { window.StoreWord(0x1002,1); }) && Throws([&] { window.WritableBytes(0x1002,4,4); }) &&
    Throws([&] { window.WritableBytes(0x1000,0,4); }) && Throws([&] { window.Bytes(0xfff8,16); }),
    "window keeps the reader's rejections");
  // A block the reader cannot prove costs speed only: nothing is refused that
  // the reader accepts, and nothing accepted that it refuses.
  arena.read_only_begin=0x3000; arena.read_only_end=0x3004;
  arena.validations=0;
  const GuestWritableWindow unproven(arena,0x3000,0x100);
  Require(!unproven.proven() && arena.validations==1,"unwritable block leaves the window unproven");
  Require(unproven.Word(0x3000)==0 && arena.validations==2,"unproven window reads through the reader");
  unproven.StoreWord(0x3010,7);
  Require(arena.Get(0x3010)==7 && arena.validations==3,"unproven window stores through the reader");
  Require(Throws([&] { unproven.StoreWord(0x3000,1); }),"unproven window keeps the reader's refusal");
  for(const auto& [address,size]:std::array<std::pair<uint32_t,size_t>,3>{{{0,16},{0x1000,0},{0xfffffff0,32}}}) {
    arena.validations=0;
    const GuestWritableWindow invalid(arena,address,size);
    Require(!invalid.proven() && !arena.validations,"null, empty and wrapping windows are never proven");
  }
}
// The activation's program and CPU writes, read and written through the device
// window (and the material header window inside it) as the live activation
// now does, against the plain reader: the same program, the same guest bytes,
// and a fraction of the validations.
void RunActivationThroughWindows() {
  constexpr uint32_t device=0x1000,instance=0x6000;
  ArenaReader arena;
  const auto header=[&](uint32_t at,uint32_t records,uint32_t count) { arena.Put(at,records); arena.Put(at+8,count); };
  arena.Put(instance+108,0x6100); arena.Put(0x6100,0x6200); arena.Put(0x6200,0x7000);
  arena.Put(0x6104,0x6300); arena.Put(0x6304,0x7100);
  header(instance+0,0x6400,1);   // Local vertex: first 4, two registers at 0x6800.
  arena.Put(0x6400,4); arena.Put(0x6404,0x6800); arena.Put(0x640c,2);
  header(instance+24,0x6500,1);  // Global vertex: first 10, one register through 0x6600.
  arena.Put(0x6500,0x6600); arena.Put(0x6600,0x6900); arena.Put(0x6508,1); arena.Put(0x650c,10);
  header(instance+72,0x6a00,1);  // Local texture: slot 3, handle 0x7200.
  arena.Put(0x6a04,0x7200); arena.Put(0x6a08,3);
  for(uint32_t i=0;i<4;++i) arena.Put(0x6a0c+i*4,0x100+i);
  header(instance+96,0x6b00,3);  // Blend enable, destination factor, cull.
  const std::array<std::pair<uint32_t,uint32_t>,3> states{{{0x3c,1},{0x4c,7},{0x38,2}}};
  for(size_t i=0;i<states.size();++i) {
    arena.Put(0x6b00+uint32_t(i)*8,states[i].first); arena.Put(0x6b04+uint32_t(i)*8,states[i].second);
    arena.Put(device+56+states[i].first,NativeMaterialStateSetter(states[i].first));
  }
  for(uint32_t i=0;i<12;++i) arena.Put(0x6800+i*4,0x3f800000+i);
  arena.Put(device+11576,0x0006); arena.Put(device+10424,0x10001);
  // windowed: the material header through its own window, as the activation
  // reads it; otherwise every field through the reader, as it used to.
  const auto activate=[&](const auto& reader,NativeMaterialCpuProgram& program,bool windowed) {
    if(windowed) ReadNativeMaterialCpuProgram(GuestReadWindow(reader,instance,112),instance,device,program);
    else ReadNativeMaterialCpuProgram(reader,instance,device,program);
    for(const auto& constant:program.constants) {
      const auto first=constant.first/4,last=(constant.first+constant.count-1)/4;
      const uint64_t mask=(~uint64_t(0)>>first)&(~uint64_t(0)<<(63-last));
      Require(UploadNativeMaterialConstant(reader,device,constant,mask),"arena constant is disjoint and aligned");
    }
    for(const auto& state:program.states) {
      const auto dirty=[&](uint32_t offset) { return reader.DoubleWord(device+offset); };
      const auto writes=NativeMaterialStateCpuWrites(ReadNativeMaterialRenderPass(reader,device),
        state.offset,state.value,dirty(16),dirty(24));
      Require(writes.has_value(),"arena states have CPU writes");
      for(const auto& [offset,value]:*writes) reader.StoreWord(reader.Add(device,offset),value);
    }
  };
  ArenaReader plain_arena=arena;
  NativeMaterialCpuProgram plain;
  activate(plain_arena,plain,false);
  const auto plain_validations=plain_arena.validations;
  // The reused program arrives holding a different material's lists.
  NativeMaterialCpuProgram reused;
  reused.constants.assign(9,{true,1,2,3}); reused.textures.resize(5); reused.states.assign(7,{1,2,3});
  reused.vertex=reused.pixel=0xdead;
  arena.validations=0;
  activate(GuestWritableWindow(arena,device,13520),reused,true);
  Require(plain.vertex==0x7000 && plain.pixel==0x7100 && plain.constants.size()==2 &&
    plain.textures.size()==1 && plain.states.size()==3,"arena program shape");
  Require(reused.vertex==plain.vertex && reused.pixel==plain.pixel &&
    reused.constants.size()==plain.constants.size() && reused.textures.size()==plain.textures.size() &&
    reused.states.size()==plain.states.size(),"reused program keeps only this material's lists");
  for(size_t i=0;i<plain.constants.size();++i) {
    const auto& a=plain.constants[i]; const auto& b=reused.constants[i];
    Require(a.pixel==b.pixel && a.first==b.first && a.data==b.data && a.count==b.count,"windowed constant operation");
  }
  Require(reused.textures[0].slot==3 && reused.textures[0].handle==0x7200 &&
    reused.textures[0].sampler.settings==plain.textures[0].sampler.settings,"windowed texture operation");
  for(size_t i=0;i<plain.states.size();++i)
    Require(reused.states[i].offset==plain.states[i].offset && reused.states[i].setter==plain.states[i].setter &&
      reused.states[i].value==plain.states[i].value,"windowed state operation");
  Require(arena.memory==plain_arena.memory,"windowed activation writes the same guest bytes");
  Require(arena.Get(device+10424)==0x07060706 && arena.Get(device+20)!=0,"arena blend and dirty words written");
  Require(arena.validations*3<plain_validations,"windowed activation validates a fraction of the accesses");
  // A throw mid-read leaves a partial program; the next read starts clean.
  arena.Put(instance+96+8,5000);
  Require(Throws([&] { ReadNativeMaterialCpuProgram(arena,instance,device,reused); }),"oversized state list rejected");
  arena.Put(instance+96+8,3);
  ReadNativeMaterialCpuProgram(arena,instance,device,reused);
  Require(reused.constants.size()==2 && reused.states.size()==3,"program read after a throw is complete");
}
void Run() {
  NativeMaterialRenderPass pass;
  pass.words[0]=0x10001;
  ApplyNativeMaterialState(pass,0x48,6); ApplyNativeMaterialState(pass,0x4c,7);
  Require(pass.words[0]==0x10001 && pass.blend_parameters==0x706,"disabled blending lost requested factors");
  ApplyNativeMaterialState(pass,0x3c,1);
  Require(pass.words[0]==0x07060706,"unified blend did not restore requested factors");
  ApplyNativeMaterialState(pass,0x54,1); ApplyNativeMaterialState(pass,0x58,0);
  Require(pass.words[0]==0x07060706,"disabled separate alpha changed effective blend");
  ApplyNativeMaterialState(pass,0x40,1);
  Require(pass.words[0]==0x00010706,"separate alpha did not restore saved alpha factors");
  ApplyNativeMaterialState(pass,0x3c,0);
  Require(pass.words[0]==0x10001,"blend disable did not restore opaque state");
  ApplyNativeMaterialState(pass,0x44,0xff804020);
  Require(std::bit_cast<float>(pass.blend_factor[0])==128.f*std::bit_cast<float>(0x3b808081u) &&
    std::bit_cast<float>(pass.blend_factor[3])==1.f,"ARGB blend factor channel conversion changed");
  ApplyNativeMaterialState(pass,0x28,1);
  Require(pass.depth_requested==1 && !(pass.words[1]&2),"absent depth target did not suppress depth testing");
  pass.depth_target=42; ApplyNativeMaterialState(pass,0x28,1); ApplyNativeMaterialState(pass,0x30,1);
  ApplyNativeMaterialState(pass,0x2c,3);
  Require((pass.words[1]&0x76)==0x36,"depth enable/write/function composition changed");
  ApplyNativeMaterialState(pass,0xd4,15);
  Require(pass.color_requested[0]==15 && pass.words[4]==0,"absent color target did not suppress writes");
  pass.color_targets[0]=24; ApplyNativeMaterialState(pass,0xd4,5);
  Require(pass.words[4]==5,"bound target color mask lost");
  ApplyNativeMaterialState(pass,0x38,2); ApplyNativeMaterialState(pass,0xc8,7);
  Require((pass.words[2]&7)==2 && pass.words[5]==1 && pass.scissor_enabled==7,"cull/scissor semantics changed");
  const auto decoded=DecodeNativeRenderState(pass.words);
  Require(decoded.depth_enable && decoded.depth_write && decoded.cull==kNativeCullBack && decoded.scissor,
    "resolved pass does not produce expected native pipeline state");
  bool rejected=false;
  try { ApplyNativeMaterialState(pass,0xffffffff,0); } catch(const std::exception&) { rejected=true; }
  Require(rejected,"undecoded material override silently ignored");
  const auto has=[](const auto& writes,uint32_t offset,uint32_t value) {
    return writes && std::find(writes->begin(),writes->end(),std::pair{offset,value})!=writes->end();
  };
  NativeMaterialRenderPass cpu;
  auto writes=NativeMaterialStateCpuWrites(cpu,0x28,1,0x20,0);
  Require(has(writes,11604,1) && has(writes,10420,0) && has(writes,16,0x20000) && has(writes,20,0x820),
    "CPU depth writes lost target suppression or dirty bits");
  writes=NativeMaterialStateCpuWrites(cpu,0x48,6,0,0);
  Require(writes && writes->size()==1 && has(writes,11576,6),"disabled blend dirtied effective state");
  cpu.blend_parameters=0x706;
  writes=NativeMaterialStateCpuWrites(cpu,0x3c,1,0x1000,0);
  Require(has(writes,10424,0x07060706) && has(writes,10456,0x07060706) &&
    has(writes,10460,0x07060706) && has(writes,10464,0x07060706) && has(writes,20,0x1407),
    "CPU blend replication or dirty flags changed");
  cpu.color_targets[2]=1;
  writes=NativeMaterialStateCpuWrites(cpu,0xdc,5,0,0x80);
  Require(has(writes,11596,5) && has(writes,10332,0x500) && has(writes,28,0x40080),
    "CPU MRT color mask writes changed");
  Require(!NativeMaterialStateCpuWrites(cpu,0xc8,1,0,0),"scissor rectangle owner was bypassed");
  // Captured static-group state list: 0x38=2, 0x30=1, 0x3c=1, 0x48=6, 0x4c=7.
  NativeMaterialRenderPass live;
  live.words[0]=0x10001;
  writes=NativeMaterialStateCpuWrites(live,0x38,2,0,0);
  Require(writes && writes->size()==3 && has(writes,10440,2) && has(writes,16,0) && has(writes,20,64),
    "CPU cull write or raster dirty flag changed");
  live.words[2]=0x1c; writes=NativeMaterialStateCpuWrites(live,0x38,0xfffffffa,0,0);
  Require(has(writes,10440,0x1a),"cull insertion leaked outside its field");
  writes=NativeMaterialStateCpuWrites(live,0x30,1,0,0);
  Require(writes && writes->size()==3 && has(writes,10420,4) && has(writes,20,0x800),
    "CPU depth-write enable or depth dirty flag changed");
  live.words[1]=0x36; writes=NativeMaterialStateCpuWrites(live,0x30,0,uint64_t(1)<<49,0);
  Require(has(writes,10420,0x32) && has(writes,16,0x20000) && has(writes,20,0x800),
    "depth-write disable changed other depth fields or dropped pending dirty bits");
  live.words[1]=0;
  writes=NativeMaterialStateCpuWrites(live,0x4c,7,0,0);
  Require(writes && writes->size()==1 && has(writes,11576,0x700),"disabled blend dirtied effective state for destination factor");
  live.blend_parameters=0x1f06; live.blend_control=0x80000000u;
  writes=NativeMaterialStateCpuWrites(live,0x4c,7,0,0);
  Require(has(writes,11576,0x706) && has(writes,10424,0x07060706) && has(writes,10464,0x07060706) && has(writes,20,0x407),
    "enabled destination factor lost replication or blend dirty flags");
  live.blend_control=0x40000000u|0x80000000u; live.blend_parameters=0x00010006;
  writes=NativeMaterialStateCpuWrites(live,0x4c,7,0,0);
  Require(has(writes,10424,0x00010706),"separate alpha destination factor changed alpha factors");
  NativeMaterialRenderPass captured;
  captured.words[0]=0x10001;
  for(const auto& [offset,value]:std::array<std::pair<uint32_t,uint32_t>,5>{{{0x38,2},{0x30,1},{0x3c,1},{0x48,6},{0x4c,7}}})
    ApplyNativeMaterialState(captured,offset,value);
  Require(captured.words[0]==0x07060706 && captured.words[1]==4 && captured.words[2]==2,
    "captured material state list resolved differently");
  const auto blended=DecodeNativeRenderState(captured.words);
  Require(blended.blend_enable && blended.src_color==kNativeBlendSrcAlpha && blended.dst_color==kNativeBlendInvSrcAlpha &&
    blended.src_alpha==kNativeBlendSrcAlpha && blended.dst_alpha==kNativeBlendInvSrcAlpha &&
    blended.cull==kNativeCullBack && blended.depth_write && !blended.depth_enable,"captured material pass decode changed");
  NativeMaterialRenderPass inherited=captured;
  ApplyNativeMaterialState(inherited,0x3c,0);
  Require(inherited.words[0]==0x10001 && inherited.blend_parameters==0x706,"opaque override after blended pass changed");
  // The fixed list holds the largest override (a blend update: its control
  // word, four replicated blend words and both dirty halves) and refuses more.
  NativeMaterialRenderPass largest;
  writes=NativeMaterialStateCpuWrites(largest,0x3c,1,0,0);
  Require(writes && writes->size()==7 && (*writes)[0]==std::pair<uint32_t,uint32_t>{11580,0x80000000u} &&
    (*writes)[6].first==20,"blend update write list order or size changed");
  NativeMaterialStateCpuWriteList full;
  for(uint32_t i=0;i<8;++i) full.emplace_back(i,i);
  Require(Throws([&] { full.emplace_back(8,8); }) && full.size()==8,"CPU write list overflow is refused");
}
}
int main() {
  try { Run(); RunWritableWindow(); RunActivationThroughWindows(); std::cout<<"Native material render-state checks passed\n"; }
  catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
