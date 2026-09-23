// EDF2027 - optional manual reload (not in the original game): the pure logic.
//
// EDF 2017 has no reload button. A weapon reloads when its magazine runs dry, and the
// only thing that makes a magazine run dry is the fire action. This file is everything
// needed to start that same reload on request, and nothing the game does not already do.
//
// ---- How the game reloads (EDF 2017 default.xex, title 445007D3) ----------------------
// Weapons are clWeapon01 records embedded in the soldier: array at soldier+1824, count
// +1832, 1408-byte stride, the selected one at index soldier+1920. The constructor
// sub_820E1B60 loads the SGO parameters:
//   +160  WeaponType              +384 ReloadTime (authored, ticks)   +388 reload timer
//   +412  SecondaryFire_Type      +436 FireInterval   +440 fire cooldown
//   +444  FireBurstCount          +448 shots left in the current burst
//   +800  AmmoCount (capacity)    +804 rounds in the magazine
//   +608  FireSe record, +700 its mode (1 = looping, stopped at +688)
//   +704  FireLoadSe record, +784 its delay after a shot, +788 that delay's countdown
//   +1360 list of deployed objects (bombs, sentry guns), +1368 its size
//   +1404 weapon constructed, +1405 weapon selected (sub_820E1140 via sub_820DF668)
//
// There is no "start reload" function. Reloading is a STATE, not an event: a weapon is
// reloading while its magazine is empty. Everything follows from +804 reaching zero:
//   - sub_820E28B8 (fire one shot) does `stw r11,804(r31)` at 0x820E2900 with the count
//     minus one; it is the only writer of +804 in the weapon code besides the
//     constructor and the refill. The last round leaves +804 == 0 and +388 == +384.
//   - clWeapon01 per-tick update sub_820E2C38 (called for every carried weapon by
//     sub_820DF778, the soldier's weapon update) takes its reload arm at 0x820E2EC4 when
//     +804 <= 0: provided its caller allows it (r4 = soldier+1460 != 2), ReloadTime
//     +384 >= 0, and not (SecondaryFire_Type == 2 with objects still deployed), it sets
//     +72 = 1.0 and +68 = 0, then decrements +388 (0x820E2F30) and, once the value it
//     read was <= 0, calls sub_820E1620.
//   - sub_820E1620 is the refill: +388 = +384, +804 = +800 (full magazine), clears the
//     cooldown +440 when ReloadTime != 0, clears +788, and plays FireLoadSe (+704) at
//     the muzzle - the only reload sound.
//   - sub_820E1688 is the game's "is reloading" predicate: +804 == 0 and +384 > 0 and
//     not (+412 == 2 and +1368 != 0). The HUD (clGaugeRader::slot3 sub_82176708, call at
//     0x821772EC) uses it to swap the "name / 120/120" text for the reload gauge
//     (sprites 24/25 of リロード_01.dds, bar length 1 - (+388)/(+384)). The soldier's
//     upper-body state machine at soldier+1944 polls it from the ready state
//     sub_820DEED8 (0x820DEF28) and enters the reload state sub_820DEA50, which clears
//     soldier+1924 (fire allowed) and plays the reload animation through sub_8210AED0
//     (pose soldier+1960, variant 3); when the predicate turns false it goes to
//     sub_820DEF70 (re-raise, variant 4) and back to ready.
//   - While +804 <= 0 the fire paths do nothing: sub_820E2FF8 needs +804 > 0 and
//     sub_820E28B8 returns at once. The looping fire sound is stopped by slot 3 when
//     +804 == 0 (0x820E2C84).
//
// Weapons that do not reload the ordinary way, all through the same fields:
//   ReloadTime < 0   never reloads; the magazine is all you get (BonusWeapon01,
//                    Weapon062/066/073, helicopter missiles).
//   ReloadTime == 0  refills on the tick it empties (hand grenades, Weapon090-100,
//                    Weapon147-151); there is nothing to start by hand.
//   SecondaryFire_Type 2 (C-bombs BombAmmo01, sentry guns CentryGun01): the reload waits
//                    until every deployed object is gone (+1368 == 0).
//   Lasers, flamers, rockets and missiles are ordinary clWeapon01 magazines.
//   Vehicle weapons are clWeapon01 records in the vehicle's weapon groups, updated by
//   sub_82199C48 / sub_821E4CF0; a soldier in a vehicle (soldier+1768 != 0,
//   sub_820DBBD0) skips sub_820DF778 altogether (clSoldierObject::slot3 at 0x820DFC5C).
//
// ---- The injection ---------------------------------------------------------------------
// On the simulation thread, at the entry of the soldier's weapon update sub_820DF778 for
// the local player, a consumed request writes +804 = 0 on the selected weapon: the same
// single store, to the same field, with the same value, that the fire path makes when it
// fires the last round. Nothing else is written; the reload timer, the refill, the sound,
// the HUD and the animation are then all the game's own, starting in the same call.
//
// Partial reload: the magazine is refilled to capacity by the game's refill, as always.
// EDF has no reserve pool, so the rounds that were left are not "lost" anywhere - a
// manual reload trades the remaining rounds for a full magazine after ReloadTime ticks,
// exactly as an empty reload would. Keeping the old rounds usable during the reload is not
// representable: the game's only reloading state is an empty magazine.
//
// Pure logic only - no SDK, ImGui or platform headers - so the unit tests can cover it.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "controller_logic.h"
#include "pause_menu.h"

