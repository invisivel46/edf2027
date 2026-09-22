#pragma once
#include "native_material_parameters.h"
#include "native_sampler_decode.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>

namespace edf::native {
// Material activation is an ordered program over explicit pass inputs. Keep
// alias/filter controls: canonical sampler keys alone discard CPU state needed
// by a later min/mag operation. No guest pointers are retained here.
struct NativeMaterialSamplerPass {
  SamplerStateWords words{};
  uint32_t anisotropy=0;
  uint8_t min_lod=0,max_lod=15,mip_override=0;
  bool operator==(const NativeMaterialSamplerPass&) const=default;
};
// The device interleaves sampler controls with texture format/address words.
// Those descriptors belong to retained textures, not inherited sampler state.
// Unlike the backend sampler key, retain the two mip-alias bookkeeping bits:
// a later filter operation consumes them even though sampling does not.
inline NativeMaterialSamplerPass NativeMaterialSamplerInputs(NativeMaterialSamplerPass pass) {
  const auto aliases=pass.words[2]&3u;
  pass.words=SamplerStateKey(pass.words);
  pass.words[2]|=aliases;
  return pass;
}
struct NativeMaterialSamplerOperation {
  uint32_t slot=0;
  std::optional<uint32_t> texture_lod;
  uint32_t texture_filter_high=0;
  // Bias bits, mip, min, mag; applied in guest order: bind, mag, min, mip, bias.
  std::array<uint32_t,4> settings{};
  bool operator==(const NativeMaterialSamplerOperation&) const=default;
};

inline void ApplyNativeMaterialFilter(NativeMaterialSamplerPass& pass,uint32_t value,bool mag) {
  auto& filter=pass.words[1];
  auto& lod=pass.words[2];
  const auto alias=value>>2;
  const auto other=(lod>>(mag?11:10))&1u;
  const auto aniso=pass.anisotropy & ~((other|alias)-1u);
  const auto combined=(aniso<<(mag?6:4))|alias|value;
  const auto alias_bit=mag?10:11;
  lod=(lod&~(1u<<alias_bit))|((alias&1u)<<alias_bit);
  const uint32_t mask=mag?0x0e180000u:0x0e600000u;
  filter=(filter&~mask)|(std::rotl(combined,mag?19:21)&mask);
  // CPU mip alias bookkeeping from 82136700 / 82136888. These low two bits
  // are not part of the native sampler key, but belong to the pass state.
  constexpr uint32_t rotate_mask=0x7ff7ffff;
  const auto packed=(filter&~rotate_mask)|(std::rotr(filter,1)&rotate_mask);
  const auto mip=std::rotl(packed,13)&0xfffu;
  const uint32_t override_mask=(uint32_t(pass.mip_override)>>2)-1u;
  const auto effective=(mip&override_mask)+(uint32_t(pass.mip_override)&~override_mask);
  lod=(lod&~3u)|(effective&3u);
}

inline NativeMaterialSamplerPass ApplyNativeMaterialSampler(
    NativeMaterialSamplerPass pass,const NativeMaterialSamplerOperation& operation) {
  if(operation.texture_lod) {
    const auto texture_min=(*operation.texture_lod>>2)&15u;
    const auto texture_max=(*operation.texture_lod>>6)&15u;
    pass.words[1]=(pass.words[1]&0x7ff80000u)|(operation.texture_filter_high&0x80000000u);
    pass.words[2]=(pass.words[2]&~0x3fcu)|
      (((std::max)(texture_min,uint32_t(pass.min_lod))<<2)&0x3cu)|
      (((std::min)(texture_max,uint32_t(pass.max_lod))<<6)&0x3c0u);
  }
  ApplyNativeMaterialFilter(pass,operation.settings[3],true);
  ApplyNativeMaterialFilter(pass,operation.settings[2],false);
  pass.words[1]=(pass.words[1]&~0x01800000u)|((operation.settings[1]<<23)&0x01800000u);
  // 82136C20: fmuls by the retail 32.0 constant, fctidz, low ten bits.
  // Bound the conversion explicitly so NaN/infinity/overflow never cause C++ UB.
  const float scaled=std::bit_cast<float>(operation.settings[0])*32.0f;
  int64_t integral;
  if(std::isnan(scaled) || double(scaled)<=-0x1p63 || double(scaled)==0x1p63)
    integral=(std::numeric_limits<int64_t>::min)();
  else if(double(scaled)>0x1p63) integral=(std::numeric_limits<int64_t>::max)();
  else integral=static_cast<int64_t>(scaled);
  pass.words[2]=(pass.words[2]&~0x003ff000u)|((uint32_t(integral)&1023u)<<12);
  return pass;
}

template<class Reader>
NativeMaterialSamplerPass ReadNativeMaterialSamplerPass(const Reader& reader,uint32_t device,uint32_t slot) {
  if(slot>=16) throw std::runtime_error("native material sampler slot exceeds pixel-stage range");
  NativeMaterialSamplerPass pass;
  pass.words=ReadSamplerWords(reader,device,slot);
  const auto byte=[&](uint32_t offset) { return *reader.Bytes(reader.Add(device,offset+slot),1); };
  pass.anisotropy=reader.Word(reader.Add(0x82009608,uint32_t(byte(11652))*4));
  pass.min_lod=byte(11678); pass.max_lod=byte(11704); pass.mip_override=byte(11730);
  if(reader.Word(0x82003198)!=0x42000000u)
    throw std::runtime_error("native material sampler bias scale changed");
  return pass;
}

template<class Reader>
std::vector<NativeMaterialSamplerOperation> ReadNativeMaterialSamplerOperations(
    const Reader& reader,const NativeMaterialParameters::Groups& schema) {
  std::vector<NativeMaterialSamplerOperation> result;
  // Include optimized-out resources: they can still change a slot later read
  // by a used resource, or inherited by the next material.
  for(size_t global=0;global<2;++global) for(const auto& parameter:schema.textures[global]) {
    const auto value=parameter.ReadValue(reader,global!=0);
    if(value.slot>=16) throw std::runtime_error("native material sampler slot exceeds pixel-stage range");
    NativeMaterialSamplerOperation operation; operation.slot=value.slot;
    const auto settings=global?reader.Add(reader.Word(parameter.record),32):reader.Add(parameter.record,12);
    operation.settings=ReadGuestWords<4>(reader,settings);
    if(value.handle) {
      const auto header=ReadGuestWords<2>(reader,reader.Add(value.handle,40));
      operation.texture_filter_high=header[0]&0x80000000u;
      operation.texture_lod=header[1]&0x3fcu;
    }
    result.push_back(operation);
  }
  return result;
}
}
