// EDF2027 - ReXGlue Recompiled Project
//
// Adds a first-run setup screen (ISO -> extracted game folder), a per-user
// config file, an F1 settings screen and display-mode handling on top of ReXApp.
#pragma once

#include <rex/rex_app.h>
#include <rex/audio/sdl/sdl_audio_system.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>
#include <filesystem>
#include "launcher.h"
#include "fps_overlay.h"
#include "scripted_input.h"
#include "settings_dialog.h"
#include "setup_dialog.h"

REXCVAR_DECLARE(std::string, game_data_root);
REXCVAR_DECLARE(int32_t, window_width);
REXCVAR_DECLARE(int32_t, window_height);

class Edf2017App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Edf2017App>(new Edf2017App(ctx, "edf2027", PPCImageConfig));
  }

  // Runs before the config file is loaded: pick a per-user config path and make
  // "windowed" the default display mode (the SDK defaults to fullscreen).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!rex::cvar::HasNonDefaultValue("user_data_root")) {
      auto pref = edf::PrefPath();
      if (!pref.empty()) {
        paths.user_data_root = pref;
        if (!rex::cvar::HasNonDefaultValue("cache_root")) paths.cache_root = pref / "cache";
      }
    }
    paths.config_path = paths.user_data_root / "edf2027.toml";
    if (!rex::cvar::HasNonDefaultValue("fullscreen")) rex::cvar::SetFlagByName("fullscreen", "false");
    rex::cvar::SetFlagByName("present_letterbox", REXCVAR_GET(edf_aspect) == "stretch" ? "false" : "true");
    if (edf::ForcesConsoleAspect(REXCVAR_GET(edf_aspect)) && REXCVAR_GET(window_width) > 0)
      edf::SettingsDialog::ApplyVideoMode(REXCVAR_GET(window_width), REXCVAR_GET(window_height));
    edf::ApplyControllerDbDefault();  // before the input backend loads gamecontrollerdb.txt
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    config.gpu_plugin = "xenos";  // Xenia-derived Xenos GPU emulation (D3D12 on Windows)
    config.audio_factory = REX_AUDIO_BACKEND(rex::audio::sdl::SDLAudioSystem);
    config.input_factory = REX_INPUT_BACKEND(CreateEdfInputSystem);  // SDL + NOP + optional scripted pad (EDF_INPUT_SCRIPT)
  }

  void OnPostSetup() override {
    // ReXApp's default mouse-look gate only knows about its built-in dialogs.
    // EDF's setup and F1 settings are custom dialogs, so use ImGui's capture
    // state directly. This releases relative mouse mode and restores the OS
    // cursor whenever an interactive dialog needs it. The FPS overlay uses
    // NoInputs, so it does not interrupt mouse-look.
    auto* input_system = static_cast<rex::input::InputSystem*>(runtime()->input_system());
    if (input_system && imgui_drawer()) {
      input_system->SetActiveCallback(
          [this]() { return !imgui_drawer()->GetIO().WantCaptureMouse; });
    }
  }

  // Runs after config load and window creation, before the runtime boots.
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    config_path_ = defaults.config_path;
    ApplyDisplayMode();
    edf::ApplyKeyboardDefaults();

    rex::PathConfig paths = defaults;
    // Priority: --game_data_root on the command line, then the saved edf_game_path,
    // then the per-user extraction folder.
    std::filesystem::path candidates[] = {
        std::filesystem::path(REXCVAR_GET(game_data_root)),
        std::filesystem::path(REXCVAR_GET(edf_game_path)),
        edf::GameDir(defaults.user_data_root)};
    for (auto& c : candidates) {
      if (!c.empty() && edf::HasXex(c)) {
        paths.game_data_root = c;
        break;
      }
    }

    if (!paths.game_data_root.empty() && !ShowSettingsRequested()) {
      REXLOG_INFO("EDF2027: game folder {}", paths.game_data_root.string());
      return paths;
    }
    if (!paths.game_data_root.empty()) {
      // --settings: show the settings screen first, boot when it closes.
      pending_resume_ = [resume, paths]() { resume(paths); };
      OpenSettings();
      return std::nullopt;
    }

    // No usable game folder: first-run setup, boot when it completes.
    auto* dlg = new edf::SetupDialog(
        imgui_drawer(), edf::GameDir(defaults.user_data_root),
        [this, resume, paths](const std::filesystem::path& dir) mutable {
          paths.game_data_root = dir;
          rex::cvar::SetFlagByName("edf_game_path", dir.generic_string());  // forward slashes: TOML strings treat backslash as escape
          rex::cvar::SetFlagByName("edf_setup_done", "true");
          SaveConfig();
          REXLOG_INFO("EDF2027: setup complete, game folder {}", dir.string());
          resume(paths);
        },
        [this]() { window()->RequestClose(); });
    imgui_drawer()->AddDialog(dlg);
    return std::nullopt;
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    auto* overlay = new edf::FpsOverlay(drawer);
    edf::FpsOverlay::Current() = overlay;
    rex::ui::RegisterBind("bind_edf_settings", "F1", "EDF2027 settings", [this]() { ToggleSettings(); });
    rex::ui::RegisterBind("bind_edf_fps", "F2", "Toggle FPS overlay", []() {
      rex::cvar::SetFlagByName("edf_show_fps", REXCVAR_GET(edf_show_fps) ? "false" : "true");
    });
  }

  void OnKeyDown(rex::ui::KeyEvent& event) override {
    // While the settings dialog is capturing a rebind, every key belongs to it: Escape
    // cancels the capture instead of quitting, and F1/F2 can be bound like any other key.
    if (auto* settings = edf::SettingsDialog::Current(); settings && settings->capturing()) {
      const bool cancel = event.virtual_key() == rex::ui::VirtualKey::kEscape;
      settings->FeedCapturedKey(rex::ui::VirtualKeyToString(event.virtual_key()),
                                event.is_shift_pressed(), event.is_ctrl_pressed(),
                                event.is_alt_pressed(), cancel);
      event.set_handled(true);
      return;
    }
    if (event.virtual_key() == rex::ui::VirtualKey::kEscape) {
      event.set_handled(true);
      window()->RequestClose();
      return;
    }
    rex::ui::ProcessKeyEvent(event);
  }

 private:
  void ApplyDisplayMode() {
    bool borderless = edf::DisplayMode() == "borderless";
    rex::cvar::SetFlagByName("fullscreen", borderless ? "true" : "false");  // change callback resizes the window
  }
  void SaveConfig() { edf::SaveUserConfig(config_path_); }
  void ToggleSettings() {
    if (auto* cur = edf::SettingsDialog::Current()) {
      cur->Dismiss();
      return;
    }
    OpenSettings();
  }
  void OpenSettings() {
    if (edf::SettingsDialog::Current() || !imgui_drawer()) return;
    auto* dlg = new edf::SettingsDialog(imgui_drawer(), config_path_, "SDL3 gamepad (auto-detected; XInput via --input_backend xinput)",
                                        [this]() {
                                          if (pending_resume_) {
                                            auto r = std::move(pending_resume_);
                                            pending_resume_ = nullptr;
                                            r();
                                          }
                                        },
                                        [this]() { return RestartApplication(); });
    edf::SettingsDialog::Current() = dlg;
    imgui_drawer()->AddDialog(dlg);
  }

  bool RestartApplication() {
    const char* base = SDL_GetBasePath();
    if (!base) return false;
#if defined(_WIN32)
    const std::filesystem::path executable = std::filesystem::path(base) / "edf2027.exe";
#else
    const std::filesystem::path executable = std::filesystem::path(base) / "edf2027";
#endif
    const std::string executable_string = executable.string();
    const char* arguments[] = {executable_string.c_str(), nullptr};
    SDL_PropertiesID properties = SDL_CreateProperties();
    if (!properties) return false;
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                           const_cast<char**>(arguments));
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!process) return false;
    SDL_DestroyProcess(process);
    window()->RequestClose();
    return true;
  }

  std::filesystem::path config_path_;
  std::function<void()> pending_resume_;
};
