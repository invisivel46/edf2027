// EDF2027 - controller input for the F1 menu.
//
// While the menu is open the game gets no pad input (pause_menu.h), so the menu reads
// the controllers itself, through RawPads: the physical pads of whichever input backend
// the SDK is running (input_backend), before the F1 chord, dead zones or remapping.
//   sdl     every SDL gamepad the SDK's SDL driver has open.
//   xinput  the four XInput slots, read straight from xinput1_4.dll (the library the
//           SDK's XInput driver uses). An empty slot is not asked again for a second,
//           as the SDK does, since XInputGetState on an empty slot is slow.
// Pads come and go on their own under both (hot-plug): SDL through its device events,
// XInput by the per-frame poll.
//
// MenuGamepad merges them, samples once per menu frame on the UI thread and feeds ImGui's
// gamepad navigation. Buttons already held when the menu opened (the chord that opened
// it) are ignored until released, so they do not activate the first control; the same
// mask is used after the controller-remap capture (settings_dialog.h) takes a button.
#pragma once

#include <SDL3/SDL.h>
#include <imgui.h>
#include <rex/cvar.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>
#endif
#include "pause_menu.h"

namespace edf::ui {

// ---- Physical pads, per backend ------------------------------------------------------
struct RawPad {
  std::string name;
  menu::PadSnapshot pad;
};

class RawPads {
 public:
  enum class Backend { kNone, kSdl, kXInput };

  // The backend the running SDK uses. A staged change of input_backend is not running yet.
  static Backend Active() {
#if defined(_WIN32)
    if (rex::cvar::GetFlagByName("input_backend") == "xinput") return XInput().ready() ? Backend::kXInput : Backend::kNone;
#endif
    return SDL_WasInit(SDL_INIT_GAMEPAD) ? Backend::kSdl : Backend::kNone;
  }
  static const char* BackendName(Backend backend) {
    switch (backend) {
      case Backend::kSdl: return "SDL";
      case Backend::kXInput: return "XInput";
      default: return "none";
    }
  }

  // Every connected pad, as the hardware reports it.
  static void Poll(std::vector<RawPad>& out) {
    out.clear();
    switch (Active()) {
      case Backend::kSdl: PollSdl(out); break;
#if defined(_WIN32)
      case Backend::kXInput: XInput().Poll(out); break;
#endif
      default: break;
    }
  }

  // Stops any vibration in flight. The game's own vibration holds until it changes it,
  // and it does not while the menu pauses it.
  static void StopRumble() {
    switch (Active()) {
      case Backend::kSdl: {
        int count = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
          for (int i = 0; i < count; ++i)
            if (SDL_Gamepad* gamepad = SDL_GetGamepadFromID(ids[i])) SDL_RumbleGamepad(gamepad, 0, 0, 0);
          SDL_free(ids);
        }
        break;
      }
#if defined(_WIN32)
      case Backend::kXInput: XInput().StopRumble(); break;
#endif
      default: break;
    }
  }

