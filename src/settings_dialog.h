// EDF 2017 PC - settings screen (F1). Writes SDK cvars and saves the per-user config TOML.
#pragma once
#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <imgui.h>
#include <algorithm>
#include <cstdlib>
#include <array>
#include <filesystem>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <functional>
#include <string>
#include "launcher.h"
#include "ui_strings.h"

REXCVAR_DECLARE(bool, fullscreen);
REXCVAR_DECLARE(int32_t, window_width);
REXCVAR_DECLARE(int32_t, window_height);
REXCVAR_DECLARE(int32_t, video_mode_width);
REXCVAR_DECLARE(int32_t, video_mode_height);
REXCVAR_DECLARE(bool, audio_mute);
REXCVAR_DECLARE(bool, mnk_mode);
REXCVAR_DECLARE(bool, mnk_mouse);
REXCVAR_DECLARE(double, mnk_sensitivity);
REXCVAR_DECLARE(std::string, input_backend);
REXCVAR_DECLARE(std::string, edf_aspect);

namespace edf {

class SettingsDialog final : public rex::ui::ImGuiDialog {
 public:
  SettingsDialog(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path, std::string pad_name,
                 std::function<void()> on_close = nullptr, std::function<bool()> on_restart = nullptr)
      : ImGuiDialog(drawer), config_path_(std::move(config_path)), pad_name_(std::move(pad_name)),
        on_close_(std::move(on_close)), on_restart_(std::move(on_restart)) {
    display_mode_ = REXCVAR_GET(edf_display_mode) == "borderless" ? 1 : 0;
    int w = REXCVAR_GET(window_width) > 0 ? REXCVAR_GET(window_width) : REXCVAR_GET(video_mode_width);
    int h = REXCVAR_GET(window_height) > 0 ? REXCVAR_GET(window_height) : REXCVAR_GET(video_mode_height);
    res_index_ = (int)kPresets.size() - 1;
    for (size_t i = 0; i + 1 < kPresets.size(); i++) if (kPresets[i].w == w && kPresets[i].h == h) res_index_ = (int)i;
    custom_w_ = w; custom_h_ = h;
    scale_index_ = std::max(0, std::min(3, std::atoi(rex::cvar::GetFlagByName("draw_resolution_scale_x").c_str()) - 1));
    vsync_ = rex::cvar::GetFlagByName("vsync") != "false";
    fps_cap_index_ = 0; for (int i = 0; i < 4; i++) if (kFpsCaps[i] == REXCVAR_GET(edf_fps_cap)) fps_cap_index_ = i;
    mute_ = REXCVAR_GET(audio_mute);
    mnk_ = REXCVAR_GET(mnk_mode);
    mnk_mouse_ = REXCVAR_GET(mnk_mouse);
    mnk_sens_ = (float)REXCVAR_GET(mnk_sensitivity);
    rumble_ = REXCVAR_GET(edf_rumble);
    show_fps_ = REXCVAR_GET(edf_show_fps);
    frametime_log_ = REXCVAR_GET(edf_frametime_log);
    trace_input_ = REXCVAR_GET(edf_trace_input);
    af_index_ = std::clamp(GetInt("anisotropic_override", 3) + 1, 0, 6);
    msaa_ = GetBool("native_2x_msaa", true);
    { const std::string v = GetStr("swap_post_effect", "none"); fxaa_index_ = v == "fxaa" ? 1 : v == "fxaa_extreme" ? 2 : 0; }
    aspect_index_ = AspectIndex(REXCVAR_GET(edf_aspect));
    { const std::string v = GetStr("present_effect", "bilinear"); upscale_index_ = v == "cas" ? 1 : v == "fsr" ? 2 : 0; }
    cas_sharp_ = static_cast<float>(GetDouble("present_cas_additional_sharpness", 0.0));
    fsr_sharp_ = static_cast<float>(GetDouble("present_fsr_sharpness_reduction", 0.2));
    dither_ = GetBool("present_dither", false);
    async_shaders_ = GetBool("async_shader_compilation", true);
    int refresh = static_cast<int>(GetDouble("video_mode_refresh_rate", 60));
    for (int i = 0; i < 4; ++i) if (refresh == kRefresh[i]) refresh_index_ = i;
  }
  static SettingsDialog*& Current() { static SettingsDialog* s = nullptr; return s; }
  void Dismiss() { Close(); }

