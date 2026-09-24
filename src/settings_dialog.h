// EDF 2017 PC - the F1 settings menu.
//
// A pause menu (the app pauses the game while it is open, see pause_menu.h and
// Edf2017App::OpenSettings) with a sidebar of sections: Display, Graphics, Performance,
// Controls, Audio and Advanced. Settings the running game reads live are applied at once;
// the ones it reads only at startup carry a "restart" badge and are staged instead of
// written (settings_logic.h RestartTracker), then saved to the config file, so the bottom
// bar can say how many changes a restart would apply. Display-mode and window-size
// changes ask to be kept and revert on their own after 10 seconds.
//
// Everything is drawn over the frozen game by the native presenter's ImGui overlay, in
// the EDF theme of menu_ui.h, scaled from the window height, and navigable with a
// controller (menu_gamepad.h): LB/RB switch sections, B or Start resume.
#pragma once
#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdlib>
#include <filesystem>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "controller_logic.h"
#include "keybind_logic.h"
#include "launcher.h"
#include "manual_reload_logic.h"
#include "menu_gamepad.h"
#include "menu_ui.h"
#include "pause_menu.h"
#include "settings_logic.h"

REXCVAR_DECLARE(bool, fullscreen);
REXCVAR_DECLARE(int32_t, window_width);
REXCVAR_DECLARE(int32_t, window_height);
REXCVAR_DECLARE(int32_t, video_mode_width);
REXCVAR_DECLARE(int32_t, video_mode_height);
REXCVAR_DECLARE(std::string, edf_aspect);

namespace edf {

class SettingsDialog final : public rex::ui::ImGuiDialog {
 public:
  // What the menu needs from the app. Every member may be empty.
  struct Hooks {
    std::function<void()> on_close;                 // after the menu is gone (resume, boot)
    std::function<bool()> on_restart;               // relaunch the game; false if it failed
    std::function<float()> physical_height;         // the window's height in physical pixels
    std::function<void(int, int)> resize_window;    // live resize (windowed mode only)
    std::function<bool()> game_running;             // false before boot (--settings)
    std::function<bool()> user_mute;                // the player's mute setting
    std::function<void(bool)> set_user_mute;        // ...changed from the Audio section
    std::function<bool()> paused;                   // the game is paused under the menu
    std::string version;                            // build identity, shown under the sections
    std::function<std::string()> fsr_unavailable;   // why FSR cannot run ("" when it can)
  };

  SettingsDialog(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path, Hooks hooks,
                 bool opened_by_pad = false)
      : ImGuiDialog(drawer), config_path_(std::move(config_path)), hooks_(std::move(hooks)),
        focus_request_(opened_by_pad) {
    const auto [w, h] = CurrentWindowSize();
    custom_w_ = w;
    custom_h_ = h;
    render_custom_w_ = GetInt(Pending("edf_native_render_width"), 0);
    render_custom_h_ = GetInt(Pending("edf_native_render_height"), 0);
    if (!render_custom_w_ || !render_custom_h_) { render_custom_w_ = 1920; render_custom_h_ = 1080; }
    gamepad_.Reset();
    ui::RawPads::StopRumble();  // a paused game would otherwise leave the motors running
  }
  static SettingsDialog*& Current() { static SettingsDialog* s = nullptr; return s; }
  void Dismiss() { Close(); }

  // Escape from the app's key handler: close, unless ImGui has a popup open or a control
  // active that Escape should cancel first.
  bool WantsEscapeToClose() const { return !popup_or_active_ && !capturing(); }

  // Restart-only changes staged since launch. Process-wide: they outlive the menu, so
  // closing and reopening it still shows them, and the config file keeps them.
  static settings::RestartTracker& Staged() {
    static settings::RestartTracker tracker;
    return tracker;
  }

  // ---- Rebind capture -------------------------------------------------------------
  // Capture runs off the app's key events rather than ImGui's, for two reasons: the
  // events carry VirtualKey values, which are already the vocabulary the keybind_*
  // cvars are written in, and routing them here lets Escape cancel a capture instead of
  // closing the menu (Edf2017App::OnKeyDown) and lets F1/F2 be bound like any other key.
  // A controller-remap capture (pad_capture_target_) waits for a controller button instead;
  // while it runs every key is swallowed too, and Escape cancels it.
  bool capturing() const { return !capture_cvar_.empty() || pad_capture_target_ >= 0; }

  // Returns true if the key was consumed. `key` is a name from the SDK's key table, or
  // empty for a key it does not know; kNone/unknown keys are swallowed so a stray
  // modifier press does not end the capture.
  bool FeedCapturedKey(const std::string& key, bool shift, bool ctrl, bool alt, bool cancel) {
    if (!capturing()) return false;
    if (pad_capture_target_ >= 0) {
      if (cancel) pad_capture_target_ = -1;
      return true;
    }
    if (cancel) { capture_cvar_.clear(); return true; }
    if (key.empty() || key == "Shift" || key == "Control" || key == "Alt") return true;
    const std::string token = FormatBindToken(shift, ctrl, alt, key);
    const std::string current = rex::cvar::GetFlagByName(capture_cvar_);
    rex::cvar::SetFlagByName(capture_cvar_, capture_replace_ ? token : AddBindAlternative(current, token));
    capture_cvar_.clear();
    return true;
  }

  static void ApplyVideoMode(int width, int height) {
    const auto mode = GuestVideoMode(width, height, REXCVAR_GET(edf_aspect));
    rex::cvar::SetFlagByName("video_mode_width", std::to_string(mode.width));
    rex::cvar::SetFlagByName("video_mode_height", std::to_string(mode.height));
  }

 protected:
  void OnClose() override {
    // Closing with a display change still unconfirmed takes it back, as Windows does.
    if (revert_.active()) { revert_.Cancel(); if (revert_action_) revert_action_(); }
    gamepad_.Release(GetIO());
    GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    if (Current() == this) Current() = nullptr;
    Save();
    if (hooks_.on_close) { auto f = std::move(hooks_.on_close); hooks_.on_close = nullptr; f(); }
  }

  void OnDraw(ImGuiIO& io) override {
    const float physical = hooks_.physical_height ? hooks_.physical_height() : 0.0f;
    metrics_ = ui::ComputeMetrics(io.DisplaySize.y, physical, float(GetDouble(Get("edf_menu_scale"), 1.0)));
    const float s = metrics_.scale;
    ui::ScopedMenuStyle style(s);
    ui::ScopedFont body_font(ui::FontRole::kBody, metrics_);
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // While a remap capture waits for a button, the pad drives the capture, not the menu.
    const auto pad = gamepad_.Poll(io, pad_capture_target_ < 0);
    pad_connected_ = pad.connected;
    last_pad_raw_ = pad.raw;
    if (pad_capture_target_ >= 0 && section_ != kControlsSection) pad_capture_target_ = -1;
    if (pad_capture_target_ >= 0) UpdatePadCapture(pad.raw);
    bool close = false;
    if (!capturing() && !revert_.active()) {
      if (pad.pressed & menu::kPadStart) close = true;
      if ((pad.pressed & menu::kPadB) && !popup_or_active_) close = true;
      if (pad.pressed & (menu::kPadLeftShoulder | menu::kPadRightShoulder)) {
        const int step = (pad.pressed & menu::kPadRightShoulder) ? 1 : -1;
        section_ = (section_ + step + kSectionCount) % kSectionCount;
        focus_request_ = true;
      }
    }

    ui::DrawBackdrop(io.DisplaySize);
    const ImVec2 size(std::min(1180.0f * s, io.DisplaySize.x * 0.94f), std::min(780.0f * s, io.DisplaySize.y * 0.92f));
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::Begin("##edf_settings", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    focus_help_.clear();
    DrawHeader();
    const float footer_h = ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y * 2.0f;
    const float body_h = std::max(100.0f, ImGui::GetContentRegionAvail().y - footer_h);
    DrawSidebar(body_h);
    ImGui::SameLine(0, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f * s, 12.0f * s));
    ImGui::BeginChild("##content", ImVec2(0, body_h), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    switch (section_) {
      case 0: DrawDisplay(); break;
      case 1: DrawGraphics(); break;
      case 2: DrawPerformance(); break;
      case 3: DrawControls(); break;
      case 4: DrawAudio(); break;
      default: DrawAdvanced(); break;
    }
    ImGui::EndChild();
    if (DrawFooter()) close = true;
    popup_or_active_ = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) || ImGui::IsAnyItemActive();
    ImGui::End();
    DrawRevertModal();
    if (close) Close();
  }

