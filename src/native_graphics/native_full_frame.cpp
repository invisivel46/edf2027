#include "native_full_frame.h"

namespace edf::native {
namespace {
// Placeholder for a pass not yet implemented natively: it records nothing, so
// its content is missing from native frames (A/B captures show which).
class NativeFrameStubPass final : public NativeFramePass {
 public:
  explicit NativeFrameStubPass(const char* name):name_(name) {}
  const char* name() const override { return name_; }
  void Record(NativeFrameContext& context) override { context.host.Unimplemented(name_); }
 private:
  const char* name_;
};
// The finish stage: the host records the native post (RecordNativeFullFramePost)
// and binds its output on the guest device as 8219C930 does (the HUD's draws
// check that binding), falling back to the guest 820B0B80 only when the post
// reports an error. output_ready is NativeOutputBound.
class NativeFramePostPass final : public NativeFramePass {
 public:
  const char* name() const override { return "post"; }
  void Record(NativeFrameContext& context) override { context.output_ready=context.host.Finish(context); }
};
}
std::vector<std::unique_ptr<NativeFramePass>> MakeNativeFramePasses() {
  std::vector<std::unique_ptr<NativeFramePass>> passes;
  for(size_t i=0;i<kNativeFrameViewPassCount;++i)
    passes.push_back(std::make_unique<NativeFrameStubPass>(kNativeFramePassOrder[i].data()));  // Literals: NUL-terminated.
  return passes;
}
std::vector<std::unique_ptr<NativeFramePass>> MakeNativeFrameFinishPasses() {
  std::vector<std::unique_ptr<NativeFramePass>> passes;
  passes.push_back(std::make_unique<NativeFramePostPass>());
  return passes;
}
NativeFullFrame::NativeFullFrame():view_passes_(MakeNativeFramePasses()),frame_passes_(MakeNativeFrameFinishPasses()) {}
bool NativeFullFrame::Replace(std::unique_ptr<NativeFramePass> pass) {
  const std::string_view name=pass->name();
  for(auto* list:{&view_passes_,&frame_passes_}) for(auto& slot:*list)
    if(name==slot->name()) { slot=std::move(pass); return true; }
  return false;
}
void NativeFullFrame::Run(NativeFrameHost& host) {
  ++frames_;
  const auto inputs=host.AcquireInputs();
  const auto views=host.Views();
  for(uint32_t index=0;index<views.size();++index) {
    NativeFrameContext context{host,inputs,{views[index],index,0}};
    context.view.serial=host.AdvanceSerial(views[index]);
    if(host.BeginView(context))
      for(size_t pass=0;pass<view_passes_.size();++pass) host.RunPass(pass,*view_passes_[pass],context);
    host.ViewOverlays(context);
    host.EndView(context);
  }
  host.SideEffects(inputs);
  NativeFrameContext frame{host,inputs,{}};
  for(size_t pass=0;pass<frame_passes_.size();++pass) host.RunPass(view_passes_.size()+pass,*frame_passes_[pass],frame);
  host.Phases(frame);
  host.EndScene(inputs,frame.output_ready);
}
}
