// Guest-side input hook: overrides the game's XInputGetState wrapper
// (sub_8212EA20: r3=user, r4=X_INPUT_STATE*) so we can see exactly what the
// game polls/receives and adds mouse button bindings.
#include <chrono>
#include <algorithm>
#include <string>
#include <vector>
#include <thread>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/input/input.h>
#include <rex/types.h>
#include <SDL3/SDL.h>
#include "frame_stats.h"
#include "core_logic.h"
#include "keybind_logic.h"
using rex::be;
REXCVAR_DECLARE(bool, mnk_mode);
REXCVAR_DECLARE(std::string, edf_mouse_left);
REXCVAR_DECLARE(std::string, edf_mouse_right);
REXCVAR_DECLARE(std::string, edf_mouse_middle);
REXCVAR_DECLARE(bool, edf_trace_input);
REXCVAR_DECLARE(bool, edf_rumble);
REXCVAR_DECLARE(int32_t, edf_frame_pacer_spin_us);

namespace {
uint32_t g_calls = 0, g_last_rc = 0xFFFFFFFF;
uint16_t g_last_btn = 0xFFFF;
}  // namespace

REX_EXTERN(__imp__sub_8212EA20);

REX_HOOK_RAW(sub_8212EA20) {
  const uint32_t user = ctx.r3.u32, ptr = ctx.r4.u32;
  __imp__sub_8212EA20(ctx, base);
  uint32_t rc = ctx.r3.u32;
  if (!ptr || rc != 0) {
    if (REXCVAR_GET(edf_trace_input) && user == 0 && rc != g_last_rc)
      REXLOG_INFO("XInputGetState hook: user={} rc={:#x} state={:#x}", user, rc, ptr);
    g_last_rc = rc;
    return;
  }
  auto* st = reinterpret_cast<rex::input::X_INPUT_STATE*>(base + ptr);
  uint16_t btn = st->gamepad.buttons;
  ++g_calls;
  // Keyboard & mouse mode: the SDK driver has no mouse-button bindings, so fold them in here
  // using this port's own edf_mouse_* cvars (see keybind_logic.h for the action tokens).
  if (user == 0 && rc == 0 && REXCVAR_GET(mnk_mode)) {
    static SDL_MouseButtonFlags previous_mouse_buttons = 0;
    SDL_MouseButtonFlags mb = SDL_GetMouseState(nullptr, nullptr);
    auto apply = [st](SDL_MouseButtonFlags pressed, const std::string& action) {
      if (!pressed) return;
      const edf::MouseTarget& target = edf::MouseTargetAt(edf::MouseTargetIndex(action));
      if (target.right_trigger) st->gamepad.right_trigger = 0xFF;
      if (target.left_trigger) st->gamepad.left_trigger = 0xFF;
      if (target.button_mask) st->gamepad.buttons = (uint16_t)(st->gamepad.buttons | target.button_mask);
    };
    apply(mb & SDL_BUTTON_LMASK, REXCVAR_GET(edf_mouse_left));
    apply(mb & SDL_BUTTON_RMASK, REXCVAR_GET(edf_mouse_right));
    apply(mb & SDL_BUTTON_MMASK, REXCVAR_GET(edf_mouse_middle));
    constexpr SDL_MouseButtonFlags kMappedButtons =
        SDL_BUTTON_LMASK | SDL_BUTTON_RMASK | SDL_BUTTON_MMASK;
    if ((mb & kMappedButtons) != (previous_mouse_buttons & kMappedButtons))
      ++st->packet_number;
    previous_mouse_buttons = mb;
    btn = st->gamepad.buttons;
  }
  if (REXCVAR_GET(edf_trace_input) && user == 0 &&
      (rc != g_last_rc || btn != g_last_btn || (g_calls % 6000) == 0)) {
    REXLOG_INFO("XInputGetState hook: user={} rc={:#x} buttons={:#06x} packet={} (call {})", user, rc, btn,
                (uint32_t)st->packet_number, g_calls);
    g_last_rc = rc; g_last_btn = btn;
  }
}

// Gate the game's XInputSetState wrapper so the option applies equally to the
// SDL and XInput backends. Sending zero also stops an effect already in flight
// the next time the guest updates vibration.
REX_EXTERN(__imp__sub_8212EA30);
REX_HOOK_RAW(sub_8212EA30) {
  if (!REXCVAR_GET(edf_rumble) && ctx.r4.u32) {
    auto* vibration = reinterpret_cast<rex::input::X_INPUT_VIBRATION*>(base + ctx.r4.u32);
    vibration->left_motor_speed = 0;
    vibration->right_motor_speed = 0;
  }
  __imp__sub_8212EA30(ctx, base);
}

