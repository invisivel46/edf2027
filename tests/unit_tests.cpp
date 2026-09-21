#include "core_logic.h"
#include "scripted_input_logic.h"
#include "keybind_logic.h"
#include "native_kbm_logic.h"
#include "xdvdfs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
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
  using Clock=std::chrono::steady_clock;
  using namespace std::chrono_literals;
  const auto epoch=Clock::time_point{};
  CHECK(edf::NextFrameDeadline(epoch,epoch+2ms,120,false)==epoch+8333333ns);
  CHECK(edf::NextFrameDeadline(epoch,epoch+20ms,120,false)==epoch+20ms);
  CHECK(edf::NextFrameDeadline(epoch+20ms,epoch+40ms,120,false)==epoch+40ms);
  CHECK(edf::NextFrameDeadline(epoch+40ms,epoch+41ms,120,false)==epoch+40ms+8333333ns);
  CHECK(edf::NextFrameDeadline(epoch,epoch+2ms,60,true)==epoch+2ms);
  CHECK(edf::NextFrameDeadline(epoch,epoch+2ms,0,false)==epoch+2ms);
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
  if (failures) std::cerr << failures << " test assertion(s) failed\n";
  else std::cout << "All unit tests passed\n";
  return failures ? 1 : 0;
}
