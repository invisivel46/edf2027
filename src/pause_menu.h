// EDF2027 - the F1 menu as a pause menu: pause state, input routing and the pad chord.
//
// The pure pieces (PauseController, MenuInputGate, PadChord, ParsePadChord) are unit
// tested. The process-wide state below them is shared by the UI thread (the settings
// dialog opens and closes the menu), the engine thread (the heartbeat hook in
// guest_shader_bridge.cpp holds the simulation while a pause is requested) and the guest
// input hooks (input_hooks.cpp for the pad, native_kbm.cpp for keyboard and mouse).
//
// Input routing while the menu is open:
//   mouse / keyboard  -> ImGui only. The native K/M driver releases its capture the
//                        moment the menu opens (native_kbm_driver.h) and the guest-side
//                        merge drops everything (native_kbm.cpp).
//   pad               -> ImGui only. The dialog reads SDL gamepads itself; the guest's
//                        XInputGetState result is blanked (input_hooks.cpp) and the SDK
//                        drivers are told the game is inactive (edf2017_app.h).
// After the menu closes each source stays withheld from the game until it has been seen
// idle (nothing held) or kDrainTimeoutMs passes, so the click, key or button that closed
// the menu never arrives in the game as a fresh press.
//
// No SDK, ImGui or platform headers.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace edf::menu {

// ---- Pause ---------------------------------------------------------------------------
// What opening and closing the menu does to the game. `hold_engine` asks the engine
// heartbeat to stop dispatching simulation steps; `mute` forces audio_mute while the
// menu is open and gives back the player's own setting when it closes.
struct PauseEffects {
  bool hold_engine = false;
  std::optional<bool> set_mute;  // a value to write to audio_mute, if any
};
class PauseController {
 public:
  // pause: hold the simulation while open (edf_menu_pause). mute: silence the game
  // while open (edf_menu_mute_audio). `game_running`: false before boot (--settings),
  // where there is nothing to pause.
  PauseEffects Open(bool pause, bool mute, bool game_running, bool user_mute) {
    if (open_) return {hold_, std::nullopt};
    open_ = true;
    hold_ = pause && game_running;
    muted_ = mute && game_running;
    user_mute_ = user_mute;
    PauseEffects effects{hold_, std::nullopt};
    if (muted_ && !user_mute) effects.set_mute = true;
    return effects;
  }
  PauseEffects Close() {
    if (!open_) return {};
    open_ = false;
    hold_ = false;
    PauseEffects effects;
    if (muted_) effects.set_mute = user_mute_;
    muted_ = false;
    return effects;
  }
  // The Mute checkbox changed while the menu was open: that is the player's setting,
  // restored on close, even though the game stays silent until then.
  void SetUserMute(bool mute) { user_mute_ = mute; }
  // The value to persist for audio_mute: the player's, never the menu's forced one.
  bool PersistentMute(bool current) const { return open_ && muted_ ? user_mute_ : current; }
  bool open() const { return open_; }
  bool holding() const { return hold_; }
  bool forcing_mute() const { return open_ && muted_; }

 private:
  bool open_ = false, hold_ = false, muted_ = false, user_mute_ = false;
};

// ---- Pad -----------------------------------------------------------------------------
// XInput button bits (X_INPUT_GAMEPAD::buttons).
enum PadButton : uint16_t {
  kPadUp = 0x0001, kPadDown = 0x0002, kPadLeft = 0x0004, kPadRight = 0x0008,
  kPadStart = 0x0010, kPadBack = 0x0020, kPadLeftThumb = 0x0040, kPadRightThumb = 0x0080,
  kPadLeftShoulder = 0x0100, kPadRightShoulder = 0x0200,
  kPadA = 0x1000, kPadB = 0x2000, kPadX = 0x4000, kPadY = 0x8000,
};
struct PadSnapshot {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0, right_trigger = 0;
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};
// Nothing held: no button, triggers under XInput's trigger threshold, sticks inside
// the usual dead zones (XInput's own are 7849 and 8689).
inline bool PadIdle(const PadSnapshot& pad) {
  constexpr int kStick = 9000, kTrigger = 30;
  return pad.buttons == 0 && pad.left_trigger <= kTrigger && pad.right_trigger <= kTrigger &&
         std::abs(pad.lx) <= kStick && std::abs(pad.ly) <= kStick && std::abs(pad.rx) <= kStick &&
         std::abs(pad.ry) <= kStick;
}

