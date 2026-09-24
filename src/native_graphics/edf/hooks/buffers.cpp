// Buffer writes: the copy, fill and read providers, the vertex/index buffer locks and unlocks, the cache flush.
// Moved from guest_shader_bridge.cpp unchanged (stage 3 of its split); what this file shares with the bridge
// and the other hook files is in bridge/bridge_shared.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../bridge/bridge_shared.h"
#include "../../guest_shader_bridge.h"
#include "../../native_renderer_preset.h"
#include "../../../pause_menu.h"
#include "../../native_scene_adapter.h"
#include "../../native_static_world_resolve.h"
#include "../../guest_draw_state.h"
#include "../../guest_sdk_readable_range.h"
#include "../../native_model_buffers.h"
#include "../../native_cache_flush.h"
#include "../../native_load_trace.h"
#include "../../native_buffer_write_frame.h"
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

REX_EXTERN(sub_821E8320);
REX_EXTERN(__imp__sub_821E8320);
// Called by the hash-gated native lock tail only after retained fence/range
// services return. The following suffix writes header +0, +0x14 or +0x18.
edf::native::NativeBufferWrites::WriterScope edf_native_begin_lock_header_write(uint8_t* base,uint32_t owner) {
  return BeginNativeBufferWrite(base,owner,28,true);
}
void edf_native_complete_lock_header_write(uint8_t* base,uint32_t owner) {
  NotifyCompletedNativeBufferWrite(base,owner,28);
}
// Optional generated-store endpoint (scalar, SIMD, atomic and inline zero).
// Uncovered SDK providers remain outside it; source comparisons stay enabled.
extern "C" void edf_native_observe_guest_scalar_store(uint8_t* base,uint32_t address,uint32_t bytes,const char* file,uint32_t line) {
  NotifyCompletedNativeBufferWrite(base,address,bytes,false,true,{file,line});
}
REX_HOOK_RAW(sub_821E8320) {
  const auto destination=ctx.r3.u32,bytes=ctx.r5.u32;
  const auto caller=uint32_t(ctx.lr);
  // Mark before entering the provider, including writes to not-yet-published
  // owners. Nesting and exceptional returns release the active-write count.
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,true);
  __imp__sub_821E8320(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,false,false,{nullptr,0,0x821e8320,caller});
}
// Off-thread dirty-range accumulation atomically updates one packed min/max
// pair, including the retry-path conditional store. No nested guest services.
REX_EXTERN(__imp__sub_8213BDF8);
REX_HOOK_RAW(sub_8213BDF8) {
  const auto destination=ctx.r3.u32;
  const auto writer=BeginNativeBufferWrite(base,destination,8,true);
  __imp__sub_8213BDF8(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,8);
}
// Independent forward-copy implementation (aligned word loop plus byte tails).
// It does not call 821E8320, and mutates r5 while copying the leading bytes.
// Capture the original extent and notify only after the guest copy completes.
REX_EXTERN(__imp__sub_821E8740);
REX_HOOK_RAW(sub_821E8740) {
  const auto destination=ctx.r3.u32,bytes=ctx.r5.u32;
  const auto caller=uint32_t(ctx.lr);
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,true);
  __imp__sub_821E8740(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,false,false,{nullptr,0,0x821e8740,caller});
}
// A320's backward memmove path writes inline and consumes r5. The forward
// branch tail-calls the already tracked 8320; equal addresses perform no write.
// Match the original signed address comparison when selecting the notification.
REX_EXTERN(__imp__sub_821EA320);
REX_HOOK_RAW(sub_821EA320) {
  const auto destination=ctx.r3.u32,bytes=ctx.r5.u32;
  const auto caller=uint32_t(ctx.lr);
  const bool backward=ctx.r3.s32>ctx.r4.s32;
  const auto writer=BeginNativeBufferWrite(base,destination,backward?bytes:0,true);
  __imp__sub_821EA320(ctx,base);
  if(backward) NotifyCompletedNativeBufferWrite(base,destination,bytes,false,false,{nullptr,0,0x821ea320,caller});
}
// Bulk fill writes its byte prefix, 16-byte blocks and tails inline. Save the
// original extent because alignment consumes r5 before the routine returns.
REX_EXTERN(__imp__sub_821E9BA0);
REX_HOOK_RAW(sub_821E9BA0) {
  const auto destination=ctx.r3.u32,bytes=ctx.r5.u32;
  const auto caller=uint32_t(ctx.lr);
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,true);
  __imp__sub_821E9BA0(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,false,false,{nullptr,0,0x821e9ba0,caller});
}
// The current SDK NtReadFile implementation completes XFile::Read synchronously
// before returning (even when reporting STATUS_PENDING for asynchronous handles).
// XFile bypasses virtual write protection for physical destinations and only
// emits its own callbacks on success. Notify the requested extent on all return
// statuses: failed/short reads must not leave a potentially changed native owner
// reusable. This is conservative and is not synchronization with a concurrent draw.
REX_EXTERN(__imp__NtReadFile);
REX_HOOK_RAW(edf_native_NtReadFile) {
  const auto destination=ctx.r8.u32,bytes=ctx.r9.u32,status=ctx.r7.u32;
  // SDK queues an APC only with a non-low-bit routine and nonnull context.
  // Without that path, audited guest outputs are data and the status block;
  // event/completion-port and memory-invalidation work changes host metadata.
  // APC-capable calls retain global exclusion for their guest queue writes.
  const bool exact=!((ctx.r5.u32&~1u) && ctx.r6.u32);
  const bool trace=LoadTraceOn();
  std::optional<edf::native::LoadTraceScope> notify_trace;
  if(trace) notify_trace.emplace(true,edf::native::LoadTraceKind::FileReadNotify);
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,exact,edf::native::NativeBufferWrites::WriterKind::FileRead);
  const auto status_writer=BeginNativeBufferWrite(base,status,status?8u:0u,exact,edf::native::NativeBufferWrites::WriterKind::FileRead);
  if(notify_trace) notify_trace->Finish();
  {
    edf::native::LoadTraceScope read_trace(trace,edf::native::LoadTraceKind::FileRead,bytes);
    __imp__NtReadFile(ctx,base);
  }
  if(trace) notify_trace.emplace(true,edf::native::LoadTraceKind::FileReadNotify);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,true);
  if(status) NotifyCompletedNativeBufferWrite(base,status,8,true);
}
// SDK bulk fill bypasses generated store instrumentation. Preserve the original
// provider, including floor(length/4) words and its register/return behavior.
REX_EXTERN(__imp__RtlFillMemoryUlong);
REX_HOOK_RAW(edf_native_RtlFillMemoryUlong) {
  const auto destination=ctx.r3.u32,bytes=ctx.r4.u32&~3u;
  // Audited SDK body only stores floor(length/4) words to this destination;
  // no secondary guest outputs or callbacks require global exclusion.
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,true,edf::native::NativeBufferWrites::WriterKind::WordFill);
  __imp__RtlFillMemoryUlong(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,true);
}
// Explicit VB/IB Unlock boundaries. Their callers finish CPU writes before
// entering these wrappers; preserve original coherency/wait behavior first.
// Other writers may bypass Lock/Unlock, so source comparisons remain required.
REX_EXTERN(__imp__sub_82134408);
REX_EXTERN(__imp__edf_native_buffer_lock_cpu_tail);
REX_HOOK_RAW(sub_82134408) {
  // AD70 is the audited texture subresource lock wrapper. Its access code is
  // 14; the extracted helper preserves parent fences, both dirty ranges and
  // the returned CPU alias, omitting only the Xbox cache packet block.
  const bool texture_lock=ctx.r4.u32==14 && uint32_t(ctx.lr)==0x8213ae24;
  if(EDF_NATIVE_FLAG(shader_bridge) && (ctx.r4.u32==10 || ctx.r4.u32==12 || texture_lock)) {
    __imp__edf_native_buffer_lock_cpu_tail(ctx,base);
    if(texture_lock) {
      static thread_local uint64_t completed=0;
      if(++completed<=3) REXLOG_INFO("Native texture lock: preserved CPU lock/fence state; omitted Xbox cache packets");
    }
  } else __imp__sub_82134408(ctx,base);
}
namespace {
void NotifyNativeBufferUpdate(uint8_t* base,uint32_t owner,bool index_buffer) {
  if(!EDF_NATIVE_FLAG(shader_bridge)) return;
  auto& state=edf::native::State();
  // Invalidate CPU ownership/cache snapshots under the registry lock. No GPU
  // commands are issued, so completed CPU writes need not wait for refresh.
  std::lock_guard lock(state.mutex);
  const bool tracked=state.model_buffers.NotifyUpdateAliases(owner,
    [&](uint32_t affected) { state.meshes.Invalidate(affected); });
  if(!tracked) {
    state.meshes.Invalidate(owner);
    // A generic (non-model) resource header can alias a published model's
    // payload. Owner-only invalidation misses that relationship. Queue its
    // physical extent through the same checked mapper as other CPU writers;
    // the next indexed consumer drains it before consulting retained storage.
    const edf::native::GuestReader reader(base);
    const auto extent=edf::native::DecodeNativeBufferUpdateExtent(
      reader.Word(reader.Add(owner,24)),reader.Word(reader.Add(owner,28)),index_buffer);
    NotifyCompletedNativeBufferWrite(base,extent.address,extent.bytes);
  }
  if(++state.buffer_update_notifications<=8)
    REXLOG_INFO("Native buffer update: owner={:#x}, model_tracked={}, notification={}",
      owner,tracked,state.buffer_update_notifications);
}
}
REX_EXTERN(__imp__sub_82141AB8);
REX_HOOK_RAW(sub_82141AB8) {
  if(EDF_NATIVE_FLAG(shader_bridge)) {
    edf::native::NativeCacheFlushCpu(ctx,edf::native::GuestReader(base));
    return;
  }
  __imp__sub_82141AB8(ctx,base);
}
REX_EXTERN(__imp__sub_821349B8);
REX_EXTERN(__imp__edf_native_unlock_821349B8);
REX_HOOK_RAW(sub_821349B8) {
  const auto owner=ctx.r3.u32;
  {
    // VB unlock passes r5=0 to 82134640, so its non-stack outputs are
    // header word 0 and conditional word +20, never word +24. Payload
    // copies have their own producer scopes; release this before state.mutex.
    const auto bytes=EDF_NATIVE_FLAG(shader_bridge)?24u:0u;
    const auto header_writer=BeginNativeBufferWrite(base,owner,bytes,true);
    if(bytes) __imp__edf_native_unlock_821349B8(ctx,base);
    else __imp__sub_821349B8(ctx,base);
    if(bytes) NotifyCompletedNativeBufferWrite(base,owner,bytes);
  }
  NotifyNativeBufferUpdate(base,owner,false);
}
REX_EXTERN(__imp__sub_82134AD8);
REX_EXTERN(__imp__edf_native_unlock_82134AD8);
REX_HOOK_RAW(sub_82134AD8) {
  const auto owner=ctx.r3.u32;
  const bool inline_indices=EDF_NATIVE_FLAG(shader_bridge) && uint32_t(ctx.lr)==0x8242D3DC;
  if(inline_indices) edf::native::NativeBufferWriteFrame::Current().RequireOwner(owner);
  {
    // Index unlock tail-calls 82134640 with r5=0: header word 0 and
    // conditional word +0x14 are its guest header outputs, not +0x18.
    // This header contract holds for every caller, independently of the
    // special twelve-byte inline payload and its physical mapping.
    const auto bytes=EDF_NATIVE_FLAG(shader_bridge)?24u:0u;
    const auto header_writer=BeginNativeBufferWrite(base,owner,bytes,true,
      edf::native::NativeBufferWrites::WriterKind::InlineIndices);
    if(bytes) __imp__edf_native_unlock_82134AD8(ctx,base);
    else __imp__sub_82134AD8(ctx,base);
    if(bytes) NotifyCompletedNativeBufferWrite(base,owner,bytes);
  }
  if(inline_indices) edf::native::NativeBufferWriteFrame::Current().Finish(owner,[&](uint32_t destination) {
    NotifyCompletedNativeBufferWrite(base,destination,12);
  });
  // No inline payload scope may remain active while this takes state.mutex.
  // The queued exact write already protects a draw that gets that lock first.
  NotifyNativeBufferUpdate(base,owner,true);
}
// Ghidra/assembly: this producer writes six uint16 indices at 8242D3B0..D3D0
// between the lock return D3A8 and unlock return D3DC. Do not generalize this
// extent to arbitrary index locks, saved aliases, or other producer callbacks.
REX_EXTERN(__imp__sub_8242D2B0);
REX_HOOK_RAW(sub_8242D2B0) {
  if(!EDF_NATIVE_FLAG(shader_bridge)) { __imp__sub_8242D2B0(ctx,base); return; }
  edf::native::NativeBufferWriteFrame frame;
  __imp__sub_8242D2B0(ctx,base);
  frame.RequireFinished();
}
REX_EXTERN(__imp__sub_82134A78);
REX_EXTERN(__imp__edf_native_lock_82134A78);
REX_EXTERN(__imp__sub_82134958);
REX_EXTERN(__imp__edf_native_lock_82134958);
REX_HOOK_RAW(sub_82134958) {
  if(EDF_NATIVE_FLAG(shader_bridge)) __imp__edf_native_lock_82134958(ctx,base);
  else __imp__sub_82134958(ctx,base);
}
REX_HOOK_RAW(sub_82134A78) {
  const auto owner=ctx.r3.u32;
  const bool inline_indices=EDF_NATIVE_FLAG(shader_bridge) && uint32_t(ctx.lr)==0x8242D3A8;
  if(EDF_NATIVE_FLAG(shader_bridge)) __imp__edf_native_lock_82134A78(ctx,base);
  else __imp__sub_82134A78(ctx,base);
  if(inline_indices) {
    // Fence callbacks and lock-entry writes have already returned. The six
    // payload stores follow; unlock header writes have an independent guard.
    std::optional<edf::native::NativeBufferWrites::Range> range;
    auto* queue=NativeBufferWriteQueue(base,ctx.r3.u32,12,&range);
    edf::native::NativeBufferWriteFrame::Current().Begin(owner,queue,ctx.r3.u32,range);
  }
}
