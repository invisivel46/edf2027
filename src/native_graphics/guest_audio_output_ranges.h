#pragma once
#include <cstdint>
#include <stdexcept>

namespace edf::native {
// Retail XMA owner: count at +0, 96-byte records at +8. Each record holds
// pending context words and a live 64-byte context pointer at +64. Include both:
// 823C1F88 may copy pending words or simply resume the already-live context.
template<class Reader,class Visitor>
uint32_t VisitGuestAudioOutputRanges(const Reader& reader,uint32_t owner,Visitor&& visit) {
  const auto count=reader.Word(owner);
  if(count>320) throw std::runtime_error("XMA record count exceeds decoder capacity");
  const auto records=reader.Word(reader.Add(owner,8));
  uint32_t ranges=0;
  auto observe=[&](uint32_t context) {
    const uint32_t length=((reader.Word(context)>>22)&31)*256;
    if(!length) return;
    const auto physical=reader.Word(reader.Add(context,28));
    if(physical>=0x20000000u || length>0x20000000u-physical)
      throw std::runtime_error("invalid XMA physical output range");
    visit(physical,length); ++ranges;
  };
  for(uint32_t i=0;i<count;++i) {
    const auto record=reader.Add(records,i*96);
    observe(record);
    const auto live=reader.Word(reader.Add(record,64));
    if(live) observe(live);
  }
  return ranges;
}
}
