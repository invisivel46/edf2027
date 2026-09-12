#include "native_graphics/native_pacing.h"
#include <iostream>
#include <limits>

int main() {
  using namespace edf::native;
  using namespace std::chrono;
  int failures = 0;
  auto check = [&](bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; ++failures; }
  };
  NativePacingClock clock;
  for(uint32_t phase=1;phase<=100;++phase) {
    NativeSwapPacingState retail{101,100,0,0},native=retail;
    check(!retail.Complete(1u<<16,phase) && retail.pending==1,
      "retail zero phase threshold did not reproduce extra wait");
    check(native.CompleteNative(1) && native.acknowledged==101 &&
      native.pending==0 && native.callbacks==1,"native late frame deferred another tick");
  }
  for(uint32_t interval:{1u,2u,3u}) {
    NativeSwapPacingState early{100,100,0,0};
    check(!early.CompleteNative(interval) && early.pending==interval,
      "native swap bypassed minimum interval");
    if(interval>1) check(!early.Advance(interval-1),"native swap released early");
    check(early.Advance(1) && early.acknowledged==100+interval,
      "native interval completion changed tick accounting");
  }
  NativeSwapPacingState wrapped{0,0xffffffffu,0,0};
  check(wrapped.CompleteNative(1) && wrapped.acknowledged==0,"native wraparound interval");
  for(uint32_t interval:{0u,4u,0xffffffffu}) {
    NativeSwapPacingState invalid{1,2,3,4};
    try { invalid.CompleteNative(interval); check(false,"invalid interval accepted"); }
    catch(const std::invalid_argument&) {}
    check(invalid.pending==3 && invalid.callbacks==4,"invalid interval mutated state");
  }
  for(uint32_t initial:{0u,3u,0xfffffffdu}) for(uint32_t pending:{0u,1u,3u,0xffffffffu})
    for(uint64_t elapsed:{0ull,1ull,2ull,3ull,8ull}) {
      NativeSwapPacingState bulk{initial,99,pending,7},single=bulk;
      bool expected_ack=false;
      for(uint64_t i=0;i<elapsed;++i) {
        ++single.ticks;
        if(static_cast<int32_t>(single.pending)>0 && --single.pending==0) {
          single.acknowledged=single.ticks;
          expected_ack=true;
        }
      }
      check(bulk.Advance(elapsed)==expected_ack && bulk.ticks==single.ticks &&
        bulk.acknowledged==single.acknowledged && bulk.pending==single.pending && bulk.callbacks==single.callbacks,
        "native swap tick catch-up differs from per-tick CPU bookkeeping");
    }
  NativeSwapPacingState long_pause{0xfffffffeu,0,3,0};
  check(long_pause.Advance(0x100000005ull) && long_pause.ticks==3 && long_pause.acknowledged==1 && !long_pause.pending,
    "long swap pause lost acknowledgement tick or counter wrap");
  for(uint32_t phase:{0u,101u}) {
    NativeSwapPacingState invalid{1,2,3,4};
    try { invalid.Complete(0,phase); check(false,"invalid native phase accepted"); }
    catch(const std::invalid_argument&) {}
    check(invalid.ticks==1 && invalid.acknowledged==2 && invalid.pending==3 && invalid.callbacks==4,
      "invalid native phase mutated bookkeeping");
  }
  const NativePacingClock::Clock::time_point epoch{};
  try { (void)clock.Sample(epoch); check(false,"uninitialized clock accepted"); }
  catch (const std::logic_error&) {}
  clock.Reset(epoch, 0);
  // A 20 ms renderer should run at 50 FPS with 60 elapsed simulation steps,
  // not be rounded down to 30 by an emulated scanout-phase deadline.
  NativeSwapPacingState continuous{};
  uint64_t continuous_previous=0,continuous_steps=0;
  for(uint32_t frame=1;frame<=50;++frame) {
    const auto tick=clock.Sample(epoch+milliseconds(frame*20));
    continuous.Advance(tick-continuous_previous);
    check(continuous.CompleteNative(1),"20 ms native frame received artificial vblank delay");
    continuous_steps+=NativePacingResult(NativePacingSteps(tick,continuous_previous,1));
    continuous_previous=tick;
  }
  check(continuous_steps==60 && continuous.callbacks==50,
    "native deadline change altered elapsed simulation time");
  check(clock.Sample(epoch)==0,"epoch");
  check(clock.Sample(epoch+nanoseconds(16666666))==0,"first tick premature");
  check(clock.Sample(epoch+nanoseconds(16666667))==1,"first tick late");
  check(clock.Sample(epoch+nanoseconds(33333333))==1,"second tick premature");
  check(clock.Sample(epoch+nanoseconds(33333334))==2,"second tick late");
  check(clock.Sample(epoch+seconds(1))==60,"one second drift");
  check(clock.Sample(epoch+hours(24))==5184000,"daily drift");
  check(clock.PhasePercent(epoch)==1,"phase origin");
  check(clock.PhasePercent(epoch+nanoseconds(16666666))==100,"phase before tick");
  check(clock.PhasePercent(epoch+nanoseconds(16666667))==1,"phase after tick");
  check(clock.PhasePercent(epoch+seconds(1))==1,"phase second boundary");
  for(uint64_t ns=0;ns<1000000000;ns+=7919) {
    const auto now=epoch+nanoseconds(ns);
    check(clock.Sample(now)==ns*60/1000000000 &&
      clock.PhasePercent(now)==(ns*60%1000000000)/10000000+1,"phase and tick disagree");
  }
  try { (void)clock.Sample(epoch-nanoseconds(1)); check(false,"backward clock accepted"); }
  catch (const std::logic_error&) {}
  clock.Reset(epoch+seconds(10), 99);
  check(clock.Sample(epoch+seconds(11))==159,"reset baseline");
  clock.Reset(epoch, UINT64_MAX);
  check(clock.Sample(epoch+nanoseconds(16666667))==0,"counter wrap");
  check(NativePacingSteps(0,UINT64_MAX,1)==1,"wrapped delta");
  check(NativePacingSteps(5,0,2)==2,"divisor");
  for (auto divisor : {0u,0x80000000u,0xffffffffu}) {
    try { (void)NativePacingSteps(1,0,divisor); check(false,"invalid divisor accepted"); }
    catch (const std::invalid_argument&) {}
  }
  check(RetailPacingPending(0,0),"retail zero must wait");
  check(RetailPacingPending(1,1),"retail one waits for next quotient");
  check(!RetailPacingPending(0,1),"retail first increment releases");
  check(!RetailPacingPending(1,2),"retail second increment releases");
  check(!RetailPacingPending(2,2),"retail catch-up does not wait");
  check(NativePacingPending(0),"native zero must wait");
  check(!NativePacingPending(1),"native elapsed tick must not wait a second time");
  check(!NativePacingPending(2),"native catch-up must not wait");
  // A presentation loop returning one tick later must produce 60 frames and
  // 60 simulation steps per second, not 30 double-step frames or 120 steps.
  uint64_t native_frames=0,native_steps=0,previous=0;
  for(uint64_t tick=1;tick<=60;++tick) {
    const auto elapsed=NativePacingSteps(tick,previous,1);
    if(!NativePacingPending(elapsed)) {
      ++native_frames; native_steps+=NativePacingResult(elapsed); previous=tick;
    }
  }
  check(native_frames==60 && native_steps==60,"native pacing changes simulation speed or halves frame rate");
  // A slow renderer still accounts for the elapsed ticks rather than pretending
  // that fewer frames imply a slower simulation.
  native_frames=0; native_steps=0; previous=0;
  for(uint64_t tick=2;tick<=60;tick+=2) {
    const auto elapsed=NativePacingSteps(tick,previous,1);
    check(!NativePacingPending(elapsed),"slow native frame adds another wait");
    ++native_frames; native_steps+=NativePacingResult(elapsed); previous=tick;
  }
  check(native_frames==30 && native_steps==60,"slow renderer lost elapsed simulation steps");
  for (uint64_t n=0;n!=8;++n)
    check(NativePacingResult(n)==(n>4?1:n),"retail catch-up clamp");
  std::cout << "Native pacing failures: " << failures << '\n';
  return failures ? 1 : 0;
}
