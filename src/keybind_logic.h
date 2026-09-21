// EDF2027 - keyboard and mouse binding tables and bind-string handling.
//
// Bindings name game actions, not pad buttons: native_kbm.cpp writes them into the game's
// own input channels (see native_kbm_logic.h), and the SDK's pad-emulation driver is off.
// They live in this port's kbm_* cvars. The value grammar is a comma-separated list of
// alternatives, each "Mod+Mod+Key", where Mod is Shift, Ctrl/Control or Alt and Key is a
// name from the SDK's key table. A bind needs the modifiers it names and ignores others,
// so "W" still moves while Shift is held.
//
// Mouse buttons are chosen from a list rather than captured, since key capture runs
// through the app's key events.
//
// Pure logic only - no SDK, ImGui or platform headers - so the unit tests can cover it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace edf {

struct KeyAction {
  const char* cvar;
  const char* label;
  const char* group;
  const char* default_value;
};

// X(cvar, label, group, default). One list, so the table below and the cvar definitions
// in launcher_cvars.cpp cannot disagree about a name or a default.
#define EDF_KEY_ACTIONS(X) \
  X(kbm_move_forward, "Move forward", "Movement", "W") \
  X(kbm_move_back, "Move back", "Movement", "S") \
  X(kbm_strafe_left, "Strafe left", "Movement", "A") \
  X(kbm_strafe_right, "Strafe right", "Movement", "D") \
  X(kbm_fire, "Fire", "Combat", "X") \
  X(kbm_zoom, "Weapon zoom", "Combat", "Z") \
  X(kbm_jump, "Jump / roll, menu confirm", "Combat", "Space") \
  X(kbm_next_weapon, "Next weapon", "Combat", "Q") \
  X(kbm_vehicle, "Enter vehicle (pad Y)", "Combat", "E") \
  X(kbm_pad_x, "Pad X", "Combat", "R") \
  X(kbm_cancel, "Menu cancel (pad B)", "Combat", "Backspace") \
  X(kbm_lstick_press, "Left stick press", "Combat", "F") \
  X(kbm_rstick_press, "Right stick press", "Combat", "C") \
  X(kbm_menu_up, "Menu up", "Menus", "Up") \
  X(kbm_menu_down, "Menu down", "Menus", "Down") \
  X(kbm_menu_left, "Menu left", "Menus", "Left") \
  X(kbm_menu_right, "Menu right", "Menus", "Right") \
  X(kbm_start, "Start (title screen, pause)", "System", "Return") \
  X(kbm_back, "Back (retire)", "System", "Tab")

// The keyboard bindings this port exposes, in display order, which is also the order of
// edf::kbm::Action in native_kbm_logic.h. Fire, zoom, jump and next weapon follow whatever pad
// control the in-game controller settings give them; the rest are fixed controls, named
// by what is known of them.
#define EDF_KEY_ACTION_ROW(cvar, label, group, default_value) {#cvar, label, group, default_value},
inline constexpr KeyAction kKeyActions[] = {EDF_KEY_ACTIONS(EDF_KEY_ACTION_ROW)};
#undef EDF_KEY_ACTION_ROW

// Mouse buttons are bound to an action rather than to a key.
inline constexpr KeyAction kMouseActions[] = {
    {"edf_mouse_left", "Left button", "Mouse", "fire"},
    {"edf_mouse_right", "Right button", "Mouse", "zoom"},
    {"edf_mouse_middle", "Middle button", "Mouse", "next_weapon"},
};

// Actions a mouse button can be bound to, in combo order. `action` indexes kKeyActions
// (and edf::kbm::Action); -1 is unbound.
struct MouseTarget {
  const char* value;
  const char* label;
  int action;
};

