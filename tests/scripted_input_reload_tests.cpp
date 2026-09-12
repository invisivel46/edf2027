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
    std::cout<<"Scripted input file reload, unchanged/missing files, retained schedule and release passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n'; return 1;}
}
