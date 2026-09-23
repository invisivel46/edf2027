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
#if defined(_WIN32)
#include "native_graphics/guest_shader_bridge.h"
#include "native_graphics/native_renderer_preset.h"
#include "native_graphics/guest_mesh_watch_audit.h"
#include "native_graphics/native_preview_window.h"
#include "native_graphics/native_host_surface.h"
#include "native_graphics/native_immediate_drawer.h"
#include "native_graphics/native_backend_immediate_drawer.h"
#include "native_graphics/native_backend_host.h"
#include "native_graphics/d3d12_backend.h"
#endif

REXCVAR_DECLARE(std::string, game_data_root);
REXCVAR_DECLARE(int32_t, window_width);
REXCVAR_DECLARE(int32_t, window_height);
REXCVAR_DECLARE(bool, edf_native_preview_window);
REXCVAR_DECLARE(bool, edf_native_host);
REXCVAR_DECLARE(std::string, edf_native_scene_backend);
REXCVAR_DECLARE(bool, edf_native_untiled_scene);
REXCVAR_DECLARE(bool, edf_native_mesh_watch_audit);

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
    // This port has no GPU-emulation fallback. Migrate old saved settings too,
    // before SDK setup and immediate-drawer creation.
    config.gpu_plugin.clear();
#if defined(_WIN32)
    if(!REXCVAR_GET(edf_native_host))
      REXLOG_INFO("EDF2027: ignoring legacy edf_native_host=false; the port uses native rendering");
    rex::cvar::SetFlagByName("edf_native_host","true");
    if(!REXCVAR_GET(edf_native_untiled_scene))
      REXLOG_INFO("EDF2027: ignoring legacy edf_native_untiled_scene=false; Xbox tile recording is not supported");
    rex::cvar::SetFlagByName("edf_native_untiled_scene","true");
    if(REXCVAR_GET(edf_native_preview_window)) throw std::runtime_error("native host and preview modes are mutually exclusive");
    rex::cvar::SetFlagByName("edf_native_shader_bridge","true");
    rex::cvar::SetFlagByName("edf_native_publish_frames","true");
    edf::native::ResolveNativeRendererPreset();  // once, after the migrations above; logs the effective flags
#endif
    config.audio_factory = REX_AUDIO_BACKEND(rex::audio::sdl::SDLAudioSystem);
    config.input_factory = REX_INPUT_BACKEND(CreateEdfInputSystem);  // SDL + NOP + optional scripted pad (EDF_INPUT_SCRIPT)
  }

  std::unique_ptr<rex::ui::ImmediateDrawer> OnCreateImmediateDrawer() override {
#if defined(_WIN32)
    if(EDF_NATIVE_FLAG(host)) {
      edf::native::InitializeGuestShaderBridge({});
      if(REXCVAR_GET(edf_native_scene_backend).starts_with("d3d12")) {
        edf::native::RegisterNativeD3D12Backend();
        auto backend=std::shared_ptr<edf::native::NativeRenderBackend>(
          edf::native::CreateNativeRenderBackend(REXCVAR_GET(edf_native_scene_backend)));
        auto drawer=std::make_unique<edf::native::NativeBackendImmediateDrawer>(backend);
        native_backend_immediate_=drawer.get();
        native_backend_host_=edf::native::NativeBackendHost::Create(
          static_cast<HWND>(window()->GetNativeWindowHandle()),backend,
          [this](edf::native::NativeBackendRenderTarget& target) {
            if(!imgui_drawer()) return;
            native_backend_immediate_->SetTarget(&target);
            rex::ui::AppUIDrawContext context(target.width(),target.height());
            try { imgui_drawer()->Draw(context); }
            catch(...) {
              if(native_backend_immediate_) { native_backend_immediate_->End(); native_backend_immediate_->SetTarget(nullptr); }
              throw;
            }
            if(native_backend_immediate_) native_backend_immediate_->SetTarget(nullptr);
          },[context=&app_context()](std::function<void()> callback) {
            return context->CallInUIThreadDeferred(std::move(callback));
          });
        return drawer;
      }
      std::unique_ptr<edf::native::NativeImmediateDrawer> drawer;
      edf::native::VisitNativePresentationContext([&](auto& device,auto& context) {
        drawer=std::make_unique<edf::native::NativeImmediateDrawer>(device,context);
      });
      if(!drawer) throw std::runtime_error("native host has no rendering context");
      native_immediate_=drawer.get();
      native_host_=edf::native::NativeHostSurface::Create(
        static_cast<HWND>(window()->GetNativeWindowHandle()),[this](UINT width,UINT height) {
          if(!imgui_drawer()) return;
          rex::ui::AppUIDrawContext context(width,height);
          try { imgui_drawer()->Draw(context); }
          catch(...) { if(native_immediate_) native_immediate_->End(); throw; }
        },[context=&app_context()](std::function<void()> callback) {
          return context->CallInUIThreadDeferred(std::move(callback));
        });
      return drawer;
    }
#endif
    return nullptr;
  }

  void OnPostSetup() override {
#if defined(_WIN32)
    edf::native::InitializeGuestShaderBridge(runtime()->game_data_root());
    if(REXCVAR_GET(edf_native_mesh_watch_audit)) {
      native_mesh_audit_=std::make_shared<edf::native::GuestMeshWatchAudit>(*runtime()->memory());
      edf::native::SetNativeMeshWatchAudit(native_mesh_audit_);
    }
    if(REXCVAR_GET(edf_native_preview_window))
      native_preview_=std::make_unique<edf::native::NativePreviewWindow>();
#endif
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

  void OnShutdown() override {
#if defined(_WIN32)
    edf::native::LogNativeCoverageCensusFinal();
    edf::native::SetNativeMeshWatchAudit({});
    native_mesh_audit_.reset();
    if(native_backend_host_) native_backend_host_->Stop();
    native_backend_host_.reset(); native_backend_immediate_=nullptr;
    if(native_host_) native_host_->Stop();
    native_host_.reset(); native_immediate_=nullptr;
    native_preview_.reset();
#endif
  }
  bool OnWindowCloseRequested() override {
#if defined(_WIN32)
    edf::native::LogNativeCoverageCensusFinal();
    if(native_backend_host_) native_backend_host_->Stop();
    native_backend_host_.reset(); native_backend_immediate_=nullptr;
    if(native_host_) native_host_->Stop();
    native_host_.reset(); native_immediate_=nullptr;
    native_preview_.reset();
#endif
    return true;
  }
 private:
#if defined(_WIN32)
  std::unique_ptr<edf::native::NativePreviewWindow> native_preview_;
  std::shared_ptr<edf::native::GuestMeshWatchAudit> native_mesh_audit_;
  std::shared_ptr<edf::native::NativeBackendHost> native_backend_host_;
  edf::native::NativeBackendImmediateDrawer* native_backend_immediate_=nullptr;
  std::shared_ptr<edf::native::NativeHostSurface> native_host_;
  edf::native::NativeImmediateDrawer* native_immediate_=nullptr; // Owned by SDK.
#endif
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
