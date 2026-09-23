// EDF2027 - look and feel of the F1 menu and the performance overlay: the embedded fonts,
// the EDF theme (dark military panels, HUD green accents, an orange highlight) and a few
// widgets ImGui does not have (sidebar entries, badges, a segmented control).
//
// Fonts. The SDK's ImGui renderer uploads the font atlas once and cannot rasterize new
// sizes later (no ImGuiBackendFlags_RendererHasTextures), so each face is baked at a few
// pixel sizes here, from OnConfigureFonts, and the menu picks the smallest bake at least
// as large as the size it wants (settings::PickBakedSize) and draws it at that size.
// Only ASCII plus a handful of typographic characters and the icons actually used are
// baked, which keeps the atlas small.
#pragma once

#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include "menu_fonts.h"
#include "settings_logic.h"

namespace edf::ui {

// ---- Icons (Font Awesome Free 6, solid) ----------------------------------------------
namespace icon {
inline constexpr const char* kDisplay = "";      // desktop
inline constexpr const char* kGraphics = "";     // wand-magic-sparkles
inline constexpr const char* kPerformance = "";  // gauge-high
inline constexpr const char* kControls = "";     // gamepad
inline constexpr const char* kAudio = "";        // volume-high
inline constexpr const char* kAdvanced = "";     // screwdriver-wrench
inline constexpr const char* kResume = "";       // play
inline constexpr const char* kPause = "";        // pause
inline constexpr const char* kRestart = "";      // rotate
inline constexpr const char* kReset = "";        // rotate-left
inline constexpr const char* kWarning = "";      // triangle-exclamation
inline constexpr const char* kInfo = "";         // circle-info
inline constexpr const char* kClose = "";        // xmark
inline constexpr const char* kCheck = "";        // check
inline constexpr const char* kKeyboard = "";     // keyboard
inline constexpr const char* kMouse = "";        // computer-mouse
inline constexpr const char* kClock = "";        // clock
inline constexpr const char* kSave = "";         // floppy-disk
}  // namespace icon

// ---- Colors --------------------------------------------------------------------------
namespace color {
inline constexpr ImVec4 kGreen{0.235f, 0.878f, 0.627f, 1.0f};       // #3CE0A0, the HUD's green
inline constexpr ImVec4 kGreenDim{0.165f, 0.560f, 0.420f, 1.0f};
inline constexpr ImVec4 kOrange{1.000f, 0.560f, 0.180f, 1.0f};      // highlight
inline constexpr ImVec4 kRed{1.000f, 0.380f, 0.320f, 1.0f};
inline constexpr ImVec4 kText{0.900f, 0.935f, 0.915f, 1.0f};
inline constexpr ImVec4 kTextDim{0.560f, 0.640f, 0.605f, 1.0f};
inline constexpr ImVec4 kPanel{0.047f, 0.071f, 0.063f, 0.965f};
inline constexpr ImVec4 kSidebar{0.031f, 0.047f, 0.041f, 1.0f};
inline constexpr ImVec4 kFrame{0.090f, 0.133f, 0.118f, 1.0f};
inline constexpr ImVec4 kFrameHover{0.122f, 0.188f, 0.161f, 1.0f};
inline constexpr ImVec4 kFrameActive{0.153f, 0.251f, 0.212f, 1.0f};
inline constexpr ImVec4 kBorder{0.165f, 0.239f, 0.208f, 1.0f};
inline ImU32 U32(ImVec4 c, float alpha = 1.0f) { c.w *= alpha; return ImGui::ColorConvertFloat4ToU32(c); }
}  // namespace color

// ---- Fonts ---------------------------------------------------------------------------
enum class FontRole { kSmall, kBody, kHeading, kTitle };
// Design sizes at 1080p, in pixels.
inline float DesignSize(FontRole role) {
  switch (role) {
    case FontRole::kSmall: return 15.0f;
    case FontRole::kBody: return 18.0f;
    case FontRole::kHeading: return 22.0f;
    default: return 30.0f;
  }
}
struct MenuFonts {
  static constexpr std::array<float, 4> kRegularSizes{14.0f, 18.0f, 24.0f, 36.0f};
  static constexpr std::array<float, 5> kSemiBoldSizes{18.0f, 24.0f, 32.0f, 48.0f, 64.0f};
  std::array<ImFont*, kRegularSizes.size()> regular{};
  std::array<ImFont*, kSemiBoldSizes.size()> semibold{};
  bool loaded = false;
};
inline MenuFonts& Fonts() {
  static MenuFonts fonts;
  return fonts;
}

// OnConfigureFonts. Must not change the atlas's first font: that is the SDK's default,
// which its own overlays keep using.
inline void LoadMenuFonts(ImFontAtlas* atlas) {
  static const ImWchar kText[] = {0x0020, 0x007E, 0x00B0, 0x00B0, 0x00B7, 0x00B7, 0x00D7, 0x00D7,
                                  0x2013, 0x2014, 0x2022, 0x2022, 0x2026, 0x2026, 0x2191, 0x2191,
                                  0x2193, 0x2193, 0};
  static const ImWchar kIcons[] = {0xe2ca, 0xe2ca, 0xf00c, 0xf00d, 0xf017, 0xf017, 0xf028, 0xf028,
                                   0xf04b, 0xf04c, 0xf05a, 0xf05a, 0xf071, 0xf071, 0xf0c7, 0xf0c7,
                                   0xf108, 0xf108, 0xf11b, 0xf11c, 0xf2ea, 0xf2ea, 0xf2f1, 0xf2f1,
                                   0xf625, 0xf625, 0xf7d9, 0xf7d9, 0xf8cc, 0xf8cc, 0};
  auto& fonts = Fonts();
  const EmbeddedFont regular = InterRegular(), semibold = InterSemiBold(), icons = FontAwesomeSolid();
  auto add = [&](const EmbeddedFont& face, float size, bool with_icons) -> ImFont* {
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.OversampleH = config.OversampleV = 1;
    config.PixelSnapH = true;
    ImFont* font = atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(face.data), int(face.size), size,
                                               &config, kText);
    if (font && with_icons) {
      ImFontConfig merge;
      merge.FontDataOwnedByAtlas = false;
      merge.MergeMode = true;
      merge.OversampleH = merge.OversampleV = 1;
      merge.PixelSnapH = true;
      merge.GlyphMinAdvanceX = size * 1.25f;  // icons line up in lists
      merge.GlyphOffset = ImVec2(0.0f, size * 0.02f);
      atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(icons.data), int(icons.size), size * 0.92f, &merge,
                                  kIcons);
    }
    return font;
  };
  for (size_t i = 0; i < fonts.regular.size(); ++i) fonts.regular[i] = add(regular, MenuFonts::kRegularSizes[i], true);
  for (size_t i = 0; i < fonts.semibold.size(); ++i)
    fonts.semibold[i] = add(semibold, MenuFonts::kSemiBoldSizes[i], false);
  fonts.loaded = std::all_of(fonts.regular.begin(), fonts.regular.end(), [](ImFont* f) { return f; }) &&
                 std::all_of(fonts.semibold.begin(), fonts.semibold.end(), [](ImFont* f) { return f; });
}

