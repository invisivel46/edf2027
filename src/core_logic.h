#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "native_graphics/native_display_layout.h"

namespace edf {

inline uint32_t ReadBigEndian32(std::span<const uint8_t> bytes, size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 4) return 0;
  return (static_cast<uint32_t>(bytes[offset]) << 24) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
         static_cast<uint32_t>(bytes[offset + 3]);
}

inline uint32_t ParseXexTitleId(std::span<const uint8_t> header) {
  if (header.size() < 0x18 || header[0] != 'X' || header[1] != 'E' ||
      header[2] != 'X' || header[3] != '2')
    return 0;
  const uint32_t count = ReadBigEndian32(header, 0x14);
  const size_t available_entries = (header.size() - 0x18) / 8;
  for (size_t i = 0; i < std::min<size_t>({count, 256, available_entries}); ++i) {
    const size_t entry = 0x18 + i * 8;
    if (ReadBigEndian32(header, entry) == 0x00040006) {
      const uint32_t offset = ReadBigEndian32(header, entry + 4);
      return ReadBigEndian32(header, static_cast<size_t>(offset) + 12);
    }
  }
  return 0;
}

inline bool IsTransientConfigLine(std::string_view line) {
  constexpr std::array<std::string_view, 4> keys{
      "log_file", "game_data_root", "edf_show_settings", "settings"};
  for (const auto key : keys) {
    if (line.starts_with(key) && line.size() > key.size() &&
        (line[key.size()] == ' ' || line[key.size()] == '='))
      return true;
  }
  return false;
}

inline std::string FilterPersistentConfig(std::string_view config) {
  std::string output;
  size_t begin = 0;
  while (begin < config.size()) {
    const size_t end = config.find('\n', begin);
    const size_t length = end == std::string_view::npos ? config.size() - begin : end - begin;
    std::string_view line = config.substr(begin, length);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (!IsTransientConfigLine(line)) {
      output.append(line);
      output.push_back('\n');
    }
    if (end == std::string_view::npos) break;
    begin = end + 1;
  }
  return output;
}

struct VideoMode { int width; int height; };
// edf_native_render_width/height as a pair: (0,0) original 720 lines, (0,h) h lines
// in the window's shape, (0,-1) the window's size, (w,h) exactly w x h
// (native_display_layout.h ResolveNativeRenderSize).
inline bool ValidNativeRenderMode(int width,int height) {
  return native::ValidNativeRenderRequest(width,height);
}

inline bool ForcesConsoleAspect(std::string_view aspect) {
  return aspect == "letterbox" || aspect == "stretch";
}

// The video mode the guest is told about. Always 16:9: the engine reads only its
// widescreen flag from it (8219E3B8 picks 1280x720 or 1280x960 and the 2D canvas
// callbacks 820A4DD0/8216E630 shrink to 3/4 when the flag is clear), and the
// render size the window's shape needs is applied separately by the 82139A40
// hook. A 4:3 or 16:10 video mode would take the engine down its SD paths.
inline VideoMode GuestVideoMode(int width, int height, std::string_view aspect) {
  (void)aspect;
  if (static_cast<int64_t>(width) * 9 != static_cast<int64_t>(height) * 16) {
    width = height * 16 / 9;
    if (width < 1280) return {1280, 720};
  }
  return {width, height};
}

inline bool PresentLetterbox(std::string_view aspect) { return aspect != "stretch"; }
inline int AspectIndex(std::string_view value) {
  return value == "ultrawide" ? 1 : value == "letterbox" ? 2 : value == "stretch" ? 3 : 0;
}
inline std::string_view AspectValue(int index) {
  return index == 1 ? "ultrawide" : index == 2 ? "letterbox" : index == 3 ? "stretch" : "native";
}
inline int FpsCapValue(int index) { constexpr int values[]{0, 30, 60, 120}; return index >= 0 && index < 4 ? values[index] : 0; }
inline int RefreshValue(int index) { constexpr int values[]{60, 30, 120, 144}; return index >= 0 && index < 4 ? values[index] : 60; }
inline int ResolutionScaleValue(int index) { return std::clamp(index, 0, 3) + 1; }
inline int AnisotropicValue(int index) { return std::clamp(index, 0, 6) - 1; }
inline std::string_view FxaaValue(int index) { return index == 1 ? "fxaa" : index == 2 ? "fxaa_extreme" : "none"; }
// present_effect. fsr2/fsr3 drive the runtime's temporal upscaler from synthesized
// depth/motion inputs and may fall back to spatial FSR, so they are labelled experimental.
inline std::string_view UpscaleValue(int index) {
  return index == 1 ? "cas" : index == 2 ? "fsr" : index == 3 ? "fsr2" : index == 4 ? "fsr3" : "bilinear";
}
inline int UpscaleIndex(std::string_view value) {
  return value == "cas" ? 1 : value == "fsr" ? 2 : value == "fsr2" ? 3 : value == "fsr3" ? 4 : 0;
}

// present_fsr_quality_mode picks the render resolution the temporal upscaler runs at.
// The runtime only consults it on the fsr2/fsr3 paths - spatial FSR 1.0 upsamples from
// whatever the guest already rendered - so the control is meaningless for anything else.
inline bool UsesTemporalUpscaler(std::string_view effect) {
  return effect == "fsr2" || effect == "fsr3";
}
inline std::string_view FsrQualityValue(int index) {
  constexpr std::string_view values[]{"auto", "nativeaa", "quality", "balanced", "performance",
                                      "ultra_performance"};
  return index >= 0 && index < 6 ? values[index] : values[0];
}
inline int FsrQualityIndex(std::string_view value) {
  for (int i = 0; i < 6; ++i)
    if (FsrQualityValue(i) == value) return i;
  return 0;
}

// Chained EASU passes; the runtime clamps to [1, kFsrMaxUpscalingPassesMax].
inline int FsrPassesValue(int passes) { return std::clamp(passes, 1, 4); }

struct FrameSummary {
  double average_ms = 0, minimum_ms = 0, maximum_ms = 0, low_1_percent_ms = 0;
};

inline FrameSummary SummarizeFrameTimes(std::span<const double> samples) {
  if (samples.empty()) return {};
  std::vector<double> sorted(samples.begin(), samples.end());
  std::sort(sorted.begin(), sorted.end());
  double sum = 0;
  for (double value : sorted) sum += value;
  const size_t low_index = sorted.size() - 1 - static_cast<size_t>(sorted.size() * 0.01);
  return {sum / sorted.size(), sorted.front(), sorted.back(), sorted[low_index]};
}

inline int64_t FramePeriodNanoseconds(int fps_cap) {
  return fps_cap > 0 ? 1000000000LL / fps_cap : 0;
}

}  // namespace edf
