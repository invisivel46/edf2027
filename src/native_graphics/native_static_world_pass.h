#pragma once
#include "native_scene_adapter.h"
#include "native_queued_scene.h"
#include "native_material_render_state.h"
#include "native_material_sampler.h"
#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <vector>

namespace edf::native {
// Why a published static group ran through the guest group callback 821D96D8
// instead of the native world pass.
enum class NativeStaticWorldFallback : uint32_t {
  GuestQueue, Program, Scissor, Eligibility, PassState, Instance, Count
};
inline constexpr std::array<const char*,size_t(NativeStaticWorldFallback::Count)> kNativeStaticWorldFallbackNames{
  "guest_queue","program","scissor","eligibility","pass_state","instance"
};
struct NativeStaticWorldPassCounters {
  uint64_t passes=0,original=0,native_groups=0,empty_groups=0,instances=0,draws=0,batches=0,handoffs=0,replays=0;
  std::array<uint64_t,size_t(NativeStaticWorldFallback::Count)> fallbacks{};
};
// The published walk of owner+240, or null when the original 821C3BB8 must run:
// no publication, no order for this owner, queues materialized to the guest, or
// a native selection for a group outside the order (a stale order would leave
// it undrawn and the frame's queues non-empty).
inline std::shared_ptr<const NativeSceneGroupOrder> NativeStaticWorldOrder(
    const NativeScenePublication* publication,uint32_t owner,const NativeSceneQueues* queues) {
  if(!publication || !queues || !queues->enabled) return nullptr;
  const auto* found=publication->group_order.Find(owner);
  if(!found || !*found) return nullptr;
  std::vector<uint32_t> sorted((*found)->begin(),(*found)->end());
  std::ranges::sort(sorted);
  if(!queues->Within([&](uint32_t group) { return std::ranges::binary_search(sorted,group); })) return nullptr;
  return *found;
}
// Published order, one decision per group. native returns a fallback reason
// without having changed guest or queue state; the owed device state of every
// native group so far is handed off before any guest group and at pass end.
template<class Native,class Fallback,class Handoff>
void WalkNativeStaticWorld(std::span<const uint32_t> order,Native&& native,Fallback&& fallback,Handoff&& handoff) {
  for(const auto group:order) {
    const std::optional<NativeStaticWorldFallback> reason=native(group);
    if(!reason) continue;
    handoff(); fallback(group,*reason);
  }
  handoff();
}
// One native group owed to the guest device: its ordered state operations and
// the sampler slots its textures touch (bit per slot).
struct NativeStaticWorldHandoffGroup {
  std::span<const std::array<uint32_t,2>> states;
  uint32_t slots=0;
};
// Activations replayed at handoff, in walk order: the last group (shaders,
// constants, its textures) and the last group to touch each sampler slot
// (bound texture, descriptor words, retirement of the previous handle).
inline std::vector<size_t> NativeStaticWorldReplayPlan(std::span<const NativeStaticWorldHandoffGroup> groups) {
  std::vector<size_t> result;
  uint32_t seen=0;
  for(size_t i=groups.size();i--;) {
    if(i+1==groups.size() || (groups[i].slots&~seen)) result.push_back(i);
    seen|=groups[i].slots;
  }
  std::ranges::reverse(result);
  return result;
}
// Combined CPU mirror writes of every state operation in the run, as the
// sequential setters would leave them: last value per device offset, dirty
// masks OR'd, and each operation offset once (last occurrence) for publication.
struct NativeStaticWorldStateWrites {
  std::vector<std::pair<uint32_t,uint32_t>> words;
  uint64_t dirty16=0,dirty24=0;
  std::vector<uint32_t> operations;
  NativeMaterialRenderPass render;
};
inline NativeStaticWorldStateWrites NativeStaticWorldStateHandoff(NativeMaterialRenderPass pass,
    std::span<const NativeStaticWorldHandoffGroup> groups) {
  NativeStaticWorldStateWrites result;
  for(const auto& group:groups) for(const auto& [offset,value]:group.states) {
    const auto writes=NativeMaterialStateCpuWrites(pass,offset,value,0,0);
    if(!writes) throw std::runtime_error("native static world handoff has a scissor state callback");
    ApplyNativeMaterialState(pass,offset,value);
    for(const auto& [address,word]:*writes) {
      if(address==16) result.dirty16|=uint64_t(word)<<32;
      else if(address==20) result.dirty16|=word;
      else if(address==24) result.dirty24|=uint64_t(word)<<32;
      else if(address==28) result.dirty24|=word;
      else {
        const auto found=std::ranges::find(result.words,address,&std::pair<uint32_t,uint32_t>::first);
        if(found!=result.words.end()) found->second=word; else result.words.emplace_back(address,word);
      }
    }
    std::erase(result.operations,offset); result.operations.push_back(offset);
  }
  result.render=pass;
  return result;
}
// Leaves the device mirrors as the skipped guest groups would: replay the
// planned activations, then write the combined render words and dirty masks,
// then the combined sampler words of every touched slot. Sampler words outside
// the key (texture format/address) and bound handles come from the replayed
// last binder of that slot. Computed before any write, so a throw changes nothing.
template<class Reader,class Replay>
NativeStaticWorldStateWrites HandOffNativeStaticWorld(const Reader& reader,uint32_t device,
    const NativeMaterialRenderPass& start,const std::array<NativeMaterialSamplerPass,16>& samplers,
    std::span<const NativeStaticWorldHandoffGroup> groups,Replay&& replay) {
  auto writes=NativeStaticWorldStateHandoff(start,groups);
  for(const auto index:NativeStaticWorldReplayPlan(groups)) replay(index);
  for(const auto& [offset,value]:writes.words) reader.StoreWord(reader.Add(device,offset),value);
  const auto dirty=[&](uint32_t offset,uint64_t mask) {
    if(!mask) return;
    const auto at=reader.Add(device,offset);
    const auto value=((uint64_t(reader.Word(at))<<32)|reader.Word(reader.Add(at,4)))|mask;
    reader.StoreWord(at,uint32_t(value>>32)); reader.StoreWord(reader.Add(at,4),uint32_t(value));
  };
  dirty(16,writes.dirty16); dirty(24,writes.dirty24);
  uint32_t slots=0;
  for(const auto& group:groups) slots|=group.slots;
  for(uint32_t slot=0;slot<16;++slot) if(slots&(1u<<slot)) {
    const auto record=reader.Add(device,1024+slot*24);
    const auto filter=reader.Add(record,12);
    reader.StoreWord(filter,(reader.Word(filter)&0x7ffffu)|(samplers[slot].words[1]&0xfff80000u));
    reader.StoreWord(reader.Add(record,16),samplers[slot].words[2]);
  }
  return writes;
}
}
