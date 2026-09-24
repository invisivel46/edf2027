#pragma once
#include "native_render_backend.h"
#include <array>
#include <cstdint>

namespace edf::native {
// CPU-side D3D9 sampler-state snapshot: addressing, filtering, LOD, extras.
// Only state fields are consumed, never texture addresses or GPU commands.
using SamplerStateWords = std::array<uint32_t,4>;

// Decoding these words is guest logic, like the render state and the vertex
// declarations before it, and a second backend needs the identical answer. It
// lives here once so the two cannot drift - a sampler that filtered differently
// on one backend would show as one surface looking softer than the other, which
// is the sort of difference nobody attributes to a sampler.
//
// Throws std::runtime_error on state this renderer does not implement.
NativeBackendSamplerDesc DecodeNativeGuestSampler(const SamplerStateWords& words);

SamplerStateWords SamplerStateKey(SamplerStateWords words);
// -1 preserves the game; 0 disables AF; 1..5 select 1x..16x.
// Point and base-only samplers are deliberately preserved.
//
// The value may also carry a mip LOD bias (NativeFilteringWithMipBias: FSR
// upscaling's negative bias for the 3D scene's materials), added to the
// guest's own bias field (words[2] bits 12..21, 1/32 steps, clamped to its
// signed 10-bit range). The value is what the pass caches key on, so a bias
// reaches every sampler resolved for that pass and changes its keys; without
// one the value is the plain mode, bit for bit.
inline constexpr int kNativeFilteringMipBiasFlag = 1 << 30;
constexpr int NativeFilteringWithMipBias(int mode, int bias_steps) {
  if (!bias_steps) return mode;
  return kNativeFilteringMipBiasFlag | ((bias_steps & 1023) << 8) | ((mode + 1) & 255);
}
constexpr int NativeFilteringMode(int filtering) {
  return (filtering & kNativeFilteringMipBiasFlag) && filtering > 0 ? (filtering & 255) - 1 : filtering;
}
constexpr int NativeFilteringMipBias(int filtering) {
  if (!(filtering & kNativeFilteringMipBiasFlag) || filtering < 0) return 0;
  const int steps = (filtering >> 8) & 1023;
  return steps & 512 ? steps - 1024 : steps;
}
SamplerStateWords NativeFilteringKey(SamplerStateWords words, int override_mode);
}  // namespace edf::native