// The pad combination that opens the menu (edf_menu_pad_chord): button names joined by
// '+', at least two buttons, or "off". Anything unknown reads as off rather than as a
// partial chord that fires on a single button.
inline uint16_t ParsePadChord(std::string_view text) {
  struct Name { std::string_view name; uint16_t bit; };
  static constexpr std::array<Name, 14> kNames{{
      {"a", kPadA}, {"b", kPadB}, {"x", kPadX}, {"y", kPadY}, {"start", kPadStart}, {"back", kPadBack},
      {"lb", kPadLeftShoulder}, {"rb", kPadRightShoulder}, {"ls", kPadLeftThumb}, {"rs", kPadRightThumb},
      {"up", kPadUp}, {"down", kPadDown}, {"left", kPadLeft}, {"right", kPadRight}}};
  uint16_t mask = 0;
  size_t begin = 0;
  while (begin <= text.size()) {
    const size_t end = std::min(text.find('+', begin), text.size());
    std::string token(text.substr(begin, end - begin));
    for (auto& c : token) c = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    uint16_t bit = 0;
    for (const auto& n : kNames)
      if (n.name == token) bit = n.bit;
    if (!bit) return 0;
    mask |= bit;
    begin = end + 1;
  }
  return std::popcount(unsigned(mask)) >= 2 ? mask : 0;
}
struct ChordOption {
  const char* value;
  const char* label;
};
inline constexpr std::array<ChordOption, 4> kChordOptions{{
    {"back+start", "Back + Start"},
    {"ls+rs", "Click both sticks (L3 + R3)"},
    {"back+rb", "Back + RB"},
    {"off", "Off (keyboard F1 only)"},
}};

// Watches the guest's pad polls for the chord. Fires once when the last chord button
// goes down. From the moment a second chord button is held none of them reach the game,
// so Back + Start opens this menu without also opening the game's own pause menu; the
// first button pressed does reach the game, since until the second arrives it is an
// ordinary press.
class PadChord {
 public:
  struct Result {
    uint16_t game_buttons;
    bool fired;
  };
  Result Filter(uint16_t chord, uint16_t buttons) {
    if (!chord) { complete_ = false; return {buttons, false}; }
    const uint16_t held = buttons & chord;
    const bool complete = held == chord;
    const bool fired = complete && !complete_;
    complete_ = complete;
    const uint16_t game = std::popcount(unsigned(held)) >= 2 ? uint16_t(buttons & ~chord) : buttons;
    return {game, fired};
  }

 private:
  bool complete_ = false;
};

// ---- Input gate ----------------------------------------------------------------------
class MenuInputGate {
 public:
  enum class State { kGame, kMenu, kDraining };
  enum Source : size_t { kPad, kKeyboardMouse, kSourceCount };
  static constexpr int64_t kDrainTimeoutMs = 1500;

  void Open() { state_ = State::kMenu; }
  void Close(int64_t now_ms) {
    if (state_ != State::kMenu) return;
    state_ = State::kDraining;
    deadline_ms_ = now_ms + kDrainTimeoutMs;
    clear_.fill(false);
  }
  // Whether this poll's input from `source` is withheld from the game. `idle`: the
  // source currently has nothing held.
  bool Block(Source source, bool idle, int64_t now_ms) {
    if (state_ == State::kGame) return false;
    if (state_ == State::kMenu) return true;
    // The deadline ends the drain for every source, including one that never polls
    // (keyboard and mouse with edf_kbm off).
    if (now_ms >= deadline_ms_) { state_ = State::kGame; return false; }
    bool& clear = clear_[source];
    if (idle) clear = true;
    if (clear_[kPad] && clear_[kKeyboardMouse]) state_ = State::kGame;
    return !clear;
  }
  State state() const { return state_; }
  bool menu_open() const { return state_ == State::kMenu; }

