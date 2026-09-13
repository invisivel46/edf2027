#include "d3d11_sampler.h"
#include <stdexcept>

namespace edf::native {
// The guest words are decoded once, in native_sampler_decode.cpp, and shared
// with every other backend. This converts that answer into D3D11's encoding;
// it does not decide anything itself, so the two cannot disagree about what a
// guest sampler means.
D3D11_SAMPLER_DESC DecodeNativeSampler(const SamplerStateWords& input) {
  const auto decoded=DecodeNativeGuestSampler(input);
  const auto address=[](NativeBackendAddress mode) {
    switch(mode) {
      case NativeBackendAddress::Wrap: return D3D11_TEXTURE_ADDRESS_WRAP;
      case NativeBackendAddress::Mirror: return D3D11_TEXTURE_ADDRESS_MIRROR;
      case NativeBackendAddress::Clamp: return D3D11_TEXTURE_ADDRESS_CLAMP;
      case NativeBackendAddress::MirrorOnce: return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
      case NativeBackendAddress::Border: return D3D11_TEXTURE_ADDRESS_BORDER;
    }
    throw std::runtime_error("unsupported native sampler address/border mode");
  };
  const auto filter=[](NativeBackendFilter mode) {
    return mode==NativeBackendFilter::Point?D3D11_FILTER_TYPE_POINT:D3D11_FILTER_TYPE_LINEAR;
  };
  D3D11_SAMPLER_DESC desc{};
  desc.AddressU=address(decoded.u);
  desc.AddressV=address(decoded.v);
  desc.AddressW=address(decoded.w);
  const bool anisotropic=decoded.min==NativeBackendFilter::Anisotropic;
  desc.Filter=anisotropic?D3D11_FILTER_ANISOTROPIC
                         :D3D11_ENCODE_BASIC_FILTER(filter(decoded.min),filter(decoded.mag),
                                                    filter(decoded.mip),
                                                    D3D11_FILTER_REDUCTION_TYPE_STANDARD);
  desc.MaxAnisotropy=decoded.max_anisotropy;
  desc.ComparisonFunc=D3D11_COMPARISON_ALWAYS;
  desc.MipLODBias=decoded.mip_lod_bias;
  desc.MinLOD=decoded.min_lod;
  desc.MaxLOD=decoded.max_lod;
  for(size_t index=0;index<4;++index) desc.BorderColor[index]=decoded.border[index];
  return desc;
}
}
