// EDF 2017 PC - launcher helpers: config cvars, user paths, XEX identification.
#pragma once
#include <SDL3/SDL.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "core_logic.h"

REXCVAR_DECLARE(std::string, edf_game_path);      // extracted disc folder (contains default.xex)
REXCVAR_DECLARE(std::string, edf_display_mode);   // "windowed" | "borderless"
REXCVAR_DECLARE(int32_t, edf_fps_cap);            // 0 = off
REXCVAR_DECLARE(bool, edf_show_settings);         // open the settings screen at startup
REXCVAR_DECLARE(bool, settings);                  // --settings alias
inline bool ShowSettingsRequested() { return REXCVAR_GET(edf_show_settings) || REXCVAR_GET(settings); }
REXCVAR_DECLARE(bool, edf_setup_done);
REXCVAR_DECLARE(std::string, edf_aspect);          // "native" | "ultrawide" | "letterbox" | "stretch"
REXCVAR_DECLARE(bool, edf_show_fps);
REXCVAR_DECLARE(bool, edf_frametime_log);
REXCVAR_DECLARE(bool, edf_trace_input);
REXCVAR_DECLARE(bool, edf_rumble);
REXCVAR_DECLARE(int32_t, edf_frame_pacer_spin_us);

namespace edf {

void ApplyKeyboardDefaults();
void ApplyControllerDbDefault();

constexpr uint32_t kTitleId = 0x445007D3;  // Earth Defense Force 2017 (USA/Europe)
constexpr const char* kVersion = "0.2.0";

inline std::filesystem::path GameDir(const std::filesystem::path& user_data_root) {
  return user_data_root / "game";
}

inline bool HasXex(const std::filesystem::path& dir) {
  return std::filesystem::is_regular_file(dir / "default.xex");
}

// Reads the title id from a XEX2 header (the header is never encrypted; only the PE body is).
// Returns 0 if the file is not a XEX2 or has no execution-info header.
inline uint32_t ReadXexTitleId(const std::filesystem::path& xex) {
  std::ifstream f(xex, std::ios::binary);
  if (!f) return 0;
  std::vector<uint8_t> h(0x1000);
  f.read(reinterpret_cast<char*>(h.data()), h.size());
  h.resize(static_cast<size_t>(f.gcount()));
  return ParseXexTitleId(h);
}

inline std::string DisplayMode() { return REXCVAR_GET(edf_display_mode); }

// Saves the cvar config, then strips per-launch options that must not persist
// (log file from the command line, one-shot flags, explicit --game_data_root).
inline void SaveUserConfig(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  rex::cvar::SaveConfig(path);
  std::ifstream in(path, std::ios::binary);
  if (!in) return;
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::string out = FilterPersistentConfig(contents);
  in.close();
  std::ofstream(path, std::ios::trunc) << out;
}

// Per-user data folder (Windows: %APPDATA%\edf2017, Linux: ~/.local/share/edf2017,
// macOS: ~/Library/Application Support/edf2017). Kept out of Documents so the
// extracted game data does not land in cloud-synced folders.
inline std::filesystem::path PrefPath() {
  char* p = SDL_GetPrefPath("", "edf2017");
  std::filesystem::path r = p ? std::filesystem::path(p) : std::filesystem::path();
  if (p) SDL_free(p);
  return r;
}

}  // namespace edf
