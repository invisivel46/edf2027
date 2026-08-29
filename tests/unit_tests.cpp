#include "core_logic.h"
#include "scripted_input_logic.h"

#include <cmath>
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
}

void TestGraphicsMapping() {
  auto mode = edf::GuestVideoMode(1920, 1080, "native");
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
}
}  // namespace

int main() {
  TestXexParsing();
  TestConfigFiltering();
  TestScriptedInput();
  TestFrameLogic();
  TestGraphicsMapping();
  if (failures) std::cerr << failures << " test assertion(s) failed\n";
  else std::cout << "All unit tests passed\n";
  return failures ? 1 : 0;
}