// Everything the menu scales by: `scale` multiplies design sizes into logical pixels
// (ImGui's units); `physical_per_logical` converts those into the pixels the font was
// baked for, so a DPI-scaled window still picks a sharp bake.
struct MenuMetrics {
  float scale = 1.0f;
  float physical_per_logical = 1.0f;
  float Px(float design) const { return design * scale; }
};
inline MenuMetrics ComputeMetrics(float logical_height, float physical_height, float user_scale) {
  MenuMetrics metrics;
  metrics.physical_per_logical = logical_height > 0 && physical_height > 0 ? physical_height / logical_height : 1.0f;
  // settings::MenuScale is in physical terms; bring it back to logical units.
  metrics.scale = settings::MenuScale(physical_height > 0 ? physical_height : logical_height, user_scale) /
                  metrics.physical_per_logical;
  return metrics;
}
// PushFont for a role at the current metrics. Returns false (and pushes nothing) when the
// embedded fonts did not load; callers then keep ImGui's default font.
inline bool PushRoleFont(FontRole role, const MenuMetrics& metrics) {
  const auto& fonts = Fonts();
  if (!fonts.loaded) return false;
  const float logical = metrics.Px(DesignSize(role));
  const float physical = logical * metrics.physical_per_logical;
  ImFont* font = nullptr;
  if (role == FontRole::kHeading || role == FontRole::kTitle)
    font = fonts.semibold[settings::PickBakedSize(MenuFonts::kSemiBoldSizes, physical)];
  else
    font = fonts.regular[settings::PickBakedSize(MenuFonts::kRegularSizes, physical)];
  ImGui::PushFont(font, logical);
  return true;
}
struct ScopedFont {
  bool pushed;
  ScopedFont(FontRole role, const MenuMetrics& metrics) : pushed(PushRoleFont(role, metrics)) {}
  ~ScopedFont() { if (pushed) ImGui::PopFont(); }
  ScopedFont(const ScopedFont&) = delete;
  ScopedFont& operator=(const ScopedFont&) = delete;
};

