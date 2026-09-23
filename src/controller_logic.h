// EDF2027 - controller remapping, dead zones and synthetic pad actions: the pure logic.
//
// Everything a physical pad goes through before the game sees it, in the order the
// XInputGetState hook (input_hooks.cpp) applies it:
//
//   raw pad (the SDK's merged state for one guest user, SDL or XInput backend)
//     -> F1 menu chord and input gate (pause_menu.h). The chord is matched on the RAW
//        buttons, so remapping can never make the menu impossible to open, and the chord
//        stays the physical buttons the player sees in the menu.
//     -> dead zones (ApplyDeadzones): per-stick radial inner dead zone and a trigger
//        threshold, both rescaled so the usable range still reaches full deflection.
//        Applied the same way under both backends; the SDK applies none of its own.
//     -> remap (ApplyRemap): the player's table - buttons, triggers, stick swap and
//        per-axis inversion - producing the pad the game sees plus the synthetic actions.
//     -> the game. Native keyboard and mouse input is merged later, into the game's own
//        input channels (native_kbm.cpp), so a controller remap never changes the keys.
//
// ---- The remap table --------------------------------------------------------------
// Target-major, like the key bindings: one row per TARGET (a game control or a synthetic
// action), each holding the physical control that drives it (or none). The default table
// is the identity for game controls and unbound for synthetic actions, and serializes to
// an empty string. Assigning a physical control to a row takes it away from whichever row
// had it, which gets this row's previous source instead (AssignSource): swapping A and B
// is two presses, and the game controls always stay a permutation of the pad unless a
// row is cleared on purpose or a synthetic action takes a button.
//
// Triggers keep their analogue travel when they drive a trigger (LT <-> RT swapped still
// gives the game a 0..255 value). A digital button driving a trigger gives 0 or 255. A
// trigger driving a digital button (or a synthetic action) counts as pressed from
// kTriggerPressThreshold (XInput's own 30) on the dead-zoned value.
//
// ---- Synthetic actions: the extension point ------------------------------------------
// A remap row can also target an action the pad does not have, such as the manual
// "Reload" another feature adds. They are listed ONCE, in EDF_PAD_SYNTHETIC_ACTIONS
// below; the settings menu, the config grammar and the hook all read that list. To add
// one: append X(token, "Label") to the macro (the token becomes the config key and the
// SyntheticAction enumerator k<Token> must be added in the same order), then read it in
// the feature with edf::pad::SyntheticActionHeld(player, SyntheticAction::kX) - a level,
// updated on every guest pad poll of that player; edge-detect it on the consumer side.
// A physical control mapped to a synthetic action no longer drives the game control it
// used to (see AssignSource), so the button does one thing.
//
// Persistence: one string cvar per player (edf_pad_remap_p1, edf_pad_remap_p2), a
// comma-separated list of the rows that differ from the default - "target=source" with
// "none" for an unbound row - plus the flags swap_sticks and invert_lx/ly/rx/ry. Unknown
// tokens are ignored, so an older build reads a newer config without losing the rest.
//
// No SDK, ImGui or platform headers, so tests/unit_tests.cpp covers all of it.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "pause_menu.h"

