#pragma once
#include <cstdint>
#include <initializer_list>
#include <stdexcept>

namespace edf::native {
template<class Reader,class Call>
void DispatchNativeFrameBuckets(const Reader& reader,uint32_t owner,uint32_t context,Call call) {
  const auto zero=reader.Word(0x820009a4);
  for(const auto offset:{32u,36u,40u}) reader.StoreWord(reader.Add(context,offset),zero);
  reader.StoreWord(reader.Add(context,44),reader.Word(0x820008cc));
  for(uint32_t index=0;index<256;++index) {
    auto object=reader.Word(reader.Add(owner,168+index*4));
    while(object) {
      const auto next=reader.Word(reader.Add(object,60));
      const auto bucket=reader.Add(owner,1192+uint32_t(*reader.Bytes(reader.Add(object,41),1))*4);
      reader.StoreWord(reader.Add(object,60),reader.Word(bucket));
      reader.StoreWord(bucket,object);
      object=next;
    }
  }
  // Bucket zero is deliberately excluded by the original descending loop.
  for(uint32_t index=255;index>0;--index) {
    auto object=reader.Word(reader.Add(owner,1192+index*4));
    while(object) {
      reader.StoreWord(reader.Add(context,40),reader.Word(reader.Add(object,44)));
      call(reader.Word(reader.Add(reader.Word(object),16)),object,context,0,0x821A3C54);
      object=reader.Word(reader.Add(object,60));
    }
  }
}
// Native ownership of the outer frame phase order. The callback boundary is
// explicit so remaining world, overlay and presentation phases can migrate
// independently. Lists are reacquired after callbacks that may mutate them.
template<class Reader,class Call>
void DispatchNativeFrame(const Reader& reader,uint32_t owner,uint32_t context,Call call) {
  const auto word=[&](uint32_t offset) { return reader.Word(reader.Add(owner,offset)); };
  const auto flag=[&](uint32_t offset) { return *reader.Bytes(reader.Add(owner,offset),1)!=0; };
  const auto virtual_call=[&](uint32_t object,uint32_t method,uint32_t arg,uint32_t index,uint32_t lr) {
    call(reader.Word(reader.Add(reader.Word(object),method)),object,arg,index,lr);
  };
  if(!flag(2261) && !flag(2262) && (flag(2216) || flag(2217))) {
    const auto zero=reader.Word(0x820009a4),one=reader.Word(0x820008cc);
    for(const auto offset:{0u,4u,32u,36u,40u}) reader.StoreWord(reader.Add(context,offset),zero);
    reader.StoreWord(reader.Add(context,44),one);
    reader.StoreWord(reader.Add(context,12),0);
    reader.StoreWord(reader.Add(context,16),0);
    auto view_node=word(0);
    const auto view_end=word(12);
    const auto near_value=reader.Word(0x8201711c),far_value=reader.Word(0x82017120);
    uint32_t view_index=0;
    while(view_node!=view_end) {
      const auto view=reader.Word(reader.Add(view_node,8));
      virtual_call(word(132),4,view,view_index,0x821A515C);
      const auto serial=word(136);
      reader.StoreWord(context,near_value);
      reader.StoreWord(reader.Add(context,4),far_value);
      reader.StoreWord(reader.Add(context,8),reader.Word(reader.Add(view,400)));
      reader.StoreWord(reader.Add(context,12),serial);
      reader.StoreWord(reader.Add(context,16),view);
      reader.StoreWord(reader.Add(owner,136),serial+1);
      for(uint32_t i=0;i<512;++i) reader.StoreWord(reader.Add(owner,168+i*4),0);
      auto world_node=word(44);
      const auto world_end=word(56);
      while(world_node!=world_end) {
        virtual_call(reader.Word(reader.Add(world_node,8)),8,context,0,0x821A51DC);
        world_node=reader.Word(world_node);
      }
      call(0x821A3BA0,owner,context,0,0x821A520C);
      auto overlay_node=reader.Word(word(2232));
      while(overlay_node!=word(2232)) {
        virtual_call(reader.Word(reader.Add(overlay_node,12)),12,context,0,0x821A5268);
        if(overlay_node==word(2232)) throw std::runtime_error("native frame overlay iterator invalidated");
        overlay_node=reader.Word(overlay_node);
      }
      virtual_call(view,16,0,0,0x821A5294);
      virtual_call(word(132),8,view,0,0x821A52AC);
      view_node=reader.Word(view_node);
      ++view_index;
    }
  }
  virtual_call(word(132),12,0,0,0x821A52E8);
  virtual_call(word(132),16,0,0,0x821A52FC);
  for(auto phase=word(140);phase!=word(144);++phase) {
    auto node=reader.Word(word(2232));
    while(node!=word(2232)) {
      virtual_call(reader.Word(reader.Add(node,12)),16,phase,0,0x821A536C);
      if(node==word(2232)) throw std::runtime_error("native frame phase iterator invalidated");
      node=reader.Word(node);
    }
  }
}
}