inline constexpr MouseTarget kMouseTargets[] = {
    {"none", "Unbound", -1},
    {"fire", "Fire", 4},
    {"zoom", "Weapon zoom", 5},
    {"jump", "Jump / roll, menu confirm", 6},
    {"next_weapon", "Next weapon", 7},
    {"vehicle", "Enter vehicle (pad Y)", 8},
    {"pad_x", "Pad X", 9},
    {"cancel", "Menu cancel (pad B)", 10},
    {"lstick_press", "Left stick press", 11},
    {"rstick_press", "Right stick press", 12},
    {"start", "Start (title screen, pause)", 17},
    {"back", "Back (retire)", 18},
};

inline constexpr int kMouseTargetCount = static_cast<int>(std::size(kMouseTargets));

inline int MouseTargetIndex(std::string_view value) {
  for (int i = 0; i < kMouseTargetCount; ++i)
    if (value == kMouseTargets[i].value) return i;
  return 0;
}
inline const MouseTarget& MouseTargetAt(int index) {
  return kMouseTargets[index >= 0 && index < kMouseTargetCount ? index : 0];
}

inline std::string_view TrimBindSpaces(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

// Splits a bind value into its alternatives, dropping empty ones.
inline std::vector<std::string> SplitBind(std::string_view value) {
  std::vector<std::string> out;
  while (!value.empty()) {
    const size_t comma = value.find(',');
    std::string_view token = value.substr(0, comma);
    value = comma == std::string_view::npos ? std::string_view() : value.substr(comma + 1);
    token = TrimBindSpaces(token);
    if (!token.empty()) out.emplace_back(token);
  }
  return out;
}

inline std::string JoinBind(const std::vector<std::string>& tokens) {
  std::string out;
  for (const auto& token : tokens) {
    if (!out.empty()) out.push_back(',');
    out += token;
  }
  return out;
}

// Builds one alternative in the driver's grammar. Modifier order is fixed so that the
// same combination always produces the same string.
inline std::string FormatBindToken(bool shift, bool ctrl, bool alt, std::string_view key) {
  if (key.empty()) return std::string();
  std::string out;
  if (shift) out += "Shift+";
  if (ctrl) out += "Ctrl+";
  if (alt) out += "Alt+";
  out += key;
  return out;
}

inline bool BindContains(std::string_view value, std::string_view token) {
  for (const auto& existing : SplitBind(value))
    if (existing == token) return true;
  return false;
}

// Appends an alternative unless it is already bound.
inline std::string AddBindAlternative(std::string_view value, std::string_view token) {
  if (token.empty() || BindContains(value, token)) return std::string(value);
  std::vector<std::string> tokens = SplitBind(value);
  tokens.emplace_back(token);
  return JoinBind(tokens);
}

inline std::string RemoveBindAlternative(std::string_view value, size_t index) {
  std::vector<std::string> tokens = SplitBind(value);
  if (index < tokens.size()) tokens.erase(tokens.begin() + static_cast<ptrdiff_t>(index));
  return JoinBind(tokens);
}

// Human-readable form for the settings list.
inline std::string PrettyBind(std::string_view value) {
  const std::vector<std::string> tokens = SplitBind(value);
  if (tokens.empty()) return "(unbound)";
  std::string out;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (i) out += "  or  ";
    out += tokens[i];
  }
  return out;
}

// Display name for a cvar, for messages that name the other side of a conflict.
inline std::string_view ActionLabel(std::string_view cvar) {
  for (const auto& action : kKeyActions)
    if (cvar == action.cvar) return action.label;
  for (const auto& action : kMouseActions)
    if (cvar == action.cvar) return action.label;
  return cvar;
}

// Every cvar an action list touches, used to detect a binding shared by two actions.
inline std::string_view ConflictingAction(std::string_view token,
                                          std::string_view skip_cvar,
                                          const std::vector<std::pair<std::string, std::string>>& bindings) {
  if (token.empty()) return {};
  for (const auto& [cvar, value] : bindings) {
    if (cvar == skip_cvar) continue;
    if (BindContains(value, token)) return cvar;
  }
  return {};
}

}  // namespace edf
