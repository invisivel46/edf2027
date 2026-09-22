#include "native_graphics/native_material_sampler.h"
#include <iostream>

using namespace edf::native;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void TestSamplerProgram() {
  NativeMaterialSamplerPass initial;
  initial.words={0x00024800,0,0x000002c4,3}; // Addressing, LOD 1..11, border metadata.
  initial.min_lod=3; initial.max_lod=9;
  NativeMaterialSamplerOperation operation;
  operation.texture_lod=(2u<<2)|(12u<<6);
  operation.settings={std::bit_cast<uint32_t>(-.125f),1,1,1};
  const auto filtered=ApplyNativeMaterialSampler(initial,operation);
  Require(initial.words[2]==0x2c4,"material program mutated inherited input");
  Require(filtered.words[0]==initial.words[0] && filtered.words[3]==3,"material changed inherited addressing/border");
  Require(((filtered.words[2]>>2)&15)==3 && ((filtered.words[2]>>6)&15)==9,"texture LOD limits were not clamped against pass limits");
  Require(((filtered.words[2]>>12)&1023)==1020,"negative bias lost signed ten-bit representation");
  Require(((filtered.words[1]>>19)&63)==21,"point/linear/mip filters resolved incorrectly");
  operation.texture_lod.reset();
  operation.settings={std::bit_cast<uint32_t>(.04f),2,0,0};
  const auto unbound=ApplyNativeMaterialSampler(filtered,operation);
  Require((unbound.words[2]&0x3fc)==(filtered.words[2]&0x3fc),"null texture erased inherited LOD range");
  Require(((unbound.words[2]>>12)&1023)==1 && ((unbound.words[1]>>23)&3)==2,"bias did not truncate or base-only mip was lost");
  initial.anisotropy=5;
  operation.settings={0,1,4,4};
  const auto anisotropic=ApplyNativeMaterialSampler(initial,operation);
  Require(((anisotropic.words[1]>>25)&7)==5 && (anisotropic.words[2]&0xc00)==0xc00,
    "anisotropic min/mag aliases did not use explicit pass metadata");
  auto min_changed=anisotropic;
  ApplyNativeMaterialFilter(min_changed,0,false);
  Require(((min_changed.words[1]>>25)&7)==5 && (min_changed.words[2]&0xc00)==0x400,
    "changing min discarded the other filter's anisotropy alias");
  ApplyNativeMaterialFilter(min_changed,1,true);
  Require(((min_changed.words[1]>>25)&7)==0 && (min_changed.words[2]&0xc00)==0,
    "clearing both aliases retained anisotropy");
  for(const auto& [bits,expected]:std::array<std::pair<uint32_t,uint32_t>,7>{{
      {0x7fc00000,0},{0x7f800000,1023},{0xff800000,0},{0x5e800000,1023},{0xde800000,0},{0x80000000,0},{0x5c800000,0}}}) {
    operation.settings[0]=bits;
    Require(((ApplyNativeMaterialSampler(initial,operation).words[2]>>12)&1023)==expected,
      "bias conversion boundary differs from guest fctidz endpoint");
  }
  for(uint8_t mip=4;mip<=6;++mip) {
    auto pass=initial; pass.mip_override=mip;
    ApplyNativeMaterialFilter(pass,1,true);
    Require((pass.words[2]&3)==(mip&3),"explicit mip alias override ignored");
  }
  // Final state depends on order when two records use the same slot.
  auto first=operation,second=operation;
  first.texture_lod=0x3c0; first.settings={0,1,1,1};
  second.texture_lod.reset(); second.settings={0,0,0,0};
  const auto forward=ApplyNativeMaterialSampler(ApplyNativeMaterialSampler(initial,first),second);
  const auto reverse=ApplyNativeMaterialSampler(ApplyNativeMaterialSampler(initial,second),first);
  Require(forward.words!=reverse.words,"material texture operation order was lost");
}
void TestInheritedSamplerProjection() {
  uint32_t random=0x71a934cdu;
  const auto next=[&] { random=random*1664525u+1013904223u; return random; };
  for(uint32_t sample=0;sample<1024;++sample) {
    NativeMaterialSamplerPass raw;
    for(auto& word:raw.words) word=next();
    raw.anisotropy=sample%6; raw.min_lod=sample%8; raw.max_lod=8+sample%8;
    raw.mip_override=sample%7;
    auto native=NativeMaterialSamplerInputs(raw);
    for(uint32_t step=0;step<4;++step) {
      NativeMaterialSamplerOperation operation;
      if(step&1) { operation.texture_lod=next()&0x3fc; operation.texture_filter_high=next()&0x80000000u; }
      operation.settings={std::bit_cast<uint32_t>(float(int(sample%17)-8)*.125f),
        next()%3,next()%8,next()%8};
      raw=ApplyNativeMaterialSampler(raw,operation);
      native=NativeMaterialSamplerInputs(ApplyNativeMaterialSampler(native,operation));
      Require(native==NativeMaterialSamplerInputs(raw),
        "discarded texture descriptor bits changed a later material sampler operation");
    }
  }
}
}
int main() {
  try { TestSamplerProgram(); TestInheritedSamplerProjection(); std::cout<<"Native material sampler program checks passed\n"; }
  catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
