#pragma once
#include "guest_parameter_records.h"
#include <map>
#include <memory>

namespace edf::native {
// Names/register layout are published by material construction/copy. Values
// remain live: copied retail records may still refer to source-owned storage.
struct NativeMaterialParameter {
  std::string name;
  uint32_t record,registers,first;
  struct Value { uint32_t data,available; };
  template<class Reader> Value ReadValue(const Reader& reader,bool global) const {
    if(!global) return {GuestBlockWord(reader.Bytes(reader.Add(record,4),4)),registers};
    const auto address=GuestBlockWord(reader.Bytes(record,4));
    const auto* vector=reader.Bytes(address,12);
    return {GuestBlockWord(vector),GuestBlockWord(vector+8)};
  }
};
struct NativeMaterialTexture {
  std::string name;
  uint32_t record;
  struct Value { uint32_t handle,slot; };
  template<class Reader> Value ReadValue(const Reader& reader,bool global) const {
    if(!global) {
      const auto* value=reader.Bytes(reader.Add(record,4),8);
      return {GuestBlockWord(value),GuestBlockWord(value+4)};
    }
    const auto* value=reader.Bytes(record,8);
    const auto address=GuestBlockWord(value),slot=GuestBlockWord(value+4);
    if(address<40) throw std::runtime_error("invalid native global texture node");
    return {GuestBlockWord(reader.Bytes(reader.Add(address,28),4)),slot};
  }
};
class NativeMaterialParameters {
 public:
  struct Groups : std::array<std::vector<NativeMaterialParameter>,4> {
    std::array<std::vector<NativeMaterialTexture>,2> textures;
  };
  template<class Reader> void Publish(const Reader& reader,uint32_t instance) {
    if(!instance) throw std::runtime_error("null native material owner");
    auto fresh=std::make_shared<Groups>();
    for(uint32_t stage=0;stage<2;++stage) for(uint32_t global=0;global<2;++global) {
      const auto* vector=reader.Bytes(reader.Add(instance,stage*36+(global?24:12)),12);
      const auto count=GuestBlockWord(vector+8),address=GuestBlockWord(vector);
      if(count>4096) throw std::runtime_error("invalid native material parameter count");
      if(!count) continue;
      const auto* records=reader.Bytes(address,size_t(count)*16);
      auto& group=(*fresh)[stage*2+global]; group.reserve(count);
      for(uint32_t i=0;i<count;++i) {
        const auto* record=records+size_t(i)*16;
        const auto registers=GuestBlockWord(record+8);
        if(registers>4096) throw std::runtime_error("invalid native material register count");
        group.push_back({reader.String(GuestBlockWord(record+(global?4:0)),256),
          reader.Add(address,size_t(i)*16),registers,GuestBlockWord(record+12)});
      }
    }
    for(uint32_t global=0;global<2;++global) {
      auto records=ReadTextureParameters(reader,instance,global!=0);
      if(records.empty()) continue;
      const auto address=GuestBlockWord(reader.Bytes(reader.Add(instance,global?84:72),4));
      auto& textures=fresh->textures[global]; textures.reserve(records.size());
      for(size_t i=0;i<records.size();++i)
        textures.push_back({std::move(records[i].name),reader.Add(address,uint32_t(i*(global?8:28)))});
    }
    owners_.insert_or_assign(instance,std::move(fresh));
  }
  std::shared_ptr<const Groups> Get(uint32_t instance) const {
    const auto found=owners_.find(instance);
    if(found==owners_.end()) throw std::runtime_error("unpublished native material parameters");
    return found->second;
  }
  void Retire(uint32_t instance) { owners_.erase(instance); }
  void RetireArray(uint32_t address,uint32_t count) {
    const uint64_t end=uint64_t(address)+uint64_t(count)*112;
    if(end>(uint64_t(1)<<32)) throw std::runtime_error("invalid material retirement extent");
    for(auto it=owners_.lower_bound(address);it!=owners_.end() && it->first<end;) {
      if((it->first-address)%112==0) it=owners_.erase(it); else ++it;
    }
  }
 private:
  std::map<uint32_t,std::shared_ptr<const Groups>> owners_;
};
}
