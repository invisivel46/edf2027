// EDF2027 - native keyboard and mouse input: the pure logic.
//
// This is not pad emulation. Keys are written straight into the game's own named input
// channels (Dx::clInputDevice_XPad, polled by sub_821B09A0), after the pad poll and before
// the game derives held / pressed / repeat from them, so on-foot actions, vehicles, menus
// and pause all see them and a physical pad keeps working alongside. The mouse never
// becomes a stick: on foot it is added as an angle where the game integrates aim
// (sub_820DC890), which has no curve, clamp or deadzone of its own.
//
// Guest facts this file relies on (EDF 2017 default.xex, title 445007D3):
//   - Technical control type: yaw += in * k, pitch += in * k per 60 Hz tick, with
//     k = profile_sensitivity / weapon_zoom * 0.05 rad. Writing in = angle / k therefore
//     turns by exactly `angle`, and leaving the zoom term in k keeps scoped aim slower.
//   - The four configurable on-foot actions are bound through the controller profile
//     (words +12, +16, +20, +24 index a ten-name pad table), so they are resolved through
//     the player's live profile rather than assumed.
//
// Pure logic only - no SDK, ImGui or platform headers - so the unit tests can cover it.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "keybind_logic.h"

namespace edf::kbm {

// ---- bind strings ----------------------------------------------------------------------

inline constexpr uint8_t kModShift = 1u << 0;
inline constexpr uint8_t kModCtrl = 1u << 1;
inline constexpr uint8_t kModAlt = 1u << 2;

// Windows virtual-key codes of the generic modifiers, which is what key events carry.
inline constexpr uint16_t kVkShift = 0x10, kVkControl = 0x11, kVkAlt = 0x12;

using KeyState = std::array<bool, 256>;
// Key name -> virtual-key code, 0 when unknown. The SDK's ParseVirtualKey in the app, a
// table in the tests.
using KeyResolver = uint16_t (*)(std::string_view name);

struct BindAlternative {
  uint16_t key = 0;
  uint8_t modifiers = 0;
};

// Same grammar as the settings screen edits: comma-separated alternatives, "Mod+Mod+Key".
// Unknown key names are dropped rather than bound to something arbitrary.
inline std::vector<BindAlternative> ParseBind(std::string_view value, KeyResolver resolve) {
  std::vector<BindAlternative> out;
  if (!resolve) return out;
  for (const std::string& alternative : SplitBind(value)) {
    std::string_view token = alternative;
    BindAlternative parsed;
    for (;;) {
      const size_t plus = token.find('+');
      if (plus == std::string_view::npos || plus == 0) break;
      const std::string_view head = token.substr(0, plus);
      uint8_t bit = 0;
      if (head == "Shift") bit = kModShift;
      else if (head == "Ctrl" || head == "Control") bit = kModCtrl;
      else if (head == "Alt") bit = kModAlt;
      else break;
      parsed.modifiers |= bit;
      token.remove_prefix(plus + 1);
    }
    parsed.key = resolve(token);
    if (parsed.key != 0 && parsed.key < 256) out.push_back(parsed);
  }
  return out;
}

inline uint8_t LiveModifiers(const KeyState& keys) {
  return static_cast<uint8_t>((keys[kVkShift] ? kModShift : 0) | (keys[kVkControl] ? kModCtrl : 0) |
                              (keys[kVkAlt] ? kModAlt : 0));
}

// A bind needs the modifiers it names and ignores any others. The SDK's pad-emulation
// driver matched modifiers exactly, which made "W" go dead while Shift was held and made a
// bare "Ctrl" bind impossible to trigger; neither is acceptable for movement and fire.
inline bool BindPressed(const std::vector<BindAlternative>& bind, const KeyState& keys) {
  const uint8_t live = LiveModifiers(keys);
  for (const BindAlternative& alternative : bind)
    if (keys[alternative.key] && (alternative.modifiers & live) == alternative.modifiers) return true;
  return false;
}

// ---- actions ---------------------------------------------------------------------------

// Order matches kKeyActions in keybind_logic.h.
enum class Action : uint8_t {
  kMoveForward, kMoveBack, kStrafeLeft, kStrafeRight,
  kFire, kZoom, kJump, kNextWeapon,
  kVehicle, kPadX, kCancel, kLeftStickPress, kRightStickPress,
  kMenuUp, kMenuDown, kMenuLeft, kMenuRight,
  kStart, kBack,
  kReload,  // synthetic: presses no pad control; manual_reload.h, only with edf_manual_reload on
  kCount
};
inline constexpr size_t kActionCount = static_cast<size_t>(Action::kCount);
using ActionState = std::array<bool, kActionCount>;

// The game's pad-name table, in the order its binder (sub_820A0F70) indexes it with the
// profile words.
enum class PadControl : uint8_t {
  kA, kB, kX, kY, kLeftShoulder, kRightShoulder, kLeftTrigger, kRightTrigger, kLeftThumb, kRightThumb,
  kCount
};

// Profile rows of the four Technical on-foot actions, and what sub_820A0028 writes there.
struct TechnicalBindings {
  PadControl fire = PadControl::kRightTrigger;      // profile +12 -> control slot 4
  PadControl zoom = PadControl::kLeftShoulder;      // profile +16 -> control slot 6
  PadControl jump = PadControl::kLeftTrigger;       // profile +20 -> control slot 7
  PadControl next_weapon = PadControl::kRightShoulder;  // profile +24 -> control slot 8
};

// A profile word outside the table would index past it in the guest; keep the default.
inline PadControl ResolvePadControl(uint32_t profile_word, PadControl fallback) {
  return profile_word < static_cast<uint32_t>(PadControl::kCount) ? static_cast<PadControl>(profile_word)
                                                                 : fallback;
}

// What one poll writes into the pad device's channels. Axes are -1, 0 or +1; zero means
// "leave whatever the physical pad supplied".
struct ChannelFrame {
  int move_x = 0, move_y = 0;    // strafe right +, forward +
  int menu_x = 0, menu_y = 0;    // d-pad: right +, up +
  std::array<bool, static_cast<size_t>(PadControl::kCount)> pad{};
  bool start = false, back = false;
};

inline ChannelFrame BuildChannelFrame(const ActionState& actions, const TechnicalBindings& bindings) {
  const auto on = [&](Action action) { return actions[static_cast<size_t>(action)]; };
  const auto press = [](ChannelFrame& frame, PadControl control) { frame.pad[static_cast<size_t>(control)] = true; };
  ChannelFrame frame;
  frame.move_x = (on(Action::kStrafeRight) ? 1 : 0) - (on(Action::kStrafeLeft) ? 1 : 0);
  frame.move_y = (on(Action::kMoveForward) ? 1 : 0) - (on(Action::kMoveBack) ? 1 : 0);
  frame.menu_x = (on(Action::kMenuRight) ? 1 : 0) - (on(Action::kMenuLeft) ? 1 : 0);
  frame.menu_y = (on(Action::kMenuUp) ? 1 : 0) - (on(Action::kMenuDown) ? 1 : 0);
  if (on(Action::kFire)) press(frame, bindings.fire);
  if (on(Action::kZoom)) press(frame, bindings.zoom);
  if (on(Action::kNextWeapon)) press(frame, bindings.next_weapon);
  if (on(Action::kJump)) {
    press(frame, bindings.jump);
    // Menus confirm on A, and the Technical soldier never reads A, so one key does both.
    press(frame, PadControl::kA);
  }
  if (on(Action::kVehicle)) press(frame, PadControl::kY);
  if (on(Action::kPadX)) press(frame, PadControl::kX);
  if (on(Action::kCancel)) press(frame, PadControl::kB);
  if (on(Action::kLeftStickPress)) press(frame, PadControl::kLeftThumb);
  if (on(Action::kRightStickPress)) press(frame, PadControl::kRightThumb);
  frame.start = on(Action::kStart);
  frame.back = on(Action::kBack);
  return frame;
}

// A held key presses the control fully; otherwise the pad's value stands.
inline float MergeButton(float pad, bool key) { return key ? 1.0f : pad; }

// ---- mouse -----------------------------------------------------------------------------

// Degrees turned per mouse count at sensitivity 1.0, before the weapon's zoom divides it.
inline constexpr float kDegreesPerCount = 0.05f;
inline constexpr float kRadiansPerDegree = 0.0174532925f;
// The guest's per-tick aim factor (0x82003E94) and its default profile sensitivity.
inline constexpr float kGuestAimStep = 0.05f;
inline constexpr float kGuestDefaultSensitivity = 0.5f;

// Input value that makes the guest's integrator turn by `counts` worth of angle. The
// profile sensitivity is divided out so the in-game stick setting does not also scale the
// mouse; the zoom divisor is deliberately left to the guest.
inline float AimInputForCounts(float counts, float mouse_sensitivity, float profile_sensitivity) {
  if (!(profile_sensitivity > 0.0f) || !std::isfinite(profile_sensitivity))
    profile_sensitivity = kGuestDefaultSensitivity;
  if (!std::isfinite(counts) || !std::isfinite(mouse_sensitivity)) return 0.0f;
  const float radians = counts * mouse_sensitivity * kDegreesPerCount * kRadiansPerDegree;
  return radians / (profile_sensitivity * kGuestAimStep);
}

// Routes mouse motion between the two consumers. The pad is polled before anyone aims, so
// which one applies is judged from whether the on-foot aim update ran recently:
//   - on foot, motion accumulates until the aim update takes it, as an exact angle;
//   - otherwise (vehicles, whose aim code is not understood yet) it is offered to the
//     right stick as a rate, the only thing those classes read.
class MouseRouter {
 public:
  // Polls without an aim update before the mouse is treated as not-on-foot. Aim runs once
  // per 60 Hz tick and polls may outnumber ticks when the frame rate is unlocked.
  static constexpr uint32_t kOnFootGracePolls = 8;
  // Stick deflection per count per poll in the fallback.
  static constexpr float kStickPerCount = 0.02f;