// ---- Theme ---------------------------------------------------------------------------
// Applied over a copy of the current style (so the font-size fields the SDK set stay),
// for the duration of the menu's draw; the caller restores the previous style afterwards
// so the SDK's own overlays keep theirs.
inline void ApplyMenuStyle(ImGuiStyle& style, float scale) {
  using namespace color;
  style.Alpha = 1.0f;
  style.DisabledAlpha = 0.45f;
  style.WindowPadding = ImVec2(22, 20);
  style.WindowRounding = 10;
  style.WindowBorderSize = 1;
  style.ChildRounding = 8;
  style.ChildBorderSize = 0;
  style.PopupRounding = 8;
  style.PopupBorderSize = 1;
  style.FramePadding = ImVec2(12, 8);
  style.FrameRounding = 6;
  style.FrameBorderSize = 0;
  style.ItemSpacing = ImVec2(12, 10);
  style.ItemInnerSpacing = ImVec2(10, 8);
  style.CellPadding = ImVec2(10, 7);
  style.IndentSpacing = 22;
  style.ScrollbarSize = 14;
  style.ScrollbarRounding = 7;
  style.GrabMinSize = 16;
  style.GrabRounding = 6;
  style.TabRounding = 6;
  style.SeparatorTextBorderSize = 2;
  style.SeparatorTextPadding = ImVec2(0, 6);
  style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
  style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
  style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
  style.AntiAliasedLines = style.AntiAliasedFill = true;
  style.ScaleAllSizes(scale);

  ImVec4* c = style.Colors;
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_TextDisabled] = kTextDim;
  c[ImGuiCol_WindowBg] = kPanel;
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = ImVec4(0.055f, 0.086f, 0.075f, 0.985f);
  c[ImGuiCol_Border] = kBorder;
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = kFrame;
  c[ImGuiCol_FrameBgHovered] = kFrameHover;
  c[ImGuiCol_FrameBgActive] = kFrameActive;
  c[ImGuiCol_TitleBg] = kSidebar;
  c[ImGuiCol_TitleBgActive] = kSidebar;
  c[ImGuiCol_TitleBgCollapsed] = kSidebar;
  c[ImGuiCol_MenuBarBg] = kSidebar;
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0.15f);
  c[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.30f, 0.26f, 1.0f);
  c[ImGuiCol_ScrollbarGrabHovered] = kGreenDim;
  c[ImGuiCol_ScrollbarGrabActive] = kGreen;
  c[ImGuiCol_CheckMark] = kGreen;
  c[ImGuiCol_SliderGrab] = kGreenDim;
  c[ImGuiCol_SliderGrabActive] = kGreen;
  c[ImGuiCol_Button] = kFrame;
  c[ImGuiCol_ButtonHovered] = kFrameHover;
  c[ImGuiCol_ButtonActive] = kFrameActive;
  c[ImGuiCol_Header] = ImVec4(0.235f, 0.878f, 0.627f, 0.16f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.235f, 0.878f, 0.627f, 0.24f);
  c[ImGuiCol_HeaderActive] = ImVec4(0.235f, 0.878f, 0.627f, 0.34f);
  c[ImGuiCol_Separator] = kBorder;
  c[ImGuiCol_SeparatorHovered] = kGreenDim;
  c[ImGuiCol_SeparatorActive] = kGreen;
  c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ResizeGripHovered] = kGreenDim;
  c[ImGuiCol_ResizeGripActive] = kGreen;
  c[ImGuiCol_InputTextCursor] = kGreen;
  c[ImGuiCol_Tab] = kFrame;
  c[ImGuiCol_TabHovered] = kFrameHover;
  c[ImGuiCol_TabSelected] = kFrameActive;
  c[ImGuiCol_TabSelectedOverline] = kGreen;
  c[ImGuiCol_TabDimmed] = kFrame;
  c[ImGuiCol_TabDimmedSelected] = kFrameActive;
  c[ImGuiCol_TabDimmedSelectedOverline] = kGreenDim;
  c[ImGuiCol_PlotLines] = kGreen;
  c[ImGuiCol_PlotLinesHovered] = kOrange;
  c[ImGuiCol_PlotHistogram] = kGreen;
  c[ImGuiCol_PlotHistogramHovered] = kOrange;
  c[ImGuiCol_TableHeaderBg] = kFrame;
  c[ImGuiCol_TableBorderStrong] = kBorder;
  c[ImGuiCol_TableBorderLight] = ImVec4(0.12f, 0.18f, 0.155f, 1.0f);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
  c[ImGuiCol_TextSelectedBg] = ImVec4(0.235f, 0.878f, 0.627f, 0.30f);
  c[ImGuiCol_NavCursor] = kOrange;  // the pad/keyboard cursor is the orange highlight
  c[ImGuiCol_NavWindowingHighlight] = kOrange;
  c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.5f);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.02f, 0.01f, 0.55f);
}
struct ScopedMenuStyle {
  ImGuiStyle saved;
  explicit ScopedMenuStyle(float scale) : saved(ImGui::GetStyle()) { ApplyMenuStyle(ImGui::GetStyle(), scale); }
  ~ScopedMenuStyle() { ImGui::GetStyle() = saved; }
  ScopedMenuStyle(const ScopedMenuStyle&) = delete;
  ScopedMenuStyle& operator=(const ScopedMenuStyle&) = delete;
};

