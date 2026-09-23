#include "native_graphics/native_full_frame.h"
#include "native_graphics/native_ab_alternate.h"
#include <array>
#include <atomic>
#include <bit>
#include <functional>
#include <thread>
#include <map>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace edf::native;
// Records every host call in order; views, view acceptance and the finish
// result are scripted.
class TraceHost final : public NativeFrameHost {
 public:
  std::vector<std::string> trace;
  std::vector<uint32_t> views;
  std::vector<bool> accept;
  uint32_t serial=40;
  bool finish=true,output_ready=false;
  NativeFrameInputs AcquireInputs() override { trace.push_back("acquire"); return {nullptr,nullptr,nullptr,7}; }
  std::vector<uint32_t> Views() override { trace.push_back("views"); return views; }
  uint32_t AdvanceSerial(uint32_t view) override { trace.push_back("serial:"+std::to_string(view)); return serial++; }
  bool BeginView(NativeFrameContext& context) override {
    trace.push_back("begin:"+std::to_string(context.view.index)+":"+std::to_string(context.view.scene)+":"+std::to_string(context.view.serial));
    return context.view.index<accept.size()?bool(accept[context.view.index]):true;
  }
  void RunPass(size_t index,NativeFramePass& pass,NativeFrameContext& context) override {
    trace.push_back(std::to_string(index)+":"+pass.name());
    NativeFrameHost::RunPass(index,pass,context);
  }
  void ViewOverlays(NativeFrameContext& context) override { trace.push_back("overlays:"+std::to_string(context.view.scene)); }
  void EndView(NativeFrameContext&) override { trace.push_back("end_view"); }
  void SideEffects(const NativeFrameInputs& inputs) override { trace.push_back("side_effects:"+std::to_string(inputs.motion_publication)); }
  bool Finish(NativeFrameContext& context) override { trace.push_back("finish:"+std::to_string(context.view.scene)); return finish; }
  void Phases(NativeFrameContext&) override { trace.push_back("phases"); }
  void EndScene(const NativeFrameInputs&,bool ready) override { output_ready=ready; trace.push_back("end_scene"); }
  void Unimplemented(const char* pass) override { trace.push_back(std::string("stub:")+pass); }
};
class NamedPass final : public NativeFramePass {
 public:
  explicit NamedPass(const char* name):name_(name) {}
  const char* name() const override { return name_; }
  void Record(NativeFrameContext& context) override { context.host.Unimplemented("replaced"); }
 private:
  const char* name_;
};
std::vector<std::string> Tail(bool with_finish=true) {
  std::vector<std::string> tail{"side_effects:7"};
  if(with_finish) { tail.push_back("5:post"); tail.push_back("finish:0"); }
  tail.push_back("phases"); tail.push_back("end_scene");
  return tail;
}
}

