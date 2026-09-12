#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

namespace edf::native {
// Compatibility fields still read by recompiled engine resource consumers.
// This is only the model constructors' usage=0, VB / uint16-IB contract, not
// a general replacement for Xbox resource creation or texture relocation.
inline std::array<uint32_t,8> NativeModelHeader(bool index,uint32_t address,uint32_t bytes) {
  if(address%4 || bytes>0x03fffffcu)
    throw std::runtime_error("invalid native model header extent");
  return {index?0x20000002u:1u,1u,0u,0u,0u,0xffff0000u,
    index?address:(address|3u),index?bytes:((bytes&0x03fffffcu)|0x10000002u)};
}
}
