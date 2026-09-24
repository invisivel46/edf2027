// Loading and the load trace: the map, mission and menu load scopes, the resource coordinator and helper, the loading screen presenter.
// Moved from guest_shader_bridge.cpp unchanged (stage 3 of its split); what this file shares with the bridge
// and the other hook files is in bridge/bridge_shared.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../bridge/bridge_shared.h"
#include "../../guest_shader_bridge.h"
#include "../../guest_sdk_readable_range.h"
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
// Observational resource-worker boundaries. The persistent coordinator/helper
// are not BeginLoading/EndLoading transactions; preserve their original calls.
class ScriptLoadTiming {
 public:
  ScriptLoadTiming(const char* api,uint32_t mode,uint32_t index,bool active=true)
      : api_(api),mode_(mode),index_(index),enabled_(active && REXCVAR_GET(edf_native_load_timings)) {
    if(enabled_) start_=std::chrono::steady_clock::now();
  }
  ~ScriptLoadTiming() {
    if(!enabled_) return;
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start_).count();
    if(ms>=1.0) REXLOG_INFO("Native script timing: api={} mode={} index={} total_ms={} (inclusive, calls below 1ms omitted)",
                            api_,mode_,index_,ms);
  }
 private:
  const char* api_;
  uint32_t mode_,index_;
  bool enabled_;
  std::chrono::steady_clock::time_point start_{};
};
thread_local uint32_t map_load_depth=0;
}
REX_EXTERN(__imp__sub_820CBD28);
REX_HOOK_RAW(sub_820CBD28) {
  struct Scope {
    Scope() { ++map_load_depth; }
    ~Scope() { --map_load_depth; }
  } scope;
  const uint32_t caller=uint32_t(ctx.lr);
  LoadTraceEvent("map_load_begin",caller);
  {
    edf::native::LoadTraceScope trace(LoadTraceOn(),edf::native::LoadTraceKind::MapLoad);
    __imp__sub_820CBD28(ctx,base);
  }
  LoadTraceEvent("map_load_end",caller);
}
// clMissionSequence::Begin (loads the mission's .bvm, .xPath and .Cam, then
// runs "main"): marks where the previous scene's teardown ends and the
// mission's own load starts. Observation only.
REX_EXTERN(__imp__sub_820CD448);
REX_HOOK_RAW(sub_820CD448) {
  const uint32_t caller=uint32_t(ctx.lr);
  LoadTraceEvent("mission_begin",caller);
  __imp__sub_820CD448(ctx,base);
  LoadTraceEvent("mission_begin_end",caller);
}
// Inclusive nested timings, restricted to this thread's LoadMap call chain.
// Address labels avoid assigning unverified semantic names to guest helpers.
#define EDF_MAP_TIMED_HOOK(address) \
  REX_EXTERN(__imp__sub_##address); \
  REX_HOOK_RAW(sub_##address) { \
    ScriptLoadTiming timing("map." #address,0,0,map_load_depth!=0); \
    __imp__sub_##address(ctx,base); \
  }
EDF_MAP_TIMED_HOOK(821B8090)
EDF_MAP_TIMED_HOOK(820BE628)
EDF_MAP_TIMED_HOOK(820B4D58)
EDF_MAP_TIMED_HOOK(820B3B00)
EDF_MAP_TIMED_HOOK(821A6158)
EDF_MAP_TIMED_HOOK(821B7278)
EDF_MAP_TIMED_HOOK(821B2850)
EDF_MAP_TIMED_HOOK(821ACBD0)
EDF_MAP_TIMED_HOOK(820ADD28)
EDF_MAP_TIMED_HOOK(821B24C0)
EDF_MAP_TIMED_HOOK(820B77A0)
EDF_MAP_TIMED_HOOK(820B6458)
EDF_MAP_TIMED_HOOK(820B6FA8)
EDF_MAP_TIMED_HOOK(821A6278)
EDF_MAP_TIMED_HOOK(820B4858)
EDF_MAP_TIMED_HOOK(821C7B20)
EDF_MAP_TIMED_HOOK(821C7D80)
EDF_MAP_TIMED_HOOK(821C7E30)
EDF_MAP_TIMED_HOOK(821ACAD8)
EDF_MAP_TIMED_HOOK(821AB708)
EDF_MAP_TIMED_HOOK(821D6448)
EDF_MAP_TIMED_HOOK(821D6AE0)
EDF_MAP_TIMED_HOOK(821D6C20)
EDF_MAP_TIMED_HOOK(821AA130)
EDF_MAP_TIMED_HOOK(821AAB70)
EDF_MAP_TIMED_HOOK(821B5568)
EDF_MAP_TIMED_HOOK(821AB520)
EDF_MAP_TIMED_HOOK(821CB6A0)
EDF_MAP_TIMED_HOOK(821CB550)
EDF_MAP_TIMED_HOOK(821D88C0)
EDF_MAP_TIMED_HOOK(821DB008)
EDF_MAP_TIMED_HOOK(821D85B8)
EDF_MAP_TIMED_HOOK(821D7C18)
EDF_MAP_TIMED_HOOK(821D7850)
EDF_MAP_TIMED_HOOK(821D79D8)
EDF_MAP_TIMED_HOOK(821D8770)
EDF_MAP_TIMED_HOOK(821B3C98)
#undef EDF_MAP_TIMED_HOOK
REX_EXTERN(__imp__sub_820C7220);
REX_HOOK_RAW(sub_820C7220) {
  ScriptLoadTiming timing("menu",ctx.r6.u32,ctx.r7.u32);
  __imp__sub_820C7220(ctx,base);
}
REX_EXTERN(__imp__sub_820D1518);
REX_HOOK_RAW(sub_820D1518) {
  ScriptLoadTiming timing("mission",ctx.r6.u32,ctx.r7.u32);
  if(!LoadTraceOn()) { __imp__sub_820D1518(ctx,base); return; }
  const auto native=(std::min)(size_t(ctx.r7.u32),NativeLoadTraceState::kNatives-1);
  const auto start=std::chrono::steady_clock::now();
  __imp__sub_820D1518(ctx,base);
  auto& state=LoadTraceState();
  state.native_calls[native].fetch_add(1,std::memory_order_relaxed);
  state.native_nanos[native].fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now()-start).count()),std::memory_order_relaxed);
}
REX_EXTERN(__imp__sub_821A4170);
REX_HOOK_RAW(sub_821A4170) {
  const bool trace=REXCVAR_GET(edf_native_load_timings);
  const uint32_t manager=ctx.r3.u32,mode=ctx.r4.u32;
  if(trace) REXLOG_INFO("Native load request: begin manager={:#x} mode={} caller={:#x}",manager,mode,ctx.lr);
  LoadTraceEvent(mode?"begin_loading_mission":"begin_loading_menu",uint32_t(ctx.lr),true);
  __imp__sub_821A4170(ctx,base);
  if(trace) REXLOG_INFO("Native load request: armed manager={:#x}",manager);
}
REX_EXTERN(__imp__sub_821A41E8);
REX_HOOK_RAW(sub_821A41E8) {
  const bool trace=REXCVAR_GET(edf_native_load_timings);
  const uint32_t manager=ctx.r3.u32;
  if(trace) REXLOG_INFO("Native load request: finish manager={:#x} caller={:#x}",manager,ctx.lr);
  LoadTraceEvent("end_loading",uint32_t(ctx.lr));
  __imp__sub_821A41E8(ctx,base);
  if(trace) REXLOG_INFO("Native load request: disarmed manager={:#x}",manager);
}
REX_EXTERN(__imp__sub_821A4FC0);
REX_HOOK_RAW(sub_821A4FC0) {
  const bool trace=REXCVAR_GET(edf_native_load_timings);
  const uint32_t manager=ctx.r3.u32;
  if(trace) REXLOG_INFO("Native resource worker: begin manager={:#x}",manager);
  if(trace) {
    // Snapshot identities only; do not invoke callbacks or replace the original
    // traversal. r4 owns the list, while r3 supplies the slot-6 argument.
    // The bounded snapshot is diagnostic, not an ownership certificate.
    try {
      const edf::native::GuestReader reader(base);
      const uint32_t list_owner=ctx.r4.u32;
      const uint32_t owner=reader.Word(reader.Add(list_owner,132));
      const uint32_t owner_vtable=reader.Word(owner);
      REXLOG_INFO("Native resource callbacks: manager={:#x} owner={:#x} enter={:#x} leave={:#x}",
        manager,owner,reader.Word(reader.Add(owner_vtable,32)),reader.Word(reader.Add(owner_vtable,36)));
      const uint32_t sentinel=reader.Word(reader.Add(list_owner,2232));
      uint32_t node=reader.Word(sentinel);
      uint32_t count=0;
      for(;node!=sentinel && count<256;++count) {
        const uint32_t object=reader.Word(reader.Add(node,12));
        const uint32_t vtable=reader.Word(object);
        REXLOG_INFO("Native resource callback: manager={:#x} index={} object={:#x} vtable={:#x} slot6={:#x}",
          manager,count,object,vtable,reader.Word(reader.Add(vtable,24)));
        node=reader.Word(node);
      }
      if(node!=sentinel) REXLOG_WARN("Native resource callback snapshot truncated at {} records",count);
    } catch(const std::exception& error) {
      REXLOG_WARN("Native resource callback snapshot unavailable: {}",error.what());
    }
  }
  edf::native::HookTiming timing(edf::native::HookPhase::ResourceOneShot);
  __imp__sub_821A4FC0(ctx,base);
  timing.Finish();
  if(trace) REXLOG_INFO("Native resource worker: end manager={:#x}",manager);
}
// The loading screen's presenter thread body (clSatoCallback::slot6): it
// loops drawing the loading screen while the resource manager's loading
// flags (+2261 actual, +2262 requested) are set and returns as soon as both
// are clear - the guest has no minimum display time here. Observation only.
REX_EXTERN(__imp__sub_8216EBC0);
REX_HOOK_RAW(sub_8216EBC0) {
  native_load_trace_presenter_thread=true;
  LoadTraceEvent("presenter_start",ctx.r4.u32,true);
  __imp__sub_8216EBC0(ctx,base);
  LoadTraceEvent("presenter_exit",0);
  native_load_trace_presenter_thread=false;
}
