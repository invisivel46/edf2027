#pragma once

#include <cstdint>
#include <istream>
#include <sstream>
#include <string>
#include <vector>
#include <span>
#include <filesystem>
#include <fstream>
#include <optional>
#include <array>

namespace edf {

// Optional decimal analog fields: LT RT LX LY RX RY. Buttons remain hex.
using ScriptedAnalog = std::array<int32_t,6>;
struct ScriptedInputEvent {
  uint32_t start_ms, duration_ms; uint16_t buttons;
  ScriptedAnalog analog{};
  bool has_analog=false;
};

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
    if (!(fields >> start >> duration >> hex)) continue;
    try {
      size_t consumed = 0;
      buttons = std::stoull(hex, &consumed, 16);
      if (consumed != hex.size() || start > UINT32_MAX || duration > UINT32_MAX || buttons > UINT16_MAX) continue;
    } catch (...) { continue; }
    ScriptedInputEvent event{static_cast<uint32_t>(start), static_cast<uint32_t>(duration), static_cast<uint16_t>(buttons)};
    fields >> std::ws;
    if(!fields.eof()) {
      bool valid=true;
      for(size_t i=0;i<event.analog.size();++i) {
        int64_t value=0;
        if(!(fields>>value) || value<(i<2?0:INT16_MIN) || value>(i<2?UINT8_MAX:INT16_MAX)) {valid=false; break;}
        event.analog[i]=static_cast<int32_t>(value);
      }
      if(!valid || (fields>>extra)) continue;
      event.has_analog=true;
    }
    events.push_back(event);
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

// Last active analog event in file order wins (including explicit zeroes).
// Button-only events do not interrupt analog holds. Expiry releases all axes.
inline ScriptedAnalog AnalogAt(std::span<const ScriptedInputEvent> events,uint32_t elapsed_ms) {
  ScriptedAnalog result{};
  for(const auto& event:events)
    if(event.has_analog && elapsed_ms>=event.start_ms &&
       uint64_t(elapsed_ms)<uint64_t(event.start_ms)+event.duration_ms) result=event.analog;
  return result;
}

// Replace only after a readable, changed file has been parsed. The caller owns
// elapsed time: reloading never restarts the schedule or replays past presses.
inline bool ReloadInputEvents(const std::filesystem::path& path,
    std::optional<std::filesystem::file_time_type>& previous_write,
    std::vector<ScriptedInputEvent>& events) {
  std::error_code error;
  const auto written=std::filesystem::last_write_time(path,error);
  if(error || (previous_write && *previous_write==written)) return false;
  std::ifstream input(path);
  if(!input) return false;
  auto replacement=ParseInputEvents(input);
  if(input.bad()) return false;
  events=std::move(replacement);
  previous_write=written;
  return true;
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
