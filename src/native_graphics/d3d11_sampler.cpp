#include "d3d11_sampler.h"
#include <stdexcept>

namespace edf::native {
SamplerStateWords NativeFilteringKey(SamplerStateWords words, int override_mode) {
  words = SamplerStateKey(words);
  if (override_mode < 0 || override_mode > 5) return words;
  const auto min = (words[1] >> 21) & 3, mag = (words[1] >> 19) & 3;
  const auto mip = (words[1] >> 23) & 3;
  if (min != 1 || mag != 1 || mip != 1) return words;
  words[1] = (words[1] & ~(7u << 25)) | (uint32_t(override_mode) << 25);
  return words;
}
SamplerStateWords SamplerStateKey(SamplerStateWords words) {
  words[0] &= 0x7fc00; // Address U/V/W only, no resource format/address fields.
  words[1] &= 0xfff80000;
  words[2] &= 0xfffffffc;
  words[3] &= 0x1ff;
  return words;
}
D3D11_SAMPLER_DESC DecodeNativeSampler(const SamplerStateWords& input) {
  const auto words = SamplerStateKey(input);
  auto address = [](uint32_t mode) {
    switch (mode) {
      case 0: return D3D11_TEXTURE_ADDRESS_WRAP;
      case 1: return D3D11_TEXTURE_ADDRESS_MIRROR;
      case 2: return D3D11_TEXTURE_ADDRESS_CLAMP;
      case 3: return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
      default: throw std::runtime_error("unsupported native sampler address/border mode");
    }
  };
  D3D11_SAMPLER_DESC desc{};
  desc.AddressU = address((words[0] >> 10) & 7);
  desc.AddressV = address((words[0] >> 13) & 7);
  desc.AddressW = address((words[0] >> 16) & 7);
  const auto mag = (words[1] >> 19) & 3, min = (words[1] >> 21) & 3;
  const auto mip = (words[1] >> 23) & 3, aniso = (words[1] >> 25) & 7;
  if (mag > 1 || min > 1 || mip > 2 || aniso > 5 || (words[1] >> 28) ||
      (words[2] >> 22) || (words[3] & 0x1fc))
    throw std::runtime_error("unsupported native sampler filtering mode");
  desc.Filter = D3D11_ENCODE_BASIC_FILTER(min,mag,mip == 1 ? 1 : 0,D3D11_FILTER_REDUCTION_TYPE_STANDARD);
  desc.MaxAnisotropy = aniso ? 1u << (aniso-1) : 1;
  if (aniso) desc.Filter = D3D11_FILTER_ANISOTROPIC;
  desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
  int32_t bias = (words[2] >> 12) & 1023;
  if (bias & 512) bias -= 1024;
  desc.MipLODBias = float(bias)/32;
  desc.MinLOD = float((words[2] >> 2) & 15);
  desc.MaxLOD = float((words[2] >> 6) & 15);
  if (desc.MinLOD > desc.MaxLOD) throw std::runtime_error("inverted native sampler LOD range");
  if (mip == 2) {
    if (desc.MinLOD != 0) throw std::runtime_error("base-only sampler with nonzero minimum LOD");
    desc.MaxLOD = 0;
  }
  return desc;
}
}