 private:
  State state_ = State::kGame;
  int64_t deadline_ms_ = 0;
  std::array<bool, kSourceCount> clear_{};
};

// ---- Process-wide state --------------------------------------------------------------
inline int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Engine hold. Written by the UI thread, read by the engine thread's heartbeat.
struct EnginePause {
  std::atomic<bool> requested{false};
  std::atomic<bool> holding{false};  // the engine thread is inside HoldWhilePaused
  std::atomic<uint64_t> holds{0};
};
inline EnginePause& Engine() {
  static EnginePause pause;
  return pause;
}
// Engine thread. Sleeps while a pause is requested; returns whether it held at all.
inline bool HoldWhilePaused() {
  auto& pause = Engine();
  if (!pause.requested.load(std::memory_order_acquire)) return false;
  pause.holding.store(true, std::memory_order_release);
  pause.holds.fetch_add(1, std::memory_order_relaxed);
  while (pause.requested.load(std::memory_order_acquire))
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  pause.holding.store(false, std::memory_order_release);
  return true;
}

struct SharedMenuInput {
  std::mutex mutex;
  MenuInputGate gate;
  PadChord chord;
  std::string chord_text;
  uint16_t chord_mask = 0;
  std::function<void()> open_request;  // set by the app; posts OpenSettings to the UI thread
};
inline SharedMenuInput& Input() {
  static SharedMenuInput input;
  return input;
}
inline bool MenuOpen() {
  auto& input = Input();
  std::lock_guard lock(input.mutex);
  return input.gate.menu_open();
}
inline void OpenGate() {
  auto& input = Input();
  std::lock_guard lock(input.mutex);
  input.gate.Open();
}
inline void CloseGate() {
  auto& input = Input();
  std::lock_guard lock(input.mutex);
  input.gate.Close(NowMs());
}
inline bool BlockGameInput(MenuInputGate::Source source, bool idle) {
  auto& input = Input();
  std::lock_guard lock(input.mutex);
  return input.gate.Block(source, idle, NowMs());
}
inline void SetOpenRequestHandler(std::function<void()> handler) {
  auto& input = Input();
  std::lock_guard lock(input.mutex);
  input.open_request = std::move(handler);
}

// Guest thread, player 0's pad poll: applies the chord and the gate. Returns the state
// the game should see; `blank` means none of it.
struct RoutedPad {
  uint16_t buttons;
  bool blank;
};
inline RoutedPad RouteGuestPad(const PadSnapshot& pad, std::string_view chord_text, bool primary_user) {
  auto& input = Input();
  std::function<void()> request;
  RoutedPad routed{pad.buttons, false};
  {
    std::lock_guard lock(input.mutex);
    if (primary_user) {
      if (chord_text != input.chord_text) {
        input.chord_text = std::string(chord_text);
        input.chord_mask = ParsePadChord(chord_text);
      }
      const auto result = input.chord.Filter(input.chord_mask, pad.buttons);
      routed.buttons = result.game_buttons;
      if (result.fired && input.gate.state() == MenuInputGate::State::kGame) request = input.open_request;
    }
    // Only player 0's pad decides when the drain ends; other players' pads stay blank
    // until it has.
    routed.blank = primary_user ? input.gate.Block(MenuInputGate::kPad, PadIdle(pad), NowMs())
                                : input.gate.state() != MenuInputGate::State::kGame;
  }
  if (request) request();
  return routed;
}

}  // namespace edf::menu
