#pragma once
#include "guest_block.h"
#include <cstdint>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace edf::native {
struct NamedParameterRecord {
  std::string name;
  uint32_t data, registers, first, available;
};

// Every block is validated through Reader::Bytes before decoding. No mapping,
// pointer or value is cached across calls: global vectors and names remain live.
template<class Reader>
std::vector<NamedParameterRecord> ReadNamedParameters(const Reader& reader,
    uint32_t instance,uint32_t stage_offset,bool global) {
  if(stage_offset!=0 && stage_offset!=36) throw std::runtime_error("invalid parameter stage");
  const auto word=GuestBlockWord;
  const auto* vector=reader.Bytes(reader.Add(instance,stage_offset+(global?24:12)),12);
  const auto count=word(vector+8);
  if(count>4096) throw std::runtime_error("invalid named parameter count");
  std::vector<NamedParameterRecord> result;
  if(!count) return result;
  const auto* records=reader.Bytes(word(vector),size_t(count)*16);
  result.reserve(count);
  for(uint32_t i=0;i<count;++i) {
    const auto* record=records+size_t(i)*16;
    const auto registers=word(record+8);
    if(registers>4096) throw std::runtime_error("invalid parameter register span");
    const auto name=reader.String(word(record+(global?4:0)),256);
    uint32_t data=word(record+4),available=registers;
    if(global) {
      const auto* value=reader.Bytes(word(record),12);
      data=word(value); available=word(value+8);
    }
    result.push_back({name,data,registers,word(record+12),available});
  }
  return result;
}

struct TextureParameterRecord { std::string name; uint32_t handle,slot; };
template<class Reader>
std::vector<TextureParameterRecord> ReadTextureParameters(const Reader& reader,
    uint32_t instance,bool global) {
  const auto word=GuestBlockWord;
  const auto* vector=reader.Bytes(reader.Add(instance,global?84:72),12);
  const auto count=word(vector+8);
  if(count>4096) throw std::runtime_error("invalid material texture count");
  std::vector<TextureParameterRecord> result;
  if(!count) return result;
  const size_t stride=global?8:28;
  const auto* records=reader.Bytes(word(vector),size_t(count)*stride);
  result.reserve(count);
  for(uint32_t i=0;i<count;++i) {
    const auto* record=records+size_t(i)*stride;
    std::string name;
    uint32_t handle;
    if(global) {
      const auto value=word(record);
      // 821A2178 returns node+40; the string key is node+12. Its inline
      // storage is key+4, length is key+20, capacity is key+24.
      if(value<40) throw std::runtime_error("invalid global texture node");
      const auto key_address=value-28;
      const auto* key=reader.Bytes(key_address,28);
      const auto length=word(key+20),capacity=word(key+24);
      if(length>255 || length>capacity) throw std::runtime_error("invalid global texture name");
      const auto address=capacity<16?reader.Add(key_address,4):word(key+4);
      name=reader.String(address,size_t(length)+1);
      if(name.size()!=length) throw std::runtime_error("global texture name length mismatch");
      handle=word(reader.Bytes(reader.Add(value,28),4));
    } else {
      name=reader.String(word(record),256);
      handle=word(record+4);
    }
    // Only used native samplers require a valid slot. Preserve optimized-out
    // records without touching device state or rejecting their unused slots.
    result.push_back({std::move(name),handle,word(record+(global?4:8))});
  }
  return result;
}

template<class Reader>
std::array<uint32_t,4> ReadSamplerWords(const Reader& reader,uint32_t device,uint32_t slot) {
  if(slot>=16) throw std::runtime_error("invalid guest pixel sampler slot");
  const auto* sampler=reader.Bytes(reader.Add(device,1024+slot*24),24);
  return {GuestBlockWord(sampler),GuestBlockWord(sampler+12),
          GuestBlockWord(sampler+16),GuestBlockWord(sampler+20)};
}
}  // namespace edf::native