namespace edf::reload {

// ---- guest layout ------------------------------------------------------------------------
inline constexpr uint32_t kPlayerObjectVtable = 0x8200479Cu;  // clPlayerObject
// Soldier (clSoldierObject / clPlayerObject).
inline constexpr uint32_t kSoldierPlayer = 492;          // player index; allies are negative
inline constexpr uint32_t kSoldierInputDisabled = 624;   // byte, clPlayerObject::slot3 0x820DDF34
inline constexpr uint32_t kSoldierStatus = 1460;         // 0 = can act (sub_820DF778 0x820DF818)
inline constexpr uint32_t kSoldierVehicle = 1768;        // non-zero while riding (sub_820DBBD0)
inline constexpr uint32_t kSoldierWeapons = 1824, kSoldierWeaponCount = 1832, kWeaponStride = 1408;
inline constexpr uint32_t kSoldierWeaponIndex = 1920;
inline constexpr uint32_t kSoldierFireReady = 1924;      // byte, ANDed with the fire inputs
// Weapon (clWeapon01).
inline constexpr uint32_t kWeaponReloadTime = 384, kWeaponReloadTimer = 388;
inline constexpr uint32_t kWeaponSecondaryType = 412, kWeaponBurstLeft = 448;
inline constexpr uint32_t kWeaponCapacity = 800, kWeaponAmmo = 804;
inline constexpr uint32_t kWeaponDeployedCount = 1368;
inline constexpr uint32_t kWeaponConstructed = 1404, kWeaponSelected = 1405;
inline constexpr int32_t kSecondaryDeployed = 2;         // SecondaryFire_Type of bombs, sentries

// Guest memory is big-endian.
inline uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, sizeof(value));
  return __builtin_bswap32(value);
}
inline void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(base + address, &value, sizeof(value));
}

struct SoldierState {
  uint32_t vtable = 0;
  int32_t player = -1;
  bool input_disabled = false;
  int32_t status = 0;
  uint32_t vehicle = 0;
  bool fire_ready = false;
  uint32_t weapons = 0, weapon_count = 0, weapon_index = 0;
};

struct WeaponState {
  bool constructed = false, selected = false;
  int32_t ammo = 0, capacity = 0;
  int32_t reload_time = 0, reload_timer = 0;
  int32_t secondary_type = 0;
  uint32_t deployed = 0;
  int32_t burst_left = 0;
};

