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
SamplerStateWords NativeFilteringKey(SamplerStateWords words, int override_mode);
}  // namespace edf::native