 private:
  static void PollSdl(std::vector<RawPad>& out) {
    SDL_UpdateGamepads();
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) return;
    for (int i = 0; i < count; ++i) {
      SDL_Gamepad* gamepad = SDL_GetGamepadFromID(ids[i]);  // opened by the SDK's driver
      if (!gamepad) continue;
      RawPad raw;
      const char* name = SDL_GetGamepadName(gamepad);
      raw.name = name ? name : "Gamepad";
      ReadSdl(gamepad, raw.pad);
      out.push_back(std::move(raw));
    }
    SDL_free(ids);
  }
  static void ReadSdl(SDL_Gamepad* gamepad, menu::PadSnapshot& pad) {
    struct Map { SDL_GamepadButton button; uint16_t bit; };
    static constexpr Map kButtons[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, menu::kPadA}, {SDL_GAMEPAD_BUTTON_EAST, menu::kPadB},
        {SDL_GAMEPAD_BUTTON_WEST, menu::kPadX}, {SDL_GAMEPAD_BUTTON_NORTH, menu::kPadY},
        {SDL_GAMEPAD_BUTTON_START, menu::kPadStart}, {SDL_GAMEPAD_BUTTON_BACK, menu::kPadBack},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, menu::kPadLeftShoulder},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, menu::kPadRightShoulder},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, menu::kPadLeftThumb}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, menu::kPadRightThumb},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, menu::kPadUp}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, menu::kPadDown},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, menu::kPadLeft}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, menu::kPadRight}};
    for (const auto& map : kButtons)
      if (SDL_GetGamepadButton(gamepad, map.button)) pad.buttons |= map.bit;
    const auto axis = [&](SDL_GamepadAxis a) { return SDL_GetGamepadAxis(gamepad, a); };
    // XInput conventions, as the SDK's driver reports them: up is positive.
    const auto flip = [](int16_t v) { return int16_t(std::clamp(-int(v), -32768, 32767)); };
    pad.lx = axis(SDL_GAMEPAD_AXIS_LEFTX);
    pad.ly = flip(axis(SDL_GAMEPAD_AXIS_LEFTY));
    pad.rx = axis(SDL_GAMEPAD_AXIS_RIGHTX);
    pad.ry = flip(axis(SDL_GAMEPAD_AXIS_RIGHTY));
    pad.left_trigger = uint8_t(std::clamp(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >> 7, 0, 255));
    pad.right_trigger = uint8_t(std::clamp(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >> 7, 0, 255));
  }

#if defined(_WIN32)
  class XInputPads {
   public:
    XInputPads() {
      module_ = LoadLibraryW(L"xinput1_4.dll");
      if (!module_) return;
      get_ = reinterpret_cast<GetState>(GetProcAddress(module_, "XInputGetState"));
      set_ = reinterpret_cast<SetState>(GetProcAddress(module_, "XInputSetState"));
    }
    bool ready() const { return get_ != nullptr; }
    void Poll(std::vector<RawPad>& out) {
      if (!get_) return;
      const uint64_t now = GetTickCount64();
      for (DWORD slot = 0; slot < kSlots; ++slot) {
        if (now < retry_at_[slot]) continue;
        XINPUT_STATE state{};
        if (get_(slot, &state) != ERROR_SUCCESS) {
          retry_at_[slot] = now + kRetryMs;
          continue;
        }
        RawPad raw;
        raw.name = "XInput controller " + std::to_string(slot + 1);
        const XINPUT_GAMEPAD& g = state.Gamepad;
        raw.pad = {g.wButtons, g.bLeftTrigger, g.bRightTrigger, g.sThumbLX, g.sThumbLY, g.sThumbRX, g.sThumbRY};
        out.push_back(std::move(raw));
      }
    }
    void StopRumble() {
      if (!set_) return;
      const uint64_t now = GetTickCount64();
      for (DWORD slot = 0; slot < kSlots; ++slot) {
        if (now < retry_at_[slot]) continue;
        XINPUT_VIBRATION off{};
        set_(slot, &off);
      }
    }

   private:
    using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    using SetState = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
    static constexpr DWORD kSlots = 4;
    static constexpr uint64_t kRetryMs = 1000;
    HMODULE module_ = nullptr;  // kept for the process's lifetime
    GetState get_ = nullptr;
    SetState set_ = nullptr;
    std::array<uint64_t, kSlots> retry_at_{};
  };
  static XInputPads& XInput() {  // UI thread only
    static XInputPads pads;
    return pads;
  }
#endif
};

// ---- Menu navigation -----------------------------------------------------------------
class MenuGamepad {
 public:
  struct Frame {
    bool connected = false;
    uint16_t held = 0;     // XInput bits (menu::PadButton), after the open-time mask
    uint16_t pressed = 0;  // went down this frame
    menu::PadSnapshot raw;  // every pad merged, no mask: what a remap capture reads
  };

  // Call when the menu opens: forget edges, and ignore whatever is held right now.
  void Reset() { first_ = true; previous_ = 0; masked_ = 0; }
  // Call when the menu closes: ImGui must not keep a button held until the next open.
  void Release(ImGuiIO& io) {
    Feed(io, Frame{}, menu::PadSnapshot{});
    previous_ = 0;
  }
  // Ignore the buttons held now until they are released (a remap capture just took one).
  void MaskHeld() { masked_ |= last_buttons_; previous_ = 0; }