inline SoldierState ReadSoldier(const uint8_t* base, uint32_t soldier) {
  SoldierState s;
  s.vtable = Load32(base, soldier);
  s.player = static_cast<int32_t>(Load32(base, soldier + kSoldierPlayer));
  s.input_disabled = base[soldier + kSoldierInputDisabled] != 0;
  s.status = static_cast<int32_t>(Load32(base, soldier + kSoldierStatus));
  s.vehicle = Load32(base, soldier + kSoldierVehicle);
  s.fire_ready = base[soldier + kSoldierFireReady] != 0;
  s.weapons = Load32(base, soldier + kSoldierWeapons);
  s.weapon_count = Load32(base, soldier + kSoldierWeaponCount);
  s.weapon_index = Load32(base, soldier + kSoldierWeaponIndex);
  return s;
}

// The selected weapon's address, or 0 when the soldier has none (sub_820DFCC8 indexes the
// same way: soldier+1824 + soldier+1920 * 1408).
inline uint32_t SelectedWeapon(const SoldierState& s) {
  if (!s.weapons || s.weapon_index >= s.weapon_count) return 0;
  return s.weapons + s.weapon_index * kWeaponStride;
}

inline WeaponState ReadWeapon(const uint8_t* base, uint32_t weapon) {
  WeaponState w;
  w.constructed = base[weapon + kWeaponConstructed] != 0;
  w.selected = base[weapon + kWeaponSelected] != 0;
  w.ammo = static_cast<int32_t>(Load32(base, weapon + kWeaponAmmo));
  w.capacity = static_cast<int32_t>(Load32(base, weapon + kWeaponCapacity));
  w.reload_time = static_cast<int32_t>(Load32(base, weapon + kWeaponReloadTime));
  w.reload_timer = static_cast<int32_t>(Load32(base, weapon + kWeaponReloadTimer));
  w.secondary_type = static_cast<int32_t>(Load32(base, weapon + kWeaponSecondaryType));
  w.deployed = Load32(base, weapon + kWeaponDeployedCount);
  w.burst_left = static_cast<int32_t>(Load32(base, weapon + kWeaponBurstLeft));
  return w;
}

// sub_820E1688, the game's own "is reloading" predicate (HUD, soldier state machine).
inline bool GameIsReloading(const WeaponState& w) {
  const bool not_waiting = !(w.secondary_type == kSecondaryDeployed && w.deployed != 0);
  return w.ammo == 0 && w.reload_time > 0 && not_waiting;
}

// ---- the decision ------------------------------------------------------------------------
enum class Decision : uint8_t {
  kStart,           // write +804 = 0
  kNotLocalPlayer,  // an ally, another player's soldier, or not a clPlayerObject
  kInputDisabled,   // cutscene / scripted control (clPlayerObject +624)
  kInVehicle,       // vehicle weapons are not touched
  kCannotAct,       // soldier status != 0: the game blocks firing too (sub_820DF778)
  kNoWeapon,
  kWeaponInactive,  // not constructed, or not the selected weapon
  kNotReady,        // soldier+1924 clear: switching, already in the reload animation
  kNoReload,        // ReloadTime <= 0: never reloads, or refills instantly
  kWaitingDeployed, // bombs / sentries still out; the game would not advance the reload
  kAlreadyEmpty,    // magazine empty: the game's own reload is (or will be) running
  kFull,
  kFiring,          // a burst is in flight
  kTimerBusy,       // reload timer not at rest: some state this code does not know
};

