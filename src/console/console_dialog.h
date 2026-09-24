// EDF2027 - the in-game console's drop-down window (Quake style), drawn with ImGui over the
// game. Toggled by bind_edf_console (default the ` key); Escape closes it.
//
// It only draws and edits: lines go to console::Service (console.h), which runs them. Input
// routing is the F1 menu's (pause_menu.h): while the console is open the app closes the
// menu input gate, so neither keyboard nor mouse reaches the game, and frees the mouse
// (edf2017_app.h BeginConsoleCapture). With edf_console_pause the simulation also holds.
//
// Keys: Enter runs the line; Tab completes commands, cvars and arguments (twice lists the
// candidates); Up/Down browse the history; PageUp/PageDown scroll; Ctrl+L clears.
#pragma once

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include "console.h"
#include "menu_ui.h"

REXCVAR_DECLARE(double, edf_menu_scale);

namespace edf::console {

// A monospace face for the console, from the system fonts (Consolas, Cascadia Mono or
// Lucida Console), baked at a few sizes like the menu's (menu_ui.h). Without one the
// console uses ImGui's default font (ProggyClean, also monospace). Called from
// OnConfigureFonts after the menu fonts; never changes the atlas's first font.
struct ConsoleFonts {
  static constexpr std::array<float, 4> kSizes{15.0f, 18.0f, 22.0f, 28.0f};
  std::array<ImFont*, kSizes.size()> faces{};
  bool loaded = false;
};
inline ConsoleFonts& Fonts() {
  static ConsoleFonts fonts;
  return fonts;
}
inline void LoadConsoleFonts(ImFontAtlas* atlas) {
  static const ImWchar kRanges[] = {0x0020, 0x00FF, 0};
  std::filesystem::path windows = "C:\\Windows";
  if (const char* root = std::getenv("WINDIR")) windows = root;
  for (const char* file : {"consola.ttf", "CascadiaMono.ttf", "lucon.ttf"}) {
    const auto path = windows / "Fonts" / file;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) continue;
    auto& fonts = Fonts();
    bool all = true;
    for (size_t i = 0; i < fonts.faces.size(); ++i) {
      ImFontConfig config;
      config.OversampleH = config.OversampleV = 1;
      config.PixelSnapH = true;
      fonts.faces[i] = atlas->AddFontFromFileTTF(path.string().c_str(), ConsoleFonts::kSizes[i], &config, kRanges);
      all = all && fonts.faces[i];
    }
    fonts.loaded = all;
    if (all) return;
  }
}

class ConsoleDialog final : public rex::ui::ImGuiDialog {
 public:
  struct Hooks {
    std::function<void(bool open)> on_toggle;  // capture input (open) or give it back
    std::function<float()> physical_height;
  };
  ConsoleDialog(rex::ui::ImGuiDrawer* drawer, Hooks hooks) : ImGuiDialog(drawer), hooks_(std::move(hooks)) {}
  static ConsoleDialog*& Current() { static ConsoleDialog* current = nullptr; return current; }

  bool open() const { return open_; }
  void SetOpen(bool open) {
    if (open == open_) return;
    open_ = open;
    focus_input_ = open;
    candidates_.clear();
    if (hooks_.on_toggle) hooks_.on_toggle(open);
  }
  void Toggle() { SetOpen(!open_); }

