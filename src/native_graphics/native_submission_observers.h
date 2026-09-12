#pragma once
#include <cstdint>
#include <span>

namespace edf::native {
struct NativeSubmissionDescriptor { uint32_t words,address; };
constexpr uint64_t NativeObserverAddress(uint32_t address) {
  return uint64_t(address)-(address<0x20000000u?0x40000000ull:0x41000000ull);
}
// Descriptors are host-owned before callbacks run. Device cursor and observer
// configuration are accessed through the supplied reader (production owns the
// cursor natively); neither ring memory nor MMIO is used.
template<class Reader,class Callback>
void SubmitNativeObservers(const Reader& reader,uint32_t device,
    std::span<const NativeSubmissionDescriptor> descriptors,Callback callback) {
  const auto mask=reader.Word(reader.Add(device,13480));
  if(*reader.Bytes(reader.Add(device,10809),1)&2) {
    if(!reader.Word(reader.Add(device,19956))) return;
    for(const auto& range:descriptors) {
      callback(reader.Word(reader.Add(device,19956)),24,NativeObserverAddress(range.address),range.words,1,0x8213C48C);
      callback(reader.Word(reader.Add(device,19956)),28,0,0,0,0x8213C4A0);
    }
    return;
  }
  auto cursor=reader.Word(reader.Add(device,10820));
  for(const auto& range:descriptors) {
    const auto object=reader.Word(reader.Add(device,19956));
    if(object) callback(object,24,NativeObserverAddress(range.address),range.words,1,0x8213C518);
    const auto external=reader.Word(reader.Add(device,20100));
    if(external) callback(external,0,NativeObserverAddress(range.address),range.words,1,0x8213C548);
    // Retain three individual masks: arbitrary masks need not be 2^n-1.
    cursor=(cursor+1u)&mask; cursor=(cursor+1u)&mask; cursor=(cursor+1u)&mask;
  }
  reader.StoreWord(reader.Add(device,10820),cursor);
  const auto object=reader.Word(reader.Add(device,19956));
  if(object) callback(object,28,0,0,0,0x8213C5C0);
  const auto external=reader.Word(reader.Add(device,20100));
  if(external) callback(external,0,0,cursor,2,0x8213C5E4);
}
}
