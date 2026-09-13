#pragma once
#include "d3d11_bindings.h"
#include <stdexcept>
#include <bit>
#include <cmath>

namespace edf::native {
// Map the authored canvas to the full output while retaining homogeneous
// translation/depth. X grows from the left edge and Y from the top edge.
inline std::array<float,16> NativeXuiCanvasProjection(std::span<const uint8_t> bytes,
                                                     float scale_x,float scale_y) {
  if(bytes.size()!=64 || !std::isfinite(scale_x) || !std::isfinite(scale_y) ||
     scale_x<=0 || scale_y<=0) throw std::runtime_error("invalid XUI canvas projection");
  std::array<float,16> matrix{};
  for(size_t i=0;i<16;++i) {
    uint32_t word=0;
    for(size_t b=0;b<4;++b) word=(word<<8)|bytes[i*4+b];
    matrix[i]=std::bit_cast<float>(word);
  }
  for(size_t i=0;i<4;++i) {
    matrix[i]=scale_x*matrix[i]+(scale_x-1)*matrix[12+i];
    matrix[4+i]=scale_y*matrix[4+i]+(1-scale_y)*matrix[12+i];
  }
  return matrix;
}
class NativeXuiPixelBindings {
 public:
  NativeXuiPixelBindings(const ShaderBindings& shader,bool solid)
      : solid_(solid),factor_(shader.ResolveFloatRegisters("ColorFactor")),
        brush_(shader.ResolveFloatRegisters("BrushColor")),
        texture_(shader.ResolveResource("BrushTexture")),sampler_(shader.ResolveResource("BrushSampler")) {
    if(factor_.bytes()!=16 || (solid_ && brush_.bytes()!=16))
      throw std::runtime_error("invalid native XUI pixel layout");
  }
  void SetConstants(ShaderBindings& shader,std::span<const uint8_t> factor,
                    std::span<const uint8_t> brush={}) const {
    if(factor.size()!=16 || brush.size()!=(solid_?16u:0u))
      throw std::runtime_error("invalid native XUI pixel registers");
    if(!shader.Owns(factor_) || !shader.Owns(brush_))
      throw std::runtime_error("stale native XUI pixel bindings");
    shader.SetGuestFloatRegisters(factor_,factor);
    if(solid_) shader.SetGuestFloatRegisters(brush_,brush);
  }
  void SetTexture(ShaderBindings& shader,std::shared_ptr<NativeBackendTexture> texture) const {
    if(solid_ || !shader.TrySetTexture(texture_,std::move(texture))) throw std::runtime_error("missing native XUI texture binding");
  }
  void SetSampler(ShaderBindings& shader,NativeBackendSampler* sampler) const {
    if(solid_ || !shader.TrySetSampler(sampler_,sampler)) throw std::runtime_error("missing native XUI sampler binding");
  }
 private:
  bool solid_;
  ShaderBindings::FloatRegisterBinding factor_,brush_;
  ShaderBindings::ResourceBinding texture_,sampler_;
};
class NativeXuiVertexBindings {
 public:
  explicit NativeXuiVertexBindings(const ShaderBindings& shader)
      : constants_{shader.ResolveFloatRegisters("TransformRows"),shader.ResolveFloatRegisters("ProjectionRows"),
        shader.ResolveFloatRegisters("Params"),shader.ResolveFloatRegisters("TextureOffset"),
        shader.ResolveFloatRegisters("TextureRows"),shader.ResolveFloatRegisters("ArithmeticBias")} {
    constexpr size_t sizes[]{64,64,16,16,32,16};
    for(size_t i=0;i<constants_.size();++i)
      if(constants_[i].bytes()!=sizes[i]) throw std::runtime_error("invalid native XUI vertex layout");
  }
  void SetConstants(ShaderBindings& shader,std::span<const uint8_t> registers,
                    std::span<const uint8_t> bias,float scale_x=1,float scale_y=1) const {
    if(registers.size()!=192 || bias.size()!=16) throw std::runtime_error("invalid native XUI register blocks");
    for(const auto& binding:constants_)
      if(!shader.Owns(binding)) throw std::runtime_error("stale native XUI vertex bindings");
    constexpr size_t offsets[]{0,64,128,144,160};
    constexpr size_t sizes[]{64,64,16,16,32};
    // Validate before updating any binding; default path keeps exact bytes.
    std::array<uint8_t,64> scaled_projection{};
    if(scale_x!=1 || scale_y!=1) {
      const auto projection=NativeXuiCanvasProjection(registers.subspan(64,64),scale_x,scale_y);
      for(size_t i=0;i<16;++i) {
        const auto word=std::bit_cast<uint32_t>(projection[i]);
        for(size_t b=0;b<4;++b) scaled_projection[i*4+b]=uint8_t(word>>(24-b*8));
      }
    }
    for(size_t i=0;i<5;++i) {
      if(i==1 && (scale_x!=1 || scale_y!=1))
        shader.SetGuestFloatRegisters(constants_[i],scaled_projection);
      else shader.SetGuestFloatRegisters(constants_[i],registers.subspan(offsets[i],sizes[i]));
    }
    shader.SetGuestFloatRegisters(constants_[5],bias);
  }
 private:
  std::array<ShaderBindings::FloatRegisterBinding,6> constants_;
};
}
