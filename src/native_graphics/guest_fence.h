#pragma once
#include <cstdint>
#include <optional>

namespace edf::native {
// Unsigned distance comparison from retail 8213C928. Target zero is explicitly
// exempt. Compare within one device lifetime; reset invalidates old values.
constexpr bool GuestFencePending(uint32_t issued,uint32_t target,uint32_t completed) {
  return target!=0 && uint32_t(issued-target)<uint32_t(issued-completed);
}
enum class FenceWaitEntryAction { Complete, Recording, Submit, Wait };
// Recording is a distinct early return, never proof of GPU completion.
constexpr FenceWaitEntryAction EvaluateFenceWaitEntry(uint32_t issued,uint32_t target,
                                                     uint32_t completed,bool recording) {
  if(!GuestFencePending(issued,target,completed)) return FenceWaitEntryAction::Complete;
  if(target==issued) return recording?FenceWaitEntryAction::Recording:FenceWaitEntryAction::Submit;
  return FenceWaitEntryAction::Wait;
}
enum class NativeWaitResult { Complete, Pending, Unsubmitted, TimedOut, Cancelled };
// Native controller for the resource-wait boundary. Retained services currently
// own submission and timeout/accounting; a recording return is not completion.
template<class Reader,class Submit,class Begin,class Poll,class End>
void WaitNativeResourceFence(const Reader& reader,uint32_t device,uint32_t target,
    Submit submit,Begin begin,Poll poll,End end) {
  if(!target) return;
  auto pending=[&] {
    const auto completion=reader.Word(reader.Add(device,10768));
    const auto issued=reader.Word(reader.Add(device,10780));
    return GuestFencePending(issued,target,reader.Word(completion));
  };
  // Read order matches the retail entry, including the conditional recording
  // access. Do not prefetch recording state for already-complete fences.
  const auto completion=reader.Word(reader.Add(device,10768));
  const auto issued=reader.Word(reader.Add(device,10780));
  if(!GuestFencePending(issued,target,reader.Word(completion))) return;
  if(target==issued) {
    if(reader.Word(reader.Add(device,12944))) return;
    submit();
  }
  if(!pending()) return;
  begin();
  while(pending()) if(!poll()) break;
  end();
}

constexpr NativeWaitResult EvaluateNativeWait(uint32_t issued,uint32_t target,
    std::optional<uint32_t> submitted,std::optional<uint32_t> completed,
    bool cancelled,bool expired) {
  if(!target || (completed && !GuestFencePending(issued,target,*completed)))
    return NativeWaitResult::Complete;
  if(cancelled) return NativeWaitResult::Cancelled;
  if(!submitted || GuestFencePending(issued,target,*submitted)) return NativeWaitResult::Unsubmitted;
  if(expired) return NativeWaitResult::TimedOut;
  return NativeWaitResult::Pending;
}
}  // namespace edf::native
