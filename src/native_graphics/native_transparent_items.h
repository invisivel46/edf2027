#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <vector>

namespace edf::native {
class NativeBackendRecorder;
// One mode-1/2 object of a frame's transparent pass, from any producer: models,
// map effects (clMapEffectManager) and effects (native_full_frame_effects.h).
//
// sub_821C0C00 files every such object with sub_821A3B80 into the low-byte
// bucket, and sub_821A3BA0 then re-files the low buckets 0..255 by high byte
// and draws the high buckets 255..1, each from its head. Both passes insert at
// the head, so the draw order is: key descending, and on an equal key the order
// the objects were filed in (the two reversals cancel). Bucket 0 is never
// drawn: a key below 256 does not draw at all.
//
// `order` is that filing sequence for the whole pass. Producers that share one
// pass draw from one counter, so an effect filed after a model with the same
// key draws after it, as in the guest.
struct NativeTransparentItem {
  uint16_t key=0;
  uint32_t order=0;
  std::function<void(NativeBackendRecorder&)> record;
};
inline constexpr uint16_t kNativeTransparentFirstDrawnKey=256;  // sub_821A3BA0: r29 counts 255..1
inline bool NativeTransparentKeyDrawn(uint16_t key) { return key>=kNativeTransparentFirstDrawnKey; }
inline bool NativeTransparentBefore(const NativeTransparentItem& a,const NativeTransparentItem& b) {
  return a.key!=b.key?a.key>b.key:a.order<b.order;
}
// Drops the undrawn keys and puts the rest in sub_821A3BA0's draw order.
inline void SortNativeTransparentItems(std::vector<NativeTransparentItem>& items) {
  std::erase_if(items,[](const NativeTransparentItem& item) { return !NativeTransparentKeyDrawn(item.key); });
  std::stable_sort(items.begin(),items.end(),NativeTransparentBefore);
}
// One sequence from several producers' lists, each in any order.
inline std::vector<NativeTransparentItem> MergeNativeTransparentItems(
    std::vector<std::vector<NativeTransparentItem>> sources) {
  std::vector<NativeTransparentItem> merged;
  size_t total=0;
  for(const auto& source:sources) total+=source.size();
  merged.reserve(total);
  for(auto& source:sources) std::move(source.begin(),source.end(),std::back_inserter(merged));
  SortNativeTransparentItems(merged);
  return merged;
}
inline void RecordNativeTransparentItems(const std::vector<NativeTransparentItem>& items,NativeBackendRecorder& recorder) {
  for(const auto& item:items) if(item.record) item.record(recorder);
}
}
