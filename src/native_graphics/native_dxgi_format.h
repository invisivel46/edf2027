#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

namespace edf::native {
// Size arithmetic for the formats this renderer uses, shared by the two
// DXGI-based backends so they cannot disagree about how many bytes a texture
// level occupies.
//
// Block-compressed formats are the reason this exists. BC1/2/3 are what the
// game's textures actually are, and for them "bytes per texel" is not a whole
// number: a level is a grid of 4x4 blocks, a 2x2 level still costs a whole
// block, and computing a row pitch as width x bytes gives an answer that is
// wrong in a way that reads as corrupted texture data rather than as an error.
struct NativeDxgiFormatInfo {
  uint32_t bytes_per_block=0;
  uint32_t block_width=1,block_height=1;
  bool compressed() const { return block_width>1; }
};

// Throws on a format this renderer does not handle, rather than guessing a
// plausible size and silently reading the wrong number of bytes.
inline NativeDxgiFormatInfo DescribeNativeDxgiFormat(uint32_t format) {
  switch(format) {
    case 71: case 72: case 73:          // BC1_TYPELESS / BC1_UNORM / BC1_UNORM_SRGB
      return {8,4,4};
    case 74: case 75: case 76:          // BC2
    case 77: case 78: case 79:          // BC3
      return {16,4,4};
    case 28: case 29:                   // R8G8B8A8_UNORM / _SRGB
    case 87: case 91:                   // B8G8R8A8_UNORM / _SRGB
    case 24:                            // R10G10B10A2_UNORM
    case 34:                            // R16G16_FLOAT
    case 41:                            // R32_FLOAT
    case 45:                            // D24_UNORM_S8_UINT
    case 40:                            // D32_FLOAT
      return {4,1,1};
    case 20:                            // D32_FLOAT_S8X24_UINT
      return {8,1,1};
    case 10:                            // R16G16B16A16_FLOAT
      return {8,1,1};
    case 2:                             // R32G32B32A32_FLOAT
      return {16,1,1};
    case 61:                            // R8_UNORM
      return {1,1,1};
    case 49: case 54:                   // R8G8_UNORM / R16_FLOAT
      return {2,1,1};
    default: break;
  }
  throw std::runtime_error("unsupported texture format "+std::to_string(format));
}

// Bytes in one row of blocks. For an uncompressed format this is the usual
// width x texel size; for a compressed one it counts blocks, not texels.
inline uint64_t NativeDxgiRowPitch(const NativeDxgiFormatInfo& info, uint32_t width) {
  const uint64_t blocks=(static_cast<uint64_t>(width)+info.block_width-1)/info.block_width;
  return blocks*info.bytes_per_block;
}
inline uint32_t NativeDxgiRowCount(const NativeDxgiFormatInfo& info, uint32_t height) {
  return static_cast<uint32_t>((static_cast<uint64_t>(height)+info.block_height-1)/info.block_height);
}
// Bytes one mip level occupies, tightly packed.
inline uint64_t NativeDxgiLevelBytes(const NativeDxgiFormatInfo& info, uint32_t width, uint32_t height) {
  return NativeDxgiRowPitch(info,width)*NativeDxgiRowCount(info,height);
}
}  // namespace edf::native