 protected:
  void OnClose() override {
    if (Current() == this) Current() = nullptr;
    if (on_close_) { auto f = std::move(on_close_); on_close_ = nullptr; f(); }
  }
  void OnDraw(ImGuiIO& io) override {
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(640, std::min(760.0f, io.DisplaySize.y - 40.0f)), ImGuiCond_Appearing);
    bool open = true;
    ImGui::Begin(str::kSettingsTitle, &open, ImGuiWindowFlags_NoCollapse);
    ImGui::SeparatorText("Display");
    if (ImGui::Combo("Display mode", &display_mode_, "Windowed\0Borderless fullscreen\0")) ApplyDisplayMode();
    std::string labels; for (auto& p : kPresets) { labels += p.name; labels.push_back('\0'); }
    if (ImGui::Combo("Window size *", &res_index_, labels.c_str())) restart_ = true;
    if (res_index_ == (int)kPresets.size() - 1) { ImGui::InputInt("Width", &custom_w_); ImGui::InputInt("Height", &custom_h_); }
    if (ImGui::Combo("Render resolution scale *", &scale_index_, "1x (native 720p)\0" "2x\0" "3x\0" "4x\0")) restart_ = true;
    if (ImGui::Checkbox("VSync", &vsync_)) rex::cvar::SetFlagByName("vsync", vsync_ ? "true" : "false");
    if (ImGui::Combo("FPS cap", &fps_cap_index_, "Off\0" "30\0" "60\0" "120\0")) rex::cvar::SetFlagByName("edf_fps_cap", std::to_string(FpsCapValue(fps_cap_index_)));
    if (ImGui::Combo("Game refresh rate *", &refresh_index_, "60 Hz (original)\0" "30 Hz\0" "120 Hz\0" "144 Hz\0")) {
      rex::cvar::SetFlagByName("video_mode_refresh_rate", std::to_string(RefreshValue(refresh_index_))); restart_ = true;
    }
    ImGui::SameLine(); HelpMarker("The game ties logic to the console vertical blank. Rates other than 60 Hz change game speed; keep 60 unless testing.");
    if (ImGui::Combo("Aspect ratio *", &aspect_index_,
                     "Native (fills window)\0" "Ultrawide Hor+ (21:9 / 32:9)\0"
                     "16:9 letterbox\0" "16:9 stretched\0")) {
      rex::cvar::SetFlagByName("edf_aspect", std::string(AspectValue(aspect_index_)));
      rex::cvar::SetFlagByName("present_letterbox", PresentLetterbox(AspectValue(aspect_index_)) ? "true" : "false"); restart_ = true;
    }
    ImGui::SameLine(); HelpMarker("Ultrawide Hor+ gives the guest the full window dimensions, expanding horizontal view without stretching. Native does the same for any aspect. The 16:9 modes preserve the console-shaped render.");
    ImGui::SeparatorText("Quality");
    if (ImGui::Combo("Anisotropic filtering", &af_index_, "Game default\0" "Off\0" "1x\0" "2x\0" "4x\0" "8x\0" "16x\0"))
      rex::cvar::SetFlagByName("anisotropic_override", std::to_string(AnisotropicValue(af_index_)));
    if (ImGui::Checkbox("Native 2x MSAA (where requested) *", &msaa_)) {
      rex::cvar::SetFlagByName("native_2x_msaa", msaa_ ? "true" : "false"); restart_ = true;
    }
    if (ImGui::Combo("Post anti-aliasing *", &fxaa_index_, "Off\0" "FXAA\0" "FXAA extreme\0")) {
      rex::cvar::SetFlagByName("swap_post_effect", std::string(FxaaValue(fxaa_index_))); restart_ = true;
    }
    if (ImGui::Checkbox("Background shader compilation", &async_shaders_))
      rex::cvar::SetFlagByName("async_shader_compilation", async_shaders_ ? "true" : "false");
    ImGui::SeparatorText("Upscaling (AMD FidelityFX)");
    const bool fidelity_fx = HasFidelityFx();
    if (!fidelity_fx) ImGui::TextDisabled("Unavailable: runtime was built without FidelityFX.");
    ImGui::BeginDisabled(!fidelity_fx);
    if (ImGui::Combo("Effect *", &upscale_index_, "Off (bilinear)\0" "CAS (sharpen)\0" "FSR 1.0\0")) {
      rex::cvar::SetFlagByName("present_effect", std::string(UpscaleValue(upscale_index_))); restart_ = true;
    }
    if (upscale_index_ == 1 && ImGui::SliderFloat("CAS sharpness", &cas_sharp_, 0.0f, 1.0f, "%.2f"))
      rex::cvar::SetFlagByName("present_cas_additional_sharpness", std::to_string(cas_sharp_));
    if (upscale_index_ == 2 && ImGui::SliderFloat("FSR sharpness reduction", &fsr_sharp_, 0.0f, 2.0f, "%.2f stops"))
      rex::cvar::SetFlagByName("present_fsr_sharpness_reduction", std::to_string(fsr_sharp_));
    if (ImGui::Checkbox("Output dithering *", &dither_)) {
      rex::cvar::SetFlagByName("present_dither", dither_ ? "true" : "false"); restart_ = true;
    }
    ImGui::EndDisabled();
    ImGui::SeparatorText("Audio");
    if (ImGui::Checkbox("Mute", &mute_)) rex::cvar::SetFlagByName("audio_mute", mute_ ? "true" : "false");
    ImGui::SeparatorText("Controls");
    ImGui::Text("Controller: %s", pad_name_.c_str());
    if (ImGui::Checkbox("Controller vibration", &rumble_))
      rex::cvar::SetFlagByName("edf_rumble", rumble_ ? "true" : "false");
    if (ImGui::Checkbox("Keyboard & mouse controller emulation *", &mnk_)) restart_ = true;
    if (ImGui::Checkbox("Mouse look (right stick)", &mnk_mouse_)) rex::cvar::SetFlagByName("mnk_mouse", mnk_mouse_ ? "true" : "false");
    if (ImGui::SliderFloat("Mouse sensitivity", &mnk_sens_, 0.1f, 5.0f, "%.2f")) rex::cvar::SetFlagByName("mnk_sensitivity", std::to_string(mnk_sens_));
    ImGui::TextDisabled("WASD move | mouse look (IJKL fallback) | LMB / Ctrl = fire (RT) | RMB / Alt = zoom (LT)\n"
                        "Space = jump/roll (A) | R = reload (B) | Q = weapon (X) | E = vehicle (Y) | 1 / 3 = radio (LB / RB)\n"
                        "Enter = Start | Tab = Back | Esc = quit | Arrows = D-pad | F / C = stick press   (remap: F4 > Input)");
    ImGui::SeparatorText("Diagnostics");
    if (ImGui::Checkbox("Show FPS", &show_fps_))
      rex::cvar::SetFlagByName("edf_show_fps", show_fps_ ? "true" : "false");
    if (ImGui::Checkbox("Log frame-time statistics", &frametime_log_))
      rex::cvar::SetFlagByName("edf_frametime_log", frametime_log_ ? "true" : "false");
    if (ImGui::Checkbox("Verbose input tracing", &trace_input_))
      rex::cvar::SetFlagByName("edf_trace_input", trace_input_ ? "true" : "false");
    ImGui::Spacing();
    if (restart_) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", str::kRestartNote);
    if (ImGui::Button("Save", ImVec2(120, 0))) { Save(); saved_ = true; }
    ImGui::SameLine();
    if (ImGui::Button("Save and restart", ImVec2(150, 0))) {
      Save();
      saved_ = true;
      restart_failed_ = !on_restart_ || !on_restart_();
    }
    ImGui::SameLine(); if (ImGui::Button("Close", ImVec2(120, 0))) open = false;
    if (saved_) { ImGui::SameLine(); ImGui::TextDisabled("saved to %s", config_path_.filename().string().c_str()); }
    if (restart_failed_) ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1), "Could not restart the game: %s", SDL_GetError());
    ImGui::End();
    if (!open) Close();
  }

 private:
  struct Preset { const char* name; int w, h; };
  static constexpr std::array<Preset, 10> kPresets{{
      {"1280 x 720 (16:9)", 1280, 720}, {"1600 x 900 (16:9)", 1600, 900},
      {"1920 x 1080 (16:9)", 1920, 1080}, {"2560 x 1080 (21:9)", 2560, 1080},
      {"2560 x 1440 (16:9)", 2560, 1440}, {"3440 x 1440 (21:9)", 3440, 1440},
      {"3840 x 1600 (24:10)", 3840, 1600}, {"3840 x 2160 (16:9)", 3840, 2160},
      {"5120 x 1440 (32:9)", 5120, 1440}, {"Custom", 0, 0}}};
  static constexpr int kFpsCaps[4] = {0, 30, 60, 120};
  static constexpr int kRefresh[4] = {60, 30, 120, 144};

  static std::string GetStr(const char* name, const char* fallback) {
    std::string value = rex::cvar::GetFlagByName(name);
    return value.empty() ? std::string(fallback) : value;
  }
  static bool GetBool(const char* name, bool fallback) {
    const std::string value = GetStr(name, fallback ? "true" : "false");
    return value == "true" || value == "1";
  }
  static int GetInt(const char* name, int fallback) {
    const std::string value = GetStr(name, ""); return value.empty() ? fallback : std::atoi(value.c_str());
  }
  static double GetDouble(const char* name, double fallback) {
    const std::string value = GetStr(name, ""); return value.empty() ? fallback : std::atof(value.c_str());
  }
  static void HelpMarker(const char* text) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip(); ImGui::PushTextWrapPos(360.0f); ImGui::TextUnformatted(text);
      ImGui::PopTextWrapPos(); ImGui::EndTooltip();
    }
  }
  static std::filesystem::path ExeDir() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return length ? std::filesystem::path(std::wstring(buffer, length)).parent_path() : std::filesystem::current_path();
