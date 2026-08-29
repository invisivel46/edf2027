#pragma once

#include <cstdint>
#include <istream>
#include <sstream>
#include <string>
#include <vector>

namespace edf {

struct ScriptedInputEvent { uint32_t start_ms, duration_ms; uint16_t buttons; };

inline std::vector<ScriptedInputEvent> DefaultInputEvents() {
  std::vector<ScriptedInputEvent> events{{7000, 300, 0x0010}};
  for (uint32_t time = 11000; time <= 56000; time += 3000) events.push_back({time, 300, 0x1000});
  return events;
}

inline std::vector<ScriptedInputEvent> ParseInputEvents(std::istream& input) {
  std::vector<ScriptedInputEvent> events;
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream fields(line);
    uint64_t start = 0, duration = 0, buttons = 0;
    std::string hex, extra;
    if (!(fields >> start >> duration >> hex) || (fields >> extra)) continue;
    try {
      size_t consumed = 0;
      buttons = std::stoull(hex, &consumed, 16);
      if (consumed != hex.size() || start > UINT32_MAX || duration > UINT32_MAX || buttons > UINT16_MAX) continue;
    } catch (...) { continue; }
    events.push_back({static_cast<uint32_t>(start), static_cast<uint32_t>(duration), static_cast<uint16_t>(buttons)});
  }
  return events;
}

inline uint16_t ButtonsAt(std::span<const ScriptedInputEvent> events, uint32_t elapsed_ms) {
  uint16_t buttons = 0;
  for (const auto& event : events) {
    const uint64_t end = static_cast<uint64_t>(event.start_ms) + event.duration_ms;
    if (elapsed_ms >= event.start_ms && elapsed_ms < end) buttons |= event.buttons;
  }
  return buttons;
}

struct ScriptedKeystroke { uint16_t virtual_key, flags; };
inline std::vector<ScriptedKeystroke> ButtonTransitions(uint16_t previous, uint16_t current) {
  std::vector<ScriptedKeystroke> result;
  if (previous == 0xFFFF) return result;
  if ((current ^ previous) & 0x0010) result.push_back({0x5814, static_cast<uint16_t>((current & 0x0010) ? 1 : 2)});
  if ((current ^ previous) & 0x1000) result.push_back({0x5800, static_cast<uint16_t>((current & 0x1000) ? 1 : 2)});
  return result;
}

}  // namespace edf