// ---- XAM sign-in / content / notification tracing (first 30 calls each) ----
#define TRACE_WRAPPER(sym, tag)                                                              \
  REX_EXTERN(__imp__##sym);                                                                  \
  REX_HOOK_RAW(sym) {                                                                        \
    static uint32_t n = 0;                                                                   \
    const uint32_t a3 = ctx.r3.u32, a4 = ctx.r4.u32, a5 = ctx.r5.u32, a6 = ctx.r6.u32,       \
                   a7 = ctx.r7.u32;                                                          \
    __imp__##sym(ctx, base);                                                                 \
    if (REXCVAR_GET(edf_trace_input) && n++ < 30)                                             \
      REXLOG_INFO(tag " (" #sym "): args={:#x},{:#x},{:#x},{:#x},{:#x} -> r3={:#x}", a3, a4, \
                  a5, a6, a7, ctx.r3.u32);                                                   \
  }

TRACE_WRAPPER(sub_8212EA50, "XamShowSigninUI")
TRACE_WRAPPER(sub_8212EA58, "XamShowDeviceSelectorUI")
TRACE_WRAPPER(sub_821FAF78, "SigninState wrapper")
TRACE_WRAPPER(sub_821FB068, "XamContentCreateEx wrapper")
TRACE_WRAPPER(sub_8212EA48, "XamNotifyCreateListener")
TRACE_WRAPPER(sub_823E4F88, "XamUserReadProfileSettings wrapper")

REX_EXTERN(__imp__sub_82178568);
REX_HOOK_RAW(sub_82178568) {  // XNotifyGetNext wrapper: log only when it returns non-zero
  static uint32_t n = 0;
  const uint32_t a3 = ctx.r3.u32, a4 = ctx.r4.u32, a5 = ctx.r5.u32, a6 = ctx.r6.u32;
  __imp__sub_82178568(ctx, base);
  if (REXCVAR_GET(edf_trace_input) && ctx.r3.u32 && n++ < 30) {
    uint32_t id = a5 ? (uint32_t)(*reinterpret_cast<be<uint32_t>*>(base + a5)) : 0u;
    REXLOG_INFO("XNotifyGetNext (sub_82178568): args={:#x},{:#x},{:#x},{:#x} -> r3={:#x} id={:#x}", a3, a4, a5, a6, ctx.r3.u32, id);
  }
}
REX_EXTERN(__imp__sub_820A6B10);
REX_HOOK_RAW(sub_820A6B10) {
  static uint32_t n = 0;
  const uint32_t a3 = ctx.r3.u32, a4 = ctx.r4.u32, a5 = ctx.r5.u32, a6 = ctx.r6.u32;
  __imp__sub_820A6B10(ctx, base);
  if (REXCVAR_GET(edf_trace_input) && ctx.r3.u32 && n++ < 30) {
    uint32_t id = a5 ? (uint32_t)(*reinterpret_cast<be<uint32_t>*>(base + a5)) : 0u;
    REXLOG_INFO("XNotifyGetNext (sub_820A6B10): args={:#x},{:#x},{:#x},{:#x} -> r3={:#x} id={:#x}", a3, a4, a5, a6, ctx.r3.u32, id);
  }
}

// ---- frame pacer + frame-time statistics on the game's VdSwap wrapper ----
// The game itself waits on the guest vblank (video_mode_refresh_rate), so the pacer only matters for caps
// below the refresh rate or when vsync is off. Sleep to ~1.5 ms before the deadline, then spin.
REXCVAR_DECLARE(int32_t, edf_fps_cap);
REXCVAR_DECLARE(bool, edf_frametime_log);
namespace edf {
FrameStats& CurrentFrameStats() { static FrameStats s; return s; }
}
REX_EXTERN(__imp__sub_82151460);
REX_HOOK_RAW(sub_82151460) {
  using clock = std::chrono::steady_clock;
  static auto t0 = clock::now();
  static auto last_frame = t0, last_report = t0;
  static clock::time_point next_frame{};
  static std::vector<double> frame_ms;
  static uint32_t frames = 0, total = 0;
  static int previous_cap = 0;
  __imp__sub_82151460(ctx, base);
  int cap = REXCVAR_GET(edf_fps_cap);
  if (cap > 0) {
    auto period = std::chrono::nanoseconds(edf::FramePeriodNanoseconds(cap));
    auto now = clock::now();
    if (cap != previous_cap || next_frame < now - period) next_frame = now;
    next_frame += period;
    const auto spin = std::chrono::microseconds(REXCVAR_GET(edf_frame_pacer_spin_us));
    auto sleep_until = next_frame - spin;
    if (sleep_until > now) std::this_thread::sleep_until(sleep_until);
    while (clock::now() < next_frame) std::this_thread::yield();
  }
  previous_cap = cap;
  auto now = clock::now();
  if (total != 0) frame_ms.push_back(std::chrono::duration<double, std::milli>(now - last_frame).count());
  last_frame = now; ++frames; ++total;
  double dt = std::chrono::duration<double>(now - last_report).count();
  if (dt >= 5.0) {
    auto& st = edf::CurrentFrameStats();
    const auto summary = edf::SummarizeFrameTimes(frame_ms);
    const double average_ms = summary.average_ms;
    const double minimum_ms = summary.minimum_ms;
    const double maximum_ms = summary.maximum_ms;
    const double low_1_percent_ms = summary.low_1_percent_ms;
    st.fps.store(frames / dt, std::memory_order_relaxed);
    st.average_ms.store(average_ms, std::memory_order_relaxed);
    st.minimum_ms.store(minimum_ms, std::memory_order_relaxed);
    st.maximum_ms.store(maximum_ms, std::memory_order_relaxed);
    st.low_1_percent_ms.store(low_1_percent_ms, std::memory_order_relaxed);
    st.total_frames.store(total, std::memory_order_relaxed);
    REXLOG_INFO("FPS: {:.1f} (frames {} over {:.1f} s, t={:.0f} s)", frames / dt, frames, dt, std::chrono::duration<double>(now - t0).count());
    if (REXCVAR_GET(edf_frametime_log))
      REXLOG_INFO("FRAMETIME: avg {:.2f} ms min {:.2f} max {:.2f} 1%-low {:.2f} ms (cap {})", average_ms, minimum_ms, maximum_ms, low_1_percent_ms, cap);
    frame_ms.clear(); frames = 0; last_report = now;
  }
}
