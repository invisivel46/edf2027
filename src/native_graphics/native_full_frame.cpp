#include "native_full_frame.h"

namespace edf::native {
namespace {
// Placeholder for a pass not yet implemented natively: it records nothing, so
// its content is missing from native frames (A/B captures show which).
class NativeFrameStubPass final : public NativeFramePass {
 public:
  explicit NativeFrameStubPass(const char* name):name_(name) {}
  const char* name() override { return name_; }
  void Record(NativeFrameContext& context) override { context.host.Unimplemented(name_); }
 private:
  const char* name_;
};
// The native post finish plan (edf_native_post_finish) still issues its passes
// through the guest setters 821BCD58/821BCE98/821BCF28, activation 821B94E8 and
// draw 821A79B8, so it cannot run inside a native frame: a stub until the post
// chain owns its parameter records, targets and draws. Without it the frame
// leaves output_ready false and 8219C840 publishes the scene directly.
class NativeFramePostPass final : public NativeFramePass {
 public:
  const char* name() override { return "post"; }
  void Record(NativeFrameContext& context) override { context.host.Unimplemented("post"); }
};
}
std::vector<std::unique_ptr<NativeFramePass>> MakeNativeFramePasses() {
  std::vector<std::unique_ptr<NativeFramePass>> passes;
  for(const auto name:kNativeFramePassOrder) {
    if(name=="post") passes.push_back(std::make_unique<NativeFramePostPass>());
    else passes.push_back(std::make_unique<NativeFrameStubPass>(name.data()));  // Literals: NUL-terminated.
  }
  return passes;
}
NativeFullFrame::NativeFullFrame():passes_(MakeNativeFramePasses()) {}
void NativeFullFrame::Run(NativeFrameHost& host) {
  ++frames_;
  const auto inputs=host.AcquireInputs();
  const auto views=host.Views();
  bool output_ready=false;
  for(uint32_t index=0;index<views.size();++index) {
    NativeFrameContext context{host,inputs,{views[index],index,0}};
    context.view.serial=host.AdvanceSerial();
    if(!host.BeginView(context)) continue;
    for(size_t pass=0;pass<passes_.size();++pass) host.RunPass(pass,*passes_[pass],context);
    output_ready|=context.output_ready;
    host.EndView(context);
  }
  host.SideEffects(inputs);
  host.EndScene(inputs,output_ready);
}
}
