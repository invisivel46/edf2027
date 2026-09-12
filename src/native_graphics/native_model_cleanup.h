#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

namespace edf::native {
// Model VB/IB cleanup only. Preserve retail device-reference and unbind
// semantics until the remaining CPU resource consumers are migrated.
template<class Reader,class Device,class Unbind,class Release>
void CleanupNativeModelResource(const Reader& reader,uint32_t owner,bool index,
    Device get_device,Unbind unbind,Release release) {
  if(*reader.Bytes(reader.Add(owner,52),1)) {
    const auto type=reader.Word(owner)&15;
    if(type!=(index?2u:1u)) throw std::runtime_error("unexpected native model cleanup resource type");
    const auto device=get_device();
    if(!device) throw std::runtime_error("native model cleanup without device");
    const auto refs=reader.Add(device,52);
    reader.StoreCpuWords(refs,std::array<uint32_t,1>{reader.Word(refs)+1u});
    bool bound=false;
    if(index) bound=reader.Word(reader.Add(device,12164))==owner;
    else for(uint32_t slot=0;slot<16;++slot)
      bound|=reader.Word(reader.Add(device,12188+slot*4))==owner;
    if(bound) unbind(device,index);
    release(reader.Add(owner,32));
    *const_cast<uint8_t*>(reader.WritableBytes(reader.Add(owner,52),1,4))=0;
  }
  reader.StoreWord(reader.Add(owner,56),0);
  if(!index) {
    reader.StoreWord(reader.Add(owner,60),0);
    reader.StoreWord(reader.Add(owner,64),0);
  }
}
}
