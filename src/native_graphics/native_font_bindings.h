#pragma once
#include "d3d11_bindings.h"
#include "native_canvas_constants.h"
#include <stdexcept>

namespace edf::native {
// Immutable binding metadata for one native font shader generation. Glyph
// constants and atlas/sampler objects remain live inputs on every draw.
class NativeFontBindings {
 public:
  NativeFontBindings(const ShaderBindings& vertex,const ShaderBindings& pixel)
      : vertex_{vertex.ResolveFloatRegisters("VertexColor"),vertex.ResolveFloatRegisters("TexScale"),
                vertex.ResolveFloatRegisters("Offset"),vertex.ResolveFloatRegisters("Scale")},
        pixel_{pixel.ResolveFloatRegisters("ChannelSelector"),pixel.ResolveFloatRegisters("Mask")},
        texture_(pixel.ResolveResource("FontTexture")),sampler_(pixel.ResolveResource("FontSampler")) {
    for(const auto& binding:vertex_) if(binding.bytes()!=16) throw std::runtime_error("invalid native font vertex layout");
    for(const auto& binding:pixel_) if(binding.bytes()!=16) throw std::runtime_error("invalid native font pixel layout");
  }
  void SetConstants(ShaderBindings& vertex,ShaderBindings& pixel,
                    std::span<const uint8_t> vs,std::span<const uint8_t> ps,float canvas_x=1,float canvas_y=1) const {
    if(vs.size()!=64 || ps.size()!=32) throw std::runtime_error("invalid native font register block");
    // Reject either stale generation before modifying either destination.
    for(const auto& binding:vertex_) if(!vertex.Owns(binding)) throw std::runtime_error("stale native font vertex bindings");
    for(const auto& binding:pixel_) if(!pixel.Owns(binding)) throw std::runtime_error("stale native font pixel bindings");
    const auto offset=ScaleNativeCanvasXY(vs.subspan(32,16),canvas_x,canvas_y);
    const auto scale=ScaleNativeCanvasXY(vs.subspan(48,16),canvas_x,canvas_y);
    for(size_t i=0;i<vertex_.size();++i)
      vertex.SetGuestFloatRegisters(vertex_[i],i==2?std::span<const uint8_t>(offset):
        i==3?std::span<const uint8_t>(scale):vs.subspan(i*16,16));
    for(size_t i=0;i<pixel_.size();++i) pixel.SetGuestFloatRegisters(pixel_[i],ps.subspan(i*16,16));
  }
  void SetTexture(ShaderBindings& pixel,std::shared_ptr<NativeBackendTexture> texture) const {
    if(!pixel.TrySetTexture(texture_,std::move(texture))) throw std::runtime_error("missing native font texture binding");
  }
  void SetSampler(ShaderBindings& pixel,NativeBackendSampler* sampler) const {
    if(!pixel.TrySetSampler(sampler_,sampler)) throw std::runtime_error("missing native font sampler binding");
  }
 private:
  std::array<ShaderBindings::FloatRegisterBinding,4> vertex_;
  std::array<ShaderBindings::FloatRegisterBinding,2> pixel_;
  ShaderBindings::ResourceBinding texture_,sampler_;
};
}
