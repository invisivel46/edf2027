// EDF2027 - F1 settings menu: the pure logic behind it.
//
// Frame-rate choice <-> cvar mapping, graphics presets, which settings need a restart
// (and how many staged changes are waiting for one), the display-change revert timer,
// config-file overrides and the menu's resolution scaling. No SDK, ImGui or platform
// headers, so tests/unit_tests.cpp covers all of it.
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace edf::settings {

// ---- Frame rate ----------------------------------------------------------------------
// One control over two cvars: edf_native_unlock_framerate (render above the 60 Hz
// simulation, interpolated) and edf_fps_cap (the render limiter, 0 = off). "60 (locked)"
// is the original presentation: unlock off, no limiter.
struct FrameRateChoice {
  const char* label;
  bool unlock;
  int cap;
};
inline constexpr std::array<FrameRateChoice, 6> kFrameRateChoices{{
    {"60 (locked)", false, 0},
    {"120", true, 120},
    {"144", true, 144},
    {"165", true, 165},
    {"240", true, 240},
    {"Uncapped", true, 0},
}};

// The menu position for a cvar pair, or -1 when the pair is not one of the choices (a
// hand-edited config, or an older build's "unlock + cap 100"). Locked with a cap at or
// above 60 still presents at 60, so it reads as the locked choice; locked with a lower
// cap (the old "30" option) really runs slower and is kept as custom.
inline int FrameRateIndex(bool unlock, int cap) {
  if (!unlock) return cap <= 0 || cap >= 60 ? 0 : -1;
  for (size_t i = 1; i < kFrameRateChoices.size(); ++i)
    if (kFrameRateChoices[i].cap == std::max(cap, 0)) return static_cast<int>(i);
  return -1;
}
inline FrameRateChoice FrameRateAt(int index) {
  return index >= 0 && index < static_cast<int>(kFrameRateChoices.size()) ? kFrameRateChoices[size_t(index)]
                                                                          : kFrameRateChoices[0];
}

// ---- Native upscaling / anti-aliasing (edf_native_fsr) -------------------------------
inline constexpr std::array<std::string_view, 6> kNativeFsrValues{
    "off", "native_aa", "quality", "balanced", "performance", "ultra_performance"};
inline constexpr std::array<const char*, 6> kNativeFsrLabels{
    "Off", "Native AA (full resolution)", "Quality (1.5x)", "Balanced (1.7x)", "Performance (2x)",
    "Ultra performance (3x)"};
inline int NativeFsrIndex(std::string_view value) {
  for (size_t i = 0; i < kNativeFsrValues.size(); ++i)
    if (kNativeFsrValues[i] == value) return static_cast<int>(i);
  return 0;
}
// The upscaling modes' per-axis ratios (AMD's, as native_fsr.h's NativeFsrUpscaleRatio);
// 0 for off and native AA, which draw at the output size.
inline float NativeFsrUpscaleRatio(std::string_view value) {
  return value == "quality" ? 1.5f : value == "balanced" ? 1.7f : value == "performance" ? 2.0f
       : value == "ultra_performance" ? 3.0f : 0.0f;
}
// The scene's render size for an output size under an upscaling mode, as FidelityFX
// computes it: (int)(output / ratio) per axis in single precision. The output itself
// for the other modes.
inline std::pair<int, int> NativeFsrSceneSize(std::string_view value, int output_width, int output_height) {
  const float ratio = NativeFsrUpscaleRatio(value);
  if (ratio <= 0.0f) return {output_width, output_height};
  return {std::max(1, int(float(output_width) / ratio)), std::max(1, int(float(output_height) / ratio))};
}

// ---- Renderer preset (edf_native_renderer) -------------------------------------------
// The menu offers the two supported configurations; world/full are development presets
// and show as "custom" rather than being folded into one of them.
inline int RendererIndex(std::string_view value) {
  return value == "native" ? 0 : value == "off" || value.empty() ? 1 : -1;
}
inline std::string_view RendererValue(int index) { return index == 1 ? "off" : "native"; }

// ---- Graphics presets ----------------------------------------------------------------
enum class GraphicsPreset { kPerformance, kBalanced, kQuality, kUltra, kCustom };
inline constexpr std::array<const char*, 5> kGraphicsPresetLabels{"Performance", "Balanced", "Quality", "Ultra",
                                                                  "Custom"};
// anisotropic: edf_native_anisotropic_filtering (-1 game default, 0 off, 1..5 = 1x..16x).
// msaa: edf_native_msaa (0 game default = 2x, 1 off, 2, 4).
// fsr/sharpness: edf_native_fsr / edf_native_fsr_sharpness, only when those cvars exist.
struct GraphicsPresetValues {
  int anisotropic;
  int msaa;
  std::string_view fsr;
  double sharpness;
};
// With the native upscaler available, it does the anti-aliasing, so the presets turn
// MSAA off and let the upscaler's quality mode carry the ladder. Without it, MSAA and
// filtering are the only levers.
inline GraphicsPresetValues PresetValues(GraphicsPreset preset, bool fsr_available) {
  if (fsr_available) {
    switch (preset) {
      case GraphicsPreset::kPerformance: return {2, 1, "performance", 0.6};
      case GraphicsPreset::kBalanced: return {3, 1, "balanced", 0.5};
      case GraphicsPreset::kQuality: return {4, 1, "quality", 0.4};
      default: return {5, 1, "native_aa", 0.3};
    }
  }
  switch (preset) {
    case GraphicsPreset::kPerformance: return {2, 1, "off", 0.0};
    case GraphicsPreset::kBalanced: return {3, 0, "off", 0.0};
    case GraphicsPreset::kQuality: return {4, 0, "off", 0.0};
    default: return {5, 4, "off", 0.0};
  }
}
struct GraphicsState {
  int anisotropic = -1;
  int msaa = 0;
  std::string fsr = "off";
  double sharpness = 0.0;
};
inline GraphicsPreset MatchPreset(const GraphicsState& state, bool fsr_available) {
  for (int i = 0; i < 4; ++i) {
    const auto preset = static_cast<GraphicsPreset>(i);
    const auto values = PresetValues(preset, fsr_available);
    if (state.anisotropic != values.anisotropic || state.msaa != values.msaa) continue;
    if (fsr_available &&
        (state.fsr != values.fsr || std::fabs(state.sharpness - values.sharpness) > 0.005))
      continue;
    return preset;
  }
  return GraphicsPreset::kCustom;
}

// ---- Restart-required settings -------------------------------------------------------
// What the running game reads once (at startup, or once per thread) versus live. The
// menu never writes a restart-only cvar while the game runs - several of them (the scene
// backend, the input device list) are also read live by code that would break if they
// changed underneath it. Their new values are staged and written to the config file.
inline constexpr std::array<std::string_view, 16> kRestartCvars{
    "edf_native_renderer", "edf_native_scene_backend", "edf_native_backend", "edf_native_seam_draws",
    "edf_native_render_width", "edf_native_render_height", "edf_native_msaa", "edf_aspect",
    "present_letterbox", "window_width", "window_height", "video_mode_width", "video_mode_height",
    "edf_kbm", "edf_native_thread_qos", "present_effect"};
// `description` is the cvar's own help text, for cvars this file does not know (the
// upscaler's, owned by the renderer): one that says it needs a restart is treated so.
inline bool NeedsRestart(std::string_view cvar, std::string_view description = {}) {
  for (auto name : kRestartCvars)
    if (name == cvar) return true;
  if (cvar == "input_backend") return true;  // the SDK creates its input drivers once, at boot
  if (cvar.starts_with("present_") || cvar == "swap_post_effect") return true;  // classic presenter
  std::string lower(description);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return lower.find("restart") != std::string::npos || lower.find("at startup") != std::string::npos;
}

// Staged restart-only changes, grouped by the setting that made them (one "window size"
// change writes four cvars but is one change to the player).
class RestartTracker {
 public:
  using Getter = std::function<std::string(std::string_view)>;
  void Stage(const std::string& setting, std::vector<std::pair<std::string, std::string>> values) {
    staged_[setting] = std::move(values);
  }
  void Unstage(const std::string& setting) { staged_.erase(setting); }
  void Clear() { staged_.clear(); }
  // The value the next launch will use: staged if any, else the running one.
  std::string Pending(std::string_view cvar, const Getter& current) const {
    std::string value = current(cvar);
    for (const auto& [setting, values] : staged_)
      for (const auto& [name, staged] : values)
        if (name == cvar) value = staged;
    return value;
  }
  bool IsStaged(const std::string& setting) const { return staged_.contains(setting); }
  // The staged values of one setting (empty when it is not staged).
  std::vector<std::pair<std::string, std::string>> Values(const std::string& setting) const {
    const auto found = staged_.find(setting);
    return found == staged_.end() ? std::vector<std::pair<std::string, std::string>>{} : found->second;
  }
  // Staged and different from what is running: a restart would change it.
  bool Differs(const std::string& setting, const Getter& current) const {
    for (const auto& [name, value] : Values(setting))
      if (current(name) != value) return true;
    return false;
  }
  // Settings whose staged values differ from what is running. A change staged and then
  // set back does not count.
  int Count(const Getter& current) const {
    int count = 0;
    for (const auto& [setting, values] : staged_) {
      for (const auto& [name, value] : values) {
        if (current(name) != value) { ++count; break; }
      }
    }
    return count;
  }
  // Every staged cvar value, last writer wins, for the config file.
  std::vector<std::pair<std::string, std::string>> Overrides() const {
    std::map<std::string, std::string> merged;
    for (const auto& [setting, values] : staged_)
      for (const auto& [name, value] : values) merged[name] = value;
    return {merged.begin(), merged.end()};
  }
  bool empty() const { return staged_.empty(); }

 private:
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> staged_;
};

// ---- Config file overrides -----------------------------------------------------------
// The SDK writes the config as flat "name = value" lines, non-default values only. A
// staged restart setting has to reach the file without being set on the running game, so
// its line is replaced (or dropped, when the staged value is the default) here.
struct ConfigOverride {
  std::string name;
  std::string literal;     // TOML right-hand side, already quoted for strings
  bool is_default = false; // the SDK omits defaults; so does this
};
inline std::string ApplyConfigOverrides(std::string_view config, std::span<const ConfigOverride> overrides) {
  auto key_of = [](std::string_view line) {
    const size_t end = line.find_first_of(" =");
    return end == std::string_view::npos ? std::string_view{} : line.substr(0, end);
  };
  std::string output;
  size_t begin = 0;
  while (begin < config.size()) {
    const size_t end = config.find('\n', begin);
    std::string_view line = config.substr(begin, end == std::string_view::npos ? config.size() - begin : end - begin);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const auto key = key_of(line);
    const bool replaced = !key.empty() && std::any_of(overrides.begin(), overrides.end(),
                                                      [&](const ConfigOverride& o) { return o.name == key; });
    if (!replaced) { output.append(line); output.push_back('\n'); }
    if (end == std::string_view::npos) break;
    begin = end + 1;
  }
  for (const auto& o : overrides)
    if (!o.is_default) output += o.name + " = " + o.literal + "\n";
  return output;
}

// ---- Display-change confirmation -----------------------------------------------------
// "Keep these settings? Reverting in 10 s". Times are seconds on any monotonic clock.
class RevertCountdown {
 public:
  static constexpr double kDefaultSeconds = 10.0;
  void Start(double now, double seconds = kDefaultSeconds) { active_ = true; deadline_ = now + seconds; }
  bool active() const { return active_; }
  // Whole seconds left, rounded up, for the label; 0 once expired.
  int SecondsLeft(double now) const {
    if (!active_) return 0;
    return std::max(0, static_cast<int>(std::ceil(deadline_ - now)));
  }
  // True exactly once: the call that finds the deadline passed. The caller reverts.
  bool Expire(double now) {
    if (!active_ || now < deadline_) return false;
    active_ = false;
    return true;
  }
  void Keep() { active_ = false; }
  void Cancel() { active_ = false; }

 private:
  bool active_ = false;
  double deadline_ = 0.0;
};

// ---- Resolution scaling --------------------------------------------------------------
// Everything in the menu is designed at 1080p and scaled by the window's physical height,
// so it is the same fraction of the screen at 720p, 1440p and 4K. `user` is the menu
// scale setting (edf_menu_scale).
inline float MenuScale(float physical_height, float user = 1.0f) {
  const float base = physical_height > 0 ? physical_height / 1080.0f : 1.0f;
  return std::clamp(base, 0.75f, 4.0f) * std::clamp(user, 0.5f, 2.0f);
}
// The atlas cannot rasterize new sizes at run time (the SDK's renderer has no dynamic
// font textures), so each font is baked at a few sizes. Pick the smallest bake at least
// as large as the size wanted, so text is only ever scaled down; the largest otherwise.
inline size_t PickBakedSize(std::span<const float> ascending_sizes, float wanted) {
  for (size_t i = 0; i < ascending_sizes.size(); ++i)
    if (ascending_sizes[i] >= wanted * 0.97f) return i;
  return ascending_sizes.empty() ? 0 : ascending_sizes.size() - 1;
}

}  // namespace edf::settings
