#pragma once
#include <d3d11.h>
#include <array>
#include <cstdint>

namespace edf::native {
// CPU-side D3D9 sampler-state snapshot: addressing, filtering, LOD, extras.
// Only state fields are consumed, never texture addresses or GPU commands.
using SamplerStateWords = std::array<uint32_t,4>;
D3D11_SAMPLER_DESC DecodeNativeSampler(const SamplerStateWords& words);
SamplerStateWords SamplerStateKey(SamplerStateWords words);
// -1 preserves the game; 0 disables AF; 1..5 select 1x..16x.
// Point and base-only samplers are deliberately preserved.
SamplerStateWords NativeFilteringKey(SamplerStateWords words, int override_mode);
}
