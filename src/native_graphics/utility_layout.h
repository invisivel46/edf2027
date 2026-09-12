#pragma once
#include "guest_block.h"
#include <vector>

namespace edf::native {
// Only for the fingerprint-checked Utility VS_2D / VS_2DTex entry points.
// Retail solid vertices declare float3 POSITION at 0 and COLOR at 8. The
// source's float2 input ignores the overlapping third component. Normalize
// that unused component here, not in the general mesh overlap validator.
inline std::vector<uint8_t> Utility2DDeclaration(std::span<const uint8_t> declaration,
                                               bool textured) {
  if(declaration.size()!=(textured?36u:24u))
    throw std::runtime_error("unsupported Utility declaration size");
  auto word=[&](size_t at){return GuestBlockWord(declaration.data()+at);};
  const size_t color=textured?24:12;
  if(word(0)!=0 || word(4)!=(textured?0x2c23a5u:0x2a23b9u) ||
     (word(8)&0xffffff00)!=0 || word(color)!=(textured?16u:8u) ||
     word(color+4)!=0x182886 || (word(color+8)&0xffffff00)!=0xa0000 ||
     (textured && (word(12)!=8 || word(16)!=0x2c23a5 || (word(20)&0xffffff00)!=0x50000)))
    throw std::runtime_error("unsupported Utility vertex elements");
  std::vector<uint8_t> native(declaration.begin(),declaration.end());
  if(!textured) {
    native[4]=0; native[5]=0x2c; native[6]=0x23; native[7]=0xa5;
  }
  return native;
}
}  // namespace edf::native
