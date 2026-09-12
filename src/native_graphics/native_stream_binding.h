#pragma once
#include "native_index_binding.h"

namespace edf::native {
// Preserve CPU fetch/dirty fields until their remaining consumers migrate.
// Native ownership separately retains the untruncated offset and byte stride.
template<class Reader,class Reserve,class LegacyTag,class Observe=IgnoreNativeRetirement>
void SetNativeStreamResource(const Reader& reader,uint32_t device,uint32_t stream,
    uint32_t resource,uint32_t offset,uint32_t stride,uint64_t dirty_mask,
    Reserve reserve,LegacyTag legacy_tag,Observe observe={}) {
  if(stream>=16) throw std::runtime_error("invalid native vertex stream");
  const auto dirty=reader.Add(device,16);
  if(resource) {
    const uint32_t address=reader.Word(reader.Add(resource,24))+offset;
    const uint32_t descriptor=reader.Word(reader.Add(resource,28))-offset;
    reader.StoreWord(reader.Add(device,1788-stream*8),descriptor);
    reader.StoreWord(reader.Add(device,1784-stream*8),
      (address&0x1fffffffu)+(((address>>20)+512u)&0x1000u));
    reader.StoreDoubleWord(dirty,reader.DoubleWord(dirty)|dirty_mask);
  }
  const auto slot=reader.Add(device,12188+stream*4);
  RetireNativeBoundResource(reader,device,reader.Word(slot),reserve,legacy_tag,observe);
  reader.StoreWord(slot,resource);
  const uint32_t words=stride>>2;
  reader.StoreByte(reader.Add(device,12256+stream),uint8_t(words));
  if(words && words!=*reader.Bytes(reader.Add(device,11552+stream),1))
    reader.StoreDoubleWord(dirty,reader.DoubleWord(dirty)|(uint64_t(1)<<51));
}
}
