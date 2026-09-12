#pragma once
#include <cstdint>

namespace edf::native {
constexpr uint32_t NativeCommandAddress(uint32_t address) {
  return (address&0x1fffffffu)+(((address>>20)+512u)&0x1000u);
}
template<class Reader,class GrowDescriptor,class GrowWorker,class Capture,class Submit,class Refill>
void SubmitNativeDescriptor(const Reader& reader,uint32_t device,GrowDescriptor grow_descriptor,
    GrowWorker grow_worker,Capture capture,Submit submit,Refill refill) {
  const auto flags=*reader.Bytes(reader.Add(device,10809),1);
  const auto first=reader.Word(reader.Add(device,13508));
  uint32_t next=reader.Word(reader.Add(device,40))+4u;
  if(!(flags&0x40)) {
    if(*reader.Bytes(reader.Add(device,10808),1)&0x80) {
      const auto descriptor=reader.Word(reader.Add(device,13140));
      if(descriptor && !reader.Word(reader.Add(descriptor,152))) {
        const uint32_t words=uint32_t(int32_t(next-first)>>2);
        if(words) {
          auto entry=reader.Word(reader.Add(device,13160));
          if(entry>=reader.Word(reader.Add(device,13164))) entry=grow_descriptor();
          reader.StoreWord(entry,NativeCommandAddress(first));
          reader.StoreWord(reader.Add(entry,4),words);
          reader.StoreWord(reader.Add(device,13160),entry+8u);
        }
      }
    } else if(reader.Word(reader.Add(device,12944))) {
      const uint32_t words=uint32_t(int32_t(next-first)>>2);
      if(words) {
        auto entry=reader.Word(reader.Add(device,13008));
        if(uint32_t(entry+8u)>reader.Word(reader.Add(device,13012))) entry=grow_worker();
        // CPU worker command stream, not an Xbox GPU packet submission.
        reader.StoreWord(reader.Add(entry,4),NativeCommandAddress(first));
        reader.StoreWord(entry,words|0x82000000u);
        reader.StoreWord(reader.Add(device,13008),entry+8u);
      }
    } else {
      const auto captured=capture(next);
      next=submit(captured,NativeCommandAddress(first),uint32_t(int32_t(captured-first)>>2));
    }
  }
  const uint32_t aligned=(next+31u)&~31u;
  if(aligned>reader.Word(reader.Add(device,48))) {
    reader.StoreWord(reader.Add(device,40),next-4u);
    refill();
  } else {
    const auto page_align=reader.Word(reader.Add(device,19956));
    reader.StoreWord(reader.Add(device,13508),aligned);
    reader.StoreWord(reader.Add(device,40),aligned-4u);
    if(page_align) {
      const auto backward=reader.Word(reader.Add(device,13496));
      if(backward) reader.StoreWord(reader.Add(device,13496),(backward+4095u)&~4095u);
    }
  }
}
}
