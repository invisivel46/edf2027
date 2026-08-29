// EDF2027 - launcher cvars (persisted to the per-user edf2027.toml).
#include <rex/cvar.h>
#include <cstdint>
#include <string>

REXCVAR_DEFINE_STRING(edf_game_path, "", "EDF2027", "Folder containing the extracted game disc (default.xex)");
REXCVAR_DEFINE_STRING(edf_display_mode, "windowed", "EDF2027", "Display mode: windowed or borderless").allowed({"windowed", "borderless"});
REXCVAR_DEFINE_INT32(edf_fps_cap, 0, "EDF2027", "Frame rate cap (0 = off)").range(0, 480);
REXCVAR_DEFINE_BOOL(edf_show_settings, false, "EDF2027", "Open the settings screen at startup");
REXCVAR_DEFINE_BOOL(settings, false, "EDF2027", "Alias of edf_show_settings (--settings)");
REXCVAR_DEFINE_BOOL(edf_setup_done, false, "EDF2027", "First-run setup completed");
REXCVAR_DEFINE_STRING(edf_aspect, "native", "EDF2027", "Aspect handling: native, Hor+ ultrawide, 16:9 letterbox, or 16:9 stretch")
    .allowed({"native", "ultrawide", "letterbox", "stretch"});
REXCVAR_DEFINE_BOOL(edf_show_fps, false, "EDF2027", "Show the frame-rate overlay");
REXCVAR_DEFINE_BOOL(edf_frametime_log, false, "EDF2027", "Log frame-time statistics (min/avg/max/1% low) every 5 s");
REXCVAR_DEFINE_BOOL(edf_trace_input, false, "EDF2027", "Log verbose guest input and XAM diagnostics");
REXCVAR_DEFINE_BOOL(edf_rumble, true, "EDF2027", "Enable controller vibration");
REXCVAR_DEFINE_INT32(edf_frame_pacer_spin_us, 250, "EDF2027", "Busy-wait portion of the frame limiter in microseconds").range(0, 2000);

#include <SDL3/SDL.h>
#include <filesystem>
#include "keybind_logic.h"

// Mouse buttons are absent from the SDK's mnk driver, so bind them here; input_hooks.cpp
// folds the result into the guest's XInput state. Values are kMouseTargets tokens.
REXCVAR_DEFINE_STRING(edf_mouse_left, "right_trigger", "EDF2027", "Pad action for the left mouse button");
REXCVAR_DEFINE_STRING(edf_mouse_right, "left_trigger", "EDF2027", "Pad action for the right mouse button");
REXCVAR_DEFINE_STRING(edf_mouse_middle, "lstick_press", "EDF2027", "Pad action for the middle mouse button");

namespace edf {

// Keyboard & mouse defaults for this game; the binding table lives in keybind_logic.h so the
// settings dialog and these defaults cannot drift apart. Pad actions in EDF 2017: RT fire,
// LT zoom, A jump/roll, B reload, X weapon change, Y vehicle, LB/RB radio chat, Start pause,
// Back retire.
void ApplyKeyboardDefaults() {
  static const char* kModeDefaults[][2] = {
      {"mnk_mode", "true"}, {"mnk_mouse", "true"}, {"mnk_sensitivity", "1.0"}};
  for (auto& kv : kModeDefaults)
    if (!rex::cvar::HasNonDefaultValue(kv[0])) rex::cvar::SetFlagByName(kv[0], kv[1]);
  for (const auto& action : kPadActions)
    if (!rex::cvar::HasNonDefaultValue(action.cvar)) rex::cvar::SetFlagByName(action.cvar, action.default_value);
  for (const auto& action : kMouseActions)
    if (!rex::cvar::HasNonDefaultValue(action.cvar)) rex::cvar::SetFlagByName(action.cvar, action.default_value);
}

// "Reset to defaults" in the settings dialog: overwrite whatever the user has set.
void ResetKeyboardDefaults() {
  for (const auto& action : kPadActions) rex::cvar::SetFlagByName(action.cvar, action.default_value);
  for (const auto& action : kMouseActions) rex::cvar::SetFlagByName(action.cvar, action.default_value);
}

// Point the SDK at the gamecontrollerdb.txt shipped next to the executable (silences the missing-file warning).
void ApplyControllerDbDefault() {
  if (rex::cvar::HasNonDefaultValue("hid_mappings_file")) return;
  const char* base = SDL_GetBasePath();
  if (!base) return;
  std::filesystem::path db = std::filesystem::path(base) / "gamecontrollerdb.txt";
  if (std::filesystem::is_regular_file(db)) rex::cvar::SetFlagByName("hid_mappings_file", db.generic_string());
}

}  // namespace edf
