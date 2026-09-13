#pragma once
#include "d3d11_bindings.h"
#include <stdexcept>

namespace edf::native {
class NativeMovieBindings {
 public:
  NativeMovieBindings(const ShaderBindings& vertex,const ShaderBindings& pixel)
      : vertex_{vertex.ResolveFloatRegisters("TransformRows"),vertex.ResolveFloatRegisters("ProjectionRows"),
          vertex.ResolveFloatRegisters("Params"),vertex.ResolveFloatRegisters("ShadowOffset")},
        factor_(pixel.ResolveFloatRegisters("ColorFactor")),
        planes_{pixel.ResolveResource("YPlane"),pixel.ResolveResource("UPlane"),pixel.ResolveResource("VPlane")},
        samplers_{pixel.ResolveResource("YSampler"),pixel.ResolveResource("USampler"),pixel.ResolveResource("VSampler")} {
    constexpr size_t sizes[]{64,64,16,16};
    for(size_t i=0;i<4;++i) if(vertex_[i].bytes()!=sizes[i]) throw std::runtime_error("invalid native movie vertex layout");
    if(factor_.bytes()!=16) throw std::runtime_error("invalid native movie pixel layout");
  }
  void SetConstants(ShaderBindings& vertex,ShaderBindings& pixel,
                    std::span<const uint8_t> registers,std::span<const uint8_t> factor) const {
    if(registers.size()!=160 || factor.size()!=16) throw std::runtime_error("invalid native movie register blocks");
    for(const auto& binding:vertex_) if(!vertex.Owns(binding)) throw std::runtime_error("stale native movie vertex bindings");
    if(!pixel.Owns(factor_)) throw std::runtime_error("stale native movie pixel bindings");
    constexpr size_t offsets[]{0,64,128,144},sizes[]{64,64,16,16};
    for(size_t i=0;i<4;++i) vertex.SetGuestFloatRegisters(vertex_[i],registers.subspan(offsets[i],sizes[i]));
    pixel.SetGuestFloatRegisters(factor_,factor);
  }
  void SetTexture(ShaderBindings& pixel,size_t plane,std::shared_ptr<NativeBackendTexture> texture) const {
    if(plane>=3 || !pixel.TrySetTexture(planes_[plane],std::move(texture))) throw std::runtime_error("invalid native movie plane binding");
  }
  void SetSampler(ShaderBindings& pixel,size_t plane,NativeBackendSampler* sampler) const {
    if(plane>=3 || !pixel.TrySetSampler(samplers_[plane],sampler)) throw std::runtime_error("invalid native movie sampler binding");
  }
 private:
  std::array<ShaderBindings::FloatRegisterBinding,4> vertex_;
  ShaderBindings::FloatRegisterBinding factor_;
  std::array<ShaderBindings::ResourceBinding,3> planes_,samplers_;
};
}
