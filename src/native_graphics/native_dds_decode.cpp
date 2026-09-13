#include "native_dds_decode.h"
#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>
#include <string>

namespace edf::native {
namespace {
uint32_t Word(std::span<const uint8_t> data, size_t at) {
  return uint32_t(data[at]) | uint32_t(data[at+1])<<8 | uint32_t(data[at+2])<<16 |
         uint32_t(data[at+3])<<24;
}
uint8_t Channel(uint32_t pixel, uint32_t mask, uint8_t absent) {
  if(!mask) return absent;
  const auto shift=std::countr_zero(mask);
  const auto width=std::bit_width(mask>>shift);
  const auto value=(pixel&mask)>>shift;
  return uint8_t(width>=8 ? value>>(width-8) : value*255/((1u<<width)-1));
}
constexpr uint32_t kRgba8=28;  // DXGI_FORMAT_R8G8B8A8_UNORM
constexpr uint32_t kBc1=71,kBc2=74,kBc3=77;
}  // namespace

NativeDecodedDds DecodeNativeDdsTexture(std::span<const uint8_t> data) {
  if(data.size()<128 || Word(data,0)!=0x20534444 || Word(data,4)!=124 || Word(data,76)!=32)
    throw std::runtime_error("invalid DDS header");
  NativeDecodedDds result;
  result.width=Word(data,16);
  result.height=Word(data,12);
  result.mip_count=(std::max)(1u,Word(data,28));
  const uint32_t caps2=Word(data,112);
  result.cube=(caps2&0x200)!=0;
  if(!result.width || !result.height || result.width>16384 || result.height>16384 ||
     result.mip_count>std::bit_width((std::max)(result.width,result.height)) ||
     Word(data,24)>1 || (caps2&0x200000)) throw std::runtime_error("unsupported DDS dimensions");
  if(result.cube && ((caps2&0xfc00)!=0xfc00 || result.width!=result.height))
    throw std::runtime_error("incomplete or nonsquare DDS cube");

  uint32_t block_bytes=0,pixel_bytes=0;
  result.format=kRgba8;
  const auto flags=Word(data,80);
  // Retail MapXX/Shadow*.dds use DDPF_ALPHA / A8, not RGB luminance.
  // Expand to (0,0,0,A); Common.fx consumes the sampled alpha component.
  const bool alpha_only=flags==2 && Word(data,88)==8 && Word(data,92)==0 &&
    Word(data,96)==0 && Word(data,100)==0 && Word(data,104)==255;
  if(flags&4) {
    switch(Word(data,84)) {
      case 0x31545844: block_bytes=8; result.format=kBc1; break;
      case 0x33545844: block_bytes=16; result.format=kBc2; break;
      case 0x35545844: block_bytes=16; result.format=kBc3; break;
      default: throw std::runtime_error("unsupported DDS FourCC: "+std::to_string(Word(data,84)));
    }
  } else {
    const auto bits=Word(data,88);
    if((!alpha_only && !(flags&0x40)) || !bits || bits>32 || bits%8)
      throw std::runtime_error("unsupported DDS pixel format: flags="+std::to_string(flags)+
        " bits="+std::to_string(bits)+" masks="+std::to_string(Word(data,92))+"/"+
        std::to_string(Word(data,96))+"/"+std::to_string(Word(data,100))+"/"+std::to_string(Word(data,104)));
    pixel_bytes=bits/8;
  }
  const std::array<uint32_t,4> masks{Word(data,92),Word(data,96),Word(data,100),Word(data,104)};
  if(!block_bytes) {
    uint32_t used=0;
    for(auto mask:masks) {
      if((used&mask) || (pixel_bytes<4 && (mask>>(pixel_bytes*8))))
        throw std::runtime_error("overlapping or out-of-range DDS channel masks");
      if(mask) {
        const auto normalized=mask>>std::countr_zero(mask);
        if(normalized&(normalized+1u)) throw std::runtime_error("noncontiguous DDS channel mask");
      }
      used|=mask;
    }
    if(!alpha_only && (!masks[0] || !masks[1] || !masks[2]))
      throw std::runtime_error("missing DDS RGB mask");
  }

  result.faces=result.cube?6:1;
  const size_t count=size_t(result.faces)*result.mip_count;
  result.storage.resize(count);
  result.levels.resize(count);
  size_t cursor=128,decoded_total=0;
  for(uint32_t face=0;face<result.faces;++face) for(uint32_t mip=0;mip<result.mip_count;++mip) {
    const uint32_t width=(std::max)(1u,result.width>>mip),height=(std::max)(1u,result.height>>mip);
    const uint32_t rows=block_bytes?(height+3)/4:height;
    uint32_t pitch=block_bytes?((width+3)/4)*block_bytes:width*pixel_bytes;
    // For raw top levels DDSD_PITCH describes row padding; subsequent mip
    // rows in this importer use their tightly packed byte width.
    if(!block_bytes && mip==0 && (Word(data,8)&8)) {
      const auto declared=Word(data,20);
      if(declared<pitch) throw std::runtime_error("DDS row pitch is too small");
      pitch=declared;
    }
    const size_t size=size_t(pitch)*rows;
    if(cursor>data.size() || size>data.size()-cursor)
      throw std::runtime_error("truncated DDS mip payload");
    const size_t index=size_t(face)*result.mip_count+mip;
    if(block_bytes) {
      result.levels[index]={data.subspan(cursor,size),pitch,width,height};
    } else {
      const size_t decoded_size=size_t(width)*height*4;
      decoded_total+=decoded_size;
      if(decoded_total>256*1024*1024) throw std::runtime_error("DDS decoded size limit exceeded");
      auto& pixels=result.storage[index];
      pixels.resize(decoded_size);
      for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x) {
        const auto* source=data.data()+cursor+size_t(y)*pitch+size_t(x)*pixel_bytes;
        uint32_t value=0;
        for(uint32_t b=0;b<pixel_bytes;++b) value|=uint32_t(source[b])<<(8*b);
        auto* target=pixels.data()+(size_t(y)*width+x)*4;
        for(size_t c=0;c<4;++c) target[c]=Channel(value,masks[c],c==3?255:0);
      }
      result.levels[index]={pixels,width*4,width,height};
    }
    cursor+=size;
  }
  return result;
}
}  // namespace edf::native
