#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace edf::native {
// The cache validates the full layout/index pattern and uploads changed vertex
// contents with WRITE_DISCARD; contents need not allocate another cache entry.
inline std::array<uint32_t,5> ImmediateStreamKey(size_t bytes,
    uint32_t declaration,uint32_t shader,uint32_t primitive,bool reversed) {
  return {uint32_t(bytes),uint32_t(uint64_t(bytes)>>32),declaration,shader,(primitive<<1)|uint32_t(reversed)};
}
// Scratch addresses cycle across many different meshes. Content selects a
// candidate only: NativeMeshCache still compares all bytes before any reuse.
inline std::array<uint32_t,5> ImmediateMeshKey(std::span<const uint8_t> vertices,
    uint32_t declaration,uint32_t shader,uint32_t primitive,bool reversed) {
  uint64_t hash=14695981039346656037ull;
  for(uint8_t byte:vertices) { hash^=byte; hash*=1099511628211ull; }
  return {uint32_t(hash),uint32_t(hash>>32),declaration,shader,(primitive<<1)|uint32_t(reversed)};
}
}
