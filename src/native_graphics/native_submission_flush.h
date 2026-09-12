#pragma once
#include <cstdint>

namespace edf::native {
// Native cache collection has CPU dirty-range effects but produces no packets.
// Submission must precede observing the issued fence for the optional wait.
template<class Reader,class Collect,class Submit,class WaitEnabled,class Wait>
uint32_t FlushNativeSubmission(const Reader& reader,uint32_t device,Collect collect,
    Submit submit,WaitEnabled wait_enabled,Wait wait) {
  if(!reader.Word(reader.Add(device,12944)) && !(*reader.Bytes(reader.Add(device,10808),1)&0x80))
    collect();
  submit();
  if(!(*reader.Bytes(reader.Add(device,10808),1)&0x80) && wait_enabled() &&
      !(*reader.Bytes(reader.Add(device,10809),1)&2)) {
    wait(reader.Word(reader.Add(device,10780))-2u);
    const auto flags=reader.Add(device,10809);
    reader.StoreByte(flags,*reader.Bytes(flags,1)|2);
  }
  return reader.Word(reader.Add(device,40));
}
}