 protected:
  void OnClose() override {
    if (Current() == this) Current() = nullptr;
  }
  void OnDraw(ImGuiIO& io) override {
    const float target = open_ ? 1.0f : 0.0f;
    const float step = std::clamp(io.DeltaTime, 0.0f, 0.1f) * 7.0f;
    shown_ = shown_ < target ? std::min(target, shown_ + step) : std::max(target, shown_ - step);
    if (shown_ <= 0.0f) return;

    auto& service = Service::Get();
    generation_ = service.CopyOutput(generation_, lines_);
    const auto metrics = ui::ComputeMetrics(io.DisplaySize.y, hooks_.physical_height ? hooks_.physical_height() : 0.0f,
                                            float(REXCVAR_GET(edf_menu_scale)));
    const float s = metrics.scale;
    const float eased = 1.0f - (1.0f - shown_) * (1.0f - shown_);
    const float height = std::floor(io.DisplaySize.y * 0.46f);
    ui::ScopedMenuStyle style(s);
    ImGui::SetNextWindowPos(ImVec2(0.0f, -height * (1.0f - eased)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, height), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * s, 8.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * s, 4.0f * s));
    ImGui::Begin("##edf_console", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    DrawHeader(service, metrics);
    const bool pushed = PushMonoFont(metrics);
    const float line_height = ImGui::GetTextLineHeightWithSpacing();
    const float footer = ImGui::GetFrameHeightWithSpacing() + (candidates_.empty() ? 0.0f : line_height * 2.2f);
    ImGui::BeginChild("##scrollback", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    if (open_ && ImGui::IsKeyPressed(ImGuiKey_PageUp)) ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetWindowHeight() * 0.8f);
    if (open_ && ImGui::IsKeyPressed(ImGuiKey_PageDown)) ImGui::SetScrollY(ImGui::GetScrollY() + ImGui::GetWindowHeight() * 0.8f);
    ImGuiListClipper clipper;
    clipper.Begin(int(lines_.size()));
    while (clipper.Step())
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const auto& line = lines_[size_t(i)];
        ImGui::PushStyleColor(ImGuiCol_Text, ColorOf(line.severity));
        ImGui::TextUnformatted(line.text.c_str());
        ImGui::PopStyleColor();
      }
    if (generation_ != drawn_generation_) {
      if (stick_to_bottom_) ImGui::SetScrollHereY(1.0f);
      drawn_generation_ = generation_;
    }
    stick_to_bottom_ = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - line_height;
    ImGui::EndChild();
    if (!candidates_.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ui::color::kTextDim);
      ImGui::TextWrapped("%s", candidates_.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ui::color::kGreen);
    ImGui::TextUnformatted("]");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (focus_input_ && open_) {
      ImGui::SetKeyboardFocusHere();
      focus_input_ = false;
    }
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
                                      ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackCharFilter |
                                      ImGuiInputTextFlags_CallbackEdit;
    if (!open_) ImGui::BeginDisabled();
    if (ImGui::InputText("##input", input_.data(), input_.size(), flags, &ConsoleDialog::InputCallback, this)) {
      const std::string line = input_.data();
      input_[0] = 0;
      candidates_.clear();
      stick_to_bottom_ = true;
      service.Submit(line);
      focus_input_ = true;
    }
    if (!open_) ImGui::EndDisabled();
    if (open_ && ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_L)) service.ClearOutput();
    if (pushed) ImGui::PopFont();
    // A green rule along the bottom edge, like the HUD's panels.
    const ImVec2 min = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, min.y + size.y - 1.0f), ImVec2(min.x + size.x, min.y + size.y - 1.0f),
                                        ui::color::U32(ui::color::kGreen, 0.8f), 2.0f * s);
    ImGui::End();
    ImGui::PopStyleVar(4);
  }

 private:
  static ImVec4 ColorOf(Severity severity) {
    switch (severity) {
      case Severity::kOk: return ui::color::kGreen;
      case Severity::kWarn: return ui::color::kOrange;
      case Severity::kError: return ui::color::kRed;
      case Severity::kEcho: return ui::color::kTextDim;
      default: return ui::color::kText;
    }
  }
  bool PushMonoFont(const ui::MenuMetrics& metrics) {
    const auto& fonts = Fonts();
    const float logical = metrics.Px(16.0f);
    if (!fonts.loaded) {
      ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0], std::max(13.0f, std::round(logical)));
      return true;
    }
    const float physical = logical * metrics.physical_per_logical;
    size_t pick = fonts.faces.size() - 1;
    for (size_t i = 0; i < fonts.faces.size(); ++i)
      if (ConsoleFonts::kSizes[i] >= physical) { pick = i; break; }
    ImGui::PushFont(fonts.faces[pick], logical);
    return true;
  }
  void DrawHeader(Service& service, const ui::MenuMetrics& metrics) {
    const auto status = service.status();
    {
      ui::ScopedFont font(ui::FontRole::kSmall, metrics);
      ImGui::PushStyleColor(ImGuiCol_Text, ui::color::kGreen);
      ImGui::TextUnformatted("EDF2027 CONSOLE");
      ImGui::PopStyleColor();
      ImGui::SameLine();
      std::string text = "tick " + std::to_string(status.tick) + (status.in_mission ? "   in mission" : "   no mission");
      if (!status.engine_seen) text += "   engine not running yet";
      if (status.queued) text += "   " + std::to_string(status.queued) + " queued";
      if (status.runners) text += "   " + std::to_string(status.runners) + " script(s)";
      ImGui::TextDisabled("%s", text.c_str());
      if (status.cheats) {
        ImGui::SameLine();
        ui::Badge("CHEATS USED", ui::color::kOrange, false, metrics.scale);
      }
    }
  }
  static int InputCallback(ImGuiInputTextCallbackData* data) {
    auto* self = static_cast<ConsoleDialog*>(data->UserData);
    auto& service = Service::Get();
    switch (data->EventFlag) {
      case ImGuiInputTextFlags_CallbackCharFilter:
        // The console key's own character.
        return data->EventChar == '`' || data->EventChar == '~' ? 1 : 0;
      case ImGuiInputTextFlags_CallbackEdit:
        self->candidates_.clear();
        self->tab_count_ = 0;
        return 0;
      case ImGuiInputTextFlags_CallbackCompletion: {
        const std::string line(data->Buf, size_t(data->BufTextLen));
        const auto completion = service.Complete(line);
        const std::string applied = Registry::ApplyCompletion(line, completion);
        if (applied != line) {
          data->DeleteChars(0, data->BufTextLen);
          data->InsertChars(0, applied.c_str());
          self->tab_count_ = 0;
        } else {
          ++self->tab_count_;
        }
        if (completion.candidates.size() > 1) {
          std::string list;
          for (size_t i = 0; i < completion.candidates.size() && i < 60; ++i) list += completion.candidates[i] + "   ";
          if (completion.candidates.size() > 60) list += "... (" + std::to_string(completion.candidates.size()) + ")";
          self->candidates_ = list;
        } else {
          self->candidates_.clear();
        }
        return 0;
      }
      case ImGuiInputTextFlags_CallbackHistory: {
        const auto entry = data->EventKey == ImGuiKey_UpArrow ? service.history().Older() : service.history().Newer();
        if (entry) {
          data->DeleteChars(0, data->BufTextLen);
          data->InsertChars(0, entry->c_str());
        }
        return 0;
      }
      default:
        return 0;
    }
  }

  Hooks hooks_;
  bool open_ = false, focus_input_ = false, stick_to_bottom_ = true;
  float shown_ = 0.0f;
  std::array<char, 1024> input_{};
  std::vector<OutputLine> lines_;
  uint64_t generation_ = 0, drawn_generation_ = 0;
  std::string candidates_;
  int tab_count_ = 0;
};

}  // namespace edf::console
