// EDF2027 - native keyboard and mouse input: state shared between the window-side driver
// (native_kbm_driver.h, UI thread) and the guest hooks (native_kbm.cpp, guest thread).
// No SDK headers, so the driver header and the hook file can both include it cheaply.
#pragma once

#include <cstdint>

namespace edf::kbm {

// True when native keyboard/mouse input is switched on (cvar edf_kbm).
bool Enabled();
// True when the mouse should be captured for aiming (cvars edf_kbm, edf_kbm_mouse_look).
bool MouseLookWanted();

// UI thread. `vk` is a Windows virtual-key code; mouse buttons are 0 left, 1 right, 2 middle.
void SetKey(uint16_t vk, bool down);
void SetMouseButton(int button, bool down);
void AddMouseDelta(float dx, float dy);
// Focus lost, or a dialog took the input: drop everything held and any pending motion.
void ClearInput();
// Whether the game, rather than a dialog, currently owns the input.
void SetGameOwnsInput(bool owns);

}  // namespace edf::kbm