namespace edf::pad {

using menu::PadSnapshot;

// ---- Controls ------------------------------------------------------------------------
// The sixteen physical controls a remap row can name: fourteen buttons and the two
// triggers. Order is the settings table's display order.
enum Control : int {
  kA, kB, kX, kY, kLB, kRB, kLT, kRT, kLS, kRS, kBack, kStart, kUp, kDown, kLeft, kRight,
  kControlCount
};
inline constexpr int kNoSource = -1;

struct ControlInfo {
  const char* token;  // config grammar; the chord names of pause_menu.h plus lt/rt
  const char* label;
  uint16_t bit;       // XInput button bit; 0 for the triggers
};
inline constexpr std::array<ControlInfo, kControlCount> kControls{{
    {"a", "A", menu::kPadA},
    {"b", "B", menu::kPadB},
    {"x", "X", menu::kPadX},
    {"y", "Y", menu::kPadY},
    {"lb", "LB (left bumper)", menu::kPadLeftShoulder},
    {"rb", "RB (right bumper)", menu::kPadRightShoulder},
    {"lt", "LT (left trigger)", 0},
    {"rt", "RT (right trigger)", 0},
    {"ls", "Left stick press", menu::kPadLeftThumb},
    {"rs", "Right stick press", menu::kPadRightThumb},
    {"back", "Back", menu::kPadBack},
    {"start", "Start", menu::kPadStart},
    {"up", "D-pad up", menu::kPadUp},
    {"down", "D-pad down", menu::kPadDown},
    {"left", "D-pad left", menu::kPadLeft},
    {"right", "D-pad right", menu::kPadRight},
}};
// Button bits the remap owns; any other bit (Guide 0x0400, the unused 0x0800) passes through.
inline constexpr uint16_t kRemappedBits = 0xF3FF;
// A trigger reads as a pressed button from this value on (XINPUT_GAMEPAD_TRIGGER_THRESHOLD).
inline constexpr uint8_t kTriggerPressThreshold = 30;

// ---- Synthetic actions ---------------------------------------------------------------
// X(token, label). See the header comment before adding one; keep SyntheticAction in step.
#define EDF_PAD_SYNTHETIC_ACTIONS(X) \
  X(reload, "Reload (manual reload)")

enum class SyntheticAction : int { kReload, kCount };
struct SyntheticInfo {
  const char* token;
  const char* label;
};
#define EDF_PAD_SYNTHETIC_ROW(token, label) {#token, label},
inline constexpr SyntheticInfo kSyntheticActions[] = {EDF_PAD_SYNTHETIC_ACTIONS(EDF_PAD_SYNTHETIC_ROW)};
#undef EDF_PAD_SYNTHETIC_ROW
inline constexpr int kSyntheticCount = int(std::size(kSyntheticActions));
static_assert(kSyntheticCount == int(SyntheticAction::kCount), "keep SyntheticAction in step with the list");
static_assert(kSyntheticCount <= 32, "synthetic actions are published as a 32-bit mask");

// Remap rows: the game controls first (same indices as Control), then the synthetic actions.
inline constexpr int kTargetCount = kControlCount + kSyntheticCount;
inline constexpr int TargetOf(SyntheticAction action) { return kControlCount + int(action); }
inline bool IsSyntheticTarget(int target) { return target >= kControlCount && target < kTargetCount; }
inline const char* TargetToken(int target) {
  if (target >= 0 && target < kControlCount) return kControls[size_t(target)].token;
  if (IsSyntheticTarget(target)) return kSyntheticActions[size_t(target - kControlCount)].token;
  return "";
}
inline const char* TargetLabel(int target) {
  if (target >= 0 && target < kControlCount) return kControls[size_t(target)].label;
  if (IsSyntheticTarget(target)) return kSyntheticActions[size_t(target - kControlCount)].label;
  return "";
}

// Players with a remap table of their own (guest users 0 and 1).
inline constexpr int kRemapPlayers = 2;
inline constexpr std::array<const char*, kRemapPlayers> kRemapCvars{"edf_pad_remap_p1", "edf_pad_remap_p2"};

// ---- Table ---------------------------------------------------------------------------
struct PadRemap {
  std::array<int8_t, kTargetCount> source{};
  bool swap_sticks = false;
  bool invert_lx = false, invert_ly = false, invert_rx = false, invert_ry = false;

