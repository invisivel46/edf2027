// EDF2027 - controller input for the F1 menu.
//
// While the menu is open the game gets no pad input (pause_menu.h), so the menu reads
// the controllers itself: every SDL gamepad the SDK's input driver has open, merged,
// sampled on the UI thread once per menu frame and fed to ImGui's gamepad navigation.
// Buttons already held when the menu opened (the chord that opened it) are ignored until
// released, so they do not activate the first control.
//
// Only SDL gamepads: with --input_backend xinput the SDK never starts SDL's gamepad
// subsystem, and the menu is then keyboard and mouse only.
#pragma once

#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "pause_menu.h"

namespace edf::ui {

class MenuGamepad {
 public:
  struct Frame {
    bool connected = false;
    uint16_t held = 0;     // XInput bits (menu::PadButton), after the open-time mask
    uint16_t pressed = 0;  // went down this frame
  };

  // Call when the menu opens: forget edges, and ignore whatever is held right now.
  void Reset() { first_ = true; previous_ = 0; masked_ = 0; }
  // Call when the menu closes: ImGui must not keep a button held until the next open.
  void Release(ImGuiIO& io) {
    Feed(io, Frame{}, menu::PadSnapshot{});
    previous_ = 0;
  }

  Frame Poll(ImGuiIO& io) {
    Frame frame;
    menu::PadSnapshot pad;
    if (SDL_WasInit(SDL_INIT_GAMEPAD)) {
      SDL_UpdateGamepads();
      int count = 0;
      if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; ++i) {
          SDL_Gamepad* gamepad = SDL_GetGamepadFromID(ids[i]);  // opened by the SDK's driver
          if (!gamepad) continue;
          frame.connected = true;
          Merge(gamepad, pad);
        }
        SDL_free(ids);
      }
    }
    if (first_) { masked_ = pad.buttons; first_ = false; }
    masked_ &= pad.buttons;  // a masked button counts again once it has been released
    frame.held = uint16_t(pad.buttons & ~masked_);
    frame.pressed = uint16_t(frame.held & ~previous_);
    previous_ = frame.held;
    Feed(io, frame, pad);
    return frame;
  }

 private:
  static int16_t Axis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis) { return SDL_GetGamepadAxis(gamepad, axis); }
  static void Merge(SDL_Gamepad* gamepad, menu::PadSnapshot& pad) {
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
    auto strongest = [](int16_t a, int16_t b) { return std::abs(int(b)) > std::abs(int(a)) ? b : a; };
    pad.lx = strongest(pad.lx, Axis(gamepad, SDL_GAMEPAD_AXIS_LEFTX));
    pad.ly = strongest(pad.ly, Axis(gamepad, SDL_GAMEPAD_AXIS_LEFTY));  // SDL: down is positive
    pad.rx = strongest(pad.rx, Axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
    pad.ry = strongest(pad.ry, Axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY));
    pad.left_trigger = uint8_t(std::max<int>(pad.left_trigger, Axis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >> 7));
    pad.right_trigger = uint8_t(std::max<int>(pad.right_trigger, Axis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >> 7));
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
    const float lx = pad.lx / 32767.0f, ly = pad.ly / 32767.0f;
    analog(ImGuiKey_GamepadLStickLeft, -lx);
    analog(ImGuiKey_GamepadLStickRight, lx);
    analog(ImGuiKey_GamepadLStickUp, -ly);
    analog(ImGuiKey_GamepadLStickDown, ly);
    analog(ImGuiKey_GamepadL2, pad.left_trigger / 255.0f);
    analog(ImGuiKey_GamepadR2, pad.right_trigger / 255.0f);
  }

  bool first_ = true;
  uint16_t previous_ = 0, masked_ = 0;
};

}  // namespace edf::ui
