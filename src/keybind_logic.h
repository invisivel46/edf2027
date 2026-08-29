// EDF2027 - keyboard and mouse binding tables and bind-string handling.
//
// Keyboard bindings live in the SDK's keybind_* cvars, read by its mnk input driver.
// The value grammar is the driver's (see mnk_input_driver.cpp): a comma-separated list
// of alternatives, each "Mod+Mod+Key", where Mod is Shift, Ctrl/Control or Alt and Key
// is a name from the SDK's key table. Modifiers must match exactly at press time, so
// "W" does not fire while Shift is held.
//
// Mouse buttons are not part of that driver, so this port binds them to pad actions
// itself (input_hooks.cpp folds them into the guest's XInput state).
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

struct PadAction {
  const char* cvar;
  const char* label;
  const char* group;
  const char* default_value;
};

// The keyboard bindings this port exposes, in display order. Also the source of truth
// for the first-run defaults applied in launcher_cvars.cpp.
inline constexpr PadAction kPadActions[] = {
    {"keybind_lstick_up", "Move forward", "Movement", "W"},
    {"keybind_lstick_down", "Move back", "Movement", "S"},
    {"keybind_lstick_left", "Strafe left", "Movement", "A"},
    {"keybind_lstick_right", "Strafe right", "Movement", "D"},
    {"keybind_lstick_press", "Left stick press", "Movement", "F"},
    {"keybind_rstick_up", "Look up", "Camera", "I"},
    {"keybind_rstick_down", "Look down", "Camera", "K"},
    {"keybind_rstick_left", "Look left", "Camera", "J"},
    {"keybind_rstick_right", "Look right", "Camera", "L"},
    {"keybind_rstick_press", "Right stick press", "Camera", "C"},
    {"keybind_right_trigger", "Fire (RT)", "Combat", "Ctrl,X"},
    {"keybind_left_trigger", "Zoom (LT)", "Combat", "Alt,Z"},
    {"keybind_a", "Jump / roll (A)", "Combat", "Space"},
    {"keybind_b", "Reload (B)", "Combat", "R"},
    {"keybind_x", "Change weapon (X)", "Combat", "Q"},
    {"keybind_y", "Enter vehicle (Y)", "Combat", "E"},
    {"keybind_left_shoulder", "Radio chat left (LB)", "Combat", "1"},
    {"keybind_right_shoulder", "Radio chat right (RB)", "Combat", "3"},
    {"keybind_dpad_up", "D-pad up", "D-pad", "Up"},
    {"keybind_dpad_down", "D-pad down", "D-pad", "Down"},
    {"keybind_dpad_left", "D-pad left", "D-pad", "Left"},
    {"keybind_dpad_right", "D-pad right", "D-pad", "Right"},
    {"keybind_start", "Pause (Start)", "System", "Return"},
    {"keybind_back", "Retire (Back)", "System", "Tab"},
    {"keybind_guide", "Guide", "System", ""},
};

// Mouse buttons are bound to a pad action rather than to a key.
inline constexpr PadAction kMouseActions[] = {
    {"edf_mouse_left", "Left button", "Mouse", "right_trigger"},
    {"edf_mouse_right", "Right button", "Mouse", "left_trigger"},
    {"edf_mouse_middle", "Middle button", "Mouse", "lstick_press"},
};

// Pad actions a mouse button can be bound to, in combo order.
struct MouseTarget {
  const char* value;
  const char* label;
  uint16_t button_mask;  // X_INPUT_GAMEPAD button bit, 0 for the triggers
  bool left_trigger;
  bool right_trigger;
};

inline constexpr MouseTarget kMouseTargets[] = {
    {"none", "Unbound", 0, false, false},
    {"right_trigger", "Fire (RT)", 0, false, true},
    {"left_trigger", "Zoom (LT)", 0, true, false},
    {"a", "Jump / roll (A)", 0x1000, false, false},
    {"b", "Reload (B)", 0x2000, false, false},
    {"x", "Change weapon (X)", 0x4000, false, false},
    {"y", "Enter vehicle (Y)", 0x8000, false, false},
    {"left_shoulder", "Radio chat left (LB)", 0x0100, false, false},
    {"right_shoulder", "Radio chat right (RB)", 0x0200, false, false},
    {"lstick_press", "Left stick press", 0x0040, false, false},
    {"rstick_press", "Right stick press", 0x0080, false, false},
    {"start", "Pause (Start)", 0x0010, false, false},
    {"back", "Retire (Back)", 0x0020, false, false},
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