inline const char* DecisionName(Decision d) {
  switch (d) {
    case Decision::kStart: return "start";
    case Decision::kNotLocalPlayer: return "not the local player";
    case Decision::kInputDisabled: return "input disabled";
    case Decision::kInVehicle: return "in a vehicle";
    case Decision::kCannotAct: return "soldier cannot act";
    case Decision::kNoWeapon: return "no weapon";
    case Decision::kWeaponInactive: return "weapon inactive";
    case Decision::kNotReady: return "weapon not ready";
    case Decision::kNoReload: return "weapon does not reload";
    case Decision::kWaitingDeployed: return "deployed objects still out";
    case Decision::kAlreadyEmpty: return "already reloading";
    case Decision::kFull: return "magazine full";
    case Decision::kFiring: return "firing";
    case Decision::kTimerBusy: return "reload timer busy";
  }
  return "?";
}

// `player` is the player index the request is for (0 = the keyboard's player).
inline Decision Decide(const SoldierState& s, const WeaponState& w, int32_t player) {
  if (s.vtable != kPlayerObjectVtable || s.player != player || player < 0) return Decision::kNotLocalPlayer;
  if (s.input_disabled) return Decision::kInputDisabled;
  if (s.vehicle != 0) return Decision::kInVehicle;
  if (s.status != 0) return Decision::kCannotAct;
  if (!SelectedWeapon(s)) return Decision::kNoWeapon;
  if (!w.constructed || !w.selected) return Decision::kWeaponInactive;
  if (!s.fire_ready) return Decision::kNotReady;
  if (w.reload_time <= 0) return Decision::kNoReload;
  if (w.secondary_type == kSecondaryDeployed && w.deployed != 0) return Decision::kWaitingDeployed;
  if (w.ammo <= 0) return Decision::kAlreadyEmpty;
  if (w.ammo >= w.capacity) return Decision::kFull;
  if (w.burst_left > 0) return Decision::kFiring;
  if (w.reload_timer != w.reload_time) return Decision::kTimerBusy;
  return Decision::kStart;
}

// The one guest write: the store sub_820E28B8 makes at 0x820E2900 when it fires the last
// round (`stw r11,804(r31)`, r11 = rounds - 1 = 0).
inline void StartReload(uint8_t* base, uint32_t weapon) { Store32(base, weapon + kWeaponAmmo, 0); }

// Reads, decides and, when the answer is kStart, writes. Returns the decision.
inline Decision TryManualReload(uint8_t* base, uint32_t soldier, int32_t player) {
  const SoldierState s = ReadSoldier(base, soldier);
  const uint32_t weapon = SelectedWeapon(s);
  const WeaponState w = weapon ? ReadWeapon(base, weapon) : WeaponState{};
  const Decision d = Decide(s, w, player);
  if (d == Decision::kStart) StartReload(base, weapon);
  return d;
}

// ---- requests ----------------------------------------------------------------------------
// A press sets a per-player flag; the player's next weapon update consumes it, once, and
// whatever the decision the flag is gone - a press while the magazine is full does not
// fire later. A request nobody consumes (a menu, the game's pause, a loading screen, a
// vehicle) expires, so it cannot reload a weapon long after the key was let go.
class Requests {
 public:
  static constexpr size_t kPlayers = 4;
  static constexpr int64_t kLifetimeMs = 250;

  // `enabled` is edf_manual_reload: off, a press is dropped on the floor.
  void Request(size_t player, int64_t now_ms, bool enabled) {
    if (!enabled || player >= kPlayers) return;
    stamp_[player].store(now_ms, std::memory_order_relaxed);
    pending_[player].store(true, std::memory_order_release);
  }
  // The weapon update. True at most once per request; false when turned off meanwhile.
  bool Consume(size_t player, int64_t now_ms, bool enabled) {
    if (player >= kPlayers) return false;
    if (!pending_[player].exchange(false, std::memory_order_acq_rel)) return false;
    if (!enabled) return false;
    return now_ms - stamp_[player].load(std::memory_order_relaxed) <= kLifetimeMs;
  }
  bool Pending(size_t player) const {
    return player < kPlayers && pending_[player].load(std::memory_order_acquire);
  }
  void Clear() {
    for (auto& p : pending_) p.store(false, std::memory_order_release);
  }

