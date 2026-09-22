#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace edf::native {
// The last accepted instance owns these bytes. Publish host shader mirrors only
// after the complete CPU matrix is restored; callers retain the pending matrix
// until both publications succeed.
template<class Reader,class Publish>
void RestoreNativeSceneWorld(const Reader& reader,uint32_t device,uint32_t first,
    const std::array<uint8_t,64>& bytes,Publish&& publish) {
  auto* destination=const_cast<uint8_t*>(reader.WritableBytes(reader.Add(device,(112+first)*16),64,4));
  std::memcpy(destination,bytes.data(),bytes.size());
  publish(bytes);
}
}
