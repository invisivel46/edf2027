// Scripted pad for automated runs: a synthetic input device (feeds guest user 0 under the default
// SlotAssignment; its state is merged with real pads). Enabled when the EDF_INPUT_SCRIPT environment
// variable is set: "default" = Start at 7 s then A every 3 s; otherwise a file of "start_ms dur_ms buttons_hex".
#pragma once
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/input/device_assignment.h>
#include <rex/logging.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include "scripted_input_logic.h"

class ScriptedInputDriver final : public rex::input::InputDriver {
 public:
  using Event = edf::ScriptedInputEvent;
  ScriptedInputDriver() : InputDriver(nullptr, 0) { Load(); }

  static bool Enabled() { const char* e = std::getenv("EDF_INPUT_SCRIPT"); return e && *e; }

  rex::X_STATUS Setup() override { return 0; }
  void Load() {
    const char* e = std::getenv("EDF_INPUT_SCRIPT");
    std::ifstream f(e ? e : "");
    if (e && std::string(e) != "default" && f) {
      events_ = edf::ParseInputEvents(f);
    } else {
      events_ = edf::DefaultInputEvents();
    }
    t0_ = std::chrono::steady_clock::now();
    REXLOG_INFO("Scripted pad: {} events", events_.size());
  }
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    rex::input::DeviceInfo d; d.id = static_cast<rex::input::DeviceId>(0x5C71A7ED); d.name = "Scripted pad";
    d.guid = "scripted"; d.synthetic = true; out.push_back(d);
  }
  rex::X_RESULT GetDeviceState(rex::input::DeviceId id, rex::input::X_INPUT_STATE* out) override {
    if (!started_) { t0_ = std::chrono::steady_clock::now(); started_ = true; } // clock starts at first poll (Setup is not called for late drivers)
    const uint32_t ms = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_).count();
    uint16_t b = edf::ButtonsAt(events_, ms);
    if (packet_ < 3 || b != last_) REXLOG_INFO("Scripted pad: state buttons=0x{:04X} at {} ms (calls {})", b, ms, packet_);
    if (last_ != 0xFFFF && b != last_) {
      // VK_PAD_START = 0x5814, VK_PAD_A = 0x5800 (XInput virtual keys)
      for (const auto& key : edf::ButtonTransitions(last_, b)) pending_.push_back({key.virtual_key, key.flags});
    }
    last_ = b;
    *out = {};
    out->packet_number = ++packet_;
    out->gamepad.buttons = b;
    return 0;
  }
  rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId, uint32_t, rex::input::X_INPUT_CAPABILITIES* caps) override {
    *caps = {}; caps->type = 1; caps->sub_type = 1; caps->gamepad.buttons = 0xFFFF; return 0;
  }
  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId, rex::input::X_INPUT_VIBRATION*) override { return 0; }
  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId, uint32_t, rex::input::X_INPUT_KEYSTROKE* ks) override {
    if (pending_.empty()) return 0x48F; /* ERROR_EMPTY */
    *ks = {}; ks->virtual_key = pending_.front().first; ks->flags = pending_.front().second; ks->user_index = 0;
    pending_.erase(pending_.begin());
    return 0;
  }

 private:
  std::vector<Event> events_;
  std::chrono::steady_clock::time_point t0_;
  uint32_t packet_ = 0;
  uint16_t last_ = 0xFFFF;
  bool started_ = false;
  std::vector<std::pair<uint16_t, uint16_t>> pending_;
};

inline std::unique_ptr<rex::system::IInputSystem> CreateEdfInputSystem(bool tool_mode) {
  auto sys = rex::input::CreateDefaultInputSystem(tool_mode);
  if (ScriptedInputDriver::Enabled()) {
    sys->AddDriver(std::make_unique<ScriptedInputDriver>());
    sys->SetDeviceAssignment(std::make_unique<rex::input::SharedAssignment>());  // every device (incl. the scripted pad) feeds user 0
  }
  return sys;
}
