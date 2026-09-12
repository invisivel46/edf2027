#pragma once
#include <array>
#include <cstdint>

namespace edf::native {
struct NativeMeshKeyLess {
  constexpr bool operator()(const std::array<uint32_t,5>& a,
                            const std::array<uint32_t,5>& b) const noexcept {
    // Numeric lexicographic order, identical to std::array's ordering. Avoid
    // dispatching a generic vectorized mismatch routine for just five words.
    for(unsigned i=0;i<5;++i) {
      if(a[i]<b[i]) return true;
      if(a[i]>b[i]) return false;
    }
    return false;
  }
};
}
