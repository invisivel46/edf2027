#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace edf::native {
// The game's textures, decoded from DDS into something any backend can create.
//
// Almost all of this is guest logic: a DDS header, FourCC and channel-mask
// pixel formats that have to be expanded to RGBA8, cube faces, mip chains, and
// the row-pitch rule that DDSD_PITCH only describes the top level. None of it
// is D3D11's, and re-deriving it against a second API is how two backends end
// up disagreeing about what a texture contains.
//
// Levels are face-major: face 0's mips, then face 1's, matching the order both
// APIs want subresources in.
struct NativeDecodedDdsLevel {
  std::span<const uint8_t> bytes;
  uint32_t pitch=0;   // Bytes per row, or per row of blocks for a BC format.
  uint32_t width=0,height=0;
};

struct NativeDecodedDds {
  uint32_t width=0,height=0,mip_count=1,faces=1;
  bool cube=false;
  uint32_t format=0;  // DXGI format code; see native_dxgi_format.h.
  // Levels that had to be converted own their bytes here. Block-compressed
  // levels instead point into the source span, which the caller must keep
  // alive for as long as this is used - saying so here because a dangling
  // texture upload reads as corruption rather than as a lifetime bug.
  std::vector<std::vector<uint8_t>> storage;
  std::vector<NativeDecodedDdsLevel> levels;
};

// Throws std::runtime_error on a malformed or unsupported file. Validates
// every level's extent against the payload before decoding anything.
NativeDecodedDds DecodeNativeDdsTexture(std::span<const uint8_t> data);
}  // namespace edf::native
