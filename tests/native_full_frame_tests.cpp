#include "native_graphics/native_full_frame.h"
#include "native_graphics/native_ab_alternate.h"
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
