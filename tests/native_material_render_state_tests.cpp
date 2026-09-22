#include "native_graphics/native_material_render_state.h"
#include <iostream>

using namespace edf::native;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
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
}
}
int main() {
  try { Run(); std::cout<<"Native material render-state checks passed\n"; }
  catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
