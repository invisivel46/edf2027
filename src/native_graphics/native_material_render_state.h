#pragma once
#include "guest_parameter_records.h"
#include "native_render_state_decode.h"
#include <array>
#include <bit>
#include <stdexcept>
#include <optional>
#include <utility>
#include <vector>

namespace edf::native {
// Explicit CPU pass state needed to resolve material overrides. Requested
// depth/masks and disabled blend factors remain owned even when not effective.
// Targets and viewport/scissor geometry belong to the enclosing native pass.
struct NativeMaterialRenderPass {
  RenderStateWords words{};
  uint32_t blend_parameters=0,blend_control=0;
  std::array<uint32_t,4> blend_factor{},color_requested{},color_targets{};
  uint32_t color_mask=0,depth_requested=0,stencil_requested=0,depth_target=0;
  uint32_t alpha_reference=0,scissor_enabled=0;
  bool operator==(const NativeMaterialRenderPass&) const=default;
};
inline uint32_t NativeMaterialStateSetter(uint32_t offset) {
  switch(offset) {
    case 0x28:return 0x82135530; case 0x2c:return 0x821355a8; case 0x30:return 0x82135578;
    case 0x34:return 0x82134ee8; case 0x38:return 0x82134eb8;
    case 0x3c:return 0x82134f58; case 0x40:return 0x821352e8; case 0x44:return 0x82135418;
    case 0x48:return 0x82135078; case 0x4c:return 0x82135108; case 0x50:return 0x82134fe8;
    case 0x54:return 0x82135208; case 0x58:return 0x82135278; case 0x5c:return 0x82135198;
    case 0x60:return 0x82134f18; case 0x64:return 0x82135380; case 0x68:return 0x821353e8;
    case 0x6c:return 0x821355e8; case 0x70:return 0x82135630; case 0x74:return 0x821356a0;
    case 0x78:return 0x821356e0; case 0x7c:return 0x82135720; case 0x80:return 0x82135670;
    case 0x90:return 0x82135780; case 0x94:return 0x821357c0; case 0x98:return 0x82135800;
    case 0x9c:return 0x82135750; case 0xc0:return 0x82135ab8; case 0xc8:return 0x82137978;
    case 0xd4:return 0x82135b08; case 0xd8:return 0x82135b40; case 0xdc:return 0x82135b78;
    case 0xe0:return 0x82135bb0; case 0x148:return 0x82136478;
    case 0x150:return 0x821364c8; case 0x154:return 0x821364f8;
    default: throw std::runtime_error("native material state override is not decoded: "+std::to_string(offset));
  }
}
inline uint32_t NativeMaterialUnifiedBlend(uint32_t parameters) {
  const uint32_t color=parameters&0xffffu;
  // Replicate color factors to alpha with the legacy color->alpha factor
  // conversion, exactly as the shared tail in 82134F58/82134FE8/... .
  uint32_t alpha=(((color<<4)|(color&0x1010u))<<12)&0xffff0000u;
  alpha&=0xffefffffu; alpha&=0xefffffffu;
  return color|alpha;
}
inline void ApplyNativeMaterialState(NativeMaterialRenderPass& pass,uint32_t offset,uint32_t value) {
  (void)NativeMaterialStateSetter(offset); // Unsupported operations never silently disappear.
  const auto insert=[&](uint32_t& word,unsigned shift,uint32_t mask) {
    word=(word&~mask)|(std::rotl(value,int(shift))&mask);
  };
  auto& depth=pass.words[1]; auto& raster=pass.words[2]; auto& alpha=pass.words[3];
  bool blend_update=false,alpha_only=false;
  switch(offset) {
    case 0x28: pass.depth_requested=value; depth=(depth&~2u)|((pass.depth_target?value:0u)<<1&2u); break;
    case 0x2c: insert(depth,4,0x70); break;
    case 0x30: insert(depth,2,4); break;
    case 0x34: insert(raster,3,0x7f8); break;
    case 0x38: insert(raster,0,7); break;
    case 0x3c: insert(pass.blend_control,31,0x80000000); blend_update=true; break;
    case 0x40: insert(pass.blend_control,30,0x40000000); blend_update=true; break;
    case 0x44:
      for(size_t i=0;i<4;++i) {
        const auto shift=std::array<unsigned,4>{16,8,0,24}[i];
        pass.blend_factor[i]=std::bit_cast<uint32_t>(float((value>>shift)&255)*std::bit_cast<float>(0x3b808081u));
      }
      break;
    case 0x48: insert(pass.blend_parameters,0,0x1f); blend_update=true; break;
    case 0x4c: insert(pass.blend_parameters,8,0x1f00); blend_update=true; break;
    case 0x50: insert(pass.blend_parameters,5,0xe0); blend_update=true; break;
    case 0x54: insert(pass.blend_parameters,16,0x1f0000); blend_update=alpha_only=true; break;
    case 0x58: insert(pass.blend_parameters,24,0x1f000000); blend_update=alpha_only=true; break;
    case 0x5c: insert(pass.blend_parameters,21,0xe00000); blend_update=alpha_only=true; break;
    case 0x60: insert(alpha,3,8); break;
    case 0x64: pass.alpha_reference=std::bit_cast<uint32_t>(float(value)*std::bit_cast<float>(0x3b808081u)); break;
    case 0x68: insert(alpha,0,7); break;
    case 0x6c: pass.stencil_requested=value; depth=(depth&~1u)|((pass.depth_target?value:0u)&1u); break;
    case 0x70: insert(depth,7,0x80); break;
    case 0x74: insert(depth,11,0x3800); break;
    case 0x78: insert(depth,17,0xe0000); break;
    case 0x7c: insert(depth,14,0x1c000); break;
    case 0x80: insert(depth,8,0x700); break;
    case 0x90: insert(depth,23,0x3800000); break;
    case 0x94: insert(depth,29,0xe0000000); break;
    case 0x98: insert(depth,26,0x1c000000); break;
    case 0x9c: insert(depth,20,0x700000); break;
    case 0xc0: insert(raster,15,0x8000); break;
    case 0xc8: pass.scissor_enabled=value; pass.words[5]=uint32_t(value!=0); break;
    case 0xd4: case 0xd8: case 0xdc: case 0xe0: {
      const auto index=(offset-0xd4)/4;
      pass.color_requested[index]=value;
      const auto mask=15u<<(index*4);
      pass.color_mask=(pass.color_mask&~mask)|(((pass.color_targets[index]?value:0u)<<(index*4))&mask);
      pass.words[4]=pass.color_mask&15;
      break;
    }
    case 0x148: insert(raster,21,0x200000); break;
    case 0x150: insert(alpha,4,16); break;
    case 0x154: insert(alpha,24,0xff000000); break;
  }
  if(blend_update) {
    const bool enabled=offset==0x3c?value!=0:(pass.blend_control&0x80000000u)!=0;
    const bool separate=offset==0x40?value!=0:(pass.blend_control&0x40000000u)!=0;
    if(offset==0x3c || offset==0x40 || (enabled && (!alpha_only || separate)))
      pass.words[0]=enabled?(separate?pass.blend_parameters:NativeMaterialUnifiedBlend(pass.blend_parameters)):0x10001;
  }
}
// One override's (device offset, word) CPU writes, in order. Fixed capacity:
// the most any override writes is seven (a blend update's parameter or control
// word, the four replicated blend words and the two dirty halves), and the
// material activation computes one list per state override per activation -
// hundreds a frame for the HUD alone - so it lives on the stack, not the heap.
// Iterates, indexes and sizes as the vector it replaces.
class NativeMaterialStateCpuWriteList {
 public:
  using value_type=std::pair<uint32_t,uint32_t>;
  void emplace_back(uint32_t offset,uint32_t value) {
    if(count_==items_.size()) throw std::runtime_error("native material state CPU write list overflow");
    items_[count_++]={offset,value};
  }
  const value_type* begin() const { return items_.data(); }
  const value_type* end() const { return items_.data()+count_; }
  size_t size() const { return count_; }
  bool empty() const { return !count_; }
  const value_type& operator[](size_t index) const { return items_[index]; }
 private:
  std::array<value_type,8> items_{};
  size_t count_=0;
};
// CPU mirrors for the remaining mixed renderer. Offsets are relative to the
// device. Scissor enable also recomputes a rectangle and stays with that owner.
inline std::optional<NativeMaterialStateCpuWriteList> NativeMaterialStateCpuWrites(
    NativeMaterialRenderPass pass,uint32_t offset,uint32_t value,uint64_t dirty16,uint64_t dirty24) {
  if(offset==0xc8) return {};
  ApplyNativeMaterialState(pass,offset,value);
  NativeMaterialStateCpuWriteList writes;
  const auto write=[&](uint32_t address,uint32_t data) { writes.emplace_back(address,data); };
  uint64_t flags16=0,flags24=0;
  switch(offset) {
    case 0x28: write(11604,pass.depth_requested); [[fallthrough]];
    case 0x2c: case 0x6c: case 0x70: case 0x74: case 0x78: case 0x90: case 0x94:
      if(offset==0x6c) write(11608,pass.stencil_requested);
      write(10420,pass.words[1]); flags16=(uint64_t(1)<<49)|2048; break;
    case 0x30: case 0x7c: case 0x80: case 0x98: case 0x9c:
      write(10420,pass.words[1]); flags16=2048; break;
    case 0x34: case 0x38: case 0xc0: case 0x148:
      write(10440,pass.words[2]); flags16=64; break;
    case 0x3c: case 0x40: case 0x48: case 0x4c: case 0x50: case 0x54: case 0x58: case 0x5c: {
      if(offset==0x3c || offset==0x40) write(11580,pass.blend_control);
      else write(11576,pass.blend_parameters);
      const bool alpha_only=offset==0x54 || offset==0x58 || offset==0x5c;
      const bool enabled=(pass.blend_control&0x80000000u)!=0;
      const bool separate=(pass.blend_control&0x40000000u)!=0;
      if(offset==0x3c || offset==0x40 || (enabled && (!alpha_only || separate))) {
        for(const auto address:{10424u,10456u,10460u,10464u}) write(address,pass.words[0]);
        flags16=1024|4|2|1;
      }
      break;
    }
    case 0x44:
      for(uint32_t i=0;i<4;++i) write(10336+i*4,pass.blend_factor[i]);
      flags24=0x3c000; break;
    case 0x60: write(10428,pass.words[3]); flags16=(uint64_t(1)<<50)|512; break;
    case 0x64: write(10372,pass.alpha_reference); flags24=256; break;
    case 0x68: case 0x150: case 0x154: write(10428,pass.words[3]); flags16=512; break;
    case 0xd4: case 0xd8: case 0xdc: case 0xe0: {
      const auto index=(offset-0xd4)/4;
      write(11588+index*4,pass.color_requested[index]); write(10332,pass.color_mask);
      flags24=0x40000; break;
    }
  }
  if(flags16) { dirty16|=flags16; write(16,uint32_t(dirty16>>32)); write(20,uint32_t(dirty16)); }
  if(flags24) { dirty24|=flags24; write(24,uint32_t(dirty24>>32)); write(28,uint32_t(dirty24)); }
  return writes;
}
// Device mirrors only. Fixed image constants are verified by the live reader.
template<class Reader>
NativeMaterialRenderPass ReadNativeMaterialRenderPassMirrors(const Reader& reader,uint32_t device) {
  NativeMaterialRenderPass pass;
  const auto word=[&](uint32_t offset){return reader.Word(reader.Add(device,offset));};
  pass.color_mask=word(10332); pass.scissor_enabled=word(11584);
  pass.words={word(10424),word(10420),word(10440),word(10428),pass.color_mask&15,uint32_t(pass.scissor_enabled!=0)};
  pass.blend_parameters=word(11576); pass.blend_control=word(11580);
  pass.depth_requested=word(11604); pass.stencil_requested=word(11608); pass.depth_target=word(12184);
  pass.alpha_reference=word(10372);
  pass.blend_factor=ReadGuestWords<4>(reader,reader.Add(device,10336));
  pass.color_requested=ReadGuestWords<4>(reader,reader.Add(device,11588));
  pass.color_targets=ReadGuestWords<4>(reader,reader.Add(device,12168));
  return pass;
}
template<class Reader>
NativeMaterialRenderPass ReadNativeMaterialRenderPass(const Reader& reader,uint32_t device) {
  if(reader.Word(0x8200964c)!=0x3b808081u) throw std::runtime_error("native material color scale changed");
  return ReadNativeMaterialRenderPassMirrors(reader,device);
}
}
