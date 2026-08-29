// EDF2027 - small frame-rate overlay (settings: "Show FPS"), fed by the VdSwap hook statistics.
#pragma once
#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <imgui.h>
#include "frame_stats.h"

REXCVAR_DECLARE(bool, edf_show_fps);

namespace edf {

class FpsOverlay final : public rex::ui::ImGuiDialog {
 public:
  explicit FpsOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}
  static FpsOverlay*& Current() { static FpsOverlay* s = nullptr; return s; }

 protected:
  void OnClose() override { if (Current() == this) Current() = nullptr; }
  void OnDraw(ImGuiIO& io) override {
    if (!REXCVAR_GET(edf_show_fps)) return;
    const auto& st = CurrentFrameStats();
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.45f);
    ImGui::Begin("##edf_fps", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::Text("%.0f fps  %.1f ms (1%% low %.1f)", st.fps.load(std::memory_order_relaxed),
                st.average_ms.load(std::memory_order_relaxed),
                st.low_1_percent_ms.load(std::memory_order_relaxed));
    ImGui::End();
  }
};

}  // namespace edf
