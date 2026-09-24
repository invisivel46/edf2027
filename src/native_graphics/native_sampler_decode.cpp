#include "native_sampler_decode.h"
#include <stdexcept>

namespace edf::native {
SamplerStateWords SamplerStateKey(SamplerStateWords words) {
  words[0] &= 0x7fc00; // Address U/V/W only, no resource format/address fields.
  words[1] &= 0xfff80000;
  words[2] &= 0xfffffffc;
  words[3] &= 0x1ff;
  return words;
}

SamplerStateWords NativeFilteringKey(SamplerStateWords words, int filtering) {
  words = SamplerStateKey(words);
  if (const int steps = NativeFilteringMipBias(filtering)) {
    int32_t bias = (words[2] >> 12) & 1023;
    if (bias & 512) bias -= 1024;
    bias = bias + steps < -512 ? -512 : bias + steps > 511 ? 511 : bias + steps;
    words[2] = (words[2] & ~0x003ff000u) | ((uint32_t(bias) & 1023u) << 12);
  }
  const int override_mode = NativeFilteringMode(filtering);
  if (override_mode < 0 || override_mode > 5) return words;
  const auto min = (words[1] >> 21) & 3, mag = (words[1] >> 19) & 3;
  const auto mip = (words[1] >> 23) & 3;
  if (min != 1 || mag != 1 || mip != 1) return words;
  words[1] = (words[1] & ~(7u << 25)) | (uint32_t(override_mode) << 25);
  return words;
}

NativeBackendSamplerDesc DecodeNativeGuestSampler(const SamplerStateWords& input) {
  const auto words = SamplerStateKey(input);
  const auto address = [](uint32_t mode) {
    switch (mode) {
      case 0: return NativeBackendAddress::Wrap;
      case 1: return NativeBackendAddress::Mirror;
      case 2: return NativeBackendAddress::Clamp;
      case 3: return NativeBackendAddress::MirrorOnce;
      default: throw std::runtime_error("unsupported native sampler address/border mode");
    }
  };
  NativeBackendSamplerDesc desc{};
  desc.u = address((words[0] >> 10) & 7);
  desc.v = address((words[0] >> 13) & 7);
  desc.w = address((words[0] >> 16) & 7);

  const auto mag = (words[1] >> 19) & 3, min = (words[1] >> 21) & 3;
  const auto mip = (words[1] >> 23) & 3, aniso = (words[1] >> 25) & 7;
  if (mag > 1 || min > 1 || mip > 2 || aniso > 5 || (words[1] >> 28) ||
      (words[2] >> 22) || (words[3] & 0x1fc))
    throw std::runtime_error("unsupported native sampler filtering mode");
  const auto filter = [](uint32_t mode) {
    return mode ? NativeBackendFilter::Linear : NativeBackendFilter::Point;
  };
  desc.min = filter(min);
  desc.mag = filter(mag);
  // mip 2 is base-only, which is expressed as a zero LOD range below rather
  // than as a filter; only mip 1 is a linear mip filter.
  desc.mip = filter(mip == 1 ? 1 : 0);
  desc.max_anisotropy = aniso ? 1u << (aniso - 1) : 1;
  if (aniso) desc.min = desc.mag = desc.mip = NativeBackendFilter::Anisotropic;

  int32_t bias = (words[2] >> 12) & 1023;
  if (bias & 512) bias -= 1024;
  desc.mip_lod_bias = float(bias) / 32;
  desc.min_lod = float((words[2] >> 2) & 15);
  desc.max_lod = float((words[2] >> 6) & 15);
  if (desc.min_lod > desc.max_lod) throw std::runtime_error("inverted native sampler LOD range");
  if (mip == 2) {
    if (desc.min_lod != 0) throw std::runtime_error("base-only sampler with nonzero minimum LOD");
    desc.max_lod = 0;
  }
  return desc;
}
}  // namespace edf::native
