#include "controller_logic.h"
#include "core_logic.h"
#include "scripted_input_logic.h"
#include "keybind_logic.h"
#include "native_kbm_logic.h"
#include "pause_menu.h"
#include "settings_logic.h"
#include "xdvdfs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {
int failures = 0;

void Check(bool condition, const char* expression, int line) {
  if (!condition) {
    std::cerr << "line " << line << ": CHECK(" << expression << ") failed\n";
    ++failures;
  }
}
#define CHECK(value) Check(static_cast<bool>(value), #value, __LINE__)

void PutBe32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  if (bytes.size() < offset + 4) bytes.resize(offset + 4);
  bytes[offset] = static_cast<uint8_t>(value >> 24);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
  bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
  bytes[offset + 3] = static_cast<uint8_t>(value);
}

void TestXexParsing() {
  CHECK(edf::ParseXexTitleId({}) == 0);
  std::vector<uint8_t> xex(0x100, 0);
  CHECK(edf::ParseXexTitleId(xex) == 0);
  xex[0] = 'X'; xex[1] = 'E'; xex[2] = 'X'; xex[3] = '2';
  PutBe32(xex, 0x14, 2);
  PutBe32(xex, 0x18, 0x12345678); PutBe32(xex, 0x1c, 0x80);
  PutBe32(xex, 0x20, 0x00040006); PutBe32(xex, 0x24, 0x40);
  PutBe32(xex, 0x4c, 0x445007D3);
  CHECK(edf::ParseXexTitleId(xex) == 0x445007D3);
  PutBe32(xex, 0x24, 0xfffffff0);
  CHECK(edf::ParseXexTitleId(xex) == 0);
  PutBe32(xex, 0x14, 0xffffffff);
  xex.resize(0x20);
  CHECK(edf::ParseXexTitleId(xex) == 0);
}

void TestConfigFiltering() {
  const std::string input =
      "# retained\r\nlog_file = \"run.log\"\r\ngame_data_root D:/game\n"
      "edf_show_settings=true\nsettings false\nsettings_extra=true\n"
      " log_file=indented\nfullscreen=false";
  const std::string expected =
      "# retained\nsettings_extra=true\n log_file=indented\nfullscreen=false\n";
  CHECK(edf::FilterPersistentConfig(input) == expected);
  CHECK(edf::FilterPersistentConfig("").empty());
  CHECK(!edf::IsTransientConfigLine("settings"));
  CHECK(!edf::IsTransientConfigLine("settings_extra=true"));
  CHECK(edf::IsTransientConfigLine("settings=true"));
  CHECK(edf::IsTransientConfigLine("log_file value"));
}

void TestScriptedInput() {
  std::istringstream source(
      "0 100 0010\n50 100 0x1000\ninvalid\n1 2 10000\n"
      "4294967296 1 1\n1 4294967296 1\n3 4 xyz\n5 6 1 extra\n");
  const auto events = edf::ParseInputEvents(source);
  CHECK(events.size() == 2);
  CHECK(edf::ButtonsAt(events, 0) == 0x0010);
  CHECK(edf::ButtonsAt(events, 49) == 0x0010);
  CHECK(edf::ButtonsAt(events, 50) == 0x1010);
  CHECK(edf::ButtonsAt(events, 99) == 0x1010);
  CHECK(edf::ButtonsAt(events, 100) == 0x1000);
  CHECK(edf::ButtonsAt(events, 150) == 0);
  const auto defaults = edf::DefaultInputEvents();
  CHECK(defaults.size() == 17);
  CHECK(defaults.front().start_ms == 7000 && defaults.front().buttons == 0x0010);
  CHECK(defaults.back().start_ms == 56000 && defaults.back().buttons == 0x1000);
  CHECK(edf::ButtonTransitions(0xffff, 0).empty());
  auto keys = edf::ButtonTransitions(0, 0x1010);
  CHECK(keys.size() == 2 && keys[0].virtual_key == 0x5814 && keys[0].flags == 1);
  CHECK(keys[1].virtual_key == 0x5800 && keys[1].flags == 1);
  keys = edf::ButtonTransitions(0x1010, 0);
  CHECK(keys.size() == 2 && keys[0].flags == 2 && keys[1].flags == 2);
}

void TestFrameLogic() {
  const auto empty = edf::SummarizeFrameTimes({});
  CHECK(empty.average_ms == 0 && empty.minimum_ms == 0 && empty.maximum_ms == 0);
  const std::vector<double> one{16.0};
  const auto single = edf::SummarizeFrameTimes(one);
  CHECK(single.average_ms == 16 && single.low_1_percent_ms == 16);
  std::vector<double> many(100, 10.0);
  many[98] = 20; many[99] = 30;
  const auto summary = edf::SummarizeFrameTimes(many);
  CHECK(std::abs(summary.average_ms - 10.3) < 0.000001);
  CHECK(summary.minimum_ms == 10 && summary.maximum_ms == 30);
  CHECK(summary.low_1_percent_ms == 20);
  CHECK(edf::FramePeriodNanoseconds(0) == 0);
  CHECK(edf::FramePeriodNanoseconds(-1) == 0);
  CHECK(edf::FramePeriodNanoseconds(60) == 16666666);
  CHECK(edf::FramePeriodNanoseconds(120) == 8333333);
}

