// EDF 2017 PC - first-run setup screen: pick the disc image (extracted with extract-xiso)
// or an already extracted game folder. Portable: SDL3 native dialogs + SDL3 process API.
#pragma once
#include <SDL3/SDL.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <imgui.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include "iso_extract.h"
#include "launcher.h"
#include "ui_strings.h"

namespace edf {

class SetupDialog final : public rex::ui::ImGuiDialog {
 public:
  using DoneFn = std::function<void(const std::filesystem::path&)>;
  using QuitFn = std::function<void()>;

  SetupDialog(rex::ui::ImGuiDrawer* drawer, std::filesystem::path extract_dir, DoneFn on_done, QuitFn on_quit)
      : ImGuiDialog(drawer), extract_dir_(std::move(extract_dir)), on_done_(std::move(on_done)), on_quit_(std::move(on_quit)) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    PollDialogResult();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Always);
    ImGui::Begin(str::kSetupTitle, nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextWrapped("%s", str::kSetupIntro);
    ImGui::Spacing();

    switch (extractor_.state()) {
      case IsoExtractor::State::kListing:
        ImGui::Text("%s", str::kListing);
        DrawCancel();
        break;
      case IsoExtractor::State::kExtracting: {
        ImGui::Text("%s", str::kExtracting);
        double done = (double)extractor_.done_bytes() / (1024.0 * 1024.0);
        double total = (double)extractor_.total_bytes() / (1024.0 * 1024.0);
        char buf[96];
        snprintf(buf, sizeof(buf), "%.0f / %.0f MB", done, total);
        ImGui::ProgressBar((float)extractor_.progress(), ImVec2(-1, 0), buf);
        DrawCancel();
        break;
      }
      case IsoExtractor::State::kDone:
        extractor_.Reset();
        Finish(extractor_.dest());
        break;
      case IsoExtractor::State::kFailed:
        message_ = "Extraction failed: " + extractor_.error();
        extractor_.Reset();
        break;
      case IsoExtractor::State::kCancelled:
        message_ = "Extraction cancelled.";
        extractor_.Reset();
        break;
      case IsoExtractor::State::kIdle:
      default:
        DrawIdle();
        break;
    }
    ImGui::End();
  }

 private:
  void DrawCancel() {
    if (ImGui::Button(str::kCancel, ImVec2(140, 0))) extractor_.Cancel();
  }
  void DrawIdle() {
    if (!message_.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.5f, 0.4f, 1));
      ImGui::TextWrapped("%s", message_.c_str());
      ImGui::PopStyleColor();
      ImGui::Spacing();
    }
    if (ImGui::Button(str::kSelectIso, ImVec2(-1, 0)) && !dialog_open_) {
      dialog_open_ = true;
      static const SDL_DialogFileFilter filters[] = {{"Xbox 360 disc image", "iso"}};
      SDL_ShowOpenFileDialog(&SetupDialog::FileCallback, this, nullptr, filters, 1, nullptr, false);
    }
    if (ImGui::Button(str::kSelectFolder, ImVec2(-1, 0)) && !dialog_open_) {
      dialog_open_ = true;
      SDL_ShowOpenFolderDialog(&SetupDialog::FolderCallback, this, nullptr, nullptr, false);
    }
    ImGui::Spacing();
    if (ImGui::Button(str::kQuit, ImVec2(140, 0))) {
      Close();
      if (on_quit_) on_quit_();
    }
  }

  static void SDLCALL FileCallback(void* ud, const char* const* files, int) {
    auto* self = static_cast<SetupDialog*>(ud);
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->dialog_open_ = false;
    if (files && files[0]) self->pending_ = {std::filesystem::path(files[0]), true};
  }
  static void SDLCALL FolderCallback(void* ud, const char* const* files, int) {
    auto* self = static_cast<SetupDialog*>(ud);
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->dialog_open_ = false;
    if (files && files[0]) self->pending_ = {std::filesystem::path(files[0]), false};
  }

  void PollDialogResult() {
    std::optional<std::pair<std::filesystem::path, bool>> p;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      p.swap(pending_);
    }
    if (!p) return;
    message_.clear();
    if (p->second) {
      // Disc image: extract into the per-user game folder.
      std::error_code ec;
      std::filesystem::remove_all(extract_dir_, ec);
      std::filesystem::create_directories(extract_dir_, ec);
      extractor_.Start(p->first, extract_dir_);
    } else {
      Finish(p->first);
    }
  }

  void Finish(const std::filesystem::path& dir) {
    if (!HasXex(dir)) {
      message_ = str::kNoXex;
      return;
    }
    uint32_t id = ReadXexTitleId(dir / "default.xex");
    if (id != kTitleId) {
      char buf[160];
      snprintf(buf, sizeof(buf), "%s (found title id %08X, expected %08X)", str::kWrongTitle, id, kTitleId);
      message_ = buf;
      return;
    }
    auto done = on_done_;
    Close();
    if (done) done(dir);
  }

  std::filesystem::path extract_dir_;
  DoneFn on_done_;
  QuitFn on_quit_;
  IsoExtractor extractor_;
  std::mutex mutex_;
  std::optional<std::pair<std::filesystem::path, bool>> pending_;
  bool dialog_open_ = false;
  std::string message_;
};

}  // namespace edf