#else
    std::error_code error; const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::current_path() : path.parent_path();
#endif
  }
  static bool HasFidelityFx() {
    static const bool available = std::filesystem::exists(ExeDir() / "amd_fidelityfx_dx12.dll");
    return available;
  }

 public:
  static void ApplyVideoMode(int width, int height) {
    const auto mode = GuestVideoMode(width, height, REXCVAR_GET(edf_aspect));
    width = mode.width; height = mode.height;
    rex::cvar::SetFlagByName("video_mode_width", std::to_string(width));
    rex::cvar::SetFlagByName("video_mode_height", std::to_string(height));
  }

 private:

  void ApplyDisplayMode() {
    rex::cvar::SetFlagByName("edf_display_mode", display_mode_ == 1 ? "borderless" : "windowed");
    rex::cvar::SetFlagByName("fullscreen", display_mode_ == 1 ? "true" : "false");  // hot-reload: ReXApp toggles the window
  }
  void Save() {
    int w = res_index_ == (int)kPresets.size() - 1 ? custom_w_ : kPresets[res_index_].w;
    int h = res_index_ == (int)kPresets.size() - 1 ? custom_h_ : kPresets[res_index_].h;
    if (w >= 640 && h >= 480) {
      rex::cvar::SetFlagByName("window_width", std::to_string(w)); rex::cvar::SetFlagByName("window_height", std::to_string(h));
      ApplyVideoMode(w, h);
    }
    std::string s = std::to_string(ResolutionScaleValue(scale_index_));
    rex::cvar::SetFlagByName("draw_resolution_scale_x", s); rex::cvar::SetFlagByName("draw_resolution_scale_y", s);
    rex::cvar::SetFlagByName("mnk_mode", mnk_ ? "true" : "false");
    SaveUserConfig(config_path_);
  }

  std::filesystem::path config_path_; std::string pad_name_; std::function<void()> on_close_;
  std::function<bool()> on_restart_;
  int display_mode_ = 0, res_index_ = 0, custom_w_ = 1280, custom_h_ = 720, scale_index_ = 0, fps_cap_index_ = 0;
  bool vsync_ = true, mute_ = false, mnk_ = false, mnk_mouse_ = true, rumble_ = true;
  bool restart_ = false, saved_ = false, restart_failed_ = false;
  bool show_fps_ = false, frametime_log_ = false, trace_input_ = false;
  float mnk_sens_ = 1.0f;
  int af_index_ = 4, fxaa_index_ = 0, aspect_index_ = 0, upscale_index_ = 0, refresh_index_ = 0;
  bool msaa_ = true, dither_ = false, async_shaders_ = true;
  float cas_sharp_ = 0.0f, fsr_sharp_ = 0.2f;
};

}  // namespace edf