 private:
  std::array<std::atomic<bool>, kPlayers> pending_{};
  std::array<std::atomic<int64_t>, kPlayers> stamp_{};
};

// Once per simulation tick: the weapon update may run for several soldiers, and only the
// matching player's consumes. `last_tick` stops a second call in the same tick (there is
// none today) from taking a second request.
struct TickGate {
  uint64_t last_tick = ~uint64_t(0);
  bool Take(uint64_t tick) {
    if (tick == last_tick) return false;
    last_tick = tick;
    return true;
  }
};

// ---- keyboard: press edge ----------------------------------------------------------------
struct KeyEdge {
  bool held = false;
  bool Update(bool down) {
    const bool pressed = down && !held;
    held = down;
    return pressed;
  }
};

// ---- controller -------------------------------------------------------------------------
// Two ways in, in this order:
//   1. The controller remap's synthetic Reload action (controller_logic.h, F1 -> Controls ->
//      Controller mapping -> Extra actions). Unbound by default. A button mapped there is
//      taken away from the game control it drove, so it only reloads. Edge-detected here.
//   2. While that row is unbound: a fallback binding (edf_manual_reload_pad, default R3),
//      watched on the pad the GAME is given, after the remap, and never taken from it. R3
//      is chosen because the game gives it to nothing by default, so pressing it both
//      reloads and does nothing else. If the player's in-game controller settings do give
//      the button to an action, the fallback stays quiet (ProfileUsesBinding).
//
// The fallback binding: button names joined by '+', as the F1 menu chord is written (a b x
// y start back lb rb ls rs up down left right), one button allowed, or "off". Unknown
// names read as off.
inline uint16_t ParsePadBinding(std::string_view text) {
  struct Name { std::string_view name; uint16_t bit; };
  static constexpr std::array<Name, 14> kNames{{
      {"a", menu::kPadA}, {"b", menu::kPadB}, {"x", menu::kPadX}, {"y", menu::kPadY},
      {"start", menu::kPadStart}, {"back", menu::kPadBack}, {"lb", menu::kPadLeftShoulder},
      {"rb", menu::kPadRightShoulder}, {"ls", menu::kPadLeftThumb}, {"rs", menu::kPadRightThumb},
      {"up", menu::kPadUp}, {"down", menu::kPadDown}, {"left", menu::kPadLeft}, {"right", menu::kPadRight}}};
  if (text.empty()) return 0;
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
  return mask;
}

struct PadOption {
  const char* value;
  const char* label;
};
// R3 is the default: neither default controller profile nor any fixed control slot of
// the binder sub_820A0F70 uses RightThumb (the profile defaults, sub_820A0028, are
// RT/LB/LT/RB for Technical and X/Y/L3/A/B/LB/RB for Normal - L3 is Normal's zoom).
inline constexpr std::array<PadOption, 3> kPadOptions{{
    {"rs", "Click right stick (R3)"},
    {"ls", "Click left stick (L3)"},
    {"off", "Off"},
}};

// The game's pad-name table (sub_820A00A0) index of a single XInput button, -1 if the
// button is not one the controller profile can name.
inline int ProfilePadIndex(uint16_t bit) {
  switch (bit) {
    case menu::kPadA: return 0;
    case menu::kPadB: return 1;
    case menu::kPadX: return 2;
    case menu::kPadY: return 3;
    case menu::kPadLeftShoulder: return 4;
    case menu::kPadRightShoulder: return 5;
    case menu::kPadLeftThumb: return 8;
    case menu::kPadRightThumb: return 9;
    default: return -1;
  }
}