void TestGraphicsMapping() {
  auto mode = edf::GuestVideoMode(1920, 1080, "native");
  CHECK(edf::ValidNativeRenderMode(0,0));
  CHECK(edf::ValidNativeRenderMode(1920,1080));
  CHECK(edf::ValidNativeRenderMode(640,480));
  CHECK(edf::ValidNativeRenderMode(4095,4095));
  CHECK(!edf::ValidNativeRenderMode(0,720));
  CHECK(!edf::ValidNativeRenderMode(1280,0));
  CHECK(!edf::ValidNativeRenderMode(4096,2160));
  CHECK(!edf::ValidNativeRenderMode(640,479));
  CHECK(mode.width == 1920 && mode.height == 1080);
  mode = edf::GuestVideoMode(1920, 1200, "native");
  CHECK(mode.width == 1920 && mode.height == 1200);
  mode = edf::GuestVideoMode(1920, 1200, "letterbox");
  CHECK(mode.width == 2133 && mode.height == 1200);
  mode = edf::GuestVideoMode(3440, 1440, "ultrawide");
  CHECK(mode.width == 3440 && mode.height == 1440);
  mode = edf::GuestVideoMode(5120, 1440, "ultrawide");
  CHECK(mode.width == 5120 && mode.height == 1440);
  mode = edf::GuestVideoMode(800, 600, "stretch");
  CHECK(mode.width == 1280 && mode.height == 720);
  CHECK(edf::PresentLetterbox("native") && edf::PresentLetterbox("letterbox"));
  CHECK(!edf::PresentLetterbox("stretch"));
  CHECK(!edf::ForcesConsoleAspect("native") && !edf::ForcesConsoleAspect("ultrawide"));
  CHECK(edf::ForcesConsoleAspect("letterbox") && edf::ForcesConsoleAspect("stretch"));
  CHECK(edf::AspectIndex("bad") == 0 && edf::AspectIndex("ultrawide") == 1);
  CHECK(edf::AspectIndex("letterbox") == 2 && edf::AspectIndex("stretch") == 3);
  CHECK(edf::AspectValue(-1) == "native" && edf::AspectValue(1) == "ultrawide");
  CHECK(edf::AspectValue(2) == "letterbox" && edf::AspectValue(3) == "stretch");
  CHECK(edf::FpsCapValue(-1) == 0 && edf::FpsCapValue(3) == 120 && edf::FpsCapValue(9) == 0);
  CHECK(edf::RefreshValue(-1) == 60 && edf::RefreshValue(1) == 30 && edf::RefreshValue(3) == 144);
  CHECK(edf::ResolutionScaleValue(-1) == 1 && edf::ResolutionScaleValue(2) == 3 && edf::ResolutionScaleValue(99) == 4);
  CHECK(edf::AnisotropicValue(-5) == -1 && edf::AnisotropicValue(6) == 5 && edf::AnisotropicValue(99) == 5);
  CHECK(edf::FxaaValue(-1) == "none" && edf::FxaaValue(1) == "fxaa" && edf::FxaaValue(2) == "fxaa_extreme");
  CHECK(edf::UpscaleValue(-1) == "bilinear" && edf::UpscaleValue(1) == "cas" && edf::UpscaleValue(2) == "fsr");
  CHECK(edf::UpscaleValue(3) == "fsr2" && edf::UpscaleValue(4) == "fsr3" && edf::UpscaleValue(9) == "bilinear");
  // Index and value must round-trip, or the dialog reopens on a different entry than it saved.
  for (int i = 0; i < 5; ++i) CHECK(edf::UpscaleIndex(edf::UpscaleValue(i)) == i);
  CHECK(edf::UpscaleIndex("nonsense") == 0);

  // Quality mode is only consulted by the runtime on the temporal (fsr2/fsr3) paths.
  CHECK(!edf::UsesTemporalUpscaler("bilinear") && !edf::UsesTemporalUpscaler("cas"));
  CHECK(!edf::UsesTemporalUpscaler("fsr"));
  CHECK(edf::UsesTemporalUpscaler("fsr2") && edf::UsesTemporalUpscaler("fsr3"));

  // These strings must match the runtime's .allowed() list, or the cvar is rejected.
  CHECK(edf::FsrQualityValue(0) == "auto" && edf::FsrQualityValue(1) == "nativeaa");
  CHECK(edf::FsrQualityValue(2) == "quality" && edf::FsrQualityValue(3) == "balanced");
  CHECK(edf::FsrQualityValue(4) == "performance" && edf::FsrQualityValue(5) == "ultra_performance");
  CHECK(edf::FsrQualityValue(-1) == "auto" && edf::FsrQualityValue(6) == "auto");
  for (int i = 0; i < 6; ++i) CHECK(edf::FsrQualityIndex(edf::FsrQualityValue(i)) == i);
  CHECK(edf::FsrQualityIndex("nonsense") == 0);

  CHECK(edf::FsrPassesValue(0) == 1 && edf::FsrPassesValue(1) == 1 && edf::FsrPassesValue(4) == 4);
  CHECK(edf::FsrPassesValue(99) == 4 && edf::FsrPassesValue(-3) == 1);
}

