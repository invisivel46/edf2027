#pragma once
#include "effect.h"
namespace edf::native {
// Recovered fixed XUI movie programs: 82060B70 VS, 82064428 HD PS and
// 820641F0 SD PS. Entries are VS_Movie, PS_Movie (HD), PS_MovieSD.
// Ordinary native HLSL, not a guest shader translator. Live plane upload and
// ownership/binding recovery are separate from these verified shader equations.
Effect MakeNativeMovieEffect();
}
