// EDF2027 - first-run shader preparation notice.
//
// While the boot precompile (native_graphics/native_shader_precompile.h) is compiling -
// a first run, a new build or a new d3dcompiler - a small panel in the bottom-right
// corner says so and counts the effects' entries. It only reports: nothing waits for
// it, and the game keeps loading underneath. On a warm run the precompile ends at its
// stamp check within milliseconds and the panel never draws.
#pragma once
#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <imgui.h>
#include <functional>
#include "menu_ui.h"
#include "native_graphics/native_shader_precompile.h"

REXCVAR_DECLARE(double, edf_menu_scale);

namespace edf {

class ShaderPrepOverlay final : public rex::ui::ImGuiDialog {
 public:
  ShaderPrepOverlay(rex::ui::ImGuiDrawer* drawer, std::function<float()> physical_height)
      : ImGuiDialog(drawer), physical_height_(std::move(physical_height)) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const auto status = native::GetNativeShaderPrecompileStatus();
    // Not before there is something to count, so a warm run's stamp check never flashes it.
    if (!status.running() || !status.jobs) return;
    const auto metrics = ui::ComputeMetrics(io.DisplaySize.y, physical_height_ ? physical_height_() : 0.0f,
                                            float(REXCVAR_GET(edf_menu_scale)));
    const float s = metrics.scale;
    ui::ScopedMenuStyle style(s);
    ui::ScopedFont font(ui::FontRole::kSmall, metrics);
    const ImVec2 corner(io.DisplaySize.x - 12.0f * s, io.DisplaySize.y - 12.0f * s);
    ImGui::SetNextWindowPos(corner, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.62f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * s, 7.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
    ImGui::Begin("##edf_shader_prep", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::Text("Preparing shaders (first run)");
    ImGui::ProgressBar(float(status.done) / float(status.jobs), ImVec2(220.0f * s, 0.0f), nullptr);
    ImGui::TextDisabled("%u / %u", status.done, status.jobs);
    ImGui::End();
    ImGui::PopStyleVar(2);
  }

 private:
  std::function<float()> physical_height_;
};

}  // namespace edf
