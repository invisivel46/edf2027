#include "native_graphics/native_pacing.h"
#include "native_graphics/native_camera_history.h"
#include "native_graphics/native_model_pose_history.h"
#include <cmath>
#include <cstring>
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
  {
    NativeModelPoseHistory poses;
    std::vector<NativePoseMatrix> input{{2,0,0,0, 0,3,0,0, 0,0,4,0, 0,0,0,1}},output;
    poses.Sample(input,100,0,output);
    auto next=input;
    next[0][0]=4; next[0][5]=5; next[0][10]=6; next[0][12]=10;
    poses.Sample(next,101,.5f,output);
    check(output[0][0]==3 && output[0][5]==4 && output[0][10]==5 && output[0][12]==5,
      "scaled bone midpoint lost scale or translation");
    check(input[0][0]==2 && next[0][12]==10,"bone history modified source poses");
    poses.Reset(); poses.Sample(next,102,.1f,output);
    check(output==next,"retired bone source retained previous model history");
    next[0][1]=1; poses.Sample(next,103,.5f,output);
    check(output==next,"unsupported sheared bone was distorted");
    next.resize(2,input[0]); poses.Sample(next,104,.5f,output);
    check(output==next,"changed skeleton size retained old bone state");
    poses.Reset(); poses.Sample(input,200,0,output);
    auto view_dependent=input; view_dependent[0][12]=1;
    poses.Sample(view_dependent,200,.5f,output);
    check(poses.render_dependent() && output==view_dependent,"render-dependent pose was interpolated twice");
    view_dependent[0][12]=2; poses.Sample(view_dependent,201,.2f,output);
    check(output==view_dependent,"render-dependent pose resumed stale fixed-step blending");
  }
  // The stateless per-bone blend the full-frame models pass uses is what
  // Sample outputs, bit for bit: over consecutive ticks, several renders per
  // tick, for a scaled rotating bone, a stationary bone, a sheared bone, a cut
  // (a teleport) and a bone that becomes degenerate.
  {
    const auto bone=[](float angle,float scale,float x) {
      const float c=std::cos(angle),s=std::sin(angle);
      return NativePoseMatrix{c*scale,s*scale,0,0, -s*scale*1.5f,c*scale*1.5f,0,0, 0,0,scale*.75f,0, x,2*x,-x,1};
    };
    const auto frame=[&](uint64_t tick) {
      const float t=float(tick);
      std::vector<NativePoseMatrix> pose{bone(.3f*t,1+.1f*t,t),bone(1,2,3),bone(.2f*t,1,t),bone(.7f*t,.5f,tick>=13?500.f+t:t),
        bone(.1f*t,tick==12?0.f:1.f,t)};
      pose[2][1]+=.5f;  // Sheared: never blended.
      return pose;
    };
    NativeModelPoseHistory history;
    std::vector<NativePoseMatrix> output,previous=frame(10);
    history.Sample(previous,10,.5f,output);
    bool identical=true,blended=false,stationary=true,sheared=true;
    for(uint64_t tick=11;tick<16;++tick) {
      const auto current=frame(tick);
      for(const float fraction:{0.f,.125f,.5f,.8f,1.f}) {
        history.Sample(current,tick,fraction,output);
        for(size_t index=0;index<current.size();++index) {
          const auto expected=BlendNativePoseMatrix(previous[index],current[index],fraction);
          identical&=std::memcmp(expected.data(),output[index].data(),sizeof(expected))==0;
          blended|=fraction>0 && fraction<1 && expected!=current[index];
        }
        stationary&=output[1]==current[1];
        sheared&=output[2]==current[2];
      }
      previous=current;
    }
    check(identical,"the stateless pose blend differs from NativeModelPoseHistory::Sample");
    check(blended && stationary && sheared,"the blend comparison exercised no blend, or blended a stationary or sheared bone");
    const auto a=bone(.1f,1,0),b=bone(.4f,2,10);
    check(BlendNativePoseMatrix(a,b,0)==a && BlendNativePoseMatrix(a,b,-1)==a,"alpha 0 is not the previous matrix");
    check(BlendNativePoseMatrix(a,b,1)==b && BlendNativePoseMatrix(a,b,std::numeric_limits<float>::quiet_NaN())==b,
      "alpha 1 or NaN is not the current matrix");
    check(BlendNativePoseMatrix(b,b,.5f)==b,"a stationary bone is not the current matrix");
    const auto half=BlendNativePoseMatrix(a,b,.5f);
    check(std::abs(half[12]-5)<1e-5f && std::abs(half[13]-10)<1e-5f,"blended bone translation is not the midpoint");
  }
  {
    NativeCameraHistory history;
    NativeCameraPose origin;
    origin.world={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    origin.fov=1;
    origin.viewport={0,0,1280,720};
    check(history.Sample(origin,10,.5f)==origin,"first camera sample blended with uninitialized history");
    auto next=origin;
    const float sine=std::sqrt(3.f)/2;
    next.world={.5f,0,-sine,0, 0,1,0,0, sine,0,.5f,0, 10,0,0,1};
    next.fov=1.1f;
    const auto half=history.Sample(next,11,.5f);
    check(std::abs(half.world[0]-sine)<.00001f && std::abs(half.world[2]+.5f)<.00001f &&
      std::abs(half.world[12]-5)<.00001f && std::abs(half.fov-1.05f)<.00001f,
      "camera midpoint is not a rigid halfway pose");
    check(history.Sample(next,11,1)==next,"camera interpolation endpoint mismatch");
    check(history.Sample(next,13,.1f)==next,"camera blended across missed ticks");
    auto cut=next; cut.world[12]=1000;
    check(history.Sample(cut,14,.5f)==cut,"camera teleports were interpolated");
    cut.viewport[2]=640;
    check(history.Sample(cut,15,.5f)==cut,"viewport switch retained camera history");
    cut.world[12]=1001;
    check(history.Sample(cut,15,.5f)==cut,"same-tick camera replacement retained history");
    auto invalid=cut; invalid.world[0]=2;
    check(history.Sample(invalid,16,.5f)==invalid,"non-rigid camera was modified");
    check(history.Sample(origin,17,.5f)==origin,"invalid camera failed to reset history");
    // Exercise quaternion extraction around 180 degrees on every major axis,
    // including the shortest path across the equivalent +/- quaternion signs.
    for(size_t axis=0;axis<3;++axis) {
      auto rotated=[&](float angle) {
        auto pose=origin;
        const auto j=(axis+1)%3,k=(axis+2)%3;
        pose.world[j*4+j]=pose.world[k*4+k]=std::cos(angle);
        pose.world[j*4+k]=std::sin(angle);
        pose.world[k*4+j]=-std::sin(angle);
        return pose;
      };
      history.Reset();
      constexpr float pi=3.14159265358979323846f;
      history.Sample(rotated(pi*.95f),20,0);
      const auto midpoint=history.Sample(rotated(-pi*.95f),21,.5f);
      const auto expected=rotated(pi);
      for(size_t i=0;i<16;++i)
        check(std::abs(midpoint.world[i]-expected.world[i])<.00001f,"camera rotation took long arc at 180 degrees");
    }
    NativePacingClock fractional;
    const auto epoch=NativePacingClock::Clock::time_point{};
    fractional.Reset(epoch,123);
    check(fractional.Fraction(epoch)==0 && std::abs(fractional.Fraction(epoch+milliseconds(25))-.5f)<.000001f &&
      fractional.Sample(epoch+milliseconds(25))==124,"fractional render phase disagrees with simulation ticks");
    // The console's timescale: a rate change rebases, keeping the tick and its fraction.
    NativePacingClock scaled;
    scaled.Reset(epoch,1000);
    scaled.SetRate(epoch+milliseconds(1025),30.0);  // 61.5 ticks elapsed at 60 Hz
    check(scaled.rate()==30.0 && scaled.Sample(epoch+milliseconds(1025))==1061 &&
      std::abs(scaled.Fraction(epoch+milliseconds(1025))-.5f)<.001f,"rate change moved the tick or its fraction");
    check(scaled.Sample(epoch+milliseconds(1025+1000))==1091,"half rate did not run 30 ticks a second");
    scaled.SetRate(epoch+milliseconds(2025),60.0);
    check(scaled.Sample(epoch+milliseconds(3025))==1151,"returning to 60 Hz did not run 60 ticks a second");
    NativePacingClock unset;  // a rate set before the first Reset applies from it
    unset.SetRate(epoch,120.0);
    unset.Reset(epoch,0);
    check(unset.Sample(epoch+seconds(1))==120,"a rate set before Reset was lost");
  }
  for(uint32_t interval:{1u,2u,3u}) {
    NativeSwapPacingState unpaced{100,99,3,7};
    for(unsigned frame=0;frame<4;++frame)
      check(unpaced.CompleteUnpaced(interval) && unpaced.ticks==100 &&
        unpaced.acknowledged==100 && unpaced.pending==0 && unpaced.callbacks==8+frame,
        "unpaced acceptance changed simulation ticks or waited for refresh");
    check(!unpaced.CompleteNative(interval) && unpaced.pending==interval,
      "returning to paced rendering did not restore interval wait");
  }
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
  {
    // A 30 FPS movie draws every other paced swap; a 15 FPS one every fourth.
    NativeMoviePacing movie; uint64_t draws=0; bool active=false, stayed=true;
    for(int swap=0;swap!=240;++swap) {
      if(swap%2==0) { ++draws; active=true; }
      active=movie.Swap(draws,active); stayed=stayed && active;
    }
    check(stayed,"30 FPS movie flickered out of pacing");
    for(int swap=0;swap!=240;++swap) {
      if(swap%4==0) { ++draws; active=true; }
      active=movie.Swap(draws,active); stayed=stayed && active;
    }
    check(stayed,"15 FPS movie flickered out of pacing");
    int released=0; ++draws; active=movie.Swap(draws,true);
    while(active && released<100) { active=movie.Swap(draws,active); ++released; }
    check(!active && released==int(NativeMoviePacing::kIdleSwaps),"ended movie did not release after the idle swaps");
    check(!movie.Swap(draws,false) && !movie.Swap(draws,false),"released movie pacing re-armed without a draw");
    ++draws; check(movie.Swap(draws,true),"new movie draw did not re-arm pacing");
    // A completed 3D frame clears the flag even while movie draws continue.
    ++draws; check(!movie.Swap(draws,false),"movie pacing overrode the 3D-frame clear");
  }
  std::cout << "Native pacing failures: " << failures << '\n';
  return failures ? 1 : 0;
}
