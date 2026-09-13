#pragma once
#include "native_sampler_decode.h"
#include <d3d11.h>

namespace edf::native {
// D3D11's encoding of the shared guest sampler decode; the decode itself and
// the key/filtering helpers live in native_sampler_decode.h.
D3D11_SAMPLER_DESC DecodeNativeSampler(const SamplerStateWords& words);
}