 private:
  static constexpr int kSectionCount = 6;
  static constexpr int kControlsSection = 3;
  struct Section { const char* icon; const char* label; };
  static constexpr std::array<Section, kSectionCount> kSections{{
      {ui::icon::kDisplay, "Display"}, {ui::icon::kGraphics, "Graphics"}, {ui::icon::kPerformance, "Performance"},
      {ui::icon::kControls, "Controls"}, {ui::icon::kAudio, "Audio"}, {ui::icon::kAdvanced, "Advanced"}}};
  struct Preset { const char* name; int w, h; };
  static constexpr std::array<Preset, 10> kWindowPresets{{
      {"1280 x 720 (16:9)", 1280, 720}, {"1600 x 900 (16:9)", 1600, 900},
      {"1920 x 1080 (16:9)", 1920, 1080}, {"2560 x 1080 (21:9)", 2560, 1080},
      {"2560 x 1440 (16:9)", 2560, 1440}, {"3440 x 1440 (21:9)", 3440, 1440},
      {"3840 x 1600 (24:10)", 3840, 1600}, {"3840 x 2160 (16:9)", 3840, 2160},
      {"5120 x 1440 (32:9)", 5120, 1440}, {"Custom", 0, 0}}};
  // Render resolution: a line count in the window's shape (width 0), the window's own
  // size (0, -1), or a fixed size (Custom). native_display_layout.h resolves them.
  static constexpr std::array<Preset, 8> kRenderPresets{{
      {"Original (720 lines)", 0, 0}, {"900 lines", 0, 900}, {"1080 lines", 0, 1080},
      {"1440 lines", 0, 1440}, {"1800 lines", 0, 1800}, {"2160 lines", 0, 2160},
      {"Match window", 0, -1}, {"Custom (fixed size)", 0, 0}}};

  // ---- cvar access --------------------------------------------------------------------
  static std::string Get(std::string_view name) { return rex::cvar::GetFlagByName(name); }
  static bool Exists(std::string_view name) { return rex::cvar::GetFlagInfo(name) != nullptr; }
  static bool GetBool(const std::string& value) { return value == "true" || value == "1"; }
  static int GetInt(const std::string& value, int fallback) { return value.empty() ? fallback : std::atoi(value.c_str()); }
  static double GetDouble(const std::string& value, double fallback) {
    return value.empty() ? fallback : std::atof(value.c_str());
  }
  static std::string Default(std::string_view name) {
    const auto* info = rex::cvar::GetFlagInfo(name);
    return info ? info->default_value : std::string();
  }
  static bool NeedsRestart(std::string_view name) {
    const auto* info = rex::cvar::GetFlagInfo(name);
    return settings::NeedsRestart(name, info ? std::string_view(info->description) : std::string_view());
  }
  // The value the next launch will run with.
  static std::string Pending(std::string_view name) { return Staged().Pending(name, &Get); }
  // One setting, one or more cvars: live ones are written now, restart-only ones staged
  // (and unstaged again when set back to what is running).
  static void Apply(const std::string& setting, std::vector<std::pair<std::string, std::string>> values) {
    const bool restart = std::any_of(values.begin(), values.end(), [](const auto& v) { return NeedsRestart(v.first); });
    if (!restart) {
      for (const auto& [name, value] : values) rex::cvar::SetFlagByName(name, value);
      return;
    }
    const bool unchanged =
        std::all_of(values.begin(), values.end(), [](const auto& v) { return Get(v.first) == v.second; });
    if (unchanged) Staged().Unstage(setting);
    else Staged().Stage(setting, std::move(values));
  }
  static void ApplyOne(const std::string& cvar, const std::string& value) { Apply(cvar, {{cvar, value}}); }

  std::pair<int, int> CurrentWindowSize() const {
    const int w = GetInt(Pending("window_width"), 0), h = GetInt(Pending("window_height"), 0);
    if (w > 0 && h > 0) return {w, h};
    return {GetInt(Pending("video_mode_width"), 1280), GetInt(Pending("video_mode_height"), 720)};
  }

