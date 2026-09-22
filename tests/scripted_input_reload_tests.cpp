#include "../src/scripted_input_logic.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main() {
  namespace fs=std::filesystem;
  const auto path=fs::temp_directory_path()/(
    "edf-input-reload-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".txt");
  struct Cleanup {fs::path path; ~Cleanup(){std::error_code error; fs::remove(path,error);}} cleanup{path};
  try {
    std::istringstream analog_input("100 100 0000 0 255 -32768 32767 12000 -10000\n"
      "120 10 1000\n130 10 0000 0 0 0 0 0 0\n");
    const auto analog_events=edf::ParseInputEvents(analog_input);
    Require(analog_events.size()==3,"analog or legacy event parsing");
    const edf::ScriptedAnalog expected{0,255,-32768,32767,12000,-10000};
    Require(edf::AnalogAt(analog_events,99)==edf::ScriptedAnalog{} &&
      edf::AnalogAt(analog_events,100)==expected,"analog interval start");
    Require(edf::AnalogAt(analog_events,125)==expected && edf::ButtonsAt(analog_events,125)==0x1000,
      "button event interrupted analog hold");
    Require(edf::AnalogAt(analog_events,135)==edf::ScriptedAnalog{} &&
      edf::AnalogAt(analog_events,140)==expected && edf::AnalogAt(analog_events,200)==edf::ScriptedAnalog{},
      "analog override, resume or expiry");
    for(const char* bad:{"0 1 0 0", "0 1 0 -1 0 0 0 0 0", "0 1 0 0 256 0 0 0 0",
      "0 1 0 0 0 -32769 0 0 0", "0 1 0 0 0 0 32768 0 0", "0 1 0 0 0 0 0 0 0 extra",
      "0 1 0 0 0 0 0 0 1x", "0 1 0 0 0 0 0 0 999999999999999999999999"}) {
      std::istringstream input(bad);
      Require(edf::ParseInputEvents(input).empty(),"malformed analog event accepted");
    }
    std::vector<edf::ScriptedInputEvent> events{{10,20,0x10}};
    std::optional<fs::file_time_type> stamp;
    Require(!edf::ReloadInputEvents(path,stamp,events) && events.size()==1,"missing file changed events");
    auto write=[&](const char* content) {
      {std::ofstream output(path); output<<content; if(!output) throw std::runtime_error("fixture write failed");}
      // Explicitly advance metadata so this test does not depend on filesystem
      // timestamp granularity or sleep durations.
      if(stamp) fs::last_write_time(path,*stamp+std::chrono::seconds(2));
    };
    write("100 50 0010\n");
    Require(edf::ReloadInputEvents(path,stamp,events),"initial file not loaded");
    Require(edf::ButtonsAt(events,110)==0x10 && edf::ButtonsAt(events,150)==0,"initial timing mismatch");
    Require(!edf::ReloadInputEvents(path,stamp,events),"unchanged file reloaded");
    write("100 50 0010\n1000 100 1000\n");
    Require(edf::ReloadInputEvents(path,stamp,events),"changed file not loaded");
    Require(edf::ButtonsAt(events,900)==0 && edf::ButtonsAt(events,1000)==0x1000 &&
      edf::ButtonsAt(events,1100)==0,"reload reset/replayed schedule");
    write("# clear the pending inputs\n");
    Require(edf::ReloadInputEvents(path,stamp,events) && edf::ButtonsAt(events,1050)==0,"empty file did not release input");
    const auto release=edf::ButtonTransitions(0x1000,edf::ButtonsAt(events,1050));
    Require(release.size()==1 && release[0].virtual_key==0x5800 && release[0].flags==2,"reload release transition mismatch");
    write("1000 100 0 0 255 0 32767 0 0\n");
    Require(edf::ReloadInputEvents(path,stamp,events) && edf::AnalogAt(events,1050)[1]==255,
      "analog reload missed trigger");
    write("# release analog inputs\n");
    Require(edf::ReloadInputEvents(path,stamp,events) && edf::AnalogAt(events,1050)==edf::ScriptedAnalog{},
      "clearing schedule retained analog state");
    // "clock game": the clock line is not an event; the last valid clock line wins.
    const std::string clock_script="# clock game\nclock game\n100 50 1000\nclock sometimes\nclock wall extra\n";
    std::istringstream clock_events(clock_script),clock_kind(clock_script);
    Require(edf::ParseInputEvents(clock_events).size()==1,"clock line parsed as an event");
    Require(edf::ParseInputClock(clock_kind)==edf::ScriptedClock::Game,"game clock line ignored");
    std::istringstream wall_kind("clock game\nclock wall\n"),legacy_kind("20000 1000 0010\n");
    Require(edf::ParseInputClock(wall_kind)==edf::ScriptedClock::Wall &&
      edf::ParseInputClock(legacy_kind)==edf::ScriptedClock::Wall,"wall clock default or override");
    // 60 ticks per game second, written as milliseconds; saturates instead of wrapping.
    Require(edf::GameTicksToMs(0)==0 && edf::GameTicksToMs(1)==16 && edf::GameTicksToMs(3)==50 &&
      edf::GameTicksToMs(60)==1000 && edf::GameTicksToMs(10800)==180000 &&
      edf::GameTicksToMs(~0ull)==UINT32_MAX,"tick to millisecond conversion");
    edf::ScriptedClockState wall;
    Require(edf::ScriptElapsedMs(wall,1234,99999)==1234,"wall clock read ticks");
    // Ticks count from the first poll, whatever the wall time: a slow renderer
    // that spent 30 s of wall time on 600 ticks is 10 s into the script.
    edf::ScriptedClockState game{edf::ScriptedClock::Game,5000};
    Require(edf::ScriptElapsedMs(game,5,5000)==0 && edf::ScriptElapsedMs(game,30000,5600)==10000 &&
      edf::ScriptElapsedMs(game,60000,5600)==10000 && !game.fell_back,"game clock followed wall time");
    // No tick at all: wait, then fall back to wall time for good.
    edf::ScriptedClockState stalled{edf::ScriptedClock::Game,7};
    Require(edf::ScriptElapsedMs(stalled,edf::kGameClockGraceMs-1,7)==0 && !stalled.fell_back,"early fallback");
    Require(edf::ScriptElapsedMs(stalled,edf::kGameClockGraceMs,7)==edf::kGameClockGraceMs && stalled.fell_back &&
      stalled.clock==edf::ScriptedClock::Wall && edf::ScriptElapsedMs(stalled,20000,900)==20000,"missing fallback");
    // After ticks were seen, a stall holds the script and never falls back.
    edf::ScriptedClockState paused{edf::ScriptedClock::Game,0};
    Require(edf::ScriptElapsedMs(paused,100,6)==100 && edf::ScriptElapsedMs(paused,50000,6)==100 &&
      !paused.fell_back,"stalled game clock fell back");
    std::cout<<"Scripted input file reload, unchanged/missing files, retained schedule, release and game clock passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n'; return 1;}
}
