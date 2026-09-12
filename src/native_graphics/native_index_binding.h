#pragma once
#include <cstdint>
#include <stdexcept>

namespace edf::native {
enum class NativeRetirementPath { Empty, Fence, Unmasked, Deferred };
struct IgnoreNativeRetirement {
  void operator()(NativeRetirementPath,uint32_t,uint32_t,uint32_t) const {}
};
// Shared CPU lifetime effects of the stream and index setters.
template<class Reader,class Reserve,class LegacyTag,class Observe=IgnoreNativeRetirement>
void RetireNativeBoundResource(const Reader& reader,uint32_t device,uint32_t previous,
    Reserve reserve,LegacyTag legacy_tag,Observe observe={}) {
  if(previous) {
    const auto fence=reader.Word(reader.Add(device,10780));
    if(fence) {
      reader.StoreWord(reader.Add(previous,8),fence);
      observe(NativeRetirementPath::Fence,previous,fence,0);
    }
    else if(reader.Word(reader.Add(device,10784))&reader.Word(previous)) {
      const auto cursor_slot=reader.Add(device,13148),end_slot=reader.Add(device,13152);
      auto cursor=reader.Word(cursor_slot);
      if(cursor>=reader.Word(end_slot)) cursor=reserve();
      const auto end=reader.Word(end_slot);
      if(cursor%8 || cursor>end || end-cursor<8)
        throw std::runtime_error("invalid native resource retirement queue extent");
      const auto tag=(previous>>2)|(legacy_tag()&0x80000000u);
      reader.StoreDoubleWord(cursor,(uint64_t(tag)<<32)|0xffffffffu);
      reader.StoreWord(cursor_slot,reader.Add(cursor,8));
      observe(NativeRetirementPath::Deferred,previous,tag,cursor);
    } else {
      observe(NativeRetirementPath::Unmasked,previous,0,0);
    }
  } else {
    observe(NativeRetirementPath::Empty,0,0,0);
  }
}
// Native draw binding is published by the caller after lifetime effects succeed.
template<class Reader,class Reserve,class LegacyTag,class Observe=IgnoreNativeRetirement>
void SetNativeIndexResource(const Reader& reader,uint32_t device,uint32_t resource,
    Reserve reserve,LegacyTag legacy_tag,Observe observe={}) {
  const auto slot=reader.Add(device,12164);
  RetireNativeBoundResource(reader,device,reader.Word(slot),reserve,legacy_tag,observe);
  reader.StoreWord(slot,resource);
}
}