  // ---- Rows ---------------------------------------------------------------------------
  // Label on the left, control on the right; one row is one group, so hovering anywhere
  // on it shows its help, and focusing its control with a controller shows the help in
  // the footer. A restart-only setting carries a badge, filled while a change is staged.
  void BeginRow(const char* label, const char* setting = nullptr) {
    ImGui::PushID(label);
    ImGui::BeginGroup();
    const float start_x = ImGui::GetCursorPosX();
    const float label_w = ImGui::GetContentRegionAvail().x * 0.42f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (setting) {
      ImGui::SameLine();
      ui::ScopedFont small_font(ui::FontRole::kSmall, metrics_);
      const bool pending = Staged().Differs(setting, &Get);
      ui::Badge(pending ? "RESTART PENDING" : "RESTART", ui::color::kOrange, pending, metrics_.scale);
    }
    // Controls line up in one column; a label too long for it pushes only its own control.
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), start_x + label_w));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (focus_request_) {
      ImGui::SetKeyboardFocusHere();
      focus_request_ = false;
    }
  }
  void EndRow(const char* help) {
    if (help && ImGui::IsItemFocused() && ImGui::GetIO().NavVisible) focus_help_ = help;
    ImGui::EndGroup();
    if (help && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::BeginTooltip();
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
      ImGui::TextUnformatted(help);
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
    ImGui::PopID();
  }

  // A combo whose items can be individually disabled. Returns true on a new selection.
  bool Combo(const char* id, int* index, const std::vector<const char*>& items,
             const std::vector<bool>& disabled = {}, const char* preview_override = nullptr) {
    bool changed = false;
    const char* preview = preview_override ? preview_override
                          : *index >= 0 && *index < int(items.size()) ? items[size_t(*index)] : "";
    if (ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLarge)) {
      for (int i = 0; i < int(items.size()); ++i) {
        const bool off = i < int(disabled.size()) && disabled[size_t(i)];
        const bool selected = i == *index;
        if (ImGui::Selectable(items[size_t(i)], selected, off ? ImGuiSelectableFlags_Disabled : 0)) {
          changed = *index != i;
          *index = i;
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    return changed;
  }
  bool CheckboxRow(const char* label, const char* cvar, const char* help, const char* setting = nullptr) {
    BeginRow(label, setting ? setting : (NeedsRestart(cvar) ? cvar : nullptr));
    bool value = GetBool(Pending(cvar));
    const bool changed = ImGui::Checkbox("##value", &value);
    if (changed) ApplyOne(cvar, value ? "true" : "false");
    EndRow(help);
    return changed;
  }

  // ---- Header, sidebar, footer ---------------------------------------------------------
  void DrawHeader() {
    const float s = metrics_.scale;
    const float right_edge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    {
      ui::ScopedFont title(ui::FontRole::kTitle, metrics_);
      ImGui::PushStyleColor(ImGuiCol_Text, ui::color::kGreen);
      ImGui::TextUnformatted("EDF2027");
      ImGui::PopStyleColor();
      ImGui::SameLine();
      ImGui::TextDisabled("/");
      ImGui::SameLine();
      ImGui::TextUnformatted("SETTINGS");
    }
    const bool paused = hooks_.paused && hooks_.paused();
    const char* status = paused ? "GAME PAUSED" : "GAME RUNNING";
    ui::ScopedFont small_font(ui::FontRole::kSmall, metrics_);
    const float chip_w = ImGui::CalcTextSize(status).x + ImGui::CalcTextSize(ui::icon::kPause).x + 34.0f * s;
    ImGui::SameLine(right_edge - chip_w);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f * s);
    std::string chip = std::string(paused ? ui::icon::kPause : ui::icon::kResume) + "  " + status;
    ui::Badge(chip.c_str(), paused ? ui::color::kGreen : ui::color::kTextDim, paused, s);
    ImGui::Dummy(ImVec2(0, 2.0f * s));
  }

  void DrawSidebar(float height) {
    const float s = metrics_.scale;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::color::kSidebar);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 10.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4.0f * s));
    ImGui::BeginChild("##sidebar", ImVec2(250.0f * s, height),
                      ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
    const float entry_h = std::max(ImGui::GetFrameHeight() * 1.45f, 48.0f * s);  // generous hit targets
    for (int i = 0; i < kSectionCount; ++i) {
      ImGui::PushID(i);
      if (ui::SidebarEntry("##section", kSections[size_t(i)].icon, kSections[size_t(i)].label, section_ == i, entry_h,
                           s))
        section_ = i;
      ImGui::PopID();
    }
    {
      // The build a bug report should name, at the foot of the section list.
      ui::ScopedFont small_font(ui::FontRole::kSmall, metrics_);
      const float lines = (pad_connected_ ? 2.0f : 0.0f) + (hooks_.version.empty() ? 0.0f : 1.0f);
      if (lines > 0)
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), height - ImGui::GetTextLineHeightWithSpacing() * lines -
                                                                  ImGui::GetTextLineHeight() * 0.4f));
      if (pad_connected_) {
        ImGui::SetCursorPosX(18.0f * s);
        ImGui::TextDisabled("LB / RB  switch section");
        ImGui::SetCursorPosX(18.0f * s);
        ImGui::TextDisabled("B / Start  resume");
      }
      if (!hooks_.version.empty()) {
        ImGui::SetCursorPosX(18.0f * s);
        ImGui::TextDisabled("v%s", hooks_.version.c_str());
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
  }

  // Returns true when Resume was chosen.
  bool DrawFooter() {
    const float s = metrics_.scale;
    ImGui::Separator();
    const float right_edge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    bool resume = false;
    const float button_h = std::max(ImGui::GetFrameHeight() * 1.2f, 44.0f * s);
    const std::string resume_label = std::string(ui::icon::kResume) + "  Resume";
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.235f, 0.878f, 0.627f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::color::kGreen);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::color::kGreen);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.03f, 0.07f, 0.05f, 1.0f));
    if (ImGui::Button(resume_label.c_str(), ImVec2(190.0f * s, button_h))) resume = true;
    ImGui::PopStyleColor(4);
    ImGui::SameLine();
    const std::string reset_label = std::string(ui::icon::kReset) + "  Reset section";
    if (ImGui::Button(reset_label.c_str(), ImVec2(0, button_h))) ResetSection(section_);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
      ImGui::SetTooltip("Put every setting on this page back to its default.");

    // Right side: restart status, or the focused setting's help, or where settings go.
    const int pending = Staged().Count(&Get);
    ImGui::SameLine();
    const float right_x = ImGui::GetCursorPosX();
    const float avail = right_edge - right_x;
    if (pending > 0) {
      const std::string text = std::string(ui::icon::kWarning) + "  Restart to apply " + std::to_string(pending) +
                               (pending == 1 ? " change" : " changes");
      const std::string restart_label = std::string(ui::icon::kRestart) + "  Restart now";
      const float buttons_w = ImGui::CalcTextSize(restart_label.c_str()).x + ImGui::CalcTextSize("Discard").x +
                              ImGui::GetStyle().FramePadding.x * 4 + ImGui::GetStyle().ItemSpacing.x * 2;
      const float text_w = ImGui::CalcTextSize(text.c_str()).x;
      ImGui::SetCursorPosX(std::max(right_x, right_edge - buttons_w - text_w - ImGui::GetStyle().ItemSpacing.x));
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (button_h - ImGui::GetTextLineHeight()) * 0.5f);
      ImGui::TextColored(ui::color::kOrange, "%s", text.c_str());
      ImGui::SameLine();
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (button_h - ImGui::GetTextLineHeight()) * 0.5f);
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.56f, 0.18f, 0.85f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::color::kOrange);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::color::kOrange);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.05f, 0.02f, 1.0f));
      ImGui::BeginDisabled(!hooks_.on_restart);
      if (ImGui::Button(restart_label.c_str(), ImVec2(0, button_h))) {
        Save();
        restart_failed_ = !hooks_.on_restart || !hooks_.on_restart();
      }
      ImGui::EndDisabled();
      ImGui::PopStyleColor(4);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Save everything and start the game again with the new settings.");
      ImGui::SameLine();
      if (ImGui::Button("Discard", ImVec2(0, button_h))) Staged().Clear();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Forget the changes that need a restart and keep the current ones.");
    } else {
      ui::ScopedFont small_font(ui::FontRole::kSmall, metrics_);
      const std::string note = focus_help_.empty() ? std::string("Changes apply immediately and are saved when you resume.")
                                                   : focus_help_;
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (button_h - ImGui::GetTextLineHeight()) * 0.5f);
      ImGui::PushTextWrapPos(right_x + avail);
      ImGui::TextDisabled("%s", note.c_str());
      ImGui::PopTextWrapPos();
    }
    if (restart_failed_) {
      ui::ScopedFont small_font(ui::FontRole::kSmall, metrics_);
      ImGui::TextColored(ui::color::kRed, "Could not restart the game: %s", SDL_GetError());
    }
    return resume;
  }

  // ---- Display -------------------------------------------------------------------------
  void DrawDisplay() {
    ui::SectionHeading("Display", metrics_);
    BeginRow("Display mode");
    int mode = Get("edf_display_mode") == "borderless" ? 1 : 0;
    if (Combo("##mode", &mode, {"Windowed", "Borderless fullscreen"})) {
      const int previous = mode == 1 ? 0 : 1;
      SetDisplayMode(mode);
      StartRevert("Display mode changed.", [this, previous] { SetDisplayMode(previous); });
    }
    EndRow("Windowed runs in a normal window. Borderless fullscreen covers the whole screen without changing "
           "the monitor's resolution. You will be asked to keep the change.");

    BeginRow("Window size", "window_size");
    const auto [w, h] = CurrentWindowSize();
    int index = int(kWindowPresets.size()) - 1;
    for (size_t i = 0; i + 1 < kWindowPresets.size(); ++i)
      if (kWindowPresets[i].w == w && kWindowPresets[i].h == h) index = int(i);
    std::vector<const char*> names;
    for (const auto& p : kWindowPresets) names.push_back(p.name);
    if (Combo("##window", &index, names)) {
      if (index + 1 < int(kWindowPresets.size())) SetWindowSize(kWindowPresets[size_t(index)].w, kWindowPresets[size_t(index)].h);
      else { custom_w_ = w; custom_h_ = h; custom_window_ = true; }
    }
    EndRow("The window's size. In windowed mode the window resizes now and the picture is fitted into it; the game "
           "renders in the new shape after a restart.");
    if (index == int(kWindowPresets.size()) - 1 || custom_window_) {
      BeginRow("Custom size");
      const float field = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) * 0.36f;
      ImGui::SetNextItemWidth(field);
      ImGui::InputInt("##cw", &custom_w_, 0, 0);
      ImGui::SameLine();
      ImGui::TextDisabled("x");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(field);
      ImGui::InputInt("##ch", &custom_h_, 0, 0);
      ImGui::SameLine();
      const bool valid = custom_w_ >= 640 && custom_h_ >= 480 && custom_w_ <= 16384 && custom_h_ <= 16384;
      ImGui::BeginDisabled(!valid || (custom_w_ == w && custom_h_ == h));
      if (ImGui::Button("Apply", ImVec2(-FLT_MIN, 0))) SetWindowSize(custom_w_, custom_h_);
      ImGui::EndDisabled();
      EndRow("Any size from 640 x 480.");
    }

    BeginRow("Aspect ratio", "edf_aspect");
    int aspect = AspectIndex(Pending("edf_aspect"));
    if (Combo("##aspect", &aspect, {"Fill window (Hor+ / Vert+)", "Fill window (Hor+ only)", "16:9 letterbox",
                                    "16:9 stretched"})) {
      const auto value = std::string(AspectValue(aspect));
      Apply("edf_aspect", {{"edf_aspect", value}, {"present_letterbox", PresentLetterbox(value) ? "true" : "false"}});
    }
    EndRow("Fill window renders in the window's shape: wider screens (21:9, 32:9) see more at the sides, narrower "
           "ones (16:10, 4:3) see more above and below, so nothing of the 16:9 view is lost. Hor+ only keeps the "
           "vertical view everywhere, so narrower screens lose the sides. The 16:9 modes keep the console's shape, "
           "with black bars or stretched.");

    if (Exists("edf_hud_safe_area")) {
      BeginRow("HUD and menus");
      int hud = Get("edf_hud_safe_area") == "full" ? 1 : 0;
      if (Combo("##hud", &hud, {"16:9 area", "Full frame (stretched)"}))
        rex::cvar::SetFlagByName("edf_hud_safe_area", hud == 1 ? "full" : "16:9");
      EndRow("On a screen that is not 16:9: the 16:9 area keeps the HUD, menus, text and videos at their own shape "
             "in the middle of the screen; full frame spreads them over the whole screen, stretched. Identical on a "
             "16:9 screen. Applies immediately.");
    }

    if (Exists("edf_present_filter")) {
      BeginRow("Scaling filter");
      int filter = Get("edf_present_filter") == "bilinear" ? 1 : 0;
      if (Combo("##scaling", &filter, {"Sharp (automatic)", "Bilinear"}))
        rex::cvar::SetFlagByName("edf_present_filter", filter == 1 ? "bilinear" : "auto");
      EndRow("How the picture is fitted to the window when the render resolution differs from it. Sharp keeps "
             "whole-number enlargements pixel-exact and averages a larger render down cleanly (supersampling). "
             "Applies immediately.");
    }

    BeginRow("Menu size");
    float scale = float(GetDouble(Get("edf_menu_scale"), 1.0)) * 100.0f;
    if (ImGui::SliderFloat("##menu_scale", &scale, 50.0f, 200.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
      pending_menu_scale_ = scale;
    // Apply on release: resizing the menu under the slider while dragging would move it.
    if (ImGui::IsItemDeactivatedAfterEdit() && pending_menu_scale_ > 0) {
      rex::cvar::SetFlagByName("edf_menu_scale", std::to_string(pending_menu_scale_ / 100.0f));
      pending_menu_scale_ = 0;
    }
    EndRow("How large this menu and the performance overlay are. 100% is sized for the window's height.");
  }

  void SetDisplayMode(int mode) {
    rex::cvar::SetFlagByName("edf_display_mode", mode == 1 ? "borderless" : "windowed");
    rex::cvar::SetFlagByName("fullscreen", mode == 1 ? "true" : "false");  // hot reload: ReXApp toggles the window
  }
  void SetWindowSize(int width, int height) {
    const auto [old_w, old_h] = CurrentWindowSize();
    const bool had_stage = Staged().IsStaged("window_size");
    const auto old_values = Staged().Values("window_size");
    const auto mode = GuestVideoMode(width, height, Pending("edf_aspect"));
    Apply("window_size", {{"window_width", std::to_string(width)}, {"window_height", std::to_string(height)},
                          {"video_mode_width", std::to_string(mode.width)},
                          {"video_mode_height", std::to_string(mode.height)}});
    custom_window_ = false;
    // Borderless fullscreen shows no change until the restart, so there is nothing to
    // confirm; in a window the resize happens now and asks to be kept.
    if (Get("edf_display_mode") == "borderless" || !hooks_.resize_window) return;
    hooks_.resize_window(width, height);
    StartRevert("Window size changed.", [this, old_w, old_h, had_stage, old_values] {
      if (had_stage) Staged().Stage("window_size", old_values);
      else Staged().Unstage("window_size");
      hooks_.resize_window(old_w, old_h);
    });
  }

  // ---- Graphics ------------------------------------------------------------------------
  void DrawGraphics() {
    ui::SectionHeading("Graphics", metrics_);
    const bool fsr = Exists("edf_native_fsr");
    DrawPresetRow(fsr);

    BeginRow("Renderer", "edf_native_renderer");
    int renderer = settings::RendererIndex(Pending("edf_native_renderer"));
    const std::string custom = "Custom (" + Pending("edf_native_renderer") + ")";
    if (Combo("##renderer", &renderer, {"Native", "Classic"}, {}, renderer < 0 ? custom.c_str() : nullptr))
      ApplyOne("edf_native_renderer", std::string(settings::RendererValue(renderer)));
    EndRow("Native draws the whole frame with this port's own Direct3D 12 renderer, which is faster and needed "
           "for the upscaler. Classic replays the game's original draw calls, for comparison or if Native shows a "
           "problem.");

    BeginRow("Graphics API", "scene_backend");
    int api = Pending("edf_native_scene_backend") == "d3d11" ? 1 : 0;
    if (Combo("##api", &api, {"Direct3D 12", "Direct3D 11 (fallback)"})) {
      const std::string value = api == 1 ? "d3d11" : "d3d12";
      Apply("scene_backend", {{"edf_native_scene_backend", value}, {"edf_native_backend", value}});
    }
    EndRow("Direct3D 12 is the default and the only API the Native renderer and higher frame rates run on. "
           "Direct3D 11 is a fallback that uses the Classic renderer.");

    BeginRow("Render resolution", "render_size");
    const int rw = GetInt(Pending("edf_native_render_width"), 0), rh = GetInt(Pending("edf_native_render_height"), 0);
    int render = int(kRenderPresets.size()) - 1;
    for (size_t i = 0; i + 1 < kRenderPresets.size(); ++i)
      if (kRenderPresets[i].w == rw && kRenderPresets[i].h == rh) render = int(i);
    const auto [window_w, window_h] = CurrentWindowSize();
    // FSR upscaling decides the scene's resolution itself (a fraction of the window's),
    // so this setting stands aside while an upscaling mode is chosen.
    const bool fsr_upscaling = fsr && settings::NativeFsrUpscaleRatio(Pending("edf_native_fsr")) > 0.0f;
    const auto request = native::NativeRenderRequest(rw, rh, fsr_upscaling);
    const auto resolved = native::ResolveNativeRenderSize(request[0], request[1], window_w, window_h,
                                                          native::ParseNativeAspectMode(Pending("edf_aspect")));
    const auto [scene_w, scene_h] = settings::NativeFsrSceneSize(Pending("edf_native_fsr"), resolved.width, resolved.height);
    const std::string render_note =
        fsr_upscaling
            ? "Overridden by FSR upscaling: the 3D scene renders at " + std::to_string(scene_w) + " x " +
                  std::to_string(scene_h) + " and is upscaled to " + std::to_string(resolved.width) + " x " +
                  std::to_string(resolved.height) + " (the window). This choice applies again when upscaling is off. "
            : "Renders at " + std::to_string(resolved.width) + " x " + std::to_string(resolved.height) + " in a " +
                  std::to_string(window_w) + " x " + std::to_string(window_h) + " window" +
                  (resolved.clamped ? " (reduced to the largest size the game's memory allows)" : "") + ". ";
    std::vector<const char*> render_names;
    for (const auto& p : kRenderPresets) render_names.push_back(p.name);
    const std::string overridden = "Set by FSR (" + std::to_string(scene_w) + " x " + std::to_string(scene_h) + ")";
    ImGui::BeginDisabled(fsr_upscaling);
    if (Combo("##render", &render, render_names, {}, fsr_upscaling ? overridden.c_str() : nullptr)) {
      if (render + 1 < int(kRenderPresets.size()))
        SetRenderSize(kRenderPresets[size_t(render)].w, kRenderPresets[size_t(render)].h);
      else
        custom_render_ = true;
    }
    ImGui::EndDisabled();
    const std::string render_help = render_note +
        "The resolution the game is drawn at, in the window's shape unless a fixed size is chosen; the picture is "
        "then scaled to the window. Above the window's size it is supersampled, which looks smoother and costs GPU "
        "time and memory. Experimental.";
    EndRow(render_help.c_str());
    if (!fsr_upscaling && (render == int(kRenderPresets.size()) - 1 || custom_render_)) {
      BeginRow("Custom render size");
      const float field = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) * 0.36f;
      ImGui::SetNextItemWidth(field);
      ImGui::InputInt("##rw", &render_custom_w_, 0, 0);
      ImGui::SameLine();
      ImGui::TextDisabled("x");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(field);
      ImGui::InputInt("##rh", &render_custom_h_, 0, 0);
      ImGui::SameLine();
      const bool valid = render_custom_w_ > 0 && render_custom_h_ > 0 &&
                         ValidNativeRenderMode(render_custom_w_, render_custom_h_);
      ImGui::BeginDisabled(!valid);
      if (ImGui::Button("Apply", ImVec2(-FLT_MIN, 0))) SetRenderSize(render_custom_w_, render_custom_h_);
      ImGui::EndDisabled();
      EndRow(valid ? "640 to 8192 wide, 480 to 8192 high, up to 16.7 million pixels (5120 x 2880)."
                   : "Out of range: 640 to 8192 wide, 480 to 8192 high, up to 16.7 million pixels (5120 x 2880).");
    }

    if (fsr) DrawUpscalingRows();

    BeginRow("Scene anti-aliasing", "edf_native_msaa");
    const int samples = GetInt(Pending("edf_native_msaa"), 0);
    int msaa = samples == 1 ? 1 : samples == 2 ? 2 : samples == 4 ? 3 : 0;
    if (Combo("##msaa", &msaa, {"Game default (2x MSAA)", "Off", "2x MSAA", "4x MSAA"})) {
      constexpr int kSamples[]{0, 1, 2, 4};
      ApplyOne("edf_native_msaa", std::to_string(kSamples[msaa]));
    }
    EndRow("Smooths jagged edges of 3D geometry. 4x looks best and costs the most GPU time and memory. The HUD "
           "and menus are not affected.");

    BeginRow("Texture filtering");
    int af = std::clamp(GetInt(Get("edf_native_anisotropic_filtering"), -1) + 1, 0, 6);
    if (Combo("##af", &af, {"Game default", "Off", "Anisotropic 1x", "Anisotropic 2x", "Anisotropic 4x",
                            "Anisotropic 8x", "Anisotropic 16x"}))
      rex::cvar::SetFlagByName("edf_native_anisotropic_filtering", std::to_string(AnisotropicValue(af)));
    EndRow("Keeps ground and wall textures sharp at shallow angles. Applies immediately; 16x costs very little on "
           "modern GPUs.");

    DrawLegacyPresenter();
  }

  void DrawPresetRow(bool fsr) {
    settings::GraphicsState state;
    state.anisotropic = GetInt(Get("edf_native_anisotropic_filtering"), -1);
    state.msaa = GetInt(Pending("edf_native_msaa"), 0);
    if (fsr) {
      state.fsr = Pending("edf_native_fsr");
      state.sharpness = GetDouble(Pending("edf_native_fsr_sharpness"), 0.0);
    }
    const auto match = settings::MatchPreset(state, fsr);
    BeginRow("Quality preset");
    // Custom is a fifth segment that shows up only when nothing matches; it is a state,
    // not something to pick.
    const bool custom = match == settings::GraphicsPreset::kCustom;
    int index = int(match);
    const std::span<const char* const> labels(settings::kGraphicsPresetLabels.data(), custom ? 5 : 4);
    if (ui::SegmentedControl("##preset", &index, labels, metrics_.scale) && index < 4)
      ApplyPreset(settings::GraphicsPreset(index), fsr);
    EndRow(fsr ? "Sets texture filtering, anti-aliasing and upscaling together. Anything changed by hand shows "
                 "as Custom."
               : "Sets texture filtering and anti-aliasing together. Anything changed by hand shows as Custom.");
  }
  void ApplyPreset(settings::GraphicsPreset preset, bool fsr) {
    const auto values = settings::PresetValues(preset, fsr);
    rex::cvar::SetFlagByName("edf_native_anisotropic_filtering", std::to_string(values.anisotropic));
    ApplyOne("edf_native_msaa", std::to_string(values.msaa));
    if (fsr) {
      ApplyOne("edf_native_fsr", std::string(values.fsr));
      if (Exists("edf_native_fsr_sharpness")) ApplyOne("edf_native_fsr_sharpness", std::to_string(values.sharpness));
    }
  }

  // edf_native_fsr / edf_native_fsr_sharpness belong to the renderer; they are only
  // drawn when the running build defines them, and whether a change needs a restart is
  // read from the cvar's own description.
  void DrawUpscalingRows() {
    BeginRow("Upscaling / anti-aliasing", NeedsRestart("edf_native_fsr") ? "edf_native_fsr" : nullptr);
    int mode = settings::NativeFsrIndex(Pending("edf_native_fsr"));
    std::vector<const char*> labels(settings::kNativeFsrLabels.begin(), settings::kNativeFsrLabels.end());
    const bool native = settings::RendererIndex(Get("edf_native_renderer")) == 0;
    if (Combo("##fsr", &mode, labels)) ApplyOne("edf_native_fsr", std::string(settings::kNativeFsrValues[size_t(mode)]));
    EndRow(native ? "AMD FidelityFX Super Resolution. The quality modes draw the 3D scene at a lower resolution and "
                    "rebuild a sharp image at the window's resolution, for a higher frame rate; the HUD, menus and "
                    "text stay at full resolution. Native AA uses the same technique for anti-aliasing only. Needs "
                    "the Native renderer."
                  : "AMD FidelityFX Super Resolution. Needs the Native renderer, which is not the one running.");
    if (mode != 0) {
      // The effective sizes: what the scene is drawn at and what it is upscaled to.
      const std::string value(settings::kNativeFsrValues[size_t(mode)]);
      const auto [window_w, window_h] = CurrentWindowSize();
      const bool upscaling = settings::NativeFsrUpscaleRatio(value) > 0.0f;
      const auto request = native::NativeRenderRequest(GetInt(Pending("edf_native_render_width"), 0),
                                                       GetInt(Pending("edf_native_render_height"), 0), upscaling);
      const auto output = native::ResolveNativeRenderSize(request[0], request[1], window_w, window_h,
                                                          native::ParseNativeAspectMode(Pending("edf_aspect")));
      const auto [scene_w, scene_h] = settings::NativeFsrSceneSize(value, output.width, output.height);
      ui::ScopedFont small_font(ui::FontRole::kSmall, metrics_);
      if (upscaling)
        ImGui::TextDisabled("3D scene %d x %d, upscaled to %d x %d", scene_w, scene_h, output.width, output.height);
      else
        ImGui::TextDisabled("3D scene %d x %d, anti-aliased at full resolution", scene_w, scene_h);
    }
    if (mode != 0 && hooks_.fsr_unavailable) {
      // A missing or broken amd_fidelityfx_dx12.dll: the scene is resolved without FSR.
      if (const std::string why = hooks_.fsr_unavailable(); !why.empty()) {
        ui::ScopedFont small_font(ui::FontRole::kSmall, metrics_);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ui::color::kOrange, "%s  FSR is unavailable, so it is off: %s", ui::icon::kWarning, why.c_str());
        ImGui::PopTextWrapPos();
      }
    }
    if (Exists("edf_native_fsr_sharpness")) {
      BeginRow("Upscaler sharpness", NeedsRestart("edf_native_fsr_sharpness") ? "edf_native_fsr_sharpness" : nullptr);
      ImGui::BeginDisabled(mode == 0);
      float sharpness = float(GetDouble(Pending("edf_native_fsr_sharpness"), 0.0));
      if (ImGui::SliderFloat("##sharp", &sharpness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
        ApplyOne("edf_native_fsr_sharpness", std::to_string(sharpness));
      ImGui::EndDisabled();
      EndRow("How much the upscaled image is sharpened. 0 is off; higher values bring out detail but can add "
             "halos.");
    }
  }

  // The FidelityFX / FXAA options of the classic presenter. They were never implemented
  // for the native presenter, so they are shown only when the classic renderer or the
  // Direct3D 11 path is running, and stay read-only as before.
  void DrawLegacyPresenter() {
    const bool classic = settings::RendererIndex(Get("edf_native_renderer")) != 0 ||
                         Get("edf_native_scene_backend") == "d3d11";
    if (!classic) return;
    ImGui::Dummy(ImVec2(0, 6.0f * metrics_.scale));
    ui::SectionHeading("Classic presenter", metrics_);
    ImGui::BeginDisabled();
    BeginRow("Post anti-aliasing");
    const std::string post = Get("swap_post_effect");
    int fxaa = post == "fxaa" ? 1 : post == "fxaa_extreme" ? 2 : 0;
    Combo("##fxaa", &fxaa, {"Off", "FXAA", "FXAA extreme"});
    EndRow("Not implemented by this presenter yet.");
    BeginRow("Upscaling effect");
    int effect = UpscaleIndex(Get("present_effect"));
    Combo("##effect", &effect, {"Off (bilinear)", "CAS (sharpen)", "FSR 1.0", "FSR 2", "FSR 3"});
    EndRow("The SDK presenter's AMD FidelityFX effects. Not implemented by this presenter yet.");
    BeginRow("Output dithering");
    bool dither = GetBool(Get("present_dither"));
    ImGui::Checkbox("##dither", &dither);
    EndRow("Not implemented by this presenter yet.");
    ImGui::EndDisabled();
  }

  void SetRenderSize(int width, int height) {
    Apply("render_size", {{"edf_native_render_width", std::to_string(width)},
                          {"edf_native_render_height", std::to_string(height)}});
    custom_render_ = false;
  }

  // ---- Performance ---------------------------------------------------------------------
  void DrawPerformance() {
    ui::SectionHeading("Performance", metrics_);
    const bool d3d12 = Get("edf_native_scene_backend").starts_with("d3d12");
    BeginRow("Frame rate");
    const bool unlock = GetBool(Get("edf_native_unlock_framerate"));
    const int cap = GetInt(Get("edf_fps_cap"), 0);
    int index = settings::FrameRateIndex(unlock, cap);
    std::vector<const char*> labels;
    std::vector<bool> disabled;
    for (const auto& choice : settings::kFrameRateChoices) {
      labels.push_back(choice.label);
      disabled.push_back(choice.unlock && !d3d12);
    }
    const std::string custom = std::string("Custom (") + (unlock ? "unlocked" : "locked") +
                               (cap > 0 ? ", cap " + std::to_string(cap) : std::string()) + ")";
    if (Combo("##fps", &index, labels, disabled, index < 0 ? custom.c_str() : nullptr)) {
      const auto choice = settings::FrameRateAt(index);
      rex::cvar::SetFlagByName("edf_native_unlock_framerate", choice.unlock ? "true" : "false");
      rex::cvar::SetFlagByName("edf_fps_cap", std::to_string(choice.cap));
    }
    EndRow(d3d12 ? "The game always simulates at 60 Hz. Above 60, extra frames are drawn in between and motion "
                   "is smoothed, adding up to one frame of delay. With VSync on, the display's refresh rate is "
                   "the ceiling. Experimental; applies immediately."
                 : "Above 60 needs Direct3D 12 (Graphics > Graphics API).");

    BeginRow("VSync");
    bool vsync = Get("edf_native_vsync") != "false";
    if (ImGui::Checkbox("##vsync", &vsync)) rex::cvar::SetFlagByName("edf_native_vsync", vsync ? "true" : "false");
    EndRow("Waits for the display's refresh, which removes tearing. Off lets frames out as soon as they are "
           "ready: lowest delay, and a variable-refresh (G-Sync/FreeSync) display stays smooth.");

    if (Exists("edf_low_latency")) {
      BeginRow("Low latency");
      bool low_latency = GetBool(Get("edf_low_latency"));
      ImGui::BeginDisabled(!d3d12);
      if (ImGui::Checkbox("##low_latency", &low_latency))
        rex::cvar::SetFlagByName("edf_low_latency", low_latency ? "true" : "false");
      ImGui::EndDisabled();
      EndRow(d3d12 ? "Shows the newest frame as late as the display allows and, with VSync on, keeps the game at "
                     "most one frame ahead of the display instead of queueing several behind it. Mouse aim also "
                     "takes the newest movement. Less input delay; with VSync on and a frame rate above the "
                     "display's, it can cost frame rate when the GPU is the limit. Applies immediately."
                   : "Needs Direct3D 12 (Graphics > Graphics API).");
    }

    if (Exists("edf_frame_pacer_before_present")) {
      BeginRow("Frame pacing");
      int pacing = GetBool(Get("edf_frame_pacer_before_present")) ? 0 : 1;
      if (Combo("##pacing", &pacing, {"Even (steady frame times)", "Low latency"}))
        rex::cvar::SetFlagByName("edf_frame_pacer_before_present", pacing == 0 ? "true" : "false");
      EndRow("How a capped frame rate is held with VSync off. Even releases frames on a fixed rhythm; Low "
             "latency shows each frame as soon as its wait ends, up to one frame sooner but less even.");
    }

    BeginRow("CPU priority", "edf_native_thread_qos");
    int qos = std::clamp(GetInt(Pending("edf_native_thread_qos"), 2), 0, 2);
    if (Combo("##qos", &qos, {"Windows default", "High", "High, performance cores"}))
      ApplyOne("edf_native_thread_qos", std::to_string(qos));
    EndRow("How Windows schedules the game's two busiest threads. High stops Windows slowing them to save power; "
           "performance cores also keeps them off the efficiency cores of hybrid Intel CPUs. The default is the "
           "fastest.");

    BeginRow("Performance overlay");
    const bool shown = GetBool(Get("edf_show_fps"));
    int overlay = shown ? std::clamp(GetInt(Get("edf_perf_overlay_detail"), 1), 0, 2) + 1 : 0;
    if (Combo("##overlay", &overlay, {"Off", "Frame rate", "Frame rate and graph", "Frame rate, graph, CPU and GPU"})) {
      rex::cvar::SetFlagByName("edf_show_fps", overlay > 0 ? "true" : "false");
      if (overlay > 0) rex::cvar::SetFlagByName("edf_perf_overlay_detail", std::to_string(overlay - 1));
    }
    EndRow("A small readout in the top-left corner. F2 shows or hides it. GPU time needs GPU timing (Advanced) "
           "and updates every few seconds.");
  }

  // ---- Controls ------------------------------------------------------------------------
  void DrawControls() {
    ui::SectionHeading("Controller", metrics_);
    BeginRow("Controller API", Exists("input_backend") ? "input_backend" : nullptr);
    {
      const std::string backend = Pending("input_backend");
      int index = backend == "xinput" ? 1 : 0;
      const std::vector<const char*> items{"SDL (default)", "XInput"};
#if defined(_WIN32)
      const std::vector<bool> disabled{false, false};
#else
      const std::vector<bool> disabled{false, true};
#endif
      ImGui::BeginDisabled(!Exists("input_backend"));
      if (Combo("##backend", &index, items, disabled)) ApplyOne("input_backend", index == 1 ? "xinput" : "sdl");
      ImGui::EndDisabled();
    }
    EndRow("How the game talks to controllers. SDL handles most controllers (Xbox, PlayStation, Switch Pro and "
           "others). XInput talks to Xbox-compatible controllers directly, up to four. Both support plugging "
           "controllers in and out, vibration, remapping and the dead zones below. Takes effect after a restart.");

    BeginRow("Connected");
    DrawConnectedPads();
    EndRow("Controllers can be plugged in and out at any time. The first controller connected plays as player 1, "
           "the next as player 2, and each keeps its player until it is unplugged.");

    BeginRow("Open this menu with");
    const std::string chord = Get("edf_menu_pad_chord");
    int chord_index = int(menu::kChordOptions.size()) - 1;
    for (size_t i = 0; i < menu::kChordOptions.size(); ++i)
      if (chord == menu::kChordOptions[i].value) chord_index = int(i);
    std::vector<const char*> chord_labels;
    for (const auto& option : menu::kChordOptions) chord_labels.push_back(option.label);
    const std::string chord_custom = "Custom (" + chord + ")";
    const bool known = std::any_of(menu::kChordOptions.begin(), menu::kChordOptions.end(),
                                   [&](const auto& o) { return chord == o.value; });
    if (Combo("##chord", &chord_index, chord_labels, {}, known ? nullptr : chord_custom.c_str()))
      rex::cvar::SetFlagByName("edf_menu_pad_chord", menu::kChordOptions[size_t(chord_index)].value);
    EndRow("The controller buttons that open this menu, held together. These are the physical buttons, before any "
           "remapping below. The game does not see them once the second one is down. F1 always works on the "
           "keyboard.");

    CheckboxRow("Vibration", "edf_rumble", "Controller rumble. Applies immediately.");

    ui::SectionHeading("Sticks and triggers", metrics_);
    PercentSliderRow("Left stick dead zone", "edf_pad_left_deadzone",
                     "Stick travel from the centre that is ignored, for a stick that drifts. The rest of the travel "
                     "is stretched so the stick still reaches full speed. Off leaves only the game's own dead zone.");
    PercentSliderRow("Right stick dead zone", "edf_pad_right_deadzone",
                     "Stick travel from the centre that is ignored, for a stick that drifts or aim that creeps. The "
                     "rest of the travel is stretched so the stick still reaches full speed.");
    PercentSliderRow("Trigger threshold", "edf_pad_trigger_threshold",
                     "How far a trigger must be pulled before it counts. The rest of the pull is stretched to the full "
                     "range.");
    DrawControllerRemap();

    ui::SectionHeading("Gameplay additions", metrics_);
    CheckboxRow("Manual reload (not in original game)", "edf_manual_reload",
                "EDF 2017 only reloads when a magazine runs dry. This adds a Reload key (default G, under Key "
                "bindings) and a controller button (below) that start the current weapon's reload early, exactly as "
                "an empty magazine would: the full reload time, the reload gauge, then a full magazine. Rounds left "
                "in the magazine are replaced, not added. Does nothing with a full magazine, during a reload or a "
                "burst, in vehicles, or for weapons that never reload or refill at once (grenades), nor while "
                "C-bombs or sentry guns are still deployed. Off, the Reload key and button do nothing and the game "
                "gets every button as before. Applies immediately.");
    const bool manual_reload = GetBool(Get("edf_manual_reload"));
    ImGui::BeginDisabled(!manual_reload);
    BeginRow("Reload on the controller");
    const std::string reload_pad = Get("edf_manual_reload_pad");
    int reload_pad_index = -1;
    std::vector<const char*> reload_pad_labels;
    for (size_t i = 0; i < reload::kPadOptions.size(); ++i) {
      reload_pad_labels.push_back(reload::kPadOptions[i].label);
      if (reload_pad == reload::kPadOptions[i].value) reload_pad_index = int(i);
    }
    const std::string reload_pad_custom = "Custom (" + reload_pad + ")";
    if (Combo("##reload_pad", &reload_pad_index, reload_pad_labels, {},
              reload_pad_index < 0 ? reload_pad_custom.c_str() : nullptr))
      rex::cvar::SetFlagByName("edf_manual_reload_pad", reload::kPadOptions[size_t(reload_pad_index)].value);
    EndRow("Used while Controller mapping > Extra actions > Reload is unbound; mapping a button there replaces it. "
           "Clicking the right stick does nothing else in the game, so it stays with the game as well. If the F1 "
           "menu opens with both sticks, a reload needs a short tap of the right stick on its own. A button the "
           "game's own controller settings give to an action is left to that action.");
    ImGui::EndDisabled();

    ui::SectionHeading("Keyboard and mouse", metrics_);
    CheckboxRow("Keyboard and mouse", "edf_kbm",
                "Play with keyboard and mouse. The controller keeps working alongside. Uses the game's Technical "
                "control type.");
    const bool kbm = GetBool(Get("edf_kbm"));
    ImGui::BeginDisabled(!kbm);
    CheckboxRow("Aim with the mouse", "edf_kbm_mouse_look",
                "Turn and aim with mouse movement. The cursor is captured while you play and freed in this menu.");
    CheckboxRow("Invert vertical aim", "edf_kbm_invert_y", "Moving the mouse forward looks down.");
    BeginRow("Mouse sensitivity");
    float sensitivity = float(GetDouble(Get("edf_kbm_sensitivity"), 1.0));
    if (ImGui::SliderFloat("##sens", &sensitivity, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
      rex::cvar::SetFlagByName("edf_kbm_sensitivity", std::to_string(sensitivity));
    EndRow("How far the view turns per mouse movement. 1.00 is 0.05 degrees per count; zoomed weapons scale it "
           "down further.");
    ImGui::EndDisabled();
    DrawKeybinds(kbm);
  }

  void DrawConnectedPads() {
    const auto backend = ui::RawPads::Active();
    const auto& pads = gamepad_.pads();
    if (backend == ui::RawPads::Backend::kNone) {
      ImGui::TextDisabled("Controllers start with the game");
      return;
    }
    std::string text;
    for (const auto& pad : pads) text += (text.empty() ? "" : ", ") + pad.name;
    if (pads.empty()) ImGui::TextDisabled("None (%s)", ui::RawPads::BackendName(backend));
    else ImGui::TextWrapped("%s  (%s)", text.c_str(), ui::RawPads::BackendName(backend));
  }

  void PercentSliderRow(const char* label, const char* cvar, const char* help) {
    BeginRow(label);
    int value = std::clamp(GetInt(Get(cvar), 0), 0, 90);
    if (ImGui::SliderInt("##percent", &value, 0, 90, value == 0 ? "Off" : "%d%%", ImGuiSliderFlags_AlwaysClamp))
      rex::cvar::SetFlagByName(cvar, std::to_string(value));
    EndRow(help);
  }

  // ---- Controller remapping (controller_logic.h) ---------------------------------------
  static pad::PadRemap Remap(int player) { return pad::ParseRemap(Get(pad::kRemapCvars[size_t(player)])); }
  static void SetRemap(int player, const pad::PadRemap& remap) {
    rex::cvar::SetFlagByName(pad::kRemapCvars[size_t(player)], pad::SerializeRemap(remap));
  }
  static constexpr double kPadCaptureSeconds = 8.0;

  void BeginPadCapture(int target) {
    capture_cvar_.clear();
    pad_capture_target_ = target;
    pad_capture_player_ = remap_player_;
    pad_capture_started_ = ImGui::GetTime();
    pad_capture_ignore_ = pad::PressedControls(last_pad_raw_);  // what is held now does not count
  }
  // Menu frame, while capturing: the first control pressed after the capture began, on
  // any connected controller, from the raw pad (the game sees nothing while the menu is
  // open). Controls already held then count once released and pressed again.
  void UpdatePadCapture(const menu::PadSnapshot& raw) {
    const uint32_t held = pad::PressedControls(raw);
    pad_capture_ignore_ &= held;
    const int control = pad::FirstControl(held & ~pad_capture_ignore_);
    if (control != pad::kNoSource) {
      SetRemap(pad_capture_player_, pad::AssignSource(Remap(pad_capture_player_), pad_capture_target_, control));
      pad_capture_target_ = -1;
      gamepad_.MaskHeld();  // the button stays out of the menu until it is let go
      return;
    }
    if (ImGui::GetTime() - pad_capture_started_ > kPadCaptureSeconds) pad_capture_target_ = -1;
  }

  void DrawControllerRemap() {
    ImGui::Dummy(ImVec2(0, 4.0f * metrics_.scale));
    ui::SectionHeading("Controller mapping", metrics_);
    {
      ui::ScopedFont small_font(ui::FontRole::kSmall, metrics_);
      ImGui::TextDisabled("Which controller button drives each game control. Set, then press the button. Setting a "
                          "button another control uses swaps the two. Applies immediately.");
    }
    BeginRow("Player");
    {
      int player = remap_player_;
      const std::vector<const char*> players{"Player 1 (first controller)", "Player 2 (second controller)"};
      ImGui::BeginDisabled(pad_capture_target_ >= 0);
      if (Combo("##player", &player, players)) remap_player_ = std::clamp(player, 0, pad::kRemapPlayers - 1);
      ImGui::EndDisabled();
    }
    EndRow("Each player has a mapping of their own. Player 2 is for split screen.");

    pad::PadRemap remap = Remap(remap_player_);
    bool changed = false;
    BeginRow("Swap sticks");
    changed |= ImGui::Checkbox("##swap", &remap.swap_sticks);
    EndRow("The left stick aims and the right stick moves.");
    const auto invert_row = [&](const char* label, bool* x, bool* y, const char* help) {
      BeginRow(label);
      changed |= ImGui::Checkbox("Horizontal", x);
      ImGui::SameLine();
      changed |= ImGui::Checkbox("Vertical", y);
      EndRow(help);
    };
    invert_row("Invert left stick", &remap.invert_lx, &remap.invert_ly,
               "Reverse the stick the game uses as its left stick (after swapping).");
    invert_row("Invert right stick", &remap.invert_rx, &remap.invert_ry,
               "Reverse the stick the game uses as its right stick (after swapping). Vertical inverts aim.");
    if (changed) SetRemap(remap_player_, remap);

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("pad_remap", 3, kFlags)) {
      ImGui::TableSetupColumn("Game control", ImGuiTableColumnFlags_WidthStretch, 0.38f);
      ImGui::TableSetupColumn("Controller button", ImGuiTableColumnFlags_WidthStretch, 0.34f);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.28f);
      for (int target = 0; target < pad::kTargetCount; ++target) {
        if (target == pad::kControlCount) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextColored(ui::color::kGreen, "Extra actions");
        }
        // The Reload row only means something while manual reload is on.
        const bool inactive = target == pad::TargetOf(pad::SyntheticAction::kReload) &&
                              !GetBool(Get("edf_manual_reload"));
        ImGui::BeginDisabled(inactive);
        DrawRemapRow(remap, target);
        ImGui::EndDisabled();
        if (inactive && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
          ImGui::SetTooltip("Turn on Manual reload under Gameplay additions to use this.");
      }
      ImGui::EndTable();
    }
    if (const uint32_t unused = pad::UnusedSources(remap)) {
      std::string names;
      for (int c = 0; c < pad::kControlCount; ++c)
        if (unused & (1u << unsigned(c))) names += (names.empty() ? "" : ", ") + std::string(pad::kControls[size_t(c)].token);
      ui::ScopedFont small_font(ui::FontRole::kSmall, metrics_);
      ImGui::TextColored(ui::color::kOrange, "%s  Buttons that do nothing: %s", ui::icon::kWarning, names.c_str());
    }
    const std::string label = std::string(ui::icon::kReset) + "  Reset controller mapping to defaults";
    if (ImGui::Button(label.c_str())) {
      pad_capture_target_ = -1;
      SetRemap(remap_player_, pad::PadRemap::Default());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
      ImGui::SetTooltip("Every button back to itself for this player, sticks unswapped and uninverted.");
  }

  void DrawRemapRow(const pad::PadRemap& remap, int target) {
    const int source = remap.source[size_t(target)];
    const bool capturing_this = pad_capture_target_ == target && pad_capture_player_ == remap_player_;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(pad::TargetLabel(target));
    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    if (capturing_this) {
      const int left = std::max(0, int(kPadCaptureSeconds - (ImGui::GetTime() - pad_capture_started_) + 0.99));
      ImGui::TextColored(ui::color::kOrange, "Press a button (%d s, Esc cancels)", left);
    } else if (source == pad::kNoSource) {
      ImGui::TextDisabled("(unbound)");
    } else if (source != pad::PadRemap::DefaultSource(target)) {
      ImGui::TextColored(ui::color::kOrange, "%s", pad::kControls[size_t(source)].label);
    } else {
      ImGui::TextUnformatted(pad::kControls[size_t(source)].label);
    }
    ImGui::TableSetColumnIndex(2);
    ImGui::PushID(target);
    ImGui::BeginDisabled(capturing() && !capturing_this);
    ImGui::BeginDisabled(!capturing_this && !pad_connected_);
    if (ImGui::Button(capturing_this ? "Cancel" : "Set")) {
      if (capturing_this) pad_capture_target_ = -1;
      else BeginPadCapture(target);
    }
    ImGui::EndDisabled();
    if (!pad_connected_ && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Connect a controller to set a button.");
    ImGui::SameLine();
    ImGui::BeginDisabled(source == pad::kNoSource);
    if (ImGui::Button("Clear")) SetRemap(remap_player_, pad::AssignSource(remap, target, pad::kNoSource));
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::PopID();
  }

  void BeginCapture(const char* cvar, bool replace) {
    pad_capture_target_ = -1;
    capture_cvar_ = cvar;
    capture_replace_ = replace;
  }
  void DrawBindRow(const KeyAction& action, const std::vector<std::pair<std::string, std::string>>& bindings) {
    const std::string value = rex::cvar::GetFlagByName(action.cvar);
    const bool capturing_this = capture_cvar_ == action.cvar;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(action.label);
    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    if (capturing_this) {
      ImGui::TextColored(ui::color::kOrange, "Press a key (Esc cancels)");
    } else {
      const std::string pretty = PrettyBind(value);
      if (value.empty()) ImGui::TextDisabled("%s", pretty.c_str());
      else ImGui::TextUnformatted(pretty.c_str());
      // A key bound to two actions silently drives only one of them in game, so say so.
      for (const auto& token : SplitBind(value)) {
        const std::string_view other = ConflictingAction(token, action.cvar, bindings);
        if (other.empty()) continue;
        ImGui::SameLine();
        ImGui::TextColored(ui::color::kRed, "%s", ui::icon::kWarning);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s is also bound to %s", token.c_str(), std::string(ActionLabel(other)).c_str());
        break;
      }
    }
    ImGui::TableSetColumnIndex(2);
    ImGui::PushID(action.cvar);
    ImGui::BeginDisabled(capturing() && !capturing_this);
    if (ImGui::Button(capturing_this ? "Cancel" : "Set")) {
      if (capturing_this) capture_cvar_.clear();
      else BeginCapture(action.cvar, true);
    }
    ImGui::SameLine();
    // A second binding for the same action, matching the SDK's comma-separated alternatives.
    if (ImGui::Button("Add")) BeginCapture(action.cvar, false);
    ImGui::SameLine();
    ImGui::BeginDisabled(value.empty());
    if (ImGui::Button("Clear")) rex::cvar::SetFlagByName(action.cvar, "");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  void DrawMouseRow(const KeyAction& action) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(action.label);
    ImGui::TableSetColumnIndex(1);
    int index = MouseTargetIndex(rex::cvar::GetFlagByName(action.cvar));
    ImGui::PushID(action.cvar);
    ImGui::SetNextItemWidth(-FLT_MIN);
    std::vector<const char*> labels;
    for (const auto& target : kMouseTargets) labels.push_back(target.label);
    if (Combo("##target", &index, labels)) rex::cvar::SetFlagByName(action.cvar, MouseTargetAt(index).value);
    ImGui::PopID();
    ImGui::TableSetColumnIndex(2);
  }
  void DrawKeybinds(bool kbm) {
    ImGui::Dummy(ImVec2(0, 4.0f * metrics_.scale));
    ui::SectionHeading("Key bindings", metrics_);
    {
      ui::ScopedFont small_font(ui::FontRole::kSmall, metrics_);
      ImGui::TextDisabled(kbm ? "Bindings apply immediately. Set replaces a binding, Add gives an action a second key."
                              : "Turn on keyboard and mouse above to use these.");
    }
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
    // Snapshot every binding once per frame so each row can spot a key it shares.
    // The optional Reload row is greyed out, and shares nothing, while manual reload is off.
    const bool manual_reload = GetBool(Get("edf_manual_reload"));
    std::vector<std::pair<std::string, std::string>> bindings;
    bindings.reserve(std::size(kKeyActions));
    for (const auto& action : kKeyActions)
      if (manual_reload || !IsOptionalAction(action.cvar))
        bindings.emplace_back(action.cvar, rex::cvar::GetFlagByName(action.cvar));
    const char* group = nullptr;
    if (ImGui::BeginTable("keybinds", 3, kFlags)) {
      ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 0.38f);
      ImGui::TableSetupColumn("Bound to", ImGuiTableColumnFlags_WidthStretch, 0.30f);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.32f);
      for (const auto& action : kKeyActions) {
        if (!group || std::string_view(group) != action.group) {
          group = action.group;
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextColored(ui::color::kGreen, "%s", group);
        }
        const bool inactive = IsOptionalAction(action.cvar) && !manual_reload;
        static const std::vector<std::pair<std::string, std::string>> kNoBindings;
        ImGui::BeginDisabled(inactive);
        DrawBindRow(action, inactive ? kNoBindings : bindings);
        ImGui::EndDisabled();
        if (inactive && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
          ImGui::SetTooltip("Turn on Manual reload under Gameplay additions to use this key.");
      }
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextColored(ui::color::kGreen, "Mouse buttons");
      for (const auto& action : kMouseActions) DrawMouseRow(action);
      ImGui::EndTable();
    }
    const std::string label = std::string(ui::icon::kReset) + "  Reset bindings to defaults";
    if (ImGui::Button(label.c_str())) {
      capture_cvar_.clear();
      ResetKeyboardDefaults();
    }
  }

  // ---- Audio ---------------------------------------------------------------------------
  void DrawAudio() {
    ui::SectionHeading("Audio", metrics_);
    BeginRow("Mute");
    bool mute = hooks_.user_mute ? hooks_.user_mute() : GetBool(Get("audio_mute"));
    if (ImGui::Checkbox("##mute", &mute)) {
      if (hooks_.set_user_mute) hooks_.set_user_mute(mute);
      else rex::cvar::SetFlagByName("audio_mute", mute ? "true" : "false");
    }
    EndRow("Silence all game audio.");
    CheckboxRow("Silence while this menu is open", "edf_menu_mute_audio",
                "Mutes the game while the menu pauses it, so looping sounds stop. Takes effect the next time the menu "
                "opens.");
    CheckboxRow("Suspend audio while paused", "edf_menu_pause_audio_engine",
                "Also stops the game's audio engine while the menu is open, so music continues exactly where it "
                "stopped. Experimental. Takes effect the next time the menu opens.");
  }

  // ---- Advanced ------------------------------------------------------------------------
  void DrawAdvanced() {
    ui::SectionHeading("Advanced", metrics_);
    CheckboxRow("Pause the game in this menu", "edf_menu_pause",
                "Stops the game while this menu is open. Turn off to keep it running behind the menu. Takes effect "
                "the next time the menu opens.");
    if (Exists("edf_native_gpu_timings"))
      CheckboxRow("GPU timing", "edf_native_gpu_timings",
                  "Measures how long the GPU spends on each frame, for the performance overlay and the log. Costs a "
                  "little GPU time.");
    CheckboxRow("Log frame-time statistics", "edf_frametime_log",
                "Writes the frame rate, frame-time spread and 1% lows to the log every 5 seconds.");
    CheckboxRow("Verbose input tracing", "edf_trace_input", "Writes controller and system events to the log. For "
                                                            "bug reports; makes the log large.");
    ImGui::Dummy(ImVec2(0, 6.0f * metrics_.scale));
    ui::ScopedFont small_font(ui::FontRole::kSmall, metrics_);
    ImGui::TextDisabled("Settings file: %s", config_path_.string().c_str());
  }

  // ---- Reset ---------------------------------------------------------------------------
  void ResetOne(const std::string& cvar) {
    if (Exists(cvar)) ApplyOne(cvar, Default(cvar));
  }
  void ResetSection(int section) {
    switch (section) {
      case 0: {
        if (Get("edf_display_mode") != "windowed") {
          SetDisplayMode(0);
          StartRevert("Display settings reset.", [this] { SetDisplayMode(1); });
        }
        Apply("window_size", {{"window_width", Default("window_width")}, {"window_height", Default("window_height")},
                              {"video_mode_width", Default("video_mode_width")},
                              {"video_mode_height", Default("video_mode_height")}});
        Apply("edf_aspect", {{"edf_aspect", Default("edf_aspect")},
                             {"present_letterbox", PresentLetterbox(Default("edf_aspect")) ? "true" : "false"}});
        ResetOne("edf_hud_safe_area");
        ResetOne("edf_present_filter");
        ResetOne("edf_menu_scale");
        break;
      }
      case 1:
        ResetOne("edf_native_renderer");
        Apply("scene_backend", {{"edf_native_scene_backend", Default("edf_native_scene_backend")},
                                {"edf_native_backend", Default("edf_native_backend")}});
        Apply("render_size", {{"edf_native_render_width", Default("edf_native_render_width")},
                              {"edf_native_render_height", Default("edf_native_render_height")}});
        for (const char* cvar : {"edf_native_fsr", "edf_native_fsr_sharpness", "edf_native_msaa",
                                 "edf_native_anisotropic_filtering"})
          ResetOne(cvar);
        break;
      case 2:
        for (const char* cvar : {"edf_native_unlock_framerate", "edf_fps_cap", "edf_native_vsync",
                                 "edf_low_latency", "edf_frame_pacer_before_present", "edf_native_thread_qos",
                                 "edf_show_fps", "edf_perf_overlay_detail"})
          ResetOne(cvar);
        break;
      case 3:
        for (const char* cvar : {"input_backend", "edf_menu_pad_chord", "edf_rumble", "edf_pad_left_deadzone",
                                 "edf_pad_right_deadzone", "edf_pad_trigger_threshold", "edf_pad_remap_p1",
                                 "edf_pad_remap_p2", "edf_manual_reload", "edf_manual_reload_pad", "edf_kbm",
                                 "edf_kbm_mouse_look", "edf_kbm_invert_y", "edf_kbm_sensitivity"})
          ResetOne(cvar);
        capture_cvar_.clear();
        pad_capture_target_ = -1;
        ResetKeyboardDefaults();
        break;
      case 4:
        if (hooks_.set_user_mute) hooks_.set_user_mute(false);
        else ResetOne("audio_mute");
        ResetOne("edf_menu_mute_audio");
        ResetOne("edf_menu_pause_audio_engine");
        break;
      default:
        for (const char* cvar : {"edf_menu_pause", "edf_native_gpu_timings", "edf_frametime_log", "edf_trace_input"})
          ResetOne(cvar);
        break;
    }
  }

  // ---- Display-change confirmation -----------------------------------------------------
  void StartRevert(std::string what, std::function<void()> revert) {
    // A second change while one is waiting: keep the first one's way back, which
    // returns to the settings from before either change.
    if (!revert_.active()) revert_action_ = std::move(revert);
    revert_what_ = std::move(what);
    revert_.Start(ImGui::GetTime());
  }
  void DrawRevertModal() {
    static constexpr const char* kPopup = "##keep_display";
    if (!revert_.active()) return;
    if (!ImGui::IsPopupOpen(kPopup)) ImGui::OpenPopup(kPopup);
    const ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520.0f * metrics_.scale, 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
      return;
    const double now = ImGui::GetTime();
    {
      ui::ScopedFont heading(ui::FontRole::kHeading, metrics_);
      ImGui::TextColored(ui::color::kOrange, "%s  Keep these display settings?", ui::icon::kClock);
    }
    ImGui::TextUnformatted(revert_what_.c_str());
    ImGui::TextDisabled("Reverting in %d s", revert_.SecondsLeft(now));
    ImGui::Dummy(ImVec2(0, 6.0f * metrics_.scale));
    const float button_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const float button_h = std::max(ImGui::GetFrameHeight() * 1.2f, 44.0f * metrics_.scale);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    if (ImGui::Button("Keep", ImVec2(button_w, button_h))) revert_.Keep();
    ImGui::SameLine();
    if (ImGui::Button("Revert", ImVec2(button_w, button_h))) {
      revert_.Cancel();
      if (revert_action_) revert_action_();
    }
    if (revert_.Expire(now) && revert_action_) revert_action_();
    if (!revert_.active()) {
      revert_action_ = nullptr;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // ---- Save ----------------------------------------------------------------------------
  // Live settings from the running cvars; restart-only ones from the staged values; and
  // audio_mute as the player set it, not as the pause forced it.
  void Save() {
    std::vector<settings::ConfigOverride> overrides;
    for (const auto& [name, value] : Staged().Overrides()) {
      const auto* info = rex::cvar::GetFlagInfo(name);
      if (!info) continue;
      const bool quoted = info->type == rex::cvar::FlagType::String;
      overrides.push_back({name, quoted ? "\"" + value + "\"" : value, value == info->default_value});
    }
    if (hooks_.user_mute) {
      const bool mute = hooks_.user_mute();
      overrides.push_back({"audio_mute", mute ? "true" : "false", !mute});
    }
    SaveUserConfig(config_path_, overrides);
  }

  std::filesystem::path config_path_;
  Hooks hooks_;
  ui::MenuMetrics metrics_;
  ui::MenuGamepad gamepad_;
  int section_ = 0;
  bool focus_request_ = false;
  bool pad_connected_ = false;
  bool popup_or_active_ = false;  // last frame: Escape/B belong to ImGui, not to closing
  bool restart_failed_ = false;
  bool custom_window_ = false, custom_render_ = false;
  int custom_w_ = 1280, custom_h_ = 720, render_custom_w_ = 1920, render_custom_h_ = 1080;
  float pending_menu_scale_ = 0;
  std::string focus_help_;
  settings::RevertCountdown revert_;
  std::function<void()> revert_action_;
  std::string revert_what_;
  std::string capture_cvar_;      // empty when not rebinding
  bool capture_replace_ = true;   // Set replaces the binding, Add appends an alternative
  // Controller remap (DrawControllerRemap): the player whose table is shown, and the row
  // waiting for a button (-1: none), for which player, since when, and the controls that
  // were already held when it began.
  int remap_player_ = 0;
  int pad_capture_target_ = -1, pad_capture_player_ = 0;
  double pad_capture_started_ = 0;
  uint32_t pad_capture_ignore_ = 0;
  menu::PadSnapshot last_pad_raw_;
};

}  // namespace edf
