// Diagnostics and audits: the worker-callback audit, audio output observation and the guest wait timings.
// Moved from guest_shader_bridge.cpp unchanged (stage 3 of its split); what this file shares with the bridge
// and the other hook files is in bridge/bridge_shared.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../bridge/bridge_shared.h"
#include "../../guest_shader_bridge.h"
#include "../../../pause_menu.h"
#include "../../guest_sdk_readable_range.h"
#include "../../guest_mesh_watch_audit.h"
#include "../../guest_audio_output_ranges.h"
#include "../../native_load_trace.h"
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

namespace {
void ObserveAudioOutputPages(uint8_t* base,uint32_t address,bool indirect,const char* phase) {
  if(!REXCVAR_GET(edf_native_mesh_watch_audit)) return;
  auto& state=edf::native::State();
  std::lock_guard lock(state.mutex);
  auto audit=state.mesh_watch_audit.lock();
  if(!audit) return;
  try {
    const edf::native::GuestReader reader(base);
    const auto owner=indirect ? reader.Word(address) : address;
    const auto ranges=edf::native::VisitGuestAudioOutputRanges(reader,owner,
      [&](uint32_t physical,uint32_t length) { audit->ExcludePhysical(physical,length); });
    static uint64_t observed=0;
    if(++observed<=3 || (observed&(observed-1))==0)
      REXLOG_INFO("Native mesh watch audio exclusion: observations={}, ranges={}, phase={} (conservative shadow-only pages)",observed,ranges,phase);
  } catch(const std::exception& error) {
    audit->Disable();
    REXLOG_ERROR("Native mesh watch audit disabled after audio exclusion failure: {}",error.what());
  }
}
}
// Capture registration inputs rather than reading callback fields after the
// call: another worker can replace them immediately. This is workload evidence,
// not an ownership proof, and never changes the guest registration or arguments.
REX_EXTERN(__imp__sub_8243A000);
REX_HOOK_RAW(sub_8243A000) {
  if(!REXCVAR_GET(edf_native_worker_callback_audit)) {
    __imp__sub_8243A000(ctx,base);
    return;
  }
  const uint32_t worker=ctx.r3.u32, mode=ctx.r4.u32;
  const uint32_t callback=ctx.r5.u32, context=ctx.r6.u32;
  const uint32_t return_address=static_cast<uint32_t>(ctx.lr);
  __imp__sub_8243A000(ctx,base);
  static std::atomic<uint64_t> registrations{0};
  const auto sequence=registrations.fetch_add(1,std::memory_order_relaxed)+1;
  if(sequence<=256) {
    REXLOG_INFO("Native worker callback registration: sequence={}, worker={:#x}, mode={}, callback={:#x}, context={:#x}, caller_return={:#x} (inputs; observed workload only)",
                sequence,worker,mode,callback,context,return_address);
  } else if(sequence==257) {
    REXLOG_WARN("Native worker callback audit truncated: registration limit 256 reached; later registrations are not logged");
  }
}
// Retain construction coverage for deferred contexts, and observe every pending
// and live output before 1F88 copies records / signals the decoder. No guest
// registers or original audio behavior are changed by these shadow hooks.
REX_EXTERN(__imp__sub_823C1108);
REX_HOOK_RAW(sub_823C1108) {
  const uint32_t output=ctx.r6.u32;
  __imp__sub_823C1108(ctx,base);
  if(!ctx.r3.u32) ObserveAudioOutputPages(base,output,true,"constructed");
}
REX_EXTERN(__imp__sub_823C1F88);
REX_HOOK_RAW(sub_823C1F88) {
  ObserveAudioOutputPages(base,ctx.r3.u32,false,"before_decode");
  __imp__sub_823C1F88(ctx,base);
}
// Every guest function that calls a kernel wait import (KeWaitForSingleObject,
// KeWaitForMultipleObjects, NtWaitForSingleObjectEx, KeDelayExecutionThread),
// found by scanning the generated code: guest.wait, and inside an engine
// region its split by function. Timing only; the original always runs.
// 8214E328/8214E400/8214EAD0: graphics events; 823C08C0/823C0988/823C0A38:
// semaphore and multi-object waits; 82132ED0: the worker event wait;
// 821FCA78: sleep; 8243B230-8243B2D8: single-event wrappers; 821FA450,
// 821FAC48, 821FBA30: file and title-notification waits.
#define EDF_GUEST_WAIT(address) \
  REX_EXTERN(__imp__sub_##address); \
  REX_HOOK_RAW(sub_##address) { \
    struct Function { \
      uint32_t previous=std::exchange(edf::native::native_guest_wait_function,0x##address##u); \
      ~Function() { edf::native::native_guest_wait_function=previous; } \
    } function; \
    edf::native::HookTiming timing(edf::native::HookPhase::GuestWait); \
    edf::native::LoadTraceScope trace(LoadTraceOn(),edf::native::LoadTraceKind::GuestWait); \
    __imp__sub_##address(ctx,base); \
  }
EDF_GUEST_WAIT(8214E328)
EDF_GUEST_WAIT(8214E400)
EDF_GUEST_WAIT(8214EAD0)
EDF_GUEST_WAIT(823C08C0)
EDF_GUEST_WAIT(823C0988)
EDF_GUEST_WAIT(823C0A38)
EDF_GUEST_WAIT(82132ED0)
EDF_GUEST_WAIT(821FCA78)
EDF_GUEST_WAIT(8243B230)
EDF_GUEST_WAIT(8243B248)
EDF_GUEST_WAIT(8243B260)
EDF_GUEST_WAIT(8243B278)
EDF_GUEST_WAIT(8243B290)
EDF_GUEST_WAIT(8243B2A8)
EDF_GUEST_WAIT(8243B2C0)
EDF_GUEST_WAIT(8243B2D8)
EDF_GUEST_WAIT(821FA450)
EDF_GUEST_WAIT(821FAC48)
EDF_GUEST_WAIT(821FBA30)
#undef EDF_GUEST_WAIT
