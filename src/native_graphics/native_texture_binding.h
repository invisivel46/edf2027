#pragma once
#include "native_index_binding.h"
#include <array>
#include <algorithm>
#include <bit>

namespace edf::native {
inline std::array<uint32_t,6> NativeTextureBindingWords(std::array<uint32_t,6> previous,
    const std::array<uint32_t,6>& texture,uint8_t minimum,uint8_t maximum) {
  const auto address=[](uint32_t word) { return (word&0x1fffffffu)+(((std::rotl(word,12)&0xfffu)+512)&0x1000u); };
  std::array<uint32_t,6> result;
  result[0]=(texture[0]&~0x3ffc00u)|(previous[0]&0x3ffc00u);
  result[1]=(address(texture[1])&~0x800u)|(previous[1]&0x800u);
  result[2]=texture[2];
  result[3]=(texture[3]&~0x7ff80000u)|(previous[3]&0x7ff80000u);
  result[4]=(previous[4]&~0x3fcu)|
    ((std::max(uint32_t(minimum),(texture[4]>>2)&15u)<<2)&0x3cu)|
    ((std::min(uint32_t(maximum),(texture[4]>>6)&15u)<<6)&0x3c0u);
  result[5]=(texture[5]&0x1ffffe00u)+(((std::rotl(texture[5],12)&0xfffu)+512)&0x1000u);
  result[5]=(result[5]&~0x1ffu)|(previous[5]&0x1ffu);
  return result;
}
template<class Reader,class Reserve,class LegacyTag>
void SetNativeTextureResource(const Reader& reader,uint32_t device,uint32_t slot,uint32_t texture,
    uint64_t dirty_mask,Reserve reserve,LegacyTag tag) {
  // Device initialization clears all 26 bindings, including nonpixel slots.
  if(slot>=26) throw std::runtime_error("native texture binding slot exceeds device range");
  const auto binding=reader.Add(device,12272+slot*4);
  const auto previous=reader.Word(binding);
  if(texture) {
    std::array<uint32_t,6> old{},header{};
    const auto destination=reader.Add(device,1024+slot*24);
    for(uint32_t i=0;i<6;++i) {
      old[i]=reader.Word(reader.Add(destination,i*4));
      header[i]=reader.Word(reader.Add(texture,28+i*4));
    }
    const auto words=NativeTextureBindingWords(old,header,
      *reader.Bytes(reader.Add(device,11678+slot),1),*reader.Bytes(reader.Add(device,11704+slot),1));
    for(uint32_t i=0;i<6;++i) reader.StoreWord(reader.Add(destination,i*4),words[i]);
    const uint64_t dirty=(uint64_t(reader.Word(reader.Add(device,16)))<<32)|reader.Word(reader.Add(device,20));
    reader.StoreDoubleWord(reader.Add(device,16),dirty|dirty_mask);
  }
  // Retail publishes the new handle before retiring the previous resource.
  reader.StoreWord(binding,texture);
  RetireNativeBoundResource(reader,device,previous,reserve,tag);
}
}
