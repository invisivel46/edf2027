#pragma once
#include <cstdint>

namespace edf::native {
template<class Reader,class Timebase>
void BeginNativeFenceRecord(const Reader& reader,uint32_t record,uint32_t device,
    uint32_t kind,uint32_t tls,Timebase timebase) {
  reader.StoreWord(record,device);
  reader.StoreWord(reader.Add(record,4),kind);
  const auto thread=reader.Word(reader.Add(tls,256));
  const auto completion=reader.Word(reader.Add(device,10768));
  const auto now=reader.Word(reader.Add(thread,88));
  const auto completed=reader.Word(completion);
  reader.StoreWord(reader.Add(record,12),now);
  reader.StoreWord(reader.Add(record,8),completed);
  reader.StoreWord(reader.Add(record,16),now);
  reader.StoreWord(reader.Add(record,20),uint32_t(timebase()));
}

template<class Reader,class Timebase,class Profile,class Accumulate>
void EndNativeFenceRecord(const Reader& reader,uint32_t record,uint32_t tls,
    Timebase timebase,Profile profile,Accumulate accumulate) {
  if(!reader.Word(reader.Add(record,4))) return;
  const auto now=uint32_t(timebase());
  const auto started=reader.Word(reader.Add(record,20));
  const auto kernel_started=reader.Word(reader.Add(record,16));
  const auto kind=reader.Word(reader.Add(record,4));
  const auto kernel_now=reader.Word(reader.Add(reader.Word(reader.Add(tls,256)),88));
  const uint64_t kernel_elapsed=uint64_t(kernel_now)-uint64_t(kernel_started);
  auto device=reader.Word(record);
  const uint32_t ticks=now-started;
  const auto total=reader.Add(device,kind==3?20032:20024);
  accumulate(total,ticks);
  device=reader.Word(record);
  const auto callback=reader.Word(reader.Add(device,13068));
  if(callback) profile(callback,device,reader.Word(reader.Add(record,4)),ticks,kernel_elapsed);
}

template<class Reader,class Timebase,class Profile>
void EndNativeFenceRecord(const Reader& reader,uint32_t record,uint32_t tls,
    Timebase timebase,Profile profile) {
  EndNativeFenceRecord(reader,record,tls,timebase,profile,[&](uint32_t total,uint32_t ticks) {
    reader.StoreDoubleWord(total,reader.DoubleWord(total)+ticks);
  });
}

// CPU progress policy from 82139688; completion publication happens before this
// call. The 5000 threshold remains in guest thread-clock units, not host ms.
template<class Reader,class Error>
bool PollNativeFenceProgress(const Reader& reader,uint32_t record,uint32_t tls,Error error) {
  const auto device=reader.Word(record);
  if(*reader.Bytes(reader.Add(device,10809),1)&2) return false;
  const auto thread=reader.Word(reader.Add(tls,256));
  const auto completion=reader.Word(reader.Add(device,10768));
  const auto previous=reader.Word(reader.Add(record,8));
  const auto now=reader.Word(reader.Add(thread,88));
  if(previous!=reader.Word(completion)) {
    // Preserve the second completion sample used by the original service.
    const auto latest=reader.Word(completion);
    reader.StoreWord(reader.Add(record,12),now);
    reader.StoreWord(reader.Add(record,8),latest);
  }
  // 821F9FC8 reads this thread identifier directly; no guest helper is needed.
  const auto current_thread=reader.Word(reader.Add(reader.Word(reader.Add(tls,256)),332));
  if(reader.Word(reader.Add(device,10760))==current_thread &&
     reader.Word(reader.Add(device,10864))) reader.StoreWord(reader.Add(record,12),now);
  if(uint32_t(now-reader.Word(reader.Add(record,12)))<5000) return true;
  error(device);
  return false;
}
}