  static PadRemap Default() {
    PadRemap remap;
    for (int t = 0; t < kTargetCount; ++t) remap.source[size_t(t)] = int8_t(t < kControlCount ? t : kNoSource);
    return remap;
  }
  static int DefaultSource(int target) { return target < kControlCount ? target : kNoSource; }
  bool operator==(const PadRemap&) const = default;
  bool IsDefault() const { return *this == Default(); }
};

// Physical control -> row: `source` now drives `target`. Every other row it drove gets
// `target`'s previous source (a swap between game controls; for a synthetic row usually
// "none", which frees the button from its old game control). kNoSource clears the row.
inline PadRemap AssignSource(PadRemap remap, int target, int source) {
  if (target < 0 || target >= kTargetCount) return remap;
  if (source < kNoSource || source >= kControlCount) return remap;
  const int8_t previous = remap.source[size_t(target)];
  if (source != kNoSource && source != previous)
    for (int t = 0; t < kTargetCount; ++t)
      if (t != target && remap.source[size_t(t)] == source) remap.source[size_t(t)] = previous;
  remap.source[size_t(target)] = int8_t(source);
  return remap;
}

// Physical controls that drive no row: pressing them does nothing in game.
inline uint32_t UnusedSources(const PadRemap& remap) {
  uint32_t used = 0;
  for (const auto s : remap.source)
    if (s != kNoSource) used |= 1u << unsigned(s);
  return ~used & ((1u << kControlCount) - 1);
}

// ---- Config grammar ------------------------------------------------------------------
inline int ControlFromToken(std::string_view token) {
  for (int c = 0; c < kControlCount; ++c)
    if (token == kControls[size_t(c)].token) return c;
  return kNoSource - 1;  // not a control
}
inline int TargetFromToken(std::string_view token) {
  for (int t = 0; t < kTargetCount; ++t)
    if (token == TargetToken(t)) return t;
  return -1;
}

inline std::string SerializeRemap(const PadRemap& remap) {
  std::string out;
  const auto add = [&](std::string_view token) {
    if (!out.empty()) out.push_back(',');
    out += token;
  };
  for (int t = 0; t < kTargetCount; ++t) {
    const int s = remap.source[size_t(t)];
    if (s == PadRemap::DefaultSource(t)) continue;
    add(std::string(TargetToken(t)) + "=" + (s == kNoSource ? "none" : kControls[size_t(s)].token));
  }
  if (remap.swap_sticks) add("swap_sticks");
  if (remap.invert_lx) add("invert_lx");
  if (remap.invert_ly) add("invert_ly");
  if (remap.invert_rx) add("invert_rx");
  if (remap.invert_ry) add("invert_ry");
  return out;
}

inline PadRemap ParseRemap(std::string_view text) {
  PadRemap remap = PadRemap::Default();
  while (!text.empty()) {
    const size_t comma = text.find(',');
    std::string token(text.substr(0, comma));
    text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
    token.erase(std::remove_if(token.begin(), token.end(), [](char c) { return c == ' ' || c == '\t'; }),
                token.end());
    for (auto& c : token) c = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (token.empty()) continue;
    if (token == "swap_sticks") { remap.swap_sticks = true; continue; }
    if (token == "invert_lx") { remap.invert_lx = true; continue; }
    if (token == "invert_ly") { remap.invert_ly = true; continue; }
    if (token == "invert_rx") { remap.invert_rx = true; continue; }
    if (token == "invert_ry") { remap.invert_ry = true; continue; }
    const size_t eq = token.find('=');
    if (eq == std::string::npos) continue;
    const int target = TargetFromToken(std::string_view(token).substr(0, eq));
    const std::string_view value = std::string_view(token).substr(eq + 1);
    const int source = value == "none" ? kNoSource : ControlFromToken(value);
    if (target < 0 || source < kNoSource) continue;  // unknown: keep the default row
    remap.source[size_t(target)] = int8_t(source);
  }
  return remap;
}

// ---- Dead zones ----------------------------------------------------------------------
// Fractions of full travel, 0 (off) to kMaxDeadzone. From the percent cvars
// edf_pad_left_deadzone, edf_pad_right_deadzone and edf_pad_trigger_threshold.
inline constexpr float kMaxDeadzone = 0.9f;
struct Deadzones {
  float left = 0.0f, right = 0.0f, trigger = 0.0f;
  static Deadzones FromPercent(int left, int right, int trigger) {
    const auto f = [](int percent) { return std::clamp(float(percent) / 100.0f, 0.0f, kMaxDeadzone); };
    return {f(left), f(right), f(trigger)};
  }
};

// Radial (round) inner dead zone, rescaled: inside it the stick is centred; outside it the
// distance past the edge is stretched back over the full range, direction unchanged, so
// the stick still reaches full deflection and small movements start from zero.
inline void ApplyStickDeadzone(int16_t& x, int16_t& y, float deadzone) {
  if (deadzone <= 0.0f) return;
  deadzone = std::min(deadzone, kMaxDeadzone);
  const float fx = std::max(float(x), -32767.0f) / 32767.0f, fy = std::max(float(y), -32767.0f) / 32767.0f;
  const float magnitude = std::sqrt(fx * fx + fy * fy);
  if (magnitude <= deadzone) { x = 0; y = 0; return; }
  const float scaled = std::min((magnitude - deadzone) / (1.0f - deadzone), 1.0f) / magnitude;
  x = int16_t(std::lround(std::clamp(fx * scaled, -1.0f, 1.0f) * 32767.0f));
  y = int16_t(std::lround(std::clamp(fy * scaled, -1.0f, 1.0f) * 32767.0f));
}
// Trigger threshold, rescaled the same way: below it the trigger reads released.
inline uint8_t ApplyTriggerDeadzone(uint8_t value, float threshold) {
  if (threshold <= 0.0f) return value;
  const float edge = std::min(threshold, kMaxDeadzone) * 255.0f;
  if (float(value) <= edge) return 0;
  return uint8_t(std::clamp(std::lround((float(value) - edge) * 255.0f / (255.0f - edge)), 0l, 255l));
}
inline PadSnapshot ApplyDeadzones(PadSnapshot pad, const Deadzones& zones) {
  ApplyStickDeadzone(pad.lx, pad.ly, zones.left);
  ApplyStickDeadzone(pad.rx, pad.ry, zones.right);
  pad.left_trigger = ApplyTriggerDeadzone(pad.left_trigger, zones.trigger);
  pad.right_trigger = ApplyTriggerDeadzone(pad.right_trigger, zones.trigger);
  return pad;
}

// ---- Applying a table ----------------------------------------------------------------
inline bool ControlPressed(const PadSnapshot& pad, int control) {
  if (control == kLT) return pad.left_trigger >= kTriggerPressThreshold;
  if (control == kRT) return pad.right_trigger >= kTriggerPressThreshold;
  return control >= 0 && control < kControlCount && (pad.buttons & kControls[size_t(control)].bit) != 0;
}
// Every control pressed, one bit per Control; the settings menu's capture edge-detects it.
inline uint32_t PressedControls(const PadSnapshot& pad) {
  uint32_t mask = 0;
  for (int c = 0; c < kControlCount; ++c)
    if (ControlPressed(pad, c)) mask |= 1u << unsigned(c);
  return mask;
}
// The lowest control in `mask`, or kNoSource.
inline int FirstControl(uint32_t mask) {
  for (int c = 0; c < kControlCount; ++c)
    if (mask & (1u << unsigned(c))) return c;
  return kNoSource;
}

inline int16_t InvertAxis(int16_t v) { return v == INT16_MIN ? INT16_MAX : int16_t(-v); }

struct RemapResult {
  PadSnapshot game;           // what the game sees
  uint32_t synthetic = 0;     // bit i: kSyntheticActions[i] held
};
inline RemapResult ApplyRemap(const PadSnapshot& pad, const PadRemap& remap) {
  RemapResult result;
  PadSnapshot& game = result.game;
  game.buttons = uint16_t(pad.buttons & ~kRemappedBits);  // Guide and unknown bits pass through
  for (int t = 0; t < kControlCount; ++t) {
    const int s = remap.source[size_t(t)];
    if (s == kNoSource) continue;
    if (t == kLT || t == kRT) {
      // Trigger to trigger keeps its travel; a button gives all or nothing.
      const uint8_t value = s == kLT ? pad.left_trigger
                            : s == kRT ? pad.right_trigger
                            : ControlPressed(pad, s) ? uint8_t(255) : uint8_t(0);
      uint8_t& out = t == kLT ? game.left_trigger : game.right_trigger;
      out = std::max(out, value);
    } else if (ControlPressed(pad, s)) {
      game.buttons |= kControls[size_t(t)].bit;
    }
  }
  for (int i = 0; i < kSyntheticCount; ++i) {
    const int s = remap.source[size_t(kControlCount + i)];
    if (s != kNoSource && ControlPressed(pad, s)) result.synthetic |= 1u << unsigned(i);
  }
  // Sticks: swap first, then invert the axes of the stick the game sees.
  game.lx = remap.swap_sticks ? pad.rx : pad.lx;
  game.ly = remap.swap_sticks ? pad.ry : pad.ly;
  game.rx = remap.swap_sticks ? pad.lx : pad.rx;
  game.ry = remap.swap_sticks ? pad.ly : pad.ry;
  if (remap.invert_lx) game.lx = InvertAxis(game.lx);
  if (remap.invert_ly) game.ly = InvertAxis(game.ly);
  if (remap.invert_rx) game.rx = InvertAxis(game.rx);
  if (remap.invert_ry) game.ry = InvertAxis(game.ry);
  return result;
}

// Dead zones, then the table. `remap` may be null (players without a table).
inline RemapResult ProcessPad(const PadSnapshot& raw, const Deadzones& zones, const PadRemap* remap) {
  const PadSnapshot zoned = ApplyDeadzones(raw, zones);
  if (!remap) return {zoned, 0};
  return ApplyRemap(zoned, *remap);
}

// ---- Process-wide synthetic action state ---------------------------------------------
// Written by the guest pad hook on every poll of a player's pad (0 while the F1 menu
// withholds the pad), read by whatever implements the action.
inline std::array<std::atomic<uint32_t>, 4>& SyntheticState() {
  static std::array<std::atomic<uint32_t>, 4> state{};
  return state;
}
inline void PublishSynthetic(int player, uint32_t mask) {
  if (player >= 0 && player < 4) SyntheticState()[size_t(player)].store(mask, std::memory_order_relaxed);
}
inline bool SyntheticActionHeld(int player, SyntheticAction action) {
  if (player < 0 || player >= 4) return false;
  return (SyntheticState()[size_t(player)].load(std::memory_order_relaxed) >> unsigned(action)) & 1u;
}

}  // namespace edf::pad
