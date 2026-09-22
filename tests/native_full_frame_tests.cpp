#include "native_graphics/native_full_frame.h"
#include "native_graphics/native_ab_alternate.h"
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace edf::native;
// Records every host call in order; views and view acceptance are scripted.
class TraceHost final : public NativeFrameHost {
 public:
  std::vector<std::string> trace;
  std::vector<uint32_t> views;
  std::vector<bool> accept;
  uint32_t serial=40;
  bool output_ready=false;
  NativeFrameInputs AcquireInputs() override { trace.push_back("acquire"); return {nullptr,nullptr,nullptr,7}; }
  std::vector<uint32_t> Views() override { trace.push_back("views"); return views; }
  uint32_t AdvanceSerial() override { trace.push_back("serial"); return serial++; }
  bool BeginView(NativeFrameContext& context) override {
    trace.push_back("begin:"+std::to_string(context.view.index)+":"+std::to_string(context.view.scene)+":"+std::to_string(context.view.serial));
    return context.view.index<accept.size()?bool(accept[context.view.index]):true;
  }
  void RunPass(size_t index,NativeFramePass& pass,NativeFrameContext& context) override {
    trace.push_back(std::to_string(index)+":"+pass.name());
    NativeFrameHost::RunPass(index,pass,context);
  }
  void EndView(NativeFrameContext&) override { trace.push_back("end_view"); }
  void SideEffects(const NativeFrameInputs& inputs) override { trace.push_back("side_effects:"+std::to_string(inputs.motion_publication)); }
  void EndScene(const NativeFrameInputs&,bool ready) override { output_ready=ready; trace.push_back("end_scene"); }
  void Unimplemented(const char* pass) override { trace.push_back(std::string("stub:")+pass); }
};
class OutputPass final : public NativeFramePass {
 public:
  const char* name() override { return "output"; }
  void Record(NativeFrameContext& context) override { context.output_ready=context.view.index==1; }
};
}

int main() {
  int failures=0;
  auto check=[&](bool ok,const char* what) { if(!ok) { ++failures; std::cerr<<"failed: "<<what<<'\n'; } };
  // Default passes: the documented order, every one a stub.
  {
    NativeFullFrame frame;
    const std::vector<std::string> names{"static_world","models","sky","effects","transparent","post"};
    check(frame.passes().size()==names.size(),"default pass count");
    for(size_t i=0;i<names.size() && i<frame.passes().size();++i) {
      check(frame.passes()[i]->name()==names[i],"default pass order");
      check(kNativeFramePassOrder[i]==names[i],"pass order constant");
    }
    TraceHost host; host.views={100,200};
    frame.Run(host);
    std::vector<std::string> expected{"acquire","views"};
    for(uint32_t view=0;view<2;++view) {
      expected.push_back("serial");
      expected.push_back("begin:"+std::to_string(view)+":"+std::to_string(100*(view+1))+":"+std::to_string(40+view));
      for(size_t i=0;i<names.size();++i) { expected.push_back(std::to_string(i)+":"+names[i]); expected.push_back("stub:"+names[i]); }
      expected.push_back("end_view");
    }
    expected.push_back("side_effects:7"); expected.push_back("end_scene");
    check(host.trace==expected,"default frame call order");
    check(host.serial==42,"serial advanced once per view");
    check(!host.output_ready && frame.frames()==1,"stub frame has no output");
  }
  // A rejected view still advances the serial, skips its passes and end_view.
  {
    std::vector<std::unique_ptr<NativeFramePass>> passes;
    passes.push_back(std::make_unique<OutputPass>());
    NativeFullFrame frame(std::move(passes));
    TraceHost host; host.views={1,2,3}; host.accept={false,true,true};
    frame.Run(host);
    const std::vector<std::string> expected{"acquire","views",
      "serial","begin:0:1:40",
      "serial","begin:1:2:41","0:output","end_view",
      "serial","begin:2:3:42","0:output","end_view",
      "side_effects:7","end_scene"};
    check(host.trace==expected,"rejected view order");
    check(host.output_ready,"output from any view reaches end_scene");
  }
  // No views: the frame still acquires, runs the side effects and ends.
  {
    NativeFullFrame frame;
    TraceHost host;
    frame.Run(host);
    check(host.trace==std::vector<std::string>{"acquire","views","side_effects:7","end_scene"},"empty view list");
    check(host.serial==40,"no serial without views");
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
      const bool native=AbSide(frame,0,2);
      NativeAbSideLatch latch(native);
      check((SelectNativeFrameRoute(true,false,true,true,NativeAbNativeSide())==R::full_frame)==((frame/2)%2==1),"A/B alternation");
    }
    static_assert(SelectNativeFrameRoute(true,false,true,true,true)==NativeFrameRoute::full_frame);
  }
  if(failures) std::cerr<<failures<<" native full frame checks failed\n";
  return failures?1:0;
}
