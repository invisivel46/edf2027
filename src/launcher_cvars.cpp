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
REXCVAR_DEFINE_BOOL(edf_native_memory_log, false, "EDF2027",
                    "Log process memory (private bytes, working set, handles) and the simulation tick count every 5 s beside the FPS line, for soak runs (tools/soak-report.py)");
REXCVAR_DEFINE_BOOL(edf_trace_input, false, "EDF2027", "Log verbose guest input and XAM diagnostics");
REXCVAR_DEFINE_BOOL(edf_rumble, true, "EDF2027", "Enable controller vibration");
REXCVAR_DEFINE_INT32(edf_frame_pacer_spin_us, 250, "EDF2027",
                     "Minimum busy-wait margin of the frame limiter in microseconds; the margin adapts above it to the timer's observed oversleep").range(0, 2000);
REXCVAR_DEFINE_BOOL(edf_frame_pacer_before_present, true, "EDF2027",
                    "With the frame-rate unlock active and VSync off, pace capped frames before the guest present (steady present cadence) rather than after it (up to one period less latency, present times follow each frame's work)");

#include <SDL3/SDL.h>
#include <filesystem>
#include "keybind_logic.h"

// Native keyboard bindings, one cvar per game action, generated from the table in
// keybind_logic.h. Each cvar's default is its table default, so clearing a binding is a
// real, persisted choice rather than something the next launch refills.
#define EDF_DEFINE_KEY_CVAR(cvar, label, group, default_value)   REXCVAR_DEFINE_STRING(cvar, default_value, "EDF2027/Keys", label);
EDF_KEY_ACTIONS(EDF_DEFINE_KEY_CVAR)
#undef EDF_DEFINE_KEY_CVAR

// Mouse buttons are chosen from a list rather than captured. Values are kMouseTargets tokens.
REXCVAR_DEFINE_STRING(edf_mouse_left, "fire", "EDF2027", "Action for the left mouse button");
REXCVAR_DEFINE_STRING(edf_mouse_right, "zoom", "EDF2027", "Action for the right mouse button");
REXCVAR_DEFINE_STRING(edf_mouse_middle, "next_weapon", "EDF2027", "Action for the middle mouse button");

namespace edf {

// Keyboard & mouse defaults for this game; the binding table lives in keybind_logic.h so the
// settings dialog and these defaults cannot drift apart.
void ApplyKeyboardDefaults() {
  // Native input replaces the SDK's keyboard-as-pad driver; a config saved by an older
  // build may still ask for it, and both at once would press everything twice.
  rex::cvar::SetFlagByName("mnk_mode", "false");
  rex::cvar::SetFlagByName("mnk_mouse", "false");
  // A mouse-button value from the pad-emulation era names a pad control, not an action.
  for (const auto& action : kMouseActions)
    if (MouseTargetIndex(rex::cvar::GetFlagByName(action.cvar)) == 0 &&
        rex::cvar::GetFlagByName(action.cvar) != "none")
      rex::cvar::SetFlagByName(action.cvar, action.default_value);
}

// "Reset to defaults" in the settings dialog: overwrite whatever the user has set.
void ResetKeyboardDefaults() {
  for (const auto& action : kKeyActions) rex::cvar::SetFlagByName(action.cvar, action.default_value);
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
