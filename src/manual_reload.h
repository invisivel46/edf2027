// EDF2027 - optional manual reload (not in the original game). See manual_reload_logic.h
// for the guest reload flow and why the injection is a single store.
//
// Input sources call in here; the simulation-thread hook (manual_reload.cpp, on the
// soldier's weapon update sub_820DF778) consumes one request per player per tick. Every
// entry point is a no-op while edf_manual_reload is off, and none of them changes what
// the game sees from the keyboard or the pad.
//
// Controller: the remap's synthetic Reload action (controller_logic.h, Extra actions) when
// the player has bound it, else the fallback edf_manual_reload_pad (default R3) on the pad
// the game is given. See manual_reload_logic.h "controller".
// No SDK headers, so any input file can include it.
#pragma once

#include <cstdint>

#include "manual_reload_logic.h"

namespace edf::reload {

// edf_manual_reload.
bool Enabled();

// A press of the synthetic Reload action for `player` (0 = player 1).
void RequestManualReload(uint32_t player);

// Keyboard/mouse: the Reload action's held state this poll (player 0). Edge-detected here.
void OnKeyboardAction(bool down);
// Keyboard/mouse input withheld from the game (F1 menu, focus): forget the held state.
void ResetKeyboard();

// Guest XInputGetState hook, once per successful poll of guest user `user`.
void OnGuestPad(const uint8_t* base, uint32_t user, const PadPoll& poll);

}  // namespace edf::reload