// ---- Widgets -------------------------------------------------------------------------
// Dims the whole game image behind the menu, darker at the edges.
inline void DrawBackdrop(const ImVec2& size) {
  ImDrawList* draw = ImGui::GetBackgroundDrawList();
  draw->AddRectFilled(ImVec2(0, 0), size, IM_COL32(4, 10, 8, 150));
  const ImU32 edge = IM_COL32(0, 0, 0, 110), clear = IM_COL32(0, 0, 0, 0);
  const float band = size.y * 0.22f;
  draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x, band), edge, edge, clear, clear);
  draw->AddRectFilledMultiColor(ImVec2(0, size.y - band), size, clear, clear, edge, edge);
}

// A small rounded label drawn inline, e.g. "RESTART". Filled when `strong`.
inline void Badge(const char* text, ImVec4 tint, bool strong, float scale) {
  const ImVec2 text_size = ImGui::CalcTextSize(text);
  const ImVec2 pad(7.0f * scale, 2.0f * scale);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float height = text_size.y + pad.y * 2;
  const float y = pos.y + (ImGui::GetFrameHeight() - height) * 0.5f;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 min(pos.x, y), max(pos.x + text_size.x + pad.x * 2, y + height);
  if (strong) draw->AddRectFilled(min, max, color::U32(tint, 0.95f), height * 0.5f);
  else draw->AddRect(min, max, color::U32(tint, 0.9f), height * 0.5f, 0, 1.2f * scale);
  draw->AddText(ImVec2(min.x + pad.x, min.y + pad.y), strong ? IM_COL32(12, 14, 12, 255) : color::U32(tint), text);
  ImGui::Dummy(ImVec2(max.x - min.x, ImGui::GetFrameHeight()));
}

// One sidebar entry: icon and label, a green bar on the left when selected.
inline bool SidebarEntry(const char* id, const char* glyph, const char* label, bool selected, float height,
                         float scale) {
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const bool pressed = ImGui::Selectable(id, selected, ImGuiSelectableFlags_None, ImVec2(width, height));
  ImDrawList* draw = ImGui::GetWindowDrawList();
  if (selected)
    draw->AddRectFilled(ImVec2(pos.x, pos.y + height * 0.18f), ImVec2(pos.x + 4.0f * scale, pos.y + height * 0.82f),
                        color::U32(color::kGreen), 2.0f * scale);
  const float text_y = pos.y + (height - ImGui::GetFontSize()) * 0.5f;
  const ImU32 tint = selected ? color::U32(color::kGreen) : color::U32(color::kText, 0.85f);
  draw->AddText(ImVec2(pos.x + 18.0f * scale, text_y), tint, glyph);
  draw->AddText(ImVec2(pos.x + 18.0f * scale + ImGui::GetFontSize() * 1.9f, text_y),
                selected ? color::U32(color::kText) : color::U32(color::kText, 0.8f), label);
  return pressed;
}

// A row of buttons, one selected. Returns true when the selection changed.
inline bool SegmentedControl(const char* id, int* index, std::span<const char* const> labels, float scale,
                             int highlight = -1) {
  bool changed = false;
  ImGui::PushID(id);
  const float spacing = 4.0f * scale;
  const float width = (ImGui::GetContentRegionAvail().x - spacing * float(labels.size() - 1)) / float(labels.size());
  for (size_t i = 0; i < labels.size(); ++i) {
    if (i) ImGui::SameLine(0, spacing);
    const bool selected = *index == int(i);
    const bool marked = highlight == int(i) && !selected;
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(0.235f, 0.878f, 0.627f, 0.85f) : color::kFrame);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? color::kGreen : color::kFrameHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color::kGreen);
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? ImVec4(0.03f, 0.07f, 0.05f, 1.0f)
                                                  : marked ? color::kOrange : color::kText);
    if (ImGui::Button(labels[i], ImVec2(width, 0))) {
      changed = *index != int(i);
      *index = int(i);
    }
    ImGui::PopStyleColor(4);
  }
  ImGui::PopID();
  return changed;
}

// Section heading: title in the heading font, a thin green rule under it.
inline void SectionHeading(const char* text, const MenuMetrics& metrics) {
  {
    ScopedFont font(FontRole::kHeading, metrics);
    ImGui::PushStyleColor(ImGuiCol_Text, color::kGreen);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
  }
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y), ImVec2(pos.x + ImGui::GetContentRegionAvail().x, pos.y),
                                      color::U32(color::kBorder), 1.0f);
  ImGui::Dummy(ImVec2(0, 6.0f * metrics.scale));
}

}  // namespace edf::ui
