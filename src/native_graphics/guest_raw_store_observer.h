#pragma once
#include "guest_scalar_store_observer.h"
#include <cstring>

namespace edf::native {
inline void ObservedVectorStore(uint8_t* base,simde__m128i* destination,simde__m128i value,
    std::source_location site=std::source_location::current()) {
  simde_mm_store_si128(destination,value);
  ObserveRawPhysicalStore(base,destination,16,site);
}
inline void* ObservedInlineFill(uint8_t* base,void* destination,int value,size_t bytes,
    std::source_location site=std::source_location::current()) {
  auto* result=std::memset(destination,value,bytes);
  // Generated dcbz/dcbzl extents are 32/128. Keep the host memset contract
  // for any other call, conservatively invalidating on an unrepresentable size.
  ObserveRawPhysicalStore(base,destination,bytes>UINT32_MAX?UINT32_MAX:static_cast<uint32_t>(bytes),site);
  return result;
}
}
// Only append this header to generated translation units. Definitions above
// retain the original intrinsics; register-only destinations are range-filtered.
#define simde_mm_store_si128(p,v) edf::native::ObservedVectorStore(base,(p),(v))
#define memset(p,v,n) edf::native::ObservedInlineFill(base,(p),(v),(n))