  struct Stick { float x = 0.0f, y = 0.0f; };

  // dx right +, dy down + (screen convention). Returns the right-stick fallback.
  Stick OnPoll(float dx, float dy, float mouse_sensitivity) {
    if (polls_since_aim_ < kOnFootGracePolls) {
      ++polls_since_aim_;
      pending_x_ += dx;
      pending_y_ += dy;
      late_allowed_ = true;
      return {};
    }
    pending_x_ = pending_y_ = 0.0f;  // nothing on foot will ever collect these
    late_allowed_ = false;
    return {std::clamp(dx * mouse_sensitivity * kStickPerCount, -1.0f, 1.0f),
            std::clamp(-dy * mouse_sensitivity * kStickPerCount, -1.0f, 1.0f)};
  }

  // The on-foot aim update ran; hand it everything since the last one. With `late`
  // (edf_low_latency) it also takes the motion that arrived after the step's pad poll,
  // which the poll would otherwise leave for the next step, a whole tick later: `late` is
  // called only when that poll routed the mouse to the on-foot aim (every gate passed), and
  // returns the newest accumulated counts, which it drains from the shared input.
  template <class Late>
  void OnAim(float& dx, float& dy, Late&& late) {
    dx = pending_x_;
    dy = pending_y_;
    if (late_allowed_) {
      float late_x = 0.0f, late_y = 0.0f;
      late(late_x, late_y);
      dx += late_x;
      dy += late_y;
    }
    pending_x_ = pending_y_ = 0.0f;
    polls_since_aim_ = 0;
  }
  void OnAim(float& dx, float& dy) {
    OnAim(dx, dy, [](float&, float&) {});
  }
  // Whether the last poll handed the mouse to the on-foot aim (the late sample may run).
  bool late_allowed() const { return late_allowed_; }

