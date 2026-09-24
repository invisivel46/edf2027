// Effects: render-rate effect state stepped once per tick (8217C4A0 and the like) and the map-effect list walk.
// Moved from guest_shader_bridge.cpp unchanged (stage 3 of its split); what this file shares with the bridge
// and the other hook files is in bridge/bridge_shared.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../bridge/bridge_shared.h"
#include "../../guest_shader_bridge.h"
#include "../../../pause_menu.h"
#include "../../native_scene_sources.h"
#include "../../native_scene_cpu_window.h"
#include "../../guest_sdk_readable_range.h"
#include "../../native_full_frame.h"
#include "../../native_full_frame_static_world.h"
#include "../../native_full_frame_effects.h"
#include "../../native_map_effects.h"
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
// Unvalidated big-endian words at the same host address REX_LOAD_U32 and
// REX_STORE_U32 use, so the native walk faults exactly where the guest would.
struct NativeRawGuestWords {
  uint8_t* base;
  static uint32_t Add(uint32_t address,uint32_t offset) { return address+offset; }
  uint8_t* Host(uint32_t address) const { return base+address+(address>=0xE0000000u?0x1000u:0u); }
  uint32_t Word(uint32_t address) const { return edf::native::GuestBlockWord(Host(address)); }
  void StoreWord(uint32_t address,uint32_t value) const {
    auto* p=Host(address); for(uint32_t i=0;i<4;++i) p[i]=uint8_t(value>>(24-i*8));
  }
};
void RecordNativeMapEffectCensus(uint8_t* base,uint32_t list) {
  constexpr uint64_t period=600;
  static std::mutex mutex;
  static edf::native::NativeMapEffectCensus census;
  static uint64_t first_frame=0,failures=0;
  const auto frame=native_render_frames.load(std::memory_order_relaxed);
  std::lock_guard lock(mutex);
  if(!census.walks()) first_frame=frame;
  try {
    const edf::native::GuestReader reader(base);
    const edf::native::NativeSceneCpuWindow window(reader);
    census.Record(window,list);
  } catch(const std::exception& error) {
    if(failures++<8) REXLOG_ERROR("Native map-effect census: walk failed list={:#x}: {}",list,error.what());
  }
  const auto frames=frame-first_frame;
  if(frames<period) return;
  REXLOG_INFO("Native map-effect census: frames={} walks={} objects={} per_frame={:.1f} classes={} truncated={} failures={}",
    frames,census.walks(),census.objects(),double(census.objects())/double(frames),census.classes(),census.truncated(),failures);
  uint32_t rank=0;
  for(const auto& row:census.Top(16)) {
    const auto* name=edf::native::NativeMapEffectClassName(row.key.vtable);
    REXLOG_INFO("Native map-effect census: #{} vtable={:#x} class={} mode={} render={:#x} count={} share={:.2f}% hidden={}",
      ++rank,row.key.vtable,name?name:"?",row.key.mode,row.key.render,row.count,row.percent,row.hidden);
  }
  census.Reset();
}
}
REX_EXTERN(__imp__sub_820B35A0);
REX_EXTERN(sub_821C0C00);
// clMapEffectManager slot 2 (sub_820B3610 tail-calls with r4=manager+48,
// r5=context). The native walk replaces only the loop: same frame size and
// back chain, same object/context registers and return address per call, next
// link read after each callback, and nonvolatile/stack/lr state restored as
// __restgprlr_29 does. Volatile registers are left as the last callee left them.
REX_HOOK_RAW(sub_820B35A0) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderList);
  if(REXCVAR_GET(edf_native_map_effect_census)) RecordNativeMapEffectCensus(base,ctx.r4.u32);
  // The A/B guest side keeps the original loop for the whole helper call.
  if(!REXCVAR_GET(edf_native_map_effect_list) || !edf::native::NativeAbNativeSide()) { __imp__sub_820B35A0(ctx,base); return; }
  const NativeRawGuestWords words{base};
  auto work=ctx;
  const auto stack=work.r1.u32-112;
  words.StoreWord(stack,work.r1.u32); work.r1.u64=stack;
  const auto context=ctx.r5.u64;
  edf::native::WalkNativeMapEffects(words,ctx.r4.u32,[&](uint32_t object) {
    work.r4.u64=context; work.r3.u64=object; work.lr=0x820B35E4;
    // Single per-object call site. The mode-1/2 bucket port
    // (native_bucket_dispatch.h) takes over here; everything else stays guest.
    sub_821C0C00(work,base);
  });
  work.r11.u64=0; work.cr6.compare<uint32_t>(work.r11.u32,0,work.xer);
  work.r1=ctx.r1; work.lr=ctx.lr; work.r29=ctx.r29; work.r30=ctx.r30; work.r31=ctx.r31;
  ctx=work;
}
// clEffectEtc02::slot4 (vtable 0x82012C78): its first store decrements the
// +612 lifetime (8217C4B0..C0) that slot 3 (8217C3A8) kills the object on, so
// the lifetime is counted in draws. The guest helper draws it every render; on
// an unlocked render that dispatched no step (native_render_tick_frame false)
// the word is put back after the call, so it still counts once per tick as at
// the retail 60 Hz. Nothing else in the slot reads +612 (it goes on to read
// +544/+592..+640 and draw), so restoring after the call equals skipping the
// store. Every other render, and every call outside a render helper, runs the
// original alone. The full frame's effects pass does the same through
// CollectNativeEffects(commit) and never calls this slot.
// The helper runs on its own thread beside the step (821A6508: kick
// 821D58E8, step dispatch 821A4BA0, join 821D5800), so slot 3 (8217C3A8)
// can run while this draw does. That is safe:
// - The object stays allocated until after the join. Slot 3 at +612 <= 0
//   only marks it (821C0ED8: byte +36, halfword +212) and queues it
//   (821A6AA8); the queue is freed by 821A5A10 (slot 1 8217CE60, the
//   destructor), whose only callers run it after the join: 821A6508 at
//   821A663C after 821D5800 at 821A65F8, and 821A6158 after its own
//   821D5800. A new object at the same address is therefore constructed
//   after the join too.
// - Nothing else writes +612 while the helper runs: its only writers are
//   the constructor 8217C840 and this slot (every other stw to +612 in the
//   image is in another class's code); slot 3 only reads it.
// Even so the put-back is a compare-and-swap from the draw's value
// (NativeRenderStepOncePerTick), so a concurrent write would be kept.
// The HUD phase loop (owner+140..+144 x listener +16) runs every render, and
// all its phases are draws: the XUI clock (clXuiManager slot4 82173CD8, Sato
// phase 1) and timers (slot5 82173A58 -> 823F79F8, phase 3) already advance by
// elapsed time (mftb / the kernel millisecond tick), so they stay per render.
// Two draws advance a counter by a fixed step per call instead; on an unlocked
// render that dispatched no step (native_render_tick_frame false) the fields
// are put back, so they step once per tick as at the retail 60 Hz. Each is put
// back only when the call made exactly that one step, and atomically (a
// compare-and-swap from the value the draw stored), so any other writer (the
// tick-side arming or reset, which may run on the simulation thread during
// the render) is never undone.
// clGaugeRader::slot3 (Noguchi phase 0): while +296 != 0 and byte
// [8257C030]+2260 is clear, 821768A0..B0 store +296 - 1 (the damage shake's
// frames left, armed with 30 by slot2 82175FFC) and +300 * [r31+28] (the
// shake offset, decaying by -0.99). The draw reads them before the store.
REX_EXTERN(__imp__sub_82176708);
REX_HOOK_RAW(sub_82176708) {
  if(native_render_tick_frame) { __imp__sub_82176708(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  const auto gauge=ctx.r3.u32;
  edf::native::NativeRenderStepOncePerTick(reader,false,reader.Add(gauge,edf::native::kNativeRadarShakeFrames),0xFFFFFFFFu,
    std::array{reader.Add(gauge,edf::native::kNativeRadarShakeOffset)},[&] { __imp__sub_82176708(ctx,base); });
}
// sub_8218ED68, the window cursor highlight draw (via 8218EFA0 from the window
// slot 10 draws, Noguchi phase 2): 8218ED90..A8 store +68 + 1, a fade-in
// counter (alpha min(n,10) * 0.1) that the cursor move 8218F138 resets to 0.
REX_EXTERN(__imp__sub_8218ED68);
REX_HOOK_RAW(sub_8218ED68) {
  if(native_render_tick_frame) { __imp__sub_8218ED68(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  edf::native::NativeRenderStepOncePerTick(reader,false,reader.Add(ctx.r3.u32,edf::native::kNativeCursorFade),1u,
    std::array<uint32_t,0>{},[&] { __imp__sub_8218ED68(ctx,base); });
}
REX_EXTERN(__imp__sub_8217C4A0);
REX_HOOK_RAW(sub_8217C4A0) {
  if(native_render_tick_frame) { __imp__sub_8217C4A0(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  const bool held=edf::native::NativeRenderStepOncePerTick(reader,false,reader.Add(ctx.r3.u32,edf::native::kNativeEffectEtc02Lifetime),0xFFFFFFFFu,
    std::array<uint32_t,0>{},[&] { __imp__sub_8217C4A0(ctx,base); });
  if(held && native_shadow_guest) ++native_shadow_guest->lifetime_holds;
}
