#pragma once
#include <chrono>
#include <cstdint>

namespace edf::native {
struct NativeWaitProgress {
  using Clock=std::chrono::steady_clock;
  Clock::time_point last_progress=Clock::now();
  void Refresh(Clock::time_point now) { last_progress=now; }
  bool Expired(Clock::time_point now,std::chrono::milliseconds limit) const {
    return now-last_progress>=limit;
  }
};

// Native stall policy, not a conversion of the retail 5000 kernel-clock units.
// The owner still tests its specific fence/ring/worker completion predicate.
template<class Reader,class Error>
bool PollHostFenceProgress(const Reader& reader,uint32_t record,uint32_t tls,
    NativeWaitProgress& progress,NativeWaitProgress::Clock::time_point now,
    std::chrono::milliseconds limit,Error error) {
  const auto device=reader.Word(record);
  if(*reader.Bytes(reader.Add(device,10809),1)&2) { error(device,false); return false; }
  const auto completed=reader.Word(reader.Word(reader.Add(device,10768)));
  if(completed!=reader.Word(reader.Add(record,8))) {
    reader.StoreWord(reader.Add(record,8),completed);
    progress.Refresh(now);
  }
  const auto thread=reader.Word(reader.Add(reader.Word(reader.Add(tls,256)),332));
  if(reader.Word(reader.Add(device,10760))==thread && reader.Word(reader.Add(device,10864)))
    progress.Refresh(now);
  if(progress.Expired(now,limit)) { error(device,true); return false; }
  return true;
}
}