  // `feed`: false while a remap capture owns the pad, so its buttons do not also drive
  // the menu; ImGui then sees a pad with nothing held.
  Frame Poll(ImGuiIO& io, bool feed = true) {
    Frame frame;
    RawPads::Poll(pads_);
    menu::PadSnapshot& pad = frame.raw;
    for (const auto& raw : pads_) Merge(raw.pad, pad);
    frame.connected = !pads_.empty();
    last_buttons_ = pad.buttons;
    if (first_) { masked_ = pad.buttons; first_ = false; }
    masked_ &= pad.buttons;  // a masked button counts again once it has been released
    frame.held = uint16_t(pad.buttons & ~masked_);
    frame.pressed = uint16_t(frame.held & ~previous_);
    previous_ = frame.held;
    if (feed) {
      Feed(io, frame, pad);
    } else {
      Frame idle;
      idle.connected = frame.connected;
      Feed(io, idle, menu::PadSnapshot{});
    }
    return frame;
  }
  // The pads seen by the last Poll.
  const std::vector<RawPad>& pads() const { return pads_; }

 private:
  static void Merge(const menu::PadSnapshot& from, menu::PadSnapshot& pad) {
    auto strongest = [](int16_t a, int16_t b) { return std::abs(int(b)) > std::abs(int(a)) ? b : a; };
    pad.buttons |= from.buttons;
    pad.lx = strongest(pad.lx, from.lx);
    pad.ly = strongest(pad.ly, from.ly);
    pad.rx = strongest(pad.rx, from.rx);
    pad.ry = strongest(pad.ry, from.ry);
    pad.left_trigger = std::max(pad.left_trigger, from.left_trigger);
    pad.right_trigger = std::max(pad.right_trigger, from.right_trigger);
  }

  // X (FaceLeft) is left out on purpose: holding it opens ImGui's window switcher, which
  // has no place in this menu.
  static void Feed(ImGuiIO& io, const Frame& frame, const menu::PadSnapshot& pad) {
    if (frame.connected) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    else io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    auto key = [&](ImGuiKey imgui, uint16_t bit) { io.AddKeyEvent(imgui, (frame.held & bit) != 0); };
    key(ImGuiKey_GamepadFaceDown, menu::kPadA);
    key(ImGuiKey_GamepadFaceRight, menu::kPadB);
    key(ImGuiKey_GamepadFaceUp, menu::kPadY);
    key(ImGuiKey_GamepadStart, menu::kPadStart);
    key(ImGuiKey_GamepadBack, menu::kPadBack);
    key(ImGuiKey_GamepadL1, menu::kPadLeftShoulder);
    key(ImGuiKey_GamepadR1, menu::kPadRightShoulder);
    key(ImGuiKey_GamepadL3, menu::kPadLeftThumb);
    key(ImGuiKey_GamepadR3, menu::kPadRightThumb);
    key(ImGuiKey_GamepadDpadUp, menu::kPadUp);
    key(ImGuiKey_GamepadDpadDown, menu::kPadDown);
    key(ImGuiKey_GamepadDpadLeft, menu::kPadLeft);
    key(ImGuiKey_GamepadDpadRight, menu::kPadRight);
    auto analog = [&](ImGuiKey imgui, float value) {
      constexpr float kDeadZone = 0.28f;
      const float v = std::clamp((value - kDeadZone) / (1.0f - kDeadZone), 0.0f, 1.0f);
      io.AddKeyAnalogEvent(imgui, v > 0.1f, v);
    };
    // XInput convention: up is positive.
    const float lx = pad.lx / 32767.0f, ly = pad.ly / 32767.0f;
    analog(ImGuiKey_GamepadLStickLeft, -lx);
    analog(ImGuiKey_GamepadLStickRight, lx);
    analog(ImGuiKey_GamepadLStickUp, ly);
    analog(ImGuiKey_GamepadLStickDown, -ly);
    analog(ImGuiKey_GamepadL2, pad.left_trigger / 255.0f);
    analog(ImGuiKey_GamepadR2, pad.right_trigger / 255.0f);
  }

  bool first_ = true;
  uint16_t previous_ = 0, masked_ = 0, last_buttons_ = 0;
  std::vector<RawPad> pads_;
};

}  // namespace edf::ui