// Whether the player's controller profile already gives a game action one of the binding's
// buttons. The binder reads profile words +12..+24 for the Technical type (1) and
// +28..+52 for Normal (0); `words` holds the profile's first 14 words.
inline bool ProfileUsesBinding(const std::array<uint32_t, 14>& words, uint16_t binding) {
  const uint32_t type = words[0];
  const size_t first = type == 1 ? 3 : 7, last = type == 1 ? 6 : 13;  // word indices
  for (uint16_t bit = 1; bit; bit = uint16_t(bit << 1)) {
    if (!(binding & bit)) continue;
    const int index = ProfilePadIndex(bit);
    if (index < 0) continue;
    for (size_t i = first; i <= last; ++i)
      if (words[i] == uint32_t(index)) return true;
  }
  return false;
}

// The fallback binding on one player's pad.
//   - A binding that shares no button with the F1 menu chord fires when it is completed.
//   - A binding that does (R3 with the "click both sticks" chord) fires when it is let go,
//     and only if no other chord button went down meanwhile - a tap, so opening the menu
//     with L3 + R3 does not also reload.
// `game` is what the game is given (the chord's buttons are taken out of it once two are
// held); `raw` is the physical pad, which the chord is matched on.
class PadTrigger {
 public:
  bool Update(uint16_t game, uint16_t raw, uint16_t binding, uint16_t menu_chord) {
    if (!binding) { Reset(); return false; }
    const bool complete = (game & binding) == binding;
    bool fire = false;
    if (!(binding & menu_chord)) {
      fire = complete && !complete_;
    } else {
      const uint16_t others = uint16_t(menu_chord & ~binding);
      if (complete) {
        if (!complete_) spoiled_ = false;
        if (raw & others) spoiled_ = true;
      } else if (complete_) {
        fire = !spoiled_ && !(raw & others);
      }
    }
    complete_ = complete;
    return fire;
  }
  void Reset() { complete_ = spoiled_ = false; }

 private:
  bool complete_ = false, spoiled_ = false;
};

// One guest pad poll, as the XInputGetState hook saw it.
struct PadPoll {
  bool blank = false;          // the F1 menu withheld it from the game
  uint16_t raw = 0;            // physical buttons
  uint16_t game = 0;           // buttons the game is given (after the chord and the remap)
  bool reload_mapped = false;  // the player's remap table binds a button to Reload
  bool reload_held = false;    // ... and it is held (SyntheticActionHeld)
};

enum class PadPress : uint8_t { kNone, kMapped, kFallback };

// With manual reload off, a button the player mapped to Reload goes back to the game: the
// Reload row is dropped and, when mapping it had left the button's own game control
// unbound (controller_logic.h AssignSource gives that row the Reload row's old "none"),
// the button drives that control again. Off, nothing is taken from the game.
inline pad::PadRemap WithoutReload(pad::PadRemap remap) {
  const size_t row = size_t(pad::TargetOf(pad::SyntheticAction::kReload));
  const int source = remap.source[row];
  if (source == pad::kNoSource) return remap;
  remap.source[row] = int8_t(pad::kNoSource);
  if (source < 0 || source >= pad::kControlCount) return remap;
  for (int t = 0; t < pad::kControlCount; ++t)
    if (remap.source[size_t(t)] == source) return remap;  // it still drives a game control
  if (remap.source[size_t(source)] == pad::kNoSource) remap.source[size_t(source)] = int8_t(source);
  return remap;
}

class PadReload {
 public:
  PadPress Update(const PadPoll& poll, uint16_t fallback_binding, uint16_t menu_chord) {
    if (poll.blank) { Reset(); return PadPress::kNone; }
    const bool mapped_press = mapped_.Update(poll.reload_mapped && poll.reload_held);
    if (poll.reload_mapped) {
      fallback_.Reset();
      return mapped_press ? PadPress::kMapped : PadPress::kNone;
    }
    return fallback_.Update(poll.game, poll.raw, fallback_binding, menu_chord) ? PadPress::kFallback
                                                                                : PadPress::kNone;
  }
  void Reset() {
    mapped_ = KeyEdge{};
    fallback_.Reset();
  }

 private:
  KeyEdge mapped_;
  PadTrigger fallback_;
};

}  // namespace edf::reload