  void Reset() {
    pending_x_ = pending_y_ = 0.0f;
    polls_since_aim_ = kOnFootGracePolls;
    late_allowed_ = false;
  }

 private:
  float pending_x_ = 0.0f, pending_y_ = 0.0f;
  uint32_t polls_since_aim_ = kOnFootGracePolls;
  bool late_allowed_ = false;
};

// Diagnostic mouse sweep (edf_kbm_test_sweep): counts to emit for the time [from_ms, to_ms)
// of a 2 s cycle - right at `rate` counts per millisecond for 500 ms, still for 500 ms, left
// for 500 ms, still for 500 ms - so a latency trace has step changes of motion to follow
// with nobody at the mouse. Integral of the square wave, so any wake-up cadence emits the
// same total.
inline float SweepCounts(double from_ms, double to_ms, float rate) {
  const auto integral = [rate](double t) {
    const double cycle = std::floor(t / 2000.0);
    const double phase = t - cycle * 2000.0;
    // Each full cycle nets zero. Within one: +rate over [0,500), 0 over [500,1000),
    // -rate over [1000,1500), 0 over [1500,2000).
    double sum = 0.0;
    sum += std::min(phase, 500.0);
    if (phase > 1000.0) sum -= std::min(phase - 1000.0, 500.0);
    return sum * double(rate);
  };
  return to_ms > from_ms ? float(integral(to_ms) - integral(from_ms)) : 0.0f;
}

}  // namespace edf::kbm
