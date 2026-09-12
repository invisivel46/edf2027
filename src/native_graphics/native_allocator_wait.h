#pragma once
#include <cstdint>

namespace edf::native {
// Six worker signal slots. A stopped poll ends the current slot, not traversal.
template<class Reader,class Begin,class Poll,class End>
void WaitNativeWorkerSlots(const Reader& reader,uint32_t device,Begin begin,Poll poll,End end) {
  for(uint32_t slot=0;slot<6;++slot) {
    const auto entry=reader.Add(device,11208+slot*56);
    if(!reader.Word(reader.Add(entry,4))) continue;
    const auto address=reader.Word(entry-4);
    const auto generation=reader.Word(entry);
    const auto target=(address&~3u)|(generation&3u);
    begin();
    do { if(!poll()) break; }
    while(reader.Word(reader.Word(reader.Add(entry,16)))!=target);
    end();
  }
}

constexpr bool NativeGenerationPending(uint32_t pointer,uint32_t generation,uint32_t completed) {
  const auto distance=(generation-completed)&3u;
  return distance!=0 && (distance!=1 || pointer>(completed&~3u));
}
constexpr bool NativeRingRangePending(uint32_t begin,uint32_t end,uint32_t consumed) {
  return begin<end ? begin<consumed && consumed<=end : begin<consumed || consumed<=end;
}
template<class Reader,class Begin,class Poll,class End>
void WaitNativeAllocationGeneration(const Reader& reader,uint32_t device,uint32_t pointer,
    uint32_t generation,Begin begin,Poll poll,End end) {
  auto pending=[&] { return NativeGenerationPending(pointer,generation,
    reader.Word(reader.Add(reader.Word(reader.Add(device,10768)),4))); };
  if(!pending()) return;
  begin();
  // These allocator waits poll before checking completion again, unlike C928.
  do { if(!poll()) break; } while(pending());
  end();
}
template<class Reader,class Begin,class Poll,class End>
uint32_t WaitNativeRingRange(const Reader& reader,uint32_t device,uint32_t start,
    uint32_t size,Begin begin,Poll poll,End end) {
  const uint32_t finish=(start+size)&reader.Word(reader.Add(device,13480));
  auto pending=[&] { return NativeRingRangePending(start,finish,
    reader.Word(reader.Add(reader.Word(reader.Add(device,10768)),60))); };
  if(pending()) {
    begin();
    do { if(!poll()) break; } while(pending());
    end();
  }
  return finish;
}
}