int main() {
  int failures=0;
  auto check=[&](bool ok,const char* what) { if(!ok) { ++failures; std::cerr<<"failed: "<<what<<'\n'; } };
  const std::vector<std::string> names{"sky","static_world","models","effects","transparent","post"};
  // Default passes: the documented order; view passes are stubs, post finishes.
  {
    NativeFullFrame frame;
    check(frame.view_passes().size()==kNativeFrameViewPassCount && frame.frame_passes().size()==1,"default pass counts");
    for(size_t i=0;i<names.size();++i) {
      check(kNativeFramePassOrder[i]==names[i],"pass order constant");
      const auto* pass=i<kNativeFrameViewPassCount?frame.view_passes()[i].get():frame.frame_passes()[i-kNativeFrameViewPassCount].get();
      check(pass->name()==names[i],"default pass order");
    }
    TraceHost host; host.views={100};
    frame.Run(host);
    std::vector<std::string> expected{"acquire","views","serial:100","begin:0:100:40"};
    for(size_t i=0;i<kNativeFrameViewPassCount;++i) { expected.push_back(std::to_string(i)+":"+names[i]); expected.push_back("stub:"+names[i]); }
    expected.push_back("overlays:100"); expected.push_back("end_view");
    for(const auto& step:Tail()) expected.push_back(step);
    check(host.trace==expected,"one view frame call order");
    check(host.serial==41 && host.output_ready && frame.frames()==1,"serial once per view; finish output reaches end_scene");
  }
  // A declined view (no scene) skips only its native passes: the guest view
  // listeners still run, then finish and phases as usual.
  {
    NativeFullFrame frame({},MakeNativeFrameFinishPasses());
    TraceHost host; host.views={1,2}; host.accept={false,true}; host.finish=false;
    frame.Run(host);
    std::vector<std::string> expected{"acquire","views",
      "serial:1","begin:0:1:40","overlays:1","end_view",
      "serial:2","begin:1:2:41","overlays:2","end_view",
      "side_effects:7","0:post","finish:0","phases","end_scene"};
    check(host.trace==expected,"declined view order");
    check(!host.output_ready,"failed finish reports no output");
  }
  // Owner flags 2261/2262 (no views): the finish stage and phase loop still run.
  {
    NativeFullFrame frame;
    TraceHost host;
    frame.Run(host);
    std::vector<std::string> expected{"acquire","views"};
    for(const auto& step:Tail()) expected.push_back(step);
    check(host.trace==expected,"empty view list still finishes");
    check(host.serial==40,"no serial without views");
  }
  // Replace swaps a pass by name, in either list, keeping its position.
  {
    NativeFullFrame frame;
    check(frame.Replace(std::make_unique<NamedPass>("static_world")),"replace view pass");
    check(frame.Replace(std::make_unique<NamedPass>("post")),"replace frame pass");
    check(!frame.Replace(std::make_unique<NamedPass>("shadow")),"no such pass");
    TraceHost host; host.views={9};
    frame.Run(host);
    check(host.trace[6]=="1:static_world" && host.trace[7]=="stub:replaced","replaced view pass keeps its position");
    check(host.trace[host.trace.size()-3]=="stub:replaced" && !host.output_ready,"replaced post runs in the frame");
  }
  // Motion vectors: MotionVectors runs for accepted views only, after their
  // overlays and before EndView, with the frame's index; the frame context
  // (post, phases) gets the last accepted view's motion that has a texture.
  {
    class MotionVectorHost final : public NativeFrameHost {
     public:
      std::vector<std::string> trace;
      std::vector<uint64_t> frames;
      NativeBackendTexture* texture=reinterpret_cast<NativeBackendTexture*>(uintptr_t(0x1000));
      bool finish_saw=false,phases_saw=false;
      NativeFrameInputs AcquireInputs() override { return {}; }
      std::vector<uint32_t> Views() override { return {1,2,3}; }
      uint32_t AdvanceSerial(uint32_t) override { return 0; }
      bool BeginView(NativeFrameContext& context) override { return context.view.scene!=2; }
      void ViewOverlays(NativeFrameContext& context) override { trace.push_back("overlays:"+std::to_string(context.view.scene)); }
      void MotionVectors(NativeFrameContext& context) override {
        trace.push_back("motion:"+std::to_string(context.view.scene));
        frames.push_back(context.inputs.frame);
        if(context.view.scene==1) { context.motion.motion=texture; context.motion.reset=false; context.motion.fov_y=1; }
      }
      void EndView(NativeFrameContext& context) override { trace.push_back("end_view:"+std::to_string(context.view.scene)); }
      bool Finish(NativeFrameContext& context) override {
        finish_saw=context.motion.motion==texture && !context.motion.reset && context.motion.fov_y==1; return true;
      }
      void Phases(NativeFrameContext& context) override { phases_saw=context.motion.motion==texture; }
      void EndScene(const NativeFrameInputs&,bool) override {}
    } host;
    NativeFullFrame frame;
    frame.Run(host);
    frame.Run(host);
    const std::vector<std::string> one{"overlays:1","motion:1","end_view:1","overlays:2","end_view:2","overlays:3","motion:3","end_view:3"};
    std::vector<std::string> expected=one; expected.insert(expected.end(),one.begin(),one.end());
    check(host.trace==expected,"motion vectors run after the overlays of accepted views only");
    check(host.frames==std::vector<uint64_t>({1,1,2,2}),"the frame index counts frames");
    check(host.finish_saw && host.phases_saw,"post and phases get the view's motion vectors");
  }
  // The motion budget: AcquireInputs' budget reaches every pass and the frame
  // context unchanged; poses interpolate only unlocked, at divisor 1, with the
  // model interpolation setting (the guest 821C9C20 hook's condition).
  {
    class MotionHost final : public NativeFrameHost {
     public:
      NativeFrameMotion acquired;
      std::vector<NativeFrameMotion> seen;
      NativeFrameInputs AcquireInputs() override { NativeFrameInputs inputs; inputs.motion=acquired; return inputs; }
      std::vector<uint32_t> Views() override { return {1,2}; }
      uint32_t AdvanceSerial(uint32_t) override { return 0; }
      bool BeginView(NativeFrameContext&) override { return true; }
      void RunPass(size_t,NativeFramePass&,NativeFrameContext& context) override { seen.push_back(context.inputs.motion); }
      void SideEffects(const NativeFrameInputs& inputs) override { seen.push_back(inputs.motion); }
      void EndScene(const NativeFrameInputs& inputs,bool) override { seen.push_back(inputs.motion); }
    } host;
    host.acquired=MakeNativeFrameMotion(true,1,1234,.375f,2,true);
    check(host.acquired.tick==1234 && host.acquired.fraction==.375f && host.acquired.steps==2 && host.acquired.interpolate,
      "the frame motion carries the budget");
    NativeFullFrame frame;
    frame.Run(host);
    check(host.seen.size()==2*kNativeFrameViewPassCount+3,"every pass saw the inputs");
    for(const auto& motion:host.seen) check(motion==host.acquired,"a pass saw another motion budget than AcquireInputs'");
    check(!MakeNativeFrameMotion(false,1,5,.5f,1,true).interpolate,"locked frames do not interpolate models");
    check(!MakeNativeFrameMotion(true,2,5,.5f,1,true).interpolate,"divisor 2 does not interpolate models");
    check(!MakeNativeFrameMotion(true,1,5,.5f,1,false).interpolate,"edf_native_model_interpolation=false does not interpolate");
    check(!NativeFrameInputs{}.motion.interpolate,"default inputs do not interpolate");
    check(host.acquired.unlocked && !MakeNativeFrameMotion(false,1,5,.5f,1,true).unlocked,"the frame motion carries the loop mode");
  }
  // Per-tick state (NativeTickGate -> NativeFrameInputs::tick_frame): Run
  // decides once per frame, before any view, and every view, pass and the
  // finish/phases see that one answer. Locked frames always advance; unlocked
  // at two renders per tick, only the first render after a step.
  {
    class TickHost final : public NativeFrameHost {
     public:
      NativeFrameMotion acquired;
      std::vector<bool> seen;
      NativeFrameInputs AcquireInputs() override { NativeFrameInputs inputs; inputs.motion=acquired; inputs.tick_frame=false; return inputs; }
      std::vector<uint32_t> Views() override { return {1,2}; }
      uint32_t AdvanceSerial(uint32_t) override { return 0; }
      bool BeginView(NativeFrameContext&) override { return true; }
      void RunPass(size_t,NativeFramePass&,NativeFrameContext& context) override { seen.push_back(context.inputs.tick_frame); }
      bool Finish(NativeFrameContext& context) override { seen.push_back(context.inputs.tick_frame); return false; }
      void Phases(NativeFrameContext& context) override { seen.push_back(context.inputs.tick_frame); }
      void EndScene(const NativeFrameInputs& inputs,bool) override { seen.push_back(inputs.tick_frame); }
    };
    const auto run=[&](NativeFullFrame& frame,const NativeFrameMotion& motion) {
      TickHost host; host.acquired=motion;
      frame.Run(host);
      check(!host.seen.empty(),"the tick host saw the frame");
      for(const bool value:host.seen) check(value==host.seen.front(),"one tick_frame answer per frame");
      return host.seen.front();
    };
    NativeFullFrame locked;
    for(uint64_t tick=1;tick<=3;++tick)
      for(const uint32_t steps:{1u,0u,2u}) check(run(locked,MakeNativeFrameMotion(false,1,tick,1,steps,true)),"locked frames always advance");
    check(locked.held_frames()==0,"locked holds nothing");
    NativeFullFrame unlocked;
    uint32_t advanced=0;
    for(uint64_t tick=1;tick<=60;++tick)
      for(uint32_t render=0;render<2;++render) {
        const bool tick_frame=run(unlocked,MakeNativeFrameMotion(true,1,tick,render?.5f:0.f,render?0u:1u,true));
        check(tick_frame==(render==0),"unlocked: the first render of a tick advances, the render-only one holds");
        advanced+=tick_frame;
      }
    check(advanced==60 && unlocked.frames()==120 && unlocked.held_frames()==60,"120 renders over 60 ticks advance 60 times");
    // The gate alone: a two-step render counts once, a repeated tick never.
    NativeTickGate gate;
    check(!gate.Advance(MakeNativeFrameMotion(true,1,7,.2f,0,true)) && !gate.committed(),"a render-only frame before any step holds");
    check(gate.Advance(MakeNativeFrameMotion(true,1,9,0,2,true)) && gate.tick()==9,"a catch-up render advances once");
    check(!gate.Advance(MakeNativeFrameMotion(true,1,9,0,2,true)),"the same tick never advances twice");
    check(gate.Advance(MakeNativeFrameMotion(false,1,9,0,0,true)),"locked always advances, even at the same tick");
    check(NativeFrameInputs{}.tick_frame,"default inputs advance");
  }
  // Draw-counted guest steps (NativeRenderStepOncePerTick, the clEffectEtc02,
  // clGaugeRader and cursor-fade hooks): modelled on their stores, locked
  // renders step as before, and unlocked at two renders per tick the fields end
  // each tick bit-identical to the locked run.
  {
    struct Memory {
      mutable std::map<uint32_t,uint32_t> words;
      // Runs before each compare-exchange: a simulation write landing between
      // the draw and the put-back.
      mutable std::function<void()> before_exchange;
      uint32_t Word(uint32_t at) const { return words[at]; }
      void StoreWord(uint32_t at,uint32_t value) const { words[at]=value; }
      bool CompareExchangeWord(uint32_t at,uint32_t expected,uint32_t desired) const {
        if(before_exchange) std::exchange(before_exchange,nullptr)();
        if(words[at]!=expected) return false;
        words[at]=desired; return true;
      }
    };
    constexpr uint32_t gauge=0x40001000,cursor=0x40002000;
    constexpr float decay=-0.99f;
    // clGaugeRader::slot3 821768A0..B0: when +296 != 0, +300 *= -0.99 and +296 -= 1.
    const auto radar=[&](const Memory& m) {
      const auto frames=m.Word(gauge+kNativeRadarShakeFrames);
      if(!frames) return;
      m.StoreWord(gauge+kNativeRadarShakeOffset,std::bit_cast<uint32_t>(std::bit_cast<float>(m.Word(gauge+kNativeRadarShakeOffset))*decay));
      m.StoreWord(gauge+kNativeRadarShakeFrames,frames-1);
    };
    const auto cursor_fade=[&](const Memory& m) { m.StoreWord(cursor+kNativeCursorFade,m.Word(cursor+kNativeCursorFade)+1); };
    const auto render=[&](const Memory& m,bool tick_frame) {
      NativeRenderStepOncePerTick(m,tick_frame,gauge+kNativeRadarShakeFrames,0xFFFFFFFFu,
        std::array{uint32_t(gauge+kNativeRadarShakeOffset)},[&] { radar(m); });
      NativeRenderStepOncePerTick(m,tick_frame,cursor+kNativeCursorFade,1u,std::array<uint32_t,0>{},[&] { cursor_fade(m); });
    };
    const auto arm=[&](const Memory& m) {  // clGaugeRader slot2 82175FFC: 30 frames, 20.0
      m.StoreWord(gauge+kNativeRadarShakeFrames,30); m.StoreWord(gauge+kNativeRadarShakeOffset,std::bit_cast<uint32_t>(20.f));
      m.StoreWord(cursor+kNativeCursorFade,0);
    };
    Memory locked,unlocked,ungated;
    arm(locked); arm(unlocked); arm(ungated);
    NativeTickGate locked_gate,unlocked_gate;
    for(uint64_t tick=1;tick<=40;++tick) {
      const bool locked_frame=locked_gate.Advance(MakeNativeFrameMotion(false,1,tick,1,1,true));
      render(locked,locked_frame);
      for(uint32_t r=0;r<2;++r) {
        const bool tick_frame=unlocked_gate.Advance(MakeNativeFrameMotion(true,1,tick,r?.5f:0.f,r?0u:1u,true));
        render(unlocked,tick_frame);
        render(ungated,true);
      }
      check(locked.words==unlocked.words,"unlocked fields match the locked run after every tick");
    }
    check(locked.Word(gauge+kNativeRadarShakeFrames)==0 && locked.Word(cursor+kNativeCursorFade)==40,"locked: one step per render");
    check(ungated.Word(cursor+kNativeCursorFade)==80,"without the gate the unlocked fade would count 80");
    // A held render never undoes another writer: a cursor move resetting the
    // fade inside the call (8218F138) stays reset.
    Memory moved; moved.StoreWord(cursor+kNativeCursorFade,7);
    check(!NativeRenderStepOncePerTick(moved,false,cursor+kNativeCursorFade,1u,std::array<uint32_t,0>{},
      [&] { moved.StoreWord(cursor+kNativeCursorFade,0); }) && moved.Word(cursor+kNativeCursorFade)==0,"another writer is kept");
    // An idle shake (+296 == 0) is not touched on a held render.
    Memory idle; idle.StoreWord(gauge+kNativeRadarShakeOffset,123);
    check(!NativeRenderStepOncePerTick(idle,false,gauge+kNativeRadarShakeFrames,0xFFFFFFFFu,
      std::array{uint32_t(gauge+kNativeRadarShakeOffset)},[&] { radar(idle); }) && idle.Word(gauge+kNativeRadarShakeOffset)==123,"idle shake");
    // A simulation write after the draw returned and before the put-back (the
    // arming 82175FFC on the tick thread): the counter's compare-exchange
    // sees it and leaves both fields as the writer stored them.
    Memory armed; armed.StoreWord(gauge+kNativeRadarShakeFrames,5);
    armed.StoreWord(gauge+kNativeRadarShakeOffset,std::bit_cast<uint32_t>(4.f));
    armed.before_exchange=[&] { arm(armed); };
    check(!NativeRenderStepOncePerTick(armed,false,gauge+kNativeRadarShakeFrames,0xFFFFFFFFu,
      std::array{uint32_t(gauge+kNativeRadarShakeOffset)},[&] { radar(armed); }) &&
      armed.Word(gauge+kNativeRadarShakeFrames)==30 &&
      armed.Word(gauge+kNativeRadarShakeOffset)==std::bit_cast<uint32_t>(20.f),"a write before the put-back is kept");
    // The same for the cursor fade reset (8218F138) after the draw's step.
    Memory reset; reset.StoreWord(cursor+kNativeCursorFade,7);
    reset.before_exchange=[&] { reset.StoreWord(cursor+kNativeCursorFade,0); };
    check(!NativeRenderStepOncePerTick(reset,false,cursor+kNativeCursorFade,1u,std::array<uint32_t,0>{},
      [&] { cursor_fade(reset); }) && reset.Word(cursor+kNativeCursorFade)==0,"a reset before the put-back is kept");
    // A write to an `also` word after the counter was put back: the counter
    // is restored, the other word keeps the writer's value.
    Memory offset; offset.StoreWord(gauge+kNativeRadarShakeFrames,5);
    offset.StoreWord(gauge+kNativeRadarShakeOffset,std::bit_cast<uint32_t>(4.f));
    bool first_exchange=true;
    std::function<void()> second=[&] { offset.StoreWord(gauge+kNativeRadarShakeOffset,std::bit_cast<uint32_t>(9.f)); };
    offset.before_exchange=[&] { first_exchange=false; offset.before_exchange=second; };
    check(NativeRenderStepOncePerTick(offset,false,gauge+kNativeRadarShakeFrames,0xFFFFFFFFu,
      std::array{uint32_t(gauge+kNativeRadarShakeOffset)},[&] { radar(offset); }) && !first_exchange &&
      offset.Word(gauge+kNativeRadarShakeFrames)==5 &&
      offset.Word(gauge+kNativeRadarShakeOffset)==std::bit_cast<uint32_t>(9.f),"a write between the put-backs is kept");
    // Without a writer the put-back is exact.
    Memory quiet; quiet.StoreWord(gauge+kNativeRadarShakeFrames,5);
    quiet.StoreWord(gauge+kNativeRadarShakeOffset,std::bit_cast<uint32_t>(4.f));
    check(NativeRenderStepOncePerTick(quiet,false,gauge+kNativeRadarShakeFrames,0xFFFFFFFFu,
      std::array{uint32_t(gauge+kNativeRadarShakeOffset)},[&] { radar(quiet); }) &&
      quiet.Word(gauge+kNativeRadarShakeFrames)==5 &&
      quiet.Word(gauge+kNativeRadarShakeOffset)==std::bit_cast<uint32_t>(4.f),"a held render puts both back");
    // Real threads: held renders (each an atomic step and its put-back) race a
    // simulation thread that stores fresh values. Held renders are net zero,
    // so the counter must end at the simulation's last write; a plain-store
    // put-back loses it whenever the write falls between its check and store.
    {
      struct Atomic {
        mutable std::array<std::atomic<uint32_t>,2> words{};
        uint32_t Word(uint32_t at) const { return words[at].load(); }
        bool CompareExchangeWord(uint32_t at,uint32_t expected,uint32_t desired) const {
          return words[at].compare_exchange_strong(expected,desired);
        }
      } shared;
      std::atomic<bool> stop=false,pause=false,paused=false;
      std::thread renders([&] {
        while(!stop.load()) {
          if(pause.load()) { paused.store(true); while(pause.load() && !stop.load()) std::this_thread::yield(); paused.store(false); continue; }
          NativeRenderStepOncePerTick(shared,false,0,1u,std::array{uint32_t(1)},[&] {
            shared.words[0].fetch_add(1); shared.words[1].fetch_add(3);
          });
        }
      });
      uint32_t lost=0;
      for(uint32_t write=1;write<=5000;++write) {
        const uint32_t value=0x10000000u+write*16;
        shared.words[1].store(value); shared.words[0].store(value);
        for(int spin=0;spin<(write&15);++spin) std::this_thread::yield();
        // Quiesce the renders, then read: net zero since the write, but for a
        // draw that stepped the new value itself before its put-back failed.
        pause.store(true);
        while(!paused.load()) std::this_thread::yield();
        const auto counter=shared.words[0].load();
        lost+=counter!=value && counter!=value+1;
        pause.store(false);
        while(paused.load()) std::this_thread::yield();
      }
      stop=true; renders.join();
      if(lost) std::cerr<<"concurrent simulation writes undone: "<<lost<<"\n";
      check(lost==0,"a concurrent simulation write was undone");
    }
    // Locked (tick frame): the call alone, nothing put back.
    Memory plain; plain.StoreWord(cursor+kNativeCursorFade,3);
    check(!NativeRenderStepOncePerTick(plain,true,cursor+kNativeCursorFade,1u,std::array<uint32_t,0>{},[&] { cursor_fade(plain); }) &&
      plain.Word(cursor+kNativeCursorFade)==4,"a tick frame steps");
  }
  // Routing: full frame only on the native side with bridge and host; guest
  // side frames take today's path for frame-by-frame A/B comparison.
  {
    using R=NativeFrameRoute;
    check(SelectNativeFrameRoute(true,false,true,true,true)==R::full_frame,"full frame");
    check(SelectNativeFrameRoute(true,true,true,true,true)==R::full_frame,"full frame over frame dispatch");
    check(SelectNativeFrameRoute(true,false,true,true,false)==R::guest_helper,"A/B guest side: helper");
    check(SelectNativeFrameRoute(true,true,true,true,false)==R::frame_dispatch,"A/B guest side: today's frame dispatch");
    check(SelectNativeFrameRoute(true,false,false,true,true)==R::guest_helper,"full frame needs the bridge");
    check(SelectNativeFrameRoute(true,false,true,false,true)==R::guest_helper,"full frame needs the host");
    check(SelectNativeFrameRoute(false,true,true,true,true)==R::frame_dispatch,"frame dispatch unchanged");
    check(SelectNativeFrameRoute(false,true,false,true,true)==R::guest_helper,"frame dispatch needs the bridge");
    check(SelectNativeFrameRoute(false,false,true,true,true)==R::guest_helper,"off");
    // The hook's side comes from the latched A/B decision for the output frame.
    for(uint64_t frame=0;frame<8;++frame) {
      NativeAbSideLatch latch(AbSide(frame,0,2));
      check((SelectNativeFrameRoute(true,false,true,true,NativeAbNativeSide())==R::full_frame)==((frame/2)%2==1),"A/B alternation");
    }
    static_assert(SelectNativeFrameRoute(true,false,true,true,true)==NativeFrameRoute::full_frame);
  }
  // Output identity for the HUD phase loop: the bridge's active output alone
  // (what the native post sets) is refused until the guest device's bound
  // color surface is the output's owner+112 (what 8219C930 binds).
  {
    constexpr uint32_t renderer=0x40001cd0,output=0x40a00000,scene_surface=0x40b00000;
    const NativeOutputBinding bound{renderer,0,0,output,output};
    check(NativeOutputBound(renderer,bound),"output bound on bridge and device");
    auto native_post_only=bound; native_post_only.device_surface=scene_surface;
    check(!NativeOutputBound(renderer,native_post_only),"device still on the scene surface is refused");
    auto no_surface=bound; no_surface.device_surface=0;
    check(!NativeOutputBound(renderer,no_surface),"no bound device surface is refused");
    auto no_output=bound; no_output.output_surface=0; no_output.device_surface=0;
    check(!NativeOutputBound(renderer,no_output),"no output surface is refused");
    auto target=bound; target.active_target=0x40c00000;
    check(!NativeOutputBound(renderer,target),"an open target is not the output");
    auto scene=bound; scene.active_scene=renderer;
    check(!NativeOutputBound(renderer,scene),"an open scene is not the output");
    auto other=bound; other.active_output=0x40001d00;
    check(!NativeOutputBound(renderer,other),"another owner's output");
    check(!NativeOutputBound(0,NativeOutputBinding{}),"no renderer");
    static_assert(NativeOutputBound(1,{1,0,0,2,2}) && !NativeOutputBound(1,{1,0,0,2,3}));
  }
  if(failures) std::cerr<<failures<<" native full frame checks failed\n";
  return failures?1:0;
}
