// Fences, submission and workers: completion records, fence waits, ring submission, the worker threads, device reset and defaults.
// Moved from guest_shader_bridge.cpp unchanged (stage 3 of its split); what this file shares with the bridge
// and the other hook files is in bridge/bridge_shared.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../bridge/bridge_shared.h"
#include "../../guest_shader_bridge.h"
#include "../../native_renderer_preset.h"
#include "../../../pause_menu.h"
#include "../../d3d12_backend.h"
#include "../../native_static_world_resolve.h"
#include "../../guest_sdk_readable_range.h"
#include "../../guest_fence.h"
#include "../../native_fence_poll.h"
#include "../../native_fence_records.h"
#include "../../native_pix_monitor.h"
#include "../../native_allocator_wait.h"
#include "../../native_submission_flush.h"
#include "../../native_descriptor_submit.h"
#include "../../native_submission_observers.h"
#include "../../native_submission_dispatch.h"
#include "../../native_ring_cursor.h"
#include "../../native_submission_cursors.h"
#include "../../d3d11_completion.h"
#include "../../d3d11_signals.h"
#include "../../bridge/native_cvars.h"
#include "../../bridge/bridge_state.h"
#include "../../bridge/bridge_helpers.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/ppc/func.h>
#include <rex/logging.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

REX_EXTERN(__imp__KeSetEvent);
namespace edf::native {
namespace {
std::unique_ptr<NativeSignalQueue> CreateSignalQueueLocked(Bridge& state) {
  auto& backend=EnsureSceneBackendLocked(state);
  if(backend.name()=="d3d12") return std::make_unique<NativeSignalQueue>(backend);
  return std::make_unique<NativeSignalQueue>(*state.device.Get(),*state.context.Get());
}
}
// Queries may finish while C410 is returning into C868, before the latter's
// busy increment. Defer dispatch across the entire enclosing CPU transaction.
class NativeSignalSubmissionScope {
 public:
  explicit NativeSignalSubmissionScope(uint32_t device):device_(device) {
    auto global=rex::thread::global_critical_region::AcquireDirect();
    auto& state=State();
    std::lock_guard lock(state.signal_delivery_mutex);
    state.signal_deliveries[device_].BeginSubmission();
  }
  ~NativeSignalSubmissionScope() {
    auto& state=State();
    std::lock_guard lock(state.signal_delivery_mutex);
    state.signal_deliveries.at(device_).EndSubmission();
  }
  NativeSignalSubmissionScope(const NativeSignalSubmissionScope&)=delete;
  NativeSignalSubmissionScope& operator=(const NativeSignalSubmissionScope&)=delete;
 private:
  uint32_t device_;
};
void PollNativeWorkerSignals(PPCContext& ctx,uint8_t* base,uint32_t device,bool allow_flush=false) {
  // Match interrupt-side serialization, without entering the Xbox GPU ISR or
  // borrowing/modifying a guest thread's PCR CPU/TLS state. Only the audited
  // EBA0 event contract is implemented here, not arbitrary guest callbacks.
  auto& state=State();
  bool needs_gpu_poll=true;
  {
    std::lock_guard delivery_lock(state.signal_delivery_mutex);
    const auto found=state.signal_deliveries.find(device);
    if(found!=state.signal_deliveries.end()) {
      if(found->second.submitting()) return;
      needs_gpu_poll=!found->second.pending();
    }
  }
  // An occupied CPU delivery slot needs no immediate-context access. Release
  // its lock before acquiring the renderer lock on the GPU polling path.
  if(needs_gpu_poll) {
    std::lock_guard lock(state.mutex);
    std::lock_guard delivery_lock(state.signal_delivery_mutex);
    auto& delivery=state.signal_deliveries[device];
    if(delivery.submitting()) return;
    if(!delivery.pending()) {
      const auto found=state.signal_queues.find(device);
      if(found==state.signal_queues.end()) return;
      const auto ready=found->second->PeekCompleted(1,allow_flush);
      if(ready.empty()) return;
      delivery.Enqueue(ready.front()); // Nonallocating handoff before retiring GPU ownership.
      found->second->AcknowledgeCompleted(1);
    }
  }
  // No renderer lock is held while acquiring the SDK lock or waking workers.
  auto global=rex::thread::global_critical_region::AcquireDirect();
  std::lock_guard delivery_lock(state.signal_delivery_mutex);
  auto& delivery=state.signal_deliveries.at(device);
  if(delivery.submitting() || !delivery.pending()) return;
  const GuestReader reader(base);
  // Keep ownership if the guest's single publication slot is occupied.
  if(reader.Word(reader.Add(device,10900))) return;
  if(reader.Word(reader.Word(0x82000720))!=device)
    throw std::runtime_error("native worker signal device no longer matches callback global");
  NativeSignal delivered{};
  const bool completed=delivery.Deliver([&](const NativeSignal& signal,uint32_t cpu) {
    if(signal.callback!=0x8214EBA0) throw std::runtime_error("unsupported native worker callback");
      // EBA0 publishes its command-list argument before waking the selected
      // worker. The worker, not this producer, executes jobs and retires busy.
      reader.StoreWord(reader.Add(device,10900),signal.argument);
      auto event_context=ctx;
      event_context.r3.u64=reader.Add(device,11228+cpu*56);
      event_context.r4.u64=1;
      event_context.r5.u64=0;
      __imp__KeSetEvent(event_context,base);
      delivered=signal;
  });
  if(completed) {
    static std::atomic<uint64_t> dispatched{0};
    const auto count=dispatched.fetch_add(1,std::memory_order_relaxed)+1;
    if(count<=8 || !(count&(count-1)))
      REXLOG_INFO("Native worker signal completed: count={}, device={:#x}, argument={:#x}, CPUs={:#x}, busy={}",
        count,device,delivered.argument,delivered.cpu_mask,reader.Word(reader.Add(device,10868)));
  }
}
}
REX_EXTERN(__imp__sub_8213D298);
REX_EXTERN(__imp__sub_8213C9F0);
REX_EXTERN(__imp__edf_native_worker_signal_cpu_tail);
REX_HOOK_RAW(sub_8213C9F0) {
  const auto device=ctx.r3.u32,begin=ctx.r4.u32,callback=ctx.r6.u32,argument=ctx.r7.u32,flags=ctx.r5.u32;
  // The port has no Xbox command consumer. Unknown callbacks must not silently
  // emit a GPU interrupt packet and leave their CPU waiter permanently pending.
  // The former 51248 caller is replaced at 512D8 by native swap pacing.
  if(EDF_NATIVE_FLAG(host) && callback!=0x8214EBA0) {
    REXLOG_ERROR("Unsupported native signal callback: caller={:#x}, callback={:#x}, device={:#x}, argument={:#x}; no Xbox packet fallback",
      uint32_t(ctx.lr),callback,device,argument);
    throw std::runtime_error("native signal callback has no CPU completion implementation");
  }
  if(EDF_NATIVE_FLAG(host) && REXCVAR_GET(edf_native_hook_timings)) {
    const auto caller=uint32_t(ctx.lr);
    const size_t bucket=caller==0x8213D288?0:caller==0x8214ED64?1:caller==0x821513F4?2:3;
    static std::array<std::atomic<uint64_t>,4> calls{};
    const auto count=calls[bucket].fetch_add(1,std::memory_order_relaxed)+1;
    if(count<=8 || !(count&(count-1))) {
      // Observe the CPU callback contract before replacing its GPU signal packet.
      // Never execute the callback here: the enclosing submit has not run yet.
      REXLOG_INFO("Native pending signal audit: calls={}, caller={:#x}, device={:#x}, flags={:#x}, callback={:#x}, argument={:#x}, guest_tls={:#x}",
        count,uint32_t(ctx.lr),ctx.r3.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,ctx.r13.u32);
    }
  }
  if(EDF_NATIVE_FLAG(host) && callback==0x8214EBA0)
    __imp__edf_native_worker_signal_cpu_tail(ctx,base);
  else __imp__sub_8213C9F0(ctx,base);
  if(EDF_NATIVE_FLAG(host) && callback==0x8214EBA0) {
    const uint32_t bytes=ctx.r3.u32-begin;
    const auto physical=edf::native::NativeSignalCommandAddress(begin+4);
    if(uint64_t(begin)+bytes+4>(uint64_t(1)<<32) || !bytes)
      throw std::runtime_error("invalid native worker signal packet span");
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    auto& queue=state.signal_queues[device];
    if(!queue) queue=edf::native::CreateSignalQueueLocked(state);
    std::lock_guard delivery_lock(state.signal_delivery_mutex);
    const auto delivery=state.signal_deliveries.find(device);
    if(queue->pending()+size_t(delivery!=state.signal_deliveries.end() && delivery->second.pending())>=4096)
      throw std::runtime_error("native signal capacity includes undelivered CPU work");
    try {
      queue->Capture(physical,bytes,{callback,argument,edf::native::NativeSignalCpuMask(flags)});
    } catch(const std::exception& error) {
      REXLOG_ERROR("Native signal capture failed: begin={:#x}, bytes={}, argument={:#x}, pending={}, unsubmitted={}: {}",
        physical,bytes,argument,queue->pending(),queue->unsubmitted(),error.what());
      throw;
    }
  }
}
REX_EXTERN(__imp__sub_8213C868);
REX_EXTERN(__imp__sub_8214E8E0);
REX_EXTERN(__imp__edf_native_worker_audit);
REX_EXTERN(__imp__edf_native_worker_commands_audit);
REX_HOOK_RAW(sub_8214E640) {
  __imp__edf_native_worker_commands_audit(ctx,base);
}
namespace {
struct NativeWorkerCommandTrace { uint32_t worker,cursor,word,continuation,list; };
thread_local std::array<NativeWorkerCommandTrace,32> native_worker_commands{};
thread_local uint64_t native_worker_command_count=0;
void DumpNativeWorkerCommands() {
  const auto first=native_worker_command_count>32?native_worker_command_count-32:0;
  for(auto i=first;i<native_worker_command_count;++i) {
    const auto& entry=native_worker_commands[i%32];
    REXLOG_ERROR("Native worker history: step={}, worker={:#x}, cursor={:#x}, word={:#x}, continuation={:#x}, list={:#x}",
      i,entry.worker,entry.cursor,entry.word,entry.continuation,entry.list);
  }
}
}
void edf_native_audit_worker_command(PPCContext& ctx,uint8_t* base,uint32_t worker,uint32_t cursor) {
  if(!REXCVAR_GET(edf_native_hook_timings)) return;
  const edf::native::GuestReader reader(base);
  if(cursor<0x10000) {
    DumpNativeWorkerCommands();
    REXLOG_ERROR("Native invalid worker cursor: LR={:#x}, worker={:#x}, cursor={:#x}, continuation={:#x}, list={:#x}",
      uint32_t(ctx.lr),worker,cursor,reader.Word(worker+80),reader.Word(worker+84));
    return; // Preserve the original failing read; diagnostics must not skip work.
  }
  const auto word=reader.Word(cursor);
  native_worker_commands[native_worker_command_count++%32]={worker,cursor,word,reader.Word(worker+80),reader.Word(worker+84)};
  // Count actual consumer visits, not static producer sites. Tiling commands
  // remain in retail bodies that native entry hooks may already bypass.
  const auto opcode=word>>24;
  const unsigned kind=!(word&0x80000000u)?0:
    word==0xc0000000u?14:opcode>=0x80 && opcode<=0x8c?1+opcode-0x80:15;
  static std::array<std::atomic<uint64_t>,16> visits{};
  const auto count=visits[kind].fetch_add(1,std::memory_order_relaxed)+1;
  if(count<=8 || !(count&(count-1)))
    REXLOG_INFO("Native worker command coverage: kind={}, visits={}, worker={:#x}, cursor={:#x}, word={:#x}",
      kind,count,worker,cursor,word);
}
REX_EXTERN(edf_native_null_worker_job) {
  DumpNativeWorkerCommands();
  const edf::native::GuestReader reader(base);
  const auto worker=ctx.r31.u32;
  REXLOG_ERROR("Native null worker job: LR={:#x}, worker={:#x}, callback={:#x}, job_argument={:#x}, index={}, count={}, data={:#x}, cursor={:#x}, continuation={:#x}, list={:#x}, published={:#x}",
    uint32_t(ctx.lr),worker,reader.Word(worker+16),reader.Word(worker+20),reader.Word(worker+24),reader.Word(worker+28),
    reader.Word(worker+32),reader.Word(worker+36),reader.Word(worker+80),reader.Word(worker+84),reader.Word(worker+88));
  for(const auto offset:{32u,36u,80u,84u}) {
    const auto address=reader.Word(worker+offset);
    if(!address) continue;
    for(uint32_t i=0;i<8;++i)
      REXLOG_ERROR("Native null worker memory: field={}, address={:#x}, word={:#x}",offset,address+i*4,reader.Word(reader.Add(address,i*4)));
  }
}
REX_HOOK_RAW(sub_8214E8E0) {
  const auto worker_arg=ctx.r3.u32;
  const bool audit=EDF_NATIVE_FLAG(host) && REXCVAR_GET(edf_native_hook_timings);
  static std::atomic<uint64_t> calls{0};
  const auto count=audit?calls.fetch_add(1,std::memory_order_relaxed)+1:0;
  const bool sample=count && (count<=8 || !(count&(count-1)));
  const edf::native::GuestReader reader(base);
  const auto worker=sample?reader.Word(worker_arg):0;
  if(sample)
    REXLOG_INFO("Native CPU worker entered: count={}, worker={:#x}, CPU={}, busy={}, nesting={}, argument={:#x}, continuation={:#x}",
      count,worker,reader.Word(worker_arg+4),reader.Word(worker+56),reader.Word(worker+48),reader.Word(worker+88),reader.Word(worker+80));
  if(EDF_NATIVE_FLAG(host)) __imp__edf_native_worker_audit(ctx,base);
  else __imp__sub_8214E8E0(ctx,base);
  if(EDF_NATIVE_FLAG(host)) {
    const auto actual_worker=reader.Word(worker_arg);
    edf::native::HookTiming service_timing(edf::native::HookPhase::WorkerService);
    const auto device=actual_worker-10812;
    // A worker may itself submit the query that resumes its saved continuation.
    // Service that native completion before it returns to an indefinite event
    // wait. Never execute the continuation here or change its busy counter.
    for(;;) {
      edf::native::PollNativeWorkerSignals(ctx,base,device,true);
      if(reader.Word(actual_worker+88)) break;
      bool in_flight=false;
      {
        auto& state=edf::native::State();
        std::lock_guard lock(state.mutex);
        const auto found=state.signal_queues.find(device);
        in_flight=found!=state.signal_queues.end() && found->second->pending()>found->second->unsubmitted();
        std::lock_guard delivery_lock(state.signal_delivery_mutex);
        const auto delivery=state.signal_deliveries.find(device);
        in_flight|=delivery!=state.signal_deliveries.end() && delivery->second.pending();
      }
      if(!in_flight) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  if(sample)
    REXLOG_INFO("Native CPU worker returned: count={}, worker={:#x}, busy={}, nesting={}, argument={:#x}",
      count,worker,reader.Word(worker+56),reader.Word(worker+48),reader.Word(worker+88));
}
namespace {
void SubmitOwnedNativeDescriptors(PPCContext& ctx,uint8_t* base,uint32_t device,
    std::span<const edf::native::NativeSubmissionDescriptor> descriptors);
}
REX_EXTERN(__imp__KfAcquireSpinLock);
REX_EXTERN(__imp__KfReleaseSpinLock);
REX_EXTERN(sub_8213C018);
REX_HOOK_RAW(sub_8213C868) {
  const auto device=ctx.r3.u32;
  if(EDF_NATIVE_FLAG(host) && REXCVAR_GET(edf_native_hook_timings)) {
    const auto caller=uint32_t(ctx.lr);
    const size_t path=caller==0x8213CEF8?0:caller==0x8213CFD8?1:caller==0x8214EDF8?2:caller==0x8214EE44?3:4;
    static std::array<std::atomic<uint64_t>,10> calls{};
    const auto count=calls[path*2+unsigned(ctx.r7.u32!=0)].fetch_add(1,std::memory_order_relaxed)+1;
    if(count<=8 || !(count&(count-1))) {
      try {
        const edf::native::GuestReader reader(base);
        const auto device=ctx.r3.u32;
        REXLOG_INFO("Native pending list audit: calls={}, caller={:#x}, device={:#x}, busy={}, increment={}, words={}, list={:#x}, recording={}, mode={:#x}",
          count,uint32_t(ctx.lr),device,reader.Word(reader.Add(device,10868)),ctx.r7.u32,ctx.r6.u32,
          ctx.r8.u32,reader.Word(reader.Add(device,12944)),reader.Bytes(reader.Add(device,10809),1)[0]);
      } catch(const std::exception& error) {
        REXLOG_WARN("Native pending list audit read failed: {}",error.what());
      }
    }
  }
  if(EDF_NATIVE_FLAG(host)) {
    {
      edf::native::NativeSignalSubmissionScope submission(device);
      const edf::native::GuestReader reader(base);
      const auto cursor=ctx.r4.u64,list=ctx.r8.u64;
      const auto increment=ctx.r7.u32;
      const edf::native::NativeSubmissionDescriptor descriptor{ctx.r6.u32,ctx.r5.u32};
      auto work=ctx;
      if(work.r1.u32<176) throw std::runtime_error("invalid native submission dispatch stack");
      work.r1.u64=work.r1.u32-176u;
      ctx.r3.u64=edf::native::DispatchNativeSubmission(reader,device,cursor,descriptor,increment,
        [&] {
          work.r3.u64=reader.Add(device,10872); work.lr=0x8213C8A8;
          __imp__KfAcquireSpinLock(work,base); return work.r3.u64;
        },
        [&](uint64_t token) {
          work.r3.u64=reader.Add(device,10872); work.r4.u64=token; work.lr=0x8213C8EC;
          __imp__KfReleaseSpinLock(work,base);
        },
        [&](uint64_t next,edf::native::NativeSubmissionDescriptor range) {
          work.r3.u64=list; work.r4.u64=next; work.r5.u64=range.words; work.r6.u64=range.address;
          work.lr=0x8213C8CC; sub_8213C018(work,base); return work.r3.u64;
        },
        [&](const edf::native::NativeSubmissionDescriptor& range) {
          work.r3.u64=device; work.lr=0x8213C918;
          SubmitOwnedNativeDescriptors(work,base,device,std::span(&range,1));
        });
    }
    // Enqueue is not submission: only the owned submission service arms ranges.
    edf::native::PollNativeWorkerSignals(ctx,base,device);
  } else {
    __imp__sub_8213C868(ctx,base);
  }
}
REX_HOOK_RAW(sub_8213BD90) {
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32,words=ctx.r5.u32;
  auto& state=edf::native::State();
  std::lock_guard order(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto snapshot=state.submission_cursors.Get(device);
  const auto cursor=snapshot.cursor,mask=snapshot.mask;
  const auto next=edf::native::AdvanceNativeRingCursor(cursor,words,mask);
  state.submission_cursors.Publish(device,snapshot,next);
  reader.StoreWord(reader.Add(device,10820),next);
  // The extracted native tail returned the omitted reservation's masked end,
  // which can differ from the repeated-mask cursor for non-contiguous masks.
  ctx.r3.u64=(cursor+words)&mask;
}
REX_HOOK_RAW(sub_8213C410) {
  {
    std::lock_guard command_order(edf::native::State().submissions);
    const auto device=ctx.r3.u32,descriptors=ctx.r4.u32,count=ctx.r5.u32;
    edf::native::NativeSignalSubmissionScope snapshot_scope(device);
    const edf::native::GuestReader reader(base);
    // C410 accepts CPU descriptors, not Xbox packets: [word count, address].
    // Snapshot before its observer callbacks can touch the descriptor storage.
    std::vector<edf::native::NativeSubmissionDescriptor> owned_descriptors;
    if(count>UINT32_MAX/8) throw std::runtime_error("native signal descriptor count overflow");
    owned_descriptors.reserve(count);
    for(uint32_t i=0;i<count;++i) {
      const auto descriptor=reader.Add(descriptors,i*8);
      const auto words=reader.Word(descriptor);
      if(words>UINT32_MAX/4) throw std::runtime_error("native signal submission range overflow");
      const auto address=reader.Word(reader.Add(descriptor,4));
      owned_descriptors.push_back({words,address});
    }
    SubmitOwnedNativeDescriptors(ctx,base,device,owned_descriptors);
  }
}
namespace {
void SubmitOwnedNativeDescriptors(PPCContext& ctx,uint8_t* base,uint32_t device,
    std::span<const edf::native::NativeSubmissionDescriptor> owned_descriptors) {
    std::lock_guard command_order(edf::native::State().submissions);
    edf::native::NativeSignalSubmissionScope submission(device);
    const edf::native::GuestReader reader(base);
    for(const auto& range:owned_descriptors)
      if(range.words>UINT32_MAX/4) throw std::runtime_error("native signal submission range overflow");
    auto work=ctx;
    if(work.r1.u32<176) throw std::runtime_error("invalid native submission observer stack");
    work.r1.u64=work.r1.u32-176u;
    auto& state=edf::native::State();
    edf::native::NativeSubmissionCursors::Snapshot snapshot;
    { std::lock_guard lock(state.mutex); snapshot=state.submission_cursors.Get(device); }
    const edf::native::NativeSubmissionCursorAccess owned_reader(reader,device,snapshot,[&](uint32_t cursor) {
      std::lock_guard lock(state.mutex);
      state.submission_cursors.Publish(device,snapshot,cursor);
      reader.StoreWord(reader.Add(device,10820),cursor);
    });
    edf::native::SubmitNativeObservers(owned_reader,device,owned_descriptors,
      [&](uint32_t target,uint32_t slot,uint64_t address,uint32_t words,uint32_t kind,uint32_t caller) {
        const auto function=slot?reader.Word(reader.Add(reader.Word(target),slot)):target;
        work.r3.u64=slot?target:kind;
        if(slot!=28) {
          // Retail subtracts in 64 bits; preserve the negative observer address.
          work.r4.u64=kind==2?0:address;
          work.r5.u64=words; work.r6.u64=kind==2?0:1;
        }
        work.lr=caller; work.ctr.u64=function; work.last_indirect_target=function;
        rex::runtime::ResolveIndirectFunction(function)(work,base);
        // The submission gate is recursive: an observer can reset this device.
        // Reject before reading its replacement ABI state or invoking another observer.
        std::lock_guard lock(state.mutex);
        state.submission_cursors.Validate(device,snapshot);
      });
    ctx.r3=work.r3;
    std::lock_guard lock(state.mutex);
    // Special-mode submission need not publish a cursor. Validate every path
    // under the same lock as queue lookup, before arming any completion ranges.
    state.submission_cursors.Validate(device,snapshot);
    edf::native::SubmitSceneFrameLocked(state);
    const auto found=state.signal_queues.find(device);
    if(found!=state.signal_queues.end())
      for(const auto& range:owned_descriptors) if(range.words) found->second->SubmitRange(range.address,range.words*4);
    const auto fences=state.completion_queues.find(device);
    if(fences!=state.completion_queues.end())
      for(const auto& range:owned_descriptors) if(range.words) fences->second->SubmitRange(range.address,range.words*4);
    return;
}
}
REX_EXTERN(__imp__sub_8213C5F0);
REX_EXTERN(__imp__edf_native_cache_range_cpu_tail);
REX_EXTERN(__imp__edf_native_cache_reservation_cpu_tail);
REX_HOOK_RAW(sub_8213C5F0) {
  // CF60 skips only the cache packet enqueue when word count is zero; its
  // following CDC0 native fence submission and optional CPU wait still run.
  // ECD8 consumes the reserved address as fallback CPU storage: retain its
  // reservation/failure contract, but do not encode or enqueue cache packets.
  if(EDF_NATIVE_FLAG(host) && uint32_t(ctx.lr)==0x8213CF9C) {
    __imp__edf_native_cache_range_cpu_tail(ctx,base); return;
  }
  if(EDF_NATIVE_FLAG(host) && uint32_t(ctx.lr)==0x8214ED04) {
    __imp__edf_native_cache_reservation_cpu_tail(ctx,base); return;
  }
  if(EDF_NATIVE_FLAG(host)) {
    REXLOG_ERROR("Unsupported native cache-range caller={:#x}; no Xbox packet fallback",uint32_t(ctx.lr));
    throw std::runtime_error("native cache range caller has no CPU reservation contract");
  }
  __imp__sub_8213C5F0(ctx,base);
}
REX_EXTERN(__imp__edf_native_device_reset);
REX_EXTERN(__imp__sub_8213D1C8);
REX_EXTERN(__imp__edf_native_device_drain);
REX_HOOK_RAW(sub_8213D1C8) {
  if(EDF_NATIVE_FLAG(host)) {
    __imp__edf_native_device_drain(ctx,base); return;
  }
  __imp__sub_8213D1C8(ctx,base);
}
REX_EXTERN(edf_native_reset_completion_tracking) {
  // D298 has drained old work, but has not yet freed/replaced writeback storage.
  // Its saved r31 still holds the device. Do not touch guest registers/memory.
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto signals=state.signal_queues.find(ctx.r31.u32);
  std::lock_guard delivery_lock(state.signal_delivery_mutex);
  const auto delivery=state.signal_deliveries.find(ctx.r31.u32);
  if(delivery!=state.signal_deliveries.end() && delivery->second.pending())
    throw std::runtime_error("native reset attempted to discard undelivered worker signal");
  if(signals!=state.signal_queues.end() && signals->second->pending())
    throw std::runtime_error("native reset attempted to discard pending worker signals");
  state.signal_queues.erase(ctx.r31.u32);
  const auto fences=state.completion_queues.find(ctx.r31.u32);
  if(fences!=state.completion_queues.end() && fences->second->unsubmitted())
    throw std::runtime_error("native reset attempted to discard unsubmitted fences");
  state.completion_queues.erase(ctx.r31.u32);
  state.swap_clocks.erase(ctx.r31.u32);
  // D298 replaces command/completion storage, not the device's render words.
  // Keep their ownership; allocation/final Release delimit that lifetime.
  state.native_published_completions.erase(ctx.r31.u32);
  state.submission_cursors.Retire(ctx.r31.u32);
  state.completion_faults.erase(ctx.r31.u32);
  if(state.display_gamma_device==ctx.r31.u32) {
    state.display_gamma.reset(); state.display_gamma_device=0;
    if(state.presentation_frames) state.presentation_frames->Invalidate();
  }
  REXLOG_INFO("Native device reset: retired old completion tracking after drain, before storage replacement");
}
REX_EXTERN(edf_native_drain_worker_signals) {
  const auto device=ctx.r31.u32;
  for(;;) {
    edf::native::PollNativeWorkerSignals(ctx,base,device,true);
    size_t pending=0,unsubmitted=0;
    {
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      const auto found=state.signal_queues.find(device);
      if(found!=state.signal_queues.end()) {
        pending=found->second->pending(); unsubmitted=found->second->unsubmitted();
      }
      std::lock_guard delivery_lock(state.signal_delivery_mutex);
      const auto delivery=state.signal_deliveries.find(device);
      pending+=size_t(delivery!=state.signal_deliveries.end() && delivery->second.pending());
    }
    if(unsubmitted) throw std::runtime_error("native drain has unsubmitted worker signals");
    if(!pending) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const edf::native::GuestReader reader(base);
  if(reader.Word(reader.Add(device,10868))) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
REX_HOOK_RAW(sub_8213D298) {
  if(EDF_NATIVE_FLAG(host)) {
    const auto device=ctx.r3.u32,configuration=ctx.r4.u32;
    __imp__edf_native_device_reset(ctx,base);
    if(configuration && ctx.r3.u32==0) {
      auto& state=edf::native::State();
      std::lock_guard order(state.submissions);
      std::lock_guard lock(state.mutex);
      const edf::native::GuestReader reader(base);
      state.submission_cursors.Initialize(device,reader.Word(reader.Add(device,13480)));
    }
    return;
  }
  if(EDF_NATIVE_FLAG(shader_bridge) && REXCVAR_GET(edf_native_fence_probe)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    state.completion_queues.erase(ctx.r3.u32);
    state.native_published_completions.erase(ctx.r3.u32);
    state.completion_faults.erase(ctx.r3.u32);
  }
  __imp__sub_8213D298(ctx,base);
}
REX_EXTERN(__imp__edf_native_worker_init);
REX_HOOK_RAW(sub_8214EE50) {
  const edf::native::GuestReader reader(base);
  const auto flags=reader.Word(reader.Add(ctx.r3.u32,20416));
  __imp__edf_native_worker_init(ctx,base);
  REXLOG_INFO("Native worker initialization: retained CPU/thread setup; omitted Xbox setup packets; result={}, flags={:#x}, requested_mask={:#x}, skipped={}",
    ctx.r3.u32,flags,(flags>>24)&63,bool(flags&0x100));
}
REX_EXTERN(__imp__edf_native_device_defaults);
REX_HOOK_RAW(sub_8214EFF8) {
  const auto device=ctx.r3.u32;
  __imp__edf_native_device_defaults(ctx,base);
  edf::native::PublishNativeRenderState(base,device,0x8214eff8);
  REXLOG_INFO("Native device defaults: retained CPU descriptors/state; omitted Xbox setup packet tail");
}
namespace {
thread_local edf::native::NativeFenceRecords native_fence_records;
template<class Reader>
void AuditNativeFenceRecord(const Reader& reader,uint32_t record,
    uint32_t tls,uint32_t caller,unsigned phase) noexcept {
  if(!REXCVAR_GET(edf_native_retirement_audit)) return;
  static std::atomic<uint64_t> counts[3]{};
  const auto count=counts[phase].fetch_add(1,std::memory_order_relaxed)+1;
  if(count>8 && (count&(count-1))) return;
  try {
    const auto device=reader.Word(record);
    const auto now=reader.Word(reader.Add(reader.Word(reader.Add(tls,256)),88));
    REXLOG_INFO("Native fence record: phase={} count={} record={:08X} device={:08X} kind={} kernel_start={} kernel_now={} callback={:08X} caller={:08X}",
      phase,count,record,device,reader.Word(reader.Add(record,4)),reader.Word(reader.Add(record,16)),now,
      reader.Word(reader.Add(device,13068)),caller);
  } catch(const std::exception& error) {
    REXLOG_WARN("Native fence record audit failed: {}",error.what());
  }
}
}
REX_EXTERN(__imp__sub_821394D8);
REX_HOOK_RAW(sub_821394D8) {
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_821394D8(ctx,base); return; }
  const edf::native::GuestReader guest(base);
  edf::native::NativeFenceRecord owned{};
  const edf::native::NativeFenceRecordAccess reader(guest,ctx.r3.u32,owned);
  edf::native::BeginNativeFenceRecord(reader,ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r13.u32,
    [] { return rex::chrono::Clock::QueryGuestTickCount(); });
  native_fence_records.Publish(ctx.r13.u32,ctx.r3.u32,owned);
  AuditNativeFenceRecord(reader,ctx.r3.u32,ctx.r13.u32,uint32_t(ctx.lr),0);
}
edf::native::NativeBufferWrites::WriterScope edf_native_begin_fence_counter_write(uint8_t*,uint32_t);
void edf_native_complete_fence_counter_write(uint8_t*,uint32_t);
REX_EXTERN(__imp__sub_82139508);
REX_HOOK_RAW(sub_82139508) {
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_82139508(ctx,base); return; }
  const edf::native::GuestReader guest(base);
  const auto record=ctx.r3.u32;
  // Detach before accounting/callbacks, allowing reentrant reuse without a
  // dangling map reference. Exceptions cannot leave a completed record active.
  auto owned=native_fence_records.Take(ctx.r13.u32,record);
  const edf::native::NativeFenceRecordAccess reader(guest,record,owned);
  edf::native::EndNativeFenceRecord(reader,ctx.r3.u32,ctx.r13.u32,
    [] { return rex::chrono::Clock::QueryGuestTickCount(); },
    [&](uint32_t callback,uint32_t device,uint32_t kind,uint32_t ticks,uint64_t elapsed) {
      auto work=ctx;
      if(work.r1.u32<96) throw std::runtime_error("invalid native fence accounting stack");
      work.r1.u64=work.r1.u32-96u;
      work.r3.u64=0; work.r4.u64=kind; work.r6.u64=elapsed;
      PPCRegister scale{},units{};
      work.fpscr.disableFlushMode();
      scale.u32=reader.Word(reader.Add(device,20020)); units.u32=reader.Word(0x82002170);
      // Match the two separately rounded retail single-precision products.
      const float scaled=float(double(scale.f32)*double(float(ticks)));
      work.f1.f64=double(float(double(scaled)*double(units.f32)));
      work.lr=0x821395B8;
      work.ctr.u64=callback; work.last_indirect_target=callback;
      rex::runtime::ResolveIndirectFunction(callback)(work,base);
      ctx.r3=work.r3;
    },[&](uint32_t total,uint32_t ticks) {
      const auto writer=edf_native_begin_fence_counter_write(base,total);
      reader.StoreDoubleWord(total,reader.DoubleWord(total)+ticks);
      edf_native_complete_fence_counter_write(base,total);
    });
  AuditNativeFenceRecord(reader,record,ctx.r13.u32,uint32_t(ctx.lr),2);
}
// Native stall policy uses host steady time. Failure unwinds, never fabricates
// guest completion through the retail device-error handler.
REX_EXTERN(__imp__sub_82139688);
REX_HOOK_RAW(sub_82139688) {
  edf::native::HookTiming poll_timing(edf::native::HookPhase::CompletionPoll);
  const bool native=EDF_NATIVE_FLAG(host);
  if(native) {
    const auto record_token=ctx.r3.u32,tls=ctx.r13.u32;
    try {
    const edf::native::GuestReader guest(base);
    auto& owned=native_fence_records.Find(ctx.r13.u32,ctx.r3.u32);
    const edf::native::NativeFenceRecordAccess reader(guest,ctx.r3.u32,owned);
    const auto device=reader.Word(ctx.r3.u32);
    AuditNativeFenceRecord(reader,ctx.r3.u32,ctx.r13.u32,uint32_t(ctx.lr),1);
    // A queued fence may depend on a CPU worker that has not consumed its
    // completed wake signal yet. Keep both completion domains progressing.
    edf::native::PollNativeWorkerSignals(ctx,base,device,true);
    {
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      edf::native::PublishNativeCompletion(guest,state,device,true);
    }
    const auto limit=REXCVAR_GET(edf_native_wait_stall_ms);
    if(limit<1 || limit>600000) throw std::runtime_error("invalid native wait stall deadline");
    const bool again=edf::native::PollHostFenceProgress(reader,record_token,tls,owned.progress,
      edf::native::NativeWaitProgress::Clock::now(),std::chrono::milliseconds(limit),
      [&](uint32_t failed_device,bool stalled) {
        REXLOG_ERROR("Native wait failed: device={:08X} record={:08X} stalled={} limit_ms={}; no completion fabricated",
          failed_device,record_token,stalled,limit);
        throw std::runtime_error(stalled?"native GPU/worker wait made no progress":"native wait encountered device failure");
      });
    ctx.r3.u64=again?1:0;
    if(again) {
      edf::native::NativeFrameWaitTrace waiting(edf::native::FrameWaitKind::GuestFence);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return;
    } catch(...) {
      native_fence_records.Discard(tls,record_token);
      throw;
    }
  }
  __imp__sub_82139688(ctx,base);
}
// Observe the retained recording/direct-submission transitions independently
// of scene setup. No packet, queue, fence or scheduling behavior is changed.
REX_EXTERN(__imp__sub_8213CF60);
REX_EXTERN(sub_8213C5F0);
REX_EXTERN(sub_8213CDC0);
REX_EXTERN(sub_8213C928);
REX_HOOK_RAW(sub_8213CF60) {
  edf::native::HookTiming timing(edf::native::HookPhase::SubmissionFlush);
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_8213CF60(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32;
  auto work=ctx;
  if(work.r1.u32<112) throw std::runtime_error("invalid native submission flush stack");
  work.r1.u64=work.r1.u32-112u;
  ctx.r3.u64=edf::native::FlushNativeSubmission(reader,device,
    [&] {
      work.r3.u64=device; work.r4.u64=reader.Add(work.r1.u32,84); work.r5.u64=reader.Add(work.r1.u32,80);
      work.lr=0x8213CF9C; sub_8213C5F0(work,base);
      if(reader.Word(reader.Add(work.r1.u32,80))) throw std::runtime_error("native cache collection returned GPU packet words");
    },
    [&] { work.r3.u64=device; work.lr=0x8213CFE0; sub_8213CDC0(work,base); },
    [&] { return reader.Word(0x82578D08)!=0; },
    [&](uint32_t target) {
      work.r3.u64=device; work.r4.u64=target; work.r5.u64=0; work.r6.u64=0;
      work.lr=0x8213D020; sub_8213C928(work,base);
    });
}
REX_EXTERN(__imp__sub_8213CDC0);
REX_EXTERN(sub_82141500);
REX_EXTERN(sub_8213CB30);
REX_EXTERN(sub_8213C788);
REX_EXTERN(sub_8213C868);
REX_EXTERN(sub_8213CC20);
REX_HOOK_RAW(sub_8213CDC0) {
  edf::native::HookTiming timing(edf::native::HookPhase::DescriptorSubmit);
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_8213CDC0(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32;
  auto work=ctx;
  if(work.r1.u32<128) throw std::runtime_error("invalid native descriptor submission stack");
  work.r1.u64=work.r1.u32-128u;
  edf::native::SubmitNativeDescriptor(reader,device,
    [&] { work.r3.u64=device; work.lr=0x8213CE44; sub_82141500(work,base); return work.r3.u32; },
    [&] { work.r3.u64=reader.Add(device,13000); work.lr=0x8213CEA0; sub_8213CB30(work,base); return work.r3.u32; },
    [&](uint32_t next) {
      work.r3.u64=device; work.r4.u64=next; work.lr=0x8213CEC8;
      sub_8213C788(work,base); return work.r3.u32;
    },
    [&](uint32_t captured,uint32_t address,uint32_t words) {
      work.r3.u64=device; work.r4.u64=captured; work.r5.u64=address; work.r6.s64=int32_t(words);
      work.r7.u64=0; work.r8.u64=reader.Add(device,13000); work.lr=0x8213CEF8;
      sub_8213C868(work,base); return work.r3.u32;
    },
    [&] { work.r3.u64=device; work.r4.u64=0; work.lr=0x8213CF24; sub_8213CC20(work,base); });
  ctx.r3=work.r3;
}
REX_EXTERN(sub_821394D8);
REX_EXTERN(sub_82139688);
REX_EXTERN(sub_82139508);
namespace {
void RunNativeAllocatorWait(PPCContext& ctx,uint8_t* base,bool generation) {
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32,first=ctx.r4.u32,second=ctx.r5.u32;
  auto work=ctx;
  if(work.r1.u32<144) throw std::runtime_error("invalid native allocator wait stack");
  work.r1.u64=work.r1.u32-144u;
  const auto token=reader.Add(work.r1.u32,80);
  auto begin=[&] {
    work.r3.u64=token; work.r4.u64=device; work.r5.u64=generation?2:1;
    work.lr=generation?0x8213BC98:0x8213BD3C; sub_821394D8(work,base);
  };
  auto poll=[&] {
    work.r3.u64=token; work.lr=generation?0x8213BCA0:0x8213BD44;
    sub_82139688(work,base); return work.r3.u32!=0;
  };
  auto end=[&] {
    work.r3.u64=token; work.lr=generation?0x8213BCD8:0x8213BD80; sub_82139508(work,base);
  };
  if(generation) {
    edf::native::WaitNativeAllocationGeneration(reader,device,first,second,begin,poll,end);
    ctx.r3=work.r3;
  } else {
    ctx.r3.u64=edf::native::WaitNativeRingRange(reader,device,first,second,begin,poll,end);
  }
}
}
REX_EXTERN(__imp__sub_8213BC48);
REX_HOOK_RAW(sub_8213BC48) {
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_8213BC48(ctx,base); return; }
  RunNativeAllocatorWait(ctx,base,true);
}
REX_EXTERN(__imp__sub_8213BCE0);
REX_HOOK_RAW(sub_8213BCE0) {
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_8213BCE0(ctx,base); return; }
  RunNativeAllocatorWait(ctx,base,false);
}
REX_EXTERN(__imp__sub_8213C928);
REX_EXTERN(__imp__sub_8214E5B8);
REX_HOOK_RAW(sub_8214E5B8) {
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_8214E5B8(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  const auto device=reader.Word(reader.Word(0x82000720));
  auto work=ctx;
  if(work.r1.u32<144) throw std::runtime_error("invalid native worker wait stack");
  work.r1.u64=work.r1.u32-144u;
  const auto token=reader.Add(work.r1.u32,80);
  edf::native::WaitNativeWorkerSlots(reader,device,
    [&] { work.r3.u64=token; work.r4.u64=device; work.r5.u64=0;
      work.lr=0x8214E600; sub_821394D8(work,base); },
    [&] { work.r3.u64=token; work.lr=0x8214E608; sub_82139688(work,base); return work.r3.u32!=0; },
    [&] { work.r3.u64=token; work.lr=0x8214E628; sub_82139508(work,base); });
  ctx.r3=work.r3;
}
// Native mode owns wait control flow. Submission, wait-record accounting and
// error policy remain retained services; completion polling publishes native
// D3D results. Optional diagnostics below run only after this boundary returns.
REX_EXTERN(sub_8213CF60);
REX_EXTERN(sub_821394D8);
REX_EXTERN(sub_82139688);
REX_EXTERN(sub_82139508);
REX_HOOK_RAW(sub_8213C928) {
  edf::native::HookTiming wait_timing(edf::native::HookPhase::FenceWait);
  const auto device=ctx.r3.u32,target=ctx.r4.u32;
  if(EDF_NATIVE_FLAG(host)) {
    const edf::native::GuestReader reader(base);
    const auto wait_device=ctx.r3.u32,target=ctx.r4.u32,kind=ctx.r5.u32;
    auto work=ctx;
    // Preserve the caller frame for retained submission/error services. The
    // old record address is now an opaque key; its fields live in native storage.
    if(work.r1.u32<144) throw std::runtime_error("invalid native fence wait stack");
    work.r1.u64=work.r1.u32-144u;
    const auto record=reader.Add(work.r1.u32,80);
    edf::native::WaitNativeResourceFence(reader,wait_device,target,
      [&] { work.r3.u64=wait_device; work.lr=0x8213C97C; sub_8213CF60(work,base); },
      [&] { work.r3.u64=record; work.r4.u64=wait_device; work.r5.u64=kind;
        work.lr=0x8213C9A8; sub_821394D8(work,base); },
      [&] { work.r3.u64=record; work.lr=0x8213C9B4; sub_82139688(work,base); return work.r3.u32!=0; },
      [&] { work.r3.u64=record; work.lr=0x8213C9E8; sub_82139508(work,base); });
    ctx.r3=work.r3;
  } else {
    __imp__sub_8213C928(ctx,base);
  }
  wait_timing.Finish(); // Exclude optional post-wait diagnostic validation.
  if(!EDF_NATIVE_FLAG(shader_bridge) || !REXCVAR_GET(edf_native_fence_probe)) return;
  auto& state=edf::native::State();
  std::lock_guard lock(state.mutex);
  if(state.completion_faults.contains(device)) return;
  try {
    const edf::native::GuestReader reader(base);
    const auto issued=reader.Word(reader.Add(device,10780));
    const auto guest_completed=reader.Word(reader.Word(reader.Add(device,10768)));
    std::optional<uint32_t> native_completed;
    const auto queue=state.completion_queues.find(device);
    if(queue!=state.completion_queues.end() && queue->second) native_completed=queue->second->Poll();
    const bool native_pending=native_completed && edf::native::GuestFencePending(issued,target,*native_completed);
    ++state.completion_waits;
    if(native_pending) ++state.completion_waits_native_pending;
    if(state.completion_waits<=16 || state.completion_waits%1000==0 ||
       (native_pending && state.completion_waits_native_pending<=8))
      REXLOG_INFO("Native fence wait probe: device={:#x}, target={}, issued={}, guest_completed={}, guest_pending={}, native_known={}, native_completed={}, native_pending={}, waits={}, native_pending_waits={}; observational only",
        device,target,issued,guest_completed,edf::native::GuestFencePending(issued,target,guest_completed),
        native_completed.has_value(),native_completed.value_or(0),native_pending,state.completion_waits,
        state.completion_waits_native_pending);
    if(REXCVAR_GET(edf_native_validate_wait) && target) {
      if(queue==state.completion_queues.end() || !queue->second)
        throw std::runtime_error("native wait validation has no submitted event queue");
      const auto started=std::chrono::steady_clock::now();
      // A diagnostic deadline, not a guessed conversion of the retail tick
      // timeout. This does not replace guest error handling or its counters.
      const auto result=queue->second->WaitUntil(issued,target,started+std::chrono::seconds(2));
      if(result!=edf::native::NativeWaitResult::Complete)
        throw std::runtime_error(result==edf::native::NativeWaitResult::Unsubmitted ?
          "native wait validation target was not submitted" :
          result==edf::native::NativeWaitResult::TimedOut ? "native wait validation timed out" :
          "native wait validation cancelled");
      if(++state.completion_validated_waits<=8 || state.completion_validated_waits%1000==0)
        REXLOG_INFO("Native wait validated: device={:#x}, target={}, completed={}, elapsed_ms={}, count={}; guest counters unchanged",
          device,target,queue->second->completed().value_or(0),
          std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count(),
          state.completion_validated_waits);
    }
  } catch(const std::exception& error) {
    state.completion_faults.insert(device);
    REXLOG_ERROR("Native fence wait probe disabled for device {:#x}: {}",device,error.what());
  }
}
edf::native::NativeBufferWrites::WriterScope edf_native_begin_fence_counter_write(uint8_t* base,uint32_t total) {
  return BeginNativeBufferWrite(base,total,8,true);
}
void edf_native_complete_fence_counter_write(uint8_t* base,uint32_t total) {
  NotifyCompletedNativeBufferWrite(base,total,8);
}
edf::native::NativeBufferWrites::WriterScope edf_native_begin_counter_reset_write(uint8_t* base,uint32_t device) {
  return BeginNativeBufferWrite(base,edf::native::GuestReader(base).Add(device,20000),48,true);
}
void edf_native_complete_counter_reset_write(uint8_t* base,uint32_t device) {
  NotifyCompletedNativeBufferWrite(base,edf::native::GuestReader(base).Add(device,20000),48);
}
void edf_native_counter_store_word(uint8_t* base,uint32_t address,uint32_t value) {
  const auto writer=BeginNativeBufferWrite(base,address,4,true);
  edf::native::GuestReader(base).StoreWord(address,value);
  NotifyCompletedNativeBufferWrite(base,address,4);
}
REX_EXTERN(__imp__sub_82139228);
REX_EXTERN(__imp__sub_82138858);
REX_HOOK_RAW(sub_82138858) {
  if(EDF_NATIVE_FLAG(host)) {
    const edf::native::GuestReader reader(base);
    if(edf::native::RunNativePixIdle(ctx,reader,[&](uint32_t callback,auto& work) {
      rex::runtime::ResolveIndirectFunction(callback)(work,base);
    })) return;
  }
  __imp__sub_82138858(ctx,base);
}
REX_EXTERN(__imp__edf_native_counter_reset_cpu_tail);
REX_HOOK_RAW(sub_82139228) {
  if(EDF_NATIVE_FLAG(host)) __imp__edf_native_counter_reset_cpu_tail(ctx,base);
  else __imp__sub_82139228(ctx,base);
}
