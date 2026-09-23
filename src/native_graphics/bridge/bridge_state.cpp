#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "bridge_state.h"

namespace edf::native {
thread_local constinit MovieDecodeLocks* active_movie_decode=nullptr;
}  // namespace edf::native
