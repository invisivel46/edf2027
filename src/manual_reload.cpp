// EDF2027 - optional manual reload (not in the original game): the guest side.
//
// One hook, on the game's own path (EDF 2017 default.xex):
//   sub_820DF778  the soldier's weapon update, called once per tick from
//                 clSoldierObject::slot3 (sub_820DFBC0, 0x820DFCA4) - and only when the
//                 soldier is not riding a vehicle. It walks every carried weapon through
//                 clWeapon01's per-tick update sub_820E2C38, whose reload arm is where the
//                 game reloads an empty magazine. A pending request is consumed at its
//                 entry and, when manual_reload_logic.h Decide() allows it, the selected
//                 weapon's magazine is emptied with the store the fire path makes on its
//                 last round. The weapon update then runs as usual and starts the reload
//                 in the same call.
//
// Safety: the only guest write is one aligned 32-bit store to weapon+804, on the thread and
// inside the function where the game writes it itself; the value (0) is the one the game
// writes; and every precondition the game would need to reach that store by firing, or to
// run the reload afterwards, is checked first. See manual_reload_logic.h.
#include <array>
#include <atomic>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/types.h>

#include "manual_reload.h"
#include "manual_reload_logic.h"
#include "pause_menu.h"
#include "scripted_input_logic.h"

REXCVAR_DEFINE_BOOL(edf_manual_reload, false, "EDF2027",
                    "Manual reload (not in the original game): a Reload key/button starts the selected weapon's "
                    "reload as if its magazine had run dry");
REXCVAR_DEFINE_STRING(edf_manual_reload_pad, "rs", "EDF2027",
                      "Controller button(s) for manual reload while the controller mapping's Reload row is unbound, "
                      "joined by '+' (a b x y start back lb rb ls rs up down left right), or off. Default rs: click "
                      "the right stick, which the game itself leaves unused");
REXCVAR_DECLARE(std::string, edf_menu_pad_chord);

namespace edf::reload {
namespace {

Requests& Pending() {
  static Requests requests;
  return requests;
}

struct KeyboardSide {
  std::mutex mutex;
  KeyEdge edge;
};
KeyboardSide& Keyboard() {
  static KeyboardSide side;
  return side;
}

struct PadSide {
  std::mutex mutex;
  std::array<PadReload, Requests::kPlayers> pads;
  std::string binding_text, chord_text;
  uint16_t binding = 0, chord = 0;
  bool parsed = false;
};
PadSide& Pad() {
  static PadSide side;
  return side;
}

// The input manager (global 0x82578640) keeps 64-byte controller profiles from +2356,
// one per player; word 0 is the control type (see native_kbm.cpp).
constexpr uint32_t kInputManagerGlobal = 0x82578640, kProfiles = 2356, kProfileStride = 64;

bool PlayerProfileUses(const uint8_t* base, uint32_t user, uint16_t binding) {
  const uint32_t manager = Load32(base, kInputManagerGlobal);
  if (!manager) return false;
  const uint32_t profile = manager + kProfiles + user * kProfileStride;
  std::array<uint32_t, 14> words{};
  for (size_t i = 0; i < words.size(); ++i) words[i] = Load32(base, profile + uint32_t(i * 4));
  return ProfileUsesBinding(words, binding);
}

// Simulation-thread state (the weapon update runs on one guest thread).
std::array<TickGate, Requests::kPlayers> g_tick_gates;

void OnWeaponUpdate(uint8_t* base, uint32_t soldier) {
  if (!soldier || Load32(base, soldier) != kPlayerObjectVtable) return;
  const int32_t player = static_cast<int32_t>(Load32(base, soldier + kSoldierPlayer));
  if (player < 0 || size_t(player) >= Requests::kPlayers) return;
  if (!Pending().Pending(size_t(player))) return;
  // Once per tick per player. The tick counter is zero when the heartbeat hook is off;
  // the request flag alone still limits it to one reload per press.
  const uint64_t tick = edf::SimulationTicks().load(std::memory_order_relaxed);
  if (tick && !g_tick_gates[size_t(player)].Take(tick)) return;
  if (!Pending().Consume(size_t(player), menu::NowMs(), Enabled())) return;
  const SoldierState s = ReadSoldier(base, soldier);
  const uint32_t weapon = SelectedWeapon(s);
  const WeaponState w = weapon ? ReadWeapon(base, weapon) : WeaponState{};
  const Decision decision = TryManualReload(base, soldier, player);
  REXLOG_INFO("Manual reload: player {} weapon {:#x} ammo {}/{} reload {} -> {}", player, weapon, w.ammo, w.capacity,
              w.reload_time, DecisionName(decision));
}

}  // namespace

bool Enabled() { return REXCVAR_GET(edf_manual_reload); }

void RequestManualReload(uint32_t player) { Pending().Request(player, menu::NowMs(), Enabled()); }

void OnKeyboardAction(bool down) {
  bool pressed;
  {
    KeyboardSide& side = Keyboard();
    std::lock_guard lock(side.mutex);
    pressed = side.edge.Update(down);
  }
  if (pressed) RequestManualReload(0);
}

void ResetKeyboard() {
  KeyboardSide& side = Keyboard();
  std::lock_guard lock(side.mutex);
  side.edge = KeyEdge{};
}

void OnGuestPad(const uint8_t* base, uint32_t user, const PadPoll& poll) {
  if (user >= Requests::kPlayers) return;
  PadSide& side = Pad();
  PadPress press = PadPress::kNone;
  uint16_t binding = 0;
  {
    std::lock_guard lock(side.mutex);
    const std::string binding_text = REXCVAR_GET(edf_manual_reload_pad);
    const std::string chord_text = REXCVAR_GET(edf_menu_pad_chord);
    if (!side.parsed || binding_text != side.binding_text || chord_text != side.chord_text) {
      side.binding_text = binding_text;
      side.chord_text = chord_text;
      side.binding = ParsePadBinding(binding_text);
      side.chord = menu::ParsePadChord(chord_text);
      side.parsed = true;
    }
    binding = side.binding;
    PadReload& pad = side.pads[user];
    if (!Enabled()) {
      pad.Reset();
      return;
    }
    // Only player 0's pad can open the F1 menu, so only it has a chord to stay out of.
    press = pad.Update(poll, side.binding, user == 0 ? side.chord : 0);
  }
  if (press == PadPress::kMapped) RequestManualReload(user);
  // The fallback button is also the game's. If the player's in-game controller settings
  // give it to an action, leave it to that action rather than make one press do two things.
  if (press == PadPress::kFallback && !PlayerProfileUses(base, user, binding)) RequestManualReload(user);
}

}  // namespace edf::reload

REX_EXTERN(__imp__sub_820DF778);
REX_HOOK_RAW(sub_820DF778) {
  if (edf::reload::Enabled()) edf::reload::OnWeaponUpdate(base, ctx.r3.u32);
  __imp__sub_820DF778(ctx, base);
}