void PutLe16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void PutLe32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  for (int i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

// Writes one XDVDFS directory entry; child links are dword offsets into the table.
void PutDirent(std::vector<uint8_t>& bytes, size_t at, uint16_t left, uint16_t right,
               uint32_t sector, uint32_t size, uint8_t attributes, const std::string& name) {
  PutLe16(bytes, at, left);
  PutLe16(bytes, at + 2, right);
  PutLe32(bytes, at + 4, sector);
  PutLe32(bytes, at + 8, size);
  bytes[at + 12] = attributes;
  bytes[at + 13] = static_cast<uint8_t>(name.size());
  std::copy(name.begin(), name.end(), bytes.begin() + at + 14);
}

// A minimal but structurally real volume: root holds default.xex plus a media/
// subdirectory holding sound.bin.
std::vector<uint8_t> MakeDiscImage() {
  constexpr size_t kSector = edf::xdvdfs::kSectorSize;
  std::vector<uint8_t> image(40 * kSector, 0);

  const size_t descriptor = 32 * kSector;
  std::memcpy(image.data() + descriptor, edf::xdvdfs::kMagic, edf::xdvdfs::kMagicLength);
  std::memcpy(image.data() + descriptor + kSector - edf::xdvdfs::kMagicLength,
              edf::xdvdfs::kMagic, edf::xdvdfs::kMagicLength);
  PutLe32(image, descriptor + 0x14, 33);
  PutLe32(image, descriptor + 0x18, static_cast<uint32_t>(kSector));

  const size_t root = 33 * kSector;
  std::fill(image.begin() + root, image.begin() + root + kSector, 0xFF);  // padding
  PutDirent(image, root, 0xFFFF, 7, 35, 19, 0x20, "default.xex");
  PutDirent(image, root + 28, 0xFFFF, 0xFFFF, 34, static_cast<uint32_t>(kSector), 0x10, "media");

  const size_t media = 34 * kSector;
  std::fill(image.begin() + media, image.begin() + media + kSector, 0xFF);
  PutDirent(image, media, 0xFFFF, 0xFFFF, 36, 4, 0x20, "sound.bin");

  const std::string xex = "default.xex content";
  std::copy(xex.begin(), xex.end(), image.begin() + 35 * kSector);
  const std::string sound = "abcd";
  std::copy(sound.begin(), sound.end(), image.begin() + 36 * kSector);
  return image;
}

void TestXdvdfs() {
  const std::vector<uint8_t> image = MakeDiscImage();
  // Presents the volume `shift` bytes into an image that is zero-filled before it.
  auto reader = [&image](uint64_t shift) {
    const uint64_t total = shift + image.size();
    return edf::xdvdfs::ReadFn([&image, shift, total](uint64_t offset, void* dst, size_t size) {
      if (offset > total || total - offset < size) return false;
      std::memset(dst, 0, size);
      for (size_t i = 0; i < size; ++i) {
        const uint64_t at = offset + i;
        if (at >= shift && at - shift < image.size())
          static_cast<uint8_t*>(dst)[i] = image[static_cast<size_t>(at - shift)];
      }
      return true;
    });
  };

  uint64_t base = 1;
  CHECK(edf::xdvdfs::FindPartition(reader(0), &base));
  CHECK(base == 0);

  std::vector<edf::xdvdfs::Entry> entries;
  std::string error;
  CHECK(edf::xdvdfs::List(reader(0), 0, &entries, &error));
  CHECK(entries.size() == 3);
  if (entries.size() == 3) {
    CHECK(entries[0].path == "media" && entries[0].directory);
    CHECK(entries[0].offset == 34 * 2048);
    CHECK(entries[1].path == "default.xex" && !entries[1].directory);
    CHECK(entries[1].offset == 35 * 2048 && entries[1].size == 19);
    CHECK(entries[2].path == "media/sound.bin" && !entries[2].directory);
    CHECK(entries[2].offset == 36 * 2048 && entries[2].size == 4);
  }

  // XGD3 layout: the same volume, 0x2080000 bytes into the image.
  base = 1;
  CHECK(edf::xdvdfs::FindPartition(reader(0x02080000ULL), &base));
  CHECK(base == 0x02080000ULL);

  // Anything that is not a disc image has no volume descriptor at any offset.
  const std::vector<uint8_t> junk(80 * 1024, 0);
  const edf::xdvdfs::ReadFn read_junk = [&junk](uint64_t offset, void* dst, size_t size) {
    if (offset > junk.size() || junk.size() - offset < size) return false;
    std::memcpy(dst, junk.data() + offset, size);
    return true;
  };
  CHECK(!edf::xdvdfs::FindPartition(read_junk, &base));
  CHECK(!edf::xdvdfs::List(read_junk, 0, &entries, &error));
  CHECK(!error.empty());

  CHECK(edf::xdvdfs::IsSafeName("default.xex"));
  CHECK(!edf::xdvdfs::IsSafeName("") && !edf::xdvdfs::IsSafeName(".."));
  CHECK(!edf::xdvdfs::IsSafeName("a/b") && !edf::xdvdfs::IsSafeName("a\\b"));
}
void TestKeybinds() {
  // The driver's grammar: comma-separated alternatives, "Mod+Mod+Key" per alternative.
  CHECK(edf::SplitBind("").empty());
  CHECK(edf::SplitBind("W").size() == 1);
  { const auto t = edf::SplitBind(" Ctrl+X , Z ,, "); CHECK(t.size() == 2 && t[0] == "Ctrl+X" && t[1] == "Z"); }
  CHECK(edf::JoinBind({"A", "B"}) == "A,B");
  CHECK(edf::JoinBind({}).empty());

  CHECK(edf::FormatBindToken(false, false, false, "W") == "W");
  CHECK(edf::FormatBindToken(true, false, false, "Up") == "Shift+Up");
  CHECK(edf::FormatBindToken(false, true, false, "X") == "Ctrl+X");
  CHECK(edf::FormatBindToken(false, false, true, "Z") == "Alt+Z");
  // Fixed modifier order, so the same combination always yields the same string.
  CHECK(edf::FormatBindToken(true, true, true, "Q") == "Shift+Ctrl+Alt+Q");
  CHECK(edf::FormatBindToken(true, true, true, "").empty());

  CHECK(edf::BindContains("Space,Semicolon", "Semicolon"));
  CHECK(!edf::BindContains("Space,Semicolon", "Space2"));
  CHECK(!edf::BindContains("", "Space"));

  CHECK(edf::AddBindAlternative("Space", "R") == "Space,R");
  CHECK(edf::AddBindAlternative("", "R") == "R");
  CHECK(edf::AddBindAlternative("Space", "Space") == "Space");  // no duplicates
  CHECK(edf::AddBindAlternative("Space", "") == "Space");
  CHECK(edf::RemoveBindAlternative("Space,R,Q", 1) == "Space,Q");
  CHECK(edf::RemoveBindAlternative("Space", 0).empty());
  CHECK(edf::RemoveBindAlternative("Space", 7) == "Space");

  CHECK(edf::PrettyBind("") == "(unbound)");
  CHECK(edf::PrettyBind("Space") == "Space");
  CHECK(edf::PrettyBind("Space,R") == "Space  or  R");

  // Mouse targets name game actions; each must point at the row it is labelled as.
  CHECK(edf::MouseTargetIndex("none") == 0 && edf::MouseTargetAt(0).action == -1);
  CHECK(edf::MouseTargetAt(edf::MouseTargetIndex("fire")).action == static_cast<int>(edf::kbm::Action::kFire));
  CHECK(edf::MouseTargetAt(edf::MouseTargetIndex("zoom")).action == static_cast<int>(edf::kbm::Action::kZoom));
  CHECK(edf::MouseTargetAt(edf::MouseTargetIndex("start")).action == static_cast<int>(edf::kbm::Action::kStart));
  for (int i = 1; i < edf::kMouseTargetCount; ++i) {
    const auto& target = edf::MouseTargetAt(i);
    CHECK(target.action >= 0 && target.action < static_cast<int>(std::size(edf::kKeyActions)));
    CHECK(std::string_view(target.label) == edf::kKeyActions[target.action].label);
  }
  // Unknown or out-of-range values fall back to "unbound" rather than a random action.
  CHECK(edf::MouseTargetIndex("nonsense") == 0);
  CHECK(edf::MouseTargetIndex("right_trigger") == 0);  // a pad-emulation era value
  CHECK(edf::MouseTargetAt(-1).action == -1 && edf::MouseTargetAt(999).action == -1);
  for (int i = 0; i < edf::kMouseTargetCount; ++i)
    CHECK(edf::MouseTargetIndex(edf::MouseTargetAt(i).value) == i);
  for (const auto& action : edf::kMouseActions)
    CHECK(edf::MouseTargetIndex(action.default_value) != 0);

  // Every action needs a distinct cvar, or two rows would edit the same binding.
  for (size_t i = 0; i < std::size(edf::kKeyActions); ++i) {
    CHECK(edf::kKeyActions[i].label && edf::kKeyActions[i].group);
    for (size_t j = i + 1; j < std::size(edf::kKeyActions); ++j)
      CHECK(std::string_view(edf::kKeyActions[i].cvar) != edf::kKeyActions[j].cvar);
  }

  // Defaults must not collide, or an action would silently shadow another in game.
  std::vector<std::pair<std::string, std::string>> defaults;
  for (const auto& action : edf::kKeyActions) defaults.emplace_back(action.cvar, action.default_value);
  for (const auto& action : edf::kKeyActions)
    for (const auto& token : edf::SplitBind(action.default_value))
      CHECK(edf::ConflictingAction(token, action.cvar, defaults).empty());
}

uint16_t TestKeyCode(std::string_view name) {
  if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') return static_cast<uint16_t>(name[0]);
  if (name == "Space") return 0x20;
  if (name == "Ctrl") return edf::kbm::kVkControl;
  return 0;
}

void TestNativeKbm() {
  using namespace edf::kbm;
  // The action enum is the index into the binding table, so the two must stay in step.
  CHECK(kActionCount == std::size(edf::kKeyActions));
  CHECK(std::string_view(edf::kKeyActions[static_cast<size_t>(Action::kMoveForward)].cvar) == "kbm_move_forward");
  CHECK(std::string_view(edf::kKeyActions[static_cast<size_t>(Action::kFire)].cvar) == "kbm_fire");
  CHECK(std::string_view(edf::kKeyActions[static_cast<size_t>(Action::kNextWeapon)].cvar) == "kbm_next_weapon");
  CHECK(std::string_view(edf::kKeyActions[static_cast<size_t>(Action::kRightStickPress)].cvar) == "kbm_rstick_press");
  CHECK(std::string_view(edf::kKeyActions[static_cast<size_t>(Action::kMenuRight)].cvar) == "kbm_menu_right");
  CHECK(std::string_view(edf::kKeyActions[static_cast<size_t>(Action::kBack)].cvar) == "kbm_back");

  // Binds: unknown names are dropped, and a bind needs only the modifiers it names.
  CHECK(ParseBind("", &TestKeyCode).empty());
  CHECK(ParseBind("W", nullptr).empty());
  CHECK(ParseBind("Nonsense", &TestKeyCode).empty());
  { const auto bind = ParseBind("W, Shift+Q ,Nonsense", &TestKeyCode);
    CHECK(bind.size() == 2 && bind[0].key == 'W' && bind[0].modifiers == 0);
    CHECK(bind[1].key == 'Q' && bind[1].modifiers == kModShift); }
  { const auto move = ParseBind("W", &TestKeyCode);
    const auto shifted = ParseBind("Shift+Q", &TestKeyCode);
    const auto bare_modifier = ParseBind("Ctrl", &TestKeyCode);
    KeyState keys{};
    CHECK(!BindPressed(move, keys) && !BindPressed(shifted, keys));
    keys['W'] = true;
    CHECK(BindPressed(move, keys));
    keys[kVkShift] = true;
    CHECK(BindPressed(move, keys));  // still walks while Shift is held
    keys['Q'] = true;
    CHECK(BindPressed(shifted, keys));
    keys[kVkShift] = false;
    CHECK(!BindPressed(shifted, keys));
    keys[kVkControl] = true;
    CHECK(BindPressed(bare_modifier, keys)); }  // a bare modifier is a usable key

  // Profile words outside the ten-name table keep the default instead of indexing past it.
  CHECK(ResolvePadControl(7, PadControl::kA) == PadControl::kRightTrigger);
  CHECK(ResolvePadControl(10, PadControl::kLeftShoulder) == PadControl::kLeftShoulder);
  CHECK(ResolvePadControl(0xFFFFFFFFu, PadControl::kB) == PadControl::kB);

  // Actions reach the pad controls the player's profile gives them.
  { ActionState actions{};
    const auto pressed = [](const ChannelFrame& frame, PadControl control) { return frame.pad[static_cast<size_t>(control)]; };
    ChannelFrame idle = BuildChannelFrame(actions, {});
    CHECK(idle.move_x == 0 && idle.move_y == 0 && idle.menu_x == 0 && idle.menu_y == 0 && !idle.start && !idle.back);
    for (bool control : idle.pad) CHECK(!control);

    actions[static_cast<size_t>(Action::kMoveForward)] = true;
    actions[static_cast<size_t>(Action::kStrafeLeft)] = true;
    actions[static_cast<size_t>(Action::kFire)] = true;
    actions[static_cast<size_t>(Action::kJump)] = true;
    ChannelFrame frame = BuildChannelFrame(actions, {});
    CHECK(frame.move_x == -1 && frame.move_y == 1);
    CHECK(pressed(frame, PadControl::kRightTrigger) && pressed(frame, PadControl::kLeftTrigger));
    CHECK(pressed(frame, PadControl::kA));  // jump doubles as menu confirm
    CHECK(!pressed(frame, PadControl::kLeftShoulder) && !pressed(frame, PadControl::kRightShoulder));

    TechnicalBindings remapped;
    remapped.fire = PadControl::kX;
    frame = BuildChannelFrame(actions, remapped);
    CHECK(pressed(frame, PadControl::kX) && !pressed(frame, PadControl::kRightTrigger));

    actions[static_cast<size_t>(Action::kMoveBack)] = true;  // opposite keys cancel
    CHECK(BuildChannelFrame(actions, {}).move_y == 0); }

  CHECK(MergeButton(0.25f, false) == 0.25f && MergeButton(0.25f, true) == 1.0f);

  // Mouse counts become the input that makes the guest turn by exactly that angle:
  // angle = input * (profile_sensitivity / zoom) * 0.05, so with zoom 1 the product below
  // must equal counts * sensitivity * 0.05 degrees.
  { const float input = AimInputForCounts(100.0f, 2.0f, 0.5f);
    const float turned = input * 0.5f * kGuestAimStep;
    CHECK(std::fabs(turned - 100.0f * 2.0f * kDegreesPerCount * kRadiansPerDegree) < 1e-6f);
    // The in-game stick sensitivity must not scale the mouse.
    const float other = AimInputForCounts(100.0f, 2.0f, 1.0f) * 1.0f * kGuestAimStep;
    CHECK(std::fabs(other - turned) < 1e-6f);
    CHECK(AimInputForCounts(-40.0f, 1.0f, 0.5f) == -AimInputForCounts(40.0f, 1.0f, 0.5f));
    // A zero, negative or non-finite profile value falls back to the guest default.
    CHECK(AimInputForCounts(10.0f, 1.0f, 0.0f) == AimInputForCounts(10.0f, 1.0f, kGuestDefaultSensitivity));
    CHECK(AimInputForCounts(10.0f, 1.0f, -3.0f) == AimInputForCounts(10.0f, 1.0f, kGuestDefaultSensitivity));
    CHECK(AimInputForCounts(10.0f, 1.0f, NAN) == AimInputForCounts(10.0f, 1.0f, kGuestDefaultSensitivity));
    CHECK(AimInputForCounts(NAN, 1.0f, 0.5f) == 0.0f); }

  // Mouse routing: exact angles while the on-foot aim update is running, a right-stick
  // rate otherwise, and nothing carried from one mode into the other.
  { MouseRouter router;
    float dx = 1.0f, dy = 1.0f;
    MouseRouter::Stick stick = router.OnPoll(10.0f, -5.0f, 1.0f);  // nobody has aimed yet
    CHECK(std::fabs(stick.x - 10.0f * MouseRouter::kStickPerCount) < 1e-6f);
    CHECK(std::fabs(stick.y - 5.0f * MouseRouter::kStickPerCount) < 1e-6f);  // mouse up is stick up
    router.OnAim(dx, dy);
    CHECK(dx == 0.0f && dy == 0.0f);  // the fallback's motion is not replayed on foot

    stick = router.OnPoll(3.0f, 4.0f, 1.0f);
    CHECK(stick.x == 0.0f && stick.y == 0.0f);
    router.OnPoll(1.0f, 1.0f, 1.0f);  // two polls before the next tick, as when unlocked
    router.OnAim(dx, dy);
    CHECK(dx == 4.0f && dy == 5.0f);
    router.OnAim(dx, dy);
    CHECK(dx == 0.0f && dy == 0.0f);

    for (uint32_t i = 0; i < MouseRouter::kOnFootGracePolls; ++i) router.OnPoll(2.0f, 0.0f, 1.0f);
    stick = router.OnPoll(1000.0f, 0.0f, 1.0f);  // aim stopped running: fallback, clamped
    CHECK(stick.x == 1.0f);
    router.OnAim(dx, dy);
    CHECK(dx == 0.0f && dy == 0.0f);

    router.OnPoll(7.0f, 0.0f, 1.0f);
    router.Reset();
    router.OnAim(dx, dy);
    CHECK(dx == 0.0f && dy == 0.0f); }
}

// ---- F1 menu (settings_logic.h, pause_menu.h) -----------------------------------------
void TestFrameRateMapping() {
  using namespace edf::settings;
  // Every choice maps to its cvar pair and back to itself.
  for (int i = 0; i < int(kFrameRateChoices.size()); ++i) {
    const auto choice = FrameRateAt(i);
    CHECK(FrameRateIndex(choice.unlock, choice.cap) == i);
  }
  CHECK(!FrameRateAt(0).unlock && FrameRateAt(0).cap == 0);    // 60 (locked): original presentation
  CHECK(FrameRateAt(1).unlock && FrameRateAt(1).cap == 120);
  CHECK(FrameRateAt(4).unlock && FrameRateAt(4).cap == 240);
  CHECK(FrameRateAt(5).unlock && FrameRateAt(5).cap == 0);     // uncapped
  CHECK(!FrameRateAt(-1).unlock && FrameRateAt(99).cap == 0);  // out of range reads as locked
  // Locked with a cap at or above 60 still presents at 60; below it is kept as custom.
  CHECK(FrameRateIndex(false, 60) == 0);
  CHECK(FrameRateIndex(false, 120) == 0);
  CHECK(FrameRateIndex(false, 30) == -1);
  // Unlocked with a cap that is not a choice is custom, not silently rounded.
  CHECK(FrameRateIndex(true, 100) == -1);
  CHECK(FrameRateIndex(true, -5) == 5);  // negative cap is "off"
}

void TestRendererAndUpscalerMapping() {
  using namespace edf::settings;
  CHECK(RendererIndex("native") == 0);
  CHECK(RendererIndex("off") == 1);
  CHECK(RendererIndex("") == 1);
  CHECK(RendererIndex("world") == -1 && RendererIndex("full") == -1);
  CHECK(RendererValue(0) == "native" && RendererValue(1) == "off");
  for (int i = 0; i < int(kNativeFsrValues.size()); ++i) CHECK(NativeFsrIndex(kNativeFsrValues[size_t(i)]) == i);
  CHECK(NativeFsrIndex("bogus") == 0);
  CHECK(kNativeFsrValues[5] == "ultra_performance");
}

void TestGraphicsPresets() {
  using namespace edf::settings;
  for (bool fsr : {false, true}) {
    for (int i = 0; i < 4; ++i) {
      const auto preset = GraphicsPreset(i);
      const auto values = PresetValues(preset, fsr);
      GraphicsState state{values.anisotropic, values.msaa, std::string(values.fsr), values.sharpness};
      CHECK(MatchPreset(state, fsr) == preset);  // every preset recognizes itself
      CHECK(values.anisotropic >= 0 && values.anisotropic <= 5);
      CHECK(values.msaa == 0 || values.msaa == 1 || values.msaa == 2 || values.msaa == 4);
    }
    // Presets are a ladder: filtering never gets worse going up.
    for (int i = 1; i < 4; ++i)
      CHECK(PresetValues(GraphicsPreset(i), fsr).anisotropic >= PresetValues(GraphicsPreset(i - 1), fsr).anisotropic);
  }
  // With the upscaler, it does the anti-aliasing: MSAA off everywhere.
  for (int i = 0; i < 4; ++i) CHECK(PresetValues(GraphicsPreset(i), true).msaa == 1);
  CHECK(PresetValues(GraphicsPreset::kPerformance, true).fsr == "performance");
  CHECK(PresetValues(GraphicsPreset::kUltra, true).fsr == "native_aa");
  // Without it, the fsr fields are ignored when matching.
  GraphicsState plain{PresetValues(GraphicsPreset::kUltra, false).anisotropic, 4, "quality", 0.9};
  CHECK(MatchPreset(plain, false) == GraphicsPreset::kUltra);
  CHECK(MatchPreset(plain, true) == GraphicsPreset::kCustom);
  GraphicsState custom{-1, 0, "off", 0.0};  // game-default filtering matches no preset
  CHECK(MatchPreset(custom, false) == GraphicsPreset::kCustom);
  CHECK(std::string(kGraphicsPresetLabels[size_t(GraphicsPreset::kCustom)]) == "Custom");
}

void TestRestartClassification() {
  using namespace edf::settings;
  for (const char* cvar : {"edf_native_renderer", "edf_native_scene_backend", "edf_native_render_width",
                           "edf_native_msaa", "edf_aspect", "window_width", "video_mode_height", "edf_kbm",
                           "edf_native_thread_qos", "present_effect", "present_fsr_sharpness_reduction",
                           "swap_post_effect"})
    CHECK(NeedsRestart(cvar));
  for (const char* cvar : {"edf_native_anisotropic_filtering", "edf_native_vsync", "edf_native_unlock_framerate",
                           "edf_fps_cap", "edf_frame_pacer_before_present", "audio_mute", "edf_rumble",
                           "edf_kbm_sensitivity", "edf_display_mode", "fullscreen", "edf_show_fps"})
    CHECK(!NeedsRestart(cvar));
  // Unknown (renderer-owned) cvars follow their own description.
  CHECK(NeedsRestart("edf_native_fsr", "Upscaler quality mode (restart required)"));
  CHECK(NeedsRestart("edf_native_fsr", "Read AT STARTUP"));
  CHECK(!NeedsRestart("edf_native_fsr", "Upscaler quality mode; applies live"));
  CHECK(!NeedsRestart("edf_native_fsr_sharpness"));

  std::map<std::string, std::string> running{{"edf_native_msaa", "0"}, {"window_width", "1280"},
                                             {"window_height", "720"}, {"edf_aspect", "native"}};
  const RestartTracker::Getter get = [&](std::string_view name) {
    const auto found = running.find(std::string(name));
    return found == running.end() ? std::string() : found->second;
  };
  RestartTracker tracker;
  CHECK(tracker.empty() && tracker.Count(get) == 0);
  tracker.Stage("edf_native_msaa", {{"edf_native_msaa", "4"}});
  tracker.Stage("window_size", {{"window_width", "1920"}, {"window_height", "1080"}});
  CHECK(tracker.Count(get) == 2);  // two settings, three cvars: counted by setting
  CHECK(tracker.Pending("edf_native_msaa", get) == "4");
  CHECK(tracker.Pending("edf_aspect", get) == "native");  // not staged: the running value
  CHECK(tracker.Differs("window_size", get) && !tracker.Differs("edf_aspect", get));
  CHECK(tracker.Values("window_size").size() == 2 && tracker.Values("nothing").empty());
  // Staged back to what is running: no longer a pending change.
  tracker.Stage("edf_native_msaa", {{"edf_native_msaa", "0"}});
  CHECK(tracker.Count(get) == 1);
  CHECK(tracker.Overrides().size() == 3);
  tracker.Unstage("window_size");
  CHECK(tracker.Count(get) == 0);
  // After the restart the running value is the staged one: nothing pending.
  tracker.Stage("edf_native_msaa", {{"edf_native_msaa", "4"}});
  running["edf_native_msaa"] = "4";
  CHECK(tracker.Count(get) == 0);
  tracker.Clear();
  CHECK(tracker.empty());
}

void TestConfigOverrides() {
  using edf::settings::ConfigOverride;
  const std::string config = "# header\r\nedf_native_msaa = 2\nwindow_width = 1920\nedf_aspect = \"native\"\n";
  const std::vector<ConfigOverride> overrides{{"edf_native_msaa", "4", false},
                                              {"edf_aspect", "\"ultrawide\"", false},
                                              {"window_width", "0", true},
                                              {"edf_kbm", "false", false}};
  const std::string out = edf::settings::ApplyConfigOverrides(config, overrides);
  CHECK(out == "# header\nedf_native_msaa = 4\nedf_aspect = \"ultrawide\"\nedf_kbm = false\n");
  // Keys are matched whole: a longer name that starts the same survives.
  const std::vector<ConfigOverride> one{{"window_width", "800", false}};
  CHECK(edf::settings::ApplyConfigOverrides("window_width_extra = 1\n", one) ==
        "window_width_extra = 1\nwindow_width = 800\n");
  CHECK(edf::settings::ApplyConfigOverrides("a = 1\n", {}) == "a = 1\n");
}

void TestRevertCountdown() {
  edf::settings::RevertCountdown countdown;
  CHECK(!countdown.active() && !countdown.Expire(100.0) && countdown.SecondsLeft(0) == 0);
  countdown.Start(50.0);
  CHECK(countdown.active());
  CHECK(countdown.SecondsLeft(50.0) == 10);
  CHECK(countdown.SecondsLeft(50.2) == 10);  // rounded up: never shows 0 while waiting
  CHECK(countdown.SecondsLeft(59.5) == 1);
  CHECK(!countdown.Expire(59.99));
  CHECK(countdown.Expire(60.0));   // fires once...
  CHECK(!countdown.Expire(61.0));  // ...and only once
  CHECK(!countdown.active() && countdown.SecondsLeft(61.0) == 0);
  countdown.Start(0.0, 3.0);
  countdown.Keep();
  CHECK(!countdown.active() && !countdown.Expire(10.0));  // kept: never reverts
  countdown.Start(0.0);
  countdown.Start(5.0);  // a second change restarts the wait
  CHECK(!countdown.Expire(12.0) && countdown.Expire(15.0));
}

void TestMenuScaling() {
  using namespace edf::settings;
  CHECK(MenuScale(1080.0f) == 1.0f);
  CHECK(MenuScale(2160.0f) == 2.0f);
  CHECK(std::fabs(MenuScale(1440.0f) - 1.3333f) < 1e-3f);
  CHECK(MenuScale(480.0f) == 0.75f);     // never unreadably small
  CHECK(MenuScale(0.0f) == 1.0f);        // unknown height: design size
  CHECK(MenuScale(1080.0f, 1.5f) == 1.5f);
  CHECK(MenuScale(1080.0f, 9.0f) == 2.0f);  // user scale clamped
  const float sizes[] = {14.0f, 18.0f, 24.0f, 36.0f};
  CHECK(PickBakedSize(sizes, 18.0f) == 1);  // exact
  CHECK(PickBakedSize(sizes, 20.0f) == 2);  // never scaled up...
  CHECK(PickBakedSize(sizes, 17.6f) == 1);  // ...beyond a hair
  CHECK(PickBakedSize(sizes, 10.0f) == 0);
  CHECK(PickBakedSize(sizes, 72.0f) == 3);  // largest when nothing is big enough
}

void TestPauseController() {
  edf::menu::PauseController pause;
  // Pause and mute on: the engine holds and audio is forced off.
  auto open = pause.Open(true, true, true, false);
  CHECK(open.hold_engine && open.set_mute && *open.set_mute);
  CHECK(pause.open() && pause.holding() && pause.forcing_mute());
  CHECK(pause.PersistentMute(true) == false);  // the saved value is the player's, not the forced one
  // Opening again while open changes nothing.
  auto again = pause.Open(false, false, true, true);
  CHECK(again.hold_engine && !again.set_mute);
  auto close = pause.Close();
  CHECK(!close.hold_engine && close.set_mute && !*close.set_mute);  // sound back as it was
  CHECK(!pause.open() && !pause.holding());
  CHECK(!pause.Close().set_mute);  // closing twice is harmless
  // Player already muted: nothing to force, nothing to give back.
  open = pause.Open(true, true, true, true);
  CHECK(open.hold_engine && !open.set_mute);
  close = pause.Close();
  CHECK(close.set_mute && *close.set_mute);
  // Mute toggled inside the menu is what comes back.
  pause.Open(true, true, true, false);
  pause.SetUserMute(true);
  CHECK(pause.PersistentMute(true) == true);
  close = pause.Close();
  CHECK(close.set_mute && *close.set_mute);
  // Pause switched off, or nothing running yet (--settings before boot).
  open = pause.Open(false, true, true, false);
  CHECK(!open.hold_engine && open.set_mute);
  pause.Close();
  open = pause.Open(true, true, false, false);
  CHECK(!open.hold_engine && !open.set_mute && !pause.forcing_mute());
  pause.Close();
}

void TestMenuInputGate() {
  using Gate = edf::menu::MenuInputGate;
  Gate gate;
  // Closed menu: everything reaches the game.
  CHECK(!gate.Block(Gate::kPad, false, 0) && !gate.Block(Gate::kKeyboardMouse, false, 0));
  gate.Open();
  CHECK(gate.menu_open());
  CHECK(gate.Block(Gate::kPad, true, 10) && gate.Block(Gate::kKeyboardMouse, true, 10));  // even idle input
  gate.Close(1000);
  CHECK(gate.state() == Gate::State::kDraining);
  // The button that closed the menu is still down: withheld, per source.
  CHECK(gate.Block(Gate::kPad, false, 1010));
  CHECK(!gate.Block(Gate::kKeyboardMouse, true, 1010));  // keyboard idle: passes at once
  CHECK(gate.Block(Gate::kPad, false, 1500));
  CHECK(!gate.Block(Gate::kPad, true, 1600));            // released: passes...
  CHECK(!gate.Block(Gate::kPad, false, 1700));           // ...and a new press is a real one
  CHECK(gate.state() == Gate::State::kGame);
  // Held past the timeout: let through rather than lock the player out.
  gate.Open();
  gate.Close(0);
  CHECK(gate.Block(Gate::kKeyboardMouse, false, 1499));
  CHECK(!gate.Block(Gate::kKeyboardMouse, false, Gate::kDrainTimeoutMs));
  CHECK(gate.state() == Gate::State::kGame);
  // A source that never polls (keyboard with native K/M off) cannot keep the drain open.
  gate.Open();
  gate.Close(0);
  CHECK(!gate.Block(Gate::kPad, true, 5));
  CHECK(gate.state() == Gate::State::kDraining);
  CHECK(!gate.Block(Gate::kPad, false, 2000) && gate.state() == Gate::State::kGame);
  // Close without open is a no-op.
  gate.Close(0);
  CHECK(gate.state() == Gate::State::kGame);
}

void TestPadChord() {
  using namespace edf::menu;
  CHECK(ParsePadChord("back+start") == (kPadBack | kPadStart));
  CHECK(ParsePadChord("LS+RS") == (kPadLeftThumb | kPadRightThumb));
  CHECK(ParsePadChord("back+rb+y") == (kPadBack | kPadRightShoulder | kPadY));
  CHECK(ParsePadChord("off") == 0);
  CHECK(ParsePadChord("start") == 0);        // one button is not a chord
  CHECK(ParsePadChord("back+select") == 0);  // unknown name: off, not a partial chord
  CHECK(ParsePadChord("") == 0 && ParsePadChord("back+") == 0);
  for (const auto& option : kChordOptions)
    CHECK((ParsePadChord(option.value) != 0) == (std::string_view(option.value) != "off"));

  PadChord chord;
  const uint16_t mask = kPadBack | kPadStart;
  auto r = chord.Filter(mask, kPadA);
  CHECK(!r.fired && r.game_buttons == kPadA);
  r = chord.Filter(mask, kPadBack);  // first chord button: an ordinary press so far
  CHECK(!r.fired && r.game_buttons == kPadBack);
  r = chord.Filter(mask, kPadBack | kPadStart | kPadA);
  CHECK(r.fired && r.game_buttons == kPadA);  // the game never sees Start (its own pause menu)
  r = chord.Filter(mask, kPadBack | kPadStart);
  CHECK(!r.fired && r.game_buttons == 0);  // held: fires once
  r = chord.Filter(mask, kPadStart);
  CHECK(!r.fired && r.game_buttons == kPadStart);
  r = chord.Filter(mask, 0);
  r = chord.Filter(mask, kPadBack | kPadStart);
  CHECK(r.fired);  // released and pressed again: fires again
  r = chord.Filter(0, kPadBack | kPadStart);
  CHECK(!r.fired && r.game_buttons == (kPadBack | kPadStart));  // chord off: untouched

  PadSnapshot pad;
  CHECK(PadIdle(pad));
  pad.lx = 5000;
  CHECK(PadIdle(pad));  // inside the dead zone
  pad.ly = -20000;
  CHECK(!PadIdle(pad));
  pad = {};
  pad.right_trigger = 200;
  CHECK(!PadIdle(pad));
  pad = {};
  pad.buttons = kPadB;
  CHECK(!PadIdle(pad));
}

void TestGuestPadRouting() {
  using namespace edf::menu;
  // The process-wide route: chord fires the open request, the gate blanks the pad.
  int requests = 0;
  SetOpenRequestHandler([&] { ++requests; });
  PadSnapshot pad;
  pad.buttons = kPadBack;
  auto routed = RouteGuestPad(pad, "back+start", true);
  CHECK(!routed.blank && routed.buttons == kPadBack && requests == 0);
  pad.buttons = kPadBack | kPadStart;
  routed = RouteGuestPad(pad, "back+start", true);
  CHECK(requests == 1 && !routed.blank && routed.buttons == 0);
  OpenGate();
  CHECK(MenuOpen());
  routed = RouteGuestPad(pad, "back+start", true);
  CHECK(routed.blank && requests == 1);
  pad.buttons = kPadB;
  CHECK(RouteGuestPad(pad, "back+start", false).blank);  // other players too
  CloseGate();
  CHECK(!MenuOpen());
  CHECK(RouteGuestPad(pad, "back+start", true).blank);   // B that closed it: withheld
  CHECK(RouteGuestPad(pad, "back+start", false).blank);  // other players wait for player 0
  pad.buttons = 0;
  CHECK(!RouteGuestPad(pad, "back+start", true).blank);  // released
  CHECK(!BlockGameInput(MenuInputGate::kKeyboardMouse, true));
  SetOpenRequestHandler({});
  // Engine hold: nothing to wait for when no pause is requested.
  CHECK(!HoldWhilePaused());
  Engine().requested = true;
  std::thread release([] {
    while (!Engine().holding.load()) std::this_thread::yield();
    Engine().requested = false;
  });
  CHECK(HoldWhilePaused());
  release.join();
  CHECK(!Engine().holding.load() && Engine().holds.load() == 1);
}
void TestControllerRemap() {
  using namespace edf::pad;
  using edf::menu::PadSnapshot;
  namespace m = edf::menu;
  // Tokens: unique across controls and synthetic actions, and each round-trips.
  for (int a = 0; a < kTargetCount; ++a) {
    CHECK(TargetFromToken(TargetToken(a)) == a);
    for (int b = a + 1; b < kTargetCount; ++b) CHECK(std::string_view(TargetToken(a)) != TargetToken(b));
  }
  CHECK(TargetOf(SyntheticAction::kReload) == kControlCount);
  CHECK(IsSyntheticTarget(TargetOf(SyntheticAction::kReload)) && !IsSyntheticTarget(kA));

  // The default table is the identity and leaves the pad untouched (Guide passes through).
  const PadRemap identity = PadRemap::Default();
  CHECK(identity.IsDefault() && UnusedSources(identity) == 0);
  PadSnapshot pad;
  pad.buttons = m::kPadA | m::kPadUp | 0x0400;
  pad.left_trigger = 100; pad.right_trigger = 20;
  pad.lx = 1000; pad.ly = -2000; pad.rx = 3000; pad.ry = -32768;
  auto r = ApplyRemap(pad, identity);
  CHECK(r.game.buttons == pad.buttons && r.synthetic == 0);
  CHECK(r.game.left_trigger == 100 && r.game.right_trigger == 20);
  CHECK(r.game.lx == 1000 && r.game.ly == -2000 && r.game.rx == 3000 && r.game.ry == -32768);

  // Swapping A and B is two assignments' worth of swap: Set A <- B.
  PadRemap ab = AssignSource(identity, kA, kB);
  CHECK(ab.source[kA] == kB && ab.source[kB] == kA && UnusedSources(ab) == 0);
  pad = {};
  pad.buttons = m::kPadA;
  CHECK(ApplyRemap(pad, ab).game.buttons == m::kPadB);
  pad.buttons = m::kPadB | m::kPadX;
  CHECK(ApplyRemap(pad, ab).game.buttons == (m::kPadA | m::kPadX));
  // Assigning the source a row already has changes nothing.
  CHECK(AssignSource(ab, kA, kB) == ab);
  // Out-of-range rows and sources are ignored.
  CHECK(AssignSource(ab, kTargetCount, kA) == ab && AssignSource(ab, kA, kControlCount) == ab);

  // Triggers mapped to each other stay analogue.
  PadRemap triggers = AssignSource(identity, kLT, kRT);
  CHECK(triggers.source[kLT] == kRT && triggers.source[kRT] == kLT);
  pad = {};
  pad.left_trigger = 17; pad.right_trigger = 200;
  r = ApplyRemap(pad, triggers);
  CHECK(r.game.left_trigger == 200 && r.game.right_trigger == 17 && r.game.buttons == 0);

  // A trigger driving a digital button: pressed from the XInput threshold on. The button
  // it displaced drives the trigger all-or-nothing.
  PadRemap rt_to_rb = AssignSource(identity, kRB, kRT);
  CHECK(rt_to_rb.source[kRB] == kRT && rt_to_rb.source[kRT] == kRB);
  pad = {};
  pad.right_trigger = kTriggerPressThreshold - 1;
  r = ApplyRemap(pad, rt_to_rb);
  CHECK(r.game.buttons == 0 && r.game.right_trigger == 0);
  pad.right_trigger = kTriggerPressThreshold;
  r = ApplyRemap(pad, rt_to_rb);
  CHECK(r.game.buttons == m::kPadRightShoulder && r.game.right_trigger == 0);
  pad = {};
  pad.buttons = m::kPadRightShoulder;
  r = ApplyRemap(pad, rt_to_rb);
  CHECK(r.game.buttons == 0 && r.game.right_trigger == 255);

  // Clearing a row: the physical button then does nothing, and says so.
  PadRemap cleared = AssignSource(identity, kY, kNoSource);
  CHECK(cleared.source[kY] == kNoSource && UnusedSources(cleared) == (1u << kY));
  pad = {};
  pad.buttons = m::kPadY;
  CHECK(ApplyRemap(pad, cleared).game.buttons == 0);

  // Synthetic action: taking a button frees it from its game control.
  const int reload = TargetOf(SyntheticAction::kReload);
  PadRemap with_reload = AssignSource(identity, reload, kLS);
  CHECK(with_reload.source[reload] == kLS && with_reload.source[kLS] == kNoSource);
  pad = {};
  pad.buttons = m::kPadLeftThumb | m::kPadA;
  r = ApplyRemap(pad, with_reload);
  CHECK(r.game.buttons == m::kPadA && r.synthetic == (1u << unsigned(SyntheticAction::kReload)));
  // Taking the button back for its control unbinds the action again.
  PadRemap back = AssignSource(with_reload, kLS, kLS);
  CHECK(back.source[kLS] == kLS && back.source[reload] == kNoSource && back.IsDefault());
  // A trigger can drive an action too.
  r = ApplyRemap(PadSnapshot{0, 0, 255, 0, 0, 0, 0}, AssignSource(identity, reload, kRT));
  CHECK(r.synthetic == 1u && r.game.right_trigger == 0);

  // Sticks: swap first, then invert what the game sees; -32768 inverts to +32767.
  PadRemap sticks = identity;
  sticks.swap_sticks = true;
  sticks.invert_ry = true;
  pad = {};
  pad.lx = 100; pad.ly = -32768; pad.rx = 300; pad.ry = 400;
  r = ApplyRemap(pad, sticks);
  CHECK(r.game.lx == 300 && r.game.ly == 400 && r.game.rx == 100 && r.game.ry == 32767);
  sticks = identity;
  sticks.invert_lx = sticks.invert_ly = sticks.invert_rx = true;
  r = ApplyRemap(pad, sticks);
  CHECK(r.game.lx == -100 && r.game.ly == 32767 && r.game.rx == -300 && r.game.ry == 400);

  // Capture helpers: pressed controls, including triggers, and the lowest one.
  pad = {};
  pad.buttons = m::kPadB | m::kPadDown;
  pad.left_trigger = 200;
  const uint32_t pressed = PressedControls(pad);
  CHECK(pressed == ((1u << kB) | (1u << kDown) | (1u << kLT)));
  CHECK(FirstControl(pressed) == kB && FirstControl(0) == kNoSource);
  CHECK(FirstControl(pressed & ~(1u << kB)) == kLT);

  // Process-wide synthetic state, as the pad hook publishes it.
  PublishSynthetic(1, 1u << unsigned(SyntheticAction::kReload));
  CHECK(SyntheticActionHeld(1, SyntheticAction::kReload) && !SyntheticActionHeld(0, SyntheticAction::kReload));
  PublishSynthetic(1, 0);
  CHECK(!SyntheticActionHeld(1, SyntheticAction::kReload));
  PublishSynthetic(9, 1);  // out of range: ignored
  CHECK(!SyntheticActionHeld(9, SyntheticAction::kReload));
}

void TestControllerRemapPersistence() {
  using namespace edf::pad;
  CHECK(SerializeRemap(PadRemap::Default()).empty());
  CHECK(ParseRemap("").IsDefault());
  PadRemap remap = AssignSource(PadRemap::Default(), kA, kB);
  remap = AssignSource(remap, kLT, kRT);
  remap = AssignSource(remap, TargetOf(SyntheticAction::kReload), kBack);
  remap = AssignSource(remap, kUp, kNoSource);
  remap.swap_sticks = true;
  remap.invert_ly = true;
  remap.invert_rx = true;
  const std::string text = SerializeRemap(remap);
  CHECK(text == "a=b,b=a,lt=rt,rt=lt,back=none,up=none,reload=back,swap_sticks,invert_ly,invert_rx");
  CHECK(ParseRemap(text) == remap);
  // Nothing a TOML string would need escaped.
  CHECK(text.find_first_of("\"\\\n") == std::string::npos);
  // Every single-row change round-trips, for every row and source.
  for (int target = 0; target < kTargetCount; ++target)
    for (int source = kNoSource; source < kControlCount; ++source) {
      const PadRemap one = AssignSource(PadRemap::Default(), target, source);
      CHECK(ParseRemap(SerializeRemap(one)) == one);
    }
  // Lenient reading: spaces, case, empty and unknown tokens (a newer build's action) are
  // skipped; the rest still applies.
  const PadRemap lenient = ParseRemap(" A = B , ,B=a,future_action=x,x=bogus,nonsense,SWAP_STICKS ");
  CHECK(lenient.source[kA] == kB && lenient.source[kB] == kA && lenient.source[kX] == kX && lenient.swap_sticks);
  // Through the config writer, as the settings menu saves it, and back.
  using edf::settings::ConfigOverride;
  const std::vector<ConfigOverride> overrides{{"edf_pad_remap_p1", "\"" + text + "\"", false}};
  const std::string config = edf::settings::ApplyConfigOverrides("edf_pad_remap_p1 = \"\"\n", overrides);
  CHECK(config == "edf_pad_remap_p1 = \"" + text + "\"\n");
  const size_t open = config.find('"'), close = config.rfind('"');
  CHECK(ParseRemap(std::string_view(config).substr(open + 1, close - open - 1)) == remap);
  // The remap and dead-zone cvars apply live; the input backend needs a restart.
  CHECK(!edf::settings::NeedsRestart("edf_pad_remap_p1") && !edf::settings::NeedsRestart("edf_pad_left_deadzone"));
  CHECK(edf::settings::NeedsRestart("input_backend"));
}

void TestDeadzones() {
  using namespace edf::pad;
  using edf::menu::PadSnapshot;
  // Off: untouched, extremes included.
  int16_t x = -32768, y = 32767;
  ApplyStickDeadzone(x, y, 0.0f);
  CHECK(x == -32768 && y == 32767);
  // Inside the radius: centred. The radius is round, not per axis.
  x = 5000; y = 5000;  // magnitude ~0.216
  ApplyStickDeadzone(x, y, 0.25f);
  CHECK(x == 0 && y == 0);
  x = 7000; y = 7000;  // magnitude ~0.302: outside, though each axis alone is inside
  ApplyStickDeadzone(x, y, 0.25f);
  CHECK(x > 0 && y > 0 && x == y);
  // Rescaled: just past the edge is small, full travel stays full, direction is kept.
  x = int16_t(0.26f * 32767); y = 0;
  ApplyStickDeadzone(x, y, 0.25f);
  CHECK(x > 0 && x < 600 && y == 0);
  x = 32767; y = 0;
  ApplyStickDeadzone(x, y, 0.25f);
  CHECK(x == 32767 && y == 0);
  x = 0; y = -32768;
  ApplyStickDeadzone(x, y, 0.25f);
  CHECK(x == 0 && y == -32767);
  x = -16384; y = 0;  // half travel with a 20% zone: (0.5 - 0.2) / 0.8 = 0.375
  ApplyStickDeadzone(x, y, 0.2f);
  CHECK(std::abs(x - int(-0.375 * 32767)) <= 2 && y == 0);
  x = 23170; y = 23170;  // corner of a square-gated stick: clamped to the unit circle
  ApplyStickDeadzone(x, y, 0.1f);
  CHECK(std::sqrt(double(x) * x + double(y) * y) <= 32767.5 && x == y);
  // The zone is capped at 90%.
  x = int16_t(0.95f * 32767); y = 0;
  ApplyStickDeadzone(x, y, 5.0f);
  CHECK(x > 0);

  // Triggers.
  CHECK(ApplyTriggerDeadzone(0, 0.0f) == 0 && ApplyTriggerDeadzone(255, 0.0f) == 255 &&
        ApplyTriggerDeadzone(77, 0.0f) == 77);
  CHECK(ApplyTriggerDeadzone(51, 0.2f) == 0);   // 20% of 255 = 51
  CHECK(ApplyTriggerDeadzone(52, 0.2f) == 1);
  CHECK(std::abs(ApplyTriggerDeadzone(153, 0.2f) - 128) <= 1);  // (153 - 51) * 255 / 204 = 127.5
  CHECK(ApplyTriggerDeadzone(255, 0.2f) == 255);

  // Percent cvars to fractions, clamped.
  const auto zones = Deadzones::FromPercent(10, 200, -5);
  CHECK(std::abs(zones.left - 0.1f) < 1e-6f && zones.right == kMaxDeadzone && zones.trigger == 0.0f);

  // The whole pad: dead zones on the physical sticks, then the table (the swap moves the
  // already dead-zoned stick), and a trigger under the threshold does not press a button.
  PadRemap remap = AssignSource(PadRemap::Default(), kA, kLT);
  remap.swap_sticks = true;
  PadSnapshot pad;
  pad.lx = 3000; pad.rx = 32767; pad.left_trigger = 60;
  auto r = ProcessPad(pad, Deadzones::FromPercent(20, 0, 20), &remap);
  CHECK(r.game.rx == 0 && r.game.lx == 32767);  // the left stick's drift is gone, on the right
  CHECK(r.game.buttons == 0);                   // 60 -> 11 after a 20% threshold: under 30
  pad.left_trigger = 120;
  r = ProcessPad(pad, Deadzones::FromPercent(20, 0, 20), &remap);
  CHECK(r.game.buttons == edf::menu::kPadA);
  // No table (players 3 and 4): dead zones only.
  r = ProcessPad(pad, Deadzones::FromPercent(20, 0, 0), nullptr);
  CHECK(r.game.lx == 0 && r.game.rx == 32767 && r.game.left_trigger == 120 && r.synthetic == 0);
}
}  // namespace

int main() {
  TestXexParsing();
  TestConfigFiltering();
  TestScriptedInput();
  TestFrameLogic();
  TestGraphicsMapping();
  TestXdvdfs();
  TestKeybinds();
  TestNativeKbm();
  TestFrameRateMapping();
  TestRendererAndUpscalerMapping();
  TestGraphicsPresets();
  TestRestartClassification();
  TestConfigOverrides();
  TestRevertCountdown();
  TestMenuScaling();
  TestPauseController();
  TestMenuInputGate();
  TestPadChord();
  TestGuestPadRouting();
  TestControllerRemap();
  TestControllerRemapPersistence();
  TestDeadzones();
  if (failures) std::cerr << failures << " test assertion(s) failed\n";
  else std::cout << "All unit tests passed\n";
  return failures ? 1 : 0;
}
