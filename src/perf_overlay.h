// EDF2027 - performance overlay (F2, or Performance > Performance overlay in the F1 menu).
//
// Off by default and cheap when on: the swap hook writes one frame time per frame into a
// ring (frame_stats.h), and this reads it once per overlay paint. Detail levels
// (edf_perf_overlay_detail): 0 frame rate and frame time, 1 adds a frame-time sparkline,
// 2 adds the engine thread's CPU time and the GPU frame time. The GPU number comes from
// edf_native_gpu_timings' report window, so it needs GPU timing on and changes every few
// seconds; without it the overlay says so rather than showing nothing.
#pragma once
#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include "frame_stats.h"
#include "menu_ui.h"

REXCVAR_DECLARE(bool, edf_show_fps);
REXCVAR_DECLARE(int32_t, edf_perf_overlay_detail);
REXCVAR_DECLARE(double, edf_menu_scale);

namespace edf {

class PerfOverlay final : public rex::ui::ImGuiDialog {
 public:
  PerfOverlay(rex::ui::ImGuiDrawer* drawer, std::function<float()> physical_height)
      : ImGuiDialog(drawer), physical_height_(std::move(physical_height)) {}
  static PerfOverlay*& Current() { static PerfOverlay* s = nullptr; return s; }

 protected:
  void OnClose() override { if (Current() == this) Current() = nullptr; }
  void OnDraw(ImGuiIO& io) override {
    if (!REXCVAR_GET(edf_show_fps)) return;
    auto& st = CurrentFrameStats();
    const int detail = std::clamp(REXCVAR_GET(edf_perf_overlay_detail), 0, 2);
    const auto metrics = ui::ComputeMetrics(io.DisplaySize.y, physical_height_ ? physical_height_() : 0.0f,
                                            float(REXCVAR_GET(edf_menu_scale)));
    const float s = metrics.scale;

    // Newest samples, oldest first, for the graph and the averages.
    const uint32_t count = st.frame_count.load(std::memory_order_acquire);
    const uint32_t n = std::min<uint32_t>(count, kGraph);
    std::array<float, kGraph> samples{};
    for (uint32_t i = 0; i < n; ++i)
      samples[i] = st.frame_ms[(count - n + i) % FrameStats::kHistory].load(std::memory_order_relaxed);
    double sum = 0, worst = 0;
    const uint32_t recent = std::min<uint32_t>(n, 60);
    for (uint32_t i = n - recent; i < n; ++i) { sum += samples[i]; worst = std::max(worst, double(samples[i])); }
    const double average = recent ? sum / recent : 0.0;

    ui::ScopedMenuStyle style(s);
    ui::ScopedFont font(ui::FontRole::kSmall, metrics);
    ImGui::SetNextWindowPos(ImVec2(10.0f * s, 10.0f * s), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.62f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * s, 7.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * s, 3.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
    ImGui::Begin("##edf_perf", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    if (average > 0) {
      {
        ui::ScopedFont big(ui::FontRole::kHeading, metrics);
        ImGui::TextColored(ui::color::kGreen, "%.0f", 1000.0 / average);
      }
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ImGui::TextDisabled("FPS");
      ImGui::SameLine();
      ImGui::Text("%.2f ms", average);
    } else {
      ImGui::TextDisabled("Waiting for frames");
    }
    if (detail >= 1 && n > 1) {
      // Scale to twice the recent average (at least one 30 fps frame) so spikes stand out.
      const float top = float(std::max({average * 2.0, 33.4, worst}));
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0.35f));
      ImGui::PlotLines("##frametimes", samples.data(), int(n), 0, nullptr, 0.0f, top, ImVec2(200.0f * s, 38.0f * s));
      ImGui::PopStyleColor();
      ImGui::TextDisabled("worst %.1f ms  (last 60 frames)", worst);
    }
    if (detail >= 2) {
      ImGui::Text("CPU %.2f ms", st.cpu_ms.load(std::memory_order_relaxed));
      ImGui::SameLine();
      if (st.gpu_windows.load(std::memory_order_relaxed))
        ImGui::Text("GPU %.2f ms", st.gpu_ms.load(std::memory_order_relaxed));
      else
        ImGui::TextDisabled("GPU: turn on GPU timing");
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
  }

 private:
  static constexpr uint32_t kGraph = 120;
  std::function<float()> physical_height_;
};

}  // namespace edf
