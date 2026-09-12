#pragma once
#include "native_submission_observers.h"

namespace edf::native {
// Direct submissions carry a host value, never a guest-stack descriptor.
// Busy worker lists still use the retained insertion service until their
// producers and consumer have migrated together.
template<class Reader,class Acquire,class Release,class Enqueue,class Submit>
uint64_t DispatchNativeSubmission(const Reader& reader,uint32_t device,uint64_t cursor,
    NativeSubmissionDescriptor descriptor,uint32_t increment,
    Acquire acquire,Release release,Enqueue enqueue,Submit submit) {
  const auto busy=reader.Add(device,10868);
  if(reader.Word(busy)) {
    const auto token=acquire();
    bool queued=false;
    try {
      if(reader.Word(busy)) {
        cursor=enqueue(cursor,descriptor);
        reader.StoreWord(busy,reader.Word(busy)+increment);
        queued=true;
      }
    } catch(...) { release(token); throw; }
    release(token);
    if(queued) return cursor;
  }
  reader.StoreWord(busy,reader.Word(busy)+increment);
  submit(descriptor);
  return cursor;
}
}
