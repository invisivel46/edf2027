#pragma once
#include "native_index_binding.h"
#include <vector>

namespace edf::native {
struct NativeShaderDefaultWord { uint32_t offset,keep,value; };
struct NativeShaderDefaults {
  bool present=false;
  uint64_t clear_constants=0;
  bool dirty_shared=false;
  std::vector<NativeShaderDefaultWord> words;
};
template<class Reader>
NativeShaderDefaults ReadNativeShaderDefaults(const Reader& reader,uint32_t shader,bool pixel) {
  NativeShaderDefaults result;
  if(!shader) return result;
  const auto header=reader.Add(shader,pixel?40:872);
  const auto relative=reader.Word(reader.Add(header,20));
  if(!relative) return result;
  result.present=true;
  const auto data=reader.Add(header,relative);
  const auto wide=[&](uint32_t at) { return (uint64_t(reader.Word(at))<<32)|reader.Word(reader.Add(at,4)); };
  result.clear_constants=wide(data);
  result.dirty_shared=wide(reader.Add(data,8))!=0;
  const auto bytes=reader.Word(reader.Add(data,24));
  if(bytes>1<<20 || bytes%4) throw std::runtime_error("invalid native shader default extent");
  auto cursor=reader.Add(data,32);
  const auto end=reader.Add(cursor,bytes);
  const auto next=[&]() {
    if(cursor>end || end-cursor<4) throw std::runtime_error("truncated native shader defaults");
    const auto word=reader.Word(cursor); cursor=reader.Add(cursor,4); return word;
  };
  // The first section's entries are skipped by both retail binding routines.
  while(cursor<end) { const auto word=next(); if(!(word&0xffff)) break; (void)next(); }
  while(cursor<end) {
    const auto word=next(); const auto count=word&0xffff; const auto offset=word>>16;
    if(!count) break;
    if(offset%4 || count>(end-cursor)/4) throw std::runtime_error("invalid native shader default copy");
    for(uint32_t i=0;i<count;++i) result.words.push_back({offset+i*4,0,next()});
  }
  while(cursor<end) {
    const auto word=next(); const auto count=word&0xffff; const auto offset=word>>16;
    if(!count) break;
    if(offset%4 || count%2 || count>(end-cursor)/4) throw std::runtime_error("invalid native shader default mask");
    for(uint32_t i=0;i<count/2;++i) {
      const auto keep=next(),value=next(); result.words.push_back({offset+i*4,keep,value});
    }
  }
  return result;
}
template<class Reader,class Reserve,class LegacyTag>
void SetNativeShaderResource(const Reader& reader,uint32_t device,uint32_t shader,bool pixel,
    Reserve reserve,LegacyTag tag) {
  const auto slot=reader.Add(device,pixel?12416:12420);
  RetireNativeBoundResource(reader,device,reader.Word(slot),reserve,tag);
  reader.StoreWord(slot,shader);
  const auto wide=[&](uint32_t offset) {
    const auto at=reader.Add(device,offset);
    return (uint64_t(reader.Word(at))<<32)|reader.Word(reader.Add(at,4));
  };
  const auto write=[&](uint32_t offset,uint64_t value) {
    reader.StoreDoubleWord(reader.Add(device,offset),value);
  };
  if(!pixel) reader.StoreByte(reader.Add(device,10810),*reader.Bytes(reader.Add(device,10810),1)&0x7f);
  // Preserve the original two pixel dirty stores and retirement-before-defaults
  // ordering (82149608/821498C8). Retirement may write aliased shader memory.
  if(pixel) {
    const auto dirty=wide(16)|(uint64_t(1)<<52);
    write(16,dirty);
    write(16,dirty|(uint64_t(1)<<49));
  } else write(16,wide(16)|(uint64_t(1)<<51));
  if(!shader) return;
  const auto defaults=ReadNativeShaderDefaults(reader,shader,pixel);
  if(!defaults.present) return;
  write(pixel?8:0,wide(pixel?8:0)&~defaults.clear_constants);
  if(defaults.dirty_shared) write(24,wide(24)|2);
  for(const auto& word:defaults.words) {
    const auto at=reader.Add(device,1024+word.offset);
    reader.StoreWord(at,(reader.Word(at)&word.keep)|word.value);
  }
}
}
